#ifndef EDGELINK_CONTROL_SERVICE_HPP
#define EDGELINK_CONTROL_SERVICE_HPP

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <thread>

struct mosquitto;
struct mosquitto_message;

struct ControlCommand {
    std::string command_id;
    std::string device_id;
    std::string actor;
    std::string status;
    std::string message;
    bool enabled = false;
    std::chrono::steady_clock::time_point deadline;
    std::chrono::steady_clock::time_point completed_at;
};

enum class ControlSubmitResult { Accepted, NotReady, Busy, Capacity, AuditFailed, PublishFailed };

class ControlService {
public:
    ControlService(std::string database, std::string brokerHost, int brokerPort);
    ~ControlService();
    ControlService(const ControlService &) = delete;
    ControlService &operator=(const ControlService &) = delete;

    void start();
    void stop();
    bool ready() const;
    ControlSubmitResult submit(const std::string &deviceId, bool enabled, const std::string &actor,
                               ControlCommand &command);
    bool find(const std::string &deviceId, const std::string &commandId, ControlCommand &command) const;

private:
    static void onConnect(struct mosquitto *client, void *userdata, int result);
    static void onSubscribe(struct mosquitto *client, void *userdata, int mid, int count, const int *granted);
    static void onDisconnect(struct mosquitto *client, void *userdata, int result);
    static void onMessage(struct mosquitto *client, void *userdata, const struct mosquitto_message *message);

    void run();
    void acceptResponse(const struct mosquitto_message *message);
    void expireCommands();
    bool writeAudit(const ControlCommand &command, const std::string &event) const;
    void waitToRetry();

    const std::string database_;
    const std::string brokerHost_;
    const int brokerPort_;
    mutable std::mutex mutex_;
    std::condition_variable retryCondition_;
    std::map<std::string, ControlCommand> commands_;
    struct mosquitto *client_ = nullptr;
    int responseSubscriptionMid_ = -1;
    bool ready_ = false;
    std::atomic<bool> stopping_{false};
    std::thread thread_;
};

#endif
