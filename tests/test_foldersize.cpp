// フォルダの合計サイズ（ROADMAP P3-14）。
//
// 検査しているのは 3 層 ─ `fs::FolderSizeJob` そのもの（歩き方・打ち切り・
// 読めない枝）、`fs::FolderSizeCache`（二重の依頼、無効化、上限）、そして `App`
// から見た振る舞い（自動で数える／数えない、並べ替えが途中経過で動かないこと、
// ステータス行が言うこと）。ワーカーは本物のスレッドで走るので、ウィンドウと
// 同じようにポンプする。

#include <map>
#include <string>
#include <vector>

#include "Fakes.h"
#include "TestFramework.h"
#include "core/app/App.h"
#include "core/base/Format.h"
#include "core/base/PathUtil.h"
#include "core/fs/FolderSize.h"
#include "core/fs/TreeWalk.h"
#include "core/input/KeyMap.h"
#include "ui/AppUi.h"

using namespace kite;
using namespace kite::test;

namespace {

// ワーカーだけを回す最小の器。App を立てずに歩き方そのものを見るために使う。
struct JobHarness {
    FakeFileSystem files;
    FakeHost host;
    fs::FolderSizeJob job{ files, host, 2 };

    // 届いた確定値を覚えておく。2 本同時に走らせたときは、片方を待っている間に
    // もう片方の答えも届く ─ 捨てると、次に訊いたほうが永久に settle しない。
    std::map<std::string, fs::FolderSizeUpdate> settled;

    // 数え終わるまで回して、確定した結果を返す。
    fs::FolderSizeUpdate Collect(const std::string& path, int timeoutMs = 4000) {
        for (int elapsed = 0; elapsed <= timeoutMs; elapsed += 2) {
            std::vector<fs::FolderSizeUpdate> updates;
            job.Drain(updates);
            for (const fs::FolderSizeUpdate& u : updates) {
                if (u.done) settled[u.path] = u;
            }
            auto it = settled.find(path);
            if (it != settled.end()) return it->second;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        KITE_FAIL("folder size did not settle");
        return {};
    }
};

// App ごと立てる器。
struct AppHarness {
    FakeFileSystem files;
    FakeShell shell;
    FakeHost host;
    FakeWatcher watcher;
    App app{ files, shell, host, &watcher };

    AppHarness() {
        ResetFakePlatform();
        PopulateStandardTree(files);
        // beta を alpha よりはっきり大きくする ─ サイズ順の検査で «どちらが先か»
        // が偶然にならないように。
        files.AddFile("C:\\home\\beta", "big.bin", 5000, 900);
        app.SetStandalone(true);
        app.Init({});
        PumpUntilSettled(app);
    }

    Tab& tab() { return *app.workspace().focusedTab(); }

    const fs::Entry* Entry(const std::string& name) {
        for (const fs::Entry& e : tab().listing.entries) {
            if (e.name == name) return &e;
        }
        return nullptr;
    }

    // 描画が毎フレームしていることと同じ ─ 訊けば、必要なら数え始める。
    fs::FolderSize Ask(const std::string& name) {
        const fs::Entry* e = Entry(name);
        KITE_EXPECT(e != nullptr);
        return e ? app.FolderSizeFor(tab().path, *e) : fs::FolderSize{};
    }

    // 数え終わるまで訊き続ける（1 回目の問いが依頼になる）。
    fs::FolderSize Measure(const std::string& name) {
        Ask(name);
        PumpUntilSettled(app);
        return Ask(name);
    }

    void Press(const char* chord) {
        app.OnKey(ParseChord(chord));
        PumpUntilSettled(app);
    }

    // 1 フレーム描く。列に何が出るかは、実際に描いてみるまで分からない ─
    // 自動で数え始めるのもこの経路（描画が訊いた行だけ）。
    ui::AppUi ui{ app };
    FakeRenderer renderer;

    void Paint() {
        renderer.Clear();
        ui.Paint(renderer);
    }

