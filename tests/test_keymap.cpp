#include <set>

#include "TestFramework.h"
#include "core/input/Commands.h"
#include "core/input/KeyMap.h"

using namespace kite;

KITE_TEST(keymap, parses_modifier_combinations) {
    const Chord chord = ParseChord("Ctrl+Shift+T");
    KITE_EXPECT_EQ(chord.key, Key::T);
    KITE_EXPECT_EQ(int(chord.mods), int(kModCtrl | kModShift));
}

KITE_TEST(keymap, parsing_is_case_insensitive_and_accepts_aliases) {
    KITE_EXPECT_EQ(ParseChord("ctrl+t").key, Key::T);
    KITE_EXPECT_EQ(ParseChord("Esc").key, Key::Escape);
    KITE_EXPECT_EQ(ParseChord("PgDn").key, Key::PageDown);
    KITE_EXPECT_EQ(ParseChord("Return").key, Key::Enter);
}

KITE_TEST(keymap, rejects_nonsense) {
    KITE_EXPECT_FALSE(ParseChord("Ctrl+NotAKey").valid());
    KITE_EXPECT_FALSE(ParseChord("").valid());
    KITE_EXPECT_FALSE(ParseChord("Ctrl").valid());  // modifier with no key
}

KITE_TEST(keymap, format_round_trips) {
    for (const char* text : { "Ctrl+T", "Ctrl+Alt+Shift+C", "F5", "Alt+Left", "Escape" }) {
        const Chord chord = ParseChord(text);
        KITE_EXPECT(chord.valid());
        KITE_EXPECT_EQ(ParseChord(FormatChord(chord)), chord);
    }
}

KITE_TEST(keymap, defaults_resolve_expected_commands) {
    KeyMap keys;
    keys.LoadDefaults();

    KITE_EXPECT_EQ(keys.Lookup(ParseChord("Ctrl+T")), Cmd::NewTab);
    KITE_EXPECT_EQ(keys.Lookup(ParseChord("Alt+V")), Cmd::SplitLeftRight);
    KITE_EXPECT_EQ(keys.Lookup(ParseChord("Alt+H")), Cmd::SplitTopBottom);
    KITE_EXPECT_EQ(keys.Lookup(ParseChord("Alt+1")), Cmd::Session1);
    KITE_EXPECT_EQ(keys.Lookup(ParseChord("Ctrl+1")), Cmd::Tab1);
    KITE_EXPECT_EQ(keys.Lookup(ParseChord("Alt+Shift+1")), Cmd::Bookmark1);
    // The whole point of the app: the extended menu is one key away.
    KITE_EXPECT_EQ(keys.Lookup(ParseChord("Menu")), Cmd::ExtendedContextMenu);
}

KITE_TEST(keymap, unbound_chords_resolve_to_none) {
    KeyMap keys;
    keys.LoadDefaults();
    KITE_EXPECT_EQ(keys.Lookup(ParseChord("Ctrl+Alt+Shift+F9")), Cmd::None);
}

KITE_TEST(keymap, defaults_contain_no_duplicate_chords) {
    // Two commands on one chord means one of them is silently unreachable.
    KeyMap keys;
    keys.LoadDefaults();

    std::set<uint32_t> seen;
    for (const CommandInfo& info : AllCommands()) {
        for (const Chord& chord : keys.ChordsFor(info.id)) {
            if (!seen.insert(chord.packed()).second) {
                KITE_FAIL("chord " + FormatChord(chord) + " is bound more than once (" +
                          info.name + ")");
            }
        }
    }
}

KITE_TEST(keymap, ini_adds_a_binding_without_removing_defaults) {
    KeyMap keys;
    keys.LoadDefaults();

    Ini ini;
    ini.Append("keys", "pane.split_left_right", "Ctrl+Alt+V");
    keys.ApplyIni(ini);

    KITE_EXPECT_EQ(keys.Lookup(ParseChord("Ctrl+Alt+V")), Cmd::SplitLeftRight);
    KITE_EXPECT_EQ(keys.Lookup(ParseChord("Alt+V")), Cmd::SplitLeftRight);
}

