#include "control_service.hpp"

#include "device_fields.hpp"
#include "storage/device_store.hpp"

#include <mosquitto.h>
#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <iostream>
#include <regex>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace {
using json = nlohmann::json;
const auto commandTimeout = std::chrono::seconds(5);
const auto terminalRetention = std::chrono::minutes(10);
constexpr std::size_t maximumActiveCommands = 16;
constexpr std::size_t maximumRetainedCommands = 4096;

std::string commandId()
{
    static std::atomic<std::uint64_t> nextId{0};
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    return "cmd-" + std::to_string(now) + "-" + std::to_string(nextId.fetch_add(1) + 1);
}

bool validCommandId(const std::string &value)
{
    static const std::regex pattern("[A-Za-z0-9_-]{1,64}");
    return std::regex_match(value, pattern);
}

std::string stateFor(const json &response)
{
    return response.at("success").get<bool>() ? "succeeded" : "failed";
}

std::string errorText(int result)
{
    return result == MOSQ_ERR_ERRNO ? "MQTT system error" : mosquitto_strerror(result);
}

}  // namespace

ControlService::ControlService(std::string database, std::string brokerHost, int brokerPort)
    : database_(std::move(database)), brokerHost_(std::move(brokerHost)), brokerPort_(brokerPort)
{
    if (database_.empty() || brokerHost_.empty() || brokerPort_ < 1 || brokerPort_ > 65535)
        throw std::invalid_argument("invalid control service configuration");
}

ControlService::~ControlService() { stop(); }

void ControlService::start()
{
    if (thread_.joinable()) throw std::logic_error("control service already started");
    stopping_.store(false);
    thread_ = std::thread(&ControlService::run, this);
}

void ControlService::stop()
{
    stopping_.store(true);
    retryCondition_.notify_all();
    if (thread_.joinable()) thread_.join();
}

bool ControlService::ready() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return ready_;
}

ControlSubmitResult ControlService::submit(const std::string &deviceId, bool enabled,
                                           const std::string &actor, ControlCommand &command)
{
    std::unique_lock<std::mutex> lock(mutex_);
    if (!ready_ || !client_) return ControlSubmitResult::NotReady;

    const auto now = std::chrono::steady_clock::now();
    for (auto it = commands_.begin(); it != commands_.end();) {
        const auto &item = it->second;
        if (item.status != "pending" && now - item.completed_at >= terminalRetention)
            it = commands_.erase(it);
        else
            ++it;
    }
    std::size_t active = 0;
    for (const auto &entry : commands_) {
        if (entry.second.status == "pending") {
            ++active;
            if (entry.second.device_id == deviceId) return ControlSubmitResult::Busy;
        }
    }
    if (active >= maximumActiveCommands || commands_.size() >= maximumRetainedCommands)
        return ControlSubmitResult::Capacity;

    ControlCommand pending;
    pending.command_id = commandId();
    pending.device_id = deviceId;
    pending.actor = actor;
    pending.enabled = enabled;
    pending.status = "pending";
    pending.message = "waiting for device response";
    if (!writeAudit(pending, "requested")) return ControlSubmitResult::AuditFailed;

    json body = {{"command_id", pending.command_id}, {"action", "set_reporting"},
                 {"enabled", enabled}, {"timestamp", beijingTimestampNow()}};
    const auto topic = "edgelink/device/" + deviceId + "/command";
    const auto encoded = body.dump();
    int mid = 0;
    const int result = mosquitto_publish(client_, &mid, topic.c_str(), static_cast<int>(encoded.size()),
                                         encoded.data(), 1, false);
    if (result != MOSQ_ERR_SUCCESS) {
        pending.status = "failed";
        pending.message = "command could not be queued";
        pending.completed_at = std::chrono::steady_clock::now();
        commands_.emplace(pending.command_id, pending);
        command = pending;
        lock.unlock();
        writeAudit(pending, "publish_failed");
        return result == MOSQ_ERR_NO_CONN ? ControlSubmitResult::NotReady : ControlSubmitResult::PublishFailed;
    }

    // Audit writes may block on SQLite locks; the device gets its full response
    // window only after the command has actually been queued for publication.
    pending.deadline = std::chrono::steady_clock::now() + commandTimeout;
    commands_.emplace(pending.command_id, pending);
    command = pending;
    return ControlSubmitResult::Accepted;
}

