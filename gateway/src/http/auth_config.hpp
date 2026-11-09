#ifndef EDGELINK_AUTH_CONFIG_HPP
#define EDGELINK_AUTH_CONFIG_HPP

#include "httplib.h"

#include <map>
#include <string>

enum class UserRole { ReadOnly, Control };

class AuthConfig {
public:
    AuthConfig(const std::string &path, std::string allowedOrigin);
    bool authenticate(const httplib::Request &request, std::string &username, UserRole &role) const;
    const std::string &allowedOrigin() const { return allowedOrigin_; }

private:
    struct User { std::string password; UserRole role; };
    std::map<std::string, User> users_;
    std::string allowedOrigin_;
};

#endif
