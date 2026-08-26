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

KITE_TEST(settings, the_base_font_size_options_are_whole_pixels) {
    // 選択肢がそのまま `[ui] font_size` の取りうる値。1 きざみの整数でない値が
    // 動いていると、行が言う大きさと実際の大きさが食い違う。
    KITE_EXPECT_NEAR(FontSizeValue(FontSizeIndex(kDefaultFontSize)), kDefaultFontSize, 0.0001f);
    KITE_EXPECT_EQ(FontSizeIndex(kDefaultFontSize + 1.0f), FontSizeIndex(kDefaultFontSize) + 1);
    // 半端な値は最も近い選択肢へ丸める。
    KITE_EXPECT_EQ(FontSizeIndex(13.4f), FontSizeIndex(13.0f));
    // 範囲外は端に丸める。
    KITE_EXPECT_EQ(FontSizeIndex(1.0f), 0);
    KITE_EXPECT_NEAR(FontSizeValue(999), FontSizeValue(SettingOptionCount(SettingId::FontSize) - 1),
                     0.0001f);
}

KITE_TEST(settings, the_two_text_size_rows_carry_their_own_units) {
    // 「数値ならパーセント」で済ませていた頃の名残が残っていると、既定のサイズが
    // 1300 % として出る。
    Strings strings;
    strings.Load("en");
    SettingsEditor editor;
    editor.Open(strings, SettingsValues{});

    KITE_EXPECT_EQ(SettingOptionText(strings, SettingId::FontSize, FontSizeIndex(13.0f)),
                   std::string("13 px"));
    KITE_EXPECT_EQ(SettingOptionText(strings, SettingId::FontScale, FontScaleIndex(1.0f)),
                   std::string("100%"));
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

KITE_TEST(settings, the_base_font_size_takes_the_rows_up_with_it) {
    // 器を据え置いて文字だけ大きくすると下端から切れる ─ font_size を ini の
    // 生の値として渡していた頃がその形だった（Theme::Scale の注記）。
    Harness h;
    const float row = h.app.theme().rowHeight;
    h.OpenAt(SettingId::FontSize);
    for (int i = 0; i < 5; ++i) h.app.OnKey(ParseChord("Right"));

    KITE_EXPECT_NEAR(h.app.fontSize(), kDefaultFontSize + 5.0f, 0.001);
    KITE_EXPECT_NEAR(h.app.theme().fontSize, kDefaultFontSize + 5.0f, 0.001);
    KITE_EXPECT(h.app.theme().rowHeight > row);
    KITE_EXPECT(h.app.theme().sidebarWidth > Theme::Dark().sidebarWidth);
}

KITE_TEST(settings, the_zoom_multiplies_the_base_size_and_ctrl_zero_returns_to_it) {
    // 利用者が言っている «既定のフォントサイズ» はここ ─ Ctrl++ が掛かる相手で、
    // Ctrl+0 が戻る先。組み込みの 13 px に戻ってしまってはこの行を置いた意味が無い。
    Harness h;
    h.OpenAt(SettingId::FontSize);
    for (int i = 0; i < 3; ++i) h.app.OnKey(ParseChord("Right"));
    h.app.OnKey(ParseChord("Escape"));
    const float base = h.app.fontSize();
    KITE_EXPECT_NEAR(base, kDefaultFontSize + 3.0f, 0.001);

    h.app.Execute(Cmd::FontLarger);
    KITE_EXPECT_NEAR(h.app.theme().fontSize, base * h.app.fontScale(), 0.001);
    KITE_EXPECT(h.app.theme().fontSize > base);

    h.app.Execute(Cmd::FontReset);
    KITE_EXPECT_NEAR(h.app.fontScale(), 1.0f, 0.001);
    KITE_EXPECT_NEAR(h.app.theme().fontSize, base, 0.001);
}

KITE_TEST(settings, changing_the_base_size_keeps_the_zoom) {
    // 動かしているのは «100 % がどの大きさか» なので、拡大したままの人の拡大を
    // 巻き添えにしない。
    Harness h;
    h.app.Execute(Cmd::FontLarger);
    const float zoom = h.app.fontScale();
    KITE_EXPECT(zoom > 1.0f);

    h.OpenAt(SettingId::FontSize);
    h.app.OnKey(ParseChord("Right"));
    KITE_EXPECT_NEAR(h.app.fontScale(), zoom, 0.001);
    KITE_EXPECT_NEAR(h.app.theme().fontSize, (kDefaultFontSize + 1.0f) * zoom, 0.001);
}

KITE_TEST(settings, the_base_font_size_survives_a_restart) {
    Harness h;
    h.OpenAt(SettingId::FontSize);
    h.app.OnKey(ParseChord("Right"));
    KITE_EXPECT(h.SettingsFile().find("font_size") != std::string::npos);

    test::FakeFiles()["C:\\home\\config\\settings.ini"] = "[ui]\nfont_size=18\n";
    h.app.Execute(Cmd::ReloadConfig);
    // 読み直したら画面は畳む ─ 開いたままの行が出しているのは差し替わる前の値。
    KITE_EXPECT_FALSE(h.app.settingsEditor().visible());
    KITE_EXPECT_NEAR(h.app.fontSize(), 18.0f, 0.001);
    KITE_EXPECT_NEAR(h.app.theme().fontSize, 18.0f, 0.001);
    // 行もその値を出す ─ 実際に効いている大きさと画面が食い違わない。
    h.app.Execute(Cmd::ShowSettings);
    KITE_EXPECT_EQ(ValueOf(h.app.settingsEditor(), SettingId::FontSize), std::string("18 px"));
}

KITE_TEST(settings, an_ini_size_the_screen_cannot_show_is_rounded_to_one_it_can) {
    Harness h;
    test::FakeFiles()["C:\\home\\config\\settings.ini"] = "[ui]\nfont_size=13.4\n";
    h.app.Execute(Cmd::ReloadConfig);
    KITE_EXPECT_NEAR(h.app.fontSize(), 13.0f, 0.001);

    test::FakeFiles()["C:\\home\\config\\settings.ini"] = "[ui]\nfont_size=400\n";
    h.app.Execute(Cmd::ReloadConfig);
    KITE_EXPECT_NEAR(h.app.fontSize(),
                     FontSizeValue(SettingOptionCount(SettingId::FontSize) - 1), 0.001);
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

// --- the default file manager ------------------------------------------------
//
// 設定画面で唯一、実体が settings.ini の外（OS 側）にある行。そして唯一、
// 変更に確認を挟む行 ─ 効いたかどうかが Kite の中からは見えないので、矢印キーが
// かすっただけで Win+E の行き先が変わってはならない。

// 行は覚えている値ではなく OS に訊いた答えを出す。Kite の外で変えられていても
// 嘘をつかない。
KITE_TEST(settings, the_default_manager_row_reads_the_answer_from_the_os) {
    Harness h;
    h.shell.defaultManager = DefaultManager::Yes;
    h.app.Execute(Cmd::ShowSettings);
    Strings str;
    str.Load(h.app.strings().code());
    KITE_EXPECT_EQ(ValueOf(h.app.settingsEditor(), SettingId::DefaultManager),
                   str.Get("settings.yes"));

    // 別の場所の Kite が持っているのは「この exe が既定」ではない。
    h.app.Execute(Cmd::ShowSettings);  // 閉じる
    h.shell.defaultManager = DefaultManager::Other;
    h.app.Execute(Cmd::ShowSettings);
    KITE_EXPECT_EQ(ValueOf(h.app.settingsEditor(), SettingId::DefaultManager),
                   str.Get("settings.no"));
}

// 値を動かしただけでは何も書かない ─ 確認に「はい」と答えて初めて動く。
KITE_TEST(settings, changing_the_row_asks_before_it_writes) {
    Harness h;
    h.OpenAt(SettingId::DefaultManager);

    h.app.OnKey(ParseChord("Right"));
    KITE_EXPECT(h.shell.defaultManagerCalls.empty());
    KITE_EXPECT(h.app.prompt().isConfirm());
    // 設定画面は開いたまま ─ 質問は行に属している。
    KITE_EXPECT(h.app.settingsEditor().visible());

    KITE_EXPECT(h.app.OnKey(ParseChord("Enter")));
    KITE_EXPECT_EQ(h.shell.defaultManagerCalls.size(), size_t{ 1 });
    KITE_EXPECT(h.shell.defaultManagerCalls.back());
    KITE_EXPECT(h.shell.defaultManager == DefaultManager::Yes);
    KITE_EXPECT_FALSE(h.app.prompt().active());
}

// 「いいえ」と答えたら、動かした行も元に戻る ─ 起きなかったことを行が主張しない。
KITE_TEST(settings, saying_no_puts_the_row_back) {
    Harness h;
    h.OpenAt(SettingId::DefaultManager);
    Strings str;
    str.Load(h.app.strings().code());

    h.app.OnKey(ParseChord("Right"));
    KITE_EXPECT_EQ(ValueOf(h.app.settingsEditor(), SettingId::DefaultManager),
                   str.Get("settings.yes"));

    KITE_EXPECT(h.app.OnKey(ParseChord("Escape")));
    KITE_EXPECT(h.shell.defaultManagerCalls.empty());
    KITE_EXPECT_FALSE(h.app.prompt().active());
    // 設定画面はまだ開いている ─ Escape は先に質問を捨てる。
    KITE_EXPECT(h.app.settingsEditor().visible());
    KITE_EXPECT_EQ(ValueOf(h.app.settingsEditor(), SettingId::DefaultManager),
                   str.Get("settings.no"));
}

// OS が断ったときも同じ ─ 行は実際にそうなっている値へ戻る。
KITE_TEST(settings, a_refused_registration_puts_the_row_back) {
    Harness h;
    h.shell.defaultManagerSucceeds = false;
    h.OpenAt(SettingId::DefaultManager);
    Strings str;
    str.Load(h.app.strings().code());

    h.app.OnKey(ParseChord("Right"));
    h.app.OnKey(ParseChord("Enter"));

    KITE_EXPECT_EQ(h.shell.defaultManagerCalls.size(), size_t{ 1 });
    KITE_EXPECT_EQ(h.app.statusMessage(), h.app.strings().Get("ui.default_manager_failed"));
    KITE_EXPECT_EQ(ValueOf(h.app.settingsEditor(), SettingId::DefaultManager),
                   str.Get("settings.no"));
}

// 登録の口が無い環境では確認すら出さない。
KITE_TEST(settings, no_place_to_register_is_a_sentence_not_a_question) {
    Harness h;
    h.shell.defaultManager = DefaultManager::Unsupported;
    h.OpenAt(SettingId::DefaultManager);

    h.app.OnKey(ParseChord("Right"));
    KITE_EXPECT_FALSE(h.app.prompt().active());
    KITE_EXPECT(h.shell.defaultManagerCalls.empty());
    KITE_EXPECT_EQ(h.app.statusMessage(),
                   h.app.strings().Get("ui.default_manager_unsupported"));
}

// 解除も同じ道を通る。行 1 つなので、ラベルが嘘になる余地が無い。
KITE_TEST(settings, clearing_the_registration_goes_through_the_same_row) {
    Harness h;
    h.shell.defaultManager = DefaultManager::Yes;
    h.OpenAt(SettingId::DefaultManager);

    h.app.OnKey(ParseChord("Left"));
    KITE_EXPECT(h.app.prompt().isConfirm());
    h.app.OnKey(ParseChord("Enter"));

    KITE_EXPECT_EQ(h.shell.defaultManagerCalls.size(), size_t{ 1 });
    KITE_EXPECT_FALSE(h.shell.defaultManagerCalls.back());
    KITE_EXPECT(h.shell.defaultManager == DefaultManager::No);
}

// この行は settings.ini に何も残さない ─ 実体は OS の側にある。
KITE_TEST(settings, the_default_manager_row_writes_nothing_to_the_ini) {
    Harness h;
    h.OpenAt(SettingId::DefaultManager);
    h.app.OnKey(ParseChord("Right"));
    h.app.OnKey(ParseChord("Enter"));

    KITE_EXPECT(h.SettingsFile().find("default_manager") == std::string::npos);
}

// 質問が出ている間、後ろの設定画面は打鍵を受け取らない ─ 「はい」がどの行の話か
// 分からなくなる。
KITE_TEST(settings, the_settings_screen_gets_nothing_while_the_question_is_up) {
    Harness h;
    h.OpenAt(SettingId::DefaultManager);
    const int before = h.app.settingsEditor().cursor();

    h.app.OnKey(ParseChord("Right"));
    h.app.OnKey(ParseChord("Up"));
    h.app.OnKey(ParseChord("Down"));

    KITE_EXPECT_EQ(h.app.settingsEditor().cursor(), before);
    KITE_EXPECT(h.app.prompt().isConfirm());
}

// exe を移した後は登録が死んでいるが、画面はそれを言う機会を持たない ─ 起動時の
// 1 行だけがその機会。他のアプリが持っている場合は言わない（事故ではないので）。
KITE_TEST(settings, a_stale_registration_is_reported_at_startup) {
    test::FakeFileSystem files;
    test::FakeShell shell;
    test::FakeHost host;
    test::ResetFakePlatform();
    test::PopulateStandardTree(files);
    shell.defaultManager = DefaultManager::Other;

    App app(files, shell, host);
    app.Init({});
    test::PumpUntilSettled(app);
    KITE_EXPECT_EQ(app.statusMessage(), app.strings().Get("ui.default_manager_moved"));
}

KITE_TEST(settings, a_registration_held_by_another_app_is_not_reported_at_startup) {
    test::FakeFileSystem files;
    test::FakeShell shell;
    test::FakeHost host;
    test::ResetFakePlatform();
    test::PopulateStandardTree(files);
    shell.defaultManager = DefaultManager::No;

    App app(files, shell, host);
    app.Init({});
    test::PumpUntilSettled(app);
    KITE_EXPECT_NE(app.statusMessage(), app.strings().Get("ui.default_manager_moved"));
}