KITE_TEST(keymap, ini_none_clears_every_binding_for_a_command) {
    KeyMap keys;
    keys.LoadDefaults();

    Ini ini;
    ini.Append("keys", "tab.next", "none");
    keys.ApplyIni(ini);

    KITE_EXPECT_EQ(keys.Lookup(ParseChord("Ctrl+Tab")), Cmd::None);
    KITE_EXPECT_EQ(keys.Lookup(ParseChord("Ctrl+PageDown")), Cmd::None);
    KITE_EXPECT_EQ(keys.ChordsFor(Cmd::NextTab).size(), size_t{ 0 });
}

KITE_TEST(keymap, none_is_applied_before_additions_regardless_of_line_order) {
    // Users write the clear and the new binding in whatever order feels
    // natural; the result must not depend on it.
    KeyMap keys;
    keys.LoadDefaults();

    Ini ini;
    ini.Append("keys", "tab.new", "Ctrl+Shift+N");
    ini.Append("keys", "tab.new", "none");
    keys.ApplyIni(ini);

    KITE_EXPECT_EQ(keys.Lookup(ParseChord("Ctrl+Shift+N")), Cmd::NewTab);
    KITE_EXPECT_EQ(keys.Lookup(ParseChord("Ctrl+T")), Cmd::None);
}

KITE_TEST(keymap, rebinding_a_chord_steals_it_from_the_previous_command) {
    KeyMap keys;
    keys.LoadDefaults();

    Ini ini;
    ini.Append("keys", "file.new_folder", "Ctrl+T");
    keys.ApplyIni(ini);

    KITE_EXPECT_EQ(keys.Lookup(ParseChord("Ctrl+T")), Cmd::NewFolder);
    // NewTab must no longer claim it, or the help sheet lies.
    for (const Chord& chord : keys.ChordsFor(Cmd::NewTab)) {
        KITE_EXPECT_NE(chord, ParseChord("Ctrl+T"));
    }
}

KITE_TEST(keymap, unknown_command_names_are_reported_not_silently_dropped) {
    KeyMap keys;
    keys.LoadDefaults();

    Ini ini;
    ini.Append("keys", "does.not.exist", "Ctrl+9");
    std::vector<std::string> warnings;
    keys.ApplyIni(ini, &warnings);

    KITE_EXPECT_EQ(warnings.size(), size_t{ 1 });
}

KITE_TEST(keymap, generated_ini_reloads_to_the_same_mapping) {
    // The generated keys.ini doubles as the user's reference, so it has to be
    // a faithful description of the defaults.
    KeyMap defaults;
    defaults.LoadDefaults();

    Ini written;
    written.Parse(defaults.ToIni().Serialize());

    KeyMap reloaded;
    reloaded.ApplyIni(written);

    for (const CommandInfo& info : AllCommands()) {
        KITE_EXPECT_EQ(reloaded.ChordsFor(info.id).size(), defaults.ChordsFor(info.id).size());
    }
    KITE_EXPECT_EQ(reloaded.Lookup(ParseChord("Ctrl+T")), Cmd::NewTab);
    KITE_EXPECT_EQ(reloaded.Lookup(ParseChord("Alt+Shift+8")), Cmd::Bookmark8);
}

KITE_TEST(keymap, command_names_are_unique_and_resolvable) {
    std::set<std::string> names;
    for (const CommandInfo& info : AllCommands()) {
        KITE_EXPECT(names.insert(info.name).second);
        KITE_EXPECT_EQ(CommandFromName(info.name), info.id);
        KITE_EXPECT_EQ(std::string(CommandName(info.id)), std::string(info.name));
    }
    KITE_EXPECT_EQ(CommandFromName("nope"), Cmd::None);
}
