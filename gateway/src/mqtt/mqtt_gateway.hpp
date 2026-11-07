#ifndef EDGELINK_MQTT_GATEWAY_HPP
#define EDGELINK_MQTT_GATEWAY_HPP
#include <string>
// 统一负责命令行端口校验、信号退出以及 MQTT 与设备状态模块的组装。
int runMqttGateway(const std::string &host, const std::string &port = "1883",
                   const std::string &databasePath = {});
// 两个程序入口共用 HOST [PORT] [--db PATH] 参数解析。
int runMqttGatewayCommand(int argc, char **argv);
#endif