bool ControlService::find(const std::string &deviceId, const std::string &commandId,
                          ControlCommand &command) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = commands_.find(commandId);
    if (it == commands_.end() || it->second.device_id != deviceId) return false;
    if (it->second.status != "pending" &&
        std::chrono::steady_clock::now() - it->second.completed_at >= terminalRetention) return false;
    command = it->second;
    return true;
}

void ControlService::onConnect(struct mosquitto *client, void *userdata, int result)
{
    auto *service = static_cast<ControlService *>(userdata);
    {
        std::lock_guard<std::mutex> lock(service->mutex_);
        service->ready_ = false;
    }
    if (result != 0) {
        std::cerr << "[control] Broker rejected CONNECT: " << result << '\n';
        return;
    }
    int mid = 0;
    const int subscribeResult = mosquitto_subscribe(client, &mid, "edgelink/device/+/response", 1);
    if (subscribeResult != MOSQ_ERR_SUCCESS) {
        std::cerr << "[control] Response subscription failed: " << errorText(subscribeResult) << '\n';
        return;
    }
    std::lock_guard<std::mutex> lock(service->mutex_);
    service->responseSubscriptionMid_ = mid;
}

void ControlService::onSubscribe(struct mosquitto *, void *userdata, int mid, int count, const int *granted)
{
    auto *service = static_cast<ControlService *>(userdata);
    std::lock_guard<std::mutex> lock(service->mutex_);
    if (mid != service->responseSubscriptionMid_) return;
    service->responseSubscriptionMid_ = -1;
    service->ready_ = count == 1 && granted && granted[0] == 1;
    if (service->ready_)
        std::cout << "[control] Response subscription ready" << std::endl;
    else
        std::cerr << "[control] Broker rejected response subscription" << std::endl;
}

void ControlService::onDisconnect(struct mosquitto *, void *userdata, int result)
{
    auto *service = static_cast<ControlService *>(userdata);
    std::lock_guard<std::mutex> lock(service->mutex_);
    service->ready_ = false;
    service->responseSubscriptionMid_ = -1;
    if (result != 0) std::cerr << "[control] Disconnected from Broker: " << result << '\n';
}

void ControlService::onMessage(struct mosquitto *, void *userdata, const struct mosquitto_message *message)
{
    if (!message || !message->topic || !message->payload || message->payloadlen <= 0 ||
        message->payloadlen > 4096) return;
    static_cast<ControlService *>(userdata)->acceptResponse(message);
}

void ControlService::acceptResponse(const struct mosquitto_message *message)
{
    const std::string topic(message->topic);
    const std::string prefix = "edgelink/device/";
    if (topic.compare(0, prefix.size(), prefix) != 0 || message->retain) return;
    const auto slash = topic.find('/', prefix.size());
    if (slash == std::string::npos || topic.substr(slash + 1) != "response" || message->qos != 1) return;
    const std::string deviceId = topic.substr(prefix.size(), slash - prefix.size());
    if (!validDeviceId(deviceId)) return;

    try {
        const auto *begin = static_cast<const char *>(message->payload);
        const json response = json::parse(begin, begin + message->payloadlen);
        if (!response.is_object() || response.size() != 4) return;
        const auto idField = response.find("command_id");
        const auto successField = response.find("success");
        const auto messageField = response.find("message");
        const auto timestampField = response.find("timestamp");
        if (idField == response.end() || !idField->is_string() ||
            successField == response.end() || !successField->is_boolean() ||
            messageField == response.end() || !messageField->is_string() ||
            timestampField == response.end() || !timestampField->is_string()) return;

        const auto id = idField->get<std::string>();
        const auto timestamp = timestampField->get<std::string>();
        const auto messageText = messageField->get<std::string>();
        if (!validCommandId(id) || !validTimestamp(timestamp) || messageText.size() > 256) return;

        ControlCommand completed;
        std::string event;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = commands_.find(id);
            if (it == commands_.end() || it->second.device_id != deviceId || it->second.status != "pending") return;
            if (std::chrono::steady_clock::now() >= it->second.deadline) {
                it->second.status = "timed_out";
                it->second.message = "No device confirmation before timeout";
                event = "timed_out";
            } else {
                it->second.status = stateFor(response);
                it->second.message = messageText;
                event = it->second.status;
            }
            it->second.completed_at = std::chrono::steady_clock::now();
            completed = it->second;
        }
        if (!writeAudit(completed, event))
            std::cerr << "[control] Could not persist terminal audit for " << completed.command_id << '\n';
    } catch (const std::exception &error) {
        std::cerr << "[control] Ignored invalid response: " << error.what() << '\n';
    }
}

