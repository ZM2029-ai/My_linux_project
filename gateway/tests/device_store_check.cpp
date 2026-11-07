#include "storage/device_store.hpp"
#include <sqlite3.h>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <stdexcept>

namespace {
const std::string start = "2026-11-06 12:00:00";
const std::string end = "2026-11-06 12:00:10";
const std::string received = "2026-11-06 12:00:11";

void check(bool passed, const char *detail)
{
    if (!passed) throw std::runtime_error(detail);
}

// 用独立连接持有写锁，验证真实 SQLite 错误和后续恢复。
void checkWriteLock(const std::string &path, DeviceStore &store)
{
    sqlite3 *db = nullptr;
    const int result = sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READWRITE, nullptr);
    const std::unique_ptr<sqlite3, decltype(&sqlite3_close)> lock(db, sqlite3_close);
    check(result == SQLITE_OK, "open competing connection");
    check(sqlite3_exec(db, "BEGIN IMMEDIATE", nullptr, nullptr, nullptr) == SQLITE_OK, "lock database");
    bool blocked = false;
    try {
        store.record("locked", DevicePresence::Online, received);
    } catch (const std::runtime_error &error) {
        blocked = std::string(error.what()).find("begin transaction") != std::string::npos;
    }
    check(blocked, "lock must produce a contextual error");
    check(sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr) == SQLITE_OK, "release database lock");
    StoredDevice device;
    check(!store.find("locked", device), "locked write must not register device");
    store.record("locked", DevicePresence::Online, received);
    check(store.find("locked", device), "write must recover after lock release");
}

void runChecks(const std::string &path)
{
    {
        DeviceStore store(path);
        const TelemetrySample lower{start, 22, 50};
        const TelemetrySample tie{start, 23, 51};
        const TelemetrySample upper{end, 24, 52};
        store.record("sensor-01", DevicePresence::Online, received, &upper);
        check(store.rename("sensor-01", "Lab's sensor", received), "rename registered device");
        check(!store.rename("absent", "No device", received), "rename missing device");
        store.record("sensor-01", DevicePresence::Online, received, &lower);
        store.record("sensor-01", DevicePresence::Online, received, &tie);
        store.record("sensor-02", DevicePresence::Online, received, &lower);

        const TelemetrySample invalid{start, 22, 101};
        bool rejected = false;
        try {
            store.record("rollback", DevicePresence::Online, received, &invalid);
        } catch (const std::runtime_error &error) {
            rejected = std::string(error.what()).find("insert telemetry") != std::string::npos;
        }
        check(rejected, "invalid sample must fail during insertion");
        StoredDevice device;
        check(!store.find("rollback", device), "failed telemetry must roll back registration");

        checkWriteLock(path, store);
        store.updateStatuses({{"sensor-02", DevicePresence::Unknown}}, received);
        check(store.find("sensor-02", device) && device.presence == "unknown", "expire device status");
        store.resetPresence(received);
    }
    const DeviceStore reader(path, DeviceStore::OpenMode::ReadOnly);
    StoredDevice device;
    check(reader.find("sensor-01", device), "read registered device after reopening");
    check(device.display_name == "Lab's sensor" && device.presence == "unknown", "preserve name and reset status");
    check(reader.list().size() == 3, "list registered devices only");
    const auto history = reader.history("sensor-01", start, end);
    check(history.size() == 2 && history[0].sample.temperature_c == 22 &&
          history[1].sample.temperature_c == 23 && history[0].id < history[1].id &&
          history[0].received_at == received, "filter by device and half-open range, with stable tie order");
    check(reader.history("sensor-01", start, end, 1).size() == 1, "history limit");
}
}

int main()
{
    const auto stamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    const std::string path = "device-store-check-" + std::to_string(stamp) + ".sqlite3";
    int result = 0;
    try {
        runChecks(path);
        std::cout << "device_store_check passed\n";
    } catch (const std::exception &error) {
        std::cerr << "device_store_check failed: " << error.what() << '\n';
        result = 1;
    }
    std::remove(path.c_str());
    return result;
}
