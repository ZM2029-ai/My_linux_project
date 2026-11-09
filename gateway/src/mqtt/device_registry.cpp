#include "device_registry.hpp"

bool DeviceRegistry::apply(const DeviceMessage &message, Clock::time_point now)
{
    auto found = devices_.find(message.device_id);
    if (found == devices_.end()) {
        if (devices_.size() >= capacity_) {
            // Reclaim only a silent, non-online entry; active devices must not be
            // displaced by a stream of new device IDs.
            auto oldest = devices_.end();
            for (auto it = devices_.begin(); it != devices_.end(); ++it) {
                if (it->second.data.presence == DevicePresence::Online ||
                    now - it->second.last_seen < std::chrono::seconds(60)) continue;
                if (oldest == devices_.end() || it->second.last_seen < oldest->second.last_seen)
                    oldest = it;
            }
            if (oldest == devices_.end()) return false;
            devices_.erase(oldest);
        }
        found = devices_.emplace(message.device_id, Entry{}).first;
        found->second.data.device_id = message.device_id;
    }
    auto &entry = found->second;
    entry.last_seen = now;
    if (message.kind == MessageKind::Telemetry) {
        entry.data.has_telemetry = true;
        entry.data.temperature_c = message.temperature_c;
        entry.data.humidity_percent = message.humidity_percent;
        entry.data.telemetry_timestamp = message.timestamp;
        entry.data.presence = DevicePresence::Online;
        entry.last_live = now;
        entry.live_seen = true;
    } else {
        if (message.retained && entry.live_seen) return true;
        entry.data.status_timestamp = message.timestamp;
        // 保留的 online 仅为历史状态，等待实时心跳或遥测确认在线。
        if (!message.retained || (!entry.live_seen && message.presence == DevicePresence::Offline)) {
            entry.data.presence = message.presence;
            if (!message.retained) { entry.last_live = now; entry.live_seen = true; }
        }
    }
    return true;
}

std::vector<std::string> DeviceRegistry::markUnknown()
{
    std::vector<std::string> changed;
    for (auto &item : devices_) {
        item.second.live_seen = false;
        if (item.second.data.presence != DevicePresence::Unknown) {
            item.second.data.presence = DevicePresence::Unknown;
            changed.push_back(item.first);
        }
    }
    return changed;
}

std::vector<std::string> DeviceRegistry::expire(Clock::time_point now)
{
    std::vector<std::string> changed;
    for (auto &item : devices_) {
        auto &entry = item.second;
        if (entry.data.presence == DevicePresence::Online &&
            now - entry.last_live >= std::chrono::seconds(60)) {
            entry.data.presence = DevicePresence::Unknown;
            changed.push_back(item.first);
        }
    }
    return changed;
}

std::vector<DeviceSnapshot> DeviceRegistry::snapshot() const
{
    std::vector<DeviceSnapshot> result;
    for (const auto &item : devices_) result.push_back(item.second.data);
    return result;
}

const DeviceSnapshot *DeviceRegistry::find(const std::string &deviceId) const
{
    const auto entry = devices_.find(deviceId);
    return entry == devices_.end() ? nullptr : &entry->second.data;
}
