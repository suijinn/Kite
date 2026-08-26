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

AgeText FormatAge(int64_t unixSeconds, int64_t nowSeconds) {
    if (unixSeconds <= 0) return {};

    // 刻みは «その単位で 1 と言える間» まで。分を 90 分まで引っ張ると、1 時間前と
    // 2 時間前が同じ «90 分前» の側に並ぶ。
    const int64_t seconds = nowSeconds > unixSeconds ? nowSeconds - unixSeconds : 0;
    constexpr int64_t kMinute = 60;
    constexpr int64_t kHour = 60 * kMinute;
    constexpr int64_t kDay = 24 * kHour;
    // 月と年は «だいたい» でよい。ここが答えているのは «どれだけ前か» であって、
    // 暦の上の何月何日かは隣の列がすでに正確に言っている。
    constexpr int64_t kMonth = 30 * kDay;
    constexpr int64_t kYear = 365 * kDay;

    if (seconds < kMinute) return { "ui.age_now", 0 };
    if (seconds < kHour) return { "ui.age_minutes", static_cast<int>(seconds / kMinute) };
    if (seconds < kDay) return { "ui.age_hours", static_cast<int>(seconds / kHour) };
    if (seconds < kMonth) return { "ui.age_days", static_cast<int>(seconds / kDay) };
    if (seconds < kYear) return { "ui.age_months", static_cast<int>(seconds / kMonth) };
    return { "ui.age_years", static_cast<int>(seconds / kYear) };
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
