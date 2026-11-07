#include "device_store.hpp"
#include <sqlite3.h>
#include <cmath>
#include <ctime>
#include <stdexcept>

#if SQLITE_VERSION_NUMBER < 3024000
#error "EdgeLink storage requires SQLite 3.24 or newer for ON CONFLICT"
#endif

namespace {
constexpr int schemaVersion = 1;
constexpr char schema[] = R"sql(
CREATE TABLE devices (
    device_id TEXT PRIMARY KEY NOT NULL,
    display_name TEXT NOT NULL,
    registered_at TEXT NOT NULL,
    last_received_at TEXT NOT NULL,
    presence TEXT NOT NULL CHECK (presence IN ('unknown', 'online', 'offline'))
);
CREATE TABLE telemetry (
    id INTEGER PRIMARY KEY,
    device_id TEXT NOT NULL REFERENCES devices(device_id),
    sampled_at TEXT NOT NULL,
    received_at TEXT NOT NULL,
    temperature_c REAL NOT NULL CHECK (temperature_c BETWEEN -100 AND 200),
    humidity_percent REAL NOT NULL CHECK (humidity_percent BETWEEN 0 AND 100)
);
CREATE INDEX telemetry_device_time ON telemetry (device_id, sampled_at, id);
CREATE TABLE operation_log (
    id INTEGER PRIMARY KEY,
    device_id TEXT NOT NULL REFERENCES devices(device_id),
    occurred_at TEXT NOT NULL,
    operation TEXT NOT NULL,
    detail TEXT NOT NULL
);
PRAGMA user_version = 1;
)sql";

void requireSqlite(sqlite3 *db, int result, int expected, const char *operation)
{
    if (result != expected)
        throw std::runtime_error(std::string(operation) + " (SQLite " + std::to_string(result) +
                                 "): " + (db ? sqlite3_errmsg(db) : sqlite3_errstr(result)));
}

void execute(sqlite3 *db, const char *sql, const char *operation)
{
    requireSqlite(db, sqlite3_exec(db, sql, nullptr, nullptr, nullptr), SQLITE_OK, operation);
}

class Statement {
public:
    Statement(sqlite3 *db, const char *sql, const char *operation)
        : db_(db), operation_(operation)
    {
        sqlite3_stmt *statement = nullptr;
        const int result = sqlite3_prepare_v2(db, sql, -1, &statement, nullptr);
        statement_.reset(statement);
        requireSqlite(db, result, SQLITE_OK, operation);
    }

    void bind(int index, const std::string &value)
    {
        check(sqlite3_bind_text(statement_.get(), index, value.c_str(), -1, SQLITE_TRANSIENT), SQLITE_OK);
    }
    void bind(int index, int value) { check(sqlite3_bind_int(statement_.get(), index, value), SQLITE_OK); }
    void bind(int index, double value) { check(sqlite3_bind_double(statement_.get(), index, value), SQLITE_OK); }
    void write() { check(sqlite3_step(statement_.get()), SQLITE_DONE); }
    bool next()
    {
        const int result = sqlite3_step(statement_.get());
        if (result == SQLITE_DONE) return false;
        check(result, SQLITE_ROW);
        return true;
    }
    std::string text(int column) const
    {
        const auto *value = sqlite3_column_text(statement_.get(), column);
        return value ? reinterpret_cast<const char *>(value) : "";
    }
    std::int64_t integer(int column) const { return sqlite3_column_int64(statement_.get(), column); }
    double real(int column) const { return sqlite3_column_double(statement_.get(), column); }

private:
    void check(int result, int expected) const { requireSqlite(db_, result, expected, operation_); }
    sqlite3 *db_;
    const char *operation_;
    std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> statement_{nullptr, sqlite3_finalize};
};

class Transaction {
public:
    explicit Transaction(sqlite3 *db) : db_(db) { execute(db_, "BEGIN IMMEDIATE", "begin transaction"); }
    ~Transaction() { if (!committed_) sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr); }
    Transaction(const Transaction &) = delete;
    Transaction &operator=(const Transaction &) = delete;

    void commit()
    {
        execute(db_, "COMMIT", "commit transaction");
        committed_ = true;
    }

private:
    sqlite3 *db_;
    bool committed_ = false;
};

int readSchemaVersion(sqlite3 *db)
{
    Statement query(db, "PRAGMA user_version", "read schema version");
    if (!query.next()) throw std::runtime_error("cannot read schema version");
    return static_cast<int>(query.integer(0));
}

