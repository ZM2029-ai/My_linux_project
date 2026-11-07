#ifndef EDGELINK_DEVICE_MESSAGE_HPP
#define EDGELINK_DEVICE_MESSAGE_HPP
#include "device_fields.hpp"

enum class MessageKind { Telemetry, Status };
struct DeviceMessage {
    std::string device_id, timestamp;
    MessageKind kind = MessageKind::Telemetry;
    DevicePresence presence = DevicePresence::Unknown;
    double temperature_c = 0, humidity_percent = 0;
    bool retained = false;
};
bool parseDeviceMessage(const std::string &topic, const std::string &payload,
                        bool retained, DeviceMessage &message, std::string &error);
#endif
