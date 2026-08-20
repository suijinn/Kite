// コマンドパレット（Ctrl+Shift+P）。キー割り当てを覚えていない人、あるいは和音を
// 常駐ソフトに奪われた人にとって、ここが全操作への唯一の入口になる ─ F1 の一覧は
// 読むためのもので、実行する経路を持っていない。画面自体は OS に触れないので、
// ここは全部ウィンドウ無しで動く。
#include "Fakes.h"
#include "TestFramework.h"
#include "core/app/CommandPalette.h"

using namespace kite;

namespace {

Strings English() {
    Strings str;
    str.Load("en");
    return str;
}

KeyMap Defaults() {
    KeyMap keys;
    keys.LoadDefaults();
    return keys;
}

// ブックマークを 3 件だけ持たせる ─ 番号で指す 8 個のうち、行き先が在る行と
// 空の行の両方を 1 つのパレットに並べるため。
std::vector<Bookmark> ThreeBookmarks() {
    std::vector<Bookmark> marks;
    marks.push_back({ "Projects", "C:\\work\\projects" });
    marks.push_back({ "Downloads", "C:\\home\\dl" });
    marks.push_back({ "", "C:\\nameless" });
    return marks;
}

CommandPalette Opened(const std::vector<Bookmark>& marks = {}) {
    CommandPalette palette;
    const Strings str = English();
    const KeyMap keys = Defaults();
    palette.Open(str, keys, marks);
    return palette;
}

void Type(CommandPalette& palette, const std::string& text) {
    for (char c : text) palette.HandleChar(static_cast<uint32_t>(static_cast<unsigned char>(c)));
}

// 目当てのコマンドの行番号。絞り込まれて消えていれば -1。
int RowOf(const CommandPalette& palette, Cmd cmd) {
    for (size_t i = 0; i < palette.rows().size(); ++i) {
        if (palette.rows()[i].cmd == cmd) return static_cast<int>(i);
    }
    return -1;
}

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

    Tab* tab() { return app.workspace().focusedTab(); }
    Pane* pane() { return app.workspace().focusedPane(); }
};

}  // namespace

// --- 画面そのもの -----------------------------------------------------------

// 「全コマンドを絞り込み実行」なので、行が無いコマンドがあってはならない。
KITE_TEST(palette, every_command_gets_a_row_except_its_own) {
    const CommandPalette palette = Opened();

    // 自分自身だけが 1 つ欠ける ─ 選んでも同じ画面が開き直るだけの行き止まり。
    KITE_EXPECT_EQ(palette.total(), static_cast<int>(AllCommands().size()) - 1);
    KITE_EXPECT_EQ(static_cast<int>(palette.rows().size()), palette.total());
    KITE_EXPECT_EQ(RowOf(palette, Cmd::ShowCommandPalette), -1);
    KITE_EXPECT(RowOf(palette, Cmd::NewTab) >= 0);
    KITE_EXPECT(RowOf(palette, Cmd::Quit) >= 0);
}

// 並びはコマンド表の定義順（F1 の一覧と同じ）。探している行が打鍵ごとに動かない。
KITE_TEST(palette, rows_follow_the_command_table_order) {
    const CommandPalette palette = Opened();
    KITE_EXPECT(RowOf(palette, Cmd::Quit) < RowOf(palette, Cmd::NewTab));
    KITE_EXPECT(RowOf(palette, Cmd::NewTab) < RowOf(palette, Cmd::Rename));
}

// 割り当てを知らない人のための画面なので、割り当ては行が言う。keys.ini 上の名前も同じ
// 理由で行が持つ ─ 絞り込みがそれに当たるのに、どこにも出ていないと当たること自体が
// 伝わらない。
KITE_TEST(palette, a_row_carries_its_label_name_group_and_chords) {
    const CommandPalette palette = Opened();
    const int row = RowOf(palette, Cmd::NewTab);
    KITE_EXPECT(row >= 0);
    KITE_EXPECT_EQ(palette.rows()[row].label, English().Get("cmd.new_tab"));
    KITE_EXPECT_EQ(palette.rows()[row].name, std::string("tab.new"));
    KITE_EXPECT_EQ(palette.rows()[row].group, English().Get("group.tab"));
    KITE_EXPECT_EQ(palette.rows()[row].chords, std::string("Ctrl+T"));
}

