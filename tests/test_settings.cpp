// The settings screen: the rows it builds, how a value moves, and what the App
// does with the one value that changed. The screen itself never touches the OS,
// so everything here runs without a window.
#include "Fakes.h"
#include "TestFramework.h"
#include "core/app/SettingsEditor.h"

using namespace kite;

namespace {

struct Harness {
    test::FakeFileSystem files;
    test::FakeShell shell;
    test::FakeHost host;
    App app;

    Harness() : app(files, shell, host) {
        test::ResetFakePlatform();
        test::PopulateStandardTree(files);
        app.Init({});
        test::PumpUntilSettled(app);
    }

    Pane* pane() { return app.workspace().focusedPane(); }

    // 設定画面を開き、目当ての行にカーソルを合わせる。
    void OpenAt(SettingId id) {
        app.Execute(Cmd::ShowSettings);
        SettingsEditor& editor = app.settingsEditor();
        for (size_t i = 0; i < editor.rows().size(); ++i) {
            if (editor.rows()[i].id == id) {
                editor.SelectRow(static_cast<int>(i));
                return;
            }
        }
        KITE_FAIL("no row for that setting");
    }

    std::string SettingsFile() {
        auto it = test::FakeFiles().find("C:\\home\\config\\settings.ini");
        return it == test::FakeFiles().end() ? std::string() : it->second;
    }
};

std::string ValueOf(const SettingsEditor& editor, SettingId id) {
    for (const SettingsEditor::Row& row : editor.rows()) {
        if (!row.header && row.id == id) return row.value;
    }
    return {};
}

}  // namespace

// --- the editor on its own --------------------------------------------------

KITE_TEST(settings, every_setting_gets_a_row_under_a_heading) {
    Strings strings;
    strings.Load("en");
    SettingsEditor editor;
    editor.Open(strings, SettingsValues{});

    int items = 0;
    int headers = 0;
    for (const SettingsEditor::Row& row : editor.rows()) {
        if (row.header) {
            ++headers;
            KITE_EXPECT(!row.label.empty());
        } else {
            ++items;
            KITE_EXPECT(!row.label.empty());
            KITE_EXPECT(!row.value.empty());
        }
    }
    KITE_EXPECT_EQ(items, static_cast<int>(SettingId::Count));
    KITE_EXPECT_EQ(headers, static_cast<int>(SettingGroup::Count));
    // 先頭は見出し行なので、カーソルはその下から始まる。
    KITE_EXPECT(editor.cursor() > 0);
    KITE_EXPECT(editor.selected() != SettingId::Count);
}

KITE_TEST(settings, the_cursor_never_lands_on_a_heading) {
    Strings strings;
    strings.Load("en");
    SettingsEditor editor;
    editor.Open(strings, SettingsValues{});

    const int rows = static_cast<int>(editor.rows().size());
    for (int i = 0; i < rows + 2; ++i) {
        editor.MoveCursor(1);
        KITE_EXPECT(!editor.rows()[static_cast<size_t>(editor.cursor())].header);
    }
    for (int i = 0; i < rows + 2; ++i) {
        editor.MoveCursor(-1);
        KITE_EXPECT(!editor.rows()[static_cast<size_t>(editor.cursor())].header);
    }
    editor.MoveCursor(0, true);  // Home: 先頭は見出し行
    KITE_EXPECT(!editor.rows()[static_cast<size_t>(editor.cursor())].header);
}

KITE_TEST(settings, arrows_stop_at_the_ends_and_enter_wraps) {
    Strings strings;
    strings.Load("en");
    SettingsEditor editor;
    editor.Open(strings, SettingsValues{});
    // テーマ（選択肢 2 つ）で確かめる。開いた直後の選択がこれ。
    KITE_EXPECT_EQ(static_cast<int>(editor.selected()), static_cast<int>(SettingId::Theme));

    KITE_EXPECT_FALSE(editor.Adjust(-1, strings));  // 先頭では動かない
    KITE_EXPECT(editor.Adjust(1, strings));
    KITE_EXPECT_EQ(editor.values().Get(SettingId::Theme), 1);
    KITE_EXPECT_FALSE(editor.Adjust(1, strings));  // 末尾でも動かない

    // Enter だけは回り込む ─ 押すたびに何かが起きないと切り替えにならない。
    KITE_EXPECT(editor.Adjust(1, strings, true));
    KITE_EXPECT_EQ(editor.values().Get(SettingId::Theme), 0);
}

