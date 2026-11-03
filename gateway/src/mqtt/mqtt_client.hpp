#ifndef EDGELINK_MQTT_CLIENT_HPP
#define EDGELINK_MQTT_CLIENT_HPP

#include <functional>
#include <string>

// 回调在 run() 所在线程执行；载荷按长度复制，不要求以 NUL 结尾。
struct MqttMessage {
    std::string device_id;
    std::string kind;
    std::string payload;
};

using MqttMessageHandler = std::function<void(const MqttMessage &)>;
using MqttStopCheck = std::function<bool()>;

// 断线后按固定间隔重连并重新订阅，直到 stop 返回 true。
int runMqttClient(const std::string &host, int port,
                  const MqttMessageHandler &onMessage, const MqttStopCheck &stop);

#endif
