#ifndef EDGELINK_DEVICE_STORE_HPP
#define EDGELINK_DEVICE_STORE_HPP

#include "device_fields.hpp"
#include <cstdint>
#include <map>
#include <memory>
#include <vector>

struct sqlite3;

struct StoredDevice {
    std::string device_id, display_name, registered_at, last_received_at, presence;
};

struct TelemetrySample {
    std::string sampled_at;
    double temperature_c, humidity_percent;
};

struct StoredTelemetry {
    std::int64_t id;
    TelemetrySample sample;
    std::string received_at;
};

// 每个实例由创建它的线程使用。失败时抛出带操作上下文的异常。
class DeviceStore {
public:
    enum class OpenMode { Create, ReadOnly, ReadWrite };
    explicit DeviceStore(const std::string &path, OpenMode mode = OpenMode::Create);

    // 首次收到合法消息时自动注册。注册、状态和遥测在同一事务中提交。
    void record(const std::string &deviceId, DevicePresence presence, const std::string &receivedAt,
                const TelemetrySample *sample = nullptr);
    void resetPresence(const std::string &changedAt);
    void updateStatuses(const std::map<std::string, DevicePresence> &statuses, const std::string &changedAt);
    bool rename(const std::string &deviceId, const std::string &name, const std::string &changedAt);
    bool find(const std::string &deviceId, StoredDevice &result) const;
    std::vector<StoredDevice> list() const;
    // 返回 [start, end) 内的数据，按采样时间和记录 ID 升序排列。
    std::vector<StoredTelemetry> history(const std::string &deviceId, const std::string &start,
                                         const std::string &end, int limit = 100) const;

private:
    struct CloseDatabase { void operator()(sqlite3 *db) const; };
    std::unique_ptr<sqlite3, CloseDatabase> db_;
};

std::string beijingTimestampNow();

#endif
