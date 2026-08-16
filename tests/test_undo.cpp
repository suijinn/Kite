// Undo (ROADMAP P1-2). Kite keeps its own history rather than leaning on the
// shell's: that stack has no public entry point, and renames never reach it at
// all. What matters here is the boundary of what undo is allowed to touch - the
// files it created, and nothing that was already there.
#include "Fakes.h"
#include "TestFramework.h"

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

    void Settle() { test::PumpUntilSettled(app); }

    void Type(const std::string& text) {
        for (char c : text) app.OnChar(static_cast<uint32_t>(c));
    }

    // A prompt-driven command, answered and committed the way a person does it.
    // Ctrl+A first because Rename opens with the old name already in the field.
    void Commit(Cmd cmd, const std::string& text) {
        app.Execute(cmd);
        app.OnKey(ParseChord("Ctrl+A"));
        Type(text);
        app.OnKey(ParseChord("Enter"));
        Settle();
    }

    std::string Status() const { return app.statusMessage(); }
    std::string Text(const char* key) const { return app.strings().Get(key); }
};

}  // namespace

// ---------------------------------------------------------------------------
// The stack itself
// ---------------------------------------------------------------------------

KITE_TEST(undo, the_stack_forgets_the_oldest_beyond_its_limit) {
    UndoStack stack;
    for (size_t i = 0; i < UndoStack::kLimit + 5; ++i) {
        stack.Push({ UndoKind::Create, { "C:\\x" + std::to_string(i) }, {} });
    }
    KITE_EXPECT_EQ(stack.size(), UndoStack::kLimit);
    // The newest survives; the discarded end is the old one.
    const std::string newest = stack.top()->targets[0];
    KITE_EXPECT_EQ(newest, std::string("C:\\x") + std::to_string(UndoStack::kLimit + 4));
}

KITE_TEST(undo, a_delete_drops_everything_underneath_it) {
    UndoStack stack;
    stack.Push({ UndoKind::Create, { "C:\\a" }, {} });
    stack.Push({ UndoKind::Create, { "C:\\b" }, {} });
    KITE_EXPECT_EQ(stack.size(), size_t{ 2 });

    // Those two are unreachable from here on, and reaching them would be worse
    // than losing them: the deleted file would stay gone while an older,
    // unrelated operation quietly rolled back.
    stack.Push({ UndoKind::Delete, {}, {} });
    KITE_EXPECT_EQ(stack.size(), size_t{ 1 });
    KITE_EXPECT_EQ(static_cast<int>(stack.top()->kind), static_cast<int>(UndoKind::Delete));
}

KITE_TEST(undo, popping_an_empty_stack_is_harmless) {
    UndoStack stack;
    stack.Pop();
    KITE_EXPECT(stack.empty());
    KITE_EXPECT(stack.top() == nullptr);
}

// ---------------------------------------------------------------------------
// Through the controller
// ---------------------------------------------------------------------------

KITE_TEST(undo, with_no_history_it_says_so_and_touches_nothing) {
    Harness h;
    h.app.Execute(Cmd::Undo);
    KITE_EXPECT_EQ(h.Status(), h.Text("ui.undo_empty"));
    KITE_EXPECT_EQ(h.files.deleteCalls.size(), size_t{ 0 });
    KITE_EXPECT_EQ(h.files.copyCalls.size(), size_t{ 0 });
}

KITE_TEST(undo, a_rename_goes_back_to_the_old_name) {
    Harness h;
    h.app.Execute(Cmd::CursorBottom);  // notes.txt
    h.Commit(Cmd::Rename, "renamed.txt");

    KITE_EXPECT(h.files.Exists("C:\\home\\renamed.txt"));
    KITE_EXPECT_FALSE(h.files.Exists("C:\\home\\notes.txt"));
    KITE_EXPECT_EQ(h.app.undoStack().size(), size_t{ 1 });

    h.app.Execute(Cmd::Undo);
    h.Settle();
    KITE_EXPECT(h.files.Exists("C:\\home\\notes.txt"));
    KITE_EXPECT_FALSE(h.files.Exists("C:\\home\\renamed.txt"));
    KITE_EXPECT_EQ(h.Status(), h.Text("ui.undone_rename"));
    KITE_EXPECT(h.app.undoStack().empty());
}

KITE_TEST(undo, a_rename_undone_twice_does_not_ping_pong) {
    Harness h;
    h.app.Execute(Cmd::CursorBottom);
    h.Commit(Cmd::Rename, "renamed.txt");
    h.app.Execute(Cmd::Undo);
    h.Settle();

    // The inverse is not itself recorded, so there is no redo to fall into.
    h.app.Execute(Cmd::Undo);
    KITE_EXPECT_EQ(h.Status(), h.Text("ui.undo_empty"));
    KITE_EXPECT(h.files.Exists("C:\\home\\notes.txt"));
}

