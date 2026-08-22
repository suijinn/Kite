// 行き先の一覧（Ctrl+P）。ブックマーク・開いているタブ・ドライブを並べる «次にどこへ»
// の窓口で、
// 番号ショートカットが 8 個で打ち止めなので、その先へキーボードだけで届けるかどうかが
// この画面の存在理由。画面自体は OS に触れないので、ここは全部ウィンドウ無しで動く。
#include "Fakes.h"
#include "TestFramework.h"
#include "core/app/PlacePicker.h"

using namespace kite;

namespace {

std::vector<Bookmark> TenBookmarks() {
    std::vector<Bookmark> marks;
    marks.push_back({ "alpha", "C:\\home\\alpha" });
    marks.push_back({ "beta", "C:\\home\\beta" });
    for (int i = 3; i <= 9; ++i) {
        marks.push_back({ "mark" + std::to_string(i), "C:\\work\\p" + std::to_string(i) });
    }
    // 9 件目までが番号ショートカットの外側。10 件目は名前で探すしかない。
    marks.push_back({ "nested", "C:\\home\\alpha\\nested" });
    return marks;
}

KeyMap Defaults() {
    KeyMap keys;
    keys.LoadDefaults();
    return keys;
}

Strings English() {
    Strings str;
    str.Load("en");
    return str;
}

// ブックマークだけを並べた一覧。タブとドライブの行を見るテストだけがそれを渡す。
void OpenWith(PlacePicker& picker, const std::vector<Bookmark>& marks,
              const std::string& currentPath = {},
              const std::vector<PlacePicker::OpenTab>& tabs = {},
              const std::vector<fs::Root>& drives = {}) {
    picker.Open(English(), Defaults(), marks, tabs, drives, currentPath);
}

// サイドバーに出ているのと同じ 2 台。
std::vector<fs::Root> TwoDrives() {
    std::vector<fs::Root> drives;
    fs::Root c;
    c.path = "C:\\";
    c.label = "Windows (C:)";
    c.kind = fs::RootKind::Fixed;
    drives.push_back(c);
    fs::Root d;
    d.path = "D:\\";
    d.label = "Data (D:)";
    d.kind = fs::RootKind::Fixed;
    drives.push_back(d);
    return drives;
}

// 目当ての行番号。無ければ -1。
int RowOf(const PlacePicker& picker, const std::string& name) {
    for (size_t i = 0; i < picker.rows().size(); ++i) {
        if (picker.rows()[i].name == name) return static_cast<int>(i);
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
        files.AddDir("C:\\work");
        for (int i = 3; i <= 9; ++i) files.AddDir("C:\\work\\p" + std::to_string(i));
        app.Init({});
        test::PumpUntilSettled(app);
    }

    Tab* tab() { return app.workspace().focusedTab(); }
};

}  // namespace

// --- 画面そのもの -----------------------------------------------------------

KITE_TEST(placepicker, every_bookmark_gets_a_row_in_order) {
    PlacePicker picker;
    OpenWith(picker, TenBookmarks(), {});

    KITE_EXPECT(picker.visible());
    KITE_EXPECT_EQ(static_cast<int>(picker.rows().size()), 10);
    KITE_EXPECT_EQ(picker.total(), 10);
    for (size_t i = 0; i < picker.rows().size(); ++i) {
        KITE_EXPECT_EQ(picker.rows()[i].index, static_cast<int>(i));
        KITE_EXPECT(!picker.rows()[i].name.empty());
        KITE_EXPECT(!picker.rows()[i].path.empty());
    }
}

// 番号を持つのは先頭 8 件だけ。9 件目以降が空なのは不足ではなく、この画面が
// 要る理由そのもの。
KITE_TEST(placepicker, only_the_first_eight_carry_a_number_shortcut) {
    PlacePicker picker;
    OpenWith(picker, TenBookmarks(), {});

    for (int i = 0; i < 8; ++i) {
        KITE_EXPECT(!picker.rows()[static_cast<size_t>(i)].chords.empty());
    }
    KITE_EXPECT(picker.rows()[8].chords.empty());
    KITE_EXPECT(picker.rows()[9].chords.empty());
    // 1 件目は Alt+Shift+1、8 件目は Alt+Shift+8 ─ 添字の足し算ではなく表を引いている
    // ことの確認も兼ねる。
    KITE_EXPECT(picker.rows()[0].chords.find("Alt+Shift+1") != std::string::npos);
    KITE_EXPECT(picker.rows()[7].chords.find("Alt+Shift+8") != std::string::npos);
}

