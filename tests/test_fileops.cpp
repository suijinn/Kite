// 非同期のファイル操作。削除もコピーも移動も、UI スレッドでは走らない ─ シェルの
// SHFileOperation は終わるまで戻らないので、そこで呼んでいた頃は大きなコピーの間
// ウィンドウがメッセージを 1 つも処理できなかった（「しばらく使えなくなる」と
// 報告された形がこれ）。
//
// 見るのは 3 つ。キュー（fs::FileOpQueue）が「実際に何を作ったか」を正しく答えるか、
// App が **完了したときにだけ** 履歴・文言・一覧の取り直しを行うか、そして
// **同じ場所を奪い合わない依頼だけが同時に走る**か。
//
// 並列であることは「両方をゲートで止めて数える」以外に決定的な見方が無い
// （速さで見ると、スケジューラの気分で落ちるテストになる）。
#include "Fakes.h"
#include "TestFramework.h"

#include "core/fs/FileOpQueue.h"

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

    // App の破棄はワーカーを join する。ゲートに止まったままだと返らないので、
    // メンバが壊れる前にここで開ける。
    ~Harness() { files.Release(); }

    void Settle() { test::PumpUntilSettled(app); }

    std::string Status() const { return app.statusMessage(); }
    std::string Text(const char* key) const { return app.strings().Get(key); }
};

// キューを直接回す側。ワーカーは本物のスレッドなので、窓と同じように待って回収する。
struct QueueHarness {
    test::FakeFileSystem files;
    test::FakeHost host;
    fs::FileOpQueue queue;

    QueueHarness() : queue(files, host) { test::PopulateStandardTree(files); }

    ~QueueHarness() { files.Release(); }