void validateDeviceId(const std::string &id)
{
    if (!validDeviceId(id)) throw std::invalid_argument("invalid device ID");
}

void validateTimestamp(const std::string &timestamp)
{
    if (!validTimestamp(timestamp)) throw std::invalid_argument("invalid Beijing timestamp");
}

void logOperation(sqlite3 *db, const std::string &deviceId, const std::string &time,
                  const std::string &operation, const std::string &detail)
{
    Statement log(db, "INSERT INTO operation_log (device_id, occurred_at, operation, detail) VALUES (?, ?, ?, ?)",
                  "log device operation");
    log.bind(1, deviceId);
    log.bind(2, time);
    log.bind(3, operation);
    log.bind(4, detail);
    log.write();
}

// 只在持久化状态变化时记录事件，重复心跳不会增加操作记录。
void updatePresence(sqlite3 *db, const std::string &deviceId, DevicePresence presence, const std::string &time)
{
    const std::string name = presenceName(presence);
    Statement update(db, "UPDATE devices SET presence = ?1 WHERE device_id = ?2 AND presence <> ?1",
                     "update device presence");
    update.bind(1, name);
    update.bind(2, deviceId);
    update.write();
    if (sqlite3_changes(db) != 0) logOperation(db, deviceId, time, "presence", name);
}

StoredDevice readDevice(const Statement &query)
{
    return {query.text(0), query.text(1), query.text(2), query.text(3), query.text(4)};
}
}

void DeviceStore::CloseDatabase::operator()(sqlite3 *db) const { sqlite3_close(db); }

DeviceStore::DeviceStore(const std::string &path, OpenMode mode)
{
    if (path.empty() || path.find('\0') != std::string::npos) throw std::invalid_argument("invalid database path");
    int flags = mode == OpenMode::ReadOnly ? SQLITE_OPEN_READONLY : SQLITE_OPEN_READWRITE;
    if (mode == OpenMode::Create) flags |= SQLITE_OPEN_CREATE;
    sqlite3 *db = nullptr;
    const int result = sqlite3_open_v2(path.c_str(), &db, flags, nullptr);
    db_.reset(db);
    requireSqlite(db, result, SQLITE_OK, "open database");
    requireSqlite(db, sqlite3_busy_timeout(db, 100), SQLITE_OK, "set database lock timeout");
    execute(db, "PRAGMA foreign_keys = ON", "enable foreign keys");

    int version = readSchemaVersion(db);
    if (version == 0 && mode == OpenMode::Create) {
        Transaction transaction(db);
        version = readSchemaVersion(db);
        if (version == 0) {
            execute(db, schema, "initialize database schema");
            version = readSchemaVersion(db);
        }
        transaction.commit();
    }
    if (version != schemaVersion) throw std::runtime_error("unsupported database schema version: " + std::to_string(version));
}

void DeviceStore::record(const std::string &deviceId, DevicePresence presence, const std::string &receivedAt,
                         const TelemetrySample *sample)
{
    validateDeviceId(deviceId);
    validateTimestamp(receivedAt);
    if (sample) {
        validateTimestamp(sample->sampled_at);
        if (!std::isfinite(sample->temperature_c) || !std::isfinite(sample->humidity_percent))
            throw std::invalid_argument("non-finite telemetry");
    }
    auto *db = db_.get();
    Transaction transaction(db);
    Statement add(db, R"sql(
        INSERT INTO devices (device_id, display_name, registered_at, last_received_at, presence)
        VALUES (?1, ?1, ?2, ?2, 'unknown')
        ON CONFLICT(device_id) DO NOTHING
    )sql", "register device");
    add.bind(1, deviceId);
    add.bind(2, receivedAt);
    add.write();
    if (sqlite3_changes(db) != 0) {
        logOperation(db, deviceId, receivedAt, "register", deviceId);
    } else {
        Statement touch(db, "UPDATE devices SET last_received_at = ? WHERE device_id = ?", "update last received time");
        touch.bind(1, receivedAt);
        touch.bind(2, deviceId);
        touch.write();
    }
    updatePresence(db, deviceId, presence, receivedAt);

    if (sample) {
        Statement insert(db, R"sql(
            INSERT INTO telemetry (device_id, sampled_at, received_at, temperature_c, humidity_percent)
            VALUES (?, ?, ?, ?, ?)
        )sql", "insert telemetry");
        insert.bind(1, deviceId);
        insert.bind(2, sample->sampled_at);
        insert.bind(3, receivedAt);
        insert.bind(4, sample->temperature_c);
        insert.bind(5, sample->humidity_percent);
        insert.write();
    }
    transaction.commit();
}