KITE_TEST(placepicker, opens_on_the_folder_already_being_looked_at) {
    PlacePicker picker;
    OpenWith(picker, TenBookmarks(), "C:\\work\\p5");
    KITE_EXPECT_EQ(picker.selectedIndex(), 4);

    // 大文字小文字は畳む（Windows のパスなので）。
    OpenWith(picker, TenBookmarks(), "c:\\HOME\\Beta");
    KITE_EXPECT_EQ(picker.selectedIndex(), 1);

    // どれでもないところから開いたら先頭。Enter が何もしない状態にはしない。
    OpenWith(picker, TenBookmarks(), "C:\\elsewhere");
    KITE_EXPECT_EQ(picker.selectedIndex(), 0);
}

KITE_TEST(placepicker, the_filter_matches_the_name_and_the_path) {
    PlacePicker picker;
    OpenWith(picker, TenBookmarks(), {});

    picker.HandleChar('n');
    picker.HandleChar('e');
    // 名前で "nested"、パスで "C:\home\alpha\nested" ─ どちらも同じ 1 件。
    KITE_EXPECT_EQ(static_cast<int>(picker.rows().size()), 1);
    KITE_EXPECT_EQ(picker.rows()[0].name, std::string("nested"));

    // 前方一致ではなく部分一致。深いパスの末尾で探せないと意味が無い。
    OpenWith(picker, TenBookmarks(), {});
    for (char c : std::string("work")) picker.HandleChar(static_cast<uint32_t>(c));
    KITE_EXPECT_EQ(static_cast<int>(picker.rows().size()), 7);

    // 大文字小文字は畳む。
    OpenWith(picker, TenBookmarks(), {});
    for (char c : std::string("ALPHA")) picker.HandleChar(static_cast<uint32_t>(c));
    KITE_EXPECT_EQ(static_cast<int>(picker.rows().size()), 2);  // alpha と nested
}

// 絞り込むたびに行番号は振り直されるので、選択は「行」ではなく「ブックマーク」で
// 覚えていなければならない。
KITE_TEST(placepicker, the_selection_follows_the_bookmark_not_the_row) {
    PlacePicker picker;
    OpenWith(picker, TenBookmarks(), {});

    picker.SelectRow(RowOf(picker, "nested"));
    KITE_EXPECT_EQ(picker.selectedIndex(), 9);

    // 打っても選択は同じブックマークに残る。行番号のほうは 9 → 0 に変わる。
    for (char c : std::string("nest")) picker.HandleChar(static_cast<uint32_t>(c));
    KITE_EXPECT_EQ(picker.selectedIndex(), 9);
    KITE_EXPECT_EQ(picker.cursor(), 0);

    // 消しても戻ってくる。
    picker.HandleKey(ParseChord("Backspace"));
    picker.HandleKey(ParseChord("Backspace"));
    picker.HandleKey(ParseChord("Backspace"));
    picker.HandleKey(ParseChord("Backspace"));
    KITE_EXPECT(picker.filter().empty());
    KITE_EXPECT_EQ(picker.selectedIndex(), 9);
}

// 選択が絞り込みで消えたら先頭へ落とす。行が並んでいるのに Enter が何もしない、
// という状態を作らない。
KITE_TEST(placepicker, a_filtered_away_selection_falls_to_the_top) {
    PlacePicker picker;
    OpenWith(picker, TenBookmarks(), {});
    picker.SelectRow(RowOf(picker, "nested"));

    for (char c : std::string("work")) picker.HandleChar(static_cast<uint32_t>(c));
    KITE_EXPECT_EQ(picker.cursor(), 0);
    KITE_EXPECT_EQ(picker.selectedIndex(), 2);  // mark3
}

