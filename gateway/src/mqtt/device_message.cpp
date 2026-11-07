#include "device_message.hpp"
#include <nlohmann/json.hpp>
#include <cmath>
#include <stdexcept>

bool parseDeviceMessage(const std::string &topic, const std::string &payload,
                        bool retained, DeviceMessage &message, std::string &error)
{
    error.clear();
    const std::string prefix = "edgelink/device/";
    if (topic.compare(0, prefix.size(), prefix) != 0) { error = "unexpected topic"; return false; }
    const auto slash = topic.find('/', prefix.size());
    if (slash == std::string::npos) { error = "missing message kind"; return false; }
    DeviceMessage parsed;
    parsed.device_id = topic.substr(prefix.size(), slash - prefix.size());
    if (!validDeviceId(parsed.device_id)) { error = "invalid device ID"; return false; }
    const std::string kind = topic.substr(slash + 1);
    if (kind != "telemetry" && kind != "status") { error = "unsupported message kind"; return false; }
    if (payload.empty() || payload.size() > 4096 || payload.find('\0') != std::string::npos) {
        error = "invalid payload length or embedded NUL"; return false;
    }
    try {
        const auto json = nlohmann::json::parse(payload, [](int depth, nlohmann::json::parse_event_t,
                                                           nlohmann::json &) {
            if (depth > 8) throw std::invalid_argument("JSON nesting too deep");
            return true;
        });
        if (!json.is_object() || !json.at("timestamp").is_string()) {
            error = "timestamp must be a string"; return false;
        }
        parsed.timestamp = json.at("timestamp").get<std::string>();
        if (!validTimestamp(parsed.timestamp)) { error = "invalid timestamp"; return false; }
        parsed.retained = retained;
        if (kind == "status") {
            parsed.kind = MessageKind::Status;
            if (!json.at("state").is_string()) { error = "state must be a string"; return false; }
            const auto state = json.at("state").get<std::string>();
            if (state != "online" && state != "offline") { error = "invalid state"; return false; }
            parsed.presence = state == "online" ? DevicePresence::Online : DevicePresence::Offline;
        } else {
            parsed.kind = MessageKind::Telemetry;
            if (retained) { error = "telemetry must not be retained"; return false; }
            if (!json.at("temperature_c").is_number() || !json.at("humidity_percent").is_number()) {
                error = "telemetry fields must be numbers"; return false;
            }
            parsed.temperature_c = json.at("temperature_c").get<double>();
            parsed.humidity_percent = json.at("humidity_percent").get<double>();
            if (!std::isfinite(parsed.temperature_c) || !std::isfinite(parsed.humidity_percent) ||
                parsed.temperature_c < -100 || parsed.temperature_c > 200 ||
                parsed.humidity_percent < 0 || parsed.humidity_percent > 100) {
                error = "telemetry out of range"; return false;
            }
        }
        message = parsed;
        return true;
    } catch (const std::exception &e) { error = e.what(); return false; }
}