// 名前は CommandName() と同じ出どころでなければならない ─ 画面に出る綴りと keys.ini に
// 書く綴りが違えば、写した人のファイルが動かない。
KITE_TEST(palette, the_name_on_a_row_is_the_name_in_keys_ini) {
    const CommandPalette palette = Opened();
    for (const CommandPalette::Row& row : palette.rows()) {
        KITE_EXPECT_EQ(row.name, std::string(CommandName(row.cmd)));
    }
}

// 「ブックマーク 1 へ」はどのフォルダなのかを言っていない ─ 行き先の名前を添えて
// 初めて読める行になる。
KITE_TEST(palette, numbered_bookmark_rows_name_where_they_go) {
    const CommandPalette palette = Opened(ThreeBookmarks());
    const int row = RowOf(palette, Cmd::Bookmark1);
    KITE_EXPECT(row >= 0);
    KITE_EXPECT(palette.rows()[row].label.find("Projects") != std::string::npos);
    // ラベル本体は残る ─ 番号は Alt+Shift+1 の答えでもある。
    KITE_EXPECT(palette.rows()[row].label.find(English().Label("cmd.goto_bookmark_1")) !=
                std::string::npos);
}

// 名前の無いブックマークでも「どこか」は言える。
KITE_TEST(palette, a_nameless_bookmark_row_falls_back_to_its_path) {
    const CommandPalette palette = Opened(ThreeBookmarks());
    const int row = RowOf(palette, Cmd::Bookmark3);
    KITE_EXPECT(row >= 0);
    KITE_EXPECT(palette.rows()[row].label.find("nameless") != std::string::npos);
}

// 行き先の無い番号は、今までどおりのラベルだけ。空の行に «未設定» のような語を
// 増やしても、押して何も起きないことは変わらない。
KITE_TEST(palette, an_empty_bookmark_slot_keeps_its_plain_label) {
    const CommandPalette palette = Opened(ThreeBookmarks());
    const int row = RowOf(palette, Cmd::Bookmark8);
    KITE_EXPECT(row >= 0);
    KITE_EXPECT_EQ(palette.rows()[row].label, English().Label("cmd.goto_bookmark_8"));
}

// 添えた名前は絞り込みにも当たる ─ 8 個までは名前で引ける。
KITE_TEST(palette, a_bookmark_name_finds_its_numbered_row) {
    CommandPalette palette = Opened(ThreeBookmarks());
    Type(palette, "downloads");

    KITE_EXPECT(RowOf(palette, Cmd::Bookmark2) >= 0);
}

// **行は増えない。** ここが増え始めた時点で、パレットはコマンドの表ではなくなる ─
// 9 件目以降を探す場所は Ctrl+P の一覧のまま。
KITE_TEST(palette, bookmarks_do_not_add_rows_to_the_palette) {
    const CommandPalette bare = Opened();
    const CommandPalette withMarks = Opened(ThreeBookmarks());
    KITE_EXPECT_EQ(bare.total(), withMarks.total());
}

// 番号違いだけの 24 個は表 3 行で賄われているので、キーをそのまま引くと画面に
// `cmd.goto_session_1` が出る（実際にそうなっていた）。
KITE_TEST(palette, numbered_commands_get_their_number_filled_in) {
    const CommandPalette palette = Opened();
    const int row = RowOf(palette, Cmd::Session1);
    KITE_EXPECT(row >= 0);
    KITE_EXPECT_EQ(palette.rows()[row].label, English().Label("cmd.goto_session_1"));
    KITE_EXPECT(palette.rows()[row].label.find("cmd.") == std::string::npos);
    KITE_EXPECT(palette.rows()[row].label.find("{n}") == std::string::npos);
}