KITE_TEST(placepicker, nothing_matching_leaves_no_row_and_no_answer) {
    PlacePicker picker;
    OpenWith(picker, TenBookmarks(), {});
    for (char c : std::string("zzz")) picker.HandleChar(static_cast<uint32_t>(c));

    KITE_EXPECT(picker.rows().empty());
    KITE_EXPECT_EQ(picker.cursor(), -1);
    KITE_EXPECT_EQ(picker.selectedIndex(), -1);
    // 空振りの Enter が「先頭を開く」になってはならない。
    KITE_EXPECT(picker.HandleKey(ParseChord("Enter")) == PlacePicker::Action::None);
    // 総数は絞り込みでは減らない ─ 残りがまだ在ることを言えるのはこれだけ。
    KITE_EXPECT_EQ(picker.total(), 10);
}

KITE_TEST(placepicker, escape_drops_the_filter_first_then_closes) {
    PlacePicker picker;
    OpenWith(picker, TenBookmarks(), {});
    for (char c : std::string("work")) picker.HandleChar(static_cast<uint32_t>(c));

    KITE_EXPECT(picker.HandleKey(ParseChord("Escape")) == PlacePicker::Action::None);
    KITE_EXPECT(picker.filter().empty());
    KITE_EXPECT_EQ(static_cast<int>(picker.rows().size()), 10);

    KITE_EXPECT(picker.HandleKey(ParseChord("Escape")) == PlacePicker::Action::Close);
}

KITE_TEST(placepicker, enter_opens_and_ctrl_enter_asks_for_a_new_tab) {
    PlacePicker picker;
    OpenWith(picker, TenBookmarks(), {});
    KITE_EXPECT(picker.HandleKey(ParseChord("Enter")) == PlacePicker::Action::Open);
    KITE_EXPECT(picker.HandleKey(ParseChord("Ctrl+Enter")) == PlacePicker::Action::OpenNewTab);
}

KITE_TEST(placepicker, the_cursor_stops_at_both_ends) {
    PlacePicker picker;
    OpenWith(picker, TenBookmarks(), {});

    for (int i = 0; i < 20; ++i) picker.HandleKey(ParseChord("Down"));
    KITE_EXPECT_EQ(picker.cursor(), 9);
    for (int i = 0; i < 20; ++i) picker.HandleKey(ParseChord("Up"));
    KITE_EXPECT_EQ(picker.cursor(), 0);

    picker.HandleKey(ParseChord("End"));
    KITE_EXPECT_EQ(picker.cursor(), 9);
    picker.HandleKey(ParseChord("Home"));
    KITE_EXPECT_EQ(picker.cursor(), 0);
}

// 表示中は打鍵を残らず飲み込む。移動先を選んでいる最中にタブが増えては、
// 選ばせたことにならない。
KITE_TEST(placepicker, unrelated_chords_are_swallowed) {
    PlacePicker picker;
    OpenWith(picker, TenBookmarks(), {});
    KITE_EXPECT(picker.HandleKey(ParseChord("Ctrl+T")) == PlacePicker::Action::None);
    KITE_EXPECT(picker.HandleKey(ParseChord("Delete")) == PlacePicker::Action::None);
    KITE_EXPECT(picker.visible());
}

KITE_TEST(placepicker, scrolling_stays_inside_the_list) {
    PlacePicker picker;
    OpenWith(picker, TenBookmarks(), {});
    picker.SetPageRows(4);

    picker.Scroll(100);
    KITE_EXPECT_EQ(picker.scroll(), 6);  // 10 行 - 4 行
    picker.Scroll(-100);
    KITE_EXPECT_EQ(picker.scroll(), 0);

    // カーソルを下端まで動かせば追いかける。
    picker.HandleKey(ParseChord("End"));
    KITE_EXPECT_EQ(picker.scroll(), 6);
}

// --- 開いているタブの行 ------------------------------------------------------

namespace {

std::vector<PlacePicker::OpenTab> TwoTabs() {
    std::vector<PlacePicker::OpenTab> tabs;
    tabs.push_back({ 0, 1, true, "beta", "C:\\home\\beta" });
    tabs.push_back({ 1, 0, false, "docs", "C:\\work\\docs" });
    return tabs;
}

int RowOfPath(const PlacePicker& picker, const std::string& path) {
    for (size_t i = 0; i < picker.rows().size(); ++i) {
        if (picker.rows()[i].path == path) return static_cast<int>(i);
    }
    return -1;
}

}  // namespace

