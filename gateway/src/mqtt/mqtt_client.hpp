#ifndef EDGELINK_MQTT_CLIENT_HPP
#define EDGELINK_MQTT_CLIENT_HPP
#include <functional>
#include <string>

enum class MqttConnectionState { Connecting, Subscribing, Ready, Disconnected, Stopped };
const char *connectionStateName(MqttConnectionState state);
struct MqttCallbacks {
    std::function<void(const std::string &topic, const std::string &payload, bool retained)> on_message;
    std::function<void(MqttConnectionState, const std::string &detail)> on_connection;
    std::function<void()> on_tick;
};
// 阻塞运行；所有回调在调用线程执行，应快速返回。
// 两个 SUBACK 成功后报告 Ready。断线、连接或订阅失败后重试。
int runMqttClient(const std::string &host, int port, const MqttCallbacks &callbacks,
                  const std::function<bool()> &stop);
#endif