// 和音が 2 つあるものは 2 つ出す（F1 の「割り当ては全部並べる」と同じ）。
KITE_TEST(palette, all_chords_of_a_command_are_shown) {
    const CommandPalette palette = Opened();
    const int row = RowOf(palette, Cmd::Refresh);
    KITE_EXPECT(row >= 0);
    KITE_EXPECT_EQ(palette.rows()[row].chords, Defaults().ChordText(Cmd::Refresh));
    KITE_EXPECT(palette.rows()[row].chords.find(',') != std::string::npos);
}

KITE_TEST(palette, filtering_matches_the_label) {
    CommandPalette palette = Opened();
    Type(palette, "duplicate");

    KITE_EXPECT(static_cast<int>(palette.rows().size()) < palette.total());
    KITE_EXPECT(RowOf(palette, Cmd::DuplicateTab) >= 0);
}

// 設定ファイルで見た名前で探す人がいる。表示言語で動かない唯一の綴り。
KITE_TEST(palette, filtering_matches_the_ini_name) {
    CommandPalette palette = Opened();
    Type(palette, "tab.new");

    KITE_EXPECT_EQ(static_cast<int>(palette.rows().size()), 1);
    KITE_EXPECT_EQ(palette.rows()[0].cmd, Cmd::NewTab);
    KITE_EXPECT_EQ(palette.selectedCommand(), Cmd::NewTab);
}

// 分類でも当たる ─ 「タブについての操作を並べて見たい」に答えられる。
KITE_TEST(palette, filtering_matches_the_group) {
    CommandPalette palette = Opened();
    Type(palette, "session");

    KITE_EXPECT(RowOf(palette, Cmd::NewSession) >= 0);
    KITE_EXPECT(RowOf(palette, Cmd::MoveSessionLeft) >= 0);
    KITE_EXPECT_EQ(RowOf(palette, Cmd::NewTab), -1);
}

// 語尾でも当たる（前方一致にしない理由）。
KITE_TEST(palette, filtering_matches_in_the_middle_of_a_word) {
    CommandPalette palette = Opened();
    Type(palette, "hidden");
    KITE_EXPECT(RowOf(palette, Cmd::ToggleHidden) >= 0);
}

// 選択は行番号ではなくコマンドで覚える。打鍵ごとに行番号は振り直される。
KITE_TEST(palette, the_selection_stays_on_the_same_command_while_typing) {
    CommandPalette palette = Opened();
    Type(palette, "tab");
    const int row = RowOf(palette, Cmd::DuplicateTab);
    KITE_EXPECT(row >= 0);
    palette.SelectRow(row);
    KITE_EXPECT_EQ(palette.selectedCommand(), Cmd::DuplicateTab);

    // 絞り込みを足しても消えない限り選択はそのコマンドのまま。
    Type(palette, ".dup");
    KITE_EXPECT_EQ(palette.selectedCommand(), Cmd::DuplicateTab);
}

// 絞り込みで選択が消えたら先頭に落とす。行が並んでいるのに Enter が何もしない、
// という状態を作らない。
KITE_TEST(palette, a_filtered_away_selection_falls_to_the_top) {
    CommandPalette palette = Opened();
    palette.SelectRow(RowOf(palette, Cmd::Quit));
    Type(palette, "session");

    KITE_EXPECT(!palette.rows().empty());
    KITE_EXPECT_EQ(palette.cursor(), 0);
    KITE_EXPECT_EQ(palette.selectedCommand(), palette.rows()[0].cmd);
}