// ブックマークが先、タブが後 ─ ブックマークを探しに来た手が変わらないように。
KITE_TEST(placepicker, bookmarks_come_first_and_open_tabs_after) {
    PlacePicker picker;
    OpenWith(picker, TenBookmarks(), {}, TwoTabs());

    KITE_EXPECT_EQ(picker.total(), 12);
    for (int i = 0; i < 10; ++i) {
        KITE_EXPECT(picker.rows()[i].kind == PlacePicker::Kind::Bookmark);
    }
    KITE_EXPECT(picker.rows()[10].kind == PlacePicker::Kind::Tab);
    KITE_EXPECT_EQ(picker.rows()[10].name, std::string("beta"));
}

// タブの行はペインとタブの位置を持つ ─ 移動先はパスではなくそのタブ自身。
KITE_TEST(placepicker, a_tab_row_points_at_the_tab_not_the_path) {
    PlacePicker picker;
    OpenWith(picker, {}, {}, TwoTabs());

    const int row = RowOfPath(picker, "C:\\work\\docs");
    KITE_EXPECT(row >= 0);
    picker.SelectRow(row);
    KITE_EXPECT(picker.selectedRow() != nullptr);
    KITE_EXPECT_EQ(picker.selectedRow()->pane, 1);
    KITE_EXPECT_EQ(picker.selectedRow()->tab, 0);
    // ブックマークの添字は持たない ─ ブックマークではないので。
    KITE_EXPECT_EQ(picker.selectedIndex(), -1);
}

// Ctrl+<数字> が届くのはフォーカス中のペインだけなので、そこにしか和音は出さない。
KITE_TEST(placepicker, only_tabs_in_the_focused_pane_show_a_number_chord) {
    PlacePicker picker;
    OpenWith(picker, {}, {}, TwoTabs());

    KITE_EXPECT_EQ(picker.rows()[0].chords, Defaults().ChordText(Cmd::Tab2));
    KITE_EXPECT(picker.rows()[1].chords.empty());
}

// 種別は行に出るし、絞り込みにも当たる。
KITE_TEST(placepicker, the_kind_is_on_the_row_and_can_be_filtered_on) {
    PlacePicker picker;
    OpenWith(picker, TenBookmarks(), {}, TwoTabs());

    KITE_EXPECT_EQ(picker.rows()[0].kindLabel, English().Get("ui.goto_kind_bookmark"));
    KITE_EXPECT_EQ(picker.rows()[10].kindLabel, English().Get("ui.goto_kind_tab"));

    for (char c : std::string("open tab")) picker.HandleChar(static_cast<uint32_t>(c));
    KITE_EXPECT_EQ(static_cast<int>(picker.rows().size()), 2);
    for (const PlacePicker::Row& row : picker.rows()) {
        KITE_EXPECT(row.kind == PlacePicker::Kind::Tab);
    }
}

// すでに開いているタブに «新しいタブで開く» は無い ─ 黙って飲み込む。
KITE_TEST(placepicker, ctrl_enter_does_nothing_on_a_tab_row) {
    PlacePicker picker;
    OpenWith(picker, {}, {}, TwoTabs());

    KITE_EXPECT(picker.HandleKey(ParseChord("Ctrl+Enter")) == PlacePicker::Action::None);
    KITE_EXPECT(picker.HandleKey(ParseChord("Enter")) == PlacePicker::Action::Open);
}

// ブックマークの行では今までどおり効く。
KITE_TEST(placepicker, ctrl_enter_still_opens_a_bookmark_in_a_new_tab) {
    PlacePicker picker;
    OpenWith(picker, TenBookmarks(), {}, TwoTabs());

    KITE_EXPECT(picker.HandleKey(ParseChord("Ctrl+Enter")) == PlacePicker::Action::OpenNewTab);
}

// --- App を通したところ ------------------------------------------------------