KITE_TEST(undo, a_rename_whose_old_name_was_taken_again_is_refused) {
    Harness h;
    h.app.Execute(Cmd::CursorBottom);
    h.Commit(Cmd::Rename, "renamed.txt");

    // Something else claims the vacated name in the meantime. Undoing here
    // would either fail or overwrite; neither is what the user asked for.
    h.files.AddFile("C:\\home", "notes.txt");

    h.app.Execute(Cmd::Undo);
    h.Settle();
    KITE_EXPECT_EQ(h.Status(), h.Text("ui.undo_stale"));
    KITE_EXPECT(h.files.Exists("C:\\home\\renamed.txt"));
    // Spent either way: retrying will not make it any more applicable.
    KITE_EXPECT(h.app.undoStack().empty());
}

KITE_TEST(undo, a_new_folder_goes_to_the_recycle_bin) {
    Harness h;
    h.Commit(Cmd::NewFolder, "gamma");
    KITE_EXPECT(h.files.Exists("C:\\home\\gamma"));

    h.app.Execute(Cmd::Undo);
    h.Settle();
    KITE_EXPECT_FALSE(h.files.Exists("C:\\home\\gamma"));
    KITE_EXPECT_EQ(h.files.deleteCalls.size(), size_t{ 1 });
    // Never permanently - a folder made an hour ago may have been filled since.
    KITE_EXPECT(h.files.deleteRecycle[0]);
    KITE_EXPECT_EQ(h.Status(), h.Text("ui.undone_create"));
}

KITE_TEST(undo, a_new_file_is_undone_the_same_way) {
    Harness h;
    h.Commit(Cmd::NewFile, "fresh.txt");
    KITE_EXPECT(h.files.Exists("C:\\home\\fresh.txt"));

    h.app.Execute(Cmd::Undo);
    h.Settle();
    KITE_EXPECT_FALSE(h.files.Exists("C:\\home\\fresh.txt"));
}

KITE_TEST(undo, a_pasted_copy_is_thrown_out_again) {
    Harness h;
    h.app.Execute(Cmd::CursorBottom);  // notes.txt
    h.app.Execute(Cmd::Copy);
    h.app.NavigateFocused("C:\\home\\alpha");
    h.Settle();
    h.app.Execute(Cmd::Paste);
    h.Settle();
    KITE_EXPECT(h.files.Exists("C:\\home\\alpha\\notes.txt"));

    h.app.Execute(Cmd::Undo);
    h.Settle();
    KITE_EXPECT_FALSE(h.files.Exists("C:\\home\\alpha\\notes.txt"));
    // The original is untouched: undo removes the copy, not the thing copied.
    KITE_EXPECT(h.files.Exists("C:\\home\\notes.txt"));
    KITE_EXPECT_EQ(h.Status(), h.Text("ui.undone_copy"));
}

KITE_TEST(undo, a_paste_that_replaced_a_file_records_nothing) {
    Harness h;
    // beta already holds a notes.txt. Whatever sits at that name after the
    // paste is either the file that was always there or the shell's replacement
    // of it - and this operation is not entitled to throw either one away.
    h.files.AddFile("C:\\home\\beta", "notes.txt");

    h.app.Execute(Cmd::CursorBottom);
    h.app.Execute(Cmd::Copy);
    h.app.NavigateFocused("C:\\home\\beta");
    h.Settle();
    h.app.Execute(Cmd::Paste);
    h.Settle();

    KITE_EXPECT(h.app.undoStack().empty());
    h.app.Execute(Cmd::Undo);
    KITE_EXPECT_EQ(h.Status(), h.Text("ui.undo_empty"));
    KITE_EXPECT(h.files.Exists("C:\\home\\beta\\notes.txt"));
    KITE_EXPECT_EQ(h.files.deleteCalls.size(), size_t{ 0 });
}

KITE_TEST(undo, a_cut_paste_moves_back_where_it_came_from) {
    Harness h;
    h.app.Execute(Cmd::CursorBottom);  // notes.txt
    h.app.Execute(Cmd::Cut);
    h.app.NavigateFocused("C:\\home\\alpha");
    h.Settle();
    h.app.Execute(Cmd::Paste);
    h.Settle();
    KITE_EXPECT(h.files.Exists("C:\\home\\alpha\\notes.txt"));
    KITE_EXPECT_FALSE(h.files.Exists("C:\\home\\notes.txt"));

    h.app.Execute(Cmd::Undo);
    h.Settle();
    KITE_EXPECT(h.files.Exists("C:\\home\\notes.txt"));
    KITE_EXPECT_FALSE(h.files.Exists("C:\\home\\alpha\\notes.txt"));
    KITE_EXPECT_EQ(h.Status(), h.Text("ui.undone_move"));

    // Moved back, not deleted and re-created.
    KITE_EXPECT_EQ(h.files.copyCalls.size(), size_t{ 2 });
    KITE_EXPECT(h.files.copyCalls[1].move);
    KITE_EXPECT_EQ(h.files.copyCalls[1].destDir, std::string("C:\\home"));
    KITE_EXPECT_EQ(h.files.deleteCalls.size(), size_t{ 0 });
}

