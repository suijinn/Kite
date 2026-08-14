#include "TestFramework.h"
#include "core/base/PathUtil.h"

using namespace kite;

KITE_TEST(path, join_inserts_exactly_one_separator) {
    KITE_EXPECT_EQ(path::Join("C:\\a", "b"), std::string("C:\\a\\b"));
    KITE_EXPECT_EQ(path::Join("C:\\a\\", "b"), std::string("C:\\a\\b"));
    KITE_EXPECT_EQ(path::Join("C:\\a", "\\b"), std::string("C:\\a\\b"));
    KITE_EXPECT_EQ(path::Join("", "b"), std::string("b"));
    KITE_EXPECT_EQ(path::Join("C:\\a", ""), std::string("C:\\a"));
}

KITE_TEST(path, parent_walks_up_to_the_root_then_stops) {
    KITE_EXPECT_EQ(path::Parent("C:\\a\\b"), std::string("C:\\a"));
    KITE_EXPECT_EQ(path::Parent("C:\\a"), std::string("C:\\"));
    KITE_EXPECT_EQ(path::Parent("C:\\"), std::string(""));
    KITE_EXPECT_EQ(path::Parent("C:\\a\\b\\"), std::string("C:\\a"));
}

KITE_TEST(path, parent_handles_unc_roots) {
    KITE_EXPECT_EQ(path::Parent("\\\\server\\share\\dir"), std::string("\\\\server\\share\\"));
    // The share itself is a root: there is nowhere above it to go.
    KITE_EXPECT_EQ(path::Parent("\\\\server\\share\\"), std::string(""));
}

KITE_TEST(path, filename_and_stem_and_extension) {
    KITE_EXPECT_EQ(path::FileName("C:\\a\\b.txt"), std::string("b.txt"));
    KITE_EXPECT_EQ(path::Stem("C:\\a\\b.txt"), std::string("b"));
    KITE_EXPECT_EQ(path::Extension("C:\\a\\b.TXT"), std::string("txt"));
    KITE_EXPECT_EQ(path::Extension("C:\\a\\b"), std::string(""));
    // A leading dot is part of the name, not an extension.
    KITE_EXPECT_EQ(path::Extension("C:\\a\\.gitignore"), std::string(""));
    KITE_EXPECT_EQ(path::Stem("C:\\a\\.gitignore"), std::string(".gitignore"));
}

KITE_TEST(path, is_root) {
    KITE_EXPECT(path::IsRoot("C:\\"));
    KITE_EXPECT(path::IsRoot("C:"));
    KITE_EXPECT_FALSE(path::IsRoot("C:\\a"));
}

KITE_TEST(path, is_absolute) {
    KITE_EXPECT(path::IsAbsolute("C:\\a"));
    KITE_EXPECT(path::IsAbsolute("C:"));
    KITE_EXPECT(path::IsAbsolute("\\\\server\\share\\a"));
    KITE_EXPECT(path::IsAbsolute("\\a"));
    KITE_EXPECT_FALSE(path::IsAbsolute("a\\b"));
    KITE_EXPECT_FALSE(path::IsAbsolute(""));
}

// What Ctrl+arrow moves over in the address bar.
KITE_TEST(path, segment_steps_move_one_path_component) {
    const std::string p = "C:\\home\\alpha";

    // Backwards from the end: the leaf, then the folder above it, then the root.
    KITE_EXPECT_EQ(path::PrevSegment(p, p.size()), size_t{ 8 });
    KITE_EXPECT_EQ(path::PrevSegment(p, 8), size_t{ 3 });
    KITE_EXPECT_EQ(path::PrevSegment(p, 3), size_t{ 0 });
    KITE_EXPECT_EQ(path::PrevSegment(p, 0), size_t{ 0 });

    KITE_EXPECT_EQ(path::NextSegment(p, 0), size_t{ 2 });
    KITE_EXPECT_EQ(path::NextSegment(p, 2), size_t{ 7 });
    KITE_EXPECT_EQ(path::NextSegment(p, 7), p.size());
    KITE_EXPECT_EQ(path::NextSegment(p, p.size()), p.size());

    // Out of range is the end, not a crash.
    KITE_EXPECT_EQ(path::PrevSegment(p, 999), size_t{ 8 });
    KITE_EXPECT_EQ(path::NextSegment(p, 999), p.size());

    // A run of separators is crossed in one step, either way.
    const std::string doubled = "C:\\a\\\\b";
    KITE_EXPECT_EQ(path::PrevSegment(doubled, 6), size_t{ 3 });
    KITE_EXPECT_EQ(path::NextSegment(doubled, 4), doubled.size());

    // Multi-byte names: separators are ASCII, so a step never lands inside one.
    const std::string ja = "C:\\\xE3\x81\x82\\b";
    KITE_EXPECT_EQ(path::PrevSegment(ja, ja.size()), size_t{ 7 });
    KITE_EXPECT_EQ(path::PrevSegment(ja, 7), size_t{ 3 });
}

KITE_TEST(path, normalize_collapses_and_resolves) {
    KITE_EXPECT_EQ(path::Normalize("C:/a//b/./c"), std::string("C:\\a\\b\\c"));
    KITE_EXPECT_EQ(path::Normalize("C:\\a\\b\\..\\c"), std::string("C:\\a\\c"));
    KITE_EXPECT_EQ(path::Normalize("c:\\a"), std::string("C:\\a"));
    // Walking above the root must not escape it.
    KITE_EXPECT_EQ(path::Normalize("C:\\..\\..\\a"), std::string("C:\\a"));
}

KITE_TEST(path, display_name_trims_the_root_separator) {
    KITE_EXPECT_EQ(path::DisplayName("C:\\a\\b"), std::string("b"));
    KITE_EXPECT_EQ(path::DisplayName("C:\\"), std::string("C:"));
}

KITE_TEST(path, natural_compare_orders_digit_runs_numerically) {
    // The whole point: "image2" must sort before "image10".
    KITE_EXPECT(path::NaturalCompare("image2.png", "image10.png") < 0);
    KITE_EXPECT(path::NaturalCompare("image10.png", "image2.png") > 0);
    KITE_EXPECT_EQ(path::NaturalCompare("same", "same"), 0);
}

KITE_TEST(path, natural_compare_ignores_ascii_case_and_leading_zeros) {
    KITE_EXPECT_EQ(path::NaturalCompare("File", "file"), 0);
    KITE_EXPECT_EQ(path::NaturalCompare("a007", "a7"), 0);
    KITE_EXPECT(path::NaturalCompare("a", "ab") < 0);
}

KITE_TEST(path, natural_compare_handles_multibyte_names) {
    // Code point order for Japanese; the requirement is only that it is a
    // strict, stable ordering rather than byte soup.
    KITE_EXPECT(path::NaturalCompare("\xE3\x81\x82", "\xE3\x81\x84") < 0);
    KITE_EXPECT(path::NaturalCompare("\xE3\x81\x84", "\xE3\x81\x82") > 0);
}

KITE_TEST(path, token_escaping_round_trips_awkward_paths) {
    // Session layouts are serialized with these characters as delimiters, so a
    // path containing them has to survive.
    const std::string nasty = "C:\\a,b\\c(d)\\e|f\\{g}@h%i";
    const std::string escaped = path::EscapeToken(nasty);
    KITE_EXPECT(escaped.find(',') == std::string::npos);
    KITE_EXPECT(escaped.find('|') == std::string::npos);
    KITE_EXPECT_EQ(path::UnescapeToken(escaped), nasty);
}
