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
    // Above a share is the server, which lists the shares it offers.
    KITE_EXPECT_EQ(path::Parent("\\\\server\\share\\"), std::string("\\\\server"));
    KITE_EXPECT_EQ(path::Parent("\\\\server\\share"), std::string("\\\\server"));
    // The server is the top; a trailing separator does not invent another level.
    KITE_EXPECT_EQ(path::Parent("\\\\server"), std::string(""));
    KITE_EXPECT_EQ(path::Parent("\\\\server\\"), std::string(""));
}

KITE_TEST(path, unc_server_and_share_are_told_apart) {
    KITE_EXPECT(path::IsUncServer("\\\\192.168.1.5"));
    KITE_EXPECT(path::IsUncServer("\\\\192.168.1.5\\"));
    KITE_EXPECT_FALSE(path::IsUncServer("\\\\192.168.1.5\\public"));
    KITE_EXPECT_FALSE(path::IsUncServer("C:\\a"));
    KITE_EXPECT_FALSE(path::IsUncServer("\\\\"));

    KITE_EXPECT_EQ(path::UncServerLength("\\\\nas\\pub\\a"), size_t(5));
    KITE_EXPECT_EQ(path::UncServerLength("C:\\a"), size_t(0));
}

// What a credential prompt is aimed at: the connection, not the folder inside it.
KITE_TEST(path, unc_root_stops_at_the_share) {
    KITE_EXPECT_EQ(path::UncRoot("\\\\nas\\pub\\a\\b"), std::string("\\\\nas\\pub"));
    KITE_EXPECT_EQ(path::UncRoot("\\\\nas\\pub"), std::string("\\\\nas\\pub"));
    KITE_EXPECT_EQ(path::UncRoot("\\\\nas"), std::string("\\\\nas"));
    KITE_EXPECT_EQ(path::UncRoot("\\\\nas\\"), std::string("\\\\nas"));
    KITE_EXPECT_EQ(path::UncRoot("C:\\a"), std::string(""));
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
    // A server keeps its own spelling: there is no share to end, so the trailing
    // separator a drive root carries has no counterpart and is dropped.
    KITE_EXPECT_EQ(path::Normalize("//192.168.1.5/"), std::string("\\\\192.168.1.5"));
    KITE_EXPECT_EQ(path::Normalize("\\\\192.168.1.5"), std::string("\\\\192.168.1.5"));
    KITE_EXPECT_EQ(path::Normalize("\\\\nas/pub/a"), std::string("\\\\nas\\pub\\a"));
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

// A path long enough to need the extended form, laid out under `head`.
static std::string LongPath(const std::string& head, size_t length) {
    std::string out = head;
    while (out.size() < length) out += "abcdefghij\\";
    out.resize(length);
    return out;
}

KITE_TEST(path, short_paths_are_left_exactly_as_they_are) {
    // Win32 normalizes these itself, and the extended form takes that away, so
    // anything that fits inside the limit has to come back untouched.
    KITE_EXPECT_EQ(path::ToExtended("C:\\Users\\hiroki"), std::string("C:\\Users\\hiroki"));
    KITE_EXPECT_EQ(path::ToExtended("C:/a/../b"), std::string("C:/a/../b"));
    KITE_EXPECT_EQ(path::ToExtended(""), std::string(""));
}

KITE_TEST(path, long_drive_paths_get_the_extended_prefix) {
    const std::string p = LongPath("C:\\", 300);
    const std::string got = path::ToExtended(p);
    KITE_EXPECT_EQ(got.compare(0, 4, "\\\\?\\"), 0);
    // Normalize()d, not verbatim: the trailing separator this one ends on has
    // no meaning below a root, and nothing behind the prefix would strip it.
    KITE_EXPECT_EQ(got.substr(4), path::Normalize(p));
}

KITE_TEST(path, long_unc_paths_get_the_unc_spelling) {
    // \\server\share\... -> \\?\UNC\server\share\...  The plain prefix in front
    // of "\\server" names a device rather than a share and finds nothing.
    const std::string p = LongPath("\\\\nas\\share\\", 300);
    const std::string got = path::ToExtended(p);
    KITE_EXPECT_EQ(got.compare(0, 8, "\\\\?\\UNC\\"), 0);
    KITE_EXPECT_EQ(got.substr(8), path::Normalize(p).substr(2));
}

KITE_TEST(path, extending_settles_the_spelling_first) {
    // Nothing normalizes a path behind the prefix, so '/' and ".." have to be
    // gone before it goes on - otherwise a folder that opened fine at 200
    // characters stops opening at 300, which is the hardest kind of bug to see.
    const std::string p = LongPath("C:/a/b/../", 300) + "/./x";
    const std::string got = path::ToExtended(p);
    KITE_EXPECT_EQ(got.compare(0, 4, "\\\\?\\"), 0);
    KITE_EXPECT(got.find('/') == std::string::npos);
    KITE_EXPECT(got.find("\\..\\") == std::string::npos);
    KITE_EXPECT(got.find("\\.\\") == std::string::npos);
}

KITE_TEST(path, already_extended_paths_are_not_extended_twice) {
    const std::string p = LongPath("\\\\?\\C:\\", 300);
    KITE_EXPECT_EQ(path::ToExtended(p), p);
    // "\\.\" is the device namespace, and it is not a path to be rewritten either.
    const std::string device = LongPath("\\\\.\\PhysicalDrive0\\", 300);
    KITE_EXPECT_EQ(path::ToExtended(device), device);
}

KITE_TEST(path, relative_paths_cannot_be_extended) {
    // The extended form needs a full path, and this layer has no idea what the
    // current directory is. Handing the input back is the only honest answer.
    const std::string p = LongPath("relative\\", 300);
    KITE_EXPECT(path::ToExtended(p).compare(0, 4, "\\\\?\\") != 0);
}

KITE_TEST(path, the_threshold_is_measured_in_utf16_units) {
    // 100 Japanese characters are 300 bytes but only 100 units, which sits well
    // inside MAX_PATH: measuring bytes here would extend a path that fits.
    std::string p = "C:\\";
    for (int i = 0; i < 100; ++i) p += "\xE8\xB3\x87";
    KITE_EXPECT(p.size() > 240);
    KITE_EXPECT_EQ(path::ToExtended(p), p);
}

// The name a copy gets when it lands in the folder it came from. Not Explorer's
// spelling: this one has to stay ASCII and free of spaces, because it is a name
// on disk that gets typed at a command line - and it must not follow the display
// language, or switching languages would make the same operation produce a
// different name.
KITE_TEST(path, duplicate_name_marks_the_copy_without_spaces) {
    KITE_EXPECT_EQ(path::DuplicateName("notes.txt", 0, false), std::string("notes_copy.txt"));
    // Numbered from the second one on: the count the user sees starts at 2,
    // because the first copy is the unnumbered one.
    KITE_EXPECT_EQ(path::DuplicateName("notes.txt", 1, false), std::string("notes_copy2.txt"));
    KITE_EXPECT_EQ(path::DuplicateName("notes.txt", 4, false), std::string("notes_copy5.txt"));

    // No space, and nothing outside ASCII, at any attempt.
    for (int attempt = 0; attempt < 12; ++attempt) {
        const std::string name = path::DuplicateName("notes.txt", attempt, false);
        for (char c : name) {
            KITE_EXPECT(static_cast<unsigned char>(c) < 0x80);
            KITE_EXPECT_NE(c, ' ');
        }
    }
}

KITE_TEST(path, duplicate_name_keeps_the_extension_as_written) {
    // Extension() answers in lower case; rebuilding the name from it would
    // quietly rewrite ".TXT" on the way past.
    KITE_EXPECT_EQ(path::DuplicateName("REPORT.TXT", 0, false), std::string("REPORT_copy.TXT"));
    // Only the last dot separates: "archive.tar" keeps its first one.
    KITE_EXPECT_EQ(path::DuplicateName("archive.tar.gz", 0, false),
                   std::string("archive.tar_copy.gz"));
    // A leading dot is the whole name, not an extension.
    KITE_EXPECT_EQ(path::DuplicateName(".gitignore", 0, false), std::string(".gitignore_copy"));
    KITE_EXPECT_EQ(path::DuplicateName("Makefile", 0, false), std::string("Makefile_copy"));
}

KITE_TEST(path, a_folder_keeps_its_whole_name) {
    // Nobody thinks ".2026" is an extension, so a folder is not split at the
    // dot - the same call Cmd::Rename makes when it selects the stem.
    KITE_EXPECT_EQ(path::DuplicateName("backup.2026", 0, true), std::string("backup.2026_copy"));
    KITE_EXPECT_EQ(path::DuplicateName("backup.2026", 1, true), std::string("backup.2026_copy2"));
}

KITE_TEST(path, duplicate_name_answers_about_the_leaf_of_a_path) {
    KITE_EXPECT_EQ(path::DuplicateName("C:\\home\\notes.txt", 0, false),
                   std::string("notes_copy.txt"));
}

// 「下にあるか」は 2 か所が同じ答えを要る ─ ドロップ先の検査（自分の中へは落とせ
// ない）と、ファイル操作の衝突判定（同じ場所を 2 つの操作に渡さない）。文字列の
// 前方一致で書くと必ず兄弟を巻き込むので、判定は 1 か所に置いてある。
KITE_TEST(path, is_inside_finds_a_child_at_any_depth) {
    KITE_EXPECT(path::IsInside("C:\\home\\alpha", "C:\\home"));
    KITE_EXPECT(path::IsInside("C:\\home\\alpha\\nested\\deep.txt", "C:\\home"));
    KITE_EXPECT(path::IsInside("C:\\home", "C:\\"));
}

KITE_TEST(path, is_inside_is_false_for_the_same_place) {
    KITE_EXPECT_FALSE(path::IsInside("C:\\home", "C:\\home"));
    KITE_EXPECT_FALSE(path::IsInside("C:\\home\\", "C:\\home"));
    KITE_EXPECT_FALSE(path::IsInside("C:\\", "C:\\"));
}

KITE_TEST(path, is_inside_does_not_take_a_sibling_for_a_child) {
    // 前方一致だけで書くと、ここが必ず true になる。
    KITE_EXPECT_FALSE(path::IsInside("C:\\home\\alpha2", "C:\\home\\alpha"));
    KITE_EXPECT_FALSE(path::IsInside("C:\\homework", "C:\\home"));
}

KITE_TEST(path, is_inside_ignores_case_and_separator_style) {
    KITE_EXPECT(path::IsInside("c:/HOME/alpha", "C:\\home"));
    KITE_EXPECT(path::IsInside("C:\\home\\\\alpha", "C:/home/"));
}

KITE_TEST(path, is_inside_walks_a_unc_path) {
    KITE_EXPECT(path::IsInside("\\\\srv\\pub\\file.txt", "\\\\srv\\pub"));
    KITE_EXPECT(path::IsInside("\\\\srv\\pub", "\\\\srv"));
    KITE_EXPECT_FALSE(path::IsInside("\\\\srv\\pub2", "\\\\srv\\pub"));
}

KITE_TEST(path, is_inside_answers_no_when_either_side_is_empty) {
    KITE_EXPECT_FALSE(path::IsInside("", "C:\\home"));
    KITE_EXPECT_FALSE(path::IsInside("C:\\home", ""));
}
