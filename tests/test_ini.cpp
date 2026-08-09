#include "TestFramework.h"
#include "core/base/Ini.h"

using namespace kite;

KITE_TEST(ini, parses_sections_and_values) {
    Ini ini;
    ini.Parse("[ui]\ntheme = dark\nsidebar=true\n\n[view]\nsort=size\n");

    KITE_EXPECT_EQ(ini.GetStr("ui", "theme"), std::string("dark"));
    KITE_EXPECT_EQ(ini.GetBool("ui", "sidebar", false), true);
    KITE_EXPECT_EQ(ini.GetStr("view", "sort"), std::string("size"));
    KITE_EXPECT_EQ(ini.GetStr("view", "missing", "fallback"), std::string("fallback"));
}

KITE_TEST(ini, skips_comments_and_blank_lines) {
    Ini ini;
    ini.Parse("# comment\n; also a comment\n\n[a]\nk=v\n");
    KITE_EXPECT_EQ(ini.GetStr("a", "k"), std::string("v"));
    KITE_EXPECT_EQ(ini.sections().size(), size_t{ 1 });
}

KITE_TEST(ini, strips_a_utf8_bom) {
    Ini ini;
    ini.Parse("\xEF\xBB\xBF[a]\nk=v\n");
    KITE_EXPECT_EQ(ini.GetStr("a", "k"), std::string("v"));
}

KITE_TEST(ini, section_and_key_lookup_is_case_insensitive) {
    Ini ini;
    ini.Parse("[UI]\nTheme=dark\n");
    KITE_EXPECT_EQ(ini.GetStr("ui", "theme"), std::string("dark"));
}

KITE_TEST(ini, values_may_contain_equals_signs) {
    // Chords and paths both do; only the first '=' separates.
    Ini ini;
    ini.Parse("[keys]\nview.zoom=Ctrl+=\n");
    KITE_EXPECT_EQ(ini.GetStr("keys", "view.zoom"), std::string("Ctrl+="));
}

KITE_TEST(ini, append_keeps_duplicates_in_order) {
    Ini ini;
    ini.Append("keys", "nav.up", "Backspace");
    ini.Append("keys", "nav.up", "Alt+Up");

    const Ini::Section* section = ini.Find("keys");
    KITE_EXPECT(section != nullptr);
    KITE_EXPECT_EQ(section->entries.size(), size_t{ 2 });
    KITE_EXPECT_EQ(section->entries[0].value, std::string("Backspace"));
    KITE_EXPECT_EQ(section->entries[1].value, std::string("Alt+Up"));
    // GetStr returns the first match.
    KITE_EXPECT_EQ(ini.GetStr("keys", "nav.up"), std::string("Backspace"));
}

KITE_TEST(ini, set_replaces_rather_than_appends) {
    Ini ini;
    ini.Set("a", "k", "1");
    ini.Set("a", "k", "2");
    KITE_EXPECT_EQ(ini.Find("a")->entries.size(), size_t{ 1 });
    KITE_EXPECT_EQ(ini.GetStr("a", "k"), std::string("2"));
}

KITE_TEST(ini, serialize_round_trips) {
    Ini original;
    original.Set("ui", "theme", "dark");
    original.SetInt("window", "w", 1180);
    original.SetBool("ui", "sidebar", true);
    original.Append("keys", "tab.new", "Ctrl+T");
    original.Append("keys", "tab.new", "Ctrl+N");

    Ini reparsed;
    reparsed.Parse(original.Serialize());

    KITE_EXPECT_EQ(reparsed.GetStr("ui", "theme"), std::string("dark"));
    KITE_EXPECT_EQ(reparsed.GetInt("window", "w"), 1180);
    KITE_EXPECT_EQ(reparsed.GetBool("ui", "sidebar"), true);
    KITE_EXPECT_EQ(reparsed.Find("keys")->entries.size(), size_t{ 2 });
}

KITE_TEST(ini, bool_accepts_the_usual_spellings) {
    Ini ini;
    ini.Parse("[a]\nx1=true\nx2=YES\nx3=on\nx4=1\nx5=false\nx6=nonsense\n");
    KITE_EXPECT_EQ(ini.GetBool("a", "x1"), true);
    KITE_EXPECT_EQ(ini.GetBool("a", "x2"), true);
    KITE_EXPECT_EQ(ini.GetBool("a", "x3"), true);
    KITE_EXPECT_EQ(ini.GetBool("a", "x4"), true);
    KITE_EXPECT_EQ(ini.GetBool("a", "x5"), false);
    KITE_EXPECT_EQ(ini.GetBool("a", "x6"), false);
}

KITE_TEST(ini, clear_section_empties_without_removing_it) {
    Ini ini;
    ini.Set("a", "k", "v");
    ini.ClearSection("a");
    KITE_EXPECT(ini.Find("a") != nullptr);
    KITE_EXPECT_EQ(ini.Find("a")->entries.size(), size_t{ 0 });
}
