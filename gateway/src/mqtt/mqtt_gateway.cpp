#include "mqtt_gateway.hpp"
#include "mqtt_client.hpp"
#include "device_registry.hpp"
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <iostream>
#ifdef EDGELINK_ENABLE_SQLITE
#include "storage/device_store.hpp"
#endif

namespace {
volatile std::sig_atomic_t stopRequested = 0;
void requestStop(int) { stopRequested = 1; }
}

int runMqttGatewayCommand(int argc, char **argv)
{
    std::string databasePath;
    if (argc >= 3 && std::string(argv[argc - 2]) == "--db") {
        databasePath = argv[argc - 1];
        if (databasePath.empty()) {
            std::cerr << "Missing path for --db\n";
            return 1;
        }
        argc -= 2;
    }
    if (argc < 1 || argc > 2 || std::string(argv[0]) == "--db") {
        std::cerr << "Expected MQTT arguments: HOST [PORT] [--db PATH]\n";
        return 1;
    }
    return runMqttGateway(argv[0], argc == 2 ? argv[1] : "1883", databasePath);
}

int runMqttGateway(const std::string &host, const std::string &portText, const std::string &databasePath)
{
    char *end = nullptr;
    errno = 0;
    const long port = std::strtol(portText.c_str(), &end, 10);
    if (host.empty() || errno || end == portText.c_str() || *end || port < 1 || port > 65535) {
        std::cerr << "Invalid MQTT host or port (1-65535)" << std::endl; return 1;
    }
#ifdef EDGELINK_ENABLE_SQLITE
    std::unique_ptr<DeviceStore> store;
    if (!databasePath.empty()) {
        try {
            store.reset(new DeviceStore(databasePath));
            store->resetPresence(beijingTimestampNow());
        } catch (const std::exception &error) {
            std::cerr << "[storage] initialize " << databasePath << ": " << error.what() << '\n';
            return 1;
        }
    }
    // 每台设备最多保留一项待同步状态。遥测写入失败会记录日志，继续处理后续消息。
    std::map<std::string, DevicePresence> pendingStatuses;
    auto nextStatusWrite = DeviceRegistry::Clock::time_point::min();
    auto flushStatuses = [&](bool stopping = false) {
        if (!store || pendingStatuses.empty()) return;
        const auto now = DeviceRegistry::Clock::now();
        if (!stopping && now < nextStatusWrite) return;
        try {
            store->updateStatuses(pendingStatuses, beijingTimestampNow());
            pendingStatuses.clear();
            nextStatusWrite = now;
        } catch (const std::exception &error) {
            std::cerr << "[storage] sync " << pendingStatuses.size() << " device states: " << error.what() << '\n';
            nextStatusWrite = now + std::chrono::seconds(1);
        }
    };
#else
    if (!databasePath.empty()) {
        std::cerr << "[storage] --db requires a build with EDGELINK_ENABLE_SQLITE=ON\n";
        return 1;
    }
#endif
    stopRequested = 0;
    const auto oldInt = std::signal(SIGINT, requestStop);
    const auto oldTerm = std::signal(SIGTERM, requestStop);
    DeviceRegistry devices;
    MqttCallbacks callbacks;
    callbacks.on_message = [&](const std::string &topic, const std::string &payload, bool retained) {
        DeviceMessage message;
        std::string error;
        if (!parseDeviceMessage(topic, payload, retained, message, error)) {
            std::cerr << "[mqtt] Rejected message: " << error << std::endl; return;
        }
        if (!devices.apply(message)) {
            std::cerr << "[mqtt] Device capacity reached (256)" << std::endl; return;
        }
        const auto presence = devices.find(message.device_id)->presence;
#ifdef EDGELINK_ENABLE_SQLITE
        if (store) {
            const TelemetrySample sample{message.timestamp, message.temperature_c, message.humidity_percent};
            try {
                store->record(message.device_id, presence, beijingTimestampNow(),
                              message.kind == MessageKind::Telemetry ? &sample : nullptr);
                pendingStatuses.erase(message.device_id);
            } catch (const std::exception &error) {
                pendingStatuses[message.device_id] = presence;
                std::cerr << "[storage] write device " << message.device_id << ": " << error.what() << '\n';
            }
        }
#endif
        std::cout << "[device] id=" << message.device_id << " time=" << message.timestamp;
        if (message.kind == MessageKind::Telemetry)
            std::cout << " temperature_c=" << message.temperature_c << " humidity_percent=" << message.humidity_percent;
        else
            std::cout << " reported=" << presenceName(message.presence) << " retained=" << message.retained;
        std::cout << " state=" << presenceName(presence) << std::endl;
    };
    auto reportUnknown = [&](const std::vector<std::string> &ids, const char *reason) {
        for (const auto &id : ids) {
            std::cout << "[device] id=" << id << " state=unknown (" << reason << ')' << std::endl;
#ifdef EDGELINK_ENABLE_SQLITE
            if (store) pendingStatuses[id] = DevicePresence::Unknown;
#endif
        }
    };
    callbacks.on_connection = [&](MqttConnectionState state, const std::string &detail) {
        std::cout << "[mqtt] " << connectionStateName(state) << ": " << detail << std::endl;
        if (state == MqttConnectionState::Disconnected || state == MqttConnectionState::Stopped) {
            reportUnknown(devices.markUnknown(), "gateway disconnected");
#ifdef EDGELINK_ENABLE_SQLITE
            flushStatuses(state == MqttConnectionState::Stopped);
#endif
        }
    };
    callbacks.on_tick = [&] {
        reportUnknown(devices.expire(), "no live message for 60 seconds");
#ifdef EDGELINK_ENABLE_SQLITE
        flushStatuses();
#endif
    };
    const int result = runMqttClient(host, static_cast<int>(port), callbacks, [] { return stopRequested != 0; });
    if (result != 0) std::cerr << "[mqtt] Gateway failed" << std::endl;
    if (oldInt != SIG_ERR) std::signal(SIGINT, oldInt);
    if (oldTerm != SIG_ERR) std::signal(SIGTERM, oldTerm);
    return result;
}