KITE_TEST(placepicker, nine_or_more_bookmarks_are_reachable_from_the_keyboard) {
    Harness h;
    h.app.workspace().bookmarks = TenBookmarks();

    h.app.Execute(Cmd::ShowPlaces);
    KITE_EXPECT(h.app.placePicker().visible());

    // 10 件目 ─ Alt+Shift+1..8 のどれでも届かないところ。名前で絞って Enter。
    KITE_EXPECT(h.app.OnChar('n'));
    KITE_EXPECT(h.app.OnChar('e'));
    KITE_EXPECT(h.app.OnChar('s'));
    KITE_EXPECT_EQ(static_cast<int>(h.app.placePicker().rows().size()), 1);

    KITE_EXPECT(h.app.OnKey(ParseChord("Enter")));
    test::PumpUntilSettled(h.app);

    KITE_EXPECT_FALSE(h.app.placePicker().visible());
    KITE_EXPECT_EQ(h.tab()->path, std::string("C:\\home\\alpha\\nested"));
}

KITE_TEST(placepicker, ctrl_enter_opens_the_bookmark_in_a_new_tab) {
    Harness h;
    h.app.workspace().bookmarks = TenBookmarks();
    const size_t before = h.app.workspace().focusedPane()->tabs.size();

    h.app.Execute(Cmd::ShowPlaces);
    for (char c : std::string("beta")) h.app.OnChar(static_cast<uint32_t>(c));
    KITE_EXPECT(h.app.OnKey(ParseChord("Ctrl+Enter")));
    test::PumpUntilSettled(h.app);

    KITE_EXPECT_FALSE(h.app.placePicker().visible());
    KITE_EXPECT_EQ(h.app.workspace().focusedPane()->tabs.size(), before + 1);
    KITE_EXPECT_EQ(h.tab()->path, std::string("C:\\home\\beta"));
}

// ドライブが並ぶようになったので、行き先が 1 つも無い状態は実機では起こらない ─
// ブックマークが 0 件で、開いているタブが今いる 1 枚だけでも、ドライブが残る。
// （空の一覧は「押しても何も起きない」と同じで、しかも閉じる手間だけ増えるので、
// 本当に 0 件なら開かずにステータス行で言う。その道は残してある。）
KITE_TEST(placepicker, drives_alone_are_enough_to_open_the_list) {
    Harness h;
    h.app.workspace().bookmarks.clear();

    h.app.Execute(Cmd::ShowPlaces);
    KITE_EXPECT(h.app.placePicker().visible());
    KITE_EXPECT_EQ(static_cast<int>(h.app.placePicker().rows().size()), 1);
    KITE_EXPECT(h.app.placePicker().rows()[0].kind == PlacePicker::Kind::Drive);
    KITE_EXPECT_EQ(h.app.placePicker().rows()[0].path, std::string("C:\\"));
}

KITE_TEST(placepicker, the_key_that_opens_it_closes_it) {
    Harness h;
    h.app.workspace().bookmarks = TenBookmarks();

    // 押すのは「今この画面に割り当てられている和音」。既定の和音は移りうる
    // （ROADMAP P3-12 の «和音の格上げ»）ので、和音そのものを書くと、移した日に
    // トグルの検査が「キーが違う」で落ちる ─ 試しているのはトグルのほう。
    const std::vector<Chord> chords = KeyMap::DefaultChordsFor(Cmd::ShowPlaces);
    KITE_EXPECT(!chords.empty());
    KITE_EXPECT(h.app.OnKey(chords.front()));
    KITE_EXPECT(h.app.placePicker().visible());
    KITE_EXPECT(h.app.OnKey(chords.front()));
    KITE_EXPECT_FALSE(h.app.placePicker().visible());
}

// 開いている間、後ろの一覧は何も受け取らない。
KITE_TEST(placepicker, the_list_behind_it_gets_nothing) {
    Harness h;
    h.app.workspace().bookmarks = TenBookmarks();
    const size_t before = h.app.workspace().focusedPane()->tabs.size();
    const std::string path = h.tab()->path;

    h.app.Execute(Cmd::ShowPlaces);
    KITE_EXPECT(h.app.OnKey(ParseChord("Ctrl+T")));
    KITE_EXPECT(h.app.OnKey(ParseChord("Alt+Up")));
    test::PumpUntilSettled(h.app);

    KITE_EXPECT(h.app.placePicker().visible());
    KITE_EXPECT_EQ(h.app.workspace().focusedPane()->tabs.size(), before);
    KITE_EXPECT_EQ(h.tab()->path, path);
}

