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
