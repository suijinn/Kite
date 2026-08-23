#include <set>

#include "TestFramework.h"
#include "core/input/Commands.h"
#include "core/i18n/Strings.h"
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
    // Shift adds the extended verbs, the way it does in Explorer. Those verbs are
    // the ones the shell hides on purpose, and handlers stock them accordingly -
    // TortoiseGit files "Git Clone..." there - so the plain menu is what the bare
    // key gives. Both halves are pinned: swapping one of the four silently costs
    // a menu nobody can reach any more.
    KITE_EXPECT_EQ(keys.Lookup(ParseChord("Menu")), Cmd::ContextMenu);
    KITE_EXPECT_EQ(keys.Lookup(ParseChord("Shift+F10")), Cmd::ExtendedContextMenu);
    KITE_EXPECT_EQ(keys.Lookup(ParseChord("Ctrl+Menu")), Cmd::FolderContextMenu);
    KITE_EXPECT_EQ(keys.Lookup(ParseChord("Ctrl+Shift+F10")), Cmd::ExtendedFolderContextMenu);
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

// The file Kite writes lists every command, so a line in it is how a key gets
// changed. Adding on top of the default left the default bound underneath and
// still first in insertion order - which is the chord the F1 sheet prints, so
// the edit showed up nowhere the user could see it.
KITE_TEST(keymap, ini_replaces_the_bindings_of_the_commands_it_names) {
    KeyMap keys;
    keys.LoadDefaults();

    Ini ini;
    ini.Append("keys", "pane.split_left_right", "Ctrl+Alt+V");
    keys.ApplyIni(ini);

    KITE_EXPECT_EQ(keys.Lookup(ParseChord("Ctrl+Alt+V")), Cmd::SplitLeftRight);
    KITE_EXPECT_EQ(keys.Lookup(ParseChord("Alt+V")), Cmd::None);
    KITE_EXPECT_EQ(keys.ChordsFor(Cmd::SplitLeftRight).size(), size_t{ 1 });
    KITE_EXPECT_EQ(keys.ChordText(Cmd::SplitLeftRight), std::string("Ctrl+Alt+V"));

    // And a command the file says nothing about keeps everything it had.
    KITE_EXPECT_EQ(keys.Lookup(ParseChord("Alt+H")), Cmd::SplitTopBottom);
}

