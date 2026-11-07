#ifndef EDGELINK_DEVICE_FIELDS_HPP
#define EDGELINK_DEVICE_FIELDS_HPP

#include <cstdio>
#include <string>

enum class DevicePresence { Unknown, Online, Offline };

inline const char *presenceName(DevicePresence presence)
{
    switch (presence) {
    case DevicePresence::Online: return "online";
    case DevicePresence::Offline: return "offline";
    default: return "unknown";
    }
}

inline bool validDeviceId(const std::string &id)
{
    if (id.empty() || id.size() > 64) return false;
    for (char c : id) {
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
    }
    return true;
}

// 协议和历史查询共用固定宽度的北京时间格式。
inline bool validTimestamp(const std::string &s)
{
    if (s.size() != 19) return false;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const char separator = i == 4 || i == 7 ? '-' : i == 10 ? ' ' : i == 13 || i == 16 ? ':' : 0;
        if (separator ? s[i] != separator : s[i] < '0' || s[i] > '9') return false;
    }
    int year, month, day, hour, minute, second;
    if (std::sscanf(s.c_str(), "%d-%d-%d %d:%d:%d", &year, &month, &day,
                    &hour, &minute, &second) != 6) return false;
    if (year < 1970 || month < 1 || month > 12 || hour > 23 || minute > 59 || second > 59) return false;
    const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    return day >= 1 && day <= days[month - 1] + (month == 2 && leap ? 1 : 0);
}

#endif