// 候補が 0 件のときの Enter は何もしない。打ち間違えたまま実行させない。
KITE_TEST(palette, enter_does_nothing_when_nothing_matches) {
    CommandPalette palette = Opened();
    Type(palette, "zzzznope");

    KITE_EXPECT(palette.rows().empty());
    KITE_EXPECT_EQ(palette.selectedCommand(), Cmd::None);
    KITE_EXPECT(palette.HandleKey(ParseChord("Enter")) == CommandPalette::Action::None);
}

// Escape は先に絞り込みを捨てる。打ち間違えた絞り込みのために開き直させない。
KITE_TEST(palette, escape_clears_the_filter_before_closing) {
    CommandPalette palette = Opened();
    Type(palette, "tab");

    KITE_EXPECT(palette.HandleKey(ParseChord("Escape")) == CommandPalette::Action::None);
    KITE_EXPECT(palette.filter().empty());
    KITE_EXPECT_EQ(static_cast<int>(palette.rows().size()), palette.total());
    KITE_EXPECT(palette.HandleKey(ParseChord("Escape")) == CommandPalette::Action::Close);
}

KITE_TEST(palette, backspace_shortens_the_filter) {
    CommandPalette palette = Opened();
    Type(palette, "tab.new");
    palette.HandleKey(ParseChord("Backspace"));

    KITE_EXPECT_EQ(palette.filter(), std::string("tab.ne"));
    KITE_EXPECT(RowOf(palette, Cmd::NewTab) >= 0);
}

// Enter だけが実行。修飾を足した Ctrl+Enter に «別の実行のしかた» は無い。
KITE_TEST(palette, only_enter_runs_a_command) {
    CommandPalette palette = Opened();
    KITE_EXPECT(palette.HandleKey(ParseChord("Enter")) == CommandPalette::Action::Run);
    KITE_EXPECT(palette.HandleKey(ParseChord("Ctrl+Enter")) == CommandPalette::Action::None);
}

// 表示中は全打鍵を飲み込む ─ コマンドを選んでいる最中に別のコマンドが暴発しない。
KITE_TEST(palette, unrelated_chords_are_swallowed) {
    CommandPalette palette = Opened();
    KITE_EXPECT(palette.HandleKey(ParseChord("Ctrl+T")) == CommandPalette::Action::None);
    KITE_EXPECT(palette.HandleKey(ParseChord("Delete")) == CommandPalette::Action::None);
    KITE_EXPECT(palette.HandleKey(ParseChord("Alt+Left")) == CommandPalette::Action::None);
}

KITE_TEST(palette, the_cursor_moves_and_stops_at_the_ends) {
    CommandPalette palette = Opened();
    KITE_EXPECT_EQ(palette.cursor(), 0);

    palette.HandleKey(ParseChord("Up"));
    KITE_EXPECT_EQ(palette.cursor(), 0);

    palette.HandleKey(ParseChord("Down"));
    KITE_EXPECT_EQ(palette.cursor(), 1);

    palette.HandleKey(ParseChord("End"));
    KITE_EXPECT_EQ(palette.cursor(), static_cast<int>(palette.rows().size()) - 1);

    palette.HandleKey(ParseChord("Home"));
    KITE_EXPECT_EQ(palette.cursor(), 0);
}

// 100 行を超える一覧なので、スクロールは選択を必ず画面内に連れてくる。
KITE_TEST(palette, the_view_follows_the_cursor) {
    CommandPalette palette = Opened();
    palette.SetPageRows(10);
    KITE_EXPECT_EQ(palette.scroll(), 0);

    palette.HandleKey(ParseChord("End"));
    KITE_EXPECT_EQ(palette.scroll(), static_cast<int>(palette.rows().size()) - 10);

    palette.HandleKey(ParseChord("Home"));
    KITE_EXPECT_EQ(palette.scroll(), 0);
}

