#include "TestFramework.h"
#include "core/base/Utf8.h"

using namespace kite;

KITE_TEST(utf8, decodes_ascii) {
    size_t i = 0;
    KITE_EXPECT_EQ(utf8::Decode("A", i), 0x41u);
    KITE_EXPECT_EQ(i, size_t{ 1 });
}

KITE_TEST(utf8, decodes_multibyte) {
    // U+3042 HIRAGANA LETTER A, three bytes.
    const std::string a = "\xE3\x81\x82";
    size_t i = 0;
    KITE_EXPECT_EQ(utf8::Decode(a, i), 0x3042u);
    KITE_EXPECT_EQ(i, size_t{ 3 });
}

KITE_TEST(utf8, decodes_surrogate_range_codepoint) {
    // U+1F600, four bytes.
    const std::string emoji = "\xF0\x9F\x98\x80";
    size_t i = 0;
    KITE_EXPECT_EQ(utf8::Decode(emoji, i), 0x1F600u);
    KITE_EXPECT_EQ(i, size_t{ 4 });
}

KITE_TEST(utf8, truncated_sequence_does_not_run_past_the_end) {
    // A lead byte promising three bytes but only one present must consume
    // exactly one byte, or callers loop forever.
    const std::string broken = "\xE3";
    size_t i = 0;
    const uint32_t cp = utf8::Decode(broken, i);
    KITE_EXPECT_EQ(cp, utf8::kReplacement);
    KITE_EXPECT_EQ(i, size_t{ 1 });
}

KITE_TEST(utf8, encode_round_trips) {
    const uint32_t points[] = { 0x41, 0xE9, 0x3042, 0x1F600 };
    for (uint32_t cp : points) {
        const std::string encoded = utf8::Encode(cp);
        size_t i = 0;
        KITE_EXPECT_EQ(utf8::Decode(encoded, i), cp);
        KITE_EXPECT_EQ(i, encoded.size());
    }
}

KITE_TEST(utf8, boundaries_step_whole_characters) {
    const std::string mixed = "a\xE3\x81\x82" "b";  // a, HIRAGANA A, b
    KITE_EXPECT_EQ(utf8::NextBoundary(mixed, 0), size_t{ 1 });
    KITE_EXPECT_EQ(utf8::NextBoundary(mixed, 1), size_t{ 4 });
    KITE_EXPECT_EQ(utf8::PrevBoundary(mixed, 4), size_t{ 1 });
    KITE_EXPECT_EQ(utf8::PrevBoundary(mixed, 0), size_t{ 0 });
    KITE_EXPECT_EQ(utf8::CharCount(mixed), size_t{ 3 });
}

KITE_TEST(utf8, boundaries_are_clamped_at_the_ends) {
    const std::string s = "ab";
    KITE_EXPECT_EQ(utf8::NextBoundary(s, 2), size_t{ 2 });
    KITE_EXPECT_EQ(utf8::NextBoundary(s, 99), size_t{ 2 });
}

KITE_TEST(utf8, ascii_case_folding_leaves_multibyte_alone) {
    KITE_EXPECT_EQ(utf8::ToLowerAscii("MiXeD"), std::string("mixed"));
    KITE_EXPECT_EQ(utf8::ToLowerAscii("\xE3\x81\x82"), std::string("\xE3\x81\x82"));
    KITE_EXPECT(utf8::EqualsIgnoreCaseAscii("Kite", "kITE"));
    KITE_EXPECT_FALSE(utf8::EqualsIgnoreCaseAscii("Kite", "Kites"));
}

KITE_TEST(utf8, prefix_matching_folds_ascii_and_compares_the_rest_byte_for_byte) {
    KITE_EXPECT(utf8::StartsWithIgnoreCaseAscii("Kite", "ki"));
    KITE_EXPECT(utf8::StartsWithIgnoreCaseAscii("Kite", "Kite"));
    // Nothing is not a prefix of everything by accident: the empty string is.
    KITE_EXPECT(utf8::StartsWithIgnoreCaseAscii("Kite", ""));
    KITE_EXPECT_FALSE(utf8::StartsWithIgnoreCaseAscii("Kite", "Kites"));
    KITE_EXPECT_FALSE(utf8::StartsWithIgnoreCaseAscii("", "k"));
    // \xE8\xB3\x87 = U+8CC7, matched as bytes - there is no case to fold there.
    KITE_EXPECT(utf8::StartsWithIgnoreCaseAscii("\xE8\xB3\x87\xE6\x96\x99", "\xE8\xB3\x87"));
}