    bool HasText(const std::string& text) {
        for (const FakeRenderer::Text& t : renderer.texts) {
            if (t.text == text) return true;
        }
        return false;
    }

    // 明度で語る表現の検査は色まで見る（切り取られた行と同じ手）。
    float AlphaOf(const std::string& text) {
        for (const FakeRenderer::Text& t : renderer.texts) {
            if (t.text == text) return t.color.a;
        }
        KITE_FAIL("no such text on screen");
        return 0.0f;
    }

    std::vector<std::string> VisibleNames() {
        std::vector<std::string> names;
        for (int index : tab().visible) {
            if (const fs::Entry* e = tab().EntryAt(index)) names.push_back(e->name);
        }
        return names;
    }
};

}  // namespace

// --- the walker -------------------------------------------------------------

KITE_TEST(foldersize, sums_every_file_below_the_folder) {
    JobHarness h;
    PopulateStandardTree(h.files);
    h.files.AddFile("C:\\home\\alpha\\nested", "deep.bin", 900, 10);

    h.job.Request("C:\\home\\alpha");
    const fs::FolderSizeUpdate done = h.Collect("C:\\home\\alpha");

    KITE_EXPECT_EQ(done.bytes, static_cast<uint64_t>(64 + 900));
    KITE_EXPECT_EQ(done.files, static_cast<uint64_t>(2));
    // 自分自身は数えない ─ 中に在るのは nested ひとつ。
    KITE_EXPECT_EQ(done.dirs, static_cast<uint64_t>(1));
    KITE_EXPECT_FALSE(done.incomplete);
    KITE_EXPECT_FALSE(done.failed);
}

KITE_TEST(foldersize, does_not_descend_into_links) {
    JobHarness h;
    h.files.dirs["C:\\home"];
    h.files.AddDir("C:\\home\\target");
    h.files.AddFile("C:\\home\\target", "big.bin", 4096, 10);
    h.files.AddDir("C:\\home\\link");
    // 同じ木を指すジャンクション。辿れば輪に入るうえ、合計を 2 度数えることになる。
    h.files.AddFile("C:\\home\\link", "inner.txt", 8, 10);
    for (fs::Entry& e : h.files.dirs["C:\\home"]) {
        if (e.name == "link") e.attrs = e.attrs | fs::Attr::Link;
    }

    h.job.Request("C:\\home");
    const fs::FolderSizeUpdate done = h.Collect("C:\\home");

    // link の中の 8 バイトは入らない。リンクそのものはフォルダとして 1 つ数える。
    KITE_EXPECT_EQ(done.bytes, static_cast<uint64_t>(4096));
    KITE_EXPECT_EQ(done.dirs, static_cast<uint64_t>(2));
}

KITE_TEST(foldersize, an_unreadable_branch_is_skipped_but_remembered) {
    JobHarness h;
    PopulateStandardTree(h.files);
    h.files.denied.push_back("C:\\home\\alpha\\nested");

    h.job.Request("C:\\home\\alpha");
    const fs::FolderSizeUpdate done = h.Collect("C:\\home\\alpha");

    // 1 つのアクセス拒否で数えるのをやめない ─ 読めたぶんは答えになる。
    KITE_EXPECT_EQ(done.bytes, static_cast<uint64_t>(64));
    // ただし «少なくともこれだけ» でしかないことは覚えている。
    KITE_EXPECT(done.incomplete);
    KITE_EXPECT_FALSE(done.failed);
}

KITE_TEST(foldersize, an_unreadable_folder_itself_is_a_failure) {
    JobHarness h;
    PopulateStandardTree(h.files);
    h.files.denied.push_back("C:\\home\\alpha");

    h.job.Request("C:\\home\\alpha");
    const fs::FolderSizeUpdate done = h.Collect("C:\\home\\alpha");

    KITE_EXPECT(done.failed);
    KITE_EXPECT_EQ(done.bytes, static_cast<uint64_t>(0));
}

KITE_TEST(foldersize, the_same_folder_is_not_walked_twice) {
    JobHarness h;
    PopulateStandardTree(h.files);

    h.files.Gate("C:\\home\\alpha");
    h.job.Request("C:\\home\\alpha");
    h.job.Request("C:\\home\\alpha");
    KITE_EXPECT(h.files.WaitForGate(1));
    // 2 本目が走っていれば、ここで 2 が止まっている。
    KITE_EXPECT_EQ(h.files.GateWaiting(), 1);
    h.files.Release();
    h.Collect("C:\\home\\alpha");
}

KITE_TEST(foldersize, two_folders_are_counted_at_the_same_time) {
    JobHarness h;
    PopulateStandardTree(h.files);

    // 速さで見ない ─ 両方をその場で止めて数える（FileOpQueue のテストと同じ）。
    h.files.Gate("C:\\home\\alpha");
    h.files.Gate("C:\\home\\beta");
    h.job.Request("C:\\home\\alpha");
    h.job.Request("C:\\home\\beta");

    KITE_EXPECT(h.files.WaitForGate(2));
    h.files.Release();
    h.Collect("C:\\home\\alpha");
    h.Collect("C:\\home\\beta");
}

KITE_TEST(foldersize, cancelling_stops_the_walk_and_changes_the_epoch) {
    JobHarness h;
    PopulateStandardTree(h.files);

    const uint64_t before = h.job.epoch();
    h.files.Gate("C:\\home\\alpha");
    h.job.Request("C:\\home\\alpha");
    KITE_EXPECT(h.files.WaitForGate(1));

    h.job.CancelAll();
    KITE_EXPECT(h.job.epoch() != before);
    h.files.Release();

    // 畳まれた歩きは «確定» を出さない。
    for (int elapsed = 0; elapsed < 200; elapsed += 2) {
        std::vector<fs::FolderSizeUpdate> updates;
        h.job.Drain(updates);
        for (const fs::FolderSizeUpdate& u : updates) KITE_EXPECT_FALSE(u.done);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

// --- the table --------------------------------------------------------------

KITE_TEST(foldersize, the_table_asks_for_a_folder_only_once) {
    fs::FolderSizeCache cache;
    KITE_EXPECT(cache.Request("C:\\a"));
    // 描画は毎フレーム訊く。2 度目からは «数え中» なので頼み直さない。
    KITE_EXPECT_FALSE(cache.Request("C:\\a"));
    KITE_EXPECT_EQ(static_cast<int>(cache.Get("C:\\a").state),
                   static_cast<int>(fs::SizeState::Counting));
    KITE_EXPECT_EQ(static_cast<int>(cache.countingCount()), 1);

    fs::FolderSizeUpdate done;
    done.path = "C:\\a";
    done.bytes = 42;
    done.done = true;
    KITE_EXPECT(cache.Apply(done));
    KITE_EXPECT_EQ(cache.Get("C:\\a").bytes, static_cast<uint64_t>(42));
    KITE_EXPECT(cache.Get("C:\\a").settled());
    KITE_EXPECT_EQ(static_cast<int>(cache.countingCount()), 0);
    // 答えが出ているものも頼み直さない。
    KITE_EXPECT_FALSE(cache.Request("C:\\a"));
}

KITE_TEST(foldersize, an_answer_for_a_forgotten_folder_is_dropped) {
    fs::FolderSizeCache cache;
    cache.Request("C:\\a");
    cache.ForgetRelated("C:\\a");

    fs::FolderSizeUpdate done;
    done.path = "C:\\a";
    done.bytes = 42;
    done.done = true;
    // 捨てたはずの値が «数え終わった» 顔で戻ってきてはならない。
    KITE_EXPECT_FALSE(cache.Apply(done));
    KITE_EXPECT_EQ(static_cast<int>(cache.Get("C:\\a").state),
                   static_cast<int>(fs::SizeState::Unknown));
}

KITE_TEST(foldersize, forgetting_takes_the_branch_and_everything_above_it) {
    fs::FolderSizeCache cache;
    auto store = [&](const std::string& path, uint64_t bytes) {
        cache.Request(path);
        fs::FolderSizeUpdate u;
        u.path = path;
        u.bytes = bytes;
        u.done = true;
        cache.Apply(u);
    };
    store("C:\\a", 10);
    store("C:\\a\\b", 20);
    store("C:\\a\\b\\c", 30);
    store("C:\\a2", 40);

    cache.ForgetRelated("C:\\a\\b");

    // 下が変われば上の合計も変わる ─ 両方向を捨てる。
    KITE_EXPECT_FALSE(cache.Get("C:\\a").known());
    KITE_EXPECT_FALSE(cache.Get("C:\\a\\b").known());
    KITE_EXPECT_FALSE(cache.Get("C:\\a\\b\\c").known());
    // 名前の頭が同じだけの兄弟は巻き添えにしない。
    KITE_EXPECT(cache.Get("C:\\a2").settled());
}

KITE_TEST(foldersize, stopping_forgets_what_was_being_counted) {
    fs::FolderSizeCache cache;
    cache.Request("C:\\a");
    fs::FolderSizeUpdate progress;
    progress.path = "C:\\a";
    progress.bytes = 5;
    cache.Apply(progress);
    KITE_EXPECT_EQ(cache.Get("C:\\a").bytes, static_cast<uint64_t>(5));

    cache.ResetInFlight();
    // 途中経過は残さない ─ 止めた値が «数え終わった» 値と同じ顔で残ると、
    // どちらなのかを画面が言えない。
    KITE_EXPECT_EQ(cache.Get("C:\\a").bytes, static_cast<uint64_t>(0));
    KITE_EXPECT_FALSE(cache.Get("C:\\a").known());
    KITE_EXPECT_EQ(static_cast<int>(cache.countingCount()), 0);
    // ただし «まだ» には戻さない ─ 戻すと、自動で数える設定では次に描いた
    // 1 フレームがそのまま頼み直す。
    KITE_EXPECT_EQ(static_cast<int>(cache.Get("C:\\a").state),
                   static_cast<int>(fs::SizeState::Stopped));
    KITE_EXPECT_FALSE(cache.Request("C:\\a"));
    KITE_EXPECT(cache.Request("C:\\a", true));
}

KITE_TEST(foldersize, a_value_counted_a_moment_ago_survives_a_change_notification) {
    fs::FolderSizeCache cache;
    cache.Request("C:\\a");
    fs::FolderSizeUpdate done;
    done.path = "C:\\a";
    done.bytes = 42;
    done.done = true;
    done.atMs = 1000;
    cache.Apply(done);

    // 通知は書き込みのたびに届く。数えたばかりの値を毎回捨てると、見ているだけで
    // その下の木を何度も歩き直すことになる。
    cache.ForgetChanged("C:\\a", 1500, fs::kFolderSizeRecountMs);
    KITE_EXPECT(cache.Get("C:\\a").settled());

    // 間隔が空けば、次の通知で数え直す。
    cache.ForgetChanged("C:\\a", 1000 + fs::kFolderSizeRecountMs + 1, fs::kFolderSizeRecountMs);
    KITE_EXPECT_FALSE(cache.Get("C:\\a").known());

    // F5 と自分で起こしたファイル操作はこの間隔を通らない。
    cache.Request("C:\\a");
    done.atMs = 9000;
    cache.Apply(done);
    cache.ForgetRelated("C:\\a");
    KITE_EXPECT_FALSE(cache.Get("C:\\a").known());
}

KITE_TEST(foldersize, the_table_drops_the_oldest_but_never_one_being_counted) {
    fs::FolderSizeCache cache;
    cache.SetCapacity(2);

    cache.Request("C:\\counting");  // 数え中のまま残す
    cache.Request("C:\\old");
    fs::FolderSizeUpdate u;
    u.path = "C:\\old";
    u.done = true;
    cache.Apply(u);

    cache.Request("C:\\new");
    KITE_EXPECT_EQ(static_cast<int>(cache.Get("C:\\counting").state),
                   static_cast<int>(fs::SizeState::Counting));
    KITE_EXPECT_FALSE(cache.Get("C:\\old").known());
}

// --- the app ----------------------------------------------------------------

KITE_TEST(foldersize, a_folder_on_screen_is_counted_by_itself) {
    AppHarness h;
    // 既定は «自動» ─ 訊かれた行がそのまま依頼になる。
    KITE_EXPECT_EQ(static_cast<int>(h.app.folderSizeMode()),
                   static_cast<int>(FolderSizeMode::Auto));
    KITE_EXPECT_EQ(static_cast<int>(h.Ask("alpha").state),
                   static_cast<int>(fs::SizeState::Counting));

    PumpUntilSettled(h.app);
    const fs::FolderSize value = h.Ask("alpha");
    KITE_EXPECT(value.settled());
    KITE_EXPECT_EQ(value.bytes, static_cast<uint64_t>(64));
    // ファイルの行には関係が無い。
    const fs::Entry* file = h.Entry("notes.txt");
    KITE_EXPECT_EQ(static_cast<int>(h.app.FolderSizeFor(h.tab().path, *file).state),
                   static_cast<int>(fs::SizeState::Unknown));
}

KITE_TEST(foldersize, the_status_line_names_the_counts_of_the_row_under_the_cursor) {
    AppHarness h;
    h.Measure("alpha");
    // カーソルを alpha に置く（先頭は ".." の行）。
    for (int i = 0; i < static_cast<int>(h.tab().visible.size()); ++i) {
        const fs::Entry* e = h.tab().EntryAt(i);
        if (e && e->name == "alpha") h.tab().cursor = i;
    }
    const std::string detail = h.app.folderSizeDetail();
    KITE_EXPECT(detail.find("1") != std::string::npos);   // ファイル 1
    KITE_EXPECT(detail.find("nested") == std::string::npos);
    // 数えている間は右に出る。
    KITE_EXPECT(h.app.folderSizeStatus().empty());
}

KITE_TEST(foldersize, an_unreadable_branch_is_reported_on_the_status_line) {
    AppHarness h;
    h.files.denied.push_back("C:\\home\\alpha\\nested");
    h.Measure("alpha");
    for (int i = 0; i < static_cast<int>(h.tab().visible.size()); ++i) {
        const fs::Entry* e = h.tab().EntryAt(i);
        if (e && e->name == "alpha") h.tab().cursor = i;
    }
    const std::string detail = h.app.folderSizeDetail();
    const Strings& str = h.app.strings();
    KITE_EXPECT(detail.find(str.Get("ui.folder_size_incomplete")) != std::string::npos);
}

KITE_TEST(foldersize, sorting_by_size_counts_every_folder_and_orders_them) {
    AppHarness h;
    h.app.Execute(Cmd::SortBySize);
    PumpUntilSettled(h.app);

    // 画面に出ている行だけでは順序が整わないので、一覧のフォルダを全部数える。
    KITE_EXPECT(h.Ask("beta").settled());
    KITE_EXPECT_EQ(h.Ask("beta").bytes, static_cast<uint64_t>(5000));

    const std::vector<std::string> names = h.VisibleNames();
    // フォルダが先（既定）で、その中は合計の昇順 ─ alpha(64) が beta(5000) より前。
    KITE_EXPECT(names.size() >= 2);
    KITE_EXPECT_EQ(names[0], std::string("alpha"));
    KITE_EXPECT_EQ(names[1], std::string("beta"));
}

KITE_TEST(foldersize, the_order_does_not_move_while_the_walk_is_still_going) {
    AppHarness h;
    // ゲートは «頼む前» に張る ─ 後から張ると、beta の列挙がもう終わっている
    // ことがあり、その日のスケジューラ次第で落ちるテストになる。
    h.files.Gate("C:\\home\\beta");
    h.app.Execute(Cmd::SortBySize);
    KITE_EXPECT(h.files.WaitForGate(1));

    // alpha だけが数え終わる。ここで並べ直すと、まだ 0 のままの beta が «いちばん
    // 小さいもの» として先頭へ来る ─ 数えている間ずっと行が動くのがこの形。
    for (int i = 0; i < 50; ++i) {
        h.app.PumpLoader();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    KITE_EXPECT_EQ(h.VisibleNames()[0], std::string("alpha"));

    h.files.Release();
    PumpUntilSettled(h.app);
    // 全部確定してから 1 回だけ並べ直す。64 < 5000 なので順序はそのまま。
    KITE_EXPECT_EQ(h.VisibleNames()[0], std::string("alpha"));
    KITE_EXPECT_EQ(h.VisibleNames()[1], std::string("beta"));
    KITE_EXPECT_EQ(h.Ask("beta").bytes, static_cast<uint64_t>(5000));
}

KITE_TEST(foldersize, a_change_in_the_folder_throws_the_count_away) {
    AppHarness h;
    KITE_EXPECT(h.Measure("alpha").settled());

    // 自分で起こした変更は、監視の通知を待たずに無効になる。
    h.files.AddFile("C:\\home\\alpha", "extra.bin", 1000, 20);
    h.app.Execute(Cmd::Refresh);
    PumpUntilSettled(h.app);

    // F5 は «訊き直せ» ─ 数えた値も捨てて、次に訊かれたときに歩き直す。
    KITE_EXPECT_EQ(static_cast<int>(h.Ask("alpha").state),
                   static_cast<int>(fs::SizeState::Counting));
    PumpUntilSettled(h.app);
    KITE_EXPECT_EQ(h.Ask("alpha").bytes, static_cast<uint64_t>(64 + 1000));
}

KITE_TEST(foldersize, nothing_is_counted_by_itself_when_the_setting_says_so) {
    AppHarness h;
    FakeFiles()["C:\\home\\config\\settings.ini"] = "[ui]\nfolder_sizes=manual\n";
    h.app.Execute(Cmd::ReloadConfig);
    PumpUntilSettled(h.app);
    KITE_EXPECT_EQ(static_cast<int>(h.app.folderSizeMode()),
                   static_cast<int>(FolderSizeMode::Manual));

    // 訊かれても数え始めない。
    KITE_EXPECT_EQ(static_cast<int>(h.Ask("alpha").state),
                   static_cast<int>(fs::SizeState::Unknown));
    PumpUntilSettled(h.app);
    KITE_EXPECT_EQ(static_cast<int>(h.Ask("alpha").state),
                   static_cast<int>(fs::SizeState::Unknown));

    // 頼まれれば数える。対象はカーソル行（選択が無いとき）。
    for (int i = 0; i < static_cast<int>(h.tab().visible.size()); ++i) {
        const fs::Entry* e = h.tab().EntryAt(i);
        if (e && e->name == "alpha") h.tab().cursor = i;
    }
    h.app.Execute(Cmd::CountFolderSize);
    PumpUntilSettled(h.app);
    KITE_EXPECT_EQ(h.Ask("alpha").bytes, static_cast<uint64_t>(64));
}

KITE_TEST(foldersize, the_off_setting_says_why_it_did_nothing) {
    AppHarness h;
    FakeFiles()["C:\\home\\config\\settings.ini"] = "[ui]\nfolder_sizes=off\n";
    h.app.Execute(Cmd::ReloadConfig);
    PumpUntilSettled(h.app);

    h.app.Execute(Cmd::CountFolderSize);
    // 「効かないキー」に見せない。
    KITE_EXPECT(!h.app.statusMessage().empty());
    KITE_EXPECT_EQ(static_cast<int>(h.Ask("alpha").state),
                   static_cast<int>(fs::SizeState::Unknown));
}

KITE_TEST(foldersize, stopping_stays_stopped_until_it_is_asked_for_again) {
    AppHarness h;
    // 数え始めさせてから、止める。
    h.Ask("alpha");
    h.app.Execute(Cmd::StopFolderSizes);
    KITE_EXPECT_FALSE(h.app.folderSizesBusy());

    // **自動で数える設定でも数え直さない。** «まだ» に戻すと、次に描いた 1 フレームが
    // そのまま «数えてくれ» になり、やめる道が無いのと同じになる。
    for (int i = 0; i < 3; ++i) {
        KITE_EXPECT_FALSE(h.Ask("alpha").known());
        h.app.PumpLoader();
    }

    // 頼まれれば数える。
    for (int i = 0; i < static_cast<int>(h.tab().visible.size()); ++i) {
        const fs::Entry* e = h.tab().EntryAt(i);
        if (e && e->name == "alpha") h.tab().cursor = i;
    }
    h.app.Execute(Cmd::CountFolderSize);
    PumpUntilSettled(h.app);
    KITE_EXPECT_EQ(h.Ask("alpha").bytes, static_cast<uint64_t>(64));
}

KITE_TEST(foldersize, stopping_answers_even_when_nothing_was_running) {
    AppHarness h;
    h.app.Execute(Cmd::StopFolderSizes);
    KITE_EXPECT_EQ(h.app.statusMessage(), h.app.strings().Get("ui.folder_size_stopped"));
    KITE_EXPECT_FALSE(h.app.folderSizesBusy());
}

KITE_TEST(foldersize, the_saved_mode_is_read_back_on_the_next_run) {
    AppHarness h;
    FakeFiles()["C:\\home\\config\\settings.ini"] = "[ui]\nfolder_sizes=off\n";
    h.app.Execute(Cmd::ReloadConfig);
    KITE_EXPECT_EQ(static_cast<int>(h.app.folderSizeMode()),
                   static_cast<int>(FolderSizeMode::Off));
    // 読めない綴りは «自動» に落ちる ─ 既定がそこなので、打ち間違いが
    // «この機能が消えた» という形で効かない。
    FakeFiles()["C:\\home\\config\\settings.ini"] = "[ui]\nfolder_sizes=sometimes\n";
    h.app.Execute(Cmd::ReloadConfig);
    KITE_EXPECT_EQ(static_cast<int>(h.app.folderSizeMode()),
                   static_cast<int>(FolderSizeMode::Auto));
}

// --- what the row actually says ---------------------------------------------

KITE_TEST(foldersize, the_number_is_faint_until_it_is_final) {
    AppHarness h;
    const Strings& str = h.app.strings();

    // 数え終わったかどうかを列だけで言えるようにする。末尾の「…」は 1 行を読めば
    // 分かるが、20 行を見渡して «どれが確定したのか» を言うのは明度のほう。
    h.Paint();
    const float growing = h.AlphaOf(str.Format("ui.size_counting", { FormatSize(0) }));

    PumpUntilSettled(h.app);
    h.Paint();
    const float settled = h.AlphaOf(FormatSize(64));
    KITE_EXPECT(growing < settled);

    // 確定した合計はファイルのサイズとまったく同じ顔になる ─ そこが狙いで、
    // フォルダの合計がファイルと同じ意味で読めるということ。
    KITE_EXPECT_NEAR(settled, h.AlphaOf(FormatSize(120)), 0.001f);
    // 数えていないことを言う `<DIR>` も薄めない ─ あれは «途中の値» ではなく
    // «値が無い» で、別のことを言っている。
    KITE_EXPECT_NEAR(h.AlphaOf(str.Get("ui.dir_marker")), settled, 0.001f);
}

KITE_TEST(foldersize, the_column_says_it_is_walking_and_then_says_the_total) {
    AppHarness h;
    const Strings& str = h.app.strings();

    // 1 フレーム目の問いがそのまま依頼になる（シェルアイコンと同じ形）。その間の
    // 表示は «増えていく数» で、確定した値と同じ顔にはしない。
    h.Paint();
    KITE_EXPECT(h.HasText(str.Format("ui.size_counting", { FormatSize(0) })));

    PumpUntilSettled(h.app);
    h.Paint();
    KITE_EXPECT(h.HasText(FormatSize(64)));
    KITE_EXPECT(h.HasText(FormatSize(5000)));
    // 「..」の行は数える相手ではないので今までどおり。
    KITE_EXPECT(h.HasText(str.Get("ui.dir_marker")));
}