// ホイールで離れたスクロールは、描く前に選択へ引き戻されない。
KITE_TEST(palette, a_wheel_scroll_survives_the_next_frame) {
    CommandPalette palette = Opened();
    palette.SetPageRows(10);
    palette.Scroll(5);
    KITE_EXPECT_EQ(palette.scroll(), 5);

    palette.SetPageRows(10);
    KITE_EXPECT_EQ(palette.scroll(), 5);
}

KITE_TEST(palette, a_closed_palette_answers_nothing) {
    CommandPalette palette;
    KITE_EXPECT_FALSE(palette.visible());
    KITE_EXPECT_FALSE(palette.HandleChar('a'));
    KITE_EXPECT(palette.HandleKey(ParseChord("Enter")) == CommandPalette::Action::None);
    KITE_EXPECT_EQ(palette.selectedCommand(), Cmd::None);
}

// --- App を通したところ ------------------------------------------------------

KITE_TEST(palette, the_key_that_opens_it_closes_it) {
    Harness h;

    KITE_EXPECT(h.app.OnKey(ParseChord("Ctrl+Shift+P")));
    KITE_EXPECT(h.app.commandPalette().visible());
    KITE_EXPECT(h.app.OnKey(ParseChord("Ctrl+Shift+P")));
    KITE_EXPECT_FALSE(h.app.commandPalette().visible());
}

// この画面の存在理由 ─ 割り当てを打たずにコマンドを実行できること。
KITE_TEST(palette, filtering_and_enter_runs_the_command) {
    Harness h;
    const size_t before = h.pane()->tabs.size();

    h.app.Execute(Cmd::ShowCommandPalette);
    for (char c : std::string("tab.new")) h.app.OnChar(static_cast<uint32_t>(c));
    KITE_EXPECT_EQ(static_cast<int>(h.app.commandPalette().rows().size()), 1);

    KITE_EXPECT(h.app.OnKey(ParseChord("Enter")));
    test::PumpUntilSettled(h.app);

    KITE_EXPECT_FALSE(h.app.commandPalette().visible());
    KITE_EXPECT_EQ(h.pane()->tabs.size(), before + 1);
}

// 入力欄を出すコマンドは、パレットが閉じた後の画面に出る。
KITE_TEST(palette, a_command_that_asks_for_a_name_opens_its_field) {
    Harness h;

    h.app.Execute(Cmd::ShowCommandPalette);
    for (char c : std::string("file.new_folder")) h.app.OnChar(static_cast<uint32_t>(c));
    KITE_EXPECT(h.app.OnKey(ParseChord("Enter")));

    KITE_EXPECT_FALSE(h.app.commandPalette().visible());
    KITE_EXPECT(h.app.prompt().active());
}

// マウスは 1 クリックで実行 ─ 押した人はもう決めている。
KITE_TEST(palette, a_click_runs_the_selected_command) {
    Harness h;
    const bool hidden = h.tab()->view.showHidden;

    h.app.Execute(Cmd::ShowCommandPalette);
    for (char c : std::string("view.toggle_hidden")) h.app.OnChar(static_cast<uint32_t>(c));
    KITE_EXPECT_EQ(static_cast<int>(h.app.commandPalette().rows().size()), 1);

    h.app.commandPalette().SelectRow(0);
    h.app.RunPaletteCommand();
    test::PumpUntilSettled(h.app);

    KITE_EXPECT_FALSE(h.app.commandPalette().visible());
    KITE_EXPECT_EQ(h.tab()->view.showHidden, !hidden);
}

// 開いている間、後ろの一覧は何も受け取らない。
KITE_TEST(palette, the_list_behind_it_gets_nothing) {
    Harness h;
    const size_t before = h.pane()->tabs.size();
    const std::string path = h.tab()->path;

    h.app.Execute(Cmd::ShowCommandPalette);
    KITE_EXPECT(h.app.OnKey(ParseChord("Ctrl+T")));
    KITE_EXPECT(h.app.OnKey(ParseChord("Alt+Up")));
    test::PumpUntilSettled(h.app);

    KITE_EXPECT(h.app.commandPalette().visible());
    KITE_EXPECT_EQ(h.pane()->tabs.size(), before);
    KITE_EXPECT_EQ(h.tab()->path, path);
}

