#include "mqtt/device_message.hpp"
#include "mqtt/device_registry.hpp"
#include <iostream>
#include <stdexcept>

void require(bool condition, const char *message)
{
    if (!condition) throw std::runtime_error(message);
}

int main()
{
    try {
        DeviceMessage message;
        std::string error;
        const std::string status = R"({"timestamp":"2026-11-03 12:00:00","state":"online"})";
        const std::string telemetry = R"({"timestamp":"2026-11-03 12:00:01","temperature_c":25.5,"humidity_percent":60})";
        require(parseDeviceMessage("edgelink/device/a/status", status, true, message, error), "valid status rejected");
        DeviceRegistry registry(2);
        const auto now = DeviceRegistry::Clock::now();
        require(registry.apply(message, now), "status not stored");
        require(registry.snapshot()[0].presence == DevicePresence::Unknown, "retained online trusted as live");
        require(parseDeviceMessage("edgelink/device/a/telemetry", telemetry, false, message, error), "valid telemetry rejected");
        registry.apply(message, now);
        require(registry.snapshot()[0].presence == DevicePresence::Online, "live telemetry did not confirm online");
        require(registry.snapshot()[0].temperature_c == 25.5, "telemetry not stored");
        DeviceMessage stale;
        require(parseDeviceMessage("edgelink/device/a/status", R"({"timestamp":"2026-11-03 11:00:00","state":"offline"})", true, stale, error), "retained offline rejected");
        registry.apply(stale, now);
        require(registry.snapshot()[0].presence == DevicePresence::Online, "retained offline overwrote live evidence");
        require(registry.expire(now + std::chrono::seconds(59)).empty(), "expired too early");
        require(registry.expire(now + std::chrono::seconds(60)).size() == 1, "silent device did not become unknown");
        require(parseDeviceMessage("edgelink/device/a/status", status, false, message, error), "live status rejected");
        registry.apply(message, now);
        require(registry.markUnknown().size() == 1, "gateway loss did not invalidate state");
        const std::string offline = R"({"timestamp":"2026-11-03 12:00:00","state":"offline"})";
        require(parseDeviceMessage("edgelink/device/a/status", offline, true, message, error), "offline rejected");
        registry.apply(message, now);
        require(registry.snapshot()[0].presence == DevicePresence::Offline, "offline state not stored");
        message.device_id = "b"; require(registry.apply(message, now), "second device rejected");
        message.device_id = "c"; require(!registry.apply(message, now), "capacity not enforced");

        require(!parseDeviceMessage("edgelink/device/bad.id/status", status, false, message, error), "bad ID accepted");
        require(!parseDeviceMessage("edgelink/device/a/status/extra", status, false, message, error), "extra topic level accepted");
        require(!parseDeviceMessage("edgelink/device/a/status", "{broken", false, message, error), "malformed JSON accepted");
        require(!parseDeviceMessage("edgelink/device/a/status", status + "junk", false, message, error), "trailing garbage accepted");
        require(!parseDeviceMessage("edgelink/device/a/status", status + std::string(1, '\0'), false, message, error), "embedded NUL accepted");
        require(!parseDeviceMessage("edgelink/device/a/status", R"({"timestamp":"2026-02-30 12:00:00","state":"online"})", false, message, error), "invalid date accepted");
        require(!parseDeviceMessage("edgelink/device/a/telemetry", telemetry, true, message, error), "retained telemetry accepted");
        require(!parseDeviceMessage("edgelink/device/a/telemetry", R"({"timestamp":"2026-11-03 12:00:00","temperature_c":"25","humidity_percent":60})", false, message, error), "numeric string accepted");
        require(!parseDeviceMessage("edgelink/device/a/telemetry", R"({"timestamp":"2026-11-03 12:00:00","temperature_c":25,"humidity_percent":101})", false, message, error), "bad humidity accepted");
        std::cout << "MQTT parser and device state checks passed" << std::endl;
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << std::endl; return 1;
    }
}