// 他のオーバーレイと同居しない ─ 2 枚重なると、どちらが打鍵を受けるのか読めない。
KITE_TEST(placepicker, opening_another_overlay_closes_it) {
    Harness h;
    h.app.workspace().bookmarks = TenBookmarks();

    h.app.Execute(Cmd::ShowPlaces);
    h.app.Execute(Cmd::ShowSettings);
    KITE_EXPECT_FALSE(h.app.placePicker().visible());
    KITE_EXPECT(h.app.settingsEditor().visible());

    h.app.Execute(Cmd::ShowSettings);
    h.app.Execute(Cmd::ShowPlaces);
    h.app.Execute(Cmd::ShowKeyHelp);
    KITE_EXPECT_FALSE(h.app.placePicker().visible());
    KITE_EXPECT(h.app.keyHelpVisible());
}

// 開いてあるタブを選んだら、そのタブへ移る ─ 同じフォルダをもう 1 枚開くのでは
// 答えになっていない。
KITE_TEST(placepicker, choosing_an_open_tab_switches_to_it) {
    Harness h;
    h.app.Execute(Cmd::NewTab);
    test::PumpUntilSettled(h.app);
    h.app.NavigateFocused("C:\\home\\beta");
    test::PumpUntilSettled(h.app);
    const size_t tabs = h.app.workspace().focusedPane()->tabs.size();
    const int active = h.app.workspace().focusedPane()->active;

    h.app.Execute(Cmd::ShowPlaces);
    KITE_EXPECT(h.app.placePicker().visible());
    // 今いるタブは並んでいないので、残っているのはもう 1 枚のほう（＋ドライブ 1 台）。
    KITE_EXPECT_EQ(static_cast<int>(h.app.placePicker().rows().size()),
                   static_cast<int>(tabs) - 1 + 1);

    KITE_EXPECT(h.app.OnKey(ParseChord("Enter")));
    test::PumpUntilSettled(h.app);

    KITE_EXPECT_FALSE(h.app.placePicker().visible());
    // タブは増えていない。アクティブなタブが変わっただけ。
    KITE_EXPECT_EQ(h.app.workspace().focusedPane()->tabs.size(), tabs);
    KITE_EXPECT_NE(h.app.workspace().focusedPane()->active, active);
}

// ブックマークが 0 件でも、開いているタブがあれば開く。
KITE_TEST(placepicker, open_tabs_alone_are_enough_to_open_the_list) {
    Harness h;
    h.app.workspace().bookmarks.clear();
    h.app.Execute(Cmd::NewTab);
    test::PumpUntilSettled(h.app);

    h.app.Execute(Cmd::ShowPlaces);
    KITE_EXPECT(h.app.placePicker().visible());
    // もう 1 枚のタブと、ドライブ 1 台。
    KITE_EXPECT_EQ(static_cast<int>(h.app.placePicker().rows().size()), 2);
    KITE_EXPECT(h.app.placePicker().rows()[0].kind == PlacePicker::Kind::Tab);
}

// --- ドライブ ----------------------------------------------------------------

// 番号ショートカットもサイドバーも使わずにドライブへ届く道はここしかない ─
// キーボードだけで «どのディスクがあるのか» を尋ねられる場所が他に無い。
KITE_TEST(placepicker, drives_are_listed_after_the_bookmarks_and_the_tabs) {
    PlacePicker picker;
    OpenWith(picker, TenBookmarks(), {}, TwoTabs(), TwoDrives());

    const std::vector<PlacePicker::Row>& rows = picker.rows();
    KITE_EXPECT_EQ(static_cast<int>(rows.size()), 10 + 2 + 2);
    // ブックマークが先、開いているタブ、そしてドライブ。ドライブを上に出すと、
    // いつも同じ顔ぶれの «背景» が、実際に開いているものを画面の下へ押しやる。
    KITE_EXPECT(rows[0].kind == PlacePicker::Kind::Bookmark);
    KITE_EXPECT(rows[10].kind == PlacePicker::Kind::Tab);
    KITE_EXPECT(rows[12].kind == PlacePicker::Kind::Drive);
    KITE_EXPECT_EQ(rows[12].name, std::string("Windows (C:)"));
    KITE_EXPECT_EQ(rows[13].path, std::string("D:\\"));
}