    // 完了を n 件ぶん回収するまで回す。
    std::vector<fs::FileOpDone> Collect(size_t n, int timeoutMs = 4000) {
        std::vector<fs::FileOpDone> done;
        for (int elapsed = 0; elapsed <= timeoutMs && done.size() < n; elapsed += 2) {
            queue.Drain(done);
            if (done.size() < n) std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return done;
    }

    // 1 件依頼して、その結果が返るまで回す。
    fs::FileOpDone Run(fs::FileOpRequest request) {
        const uint64_t token = queue.Request(std::move(request));
        std::vector<fs::FileOpDone> done;
        for (int elapsed = 0; elapsed <= 4000 && done.empty(); elapsed += 2) {
            queue.Drain(done);
            if (done.empty()) std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        if (done.empty()) return {};
        KITE_EXPECT_EQ(done[0].token, token);
        return done[0];
    }
};

fs::FileOpRequest TransferRequest(const std::vector<std::string>& sources,
                                  const std::string& destDir, bool move) {
    fs::FileOpRequest request;
    request.kind = fs::FileOpKind::Transfer;
    request.move = move;
    request.groups.push_back({ destDir, sources });
    return request;
}

}  // namespace

// ---------------------------------------------------------------------------
// キューそのもの
// ---------------------------------------------------------------------------

KITE_TEST(fileops, a_copy_answers_with_what_it_created) {
    QueueHarness h;
    const fs::FileOpDone done =
        h.Run(TransferRequest({ "C:\\home\\notes.txt" }, "C:\\home\\alpha", false));

    KITE_EXPECT(done.ok);
    KITE_EXPECT_EQ(done.created.size(), size_t{ 1 });
    KITE_EXPECT_EQ(done.created[0], std::string("C:\\home\\alpha\\notes.txt"));
    // コピーには戻る先が無い。
    KITE_EXPECT(done.origins.empty());
    KITE_EXPECT(h.files.Exists("C:\\home\\alpha\\notes.txt"));
}

// 移動だけが「どこから来たか」を答える ─ 戻すのにそれが要るのは移動だけ。
KITE_TEST(fileops, a_move_answers_with_where_each_item_came_from) {
    QueueHarness h;
    const fs::FileOpDone done =
        h.Run(TransferRequest({ "C:\\home\\notes.txt" }, "C:\\home\\alpha", true));

    KITE_EXPECT(done.ok);
    KITE_EXPECT_EQ(done.created.size(), size_t{ 1 });
    KITE_EXPECT_EQ(done.origins.size(), size_t{ 1 });
    KITE_EXPECT_EQ(done.origins[0], std::string("C:\\home\\notes.txt"));
}

// 操作の前から在ったものは、この操作の産物ではない。上書きされていようと別名が
// 付いていようと、触ってよいとは言えない。
KITE_TEST(fileops, a_destination_that_already_existed_is_not_reported_as_created) {
    QueueHarness h;
    h.files.AddFile("C:\\home\\beta", "notes.txt");

    const fs::FileOpDone done =
        h.Run(TransferRequest({ "C:\\home\\notes.txt" }, "C:\\home\\beta", false));

    KITE_EXPECT(done.ok);
    KITE_EXPECT(done.created.empty());
}

KITE_TEST(fileops, a_duplicate_reports_the_names_that_arrived) {
    QueueHarness h;
    fs::FileOpRequest request;
    request.kind = fs::FileOpKind::Duplicate;
    request.paths = { "C:\\home\\notes.txt" };
    request.destPaths = { "C:\\home\\notes_copy.txt" };

    const fs::FileOpDone done = h.Run(std::move(request));
    KITE_EXPECT(done.ok);
    KITE_EXPECT_EQ(done.created.size(), size_t{ 1 });
    KITE_EXPECT_EQ(done.created[0], std::string("C:\\home\\notes_copy.txt"));
}

KITE_TEST(fileops, a_failure_comes_back_with_the_reason_and_nothing_created) {
    QueueHarness h;
    h.files.locked.push_back("C:\\home\\notes.txt");
    h.files.lockedMessage = "in use";

    const fs::FileOpDone done =
        h.Run(TransferRequest({ "C:\\home\\notes.txt" }, "C:\\home\\alpha", false));

    KITE_EXPECT_FALSE(done.ok);
    KITE_EXPECT_EQ(done.error, std::string("in use"));
    KITE_EXPECT(done.created.empty());
}

// 「移動を戻す」は元のフォルダごとに分かれる。1 まとまりが落ちたらそこで止める ─
// 半分だけ戻ったものを成功として答えると、元どおりだと信じさせることになる。
KITE_TEST(fileops, a_transfer_stops_at_the_first_group_that_fails) {
    QueueHarness h;
    h.files.AddFile("C:\\home\\alpha", "one.txt");
    h.files.AddFile("C:\\home\\alpha", "two.txt");
    h.files.locked.push_back("C:\\home\\alpha\\two.txt");

    fs::FileOpRequest request;
    request.kind = fs::FileOpKind::Transfer;
    request.move = true;
    request.groups.push_back({ "C:\\home", { "C:\\home\\alpha\\one.txt" } });
    request.groups.push_back({ "C:\\home\\beta", { "C:\\home\\alpha\\two.txt" } });

    const fs::FileOpDone done = h.Run(std::move(request));
    KITE_EXPECT_FALSE(done.ok);
    // 先に済んだぶんは実際に動いているので、答えからも消さない。
    KITE_EXPECT_EQ(done.created.size(), size_t{ 1 });
    KITE_EXPECT_EQ(done.created[0], std::string("C:\\home\\one.txt"));
}

KITE_TEST(fileops, a_delete_goes_where_it_was_told) {
    QueueHarness h;
    fs::FileOpRequest request;
    request.kind = fs::FileOpKind::Delete;
    request.paths = { "C:\\home\\notes.txt" };
    request.recycle = false;

    const fs::FileOpDone done = h.Run(std::move(request));
    KITE_EXPECT(done.ok);
    KITE_EXPECT_EQ(h.files.deleteRecycle.size(), size_t{ 1 });
    KITE_EXPECT_FALSE(h.files.deleteRecycle[0]);
    KITE_EXPECT_FALSE(h.files.Exists("C:\\home\\notes.txt"));
}

// ---------------------------------------------------------------------------
// 衝突の判定 ─ スレッドを起こさずに規則そのものを見る
// ---------------------------------------------------------------------------

namespace {

fs::FileOpRequest DeleteRequest(const std::vector<std::string>& paths) {
    fs::FileOpRequest request;
    request.kind = fs::FileOpKind::Delete;
    request.paths = paths;
    return request;
}

bool Conflicts(const fs::FileOpRequest& a, const fs::FileOpRequest& b) {
    return fs::FileOpsConflict(fs::FileOpTouches(a), fs::FileOpTouches(b));
}

}  // namespace

// これを衝突扱いにすると、並列にした意味がちょうど失われる ─ 「大きなコピーの裏で
// 別のファイルを消す」がまさにこの形。だから触る場所に親フォルダは入れない。
KITE_TEST(fileops, work_in_one_folder_does_not_block_work_on_another_item_there) {
    const fs::FileOpRequest copy =
        TransferRequest({ "C:\\home\\big" }, "E:\\backup", false);
    const fs::FileOpRequest del = DeleteRequest({ "C:\\home\\notes.txt" });
    KITE_EXPECT_FALSE(Conflicts(copy, del));
}

// 行き先が同じフォルダでも、置く名前が違えば奪い合いではない ─ ここを
// フォルダごと名乗らせると、USB へ 2 つコピーするだけで並列にならなくなる
// （実際に「並列にならない」と報告された形がこれ）。
KITE_TEST(fileops, two_transfers_into_one_folder_do_not_conflict) {
    const fs::FileOpRequest a = TransferRequest({ "C:\\one\\a.txt" }, "C:\\dest", false);
    const fs::FileOpRequest b = TransferRequest({ "C:\\two\\b.txt" }, "C:\\dest", false);
    KITE_EXPECT_FALSE(Conflicts(a, b));
}

// 名前まで一致したときだけ待たせる。ここが «操作の前に無く、後に在る» の判定が
// 隣の産物を拾いうる唯一の形で、通すと Ctrl+Z が他方の作ったファイルを消す。
KITE_TEST(fileops, two_transfers_landing_on_one_name_conflict) {
    const fs::FileOpRequest a = TransferRequest({ "C:\\one\\same.txt" }, "C:\\dest", false);
    const fs::FileOpRequest b = TransferRequest({ "C:\\two\\same.txt" }, "C:\\dest", false);
    KITE_EXPECT(Conflicts(a, b));
}

// 行き先に置く当の名前に触る依頼も同じ場所の話。
KITE_TEST(fileops, a_transfer_conflicts_with_work_on_the_name_it_will_write) {
    const fs::FileOpRequest copy = TransferRequest({ "C:\\one\\a.txt" }, "C:\\dest", false);
    KITE_EXPECT(Conflicts(copy, DeleteRequest({ "C:\\dest\\a.txt" })));
    // 同じフォルダの別の名前は関係が無い。
    KITE_EXPECT_FALSE(Conflicts(copy, DeleteRequest({ "C:\\dest\\other.txt" })));
}

// コピー元は読むだけ。1 つのファイルを 2 か所へ配るのは奪い合いではない ─
// 同じクリップボードを 2 つのフォルダへ貼るのがこの形。
KITE_TEST(fileops, copying_one_file_to_two_places_is_not_a_tug_of_war) {
    const fs::FileOpRequest a = TransferRequest({ "C:\\one\\a.txt" }, "C:\\dest1", false);
    const fs::FileOpRequest b = TransferRequest({ "C:\\one\\a.txt" }, "C:\\dest2", false);
    KITE_EXPECT_FALSE(Conflicts(a, b));
}

// 移動は元を空にするので書く側。同じものを 2 度動かす依頼は順番に。
KITE_TEST(fileops, moving_the_same_file_twice_is_serialized) {
    const fs::FileOpRequest a = TransferRequest({ "C:\\one\\a.txt" }, "C:\\dest1", true);
    const fs::FileOpRequest b = TransferRequest({ "C:\\one\\a.txt" }, "C:\\dest2", true);
    KITE_EXPECT(Conflicts(a, b));
}

// 読む側と書く側が同じものを指したら待たせる ─ 消されている最中のファイルを
// コピーしても、途中までのものしか渡らない。
KITE_TEST(fileops, copying_a_file_that_another_request_deletes_conflicts) {
    const fs::FileOpRequest copy = TransferRequest({ "C:\\one\\a.txt" }, "C:\\dest", false);
    KITE_EXPECT(Conflicts(copy, DeleteRequest({ "C:\\one\\a.txt" })));
    // フォルダごと消す依頼とも（その中に在るものなので）。
    KITE_EXPECT(Conflicts(copy, DeleteRequest({ "C:\\one" })));
}

// フォルダを消す依頼と、その中へ入れる依頼。文字列としては一致しない。
KITE_TEST(fileops, deleting_a_folder_conflicts_with_copying_into_it) {
    const fs::FileOpRequest del = DeleteRequest({ "C:\\home\\alpha" });
    const fs::FileOpRequest copy =
        TransferRequest({ "C:\\other\\x.txt" }, "C:\\home\\alpha\\nested", false);
    KITE_EXPECT(Conflicts(del, copy));
}

// 名前の頭が同じだけの兄弟は別の場所。
KITE_TEST(fileops, a_sibling_with_a_shared_prefix_is_not_the_same_place) {
    KITE_EXPECT_FALSE(Conflicts(DeleteRequest({ "C:\\home\\alpha" }),
                                DeleteRequest({ "C:\\home\\alpha2" })));
}

// 区切りと大文字小文字はまたぐ。
KITE_TEST(fileops, the_same_place_spelled_differently_still_conflicts) {
    KITE_EXPECT(Conflicts(DeleteRequest({ "C:/home/alpha/inner.md" }),
                          DeleteRequest({ "c:\\HOME\\alpha\\inner.md" })));
}

// 複製は行き先の名前を自分で名乗る ─ そこを他の依頼に取られてはならない。
KITE_TEST(fileops, a_duplicate_claims_the_names_it_picked) {
    fs::FileOpRequest dup;
    dup.kind = fs::FileOpKind::Duplicate;
    dup.paths = { "C:\\home\\notes.txt" };
    dup.destPaths = { "C:\\home\\notes_copy.txt" };
    KITE_EXPECT(Conflicts(dup, DeleteRequest({ "C:\\home\\notes_copy.txt" })));
}

// 項目が多すぎる依頼は親フォルダを名乗る。総当たりの比較が 1 万件で 1 億回に
// なるのを避けるためで、それだけの項目を持つ依頼は実質そのフォルダを相手にしている。
KITE_TEST(fileops, a_very_large_request_claims_the_folder_instead_of_every_item) {
    std::vector<std::string> many;
    for (size_t i = 0; i <= fs::kFileOpTouchLimit; ++i) {
        many.push_back("C:\\home\\file" + std::to_string(i) + ".txt");
    }
    const std::vector<fs::FileOpTouch> touches = fs::FileOpTouches(DeleteRequest(many));
    KITE_EXPECT_EQ(touches.size(), size_t{ 1 });
    KITE_EXPECT_EQ(touches[0].path, std::string("C:\\home"));
    KITE_EXPECT(touches[0].write);
    // そのぶん、同じフォルダの中の別件は待たされる側になる。
    KITE_EXPECT(Conflicts(DeleteRequest(many), DeleteRequest({ "C:\\home\\notes.txt" })));
}

// ---------------------------------------------------------------------------
// 並列に走ること
// ---------------------------------------------------------------------------

KITE_TEST(fileops, requests_that_share_no_place_run_at_the_same_time) {
    QueueHarness h;
    h.files.Gate("C:\\one\\a.txt");
    h.files.Gate("C:\\two\\b.txt");

    h.queue.Request(TransferRequest({ "C:\\one\\a.txt" }, "C:\\dest1", false));
    h.queue.Request(TransferRequest({ "C:\\two\\b.txt" }, "C:\\dest2", false));

    // 2 本とも操作の中まで入った ─ これが「同時に走っている」の実体。
    KITE_EXPECT(h.files.WaitForGate(2));
    KITE_EXPECT_EQ(h.queue.running(), 2);

    h.files.Release();
    KITE_EXPECT_EQ(h.Collect(2).size(), size_t{ 2 });
}

KITE_TEST(fileops, requests_that_want_the_same_place_wait_for_each_other) {
    QueueHarness h;
    h.files.Gate("C:\\one\\same.txt");

    h.queue.Request(TransferRequest({ "C:\\one\\same.txt" }, "C:\\dest", false));
    KITE_EXPECT(h.files.WaitForGate(1));
    // 同じ名前に着地するので、2 つ目は席が空いていても走り出せない。
    h.queue.Request(TransferRequest({ "C:\\two\\same.txt" }, "C:\\dest", false));

    // 走り出していないことは、少し待っても実行中が 1 件のままであることで見る。
    for (int i = 0; i < 25 && h.queue.running() < 2; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    KITE_EXPECT_EQ(h.queue.running(), 1);
    KITE_EXPECT_EQ(h.queue.pending(), 2);

    h.files.Release();
    KITE_EXPECT_EQ(h.Collect(2).size(), size_t{ 2 });
}

// 衝突する依頼の順序は依頼した順。後から来たものが先に走ると、2 つは逆順に
// 適用される ─ 「コピーしてから消す」が「消してからコピー」になる。
KITE_TEST(fileops, conflicting_requests_keep_the_order_they_were_asked_in) {
    QueueHarness h;
    h.files.AddDir("C:\\alt");
    h.files.AddFile("C:\\home", "same.txt");
    h.files.AddFile("C:\\alt", "same.txt");
    h.files.Gate("C:\\home\\same.txt");

    const uint64_t a = h.queue.Request(TransferRequest({ "C:\\home\\same.txt" }, "C:\\dest", true));
    KITE_EXPECT(h.files.WaitForGate(1));
    const uint64_t b = h.queue.Request(TransferRequest({ "C:\\alt\\same.txt" }, "C:\\dest", true));

    h.files.Release();
    const std::vector<fs::FileOpDone> done = h.Collect(2);
    KITE_EXPECT_EQ(done.size(), size_t{ 2 });
    KITE_EXPECT_EQ(done[0].token, a);
    KITE_EXPECT_EQ(done[1].token, b);
}

// 上限が要るのは、進捗ダイアログが積み上がるのと、同じディスクでは並列コピーが
// かえって遅いから。
KITE_TEST(fileops, no_more_than_the_limit_run_at_once) {
    QueueHarness h;
    for (int i = 0; i < 5; ++i) {
        const std::string src = "C:\\src" + std::to_string(i) + "\\item.txt";
        h.files.Gate(src);
        h.queue.Request(TransferRequest({ src }, "C:\\dest" + std::to_string(i), false));
    }

    KITE_EXPECT(h.files.WaitForGate(fs::kFileOpWorkers));
    for (int i = 0; i < 25 && h.queue.running() <= fs::kFileOpWorkers; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    KITE_EXPECT_EQ(h.queue.running(), fs::kFileOpWorkers);
    KITE_EXPECT_EQ(h.queue.pending(), 5);

    h.files.Release();
    KITE_EXPECT_EQ(h.Collect(5).size(), size_t{ 5 });
}

// ---------------------------------------------------------------------------
// App から見た非同期性
// ---------------------------------------------------------------------------

KITE_TEST(fileops, a_delete_is_still_running_when_the_keystroke_returns) {
    Harness h;
    h.app.Execute(Cmd::CursorBottom);  // notes.txt
    h.app.Execute(Cmd::DeleteToRecycle);
    h.app.OnKey(ParseChord("Enter"));

    // 依頼しただけ。履歴も文言も、まだ何も動いていない。
    KITE_EXPECT(h.app.fileOpsBusy());
    KITE_EXPECT(h.app.undoStack().empty());

    h.Settle();
    KITE_EXPECT_FALSE(h.app.fileOpsBusy());
    KITE_EXPECT_FALSE(h.files.Exists("C:\\home\\notes.txt"));
    KITE_EXPECT_FALSE(h.app.undoStack().empty());
    KITE_EXPECT_EQ(h.Status(), h.Text("ui.done"));
}

// 数分かかる操作の間、画面が黙っていると「押したのに何も起きていない」と
// 見分けが付かない。しかもこの行は期限で消えない。
KITE_TEST(fileops, the_status_line_says_what_is_running) {
    Harness h;
    h.app.Execute(Cmd::CursorBottom);
    h.app.Execute(Cmd::DeleteToRecycle);
    h.app.OnKey(ParseChord("Enter"));

    KITE_EXPECT_EQ(h.app.fileOpStatus(), h.Text("ui.deleting"));

    h.Settle();
    KITE_EXPECT(h.app.fileOpStatus().empty());
}

// 同じフォルダの 2 件は奪い合うので順番待ちになる。黙っていると、2 回目のキーが
// 効かなかったようにしか見えない。
KITE_TEST(fileops, a_queued_operation_says_how_many_are_waiting) {
    Harness h;
    h.app.Execute(Cmd::CursorBottom);  // notes.txt
    const std::string first = h.app.workspace().focusedTab()->SelectionPaths()[0];
    h.files.Gate(first);

    h.app.Execute(Cmd::DeleteToRecycle);
    h.app.OnKey(ParseChord("Enter"));
    KITE_EXPECT(h.files.WaitForGate(1));

    h.app.Execute(Cmd::CursorUp);
    h.app.Execute(Cmd::DeleteToRecycle);
    h.app.OnKey(ParseChord("Enter"));

    const std::string expected =
        h.Text("ui.deleting") + h.app.strings().Format("ui.file_op_more", { "1" });
    KITE_EXPECT_EQ(h.app.fileOpStatus(), expected);

    h.files.Release();
    h.Settle();
    KITE_EXPECT(h.app.fileOpStatus().empty());
}

// 2 件以上が同時に走ると、どちらの言葉で言うことも嘘になりうる（削除とコピーが
// 混ざる）ので、件数で言う。
KITE_TEST(fileops, two_operations_at_once_are_reported_as_a_count) {
    Harness h;
    h.files.AddDir("C:\\one");
    h.files.AddDir("C:\\two");
    h.files.AddDir("C:\\three");
    h.files.AddDir("C:\\four");
    h.files.AddFile("C:\\one", "a.txt");
    h.files.AddFile("C:\\three", "b.txt");
    h.files.Gate("C:\\one\\a.txt");
    h.files.Gate("C:\\three\\b.txt");

    KITE_EXPECT(h.app.PerformDrop({ "C:\\one\\a.txt" }, "C:\\two", false));
    KITE_EXPECT(h.app.PerformDrop({ "C:\\three\\b.txt" }, "C:\\four", false));
    KITE_EXPECT(h.files.WaitForGate(2));

    KITE_EXPECT_EQ(h.app.fileOpStatus(),
                   h.app.strings().Format("ui.file_op_running", { "2" }));

    h.files.Release();
    h.Settle();
    KITE_EXPECT(h.files.Exists("C:\\two\\a.txt"));
    KITE_EXPECT(h.files.Exists("C:\\four\\b.txt"));
    KITE_EXPECT(h.app.fileOpStatus().empty());
}

// 実際に報告された形 ─ 同じフォルダ（USB のルートなど）へ 2 つコピーする。
// 行き先をフォルダごと名乗っていた頃は、これが順番待ちになっていた。
KITE_TEST(fileops, two_copies_into_the_same_folder_run_at_once) {
    Harness h;
    h.files.AddDir("C:\\src1");
    h.files.AddDir("C:\\src2");
    h.files.AddDir("C:\\usb");
    h.files.AddFile("C:\\src1", "one.bin");
    h.files.AddFile("C:\\src2", "two.bin");
    h.files.Gate("C:\\src1\\one.bin");
    h.files.Gate("C:\\src2\\two.bin");

    KITE_EXPECT(h.app.PerformDrop({ "C:\\src1\\one.bin" }, "C:\\usb", false));
    KITE_EXPECT(h.app.PerformDrop({ "C:\\src2\\two.bin" }, "C:\\usb", false));

    KITE_EXPECT(h.files.WaitForGate(2));
    KITE_EXPECT_EQ(h.app.fileOpStatus(),
                   h.app.strings().Format("ui.file_op_running", { "2" }));

    h.files.Release();
    h.Settle();
    KITE_EXPECT(h.files.Exists("C:\\usb\\one.bin"));
    KITE_EXPECT(h.files.Exists("C:\\usb\\two.bin"));
}

// 無関係な依頼は、先に頼まれた大きな操作を待たない ─ この機能が在る理由そのもの。
KITE_TEST(fileops, an_unrelated_operation_does_not_wait_for_a_long_one) {
    Harness h;
    h.files.AddDir("C:\\slow");
    h.files.AddDir("C:\\slow_dest");
    h.files.AddFile("C:\\slow", "big.bin");
    h.files.Gate("C:\\slow\\big.bin");

    KITE_EXPECT(h.app.PerformDrop({ "C:\\slow\\big.bin" }, "C:\\slow_dest", false));
    KITE_EXPECT(h.files.WaitForGate(1));

    // 止まったままのコピーの裏で、別フォルダの削除が最後まで通る。
    h.app.Execute(Cmd::CursorBottom);  // notes.txt
    h.app.Execute(Cmd::DeleteToRecycle);
    h.app.OnKey(ParseChord("Enter"));
    for (int i = 0; i < 500 && h.files.Exists("C:\\home\\notes.txt"); ++i) {
        h.app.PumpLoader();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    KITE_EXPECT_FALSE(h.files.Exists("C:\\home\\notes.txt"));
    // コピーのほうはまだゲートの中。
    KITE_EXPECT(h.app.fileOpsBusy());

    h.files.Release();
    h.Settle();
    KITE_EXPECT(h.files.Exists("C:\\slow_dest\\big.bin"));
}

// 実行中の操作の履歴が積まれるのは完了したとき。そこで Ctrl+Z を通すと、
// **1 つ前の操作**が巻き戻る ─ 押した人が指しているのは、たった今頼んだほう。
KITE_TEST(fileops, undo_waits_for_the_operation_in_flight) {
    Harness h;
    h.app.Execute(Cmd::CursorBottom);
    h.app.Execute(Cmd::DeleteToRecycle);
    h.app.OnKey(ParseChord("Enter"));

    h.app.Execute(Cmd::Undo);
    KITE_EXPECT_EQ(h.Status(), h.Text("ui.file_op_busy"));

    h.Settle();
    h.app.Execute(Cmd::Undo);
    h.Settle();
    KITE_EXPECT_EQ(h.Status(), h.Text("ui.undone_delete"));
}

// 完了して初めて一覧を取り直す ─ 監視の通知が来ない場所（張れなかったフォルダ）
// でも、行が消えたことが画面に出ていなければならない。
KITE_TEST(fileops, the_listing_is_taken_again_when_the_operation_finishes) {
    Harness h;
    const size_t before = h.app.workspace().focusedTab()->listing.entries.size();
    h.app.Execute(Cmd::CursorBottom);
    h.app.Execute(Cmd::DeleteToRecycle);
    h.app.OnKey(ParseChord("Enter"));
    h.Settle();

    KITE_EXPECT_EQ(h.app.workspace().focusedTab()->listing.entries.size(), before - 1);
}

// 貼り付けも同じ ─ 依頼した時点ではまだ何も無く、切り取りの印もまだ落ちない。
KITE_TEST(fileops, a_paste_lands_only_after_the_pump) {
    Harness h;
    h.app.Execute(Cmd::CursorBottom);  // notes.txt
    h.app.Execute(Cmd::Cut);
    h.app.NavigateFocused("C:\\home\\alpha");
    h.Settle();

    h.app.Execute(Cmd::Paste);
    // ディスクの側はワーカーの手の中なので、ここで見てよいのは App の状態だけ。
    KITE_EXPECT(h.app.fileOpsBusy());
    KITE_EXPECT(h.app.IsCut("C:\\home\\notes.txt"));

    h.Settle();
    KITE_EXPECT(h.files.Exists("C:\\home\\alpha\\notes.txt"));
    KITE_EXPECT_FALSE(h.app.IsCut("C:\\home\\notes.txt"));
}

// 失敗したら印は残る ─ 何を切り取ってあったかを言うものが、画面から消えては困る。
KITE_TEST(fileops, a_paste_that_failed_keeps_the_cut_marks) {
    Harness h;
    h.app.Execute(Cmd::CursorBottom);  // notes.txt
    h.app.Execute(Cmd::Cut);
    h.files.locked.push_back("C:\\home\\notes.txt");
    h.app.NavigateFocused("C:\\home\\alpha");
    h.Settle();

    h.app.Execute(Cmd::Paste);
    h.Settle();

    KITE_EXPECT(h.app.IsCut("C:\\home\\notes.txt"));
    KITE_EXPECT_EQ(h.Status(), h.Text("ui.move_failed"));
}
