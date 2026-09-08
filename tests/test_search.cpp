// 再帰検索（ROADMAP P2-3）。
//
// 検査しているのは 2 層 ─ `fs::SearchJob` そのもの（歩き方・打ち切り・輪への
// 耐性）と、`App` から見た振る舞い（歩き直す／歩き直さない、抜け方、上書きされ
// ないこと）。どちらも本物のワーカースレッドで走るので、ウィンドウと同じように
// ポンプする。

#include <atomic>
#include <string>
#include <vector>

#include "Fakes.h"
#include "TestFramework.h"
#include "core/app/App.h"
#include "core/base/PathUtil.h"
#include "core/fs/SearchJob.h"
#include "core/fs/VirtualPath.h"
#include "core/input/KeyMap.h"

using namespace kite;
using namespace kite::test;

namespace {

// SearchJob だけを回す最小の器。App を立てずに歩き方そのものを見るために使う。
struct JobHarness {
    FakeFileSystem files;
    FakeHost host;
    fs::SearchJob job{ files, host };

    // 歩き終わるまで回して、集まった当たりの名前を返す。
    std::vector<std::string> Collect(uint64_t token, bool* truncated = nullptr,
                                     int timeoutMs = 4000) {
        std::vector<std::string> names;
        if (truncated) *truncated = false;
        for (int elapsed = 0; elapsed <= timeoutMs; elapsed += 2) {
            std::vector<fs::SearchBatch> batches;
            job.Drain(batches);
            bool done = false;
            for (const fs::SearchBatch& b : batches) {
                if (b.token != token) continue;
                for (const fs::Entry& e : b.entries) names.push_back(e.name);
                if (b.truncated && truncated) *truncated = true;
                if (b.done) done = true;
            }
            if (done) return names;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        KITE_FAIL("search did not finish");
        return names;
    }
};

// App ごと立てて、フォーカス中のタブで検索するための器。
struct AppHarness {
    FakeFileSystem files;
    FakeShell shell;
    FakeHost host;
    FakeWatcher watcher;
    App app{ files, shell, host, &watcher };

    AppHarness() {
        ResetFakePlatform();
        PopulateStandardTree(files);
        app.SetStandalone(true);
        app.Init({});
        PumpUntilSettled(app);
    }

    Tab& tab() { return *app.workspace().focusedTab(); }

    void Type(const std::string& text) {
        for (char c : text) app.OnChar(static_cast<uint32_t>(c));
        PumpUntilSettled(app);
    }

    void Press(const char* chord) {
        app.OnKey(ParseChord(chord));
        PumpUntilSettled(app);
    }

    void Press(const Chord& chord) {
        app.OnKey(chord);
        PumpUntilSettled(app);
    }

    // 表示中の行の名前。並び順そのままで返す。
    std::vector<std::string> Rows() {
        std::vector<std::string> names;
        Tab& t = tab();
        for (int i = 0; i < static_cast<int>(t.visible.size()); ++i) {
            if (const fs::Entry* e = t.EntryAt(i)) names.push_back(e->name);
        }
        return names;
    }