KITE_TEST(settings, the_changed_id_names_one_setting_at_a_time) {
    Strings strings;
    strings.Load("en");
    SettingsEditor editor;
    editor.Open(strings, SettingsValues{});
    KITE_EXPECT_EQ(static_cast<int>(editor.changed()), static_cast<int>(SettingId::Count));

    editor.HandleKey(ParseChord("Right"), strings);
    KITE_EXPECT_EQ(static_cast<int>(editor.changed()), static_cast<int>(SettingId::Theme));
    editor.ClearChanged();
    KITE_EXPECT_EQ(static_cast<int>(editor.changed()), static_cast<int>(SettingId::Count));
}

KITE_TEST(settings, the_screen_swallows_every_key_but_closes_on_escape) {
    Strings strings;
    strings.Load("en");
    SettingsEditor editor;
    editor.Open(strings, SettingsValues{});

    KITE_EXPECT(editor.HandleKey(ParseChord("Ctrl+T"), strings));
    KITE_EXPECT(editor.visible());
    KITE_EXPECT(editor.HandleKey(ParseChord("Escape"), strings));
    KITE_EXPECT_FALSE(editor.visible());
    KITE_EXPECT_FALSE(editor.HandleKey(ParseChord("Right"), strings));
}

KITE_TEST(settings, the_font_scale_steps_match_the_keyboard_steps) {
    // Ctrl++ / Ctrl+- が作る倍率がそのまま選択肢に乗ること。ずれると、キーで
    // 出せる大きさが設定画面では選べないことになる。
    KITE_EXPECT_EQ(FontScaleIndex(1.0f), 3);
    KITE_EXPECT_NEAR(FontScaleValue(3), 1.0f, 0.0001f);
    KITE_EXPECT_NEAR(FontScaleValue(FontScaleIndex(1.2f)), 1.2f, 0.0001f);
    // 範囲外は端に丸める。
    KITE_EXPECT_EQ(FontScaleIndex(0.1f), 0);
    KITE_EXPECT_NEAR(FontScaleValue(99), 2.0f, 0.0001f);
}

// --- through the App --------------------------------------------------------

KITE_TEST(settings, new_tabs_go_to_the_end_by_default) {
    Harness h;
    h.app.Execute(Cmd::NewTab);
    h.app.Execute(Cmd::NewTab);
    KITE_EXPECT_EQ(static_cast<int>(h.app.newTabPosition()),
                   static_cast<int>(NewTabPosition::End));
    KITE_EXPECT_EQ(h.pane()->tabs.size(), size_t{ 3 });

    // 2 枚目に戻ってもう 1 枚作ると、末尾に付く。
    h.app.Execute(Cmd::Tab2);
    KITE_EXPECT_EQ(h.pane()->active, 1);
    h.app.Execute(Cmd::NewTab);
    KITE_EXPECT_EQ(h.pane()->active, 3);
}

KITE_TEST(settings, the_new_tab_position_setting_puts_a_tab_next_to_the_current_one) {
    Harness h;
    h.OpenAt(SettingId::NewTabPos);
    h.app.settingsEditor().Adjust(1, h.app.strings());
    h.app.ApplyPendingSetting();
    h.app.Execute(Cmd::ShowSettings);  // 閉じる
    KITE_EXPECT_EQ(static_cast<int>(h.app.newTabPosition()),
                   static_cast<int>(NewTabPosition::AfterCurrent));

    h.app.Execute(Cmd::NewTab);
    h.app.Execute(Cmd::NewTab);
    h.app.Execute(Cmd::Tab1);
    KITE_EXPECT_EQ(h.pane()->active, 0);
    h.app.Execute(Cmd::NewTab);
    // 末尾ではなく隣に入る。
    KITE_EXPECT_EQ(h.pane()->active, 1);
    KITE_EXPECT_EQ(h.pane()->tabs.size(), size_t{ 4 });

    // 設定は settings.ini に残る。
    KITE_EXPECT(h.SettingsFile().find("new_tab_position=after_current") != std::string::npos);
}

KITE_TEST(settings, the_tab_bar_starts_above_the_list_and_can_be_moved_beside_it) {
    Harness h;
    KITE_EXPECT_EQ(static_cast<int>(h.app.tabBarPosition()), static_cast<int>(TabBarPosition::Top));

    h.OpenAt(SettingId::TabBarPos);
    h.app.settingsEditor().Adjust(1, h.app.strings());
    h.app.ApplyPendingSetting();
    KITE_EXPECT_EQ(static_cast<int>(h.app.tabBarPosition()),
                   static_cast<int>(TabBarPosition::Left));
    KITE_EXPECT(h.SettingsFile().find("tab_bar_position=left") != std::string::npos);

    // 上下 1 つの選択肢しか無いので、戻すのも同じ 1 手。
    h.app.settingsEditor().Adjust(-1, h.app.strings());
    h.app.ApplyPendingSetting();
    KITE_EXPECT_EQ(static_cast<int>(h.app.tabBarPosition()), static_cast<int>(TabBarPosition::Top));
}