// 種別も絞り込みに当たる ─ 「drive」と打てばドライブだけが並ぶ。
KITE_TEST(placepicker, the_kind_of_a_drive_row_is_something_to_filter_on) {
    PlacePicker picker;
    OpenWith(picker, TenBookmarks(), {}, TwoTabs(), TwoDrives());

    for (char c : std::string("drive")) picker.HandleChar(static_cast<uint32_t>(c));
    KITE_EXPECT_EQ(static_cast<int>(picker.rows().size()), 2);
    KITE_EXPECT(picker.rows()[0].kind == PlacePicker::Kind::Drive);
}

// パスを指す行なので、Ctrl+Enter に «新しいタブで開く» の意味がある（タブの行には無い）。
KITE_TEST(placepicker, ctrl_enter_opens_a_drive_in_a_new_tab) {
    PlacePicker picker;
    OpenWith(picker, {}, {}, {}, TwoDrives());

    KITE_EXPECT(picker.HandleKey(ParseChord("Ctrl+Enter")) == PlacePicker::Action::OpenNewTab);
}

// 今いる場所がドライブそのものなら、そこにカーソルを置く ─ «どれの中に居るのか» は
// この画面がただで知っていることで、黙っているほうが不親切。
KITE_TEST(placepicker, the_drive_being_looked_at_starts_selected) {
    PlacePicker picker;
    OpenWith(picker, {}, "D:\\", {}, TwoDrives());

    KITE_EXPECT_EQ(picker.cursor(), 1);
}

// --- 絞り込み欄 --------------------------------------------------------------

// 打ち間違えた 1 文字を、全部消してやり直さずに直せる。
KITE_TEST(placepicker, the_filter_box_has_a_caret_that_moves) {
    PlacePicker picker;
    OpenWith(picker, TenBookmarks());

    for (char c : std::string("nest")) picker.HandleChar(static_cast<uint32_t>(c));
    picker.HandleKey(ParseChord("Left"));
    picker.HandleKey(ParseChord("Left"));
    picker.HandleChar('x');
    KITE_EXPECT_EQ(picker.filter(), std::string("nexst"));

    picker.HandleKey(ParseChord("Backspace"));
    KITE_EXPECT_EQ(picker.filter(), std::string("nest"));
    // 一覧はその場で数え直される ─ 画面に出ている文字と行が食い違ってはならない。
    KITE_EXPECT_EQ(static_cast<int>(picker.rows().size()), 1);
}

// 選択したまま打てば置き換わる。
KITE_TEST(placepicker, the_filter_box_selects_and_replaces) {
    PlacePicker picker;
    OpenWith(picker, TenBookmarks());

    for (char c : std::string("beta")) picker.HandleChar(static_cast<uint32_t>(c));
    picker.HandleKey(ParseChord("Ctrl+A"));
    picker.HandleChar('a');
    KITE_EXPECT_EQ(picker.filter(), std::string("a"));
}

// Home / End は絞り込みが空のときだけ一覧のもの。空の欄でキャレットを動かしても
// 何も起きないので、そのときは «先頭の行へ» と読む。
KITE_TEST(placepicker, home_and_end_belong_to_the_list_until_something_is_typed) {
    PlacePicker picker;
    OpenWith(picker, TenBookmarks());

    picker.HandleKey(ParseChord("End"));
    KITE_EXPECT_EQ(picker.cursor(), 9);
    picker.HandleKey(ParseChord("Home"));
    KITE_EXPECT_EQ(picker.cursor(), 0);

    // 打ち始めたら欄のもの。行は動かない。
    for (char c : std::string("mark")) picker.HandleChar(static_cast<uint32_t>(c));
    picker.MoveCursor(2, true);
    picker.HandleKey(ParseChord("Home"));
    KITE_EXPECT_EQ(picker.cursor(), 2);
    picker.HandleChar('x');
    KITE_EXPECT_EQ(picker.filter(), std::string("xmark"));
}

