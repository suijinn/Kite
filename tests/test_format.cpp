#include "TestFramework.h"
#include "core/base/Format.h"

using namespace kite;

KITE_TEST(format, bytes_below_a_kilobyte_stay_exact) {
    KITE_EXPECT_EQ(FormatSize(0), std::string("0 B"));
    KITE_EXPECT_EQ(FormatSize(512), std::string("512 B"));
    KITE_EXPECT_EQ(FormatSize(1023), std::string("1023 B"));
}

KITE_TEST(format, scales_to_binary_units) {
    KITE_EXPECT_EQ(FormatSize(1024), std::string("1.0 KB"));
    KITE_EXPECT_EQ(FormatSize(1024 * 1024), std::string("1.0 MB"));
    KITE_EXPECT_EQ(FormatSize(1024ull * 1024 * 1024), std::string("1.0 GB"));
}

KITE_TEST(format, drops_the_decimal_once_the_number_is_wide) {
    // Under 10 keeps one decimal, above it does not - keeps the column narrow.
    KITE_EXPECT_EQ(FormatSize(1024 * 9), std::string("9.0 KB"));
    KITE_EXPECT_EQ(FormatSize(1024 * 100), std::string("100 KB"));
}

KITE_TEST(format, datetime_is_empty_for_a_missing_timestamp) {
    KITE_EXPECT_EQ(FormatDateTime(0), std::string(""));
    KITE_EXPECT_EQ(FormatDateTime(-5), std::string(""));
}

KITE_TEST(format, datetime_has_a_fixed_width_layout) {
    const std::string text = FormatDateTime(1'000'000'000);
    KITE_EXPECT_EQ(text.size(), size_t{ 16 });  // YYYY-MM-DD HH:MM
    KITE_EXPECT_EQ(text[4], '-');
    KITE_EXPECT_EQ(text[7], '-');
    KITE_EXPECT_EQ(text[10], ' ');
    KITE_EXPECT_EQ(text[13], ':');
}

// --- 経過時間 ---------------------------------------------------------------

namespace {

constexpr int64_t kNow = 1'767'225'600;  // 2026-01-01 00:00:00 UTC

// 「n 単位前」を 1 つの文字列にして比べる。キーと数はどちらが欠けても答えにならない。
std::string Age(int64_t secondsAgo) {
    const AgeText age = FormatAge(kNow - secondsAgo, kNow);
    return std::string(age.labelKey) + " " + std::to_string(age.count);
}

}  // namespace

KITE_TEST(format, age_is_empty_for_a_missing_timestamp) {
    KITE_EXPECT_EQ(std::string(FormatAge(0, kNow).labelKey), std::string(""));
    KITE_EXPECT_EQ(std::string(FormatAge(-5, kNow).labelKey), std::string(""));
}

KITE_TEST(format, age_picks_the_unit_it_can_say_one_of) {
    KITE_EXPECT_EQ(Age(0), std::string("ui.age_now 0"));
    KITE_EXPECT_EQ(Age(59), std::string("ui.age_now 0"));
    KITE_EXPECT_EQ(Age(60), std::string("ui.age_minutes 1"));
    KITE_EXPECT_EQ(Age(5 * 60), std::string("ui.age_minutes 5"));
    KITE_EXPECT_EQ(Age(59 * 60 + 59), std::string("ui.age_minutes 59"));
    KITE_EXPECT_EQ(Age(3600), std::string("ui.age_hours 1"));
    KITE_EXPECT_EQ(Age(23 * 3600), std::string("ui.age_hours 23"));
    KITE_EXPECT_EQ(Age(86400), std::string("ui.age_days 1"));
    KITE_EXPECT_EQ(Age(29 * 86400), std::string("ui.age_days 29"));
    KITE_EXPECT_EQ(Age(30 * 86400), std::string("ui.age_months 1"));
    KITE_EXPECT_EQ(Age(364 * 86400), std::string("ui.age_months 12"));
    KITE_EXPECT_EQ(Age(365 * 86400), std::string("ui.age_years 1"));
    KITE_EXPECT_EQ(Age(1000 * 86400), std::string("ui.age_years 2"));
}

KITE_TEST(format, a_time_in_the_future_reads_as_just_now) {
    // 時計のずれた共有では普通に起きる。「-3 分前」と出しても読む人の役に立たない。
    KITE_EXPECT_EQ(std::string(FormatAge(kNow + 600, kNow).labelKey), std::string("ui.age_now"));
}