// 打った文字も後ろへ落ちない（型入力ジャンプに食われない）。
KITE_TEST(palette, typed_characters_do_not_reach_the_type_ahead) {
    Harness h;
    const int cursor = h.tab()->cursor;

    h.app.Execute(Cmd::ShowCommandPalette);
    KITE_EXPECT(h.app.OnChar('t'));
    KITE_EXPECT_EQ(h.app.commandPalette().filter(), std::string("t"));
    KITE_EXPECT_EQ(h.tab()->cursor, cursor);
}

// Escape で閉じる（キーマップの Cmd::CancelOverlay 経由）。
KITE_TEST(palette, escape_closes_it_through_the_keymap) {
    Harness h;

    h.app.Execute(Cmd::ShowCommandPalette);
    for (char c : std::string("tab")) h.app.OnChar(static_cast<uint32_t>(c));

    // 1 度目は絞り込みだけを捨てる。
    KITE_EXPECT(h.app.OnKey(ParseChord("Escape")));
    KITE_EXPECT(h.app.commandPalette().visible());
    KITE_EXPECT(h.app.commandPalette().filter().empty());

    KITE_EXPECT(h.app.OnKey(ParseChord("Escape")));
    KITE_EXPECT_FALSE(h.app.commandPalette().visible());
}

// 他のオーバーレイと同居しない ─ 2 枚重なると、どちらが打鍵を受けるのか読めない。
KITE_TEST(palette, it_does_not_share_the_screen_with_another_overlay) {
    Harness h;

    h.app.Execute(Cmd::ShowCommandPalette);
    h.app.Execute(Cmd::ShowSettings);
    KITE_EXPECT_FALSE(h.app.commandPalette().visible());
    KITE_EXPECT(h.app.settingsEditor().visible());

    h.app.Execute(Cmd::ShowSettings);
    h.app.Execute(Cmd::ShowCommandPalette);
    h.app.Execute(Cmd::ShowKeyHelp);
    KITE_EXPECT_FALSE(h.app.commandPalette().visible());
    KITE_EXPECT(h.app.keyHelpVisible());

    h.app.Execute(Cmd::ShowKeyHelp);
    h.app.Execute(Cmd::ShowCommandPalette);
    h.app.Execute(Cmd::ShowKeySettings);
    KITE_EXPECT_FALSE(h.app.commandPalette().visible());
    KITE_EXPECT(h.app.keyEditor().visible());
}

// パレットから開くオーバーレイは、パレットを閉じた画面に出る。
KITE_TEST(palette, running_another_overlays_command_leaves_only_that_overlay) {
    Harness h;

    h.app.Execute(Cmd::ShowCommandPalette);
    for (char c : std::string("app.settings")) h.app.OnChar(static_cast<uint32_t>(c));
    KITE_EXPECT(h.app.OnKey(ParseChord("Enter")));

    KITE_EXPECT_FALSE(h.app.commandPalette().visible());
    KITE_EXPECT(h.app.settingsEditor().visible());
}

// 割り当ての無いコマンドがあっても行は出る ─ むしろそれがこの画面の使いどころ。
KITE_TEST(palette, an_unbound_command_still_has_a_row) {
    CommandPalette palette;
    const Strings str = English();
    KeyMap keys = Defaults();
    keys.UnbindCommand(Cmd::NewTab);
    palette.Open(str, keys, {});

    const int row = RowOf(palette, Cmd::NewTab);
    KITE_EXPECT(row >= 0);
    KITE_EXPECT(palette.rows()[row].chords.empty());
    KITE_EXPECT(palette.HandleKey(ParseChord("Enter")) == CommandPalette::Action::Run);
}
