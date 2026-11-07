// 替换 libmosquitto 的网络函数，检查本模块的失败恢复；不连接 Broker。
#include "mqtt/mqtt_client.hpp"
#include <mosquitto.h>
#include <cerrno>
#include <iostream>
#include <stdexcept>
#include <vector>

struct mosquitto {
    void *context;
    void (*connect_callback)(mosquitto *, void *, int) = nullptr;
    void (*subscribe_callback)(mosquitto *, void *, int, int, const int *) = nullptr;
    void (*message_callback)(mosquitto *, void *, const mosquitto_message *) = nullptr;
    std::vector<int> subscriptions;
    int step = 0;
};

namespace {
int attempts = 0, subscriptionCount = 0, readyCount = 0, messageCount = 0;
int cleanupCount = 0, destroyCount = 0, disconnectedCount = 0;
bool stopRequested = false, stopped = false;
void require(bool condition, const char *detail) {
    if (!condition) throw std::runtime_error(detail);
}
}

// 真实头文件的声明校验这些替身函数的签名。CMake 此目标不链接真实库。
int mosquitto_lib_init() { return MOSQ_ERR_SUCCESS; }
int mosquitto_lib_cleanup() { ++cleanupCount; return MOSQ_ERR_SUCCESS; }
mosquitto *mosquitto_new(const char *, bool, void *context) {
    auto *client = new mosquitto{}; client->context = context; return client;
}
void mosquitto_destroy(mosquitto *client) { ++destroyCount; delete client; }
const char *mosquitto_strerror(int) { return "simulated connection error"; }
int mosquitto_opts_set(mosquitto *, enum mosq_opt_t, void *) { return MOSQ_ERR_SUCCESS; }
void mosquitto_connect_callback_set(mosquitto *client, void (*callback)(mosquitto *, void *, int)) {
    client->connect_callback = callback;
}
void mosquitto_subscribe_callback_set(mosquitto *client, void (*callback)(mosquitto *, void *, int, int, const int *)) {
    client->subscribe_callback = callback;
}
void mosquitto_message_callback_set(mosquitto *client, void (*callback)(mosquitto *, void *, const mosquitto_message *)) {
    client->message_callback = callback;
}
int mosquitto_connect(mosquitto *client, const char *, int, int) {
    ++attempts; client->step = 0; client->subscriptions.clear();
    if (attempts == 1) { errno = ECONNREFUSED; return MOSQ_ERR_ERRNO; }
    return MOSQ_ERR_SUCCESS;
}
int mosquitto_disconnect(mosquitto *) { return MOSQ_ERR_SUCCESS; }
int mosquitto_subscribe(mosquitto *client, int *mid, const char *, int) {
    *mid = ++subscriptionCount; client->subscriptions.push_back(*mid); return MOSQ_ERR_SUCCESS;
}
int mosquitto_loop(mosquitto *client, int, int) {
    const int step = client->step++;
    if (step == 0) {
        // 第二次 TCP 连接成功，但被 Broker 拒绝；第三次订阅被拒绝。
        client->connect_callback(client, client->context, attempts == 2 ? 5 : 0);
    } else if (step == 1 || step == 2) {
        const int granted = attempts == 3 ? 0x80 : step - 1;
        client->subscribe_callback(client, client->context, client->subscriptions.at(step - 1), 1, &granted);
    } else if (step == 3) {
        char topic[] = "edgelink/device/check/status";
        char payload[] = {'a', '\0', 'b'};
        mosquitto_message message{};
        message.topic = topic; message.payload = payload; message.payloadlen = 3; message.retain = true;
        client->message_callback(client, client->context, &message);
    } else {
        return MOSQ_ERR_CONN_LOST;
    }
    return MOSQ_ERR_SUCCESS;
}

int main()
{
    try {
        MqttCallbacks callbacks;
        callbacks.on_connection = [](MqttConnectionState state, const std::string &) {
            if (state == MqttConnectionState::Ready) ++readyCount;
            if (state == MqttConnectionState::Disconnected) ++disconnectedCount;
            if (state == MqttConnectionState::Stopped) stopped = true;
        };
        callbacks.on_message = [](const std::string &topic, const std::string &payload, bool retained) {
            require(topic == "edgelink/device/check/status", "topic changed in transport");
            require(payload == std::string("a\0b", 3) && retained, "payload length or retain flag lost");
            ++messageCount;
            if (attempts == 5) stopRequested = true;
        };
        const int result = runMqttClient("localhost", 1883, callbacks, [] { return stopRequested; });
        require(result == 0 && attempts == 5, "connection retries failed");
        require(readyCount == 2 && disconnectedCount == 4, "ready reported before successful subscriptions");
        require(subscriptionCount == 6 && messageCount == 2, "resubscription or delivery failed");
        require(stopped && destroyCount == 1 && cleanupCount == 1, "cleanup failed");
        std::cout << "MQTT connection, rejected subscription and reconnection checks passed" << std::endl;
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << std::endl; return 1;
    }
}
