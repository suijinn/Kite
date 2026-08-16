// ブックマーク一覧（Alt+B）。番号ショートカットが 8 個で打ち止めなので、
// 9 件目以降にキーボードだけで届けるかどうかがこの画面の存在理由。画面自体は OS に
// 触れないので、ここは全部ウィンドウ無しで動く。
#include "Fakes.h"
#include "TestFramework.h"
#include "core/app/BookmarkPicker.h"

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

// 目当ての行番号。無ければ -1。
int RowOf(const BookmarkPicker& picker, const std::string& name) {
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

KITE_TEST(bookmarkpicker, every_bookmark_gets_a_row_in_order) {
    BookmarkPicker picker;
    picker.Open(TenBookmarks(), Defaults(), {});

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
KITE_TEST(bookmarkpicker, only_the_first_eight_carry_a_number_shortcut) {
    BookmarkPicker picker;
    picker.Open(TenBookmarks(), Defaults(), {});

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

KITE_TEST(bookmarkpicker, opens_on_the_folder_already_being_looked_at) {
    BookmarkPicker picker;
    picker.Open(TenBookmarks(), Defaults(), "C:\\work\\p5");
    KITE_EXPECT_EQ(picker.selectedIndex(), 4);

    // 大文字小文字は畳む（Windows のパスなので）。
    picker.Open(TenBookmarks(), Defaults(), "c:\\HOME\\Beta");
    KITE_EXPECT_EQ(picker.selectedIndex(), 1);

    // どれでもないところから開いたら先頭。Enter が何もしない状態にはしない。
    picker.Open(TenBookmarks(), Defaults(), "C:\\elsewhere");
    KITE_EXPECT_EQ(picker.selectedIndex(), 0);
}

KITE_TEST(bookmarkpicker, the_filter_matches_the_name_and_the_path) {
    BookmarkPicker picker;
    picker.Open(TenBookmarks(), Defaults(), {});

    picker.HandleChar('n');
    picker.HandleChar('e');
    // 名前で "nested"、パスで "C:\home\alpha\nested" ─ どちらも同じ 1 件。
    KITE_EXPECT_EQ(static_cast<int>(picker.rows().size()), 1);
    KITE_EXPECT_EQ(picker.rows()[0].name, std::string("nested"));

    // 前方一致ではなく部分一致。深いパスの末尾で探せないと意味が無い。
    picker.Open(TenBookmarks(), Defaults(), {});
    for (char c : std::string("work")) picker.HandleChar(static_cast<uint32_t>(c));
    KITE_EXPECT_EQ(static_cast<int>(picker.rows().size()), 7);

    // 大文字小文字は畳む。
    picker.Open(TenBookmarks(), Defaults(), {});
    for (char c : std::string("ALPHA")) picker.HandleChar(static_cast<uint32_t>(c));
    KITE_EXPECT_EQ(static_cast<int>(picker.rows().size()), 2);  // alpha と nested
}

// 絞り込むたびに行番号は振り直されるので、選択は「行」ではなく「ブックマーク」で
// 覚えていなければならない。
KITE_TEST(bookmarkpicker, the_selection_follows_the_bookmark_not_the_row) {
    BookmarkPicker picker;
    picker.Open(TenBookmarks(), Defaults(), {});

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
KITE_TEST(bookmarkpicker, a_filtered_away_selection_falls_to_the_top) {
    BookmarkPicker picker;
    picker.Open(TenBookmarks(), Defaults(), {});
    picker.SelectRow(RowOf(picker, "nested"));

    for (char c : std::string("work")) picker.HandleChar(static_cast<uint32_t>(c));
    KITE_EXPECT_EQ(picker.cursor(), 0);
    KITE_EXPECT_EQ(picker.selectedIndex(), 2);  // mark3
}

KITE_TEST(bookmarkpicker, nothing_matching_leaves_no_row_and_no_answer) {
    BookmarkPicker picker;
    picker.Open(TenBookmarks(), Defaults(), {});
    for (char c : std::string("zzz")) picker.HandleChar(static_cast<uint32_t>(c));

    KITE_EXPECT(picker.rows().empty());
    KITE_EXPECT_EQ(picker.cursor(), -1);
    KITE_EXPECT_EQ(picker.selectedIndex(), -1);
    // 空振りの Enter が「先頭を開く」になってはならない。
    KITE_EXPECT(picker.HandleKey(ParseChord("Enter")) == BookmarkPicker::Action::None);
    // 総数は絞り込みでは減らない ─ 残りがまだ在ることを言えるのはこれだけ。
    KITE_EXPECT_EQ(picker.total(), 10);
}

KITE_TEST(bookmarkpicker, escape_drops_the_filter_first_then_closes) {
    BookmarkPicker picker;
    picker.Open(TenBookmarks(), Defaults(), {});
    for (char c : std::string("work")) picker.HandleChar(static_cast<uint32_t>(c));

    KITE_EXPECT(picker.HandleKey(ParseChord("Escape")) == BookmarkPicker::Action::None);
    KITE_EXPECT(picker.filter().empty());
    KITE_EXPECT_EQ(static_cast<int>(picker.rows().size()), 10);

    KITE_EXPECT(picker.HandleKey(ParseChord("Escape")) == BookmarkPicker::Action::Close);
}

KITE_TEST(bookmarkpicker, enter_opens_and_ctrl_enter_asks_for_a_new_tab) {
    BookmarkPicker picker;
    picker.Open(TenBookmarks(), Defaults(), {});
    KITE_EXPECT(picker.HandleKey(ParseChord("Enter")) == BookmarkPicker::Action::Open);
    KITE_EXPECT(picker.HandleKey(ParseChord("Ctrl+Enter")) == BookmarkPicker::Action::OpenNewTab);
}

KITE_TEST(bookmarkpicker, the_cursor_stops_at_both_ends) {
    BookmarkPicker picker;
    picker.Open(TenBookmarks(), Defaults(), {});

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
KITE_TEST(bookmarkpicker, unrelated_chords_are_swallowed) {
    BookmarkPicker picker;
    picker.Open(TenBookmarks(), Defaults(), {});
    KITE_EXPECT(picker.HandleKey(ParseChord("Ctrl+T")) == BookmarkPicker::Action::None);
    KITE_EXPECT(picker.HandleKey(ParseChord("Delete")) == BookmarkPicker::Action::None);
    KITE_EXPECT(picker.visible());
}

KITE_TEST(bookmarkpicker, scrolling_stays_inside_the_list) {
    BookmarkPicker picker;
    picker.Open(TenBookmarks(), Defaults(), {});
    picker.SetPageRows(4);

    picker.Scroll(100);
    KITE_EXPECT_EQ(picker.scroll(), 6);  // 10 行 - 4 行
    picker.Scroll(-100);
    KITE_EXPECT_EQ(picker.scroll(), 0);

    // カーソルを下端まで動かせば追いかける。
    picker.HandleKey(ParseChord("End"));
    KITE_EXPECT_EQ(picker.scroll(), 6);
}

// --- App を通したところ ------------------------------------------------------

KITE_TEST(bookmarkpicker, nine_or_more_bookmarks_are_reachable_from_the_keyboard) {
    Harness h;
    h.app.workspace().bookmarks = TenBookmarks();

    h.app.Execute(Cmd::ShowBookmarks);
    KITE_EXPECT(h.app.bookmarkPicker().visible());

    // 10 件目 ─ Alt+Shift+1..8 のどれでも届かないところ。名前で絞って Enter。
    KITE_EXPECT(h.app.OnChar('n'));
    KITE_EXPECT(h.app.OnChar('e'));
    KITE_EXPECT(h.app.OnChar('s'));
    KITE_EXPECT_EQ(static_cast<int>(h.app.bookmarkPicker().rows().size()), 1);

    KITE_EXPECT(h.app.OnKey(ParseChord("Enter")));
    test::PumpUntilSettled(h.app);

    KITE_EXPECT_FALSE(h.app.bookmarkPicker().visible());
    KITE_EXPECT_EQ(h.tab()->path, std::string("C:\\home\\alpha\\nested"));
}

KITE_TEST(bookmarkpicker, ctrl_enter_opens_the_bookmark_in_a_new_tab) {
    Harness h;
    h.app.workspace().bookmarks = TenBookmarks();
    const size_t before = h.app.workspace().focusedPane()->tabs.size();

    h.app.Execute(Cmd::ShowBookmarks);
    for (char c : std::string("beta")) h.app.OnChar(static_cast<uint32_t>(c));
    KITE_EXPECT(h.app.OnKey(ParseChord("Ctrl+Enter")));
    test::PumpUntilSettled(h.app);

    KITE_EXPECT_FALSE(h.app.bookmarkPicker().visible());
    KITE_EXPECT_EQ(h.app.workspace().focusedPane()->tabs.size(), before + 1);
    KITE_EXPECT_EQ(h.tab()->path, std::string("C:\\home\\beta"));
}

// 空の一覧は「押しても何も起きない」と同じで、しかも閉じる手間だけ増える。
KITE_TEST(bookmarkpicker, with_no_bookmarks_it_says_so_instead_of_opening) {
    Harness h;
    h.app.workspace().bookmarks.clear();

    h.app.Execute(Cmd::ShowBookmarks);
    KITE_EXPECT_FALSE(h.app.bookmarkPicker().visible());
    KITE_EXPECT(!h.app.statusMessage().empty());
}

KITE_TEST(bookmarkpicker, the_key_that_opens_it_closes_it) {
    Harness h;
    h.app.workspace().bookmarks = TenBookmarks();

    KITE_EXPECT(h.app.OnKey(ParseChord("Alt+B")));
    KITE_EXPECT(h.app.bookmarkPicker().visible());
    KITE_EXPECT(h.app.OnKey(ParseChord("Alt+B")));
    KITE_EXPECT_FALSE(h.app.bookmarkPicker().visible());
}

// 開いている間、後ろの一覧は何も受け取らない。
KITE_TEST(bookmarkpicker, the_list_behind_it_gets_nothing) {
    Harness h;
    h.app.workspace().bookmarks = TenBookmarks();
    const size_t before = h.app.workspace().focusedPane()->tabs.size();
    const std::string path = h.tab()->path;

    h.app.Execute(Cmd::ShowBookmarks);
    KITE_EXPECT(h.app.OnKey(ParseChord("Ctrl+T")));
    KITE_EXPECT(h.app.OnKey(ParseChord("Alt+Up")));
    test::PumpUntilSettled(h.app);

    KITE_EXPECT(h.app.bookmarkPicker().visible());
    KITE_EXPECT_EQ(h.app.workspace().focusedPane()->tabs.size(), before);
    KITE_EXPECT_EQ(h.tab()->path, path);
}

// 他のオーバーレイと同居しない ─ 2 枚重なると、どちらが打鍵を受けるのか読めない。
KITE_TEST(bookmarkpicker, opening_another_overlay_closes_it) {
    Harness h;
    h.app.workspace().bookmarks = TenBookmarks();

    h.app.Execute(Cmd::ShowBookmarks);
    h.app.Execute(Cmd::ShowSettings);
    KITE_EXPECT_FALSE(h.app.bookmarkPicker().visible());
    KITE_EXPECT(h.app.settingsEditor().visible());

    h.app.Execute(Cmd::ShowSettings);
    h.app.Execute(Cmd::ShowBookmarks);
    h.app.Execute(Cmd::ShowKeyHelp);
    KITE_EXPECT_FALSE(h.app.bookmarkPicker().visible());
    KITE_EXPECT(h.app.keyHelpVisible());
}
