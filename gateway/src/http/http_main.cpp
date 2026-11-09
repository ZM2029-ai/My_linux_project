#include "storage/device_store.hpp"
#include "httplib.h"
#include <nlohmann/json.hpp>
#ifdef EDGELINK_ENABLE_CONTROL
#include "control_service.hpp"
#include "auth_config.hpp"
#endif
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <regex>
#include <stdexcept>

using json = nlohmann::json;

namespace {
json telemetryJson(const StoredTelemetry &t)
{
    return {{"id", t.id}, {"sampled_at", t.sample.sampled_at}, {"received_at", t.received_at},
            {"temperature_c", t.sample.temperature_c}, {"humidity_percent", t.sample.humidity_percent}};
}
json deviceJson(DeviceStore &store, const StoredDevice &d)
{
    StoredTelemetry latest{};
    json value = {{"device_id", d.device_id}, {"display_name", d.display_name},
                  {"presence", d.presence}, {"registered_at", d.registered_at},
                  {"last_received_at", d.last_received_at}, {"latest", nullptr}};
    if (store.latest(d.device_id, latest)) value["latest"] = telemetryJson(latest);
    return value;
}
void reply(httplib::Response &response, const json &data, int status = 200)
{
    response.status = status;
    response.set_content(data.dump(), "application/json; charset=utf-8");
    response.set_header("Cache-Control", "no-store");
}
void error(httplib::Response &response, int status, const std::string &message)
{
    reply(response, {{"error", message}}, status);
}
int number(const std::string &value, int maximum)
{
    if (value.empty() || value.size() > 5 || value.find_first_not_of("0123456789") != std::string::npos)
        throw std::invalid_argument("invalid number");
    const int result = std::stoi(value);
    if (result < 1 || result > maximum) throw std::invalid_argument("number out of range");
    return result;
}
#ifdef EDGELINK_ENABLE_CONTROL
bool isJsonContentType(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value.compare(0, 16, "application/json") == 0 &&
           (value.size() == 16 || value[16] == ';' || value[16] == ' ');
}

json controlJson(const ControlCommand &command)
{
    return {{"command_id", command.command_id}, {"device_id", command.device_id},
            {"enabled", command.enabled}, {"status", command.status}, {"message", command.message}};
}

bool isControlPost(const httplib::Request &request)
{
    static const std::regex path("/api/devices/[A-Za-z0-9_-]{1,64}/commands");
    return request.method == "POST" && std::regex_match(request.path, path);
}
#endif
}

