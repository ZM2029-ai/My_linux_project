#ifndef EDGELINK_DEVICE_REGISTRY_HPP
#define EDGELINK_DEVICE_REGISTRY_HPP
#include "device_message.hpp"
#include <chrono>
#include <map>
#include <vector>

struct DeviceSnapshot {
    std::string device_id;
    DevicePresence presence = DevicePresence::Unknown;
    bool has_telemetry = false;
    double temperature_c = 0, humidity_percent = 0;
    std::string telemetry_timestamp, status_timestamp;
};

// 仅在事件循环线程内访问，snapshot() 返回供上层使用的内存快照。
class DeviceRegistry {
public:
    using Clock = std::chrono::steady_clock;
    explicit DeviceRegistry(std::size_t capacity = 256) : capacity_(capacity) {}
    bool apply(const DeviceMessage &message, Clock::time_point now = Clock::now());
    std::vector<std::string> markUnknown();
    std::vector<std::string> expire(Clock::time_point now = Clock::now());
    std::vector<DeviceSnapshot> snapshot() const;
    const DeviceSnapshot *find(const std::string &deviceId) const;
private:
    struct Entry { DeviceSnapshot data; Clock::time_point last_live; bool live_seen = false; };
    std::size_t capacity_;
    std::map<std::string, Entry> devices_;
};
#endif