KITE_TEST(settings, the_saved_tab_bar_position_is_read_back_on_the_next_run) {
    Harness h;
    test::FakeFiles()["C:\\home\\config\\settings.ini"] = "[ui]\ntab_bar_position=left\n";
    h.app.Execute(Cmd::ReloadConfig);
    KITE_EXPECT_EQ(static_cast<int>(h.app.tabBarPosition()),
                   static_cast<int>(TabBarPosition::Left));
}

KITE_TEST(settings, the_saved_position_is_read_back_on_the_next_run) {
    Harness h;
    test::FakeFiles()["C:\\home\\config\\settings.ini"] = "[ui]\nnew_tab_position=after_current\n";
    h.app.Execute(Cmd::ReloadConfig);
    KITE_EXPECT_EQ(static_cast<int>(h.app.newTabPosition()),
                   static_cast<int>(NewTabPosition::AfterCurrent));
}

KITE_TEST(settings, changing_the_theme_on_the_screen_rebuilds_the_theme) {
    Harness h;
    KITE_EXPECT(h.app.theme().dark);
    h.OpenAt(SettingId::Theme);
    h.app.OnKey(ParseChord("Right"));
    KITE_EXPECT_FALSE(h.app.theme().dark);
    // 文字サイズの倍率は据え置かれる ─ テーマは毎回 3 段階で組み直す。
    h.app.OnKey(ParseChord("Escape"));
    KITE_EXPECT_FALSE(h.app.settingsEditor().visible());
}

KITE_TEST(settings, the_screen_takes_the_keyboard_while_it_is_up) {
    Harness h;
    const size_t before = h.pane()->tabs.size();
    h.app.Execute(Cmd::ShowSettings);
    // Ctrl+T は既定で「新しいタブ」。設定中に暴発してはいけない。
    h.app.OnKey(ParseChord("Ctrl+T"));
    KITE_EXPECT_EQ(h.pane()->tabs.size(), before);
    // 文字入力も裏へ抜けない。
    KITE_EXPECT(h.app.OnChar('a'));

    // 開いたキーがそのまま閉じるキー。
    h.app.OnKey(ParseChord("Ctrl+Comma"));
    KITE_EXPECT_FALSE(h.app.settingsEditor().visible());
}

KITE_TEST(settings, only_one_overlay_is_up_at_a_time) {
    Harness h;
    h.app.Execute(Cmd::ShowSettings);
    h.app.Execute(Cmd::ShowKeySettings);
    KITE_EXPECT_FALSE(h.app.settingsEditor().visible());
    KITE_EXPECT(h.app.keyEditor().visible());

    h.app.Execute(Cmd::ShowSettings);
    KITE_EXPECT(h.app.settingsEditor().visible());
    KITE_EXPECT_FALSE(h.app.keyEditor().visible());

    h.app.Execute(Cmd::ShowKeyHelp);
    KITE_EXPECT_FALSE(h.app.settingsEditor().visible());
    KITE_EXPECT(h.app.keyHelpVisible());
}

KITE_TEST(settings, the_screen_shows_the_values_that_are_actually_in_force) {
    Harness h;
    // 画面の外で変えた設定が、開いたときの表示に出ること。
    h.app.Execute(Cmd::ToggleTheme);
    h.app.Execute(Cmd::ShowSettings);
    Strings en;
    en.Load(h.app.strings().code());
    KITE_EXPECT_EQ(ValueOf(h.app.settingsEditor(), SettingId::Theme),
                   en.Get("settings.theme.light"));
}

KITE_TEST(settings, a_user_language_code_survives_a_change_to_another_setting) {
    // 選択肢に無い言語（lang.fr.ini を置いた人）を、別の行をいじっただけで
    // 「自動」に書き換えてしまわないこと。
    Harness h;
    test::FakeFiles()["C:\\home\\config\\settings.ini"] = "[ui]\nlanguage=fr\n";
    h.app.Execute(Cmd::ReloadConfig);

    h.OpenAt(SettingId::Sidebar);
    h.app.OnKey(ParseChord("Left"));
    h.app.OnKey(ParseChord("Escape"));

    KITE_EXPECT(h.SettingsFile().find("language=fr") != std::string::npos);
}