// 重启后没有任何设备状态属于当前网关会话。
void DeviceStore::resetPresence(const std::string &changedAt)
{
    validateTimestamp(changedAt);
    auto *db = db_.get();
    Transaction transaction(db);
    Statement log(db, R"sql(
        INSERT INTO operation_log (device_id, occurred_at, operation, detail)
        SELECT device_id, ?, 'presence', 'unknown' FROM devices WHERE presence <> 'unknown'
    )sql", "log reset device presence");
    log.bind(1, changedAt);
    log.write();
    execute(db, "UPDATE devices SET presence = 'unknown' WHERE presence <> 'unknown'", "reset device presence");
    transaction.commit();
}

void DeviceStore::updateStatuses(const std::map<std::string, DevicePresence> &statuses, const std::string &changedAt)
{
    if (statuses.empty()) return;
    validateTimestamp(changedAt);
    auto *db = db_.get();
    Transaction transaction(db);
    for (const auto &entry : statuses) {
        validateDeviceId(entry.first);
        updatePresence(db, entry.first, entry.second, changedAt);
    }
    transaction.commit();
}

bool DeviceStore::rename(const std::string &deviceId, const std::string &name, const std::string &changedAt)
{
    validateDeviceId(deviceId);
    validateTimestamp(changedAt);
    if (name.empty() || name.size() > 128 || name.find_first_of("\t\r\n") != std::string::npos ||
        name.find('\0') != std::string::npos) throw std::invalid_argument("invalid display name (1-128 bytes)");
    auto *db = db_.get();
    Transaction transaction(db);
    Statement update(db, "UPDATE devices SET display_name = ?1 WHERE device_id = ?2 AND display_name <> ?1",
                     "rename device");
    update.bind(1, name);
    update.bind(2, deviceId);
    update.write();
    if (sqlite3_changes(db) != 0) {
        logOperation(db, deviceId, changedAt, "rename", name);
    } else {
        StoredDevice device;
        if (!find(deviceId, device)) return false;
    }
    transaction.commit();
    return true;
}

bool DeviceStore::find(const std::string &deviceId, StoredDevice &result) const
{
    validateDeviceId(deviceId);
    Statement query(db_.get(), R"sql(
        SELECT device_id, display_name, registered_at, last_received_at, presence
        FROM devices WHERE device_id = ?
    )sql", "find device");
    query.bind(1, deviceId);
    if (!query.next()) return false;
    result = readDevice(query);
    return true;
}

std::vector<StoredDevice> DeviceStore::list() const
{
    Statement query(db_.get(), R"sql(
        SELECT device_id, display_name, registered_at, last_received_at, presence
        FROM devices ORDER BY device_id
    )sql", "list devices");
    std::vector<StoredDevice> devices;
    while (query.next()) devices.push_back(readDevice(query));
    return devices;
}

std::vector<StoredTelemetry> DeviceStore::history(const std::string &deviceId, const std::string &start,
                                                  const std::string &end, int limit) const
{
    validateDeviceId(deviceId);
    validateTimestamp(start);
    validateTimestamp(end);
    if (start >= end || limit < 1 || limit > 1000) throw std::invalid_argument("invalid history interval or limit");
    Statement query(db_.get(), R"sql(
        SELECT id, sampled_at, received_at, temperature_c, humidity_percent
        FROM telemetry WHERE device_id = ? AND sampled_at >= ? AND sampled_at < ?
        ORDER BY sampled_at, id LIMIT ?
    )sql", "query telemetry history");
    query.bind(1, deviceId);
    query.bind(2, start);
    query.bind(3, end);
    query.bind(4, limit);
    std::vector<StoredTelemetry> samples;
    while (query.next())
        samples.push_back({query.integer(0), {query.text(1), query.real(3), query.real(4)}, query.text(2)});
    return samples;
}

std::string beijingTimestampNow()
{
    const std::time_t shifted = std::time(nullptr) + 8 * 3600;
    std::tm time{};
#ifdef _WIN32
    if (gmtime_s(&time, &shifted) != 0) throw std::runtime_error("cannot read gateway time");
#else
    if (!gmtime_r(&shifted, &time)) throw std::runtime_error("cannot read gateway time");
#endif
    char buffer[20]{};
    if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &time) != 19)
        throw std::runtime_error("cannot format gateway time");
    return buffer;
}