// Escape は先に絞り込みを捨てる。キャレットも戻る ─ 空の文字列の 4 文字目を
// 指したままでは、次に打った文字がどこへ入るか誰にも読めない。
KITE_TEST(placepicker, escape_drops_the_filter_and_the_caret_with_it) {
    PlacePicker picker;
    OpenWith(picker, TenBookmarks());

    for (char c : std::string("beta")) picker.HandleChar(static_cast<uint32_t>(c));
    KITE_EXPECT(picker.HandleKey(ParseChord("Escape")) == PlacePicker::Action::None);
    KITE_EXPECT(picker.filter().empty());
    KITE_EXPECT_EQ(picker.field().caret, size_t(0));
    KITE_EXPECT(picker.HandleKey(ParseChord("Escape")) == PlacePicker::Action::Close);
}

// 貼り付けられなければ、Kite でただ 1 つ «パスを貼れないテキスト欄» がここになる。
// 素通しすると Cmd::Paste になって、後ろの一覧にファイルが貼り付けられる。
KITE_TEST(placepicker, the_filter_box_takes_a_paste_instead_of_the_listing) {
    Harness h;
    h.app.workspace().bookmarks = TenBookmarks();
    h.shell.SetIncomingText("nested");

    h.app.Execute(Cmd::ShowPlaces);
    KITE_EXPECT(h.app.OnKey(ParseChord("Ctrl+V")));

    KITE_EXPECT_EQ(h.app.placePicker().filter(), std::string("nested"));
    KITE_EXPECT_EQ(static_cast<int>(h.app.placePicker().rows().size()), 1);
    // 後ろの一覧は何も受け取っていない。
    KITE_EXPECT(h.shell.clipboardFiles.empty());
}

// エクスプローラーの「パスのコピー」は引用符で包んだ 1 行を寄越す。そのまま入れると
// どのファイルシステムも知らないパスになる（入力欄と同じ扱い）。
KITE_TEST(placepicker, a_pasted_path_loses_its_quotes) {
    Harness h;
    h.app.workspace().bookmarks = TenBookmarks();
    // 引用符とスラッシュを直に書くと、この 1 行だけがエスケープの読み合いになる。
    const std::string quote(1, '"');
    h.shell.SetIncomingText(quote + "C:\\home\\alpha" + quote + "\nsecond line");

    h.app.Execute(Cmd::ShowPlaces);
    h.app.OnKey(ParseChord("Ctrl+V"));

    KITE_EXPECT_EQ(h.app.placePicker().filter(), std::string("C:\\home\\alpha"));
}

// 選択が無いときの Ctrl+C は欄の全体を写す。切り取りは失うので、範囲を指すまで待つ。
KITE_TEST(placepicker, the_filter_box_copies_and_cuts) {
    Harness h;
    h.app.workspace().bookmarks = TenBookmarks();

    h.app.Execute(Cmd::ShowPlaces);
    for (char c : std::string("beta")) h.app.OnChar(static_cast<uint32_t>(c));

    h.app.OnKey(ParseChord("Ctrl+X"));
    KITE_EXPECT(h.shell.clipboardText.empty());
    KITE_EXPECT_EQ(h.app.placePicker().filter(), std::string("beta"));

    h.app.OnKey(ParseChord("Ctrl+C"));
    KITE_EXPECT_EQ(static_cast<int>(h.shell.clipboardText.size()), 1);
    KITE_EXPECT_EQ(h.shell.clipboardText.back(), std::string("beta"));

    h.app.OnKey(ParseChord("Ctrl+A"));
    h.app.OnKey(ParseChord("Ctrl+X"));
    KITE_EXPECT(h.app.placePicker().filter().empty());
    // 絞り込みが消えたので全部戻る ─ ブックマーク 10 件とドライブ 1 台。
    KITE_EXPECT_EQ(static_cast<int>(h.app.placePicker().rows().size()), 11);
}