void ControlService::expireCommands()
{
    std::vector<ControlCommand> expired;
    const auto now = std::chrono::steady_clock::now();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto &entry : commands_) {
            auto &command = entry.second;
            if (command.status == "pending" && now >= command.deadline) {
                command.status = "timed_out";
                command.message = "No device confirmation before timeout";
                command.completed_at = now;
                expired.push_back(command);
            }
        }
    }
    for (const auto &command : expired) {
        if (!writeAudit(command, "timed_out"))
            std::cerr << "[control] Could not persist timeout audit for " << command.command_id << '\n';
    }
}

bool ControlService::writeAudit(const ControlCommand &command, const std::string &event) const
{
    const json details = {{"command_id", command.command_id}, {"actor", command.actor},
                          {"enabled", command.enabled}, {"status", event}, {"message", command.message}};
    for (int attempt = 0; attempt < 3; ++attempt) {
        try {
            DeviceStore store(database_, DeviceStore::OpenMode::ReadWrite);
            store.recordOperation(command.device_id, beijingTimestampNow(), "control", details.dump());
            return true;
        } catch (const std::exception &error) {
            std::cerr << "[control] Audit write failed (attempt " << (attempt + 1) << "): "
                      << error.what() << '\n';
            if (attempt < 2) std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }
    }
    return false;
}

void ControlService::waitToRetry()
{
    for (int attempt = 0; attempt < 15 && !stopping_.load(); ++attempt) {
        expireCommands();
        std::unique_lock<std::mutex> lock(mutex_);
        retryCondition_.wait_for(lock, std::chrono::milliseconds(200), [this] { return stopping_.load(); });
    }
}

void ControlService::run()
{
    if (mosquitto_lib_init() != MOSQ_ERR_SUCCESS) {
        std::cerr << "[control] Cannot initialize libmosquitto" << std::endl;
        return;
    }
    while (!stopping_.load()) {
        struct mosquitto *client = mosquitto_new(nullptr, true, this);
        if (!client) {
            std::cerr << "[control] Cannot allocate MQTT client" << std::endl;
            waitToRetry();
            continue;
        }
        int protocol = MQTT_PROTOCOL_V311;
        int result = mosquitto_threaded_set(client, true);
        if (result == MOSQ_ERR_SUCCESS)
            result = mosquitto_opts_set(client, MOSQ_OPT_PROTOCOL_VERSION, &protocol);
        if (result == MOSQ_ERR_SUCCESS) {
            mosquitto_connect_callback_set(client, onConnect);
            mosquitto_subscribe_callback_set(client, onSubscribe);
            mosquitto_disconnect_callback_set(client, onDisconnect);
            mosquitto_message_callback_set(client, onMessage);
            {
                std::lock_guard<std::mutex> lock(mutex_);
                client_ = client;
                ready_ = false;
            }
            result = mosquitto_connect(client, brokerHost_.c_str(), brokerPort_, 30);
        }
        if (result == MOSQ_ERR_SUCCESS) {
            while (!stopping_.load()) {
                result = mosquitto_loop(client, 200, 1);
                expireCommands();
                if (result != MOSQ_ERR_SUCCESS) break;
            }
        }
        if (result != MOSQ_ERR_SUCCESS && !stopping_.load())
            std::cerr << "[control] MQTT connection ended: " << errorText(result) << '\n';
        {
            std::lock_guard<std::mutex> lock(mutex_);
            ready_ = false;
            responseSubscriptionMid_ = -1;
            client_ = nullptr;
        }
        mosquitto_disconnect(client);
        mosquitto_destroy(client);
        if (!stopping_.load()) waitToRetry();
    }
    mosquitto_lib_cleanup();
}