    bool Has(const std::string& name) {
        const std::vector<std::string> rows = Rows();
        return std::find(rows.begin(), rows.end(), name) != rows.end();
    }
};

// 既定の和音で検索を開く。直書きすると、既定を動かした日に「キーが違う」で落ちる ─
// 試しているのは振る舞いのほうで、どのキーに乗っているかではない。
Chord SearchChord() {
    const std::vector<Chord> chords = KeyMap::DefaultChordsFor(Cmd::Search);
    return chords.empty() ? ParseChord("Ctrl+Shift+F") : chords.front();
}

}  // namespace

// ---------------------------------------------------------------------------
// SearchJob
// ---------------------------------------------------------------------------

KITE_TEST(search, walks_the_whole_tree_below_the_root) {
    JobHarness h;
    h.files.dirs["C:\\r"];
    h.files.AddDir("C:\\r\\a");
    h.files.AddDir("C:\\r\\a\\b");
    h.files.AddFile("C:\\r", "top-note.txt");
    h.files.AddFile("C:\\r\\a", "mid-note.txt");
    h.files.AddFile("C:\\r\\a\\b", "deep-note.txt");
    h.files.AddFile("C:\\r\\a\\b", "other.md");

    const std::vector<std::string> hits = h.Collect(h.job.Start("C:\\r", "note"));
    KITE_EXPECT_EQ(hits.size(), size_t(3));
    KITE_EXPECT(std::find(hits.begin(), hits.end(), "deep-note.txt") != hits.end());
    KITE_EXPECT(std::find(hits.begin(), hits.end(), "other.md") == hits.end());
}

KITE_TEST(search, the_hit_carries_its_own_path) {
    // 当たった項目は今いるフォルダの直下とは限らないので、名前を親に繋いでも
    // 指せない ─ address がその答え（fs::EntryPath がそのまま返す）。
    JobHarness h;
    h.files.dirs["C:\\r"];
    h.files.AddDir("C:\\r\\a");
    h.files.AddFile("C:\\r\\a", "found.txt");

    const uint64_t token = h.job.Start("C:\\r", "found");
    std::string address;
    for (int elapsed = 0; elapsed <= 4000; elapsed += 2) {
        std::vector<fs::SearchBatch> batches;
        h.job.Drain(batches);
        bool done = false;
        for (const fs::SearchBatch& b : batches) {
            if (b.token != token) continue;
            for (const fs::Entry& e : b.entries) address = e.address;
            if (b.done) done = true;
        }
        if (done) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    KITE_EXPECT_EQ(address, std::string("C:\\r\\a\\found.txt"));
    // そのまま fs::EntryPath の答えになる ─ 開くのも消すのもこのパス。
    fs::Entry e;
    e.name = "found.txt";
    e.address = address;
    KITE_EXPECT_EQ(fs::EntryPath("C:\\r", e), std::string("C:\\r\\a\\found.txt"));
}

KITE_TEST(search, case_is_ignored_and_the_match_is_a_substring) {
    JobHarness h;
    h.files.dirs["C:\\r"];
    h.files.AddFile("C:\\r", "README.MD");
    h.files.AddFile("C:\\r", "unrelated.txt");

    const std::vector<std::string> hits = h.Collect(h.job.Start("C:\\r", "eadme"));
    KITE_EXPECT_EQ(hits.size(), size_t(1));
    KITE_EXPECT_EQ(hits.front(), std::string("README.MD"));
}

KITE_TEST(search, folders_match_too) {
    JobHarness h;
    h.files.dirs["C:\\r"];
    h.files.AddDir("C:\\r\\reports");
    h.files.AddFile("C:\\r\\reports", "q1.csv");

    const std::vector<std::string> hits = h.Collect(h.job.Start("C:\\r", "report"));
    KITE_EXPECT_EQ(hits.size(), size_t(1));
    KITE_EXPECT_EQ(hits.front(), std::string("reports"));
}

KITE_TEST(search, hidden_items_are_carried_back_and_hidden_by_the_view) {
    // 検索が答えるのは «在るか無いか»。隠すかどうかは一覧の読み方なので、ここで
    // 刈ると Ctrl+H を押しても歩き直すまで出てこない。
    JobHarness h;
    h.files.dirs["C:\\r"];
    h.files.AddFile("C:\\r", ".secret-note", 1, 0, fs::Attr::Hidden);

    const std::vector<std::string> hits = h.Collect(h.job.Start("C:\\r", "note"));
    KITE_EXPECT_EQ(hits.size(), size_t(1));
}

KITE_TEST(search, an_unreadable_folder_is_skipped_not_fatal) {
    // アクセス拒否 1 つで検索そのものを終わらせると、C:\ からの検索は
    // System Volume Information に当たった時点で必ず打ち切られる。
    JobHarness h;
    h.files.dirs["C:\\r"];
    h.files.AddDir("C:\\r\\locked");
    h.files.AddDir("C:\\r\\open");
    h.files.AddFile("C:\\r\\open", "hit.txt");
    h.files.denied.push_back("C:\\r\\locked");

    const std::vector<std::string> hits = h.Collect(h.job.Start("C:\\r", "hit"));
    KITE_EXPECT_EQ(hits.size(), size_t(1));
}

KITE_TEST(search, a_junction_is_not_followed) {
    // 辿ると輪に入って歩きが終わらない。リンクそのものは当たりになりうるので、
    // «降りない» だけで «見えない» ではない。
    JobHarness h;
    h.files.dirs["C:\\r"];
    h.files.AddFile("C:\\r", "loop-link", 0, 0, fs::Attr::Directory | fs::Attr::Link);
    // リンクの名前でそのフォルダを引けるようにしておく ─ 降りてしまえば
    // 自分自身に戻り、上限に当たるまで止まらない。
    h.files.dirs["C:\\r\\loop-link"] = h.files.dirs["C:\\r"];

    bool truncated = false;
    const std::vector<std::string> hits = h.Collect(h.job.Start("C:\\r", "loop"), &truncated);
    KITE_EXPECT_EQ(hits.size(), size_t(1));
    KITE_EXPECT_FALSE(truncated);
}

KITE_TEST(search, too_many_matches_stop_and_say_so) {
    JobHarness h;
    h.files.dirs["C:\\r"];
    for (size_t i = 0; i < fs::kSearchMaxResults + 200; ++i) {
        h.files.AddFile("C:\\r", "hit" + std::to_string(i) + ".txt");
    }

    bool truncated = false;
    const std::vector<std::string> hits = h.Collect(h.job.Start("C:\\r", "hit"), &truncated);
    KITE_EXPECT(truncated);
    KITE_EXPECT_EQ(hits.size(), fs::kSearchMaxResults);
}

KITE_TEST(search, an_empty_query_starts_nothing) {
    JobHarness h;
    h.files.dirs["C:\\r"];
    h.files.AddFile("C:\\r", "a.txt");
    KITE_EXPECT_EQ(h.job.Start("C:\\r", ""), uint64_t(0));
}

KITE_TEST(search, a_second_start_replaces_the_first) {
    JobHarness h;
    h.files.dirs["C:\\r"];
    h.files.AddFile("C:\\r", "alpha.txt");
    h.files.AddFile("C:\\r", "beta.txt");

    const uint64_t first = h.job.Start("C:\\r", "alpha");
    const uint64_t second = h.job.Start("C:\\r", "beta");
    KITE_EXPECT_NE(first, second);

    const std::vector<std::string> hits = h.Collect(second);
    KITE_EXPECT_EQ(hits.size(), size_t(1));
    KITE_EXPECT_EQ(hits.front(), std::string("beta.txt"));
}

// ---------------------------------------------------------------------------
// App
// ---------------------------------------------------------------------------

KITE_TEST(search, the_chord_opens_a_field_and_empties_the_list) {
    // 検索欄の下に並ぶ行は検索結果でなければならない ─ フォルダの中身を残すと、
    // 最初の 1 打鍵がそれを消したように見える。
    AppHarness h;
    KITE_EXPECT(h.tab().ItemCount() > 0);

    h.Press(SearchChord());
    KITE_EXPECT(h.tab().search.active);
    KITE_EXPECT_EQ(h.app.prompt().kind, PromptKind::Search);
    KITE_EXPECT_EQ(h.tab().ItemCount(), 0);
    // 一覧であってフォルダではないので「..」は出さない。
    KITE_EXPECT_FALSE(h.tab().hasParentRow());
}

KITE_TEST(search, typing_fills_the_list_from_below_the_folder) {
    AppHarness h;
    h.Press(SearchChord());
    h.Type("inner");

    // C:\home\alpha\inner.md ─ 今いるフォルダの直下にはない。
    KITE_EXPECT(h.Has("inner.md"));
    KITE_EXPECT_EQ(h.tab().ItemCount(), 1);
    KITE_EXPECT_EQ(h.tab().CursorPath(), std::string("C:\\home\\alpha\\inner.md"));
}

KITE_TEST(search, typing_further_narrows_without_walking_again) {
    // ふるいが今の問いの部分文字列である限り、当たるものはもう手元にある ─
    // ここが崩れると、深いフォルダで 1 打鍵ごとにディスクを歩き直すことになる。
    AppHarness h;
    h.files.AddFile("C:\\home\\alpha", "innermost.md");
    h.Press(SearchChord());
    h.Type("inn");
    KITE_EXPECT_EQ(h.tab().ItemCount(), 2);

    const int before = h.files.listCalls;
    h.Type("erm");
    KITE_EXPECT_EQ(h.files.listCalls, before);
    KITE_EXPECT_EQ(h.tab().ItemCount(), 1);
    KITE_EXPECT(h.Has("innermost.md"));
    // ふるいは «歩き始めたときの問い» のまま。打った文字列は filter が持つ。
    KITE_EXPECT_EQ(h.tab().search.sieve, std::string("inn"));
    KITE_EXPECT_EQ(h.tab().filter, std::string("innerm"));
}

KITE_TEST(search, shortening_the_query_walks_again) {
    AppHarness h;
    h.files.AddFile("C:\\home\\alpha", "innermost.md");
    h.Press(SearchChord());
    h.Type("innerm");
    KITE_EXPECT_EQ(h.tab().ItemCount(), 1);

    const int before = h.files.listCalls;
    h.Press("Backspace");
    h.Press("Backspace");
    // 縮めたときは «前回のふるいでは落としたもの» が要るので、歩き直す。
    KITE_EXPECT(h.files.listCalls > before);
    KITE_EXPECT_EQ(h.tab().search.sieve, std::string("inne"));
    KITE_EXPECT_EQ(h.tab().ItemCount(), 2);
}

KITE_TEST(search, clearing_the_query_leaves_an_empty_list_not_the_folder) {
    AppHarness h;
    h.Press(SearchChord());
    h.Type("inner");
    KITE_EXPECT_EQ(h.tab().ItemCount(), 1);

    h.Press("Backspace");
    h.Press("Backspace");
    h.Press("Backspace");
    h.Press("Backspace");
    h.Press("Backspace");
    KITE_EXPECT(h.tab().search.active);
    KITE_EXPECT(h.tab().search.sieve.empty());
    KITE_EXPECT_EQ(h.tab().ItemCount(), 0);
}

KITE_TEST(search, escape_puts_the_folder_back) {
    AppHarness h;
    const int before = h.tab().ItemCount();
    h.Press(SearchChord());
    h.Type("inner");

    h.Press("Escape");
    KITE_EXPECT_FALSE(h.tab().search.active);
    KITE_EXPECT(h.tab().filter.empty());
    KITE_EXPECT_EQ(h.tab().ItemCount(), before);
    KITE_EXPECT(h.tab().hasParentRow() || path::Parent(h.tab().path).empty());
}

KITE_TEST(search, escape_on_the_list_leaves_search_too) {
    // 欄を Enter で閉じた後の Escape。絞り込みだけ消しても «このフォルダの中身
    // ではない一覧» が残るので、そこから抜けるのが先。
    AppHarness h;
    h.Press(SearchChord());
    h.Type("inner");
    h.Press("Enter");
    KITE_EXPECT_FALSE(h.app.prompt().active());

    // Enter は当たった行を開くので、フォルダでなければタブはそのまま。
    KITE_EXPECT(h.tab().search.active);
    h.app.Execute(Cmd::CancelOverlay);
    PumpUntilSettled(h.app);
    KITE_EXPECT_FALSE(h.tab().search.active);
    KITE_EXPECT(h.tab().ItemCount() > 0);
}

KITE_TEST(search, opening_a_hit_that_is_a_folder_leaves_search) {
    AppHarness h;
    h.Press(SearchChord());
    h.Type("nested");
    KITE_EXPECT(h.Has("nested"));

    h.Press("Enter");
    KITE_EXPECT_EQ(h.tab().path, std::string("C:\\home\\alpha\\nested"));
    KITE_EXPECT_FALSE(h.tab().search.active);
    KITE_EXPECT(h.tab().filter.empty());
}

KITE_TEST(search, a_change_notification_does_not_wipe_the_results) {
    // 届くのはフォルダの中身で、画面に出ているのはその下から集めたもの。
    AppHarness h;
    h.Press(SearchChord());
    h.Type("inner");
    KITE_EXPECT_EQ(h.tab().ItemCount(), 1);

    h.watcher.Emit(h.tab().watchId, h.tab().path);
    PumpUntilSettled(h.app);
    KITE_EXPECT(h.tab().search.active);
    KITE_EXPECT_EQ(h.tab().ItemCount(), 1);
}

KITE_TEST(search, refresh_asks_again_rather_than_dropping_out) {
    AppHarness h;
    h.Press(SearchChord());
    h.Type("inner");
    KITE_EXPECT_EQ(h.tab().ItemCount(), 1);

    h.files.AddFile("C:\\home\\beta", "inner2.md");
    h.app.Execute(Cmd::Refresh);
    PumpUntilSettled(h.app);
    KITE_EXPECT(h.tab().search.active);
    KITE_EXPECT_EQ(h.tab().ItemCount(), 2);
}

KITE_TEST(search, a_virtual_folder_is_refused_with_a_reason) {
    // 仮想フォルダの列挙はシェルのホスト 1 本を通る。再帰的に占有すると、
    // その間ほかのフォルダが 1 つも開けなくなる。
    AppHarness h;
    h.app.NavigateFocused(vfs::kComputer);
    PumpUntilSettled(h.app);

    h.Press(SearchChord());
    KITE_EXPECT_FALSE(h.tab().search.active);
    KITE_EXPECT_FALSE(h.app.prompt().active());
    KITE_EXPECT_EQ(h.app.statusMessage(),
                   h.app.strings().Get("ui.search_unsupported"));
}

KITE_TEST(search, the_status_line_says_what_the_list_is) {
    AppHarness h;
    KITE_EXPECT(h.app.searchStatus().empty());

    h.Press(SearchChord());
    // まだ何も訊かれていないので黙る ─ 案内は一覧の側が出す。
    KITE_EXPECT(h.app.searchStatus().empty());

    h.Type("inner");
    KITE_EXPECT_FALSE(h.app.searchStatus().empty());
}

KITE_TEST(search, hidden_hits_follow_the_view_setting) {
    AppHarness h;
    h.files.AddFile("C:\\home\\alpha", ".hidden-note", 1, 0, fs::Attr::Hidden);
    h.Press(SearchChord());
    h.Type("note");
    // C:\home\notes.txt は見えるが、隠しのほうは view が伏せている。
    KITE_EXPECT(h.Has("notes.txt"));
    KITE_EXPECT_FALSE(h.Has(".hidden-note"));

    // 歩き直さずに現れる ─ 当たりは最初から手元にある。
    const int before = h.files.listCalls;
    h.app.Execute(Cmd::ToggleHidden);
    PumpUntilSettled(h.app);
    KITE_EXPECT_EQ(h.files.listCalls, before);
    KITE_EXPECT(h.Has(".hidden-note"));
}

KITE_TEST(search, enter_without_a_query_leaves_search_rather_than_stranding_an_empty_list) {
    // 何も訊かずに閉じたのなら何も変わっていないはず ─ 空の一覧と閉じた欄だけが
    // 残ると、そこから出る道が Escape しか無くなる。
    AppHarness h;
    const int before = h.tab().ItemCount();
    h.Press(SearchChord());
    h.Press("Enter");

    KITE_EXPECT_FALSE(h.tab().search.active);
    KITE_EXPECT_FALSE(h.app.prompt().active());
    KITE_EXPECT_EQ(h.tab().ItemCount(), before);
}

KITE_TEST(search, reopening_the_field_keeps_the_query_and_the_results) {
    // 同じ問いのままなので、集めてある当たりがそのまま答え ─ 歩き直す理由が無い。
    AppHarness h;
    h.Press(SearchChord());
    h.Type("inner");
    h.Press("Enter");

    const int before = h.files.listCalls;
    h.Press(SearchChord());
    KITE_EXPECT_EQ(h.app.prompt().kind, PromptKind::Search);
    KITE_EXPECT_EQ(h.app.prompt().text, std::string("inner"));
    KITE_EXPECT_EQ(h.files.listCalls, before);
    KITE_EXPECT_EQ(h.tab().ItemCount(), 1);
}