int main(int argc, char **argv)
{
    if (argc < 4
#ifndef EDGELINK_ENABLE_CONTROL
        || argc > 5
#endif
    ) {
#ifdef EDGELINK_ENABLE_CONTROL
        std::cerr << "Usage: edgelink_http DATABASE WEB_DIRECTORY PORT [BIND_ADDRESS] --users FILE --origin ORIGIN [--broker HOST PORT]\n";
#else
        std::cerr << "Usage: edgelink_http DATABASE WEB_DIRECTORY PORT [BIND_ADDRESS]\n";
#endif
        return 1;
    }
    try {
        const std::string database = argv[1];
        std::string address = "127.0.0.1";
        const int port = number(argv[3], 65535);
#ifdef EDGELINK_ENABLE_CONTROL
        std::string brokerHost = "127.0.0.1";
        int brokerPort = 1883;
        std::string usersFile;
        std::string allowedOrigin;
        int argument = 4;
        if (argument < argc && std::string(argv[argument]).compare(0, 2, "--") != 0)
            address = argv[argument++];
        while (argument < argc) {
            const std::string option = argv[argument++];
            if (option == "--users" && argument < argc) {
                usersFile = argv[argument++];
            } else if (option == "--origin" && argument < argc) {
                allowedOrigin = argv[argument++];
            } else if (option == "--broker" && argument + 1 < argc) {
                brokerHost = argv[argument++];
                brokerPort = number(argv[argument++], 65535);
            } else {
                throw std::invalid_argument("invalid HTTP service option");
            }
        }
        if (usersFile.empty() || allowedOrigin.empty())
            throw std::invalid_argument("--users and --origin are required when control is enabled");
        if (address != "127.0.0.1" && address != "::1")
            throw std::invalid_argument("control-enabled HTTP service must bind to loopback");
        AuthConfig auth(usersFile, allowedOrigin);
#else
        if (argc == 5) address = argv[4];
#endif
        { DeviceStore check(database, DeviceStore::OpenMode::ReadOnly); }
#ifdef EDGELINK_ENABLE_CONTROL
        ControlService control(database, brokerHost, brokerPort);
        control.start();
#endif
        httplib::Server server;
        server.new_task_queue = [] { return new httplib::ThreadPool(4); };
        server.set_payload_max_length(4096);
#ifdef EDGELINK_ENABLE_CONTROL
        server.set_pre_routing_handler([&](const httplib::Request &req, httplib::Response &res) {
            std::string username;
            UserRole role;
            if (!auth.authenticate(req, username, role)) {
                error(res, 401, "authentication required");
                res.set_header("WWW-Authenticate", "Basic realm=\"EdgeLink\", charset=\"UTF-8\"");
                std::cerr << "[http] rejected unauthenticated request " << req.method << ' ' << req.path << '\n';
                return httplib::Server::HandlerResponse::Handled;
            }
            if (isControlPost(req) && role != UserRole::Control) {
                error(res, 403, "control permission required");
                std::cerr << "[http] control permission denied for " << req.path << '\n';
                return httplib::Server::HandlerResponse::Handled;
            }
            if (isControlPost(req) && req.get_header_value("Origin") != auth.allowedOrigin()) {
                error(res, 403, "request Origin is not allowed");
                std::cerr << "[http] rejected control request with disallowed Origin\n";
                return httplib::Server::HandlerResponse::Handled;
            }
            return httplib::Server::HandlerResponse::Unhandled;
        });
#endif
        server.Get("/api/devices", [&](const httplib::Request &, httplib::Response &res) {
            try {
                json devices = json::array();
                { DeviceStore store(database, DeviceStore::OpenMode::ReadOnly);
                  for (const auto &device : store.list()) devices.push_back(deviceJson(store, device)); }
                reply(res, {{"devices", devices}});
            } catch (const std::exception &e) { std::cerr << "[http] " << e.what() << '\n'; error(res, 500, "database error"); }
        });
#ifdef EDGELINK_ENABLE_CONTROL
        server.Get("/api/session", [&](const httplib::Request &req, httplib::Response &res) {
            std::string username;
            UserRole role;
            if (!auth.authenticate(req, username, role)) { error(res, 401, "authentication required"); return; }
            reply(res, {{"username", username}, {"can_control", role == UserRole::Control}});
        });
#endif
        server.Get(R"(/api/devices/([A-Za-z0-9_-]+)(/status)?)", [&](const httplib::Request &req, httplib::Response &res) {
            try {
                json data;
                bool found;
                { DeviceStore store(database, DeviceStore::OpenMode::ReadOnly);
                  StoredDevice device;
                  found = store.find(req.matches[1].str(), device);
                  if (found) data = req.matches[2].matched ? json{{"device_id", device.device_id}, {"presence", device.presence}} : deviceJson(store, device); }
                if (!found) { error(res, 404, "device not found"); return; }
                reply(res, data);
            } catch (const std::invalid_argument &) { error(res, 400, "invalid device ID"); }
              catch (const std::exception &e) { std::cerr << "[http] " << e.what() << '\n'; error(res, 500, "database error"); }
        });
        server.Get(R"(/api/devices/([A-Za-z0-9_-]+)/telemetry)", [&](const httplib::Request &req, httplib::Response &res) {
            try {
                if (!req.has_param("start") || !req.has_param("end")) throw std::invalid_argument("start and end required");
                const int limit = req.has_param("limit") ? number(req.get_param_value("limit"), 1000) : 100;
                json samples = json::array();
                bool found, truncated = false;
                { DeviceStore store(database, DeviceStore::OpenMode::ReadOnly);
                  StoredDevice device;
                  found = store.find(req.matches[1].str(), device);
                  if (found) {
                      auto rows = store.history(device.device_id, req.get_param_value("start"), req.get_param_value("end"), limit + 1);
                      truncated = rows.size() > static_cast<std::size_t>(limit);
                      if (truncated) rows.pop_back();
                      for (const auto &row : rows) samples.push_back(telemetryJson(row));
                  } }
                if (!found) { error(res, 404, "device not found"); return; }
                reply(res, {{"samples", samples}, {"truncated", truncated}});
            } catch (const std::invalid_argument &e) { error(res, 400, e.what()); }
              catch (const std::exception &e) { std::cerr << "[http] " << e.what() << '\n'; error(res, 500, "database error"); }
        });
#ifdef EDGELINK_ENABLE_CONTROL
        server.Post(R"(/api/devices/([A-Za-z0-9_-]+)/commands)", [&](const httplib::Request &req, httplib::Response &res) {
            const auto deviceId = req.matches[1].str();
            if (!control.ready()) {
                std::cerr << "[http] control MQTT is not ready for " << deviceId << '\n';
                error(res, 503, "control MQTT is not ready"); return;
            }
            if (!isJsonContentType(req.get_header_value("Content-Type"))) {
                std::cerr << "[http] rejected control request with invalid Content-Type for " << deviceId << '\n';
                error(res, 400, "Content-Type must be application/json"); return;
            }
            try {
                const auto body = json::parse(req.body);
                if (!body.is_object() || body.size() != 1) {
                    std::cerr << "[http] rejected invalid control body for " << deviceId << '\n';
                    error(res, 400, "body must contain only a boolean enabled field"); return;
                }
                const auto enabled = body.find("enabled");
                if (enabled == body.end() || !enabled->is_boolean()) {
                    std::cerr << "[http] rejected invalid control body for " << deviceId << '\n';
                    error(res, 400, "body must contain only a boolean enabled field"); return;
                }
                {
                    DeviceStore store(database, DeviceStore::OpenMode::ReadOnly);
                    StoredDevice device;
                    if (!store.find(deviceId, device)) {
                        std::cerr << "[http] rejected control request for unknown device " << deviceId << '\n';
                        error(res, 404, "device not found"); return;
                    }
                }
                ControlCommand command;
                std::string actor;
                UserRole role;
                if (!auth.authenticate(req, actor, role) || role != UserRole::Control) {
                    error(res, 401, "authentication required"); return;
                }
                const auto result = control.submit(deviceId, enabled->get<bool>(), actor, command);
                switch (result) {
                case ControlSubmitResult::Accepted:
                    reply(res, controlJson(command), 202);
                    return;
                case ControlSubmitResult::Busy:
                    std::cerr << "[http] control request conflicts with a pending command for " << deviceId << '\n';
                    error(res, 409, "a command is already pending for this device");
                    return;
                case ControlSubmitResult::Capacity:
                    std::cerr << "[http] control command capacity reached\n";
                    error(res, 503, "control command capacity reached");
                    return;
                case ControlSubmitResult::AuditFailed:
                    error(res, 500, "could not record control request");
                    return;
                case ControlSubmitResult::PublishFailed:
                case ControlSubmitResult::NotReady:
                    std::cerr << "[http] control MQTT is not ready for " << deviceId << '\n';
                    error(res, 503, "control MQTT is not ready");
                    return;
                }
            } catch (const json::exception &) {
                std::cerr << "[http] rejected malformed control JSON for " << deviceId << '\n';
                error(res, 400, "invalid JSON body");
            } catch (const std::exception &e) {
                std::cerr << "[http] control request failed: " << e.what() << '\n';
                error(res, 500, "control request failed");
            }
        });
        server.Get(R"(/api/devices/([A-Za-z0-9_-]+)/commands/([A-Za-z0-9_-]{1,64}))",
                   [&](const httplib::Request &req, httplib::Response &res) {
            ControlCommand command;
            if (!control.find(req.matches[1].str(), req.matches[2].str(), command)) {
                error(res, 404, "command not found");
                return;
            }
            reply(res, controlJson(command));
        });
#endif
        if (!server.set_mount_point("/", argv[2])) throw std::runtime_error("invalid web directory");
        std::cout << "[http] listening on " << address << ':' << port << std::endl;
        if (!server.listen(address, port)) throw std::runtime_error("HTTP listen failed");
    } catch (const std::exception &e) {
        std::cerr << "[http] " << e.what() << '\n';
        return 1;
    }
    return 0;
}
