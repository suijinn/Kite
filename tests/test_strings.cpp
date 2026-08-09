#include "TestFramework.h"
#include "core/i18n/Strings.h"
#include "core/input/Commands.h"

using namespace kite;

KITE_TEST(strings, english_is_the_default_for_unknown_codes) {
    Strings strings;
    strings.Load("xx-YY");
    KITE_EXPECT_EQ(strings.code(), std::string("en"));
    KITE_EXPECT_EQ(strings.Get("cmd.new_tab"), std::string("New tab"));
}

KITE_TEST(strings, japanese_overlays_english) {
    Strings strings;
    strings.Load("ja");
    KITE_EXPECT_EQ(strings.code(), std::string("ja"));
    KITE_EXPECT_NE(strings.Get("cmd.new_tab"), std::string("New tab"));
}

KITE_TEST(strings, missing_keys_return_the_key_itself) {
    Strings strings;
    strings.Load("en");
    KITE_EXPECT_EQ(strings.Get("no.such.key"), std::string("no.such.key"));
}

KITE_TEST(strings, numbered_labels_fall_back_to_the_generic_entry) {
    Strings strings;
    strings.Load("en");
    // Only "cmd.goto_tab_n" exists in the table; _3 has to derive from it.
    KITE_EXPECT_EQ(strings.Label("cmd.goto_tab_3"), std::string("Go to tab 3"));
    KITE_EXPECT_EQ(strings.Label("cmd.goto_session_7"), std::string("Go to session 7"));
    // An entry that really exists must not go through the fallback.
    KITE_EXPECT_EQ(strings.Label("cmd.goto_tab_last"), std::string("Go to last tab"));
}

KITE_TEST(strings, format_substitutes_positional_arguments) {
    Strings strings;
    strings.Load("en");
    KITE_EXPECT_EQ(strings.Format("ui.status_items", { "12" }), std::string("12 items"));
    KITE_EXPECT_EQ(strings.Format("ui.status_selected", { "3", "1.2 MB" }),
                   std::string("3 selected (1.2 MB)"));
}

KITE_TEST(strings, ini_overrides_replace_built_in_text) {
    Strings strings;
    strings.Load("en");

    Ini overrides;
    overrides.Set("strings", "cmd.new_tab", "Fresh tab");
    strings.ApplyOverrides(overrides);

    KITE_EXPECT_EQ(strings.Get("cmd.new_tab"), std::string("Fresh tab"));
}

KITE_TEST(strings, every_command_has_a_label_in_both_languages) {
    // A missing translation shows up as the raw key in the UI, so catch it here.
    for (const char* code : { "en", "ja" }) {
        Strings strings;
        strings.Load(code);
        for (const CommandInfo& info : AllCommands()) {
            const std::string label = strings.Label(info.labelKey);
            if (label == info.labelKey) {
                KITE_FAIL(std::string("no ") + code + " text for " + info.labelKey +
                          " (command " + info.name + ")");
            }
        }
    }
}