KITE_TEST(keymap, several_lines_for_one_command_are_all_of_its_chords) {
    KeyMap keys;
    keys.LoadDefaults();

    Ini ini;
    ini.Append("keys", "nav.refresh", "F6");
    ini.Append("keys", "nav.refresh", "Ctrl+Alt+R");
    keys.ApplyIni(ini);

    KITE_EXPECT_EQ(keys.ChordsFor(Cmd::Refresh).size(), size_t{ 2 });
    KITE_EXPECT_EQ(keys.Lookup(ParseChord("F5")), Cmd::None);
    // The order in the file is the order on screen, and both of them are shown.
    KITE_EXPECT_EQ(keys.ChordText(Cmd::Refresh), std::string("F6, Ctrl+Alt+R"));
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

// What the editor writes has to survive being read back over the defaults, or
// a shortcut cleared on screen is back the next time Kite starts.
KITE_TEST(keymap, a_cleared_binding_stays_cleared_through_the_generated_ini) {
    KeyMap keys;
    keys.LoadDefaults();
    keys.UnbindCommand(Cmd::NextTab);
    keys.Bind(ParseChord("F6"), Cmd::Refresh);

    Ini written;
    written.Parse(keys.ToIni().Serialize());

    KeyMap reloaded;
    reloaded.LoadDefaults();
    reloaded.ApplyIni(written);

    KITE_EXPECT_EQ(reloaded.ChordsFor(Cmd::NextTab).size(), size_t{ 0 });
    KITE_EXPECT_EQ(reloaded.Lookup(ParseChord("Ctrl+Tab")), Cmd::None);
    KITE_EXPECT_EQ(reloaded.ChordsFor(Cmd::Refresh).size(), keys.ChordsFor(Cmd::Refresh).size());
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

KITE_TEST(keymap, one_chord_can_be_unbound_without_touching_the_others) {
    KeyMap keys;
    keys.LoadDefaults();

    keys.Unbind(ParseChord("Ctrl+PageDown"));

    KITE_EXPECT_EQ(keys.Lookup(ParseChord("Ctrl+PageDown")), Cmd::None);
    KITE_EXPECT_EQ(keys.Lookup(ParseChord("Ctrl+Tab")), Cmd::NextTab);
    KITE_EXPECT_EQ(keys.ChordText(Cmd::NextTab), std::string("Ctrl+Tab"));
}

KITE_TEST(keymap, the_display_text_lists_every_chord_in_order) {
    KeyMap keys;
    keys.LoadDefaults();
    KITE_EXPECT_EQ(keys.ChordText(Cmd::Refresh), std::string("F5, Ctrl+R"));
    KITE_EXPECT_EQ(keys.ChordText(Cmd::NewFolder), std::string("Ctrl+Shift+N"));
    KITE_EXPECT(keys.ChordText(Cmd::ToggleLanguage).empty());
}

// 名前を変えたコマンドの旧名も読む。読まないと、利用者が keys.ini に書いた割り当てが
// 「不明なコマンド」の警告 1 行と引き換えに静かに既定へ戻る。
KITE_TEST(keymap, an_old_command_name_still_binds) {
    KITE_EXPECT_EQ(CommandFromName("bookmark.list"), Cmd::ShowPlaces);
    KITE_EXPECT_EQ(CommandFromName("Bookmark.List"), Cmd::ShowPlaces);

    Ini ini;
    ini.Ensure("keys").entries.push_back({ "bookmark.list", "Alt+G" });

    KeyMap keys;
    keys.LoadDefaults();
    std::vector<std::string> warnings;
    keys.ApplyIni(ini, &warnings);

    KITE_EXPECT(warnings.empty());
    KITE_EXPECT_EQ(keys.Lookup(ParseChord("Alt+G")), Cmd::ShowPlaces);
    // 書かれたとおりに読むので、既定は残らない。
    KITE_EXPECT_EQ(keys.Lookup(ParseChord("Ctrl+P")), Cmd::None);
}

// 書き出すのは現在の名前だけ。旧名も並べると、読み戻したときファイルの中で
// 同じコマンドが 2 行を持つ。
KITE_TEST(keymap, the_written_file_uses_the_current_name) {
    KeyMap keys;
    keys.LoadDefaults();
    const Ini ini = keys.ToIni();

    const Ini::Section* sec = ini.Find("keys");
    KITE_EXPECT(sec != nullptr);
    bool sawCurrent = false;
    for (const Ini::Entry& e : sec->entries) {
        KITE_EXPECT_NE(e.key, std::string("bookmark.list"));
        if (e.key == "nav.places") sawCurrent = true;
    }
    KITE_EXPECT(sawCurrent);
}

// 設定に関わる 3 つはカンマの上に揃えてある。既定を動かすときは 3 つまとめて
// 考えるべきなので、隣り合っていることそのものを検査する。
KITE_TEST(keymap, the_configuration_screens_share_the_comma) {
    KeyMap keys;
    keys.LoadDefaults();
    KITE_EXPECT_EQ(keys.ChordText(Cmd::ShowSettings), std::string("Ctrl+,"));
    KITE_EXPECT_EQ(keys.ChordText(Cmd::ShowKeySettings), std::string("Ctrl+Shift+,"));
    KITE_EXPECT_EQ(keys.ChordText(Cmd::OpenConfigFolder), std::string("Ctrl+Alt+,"));
}

// ショートカット設定画面の和音を、表示文字列に書き写さないこと。
//
// **2 つの画面がその画面を指しており、どちらも和音を持っていない** ─ 既定は動くし
// （実際に `Ctrl+F1` から動いた）、利用者が割り当てを変えれば最初から嘘になる。
// 和音は `KeyMap::ChordText()` に訊く（KeyMap.cpp の既定表の注記）。
//
// **画面が自前で見るキーは対象外。** `ui.key_settings_hint` に並ぶ `Ctrl+Enter` や
// `Ctrl+R` はキーマップを通らず `KeyEditor::HandleKey` が直接見ているもので、
// 割り当てを変えられない ─ そちらは書いてあるとおりに動く。
KITE_TEST(keymap, no_display_string_spells_out_the_shortcut_editors_chord) {
    Strings str;
    for (const char* code : { "en", "ja" }) {
        str.Load(code);
        for (const Chord& chord : KeyMap::DefaultChordsFor(Cmd::ShowKeySettings)) {
            const std::string text = FormatChord(chord);
            KITE_EXPECT(!text.empty());
            for (const char* key : { "ui.key_help_hint", "ui.settings_hint",
                                     "ui.key_settings_hint", "ui.key_settings_title" }) {
                KITE_EXPECT(str.Get(key).find(text) == std::string::npos);
            }
        }
    }
    // 代わりに、和音を差し込む場所が在ること。ここが消えると案内が «どのキーで開くか»
    // を言わなくなり、黙っている画面に戻る。
    str.Load("en");
    KITE_EXPECT(str.Get("ui.key_help_hint").find("{0}") != std::string::npos);
    KITE_EXPECT(str.Get("ui.settings_hint").find("{0}") != std::string::npos);
    // 割り当てが無いときの逃げ道も要る（空白の空いた案内は、無い案内より悪い）。
    KITE_EXPECT(!str.Get("ui.key_help_hint_unbound").empty());
    KITE_EXPECT(!str.Get("ui.settings_hint_unbound").empty());
}
