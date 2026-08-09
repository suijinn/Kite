#include "core/base/Format.h"

#include <cstdio>
#include <ctime>

namespace kite {

std::string FormatSize(uint64_t bytes) {
    static const char* kUnits[] = { "B", "KB", "MB", "GB", "TB", "PB" };
    if (bytes < 1024) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%llu B", static_cast<unsigned long long>(bytes));
        return buf;
    }
    double v = static_cast<double>(bytes);
    int unit = 0;
    while (v >= 1024.0 && unit < 5) {
        v /= 1024.0;
        ++unit;
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), (v < 10.0 ? "%.1f %s" : "%.0f %s"), v, kUnits[unit]);
    return buf;
}

std::string FormatDateTime(int64_t unixSeconds) {
    if (unixSeconds <= 0) return {};
    const std::time_t t = static_cast<std::time_t>(unixSeconds);
    std::tm tm{};
#if defined(_WIN32)
    if (localtime_s(&tm, &t) != 0) return {};
#else
    if (localtime_r(&t, &tm) == nullptr) return {};
#endif
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d", tm.tm_year + 1900, tm.tm_mon + 1,
                  tm.tm_mday, tm.tm_hour, tm.tm_min);
    return buf;
}

}  // namespace kite