KITE_TEST(undo, a_dropped_move_is_undone_as_one_call_per_source_folder) {
    Harness h;
    // Two files from two different folders, dropped into the same place.
    KITE_EXPECT(h.app.PerformDrop({ "C:\\home\\notes.txt", "C:\\home\\alpha\\inner.md" },
                                  "C:\\home\\beta", true));
    h.Settle();
    KITE_EXPECT(h.files.Exists("C:\\home\\beta\\notes.txt"));
    KITE_EXPECT(h.files.Exists("C:\\home\\beta\\inner.md"));

    h.app.Execute(Cmd::Undo);
    h.Settle();
    KITE_EXPECT(h.files.Exists("C:\\home\\notes.txt"));
    KITE_EXPECT(h.files.Exists("C:\\home\\alpha\\inner.md"));

    // One transfer out, then one call back per origin folder - not one per file.
    KITE_EXPECT_EQ(h.files.copyCalls.size(), size_t{ 3 });
    KITE_EXPECT_EQ(h.files.copyCalls[1].destDir, std::string("C:\\home"));
    KITE_EXPECT_EQ(h.files.copyCalls[2].destDir, std::string("C:\\home\\alpha"));
}

KITE_TEST(undo, deleting_blocks_the_history_instead_of_reaching_past_it) {
    Harness h;
    h.Commit(Cmd::NewFolder, "gamma");
    KITE_EXPECT_EQ(h.app.undoStack().size(), size_t{ 1 });

    h.app.Execute(Cmd::CursorBottom);
    h.app.Execute(Cmd::DeleteToRecycle);
    h.app.OnKey(ParseChord("Enter"));
    h.Settle();

    // The mark replaced the history rather than sitting on top of it.
    KITE_EXPECT_EQ(h.app.undoStack().size(), size_t{ 1 });

    h.app.Execute(Cmd::Undo);
    KITE_EXPECT_EQ(h.Status(), h.Text("ui.undo_no_delete"));
    // gamma is still there: undo did not step over the delete to reach it.
    KITE_EXPECT(h.files.Exists("C:\\home\\gamma"));

    // And it stays blocked - the mark is not consumed by being reported.
    h.app.Execute(Cmd::Undo);
    KITE_EXPECT_EQ(h.Status(), h.Text("ui.undo_no_delete"));
    KITE_EXPECT_EQ(h.app.undoStack().size(), size_t{ 1 });
}

KITE_TEST(undo, work_done_after_a_delete_is_still_undoable) {
    Harness h;
    h.app.Execute(Cmd::CursorBottom);
    h.app.Execute(Cmd::DeleteToRecycle);
    h.app.OnKey(ParseChord("Enter"));
    h.Settle();

    h.Commit(Cmd::NewFolder, "gamma");
    h.app.Execute(Cmd::Undo);
    h.Settle();
    KITE_EXPECT_FALSE(h.files.Exists("C:\\home\\gamma"));

    // Back down to the mark, which still answers for itself.
    h.app.Execute(Cmd::Undo);
    KITE_EXPECT_EQ(h.Status(), h.Text("ui.undo_no_delete"));
}

// ---------------------------------------------------------------------------
// Clipboard feedback - separate messages so a copy and a cut are told apart
// ---------------------------------------------------------------------------

KITE_TEST(undo, copy_and_cut_report_which_one_happened) {
    Harness h;
    h.app.Execute(Cmd::CursorBottom);

    h.app.Execute(Cmd::Copy);
    KITE_EXPECT_EQ(h.Status(), h.Text("ui.copied_files"));

    h.app.Execute(Cmd::Cut);
    KITE_EXPECT_EQ(h.Status(), h.Text("ui.cut_files"));

    // The two must not collapse into one message: the whole point is knowing
    // which of them the clipboard is now holding.
    KITE_EXPECT_NE(h.Text("ui.copied_files"), h.Text("ui.cut_files"));
}

KITE_TEST(undo, a_status_message_asks_for_the_repaint_that_shows_it) {
    Harness h;
    h.app.Execute(Cmd::CursorBottom);

    // Copy moves nothing on screen, so nothing else in the command will ask for
    // a frame. Without one the answer waits for an unrelated keystroke - and so
    // does the expiry timer, which the window only arms while painting.
    const int before = h.host.invalidateCount;
    h.app.Execute(Cmd::Copy);
    KITE_EXPECT(h.host.invalidateCount > before);
    KITE_EXPECT_EQ(h.Status(), h.Text("ui.copied_files"));
}

KITE_TEST(undo, copying_nothing_reports_the_empty_selection_not_success) {
    Harness h;
    h.app.Execute(Cmd::CursorTop);  // the ".." row has nothing to offer
    h.app.Execute(Cmd::Copy);
    KITE_EXPECT_EQ(h.Status(), h.Text("ui.no_selection"));
}
