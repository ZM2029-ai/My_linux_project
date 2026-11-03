#include "mqtt_client.hpp"

#include <mosquitto.h>

#include <chrono>
#include <iostream>
#include <memory>
#include <thread>

namespace {
constexpr const char *kPrefix = "edgelink/device/";
constexpr int kMaxPayload = 65536;

struct Context {
    const MqttMessageHandler &handler;
};

bool validDeviceId(const std::string &id)
{
    if (id.empty() || id.size() > 64) return false;
    for (char c : id) {
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
    }
    return true;
}

void onConnect(struct mosquitto *client, void *, int result)
{
    if (result != 0) {
        std::cerr << "[mqtt] Broker refused connection: " << result << std::endl;
        return;
    }
    std::cout << "[mqtt] Connected; subscribing to device messages" << std::endl;
    const char *topics[] = {"edgelink/device/+/telemetry", "edgelink/device/+/status"};
    const int qos[] = {0, 1};
    for (int i = 0; i < 2; ++i) {
        const int rc = mosquitto_subscribe(client, nullptr, topics[i], qos[i]);
        if (rc != MOSQ_ERR_SUCCESS)
            std::cerr << "[mqtt] Subscribe failed: " << mosquitto_strerror(rc) << std::endl;
    }
}

void onSubscribe(struct mosquitto *, void *, int mid, int count, const int *granted)
{
    for (int i = 0; i < count; ++i) {
        if (granted[i] == 0x80)
            std::cerr << "[mqtt] Broker rejected subscription " << mid << std::endl;
    }
}

void handleMessage(struct mosquitto *, void *userdata, const struct mosquitto_message *message)
{
    if (!message || !message->topic || message->payloadlen < 0 ||
        message->payloadlen > kMaxPayload || (message->payloadlen && !message->payload)) return;
    const std::string topic(message->topic);
    const std::string prefix(kPrefix);
    if (topic.compare(0, prefix.size(), prefix) != 0) return;
    const std::string rest = topic.substr(prefix.size());
    const auto slash = rest.find('/');
    if (slash == std::string::npos) return;
    MqttMessage parsed{rest.substr(0, slash), rest.substr(slash + 1), {}};
    if (!validDeviceId(parsed.device_id) ||
        (parsed.kind != "status" && parsed.kind != "telemetry")) return;
    if (message->payloadlen)
        parsed.payload.assign(static_cast<const char *>(message->payload), message->payloadlen);
    static_cast<Context *>(userdata)->handler(parsed);
}
} // namespace

int runMqttClient(const std::string &host, int port,
                  const MqttMessageHandler &onMessage, const MqttStopCheck &stop)
{
    if (!onMessage || !stop || host.empty() || port < 1 || port > 65535) return 1;
    int rc = mosquitto_lib_init();
    if (rc != MOSQ_ERR_SUCCESS) {
        std::cerr << "[mqtt] Initialization failed: " << mosquitto_strerror(rc) << std::endl;
        return 1;
    }
    Context context{onMessage};
    using ClientPtr = std::unique_ptr<mosquitto, decltype(&mosquitto_destroy)>;
    ClientPtr client(mosquitto_new(nullptr, true, &context), &mosquitto_destroy);
    if (!client) {
        std::cerr << "[mqtt] Cannot allocate client" << std::endl;
        mosquitto_lib_cleanup();
        return 1;
    }
    mosquitto_connect_callback_set(client.get(), onConnect);
    mosquitto_subscribe_callback_set(client.get(), onSubscribe);
    mosquitto_message_callback_set(client.get(), handleMessage);

    while (!stop()) {
        rc = mosquitto_connect(client.get(), host.c_str(), port, 30);
        if (rc == MOSQ_ERR_SUCCESS) {
            while (!stop() && (rc = mosquitto_loop(client.get(), 200, 1)) == MOSQ_ERR_SUCCESS) {}
            if (stop()) break;
        }
        std::cerr << "[mqtt] Connection lost or unavailable: " << mosquitto_strerror(rc)
                  << "; retrying in 3 seconds" << std::endl;
        for (int i = 0; i < 15 && !stop(); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    mosquitto_disconnect(client.get());
    client.reset();
    mosquitto_lib_cleanup();
    return 0;
}
