#include "auth_config.hpp"

#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <utility>

#ifndef _WIN32
#include <sys/stat.h>
#endif

namespace {
bool validOrigin(const std::string &origin)
{
    const auto schemeEnd = origin.find("://");
    if (schemeEnd == std::string::npos) return false;
    const auto scheme = origin.substr(0, schemeEnd);
    if (scheme != "http" && scheme != "https") return false;
    const auto host = origin.substr(schemeEnd + 3);
    return !host.empty() && host.find_first_of("/?#@ \t\r\n") == std::string::npos;
}

int base64Value(char value)
{
    if (value >= 'A' && value <= 'Z') return value - 'A';
    if (value >= 'a' && value <= 'z') return value - 'a' + 26;
    if (value >= '0' && value <= '9') return value - '0' + 52;
    if (value == '+') return 62;
    if (value == '/') return 63;
    return -1;
}

bool decodeBasicCredentials(const std::string &header, std::string &credentials)
{
    if (header.compare(0, 6, "Basic ") != 0) return false;
    const auto encoded = header.substr(6);
    if (encoded.empty() || encoded.size() > 1024 || encoded.size() % 4 != 0) return false;
    credentials.clear();
    for (std::size_t offset = 0; offset < encoded.size(); offset += 4) {
        const bool last = offset + 4 == encoded.size();
        const int a = base64Value(encoded[offset]);
        const int b = base64Value(encoded[offset + 1]);
        if (a < 0 || b < 0) return false;
        const char c = encoded[offset + 2];
        const char d = encoded[offset + 3];
        if (c == '=') {
            if (!last || d != '=') return false;
            credentials.push_back(static_cast<char>((a << 2) | (b >> 4)));
            continue;
        }
        const int cValue = base64Value(c);
        if (cValue < 0) return false;
        credentials.push_back(static_cast<char>((a << 2) | (b >> 4)));
        credentials.push_back(static_cast<char>((b << 4) | (cValue >> 2)));
        if (d == '=') {
            if (!last) return false;
            continue;
        }
        const int dValue = base64Value(d);
        if (dValue < 0) return false;
        credentials.push_back(static_cast<char>((cValue << 6) | dValue));
    }
    return true;
}

bool validUsername(const std::string &username)
{
    if (username.empty() || username.size() > 64) return false;
    for (const unsigned char c : username) {
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.')) return false;
    }
    return true;
}

bool sameSecret(const std::string &candidate, const std::string &expected)
{
    const auto length = std::max(candidate.size(), expected.size());
    std::size_t difference = candidate.size() ^ expected.size();
    for (std::size_t i = 0; i < length; ++i) {
        const unsigned char left = i < candidate.size() ? static_cast<unsigned char>(candidate[i]) : 0;
        const unsigned char right = i < expected.size() ? static_cast<unsigned char>(expected[i]) : 0;
        difference |= left ^ right;
    }
    return difference == 0;
}
}

AuthConfig::AuthConfig(const std::string &path, std::string allowedOrigin)
    : allowedOrigin_(std::move(allowedOrigin))
{
    if (!validOrigin(allowedOrigin_)) throw std::invalid_argument("invalid allowed browser Origin");
#ifndef _WIN32
    struct stat info {};
    if (stat(path.c_str(), &info) != 0 || !S_ISREG(info.st_mode) ||
        (info.st_mode & S_IRUSR) == 0 || (info.st_mode & 0077) != 0)
        throw std::runtime_error("users file must be a regular file with owner-only permissions");
#endif
    std::ifstream input(path);
    if (!input) throw std::runtime_error("cannot open users file");
    std::string line;
    bool hasControlUser = false;
    while (std::getline(input, line)) {
        if (line.empty() || line[0] == '#') continue;
        const auto first = line.find(':');
        const auto second = first == std::string::npos ? first : line.find(':', first + 1);
        if (first == std::string::npos || second == std::string::npos ||
            line.find(':', second + 1) != std::string::npos)
            throw std::runtime_error("invalid users file entry");
        const auto username = line.substr(0, first);
        const auto roleName = line.substr(first + 1, second - first - 1);
        const auto password = line.substr(second + 1);
        if (!validUsername(username) || password.size() < 12 || password.size() > 128 ||
            password.find_first_of("\r\n") != std::string::npos)
            throw std::runtime_error("invalid users file entry");
        UserRole role;
        if (roleName == "control") {
            role = UserRole::Control;
            hasControlUser = true;
        } else if (roleName == "read") {
            role = UserRole::ReadOnly;
        } else {
            throw std::runtime_error("users file role must be control or read");
        }
        if (!users_.emplace(username, User{password, role}).second)
            throw std::runtime_error("duplicate username in users file");
    }
    if (input.bad() || users_.empty() || !hasControlUser)
        throw std::runtime_error("users file must contain at least one control account");
}

bool AuthConfig::authenticate(const httplib::Request &request, std::string &username, UserRole &role) const
{
    std::string credentials;
    if (!decodeBasicCredentials(request.get_header_value("Authorization"), credentials)) return false;
    const auto separator = credentials.find(':');
    if (separator == std::string::npos) return false;
    const auto candidate = users_.find(credentials.substr(0, separator));
    if (candidate == users_.end()) return false;
    const auto password = credentials.substr(separator + 1);
    if (!sameSecret(password, candidate->second.password)) return false;
    username = candidate->first;
    role = candidate->second.role;
    return true;
}
