#include "mqtt_client.hpp"
#include <mosquitto.h>
#include <chrono>
#include <cerrno>
#include <cstring>
#include <memory>
#include <set>
#include <thread>

namespace {
struct Context {
    const MqttCallbacks &callbacks;
    std::set<int> pending;
    bool ready = false;
    std::string failure;
    void state(MqttConnectionState state, const std::string &detail) {
        if (callbacks.on_connection) callbacks.on_connection(state, detail);
    }
};
std::string errorText(int rc)
{
    return rc == MOSQ_ERR_ERRNO ? std::string(std::strerror(errno)) : mosquitto_strerror(rc);
}
void onConnect(struct mosquitto *client, void *userdata, int result)
{
    auto &context = *static_cast<Context *>(userdata);
    try {
        if (result != 0) {
            context.failure = "Broker rejected CONNECT: " + std::to_string(result);
            return;
        }
        context.state(MqttConnectionState::Subscribing, "connected; waiting for both SUBACKs");
        const char *topics[] = {"edgelink/device/+/telemetry", "edgelink/device/+/status"};
        for (int i = 0; i < 2; ++i) {
            int mid = 0;
            const int rc = mosquitto_subscribe(client, &mid, topics[i], i == 0 ? 0 : 1);
            if (rc != MOSQ_ERR_SUCCESS) { context.failure = errorText(rc); return; }
            context.pending.insert(mid);
        }
    } catch (const std::exception &e) { context.failure = e.what(); }
}
void onSubscribe(struct mosquitto *, void *userdata, int mid, int count, const int *granted)
{
    auto &context = *static_cast<Context *>(userdata);
    try {
        if (context.pending.erase(mid) == 0) return;
        if (count != 1 || !granted || granted[0] < 0 || granted[0] > 1) {
            context.failure = "Broker rejected a subscription"; return;
        }
        if (context.pending.empty() && context.failure.empty()) {
            context.ready = true;
            context.state(MqttConnectionState::Ready, "telemetry and status subscriptions active");
        }
    } catch (const std::exception &e) { context.failure = e.what(); }
}
void onMessage(struct mosquitto *, void *userdata, const struct mosquitto_message *message)
{
    auto &context = *static_cast<Context *>(userdata);
    if (!message || !message->topic || message->payloadlen <= 0 ||
        message->payloadlen > 4096 || !message->payload) return;
    try {
        if (context.callbacks.on_message)
            context.callbacks.on_message(message->topic,
                std::string(static_cast<const char *>(message->payload), message->payloadlen), message->retain);
    } catch (const std::exception &e) { context.failure = e.what(); }
}
}

const char *connectionStateName(MqttConnectionState state)
{
    switch (state) {
    case MqttConnectionState::Connecting: return "connecting";
    case MqttConnectionState::Subscribing: return "subscribing";
    case MqttConnectionState::Ready: return "ready";
    case MqttConnectionState::Disconnected: return "disconnected";
    default: return "stopped";
    }
}

int runMqttClient(const std::string &host, int port, const MqttCallbacks &callbacks,
                  const std::function<bool()> &stop)
{
    if (!callbacks.on_message || !stop || host.empty() || port < 1 || port > 65535) return 1;
    if (mosquitto_lib_init() != MOSQ_ERR_SUCCESS) return 1;
    struct LibraryCleanup { ~LibraryCleanup() { mosquitto_lib_cleanup(); } } cleanup;
    Context context{callbacks, {}, false, {}};
    using ClientPtr = std::unique_ptr<mosquitto, decltype(&mosquitto_destroy)>;
    ClientPtr client(mosquitto_new(nullptr, true, &context), &mosquitto_destroy);
    if (!client) return 1;
    int protocol = MQTT_PROTOCOL_V311;
    if (mosquitto_opts_set(client.get(), MOSQ_OPT_PROTOCOL_VERSION, &protocol) != MOSQ_ERR_SUCCESS) return 1;
    mosquitto_connect_callback_set(client.get(), onConnect);
    mosquitto_subscribe_callback_set(client.get(), onSubscribe);
    mosquitto_message_callback_set(client.get(), onMessage);
    try {
        while (!stop()) {
            context.pending.clear(); context.ready = false; context.failure.clear();
            context.state(MqttConnectionState::Connecting, host + ":" + std::to_string(port));
            // DNS/TCP 建连可能阻塞到系统超时；网络循环和重试等待可响应停止。
            int rc = mosquitto_connect(client.get(), host.c_str(), port, 30);
            if (rc != MOSQ_ERR_SUCCESS) context.failure = errorText(rc);
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            while (rc == MOSQ_ERR_SUCCESS && context.failure.empty() && !stop()) {
                rc = mosquitto_loop(client.get(), 200, 1);
                if (rc != MOSQ_ERR_SUCCESS) { context.failure = errorText(rc); break; }
                if (!context.ready && std::chrono::steady_clock::now() >= deadline)
                    context.failure = "CONNECT/SUBACK timeout";
                if (callbacks.on_tick) callbacks.on_tick();
            }
            if (stop()) break;
            mosquitto_disconnect(client.get());
            context.state(MqttConnectionState::Disconnected, context.failure + "; retry in 3 seconds");
            for (int i = 0; i < 15 && !stop(); ++i) {
                if (callbacks.on_tick) callbacks.on_tick();
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }
        }
        mosquitto_disconnect(client.get());
        context.state(MqttConnectionState::Stopped, "stopped by caller");
        return 0;
    } catch (const std::exception &e) {
        mosquitto_disconnect(client.get());
        try { context.state(MqttConnectionState::Stopped, e.what()); } catch (...) {}
        return 1;
    }
}
