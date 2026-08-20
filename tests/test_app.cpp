// End-to-end tests over the controller, driven the way the window drives it:
// real command dispatch, real key map, real async loader.
#include "Fakes.h"
#include "TestFramework.h"
#include "core/base/Version.h"
#include "core/fs/VirtualPath.h"

using namespace kite;

namespace {

struct Harness {
    test::FakeFileSystem files;
    test::FakeShell shell;
    test::FakeHost host;
    test::FakeWatcher watcher;
    App app;

    Harness() : app(files, shell, host, &watcher) {
        test::ResetFakePlatform();
        test::PopulateStandardTree(files);
        app.Init({});
        test::PumpUntilSettled(app);
    }

    Tab* tab() { return app.workspace().focusedTab(); }
    Pane* pane() { return app.workspace().focusedPane(); }
    Session* session() { return app.workspace().activeSession(); }

    void Settle() { test::PumpUntilSettled(app); }

    void Type(const std::string& text) {
        for (char c : text) app.OnChar(static_cast<uint32_t>(c));
    }

    // Ctrl+L opens with the whole path selected, so anything typed next would
    // replace it. Tests that mean to add to the path collapse that first, which
    // is what pressing End does for a person too.
    void EditPathAppend() {
        app.Execute(Cmd::EditPath);
        app.OnKey(ParseChord("End"));
    }

    std::string CursorName() {
        const fs::Entry* entry = tab()->CursorEntry();
        return entry ? entry->name : std::string();
    }
};

}  // namespace

KITE_TEST(app, starts_in_the_home_folder_with_one_session_and_one_pane) {
    Harness h;
    KITE_EXPECT_EQ(h.app.workspace().sessions.size(), size_t{ 1 });
    KITE_EXPECT_EQ(h.session()->Panes().size(), size_t{ 1 });
    KITE_EXPECT_EQ(h.tab()->path, std::string("C:\\home"));
    KITE_EXPECT(h.tab()->loaded);
    // .hidden is filtered out by default.
    KITE_EXPECT_EQ(h.tab()->ItemCount(), 5);
    // ".." leads the list, and the cursor starts below it.
    KITE_EXPECT(h.tab()->IsParentRow(0));
    KITE_EXPECT_EQ(h.tab()->cursor, 1);
}

KITE_TEST(app, the_caption_names_the_folder_and_the_build_before_anything_is_typed) {
    // The build stamp is the whole reason the caption is worth reading twice:
    // the exe has no VERSIONINFO resource, so this and the session bar are the
    // only places a running Kite says which commit it came from. Set from Init,
    // because a window that reports its version only after the first keystroke
    // is no use to whoever is being asked what they are running.
    Harness h;
    KITE_EXPECT(h.host.title.find("C:\\home") != std::string::npos);
    KITE_EXPECT(h.host.title.find(version::kNumber) != std::string::npos);
    KITE_EXPECT(h.host.title.find(version::kCommit) != std::string::npos);
}

KITE_TEST(app, writes_a_reference_keys_file_on_first_run) {
    Harness h;
    auto it = test::FakeFiles().find("C:\\home\\config\\keys.ini");
    KITE_EXPECT(it != test::FakeFiles().end());
    KITE_EXPECT(it->second.find("tab.new=Ctrl+T") != std::string::npos);
}

// A zip extracted into Program Files, or run from read-only media. Nothing Kite
// writes will ever land, and the first launch is the only moment it can say so
// while anyone is still looking - every later write happens on the way out.
KITE_TEST(app, says_so_when_the_config_folder_refuses_the_first_write) {
    test::ResetFakePlatform();
    test::FakeReadOnlyPrefix() = "C:\\home\\config";

    test::FakeFileSystem files;
    test::FakeShell shell;
    test::FakeHost host;
    test::FakeWatcher watcher;
    test::PopulateStandardTree(files);
    App app(files, shell, host, &watcher);
    app.Init({});
    test::PumpUntilSettled(app);

    KITE_EXPECT(app.statusMessage().find("keys.ini") != std::string::npos);
    KITE_EXPECT_FALSE(app.statusExpired());
    // The refused write left nothing behind.
    KITE_EXPECT(test::FakeFiles().find("C:\\home\\config\\keys.ini") == test::FakeFiles().end());

    test::ResetFakePlatform();
}

// The folder itself failing is a separate report: it comes before any file is
// attempted, and it is the one case where nothing at all can be kept.
KITE_TEST(app, says_so_when_the_config_folder_cannot_be_created) {
    test::ResetFakePlatform();
    test::FakeReadOnlyPrefix() = "C:\\home";

    test::FakeFileSystem files;
    test::FakeShell shell;
    test::FakeHost host;
    test::FakeWatcher watcher;
    test::PopulateStandardTree(files);
    App app(files, shell, host, &watcher);
    app.Init({});
    test::PumpUntilSettled(app);

    KITE_EXPECT(!app.statusMessage().empty());
    KITE_EXPECT(app.statusMessage().find("C:\\home\\config") != std::string::npos);

    test::ResetFakePlatform();
}

KITE_TEST(app, ctrl_s_does_not_claim_to_have_saved_what_it_could_not_write) {
    Harness h;
    h.app.Execute(Cmd::SaveWorkspace);
    KITE_EXPECT_EQ(h.app.statusMessage(), std::string("Workspace saved"));

    test::FakeReadOnlyPrefix() = "C:\\home\\config";
    h.app.Execute(Cmd::SaveWorkspace);
    KITE_EXPECT_NE(h.app.statusMessage(), std::string("Workspace saved"));
    // Which of the three files is named depends on the order they are written
    // in; what has to hold is that the message points at the folder that
    // refused them, since that is the part anyone can act on.
    KITE_EXPECT(h.app.statusMessage().find("C:\\home\\config") != std::string::npos);

    // The session file is attempted even though the settings write already
    // failed, so a folder that only refuses one of them still keeps the other.
    test::FakeReadOnlyPrefix() = "C:\\home\\config\\settings.ini";
    test::FakeFiles().erase("C:\\home\\config\\sessions.ini");
    h.app.Execute(Cmd::SaveWorkspace);
    KITE_EXPECT(test::FakeFiles().count("C:\\home\\config\\sessions.ini") == 1);

    test::ResetFakePlatform();
}

KITE_TEST(app, a_chord_dispatches_its_command) {
    Harness h;
    KITE_EXPECT(h.app.OnKey(ParseChord("Ctrl+T")));
    KITE_EXPECT_EQ(h.pane()->tabs.size(), size_t{ 2 });

    // An unbound chord is not consumed, so the platform can still use it.
    KITE_EXPECT_FALSE(h.app.OnKey(ParseChord("Ctrl+Alt+Shift+F9")));
}

KITE_TEST(app, opening_a_folder_navigates_and_records_history) {
    Harness h;
    // Cursor starts on "alpha" (folders first, natural order).
    KITE_EXPECT_EQ(h.CursorName(), std::string("alpha"));

    h.app.Execute(Cmd::OpenSelected);
    h.Settle();
    KITE_EXPECT_EQ(h.tab()->path, std::string("C:\\home\\alpha"));

    h.app.Execute(Cmd::GoBack);
    h.Settle();
    KITE_EXPECT_EQ(h.tab()->path, std::string("C:\\home"));

    h.app.Execute(Cmd::GoForward);
    h.Settle();
    KITE_EXPECT_EQ(h.tab()->path, std::string("C:\\home\\alpha"));
}

KITE_TEST(app, going_up_puts_the_cursor_on_the_folder_just_left) {
    Harness h;
    h.app.Execute(Cmd::OpenSelected);  // into alpha
    h.Settle();

    h.app.Execute(Cmd::GoUp);
    h.Settle();
    KITE_EXPECT_EQ(h.tab()->path, std::string("C:\\home"));
    KITE_EXPECT_EQ(h.CursorName(), std::string("alpha"));
}

KITE_TEST(app, opening_a_file_goes_to_the_shell_not_the_navigator) {
    Harness h;
    h.app.Execute(Cmd::CursorBottom);  // notes.txt
    KITE_EXPECT_EQ(h.CursorName(), std::string("notes.txt"));

    h.app.Execute(Cmd::OpenSelected);
    KITE_EXPECT_EQ(h.tab()->path, std::string("C:\\home"));
    KITE_EXPECT_EQ(h.shell.opened.size(), size_t{ 1 });
    KITE_EXPECT_EQ(h.shell.opened[0], std::string("C:\\home\\notes.txt"));
}

KITE_TEST(app, extending_selects_a_run_and_shrinking_it_releases_the_tail) {
    Harness h;
    h.app.Execute(Cmd::ExtendDown);
    h.app.Execute(Cmd::ExtendDown);
    KITE_EXPECT_EQ(h.tab()->MarkedCount(), 3);

    h.app.Execute(Cmd::ExtendUp);
    KITE_EXPECT_EQ(h.tab()->MarkedCount(), 2);
}

KITE_TEST(app, plain_cursor_movement_leaves_marks_alone) {
    // Without this, Space could only ever collect neighbours: stepping to the
    // next row would drop whatever was just marked.
    Harness h;
    h.app.Execute(Cmd::ToggleSelection);  // marks the first item, steps down
    h.app.Execute(Cmd::CursorDown);
    h.app.Execute(Cmd::CursorDown);
    h.app.Execute(Cmd::ToggleSelection);
    KITE_EXPECT_EQ(h.tab()->MarkedCount(), 2);

    const std::vector<std::string> paths = h.tab()->SelectionPaths();
    KITE_EXPECT_EQ(paths.size(), size_t{ 2 });
    KITE_EXPECT_EQ(paths[0], std::string("C:\\home\\alpha"));
    KITE_EXPECT_EQ(paths[1], std::string("C:\\home\\image10.png"));

    h.app.Execute(Cmd::SelectNone);
    KITE_EXPECT_EQ(h.tab()->MarkedCount(), 0);
}

KITE_TEST(app, toggle_selection_accumulates_and_advances) {
    Harness h;
    h.app.Execute(Cmd::ToggleSelection);
    h.app.Execute(Cmd::ToggleSelection);
    KITE_EXPECT_EQ(h.tab()->MarkedCount(), 2);
    KITE_EXPECT_EQ(h.tab()->cursor, 3);

    // Toggling the same row again removes it.
    h.app.Execute(Cmd::CursorTop);
    h.app.Execute(Cmd::CursorDown);
    h.app.Execute(Cmd::ToggleSelection);
    KITE_EXPECT_EQ(h.tab()->MarkedCount(), 1);
}

KITE_TEST(app, the_parent_row_is_a_move_not_a_selection) {
    Harness h;
    h.app.Execute(Cmd::CursorTop);  // onto ".."
    KITE_EXPECT(h.tab()->IsParentRow(h.tab()->cursor));

    // Space steps past it without marking anything.
    h.app.Execute(Cmd::ToggleSelection);
    KITE_EXPECT_EQ(h.tab()->MarkedCount(), 0);
    KITE_EXPECT_EQ(h.tab()->cursor, 1);

    // Select-all and invert leave it alone too.
    h.app.Execute(Cmd::SelectAll);
    KITE_EXPECT_EQ(h.tab()->MarkedCount(), 5);
    h.app.Execute(Cmd::InvertSelection);
    KITE_EXPECT_EQ(h.tab()->MarkedCount(), 0);
}

KITE_TEST(app, opening_the_parent_row_goes_up_and_lands_on_the_folder_left) {
    Harness h;
    h.app.Execute(Cmd::OpenSelected);  // into alpha
    h.Settle();
    KITE_EXPECT_EQ(h.tab()->path, std::string("C:\\home\\alpha"));

    h.app.Execute(Cmd::CursorTop);  // onto ".."
    h.app.Execute(Cmd::OpenSelected);
    h.Settle();
    KITE_EXPECT_EQ(h.tab()->path, std::string("C:\\home"));
    KITE_EXPECT_EQ(h.CursorName(), std::string("alpha"));
}

KITE_TEST(app, a_drive_root_leads_up_to_the_computer) {
    Harness h;
    h.files.AddDir("C:\\");
    h.app.OpenPath("C:\\", false);
    h.Settle();
    KITE_EXPECT_EQ(h.tab()->path, std::string("C:\\"));
    KITE_EXPECT(h.tab()->hasParentRow());

    h.app.Execute(Cmd::GoUp);
    h.Settle();
    KITE_EXPECT_EQ(h.tab()->path, std::string(vfs::kComputer));

    // And that is the top: nothing above "PC" to walk into.
    h.app.Execute(Cmd::GoUp);
    h.Settle();
    KITE_EXPECT_EQ(h.tab()->path, std::string(vfs::kComputer));
}

KITE_TEST(app, the_recycle_bin_names_itself_when_asking_for_a_menu) {
    Harness h;
    // A deleted item addresses the hidden $R copy of itself, so parsing that
    // path would hand the shell an ordinary file - with no "Restore" on it. The
    // folder travels with the request so the shell can find the item inside it.
    h.files.AddFile(vfs::kRecycleBin, "notes.txt", 12, 0);
    h.app.OpenPath(vfs::kRecycleBin, false);
    h.Settle();
    KITE_EXPECT_EQ(h.tab()->ItemCount(), 1);

    h.app.Execute(Cmd::CursorBottom);
    h.app.ShowContextMenuAt(10, 10, false);
    KITE_EXPECT_EQ(h.shell.contextMenuCalls, 1);
    KITE_EXPECT_EQ(h.shell.lastContextMenuFolder, std::string(vfs::kRecycleBin));

    // A real folder must not name one: the shell would enumerate it again for
    // every right-click, and parsing the paths is both correct and free there.
    h.files.AddFile("C:\\home", "beta.txt", 1, 0);
    h.app.OpenPath("C:\\home", false);
    h.Settle();
    h.app.Execute(Cmd::CursorBottom);
    h.app.ShowContextMenuAt(10, 10, false);
    KITE_EXPECT_EQ(h.shell.contextMenuCalls, 2);
    KITE_EXPECT_EQ(h.shell.lastContextMenuFolder, std::string(""));
}

KITE_TEST(app, restore_only_answers_inside_the_recycle_bin) {
    Harness h;
    h.files.AddFile(vfs::kRecycleBin, "notes.txt", 12, 0);
    h.app.OpenPath(vfs::kRecycleBin, false);
    h.Settle();
    h.app.Execute(Cmd::CursorBottom);

    h.app.Execute(Cmd::Restore);
    KITE_EXPECT_EQ(h.shell.restoreCalls.size(), size_t{ 1 });
    KITE_EXPECT_EQ(h.shell.restoreCalls[0].size(), size_t{ 1 });

    // Elsewhere it does nothing at all - the shell verb only exists for items
    // that are in the bin, and a silent no-op would read as a broken key.
    h.app.OpenPath("C:\\home", false);
    h.Settle();
    h.app.Execute(Cmd::CursorBottom);
    h.app.Execute(Cmd::Restore);
    KITE_EXPECT_EQ(h.shell.restoreCalls.size(), size_t{ 1 });

    // Restoring is not undoable: Ctrl+Z on it would put the rescued file back.
    h.app.OpenPath(vfs::kRecycleBin, false);
    h.Settle();
    h.app.Execute(Cmd::CursorBottom);
    h.app.Execute(Cmd::Restore);
    KITE_EXPECT_EQ(h.shell.restoreCalls.size(), size_t{ 2 });
    h.app.Execute(Cmd::Undo);
    KITE_EXPECT(h.files.deleteCalls.empty());
}

KITE_TEST(app, a_virtual_folder_refuses_to_be_written_into) {
    Harness h;
    h.app.OpenPath(vfs::kComputer, false);
    h.Settle();

    h.app.Execute(Cmd::NewFolder);
    KITE_EXPECT_EQ(h.app.prompt().kind, PromptKind::None);
    h.app.Execute(Cmd::NewFile);
    KITE_EXPECT_EQ(h.app.prompt().kind, PromptKind::None);
    h.app.Execute(Cmd::Paste);
    KITE_EXPECT(h.files.copyCalls.empty());

    // A list is not a place a drop can land in either.
    KITE_EXPECT_FALSE(App::IsValidDropTarget({ "C:\\home\\a.txt" }, vfs::kComputer));
}

KITE_TEST(app, select_all_and_invert) {
    Harness h;
    h.app.Execute(Cmd::SelectAll);
    KITE_EXPECT_EQ(h.tab()->MarkedCount(), 5);

    h.app.Execute(Cmd::InvertSelection);
    KITE_EXPECT_EQ(h.tab()->MarkedCount(), 0);

    h.app.Execute(Cmd::InvertSelection);
    KITE_EXPECT_EQ(h.tab()->MarkedCount(), 5);

    h.app.Execute(Cmd::SelectNone);
    KITE_EXPECT_EQ(h.tab()->MarkedCount(), 0);
}

KITE_TEST(app, sorting_the_active_column_again_flips_the_direction) {
    Harness h;
    KITE_EXPECT_EQ(h.tab()->view.sort, SortKey::Name);
    KITE_EXPECT_FALSE(h.tab()->view.sortDesc);

    h.app.Execute(Cmd::SortByName);
    KITE_EXPECT(h.tab()->view.sortDesc);

    h.app.Execute(Cmd::SortBySize);
    KITE_EXPECT_EQ(h.tab()->view.sort, SortKey::Size);
    KITE_EXPECT_FALSE(h.tab()->view.sortDesc);
}

KITE_TEST(app, toggling_hidden_files_changes_the_listing) {
    Harness h;
    KITE_EXPECT_EQ(h.tab()->ItemCount(), 5);
    h.app.Execute(Cmd::ToggleHidden);
    KITE_EXPECT_EQ(h.tab()->ItemCount(), 6);
}

KITE_TEST(app, the_filter_prompt_narrows_the_list_while_typing) {
    Harness h;
    h.app.Execute(Cmd::FocusFilter);
    KITE_EXPECT_EQ(h.app.prompt().kind, PromptKind::Filter);

    for (char c : std::string("image")) h.app.OnChar(static_cast<uint32_t>(c));
    KITE_EXPECT_EQ(h.tab()->ItemCount(), 2);

    // Cursor keys stay live so the list is drivable while filtering.
    KITE_EXPECT_EQ(h.tab()->cursor, 1);
    h.app.OnKey(ParseChord("Down"));
    KITE_EXPECT_EQ(h.tab()->cursor, 2);
    // Printable keys still go into the filter, not the command dispatcher.
    KITE_EXPECT(h.app.OnChar('s'));
    KITE_EXPECT_EQ(h.app.prompt().text, std::string("images"));

    h.app.OnKey(ParseChord("Escape"));
    KITE_EXPECT_EQ(h.app.prompt().kind, PromptKind::None);
    KITE_EXPECT_EQ(h.tab()->ItemCount(), 5);
}

KITE_TEST(app, the_path_prompt_navigates_on_enter) {
    Harness h;
    h.EditPathAppend();
    KITE_EXPECT_EQ(h.app.prompt().kind, PromptKind::Path);
    KITE_EXPECT_EQ(h.app.prompt().text, std::string("C:\\home"));

    for (char c : std::string("\\beta")) h.app.OnChar(static_cast<uint32_t>(c));
    h.app.OnKey(ParseChord("Enter"));
    h.Settle();
    KITE_EXPECT_EQ(h.tab()->path, std::string("C:\\home\\beta"));
}

KITE_TEST(app, the_path_prompt_offers_folders_once_a_name_is_started) {
    Harness h;
    h.EditPathAppend();
    // Nothing is offered - or even enumerated - before the first keystroke.
    KITE_EXPECT_FALSE(h.app.pathComplete().open());

    h.Type("\\a");
    h.Settle();
    KITE_EXPECT(h.app.pathComplete().open());
    KITE_EXPECT_EQ(h.app.pathComplete().matches().size(), size_t{ 1 });
    KITE_EXPECT_EQ(h.app.pathComplete().matches()[0], std::string("alpha"));

    // Tab puts the candidate in the field, and Enter goes there.
    KITE_EXPECT(h.app.OnKey(ParseChord("Tab")));
    KITE_EXPECT_EQ(h.app.prompt().text, std::string("C:\\home\\alpha"));
    KITE_EXPECT_EQ(h.app.prompt().caret, h.app.prompt().text.size());
    h.app.OnKey(ParseChord("Enter"));
    h.Settle();
    KITE_EXPECT_EQ(h.tab()->path, std::string("C:\\home\\alpha"));
    KITE_EXPECT_FALSE(h.app.pathComplete().open());
}

KITE_TEST(app, one_folder_is_enumerated_once_however_much_is_typed_into_it) {
    Harness h;
    h.EditPathAppend();
    h.Type("\\a");
    h.Settle();
    const int afterFirst = h.files.listCalls;

    // Three more keystrokes inside the same folder. Re-listing per letter would
    // put a network share's latency on each one.
    h.Type("lph");
    h.Settle();
    KITE_EXPECT_EQ(h.files.listCalls, afterFirst);
    KITE_EXPECT_EQ(h.app.pathComplete().matches().size(), size_t{ 1 });

    // Descending does need the new folder.
    h.Type("a\\");
    h.Settle();
    KITE_EXPECT_EQ(h.files.listCalls, afterFirst + 1);
    KITE_EXPECT_EQ(h.app.pathComplete().matches()[0], std::string("nested"));
}

KITE_TEST(app, escape_folds_the_candidates_before_it_closes_the_prompt) {
    Harness h;
    h.EditPathAppend();
    h.Type("\\a");
    h.Settle();
    KITE_EXPECT(h.app.pathComplete().open());

    // The first Escape dismisses the offer without losing the typed text.
    KITE_EXPECT(h.app.OnKey(ParseChord("Escape")));
    KITE_EXPECT_FALSE(h.app.pathComplete().open());
    KITE_EXPECT_EQ(h.app.prompt().kind, PromptKind::Path);
    KITE_EXPECT_EQ(h.app.prompt().text, std::string("C:\\home\\a"));

    KITE_EXPECT(h.app.OnKey(ParseChord("Escape")));
    KITE_EXPECT_EQ(h.app.prompt().kind, PromptKind::None);
}

KITE_TEST(app, the_offer_goes_away_when_the_caret_leaves_the_end_of_the_text) {
    Harness h;
    h.EditPathAppend();
    h.Type("\\a");
    h.Settle();
    KITE_EXPECT(h.app.pathComplete().open());

    // Completion finishes the tail of the text, so with the caret parked in the
    // middle there is nothing it could honestly be offering.
    h.app.OnKey(ParseChord("Left"));
    KITE_EXPECT_FALSE(h.app.pathComplete().open());

    // And Tab must not quietly take the candidate it had a moment ago: that
    // would overwrite whatever the caret is sitting in front of.
    h.app.OnKey(ParseChord("Tab"));
    KITE_EXPECT_EQ(h.app.prompt().text, std::string("C:\\home\\a"));

    // Back at the end, the folder is still listed, so it completes at once.
    h.app.OnKey(ParseChord("End"));
    KITE_EXPECT(h.app.OnKey(ParseChord("Tab")));
    KITE_EXPECT_EQ(h.app.prompt().text, std::string("C:\\home\\alpha"));
}

KITE_TEST(app, tab_alone_opens_the_offer_on_a_folder_that_was_never_typed_into) {
    Harness h;
    // The drive itself, so that "C:\\home" has something to be completed from.
    h.files.dirs["C:\\"];
    h.files.AddDir("C:\\home");

    h.app.Execute(Cmd::EditPath);
    // The address bar opens on C:\home, so the first Tab has to both ask for
    // the listing and be the keystroke that opens the offer.
    h.app.OnKey(ParseChord("Tab"));
    h.Settle();
    KITE_EXPECT(h.app.pathComplete().open());
    KITE_EXPECT_EQ(h.app.pathComplete().dir(), std::string("C:\\"));
    KITE_EXPECT_EQ(h.app.pathComplete().matches().size(), size_t{ 1 });

    KITE_EXPECT(h.app.OnKey(ParseChord("Tab")));
    KITE_EXPECT_EQ(h.app.pathComplete().selected(), 0);
    KITE_EXPECT_EQ(h.app.prompt().text, std::string("C:\\home"));
}

KITE_TEST(app, completion_stays_out_of_the_other_prompts) {
    Harness h;
    h.app.Execute(Cmd::NewFolder);
    h.Type("C:\\ho");
    h.Settle();
    KITE_EXPECT_FALSE(h.app.pathComplete().open());
    // Tab is swallowed as before, so it cannot fire the pane shortcut.
    KITE_EXPECT(h.app.OnKey(ParseChord("Tab")));
    KITE_EXPECT_EQ(h.app.prompt().text, std::string("C:\\ho"));
}

KITE_TEST(app, ctrl_l_opens_with_the_whole_path_selected) {
    Harness h;
    // What every address bar on the desktop does: the path is there to be read,
    // and selected so that typing a different one does not mean clearing it
    // first. It is also what makes the row visibly stop being breadcrumbs.
    h.app.Execute(Cmd::EditPath);
    KITE_EXPECT(h.app.prompt().hasSelection());
    KITE_EXPECT_EQ(h.app.prompt().selBegin(), size_t{ 0 });
    KITE_EXPECT_EQ(h.app.prompt().selEnd(), h.app.prompt().text.size());

    h.Type("D:");
    KITE_EXPECT_EQ(h.app.prompt().text, std::string("D:"));
}

KITE_TEST(app, ctrl_a_selects_the_whole_field_and_the_next_key_replaces_it) {
    Harness h;
    h.EditPathAppend();
    KITE_EXPECT_FALSE(h.app.prompt().hasSelection());

    KITE_EXPECT(h.app.OnKey(ParseChord("Ctrl+A")));
    KITE_EXPECT(h.app.prompt().hasSelection());
    KITE_EXPECT_EQ(h.app.prompt().selBegin(), size_t{ 0 });
    KITE_EXPECT_EQ(h.app.prompt().selEnd(), h.app.prompt().text.size());

    // Typing over a selection replaces it - otherwise selecting all would be a
    // way to move the caret and nothing else.
    h.Type("D");
    KITE_EXPECT_EQ(h.app.prompt().text, std::string("D"));
    KITE_EXPECT_FALSE(h.app.prompt().hasSelection());
    KITE_EXPECT_EQ(h.app.prompt().caret, size_t{ 1 });

    // And Backspace clears it in one go.
    h.app.Execute(Cmd::EditPath);
    h.app.OnKey(ParseChord("Backspace"));
    KITE_EXPECT_EQ(h.app.prompt().text, std::string(""));
}

KITE_TEST(app, an_arrow_key_collapses_the_selection_instead_of_eating_a_character) {
    Harness h;
    h.app.Execute(Cmd::EditPath);
    const std::string full = h.app.prompt().text;

    h.app.OnKey(ParseChord("Left"));
    KITE_EXPECT_EQ(h.app.prompt().text, full);
    KITE_EXPECT_FALSE(h.app.prompt().hasSelection());
    KITE_EXPECT_EQ(h.app.prompt().caret, size_t{ 0 });

    h.app.OnKey(ParseChord("Ctrl+A"));
    h.app.OnKey(ParseChord("Right"));
    KITE_EXPECT_EQ(h.app.prompt().caret, full.size());
}

KITE_TEST(app, select_all_stays_out_of_the_confirmation_prompt) {
    Harness h;
    h.app.Execute(Cmd::CursorDown);
    h.app.Execute(Cmd::DeleteToRecycle);
    KITE_EXPECT(h.app.prompt().isConfirm());
    const std::string shown = h.app.prompt().text;

    // The text there is a count, not something to edit.
    KITE_EXPECT(h.app.OnKey(ParseChord("Ctrl+A")));
    KITE_EXPECT_FALSE(h.app.prompt().hasSelection());
    KITE_EXPECT_EQ(h.app.prompt().text, shown);
}

KITE_TEST(app, shift_and_arrows_grow_one_selection) {
    Harness h;
    h.EditPathAppend();                    // "C:\\home", caret at the end
    const std::string full = h.app.prompt().text;

    h.app.OnKey(ParseChord("Shift+Left"));
    h.app.OnKey(ParseChord("Shift+Left"));
    KITE_EXPECT(h.app.prompt().hasSelection());
    // The anchor stays where editing began, so the run grows one selection
    // rather than a new one per keystroke.
    KITE_EXPECT_EQ(h.app.prompt().anchor, full.size());
    KITE_EXPECT_EQ(h.app.prompt().caret, full.size() - 2);
    KITE_EXPECT_EQ(h.app.prompt().selEnd() - h.app.prompt().selBegin(), size_t{ 2 });

    // Coming back shrinks the same selection, and lands empty where it started.
    h.app.OnKey(ParseChord("Shift+Right"));
    KITE_EXPECT_EQ(h.app.prompt().selEnd() - h.app.prompt().selBegin(), size_t{ 1 });
    h.app.OnKey(ParseChord("Shift+Right"));
    KITE_EXPECT_FALSE(h.app.prompt().hasSelection());

    // Shift+Home takes everything up to the caret; the text is untouched.
    h.app.OnKey(ParseChord("Shift+Home"));
    KITE_EXPECT_EQ(h.app.prompt().selBegin(), size_t{ 0 });
    KITE_EXPECT_EQ(h.app.prompt().selEnd(), full.size());
    KITE_EXPECT_EQ(h.app.prompt().text, full);
}

KITE_TEST(app, ctrl_and_arrows_move_a_whole_path_component) {
    Harness h;
    h.EditPathAppend();
    h.Type("\\alpha\\nested");
    const std::string full = h.app.prompt().text;  // C:\home\alpha\nested

    h.app.OnKey(ParseChord("Ctrl+Left"));
    KITE_EXPECT_EQ(h.app.prompt().caret, full.size() - 6);   // start of "nested"
    h.app.OnKey(ParseChord("Ctrl+Left"));
    KITE_EXPECT_EQ(h.app.prompt().caret, size_t{ 8 });       // start of "alpha"
    KITE_EXPECT_FALSE(h.app.prompt().hasSelection());

    h.app.OnKey(ParseChord("Ctrl+Right"));
    KITE_EXPECT_EQ(h.app.prompt().caret, size_t{ 13 });      // end of "alpha"

    // Ctrl+Shift takes the component instead of just stepping over it, which is
    // how one folder in the middle of a path gets replaced.
    h.app.OnKey(ParseChord("Ctrl+Shift+Left"));
    KITE_EXPECT_EQ(h.app.prompt().selBegin(), size_t{ 8 });
    KITE_EXPECT_EQ(h.app.prompt().selEnd(), size_t{ 13 });
    h.Type("beta");
    KITE_EXPECT_EQ(h.app.prompt().text, std::string("C:\\home\\beta\\nested"));
}

// --- the field's own clipboard -------------------------------------------
//
// Ctrl+C and Ctrl+V are Cmd::Copy and Cmd::Paste to the key map, and those act
// on the listing. While a field holds the keyboard they have to mean the field
// instead, or the address bar is the one text box in Kite you cannot paste a
// path into.

KITE_TEST(app, ctrl_c_in_the_address_bar_copies_the_field_not_the_listing) {
    Harness h;
    h.app.Execute(Cmd::EditPath);  // opens with everything selected
    KITE_EXPECT(h.app.OnKey(ParseChord("Ctrl+C")));
    KITE_EXPECT_EQ(h.shell.clipboardText.size(), size_t{ 1 });
    KITE_EXPECT_EQ(h.shell.clipboardText.back(), std::string("C:\\home"));
    // The listing was left alone: no files went to the clipboard.
    KITE_EXPECT(h.shell.clipboardFiles.empty());

    // With the selection collapsed there is still one obvious thing to copy.
    h.app.OnKey(ParseChord("End"));
    h.app.OnKey(ParseChord("Ctrl+C"));
    KITE_EXPECT_EQ(h.shell.clipboardText.back(), std::string("C:\\home"));

    // Cut is the half that can lose text, so it waits for a range to take.
    KITE_EXPECT(h.app.OnKey(ParseChord("Ctrl+X")));
    KITE_EXPECT_EQ(h.app.prompt().text, std::string("C:\\home"));
    h.app.OnKey(ParseChord("Ctrl+Shift+Left"));
    h.app.OnKey(ParseChord("Ctrl+X"));
    KITE_EXPECT_EQ(h.shell.clipboardText.back(), std::string("home"));
    KITE_EXPECT_EQ(h.app.prompt().text, std::string("C:\\"));
}

KITE_TEST(app, pasting_into_the_address_bar_takes_one_line_and_drops_the_quotes) {
    Harness h;
    // Exactly what Explorer's "Copy as path" puts on the clipboard, plus a
    // second line to prove only the first survives.
    h.shell.SetIncomingText("\"\\\\192.168.1.5\\pub\"\r\nC:\\other");

    h.app.Execute(Cmd::EditPath);
    KITE_EXPECT(h.app.OnKey(ParseChord("Ctrl+V")));
    // The whole field was selected, so the paste replaced it.
    KITE_EXPECT_EQ(h.app.prompt().text, std::string("\\\\192.168.1.5\\pub"));
    KITE_EXPECT_EQ(h.app.prompt().caret, h.app.prompt().text.size());

    // Shift+Insert is the same key in its older spelling.
    h.app.OnKey(ParseChord("Ctrl+A"));
    KITE_EXPECT(h.app.OnKey(ParseChord("Shift+Insert")));
    KITE_EXPECT_EQ(h.app.prompt().text, std::string("\\\\192.168.1.5\\pub"));
}

KITE_TEST(app, pasting_falls_back_to_a_copied_file_when_there_is_no_text) {
    Harness h;
    h.shell.clipboardFiles = { "C:\\home\\alpha" };

    h.app.Execute(Cmd::EditPath);
    h.app.OnKey(ParseChord("Ctrl+V"));
    KITE_EXPECT_EQ(h.app.prompt().text, std::string("C:\\home\\alpha"));
}

KITE_TEST(app, the_clipboard_keys_stay_out_of_the_confirmation_prompt) {
    Harness h;
    h.shell.SetIncomingText("anything");
    h.app.Execute(Cmd::CursorDown);
    h.app.Execute(Cmd::DeleteToRecycle);
    const std::string shown = h.app.prompt().text;

    KITE_EXPECT(h.app.OnKey(ParseChord("Ctrl+V")));
    KITE_EXPECT(h.app.OnKey(ParseChord("Ctrl+X")));
    KITE_EXPECT(h.app.OnKey(ParseChord("Ctrl+C")));
    KITE_EXPECT_EQ(h.app.prompt().text, shown);
    KITE_EXPECT(h.shell.clipboardText.empty());
}

// --- network locations -----------------------------------------------------

KITE_TEST(app, a_share_walks_up_to_the_server_that_lists_it) {
    Harness h;
    h.files.dirs["\\\\srv"];
    h.files.AddFile("\\\\srv", "pub", 0, 0, fs::Attr::Directory);
    h.files.dirs["\\\\srv\\pub"];
    h.files.AddFile("\\\\srv\\pub", "readme.txt", 10, 0);

    h.app.Execute(Cmd::EditPath);
    h.Type("\\\\srv\\pub");
    h.app.OnKey(ParseChord("Enter"));
    h.Settle();
    KITE_EXPECT_EQ(h.tab()->path, std::string("\\\\srv\\pub"));
    KITE_EXPECT(h.tab()->listing.status == fs::Status::Ok);

    // The share used to be the top of the tree. The server above it is a real
    // place - it holds the list of shares - so ".." has somewhere to go.
    KITE_EXPECT(h.tab()->IsParentRow(0));
    h.app.Execute(Cmd::GoUp);
    h.Settle();
    KITE_EXPECT_EQ(h.tab()->path, std::string("\\\\srv"));
    KITE_EXPECT_EQ(h.tab()->ItemCount(), 1);  // "pub"
    // Above the server is "Network", by the same reading that put the server
    // above the share.
    KITE_EXPECT(h.tab()->IsParentRow(0));
    h.app.Execute(Cmd::GoUp);
    h.Settle();
    KITE_EXPECT_EQ(h.tab()->path, std::string(vfs::kNetwork));
}

KITE_TEST(app, signing_in_aims_at_the_share_not_the_folder_inside_it) {
    Harness h;
    h.files.dirs["\\\\srv\\pub\\sub"];
    h.app.Execute(Cmd::EditPath);
    h.Type("\\\\srv\\pub\\sub");
    h.app.OnKey(ParseChord("Enter"));
    h.Settle();

    h.app.Execute(Cmd::ConnectNetwork);
    KITE_EXPECT_EQ(h.shell.connectCalls.size(), size_t{ 1 });
    KITE_EXPECT_EQ(h.shell.connectCalls.back(), std::string("\\\\srv\\pub"));
}

KITE_TEST(app, a_terminal_opens_in_the_folder_on_screen) {
    Harness h;
    h.app.Execute(Cmd::CursorTop);
    h.app.Execute(Cmd::CursorDown);  // alpha
    h.app.Execute(Cmd::OpenSelected);
    h.Settle();

    // The cursor row is irrelevant: the command is "open a terminal here", and
    // here is the folder being listed.
    h.app.Execute(Cmd::OpenTerminal);
    KITE_EXPECT_EQ(h.shell.terminalDirs.size(), size_t{ 1 });
    KITE_EXPECT_EQ(h.shell.terminalDirs.back(), std::string("C:\\home\\alpha"));
}

// Nothing on screen moves when a terminal fails to open, so silence is
// indistinguishable from a key that does not work.
KITE_TEST(app, a_terminal_that_will_not_open_says_so) {
    Harness h;
    h.shell.terminalSucceeds = false;
    h.app.Execute(Cmd::OpenTerminal);
    KITE_EXPECT_EQ(h.app.statusMessage(), h.app.strings().Get("ui.terminal_failed"));
}

KITE_TEST(app, signing_in_has_nothing_to_offer_a_local_folder) {
    Harness h;
    h.app.Execute(Cmd::ConnectNetwork);
    KITE_EXPECT(h.shell.connectCalls.empty());
    KITE_EXPECT_EQ(h.app.statusMessage(), h.app.strings().Get("ui.not_network_path"));
}

// "Access denied" on a share is usually a question that has not been asked yet.
KITE_TEST(app, a_denied_share_names_the_key_that_signs_in) {
    Harness h;
    h.files.denied.push_back("\\\\srv\\pub");
    h.app.Execute(Cmd::EditPath);
    h.Type("\\\\srv\\pub");
    h.app.OnKey(ParseChord("Enter"));
    h.Settle();

    KITE_EXPECT(h.tab()->listing.status == fs::Status::AccessDenied);
    KITE_EXPECT(h.app.statusMessage().find("Ctrl+Shift+L") != std::string::npos);

    // A local folder that refuses gets no such hint - there is no one to sign
    // in to, and the suggestion would be noise on top of a real failure.
    test::FakeClockMs() += 60000;  // let the first message age out
    KITE_EXPECT(h.app.statusExpired());
    h.files.denied.push_back("C:\\home\\alpha");
    h.app.Execute(Cmd::EditPath);
    h.Type("C:\\home\\alpha");
    h.app.OnKey(ParseChord("Enter"));
    h.Settle();
    KITE_EXPECT(h.tab()->listing.status == fs::Status::AccessDenied);
    KITE_EXPECT(h.app.statusExpired());
}

KITE_TEST(app, moving_off_the_end_folds_the_candidate_list) {
    Harness h;
    h.EditPathAppend();
    h.Type("\\a");
    h.Settle();
    KITE_EXPECT(h.app.pathComplete().open());

    // Ctrl+Left leaves the end of the text, so the offer no longer applies.
    h.app.OnKey(ParseChord("Ctrl+Left"));
    KITE_EXPECT_FALSE(h.app.pathComplete().open());
}

KITE_TEST(app, an_in_place_field_can_be_folded_away_without_applying_it) {
    Harness h;
    const std::string was = h.tab()->path;
    h.EditPathAppend();
    h.Type("\\beta");

    // What the UI calls when a press lands outside the field. The typed path is
    // dropped, not navigated to: the click was an answer to something else.
    h.app.CancelInlineEdit();
    KITE_EXPECT_EQ(h.app.prompt().kind, PromptKind::None);
    KITE_EXPECT_FALSE(h.app.pathComplete().open());
    KITE_EXPECT_EQ(h.tab()->path, was);

    // Every field that draws itself on the thing it edits takes the same exit,
    // and nothing is created on the way out - Enter is the only word for yes.
    h.app.Execute(Cmd::NewFolder);
    h.Type("gamma");
    h.app.CancelInlineEdit();
    KITE_EXPECT_EQ(h.app.prompt().kind, PromptKind::None);
    KITE_EXPECT_EQ(h.files.dirs.count("C:\\home\\gamma"), size_t{ 0 });

    // The two that have nowhere of their own to sit stay put: the filter belongs
    // to the whole listing, and the delete confirmation is a question, not a name.
    h.app.Execute(Cmd::FocusFilter);
    h.app.CancelInlineEdit();
    KITE_EXPECT_EQ(h.app.prompt().kind, PromptKind::Filter);

    h.app.OnKey(ParseChord("Escape"));
    h.app.Execute(Cmd::CursorBottom);
    h.app.Execute(Cmd::DeleteToRecycle);
    h.app.CancelInlineEdit();
    KITE_EXPECT_EQ(h.app.prompt().kind, PromptKind::ConfirmDelete);
}

KITE_TEST(app, prompt_editing_handles_multibyte_text_one_character_at_a_time) {
    Harness h;
    h.app.Execute(Cmd::EditPath);
    h.app.prompt().text.clear();
    h.app.prompt().caret = 0;

    h.app.OnChar(0x3042);  // HIRAGANA A, three UTF-8 bytes
    KITE_EXPECT_EQ(h.app.prompt().text.size(), size_t{ 3 });

    h.app.OnKey(ParseChord("Backspace"));
    KITE_EXPECT_EQ(h.app.prompt().text.size(), size_t{ 0 });
}

KITE_TEST(app, a_prompt_swallows_shortcuts_so_typing_cannot_fire_commands) {
    Harness h;
    const size_t before = h.pane()->tabs.size();
    h.app.Execute(Cmd::NewFolder);
    KITE_EXPECT(h.app.OnKey(ParseChord("Ctrl+T")));
    KITE_EXPECT_EQ(h.pane()->tabs.size(), before);
}

KITE_TEST(app, new_folder_prompt_creates_the_folder) {
    Harness h;
    h.app.Execute(Cmd::NewFolder);
    for (char c : std::string("gamma")) h.app.OnChar(static_cast<uint32_t>(c));
    h.app.OnKey(ParseChord("Enter"));
    h.Settle();

    KITE_EXPECT(h.files.dirs.count("C:\\home\\gamma") == 1);
    KITE_EXPECT_EQ(h.tab()->ItemCount(), 6);
}

KITE_TEST(app, rename_preselects_the_stem_of_a_file) {
    Harness h;
    h.app.Execute(Cmd::CursorBottom);  // notes.txt
    h.app.Execute(Cmd::Rename);
    KITE_EXPECT_EQ(h.app.prompt().kind, PromptKind::Rename);
    KITE_EXPECT_EQ(h.app.prompt().text, std::string("notes.txt"));
    // "notes" is selected and ".txt" is not, so the first key replaces the name
    // and keeps the extension. The caret is on the far side of the selection, the
    // way it is after any other select-then-type.
    KITE_EXPECT_EQ(h.app.prompt().anchor, size_t{ 0 });
    KITE_EXPECT_EQ(h.app.prompt().caret, size_t{ 5 });

    h.Type("todo");
    KITE_EXPECT_EQ(h.app.prompt().text, std::string("todo.txt"));
    h.app.OnKey(ParseChord("Enter"));
    h.Settle();
    KITE_EXPECT(h.files.Exists("C:\\home\\todo.txt"));
}

KITE_TEST(app, rename_selects_the_whole_name_when_there_is_no_extension_to_keep) {
    Harness h;
    // A folder: "alpha" has no extension, and a dotted one would not have an
    // extension either - nobody reads "backup.2026" as a type.
    h.app.Execute(Cmd::Rename);
    KITE_EXPECT_EQ(h.app.prompt().text, std::string("alpha"));
    KITE_EXPECT_EQ(h.app.prompt().anchor, size_t{ 0 });
    KITE_EXPECT_EQ(h.app.prompt().caret, size_t{ 5 });

    // A dotfile is all stem: there is no name in front of the dot to replace.
    h.app.OnKey(ParseChord("Escape"));
    h.app.Execute(Cmd::ToggleHidden);
    h.Settle();
    h.app.Execute(Cmd::CursorTop);
    while (h.CursorName() != ".hidden" && h.tab()->cursor < h.tab()->ItemCount()) {
        h.app.Execute(Cmd::CursorDown);
    }
    KITE_EXPECT_EQ(h.CursorName(), std::string(".hidden"));
    h.app.Execute(Cmd::Rename);
    KITE_EXPECT_EQ(h.app.prompt().anchor, size_t{ 0 });
    KITE_EXPECT_EQ(h.app.prompt().caret, h.app.prompt().text.size());
}

KITE_TEST(app, renaming_a_session_starts_from_its_current_name) {
    Harness h;
    h.app.Execute(Cmd::NewSession);
    h.Settle();
    const std::string was = h.session()->name;

    h.app.Execute(Cmd::RenameSession);
    KITE_EXPECT_EQ(h.app.prompt().kind, PromptKind::SessionName);
    KITE_EXPECT_EQ(h.app.prompt().text, was);

    h.app.OnKey(ParseChord("Ctrl+A"));
    h.Type("work");
    h.app.OnKey(ParseChord("Enter"));
    KITE_EXPECT_EQ(h.session()->name, std::string("work"));
    // The other session keeps its own name: renaming answers about the active one.
    KITE_EXPECT_NE(h.app.workspace().sessions.front()->name, std::string("work"));

    // An emptied field leaves the name alone rather than blanking the chip - a
    // nameless session cannot be told from its neighbours in the bar.
    h.app.Execute(Cmd::RenameSession);
    h.app.OnKey(ParseChord("Ctrl+A"));
    h.app.OnKey(ParseChord("Delete"));
    h.app.OnKey(ParseChord("Enter"));
    KITE_EXPECT_EQ(h.session()->name, std::string("work"));
}

KITE_TEST(app, delete_asks_before_touching_anything) {
    Harness h;
    h.app.Execute(Cmd::CursorBottom);
    h.app.Execute(Cmd::DeleteToRecycle);

    KITE_EXPECT_EQ(h.app.prompt().kind, PromptKind::ConfirmDelete);
    KITE_EXPECT_EQ(h.app.prompt().pendingPaths.size(), size_t{ 1 });
    KITE_EXPECT_EQ(h.files.deleteCalls.size(), size_t{ 0 });

    h.app.OnKey(ParseChord("Escape"));
    KITE_EXPECT_EQ(h.files.deleteCalls.size(), size_t{ 0 });

    h.app.Execute(Cmd::DeleteToRecycle);
    h.app.OnKey(ParseChord("Enter"));
    h.Settle();
    KITE_EXPECT_EQ(h.files.deleteCalls.size(), size_t{ 1 });
    KITE_EXPECT(h.files.deleteRecycle[0]);  // went to the recycle bin
}

KITE_TEST(app, permanent_delete_is_a_different_confirmation) {
    Harness h;
    h.app.Execute(Cmd::DeletePermanent);
    KITE_EXPECT_EQ(h.app.prompt().kind, PromptKind::ConfirmDeletePermanent);
    h.app.OnKey(ParseChord("Enter"));
    h.Settle();
    KITE_EXPECT_EQ(h.files.deleteRecycle.size(), size_t{ 1 });
    KITE_EXPECT_FALSE(h.files.deleteRecycle[0]);
}

KITE_TEST(app, copy_and_paste_go_through_the_clipboard) {
    Harness h;
    h.app.Execute(Cmd::CursorBottom);  // notes.txt
    h.app.Execute(Cmd::Copy);
    KITE_EXPECT_EQ(h.shell.clipboardFiles.size(), size_t{ 1 });
    KITE_EXPECT_FALSE(h.shell.clipboardCut);

    h.app.NavigateFocused("C:\\home\\beta");
    h.Settle();
    h.app.Execute(Cmd::Paste);
    KITE_EXPECT_EQ(h.files.copyCalls.size(), size_t{ 1 });
    KITE_EXPECT_EQ(h.files.copyCalls[0].destDir, std::string("C:\\home\\beta"));
}

// Pasting where the items were copied from is the one collision the shell
// cannot resolve on its own: the name is already spoken for by the file being
// copied. The answer is a duplicate under a new name - the report that led here
// was a copy and paste in one folder doing nothing at all.
KITE_TEST(app, pasting_into_the_same_folder_makes_a_duplicate) {
    Harness h;
    h.app.Execute(Cmd::CursorBottom);  // notes.txt
    h.app.Execute(Cmd::Copy);
    h.app.Execute(Cmd::Paste);
    h.Settle();

    // Not CopyTo: that can only name the folder, which is how the collision
    // came about in the first place.
    KITE_EXPECT_EQ(h.files.copyCalls.size(), size_t{ 0 });
    KITE_EXPECT_EQ(h.files.copyAsCalls.size(), size_t{ 1 });
    KITE_EXPECT_EQ(h.files.copyAsCalls[0].destPaths.size(), size_t{ 1 });
    KITE_EXPECT_EQ(h.files.copyAsCalls[0].destPaths[0],
                   std::string("C:\\home\\notes_copy.txt"));
    KITE_EXPECT(h.files.Exists("C:\\home\\notes_copy.txt"));
    // The original stays where it was.
    KITE_EXPECT(h.files.Exists("C:\\home\\notes.txt"));
    KITE_EXPECT_EQ(h.app.statusMessage(),
                   h.app.strings().Format("ui.duplicated", { "1" }));
}

KITE_TEST(app, a_duplicate_steps_past_the_names_already_taken) {
    Harness h;
    h.files.AddFile("C:\\home", "notes_copy.txt");
    h.files.AddFile("C:\\home", "notes_copy2.txt");
    h.app.Execute(Cmd::CursorBottom);  // notes.txt
    h.app.Execute(Cmd::Copy);
    h.app.Execute(Cmd::Paste);
    h.Settle();

    KITE_EXPECT_EQ(h.files.copyAsCalls[0].destPaths[0],
                   std::string("C:\\home\\notes_copy3.txt"));
}

KITE_TEST(app, a_duplicated_folder_keeps_its_whole_name) {
    Harness h;
    h.files.AddDir("C:\\home\\backup.2026");
    h.app.Execute(Cmd::Refresh);
    h.Settle();
    while (h.CursorName() != "backup.2026") h.app.Execute(Cmd::CursorDown);
    h.app.Execute(Cmd::Copy);
    h.app.Execute(Cmd::Paste);
    h.Settle();

    // ".2026" is nobody's extension.
    KITE_EXPECT_EQ(h.files.copyAsCalls[0].destPaths[0],
                   std::string("C:\\home\\backup.2026_copy"));
}

// Two items reaching for one name: the first has not been written yet, so the
// disk cannot be the one to say the name is taken.
KITE_TEST(app, duplicates_in_one_batch_do_not_collide_with_each_other) {
    Harness h;
    h.shell.clipboardFiles = { "C:\\home\\notes.txt", "C:\\home\\notes.txt" };
    h.shell.clipboardCut = false;
    h.app.Execute(Cmd::Paste);
    h.Settle();

    KITE_EXPECT_EQ(h.files.copyAsCalls.size(), size_t{ 1 });
    KITE_EXPECT_EQ(h.files.copyAsCalls[0].destPaths.size(), size_t{ 2 });
    KITE_EXPECT_NE(h.files.copyAsCalls[0].destPaths[0], h.files.copyAsCalls[0].destPaths[1]);
}

// A cut has nowhere to go, so nothing happens - and the fade stays up, because
// the folder it is meant for has not been reached yet.
KITE_TEST(app, cutting_and_pasting_in_one_folder_does_nothing) {
    Harness h;
    h.app.Execute(Cmd::CursorBottom);  // notes.txt
    h.app.Execute(Cmd::Cut);
    h.app.Execute(Cmd::Paste);
    h.Settle();

    KITE_EXPECT_EQ(h.files.copyCalls.size(), size_t{ 0 });
    KITE_EXPECT_EQ(h.files.copyAsCalls.size(), size_t{ 0 });
    KITE_EXPECT(h.app.IsCut("C:\\home\\notes.txt"));
}

// One clipboard, two answers: what came from elsewhere is copied under its own
// name, what was already here is duplicated.
KITE_TEST(app, a_mixed_paste_splits_into_a_copy_and_a_duplicate) {
    Harness h;
    h.shell.clipboardFiles = { "C:\\home\\notes.txt", "C:\\home\\alpha\\inner.md" };
    h.shell.clipboardCut = false;
    h.app.Execute(Cmd::Paste);
    h.Settle();

    KITE_EXPECT_EQ(h.files.copyAsCalls.size(), size_t{ 1 });
    KITE_EXPECT_EQ(h.files.copyAsCalls[0].paths[0], std::string("C:\\home\\notes.txt"));
    KITE_EXPECT_EQ(h.files.copyCalls.size(), size_t{ 1 });
    KITE_EXPECT_EQ(h.files.copyCalls[0].paths[0], std::string("C:\\home\\alpha\\inner.md"));
    KITE_EXPECT_EQ(h.files.copyCalls[0].destDir, std::string("C:\\home"));
}

KITE_TEST(app, cut_marks_the_clipboard_as_a_move) {
    Harness h;
    h.app.Execute(Cmd::Cut);
    KITE_EXPECT(h.shell.clipboardCut);
}

// The clipboard never says what it is holding, so what was cut is remembered
// here - it is the only thing the listing can draw the fade from.
KITE_TEST(app, cut_remembers_its_items_and_copy_forgets_them) {
    Harness h;
    h.app.Execute(Cmd::CursorBottom);  // notes.txt
    h.app.Execute(Cmd::Cut);
    KITE_EXPECT(h.app.IsCut("C:\\home\\notes.txt"));
    KITE_EXPECT_FALSE(h.app.IsCut("C:\\home\\alpha"));

    h.app.Execute(Cmd::Copy);
    KITE_EXPECT_FALSE(h.app.IsCut("C:\\home\\notes.txt"));
}

KITE_TEST(app, a_whole_selection_is_cut_at_once) {
    Harness h;
    h.app.Execute(Cmd::ToggleSelection);  // alpha, and the cursor steps on
    h.app.Execute(Cmd::ToggleSelection);  // beta
    h.app.Execute(Cmd::Cut);
    KITE_EXPECT(h.app.IsCut("C:\\home\\alpha"));
    KITE_EXPECT(h.app.IsCut("C:\\home\\beta"));
}

KITE_TEST(app, pasting_ends_the_cut) {
    Harness h;
    h.app.Execute(Cmd::CursorBottom);
    h.app.Execute(Cmd::Cut);
    h.shell.clipboardFiles = { "C:\\home\\notes.txt" };
    h.shell.clipboardCut = true;

    h.app.NavigateFocused("C:\\home\\beta");
    h.Settle();
    h.app.Execute(Cmd::Paste);
    h.Settle();
    KITE_EXPECT_FALSE(h.app.IsCut("C:\\home\\notes.txt"));
}

// Escape is Cmd::SelectNone, and calling off a cut is what it does in Explorer.
KITE_TEST(app, escape_drops_the_cut_marks) {
    Harness h;
    h.app.Execute(Cmd::CursorBottom);
    h.app.Execute(Cmd::Cut);
    h.app.Execute(Cmd::SelectNone);
    KITE_EXPECT_FALSE(h.app.IsCut("C:\\home\\notes.txt"));
}

// Another window can take the clipboard while Kite is in the background, and a
// row still drawn faded would be claiming something that is no longer true.
KITE_TEST(app, coming_back_to_a_changed_clipboard_drops_the_cut_marks) {
    Harness h;
    h.app.Execute(Cmd::CursorBottom);
    h.app.Execute(Cmd::Cut);

    h.app.SetWindowActive(false);
    h.shell.clipboardFiles = { "C:\\elsewhere\\other.txt" };
    h.shell.clipboardCut = false;
    h.app.SetWindowActive(true);
    KITE_EXPECT_FALSE(h.app.IsCut("C:\\home\\notes.txt"));
}

KITE_TEST(app, coming_back_to_the_same_cut_keeps_the_marks) {
    Harness h;
    h.app.Execute(Cmd::CursorBottom);
    h.app.Execute(Cmd::Cut);

    h.app.SetWindowActive(false);
    h.app.SetWindowActive(true);
    KITE_EXPECT(h.app.IsCut("C:\\home\\notes.txt"));
}

// A .lnk that names a folder is a folder here: handing it to the shell is what
// made the target open in Explorer, in a window the user did not ask for.
KITE_TEST(app, a_folder_shortcut_opens_inside_kite) {
    Harness h;
    h.files.AddFile("C:\\home", "beta.lnk", 200, 4000);
    h.shell.shortcuts["C:\\home\\beta.lnk"] = "C:\\home\\beta";
    h.app.Execute(Cmd::Refresh);
    h.Settle();

    h.app.NavigateFocused("C:\\home");
    h.Settle();
    for (int i = 0; i < h.tab()->ItemCount(); ++i) {
        const fs::Entry* e = h.tab()->EntryAt(i);
        if (e && e->name == "beta.lnk") {
            h.app.ActivateEntry(i, false);
            break;
        }
    }
    h.Settle();
    KITE_EXPECT_EQ(h.tab()->path, std::string("C:\\home\\beta"));
    KITE_EXPECT(h.shell.opened.empty());  // the shell was never asked
}

// A link to a file is still the shell's business: it may well name a program,
// and starting it is exactly what the shell does with the .lnk itself.
KITE_TEST(app, a_file_shortcut_still_goes_to_the_shell) {
    Harness h;
    h.files.AddFile("C:\\home", "notes.lnk", 200, 4000);
    h.shell.shortcuts["C:\\home\\notes.lnk"] = "C:\\home\\notes.txt";
    h.app.Execute(Cmd::Refresh);
    h.Settle();

    h.app.NavigateFocused("C:\\home");
    h.Settle();
    for (int i = 0; i < h.tab()->ItemCount(); ++i) {
        const fs::Entry* e = h.tab()->EntryAt(i);
        if (e && e->name == "notes.lnk") {
            h.app.ActivateEntry(i, false);
            break;
        }
    }
    KITE_EXPECT_EQ(h.tab()->path, std::string("C:\\home"));
    KITE_EXPECT_EQ(h.shell.opened.size(), size_t{ 1 });
    KITE_EXPECT_EQ(h.shell.opened[0], std::string("C:\\home\\notes.lnk"));
}

KITE_TEST(app, copy_path_puts_full_paths_on_the_clipboard) {
    Harness h;
    h.app.Execute(Cmd::CursorBottom);
    h.app.Execute(Cmd::CopyPath);
    KITE_EXPECT_EQ(h.shell.clipboardText.size(), size_t{ 1 });
    KITE_EXPECT_EQ(h.shell.clipboardText[0], std::string("C:\\home\\notes.txt"));

    h.app.Execute(Cmd::CopyName);
    KITE_EXPECT_EQ(h.shell.clipboardText[1], std::string("notes.txt"));
}

KITE_TEST(app, tabs_open_close_and_reopen) {
    Harness h;
    h.app.Execute(Cmd::NewTab);
    KITE_EXPECT_EQ(h.pane()->tabs.size(), size_t{ 2 });

    h.app.NavigateFocused("C:\\home\\beta");
    h.Settle();
    h.app.Execute(Cmd::CloseTab);
    KITE_EXPECT_EQ(h.pane()->tabs.size(), size_t{ 1 });

    h.app.Execute(Cmd::ReopenTab);
    h.Settle();
    KITE_EXPECT_EQ(h.pane()->tabs.size(), size_t{ 2 });
    KITE_EXPECT_EQ(h.tab()->path, std::string("C:\\home\\beta"));
}

KITE_TEST(app, tab_numbers_select_directly) {
    Harness h;
    h.app.Execute(Cmd::NewTab);
    h.app.Execute(Cmd::NewTab);
    KITE_EXPECT_EQ(h.pane()->tabs.size(), size_t{ 3 });

    h.app.Execute(Cmd::Tab1);
    KITE_EXPECT_EQ(h.pane()->active, 0);
    h.app.Execute(Cmd::TabLast);
    KITE_EXPECT_EQ(h.pane()->active, 2);
}

KITE_TEST(app, splitting_and_closing_panes) {
    Harness h;
    h.app.Execute(Cmd::SplitLeftRight);
    h.Settle();
    KITE_EXPECT_EQ(h.session()->Panes().size(), size_t{ 2 });

    h.app.Execute(Cmd::SplitTopBottom);
    h.Settle();
    KITE_EXPECT_EQ(h.session()->Panes().size(), size_t{ 3 });

    h.app.Execute(Cmd::ClosePane);
    KITE_EXPECT_EQ(h.session()->Panes().size(), size_t{ 2 });
}

KITE_TEST(app, sync_other_pane_copies_the_current_folder_across) {
    Harness h;
    h.app.Execute(Cmd::SplitLeftRight);
    h.Settle();
    h.app.NavigateFocused("C:\\home\\alpha");
    h.Settle();

    h.app.Execute(Cmd::SyncOtherPane);
    h.Settle();
    for (Pane* pane : h.session()->Panes()) {
        KITE_EXPECT_EQ(pane->activeTab()->path, std::string("C:\\home\\alpha"));
    }
}

KITE_TEST(app, sessions_switch_instantly_and_keep_their_layout) {
    Harness h;
    h.app.Execute(Cmd::SplitLeftRight);
    h.Settle();

    h.app.Execute(Cmd::NewSession);
    h.Settle();
    KITE_EXPECT_EQ(h.app.workspace().sessions.size(), size_t{ 2 });
    KITE_EXPECT_EQ(h.session()->Panes().size(), size_t{ 1 });

    h.app.Execute(Cmd::Session1);
    h.Settle();
    KITE_EXPECT_EQ(h.app.workspace().active, 0);
    KITE_EXPECT_EQ(h.session()->Panes().size(), size_t{ 2 });
}

KITE_TEST(app, closing_the_last_tab_closes_the_window) {
    Harness h;
    KITE_EXPECT_EQ(h.pane()->tabs.size(), size_t{ 1 });

    h.app.Execute(Cmd::CloseTab);
    KITE_EXPECT_EQ(h.host.closeCount, 1);
    // The pane keeps its tab: the window is going away, not the tab.
    KITE_EXPECT_EQ(h.pane()->tabs.size(), size_t{ 1 });
    KITE_EXPECT_EQ(h.app.workspace().closedTabs.size(), size_t{ 0 });
}

KITE_TEST(app, a_tab_pulled_out_opens_a_window_on_its_folder_and_leaves_the_bar) {
    Harness h;
    h.app.Execute(Cmd::NewTab);
    h.Settle();
    h.app.OpenPath("C:\\home\\alpha", false);
    h.Settle();
    KITE_EXPECT_EQ(h.pane()->tabs.size(), size_t{ 2 });

    KITE_EXPECT(h.app.DetachTabToNewWindow(h.pane(), 1));
    h.Settle();
    KITE_EXPECT_EQ(h.host.newWindows.size(), size_t{ 1 });
    KITE_EXPECT_EQ(h.host.newWindows[0], std::string("C:\\home\\alpha"));
    KITE_EXPECT_EQ(h.pane()->tabs.size(), size_t{ 1 });
    // It moved rather than closed: handing it back with Ctrl+Shift+T would mean
    // the same folder in two windows.
    KITE_EXPECT_EQ(h.app.workspace().closedTabs.size(), size_t{ 0 });
}

KITE_TEST(app, a_tab_that_could_not_open_a_window_stays_where_it_is) {
    Harness h;
    h.app.Execute(Cmd::NewTab);
    h.Settle();
    h.host.canOpenNewWindow = false;

    KITE_EXPECT_FALSE(h.app.DetachTabToNewWindow(h.pane(), 1));
    KITE_EXPECT_EQ(h.pane()->tabs.size(), size_t{ 2 });
    KITE_EXPECT_EQ(h.app.statusMessage(), h.app.strings().Get("ui.new_window_failed"));
}

KITE_TEST(app, pulling_out_the_last_tab_of_a_split_pane_folds_the_pane_away) {
    Harness h;
    h.app.Execute(Cmd::SplitLeftRight);
    h.Settle();
    KITE_EXPECT_EQ(h.session()->Panes().size(), size_t{ 2 });

    KITE_EXPECT(h.app.DetachTabToNewWindow(h.pane(), 0));
    h.Settle();
    KITE_EXPECT_EQ(h.host.newWindows.size(), size_t{ 1 });
    KITE_EXPECT_EQ(h.session()->Panes().size(), size_t{ 1 });
}

KITE_TEST(app, the_only_tab_in_the_window_cannot_be_pulled_out) {
    Harness h;
    KITE_EXPECT_EQ(h.pane()->tabs.size(), size_t{ 1 });

    // The window it would open is the window it is already in.
    KITE_EXPECT_FALSE(h.app.DetachTabToNewWindow(h.pane(), 0));
    KITE_EXPECT_EQ(h.host.newWindows.size(), size_t{ 0 });
    KITE_EXPECT_EQ(h.pane()->tabs.size(), size_t{ 1 });
    KITE_EXPECT_EQ(h.app.statusMessage(), h.app.strings().Get("ui.cannot_detach_last"));
}

KITE_TEST(app, the_last_tab_of_a_split_pane_folds_the_pane_away) {
    Harness h;
    h.app.Execute(Cmd::SplitLeftRight);
    h.Settle();
    KITE_EXPECT_EQ(h.session()->Panes().size(), size_t{ 2 });
    KITE_EXPECT_EQ(h.pane()->tabs.size(), size_t{ 1 });

    h.app.Execute(Cmd::CloseTab);
    h.Settle();
    // The window stays: the other pane still has something to show.
    KITE_EXPECT_EQ(h.host.closeCount, 0);
    KITE_EXPECT_EQ(h.session()->Panes().size(), size_t{ 1 });
    KITE_EXPECT_EQ(h.app.workspace().closedTabs.size(), size_t{ 1 });

    // Down to one pane holding one tab, the same key closes the window.
    h.app.Execute(Cmd::CloseTab);
    KITE_EXPECT_EQ(h.host.closeCount, 1);
}

KITE_TEST(app, the_last_tab_closes_the_window_even_with_sessions_left) {
    Harness h;
    h.app.Execute(Cmd::NewSession);
    h.Settle();
    KITE_EXPECT_EQ(h.app.workspace().sessions.size(), size_t{ 2 });
    KITE_EXPECT_EQ(h.session()->Panes().size(), size_t{ 1 });

    // Only the pane step is taken. A session holds panes that are not on
    // screen, so running one pane empty must not close it.
    h.app.Execute(Cmd::CloseTab);
    KITE_EXPECT_EQ(h.host.closeCount, 1);
    KITE_EXPECT_EQ(h.app.workspace().sessions.size(), size_t{ 2 });
}

KITE_TEST(app, sessions_move_along_the_bar_and_stay_active) {
    Harness h;
    h.app.Execute(Cmd::NewSession);
    h.app.Execute(Cmd::NewSession);
    h.Settle();
    KITE_EXPECT_EQ(h.app.workspace().sessions.size(), size_t{ 3 });
    const std::string moved = h.session()->name;

    h.app.Execute(Cmd::MoveSessionLeft);
    KITE_EXPECT_EQ(h.app.workspace().active, 1);
    KITE_EXPECT_EQ(h.session()->name, moved);
    KITE_EXPECT_EQ(h.app.workspace().sessions[1]->name, moved);

    h.app.Execute(Cmd::MoveSessionRight);
    KITE_EXPECT_EQ(h.app.workspace().active, 2);
    KITE_EXPECT_EQ(h.session()->name, moved);
}

KITE_TEST(app, a_session_at_the_end_of_the_bar_does_not_wrap_round) {
    Harness h;
    h.app.Execute(Cmd::NewSession);
    h.Settle();
    KITE_EXPECT_EQ(h.app.workspace().active, 1);

    // Already last: the chip stays where it is rather than jumping to the front.
    h.app.Execute(Cmd::MoveSessionRight);
    KITE_EXPECT_EQ(h.app.workspace().active, 1);

    h.app.Execute(Cmd::MoveSessionLeft);
    KITE_EXPECT_EQ(h.app.workspace().active, 0);
    h.app.Execute(Cmd::MoveSessionLeft);
    KITE_EXPECT_EQ(h.app.workspace().active, 0);
}

KITE_TEST(app, bookmarks_toggle_and_navigate) {
    Harness h;
    h.app.NavigateFocused("C:\\home\\beta");
    h.Settle();

    h.app.Execute(Cmd::AddBookmark);
    KITE_EXPECT_EQ(h.app.workspace().bookmarks.size(), size_t{ 1 });
    KITE_EXPECT(h.app.HasBookmark("C:\\home\\beta"));

    h.app.NavigateFocused("C:\\home");
    h.Settle();
    h.app.Execute(Cmd::Bookmark1);
    h.Settle();
    KITE_EXPECT_EQ(h.tab()->path, std::string("C:\\home\\beta"));

    h.app.Execute(Cmd::RemoveBookmark);
    KITE_EXPECT_EQ(h.app.workspace().bookmarks.size(), size_t{ 0 });
}

KITE_TEST(app, losing_the_window_focus_repaints_once_and_only_on_a_change) {
    // Windows reports activation far more often than it changes - every menu,
    // every dialog - and a repaint per report is a repaint per mouse move over
    // the caption.
    Harness h;
    KITE_EXPECT(h.app.windowActive());

    const int before = h.host.invalidateCount;
    h.app.SetWindowActive(false);
    KITE_EXPECT_FALSE(h.app.windowActive());
    KITE_EXPECT_EQ(h.host.invalidateCount, before + 1);

    h.app.SetWindowActive(false);
    KITE_EXPECT_EQ(h.host.invalidateCount, before + 1);

    h.app.SetWindowActive(true);
    KITE_EXPECT(h.app.windowActive());
    KITE_EXPECT_EQ(h.host.invalidateCount, before + 2);
}

KITE_TEST(app, the_extended_context_menu_is_a_distinct_command) {
    Harness h;
    h.app.Execute(Cmd::ContextMenu);
    KITE_EXPECT_FALSE(h.shell.lastContextMenuExtended);

    h.app.Execute(Cmd::ExtendedContextMenu);
    KITE_EXPECT(h.shell.lastContextMenuExtended);
    KITE_EXPECT_EQ(h.shell.contextMenuCalls, 2);
}

KITE_TEST(app, the_folder_menu_targets_the_folder_even_with_a_selection) {
    // The whole point of the command: a selected file otherwise hides the folder
    // being viewed, and "New >" or "Open in Terminal" belong to the folder.
    Harness h;
    h.app.Execute(Cmd::SelectAll);
    KITE_EXPECT(h.tab()->MarkedCount() > 1);

    h.app.Execute(Cmd::FolderContextMenu);
    KITE_EXPECT_EQ(h.shell.lastContextMenuPaths.size(), size_t{ 1 });
    KITE_EXPECT_EQ(h.shell.lastContextMenuPaths.front(), h.tab()->path);
    // Retargeting is all it does; the extended verbs are their own command.
    KITE_EXPECT_FALSE(h.shell.lastContextMenuExtended);
}

KITE_TEST(app, the_folder_menu_has_its_own_extended_form) {
    // Both halves of the split have to survive: the extended folder menu is
    // still aimed at the folder, and still asks for the item menu.
    Harness h;
    h.app.Execute(Cmd::ExtendedFolderContextMenu);

    KITE_EXPECT(h.shell.lastContextMenuExtended);
    KITE_EXPECT_FALSE(h.shell.lastContextMenuBackground);
    KITE_EXPECT_EQ(h.shell.lastContextMenuPaths.size(), size_t{ 1 });
    KITE_EXPECT_EQ(h.shell.lastContextMenuPaths.front(), h.tab()->path);
}

KITE_TEST(app, the_folder_menu_asks_for_the_item_menu) {
    // Which menu is asked for decides what the user reads. The background menu
    // answers for the space *inside* the folder - New, Paste - so asking for it
    // here produced a menu with most of the folder's own verbs missing: no Open,
    // no Send to, no Copy, no Delete. The command means "this folder as the
    // target", which is the item menu.
    Harness h;
    h.app.Execute(Cmd::FolderContextMenu);
    KITE_EXPECT_FALSE(h.shell.lastContextMenuBackground);
    KITE_EXPECT_EQ(h.shell.lastContextMenuPaths.front(), h.tab()->path);
}

KITE_TEST(app, the_empty_space_answers_with_the_background_menu) {
    // Not the cursor row's menu: nothing was clicked, so the folder answers for
    // the space inside it. The cursor is deliberately left on a real row.
    Harness h;
    h.tab()->cursor = 1;
    KITE_EXPECT_FALSE(h.tab()->CursorPath().empty());

    h.app.ShowBackgroundContextMenu(10, 20, false);

    KITE_EXPECT(h.shell.lastContextMenuBackground);
    KITE_EXPECT_EQ(h.shell.lastContextMenuPaths.size(), size_t{ 1 });
    KITE_EXPECT_EQ(h.shell.lastContextMenuPaths.front(), h.tab()->path);
    KITE_EXPECT_FALSE(h.shell.lastContextMenuExtended);

    h.app.ShowBackgroundContextMenu(10, 20, true);
    KITE_EXPECT(h.shell.lastContextMenuExtended);
    KITE_EXPECT(h.shell.lastContextMenuBackground);
}

KITE_TEST(app, a_menu_with_nothing_to_act_on_falls_back_to_the_background_menu) {
    // Nothing marked and the cursor on "..": there is no item to offer a menu
    // for, so the folder being listed answers - as its background, which is the
    // menu an empty-space right-click gives in Explorer.
    Harness h;
    h.app.Execute(Cmd::SelectNone);
    h.tab()->cursor = 0;
    KITE_EXPECT(h.tab()->IsParentRow(0));

    h.app.Execute(Cmd::ContextMenu);

    KITE_EXPECT_EQ(h.shell.lastContextMenuPaths.size(), size_t{ 1 });
    KITE_EXPECT_EQ(h.shell.lastContextMenuPaths.front(), h.tab()->path);
    KITE_EXPECT(h.shell.lastContextMenuBackground);
}

KITE_TEST(app, a_menu_over_a_selection_stays_the_item_menu) {
    Harness h;
    h.app.Execute(Cmd::SelectAll);
    h.app.Execute(Cmd::ContextMenu);

    KITE_EXPECT(h.shell.lastContextMenuPaths.size() > 1);
    KITE_EXPECT_FALSE(h.shell.lastContextMenuBackground);
}

KITE_TEST(app, the_folder_menu_has_its_own_binding) {
    Harness h;
    KITE_EXPECT(h.app.OnKey(ParseChord("Ctrl+Menu")));
    KITE_EXPECT_EQ(h.shell.contextMenuCalls, 1);
    KITE_EXPECT_EQ(h.shell.lastContextMenuPaths.front(), h.tab()->path);
}

KITE_TEST(app, a_menu_opened_from_the_keyboard_lands_on_the_cursor_row) {
    // Not on the pointer: the pointer is wherever it was left, which on a
    // keyboard-driven move is nowhere near the row being acted on.
    Harness h;
    Pane* p = h.pane();
    p->listArea = { 10.0f, 40.0f, 400.0f, 400.0f };
    p->rowHeight = 20.0f;
    h.tab()->cursor = 2;
    h.tab()->scroll = 0.0f;

    h.app.Execute(Cmd::ContextMenu);
    // Bottom-left of row 2, indented, then offset by the fake client origin.
    KITE_EXPECT_EQ(h.shell.lastContextMenuX, 118);
    KITE_EXPECT_EQ(h.shell.lastContextMenuY, 300);
}

KITE_TEST(app, a_menu_anchor_stays_inside_the_list_when_the_cursor_is_scrolled_away) {
    Harness h;
    Pane* p = h.pane();
    p->listArea = { 10.0f, 40.0f, 400.0f, 400.0f };
    p->rowHeight = 20.0f;
    h.tab()->cursor = 0;
    h.tab()->scroll = 500.0f;

    h.app.Execute(Cmd::ContextMenu);
    // Clamped to the top of the list: 200 (fake origin) + 40 (list top).
    KITE_EXPECT_EQ(h.shell.lastContextMenuY, 240);
}

KITE_TEST(app, a_menu_falls_back_to_the_pointer_when_there_is_no_anchor) {
    // Before the first paint there is no layout to aim at, and a window that
    // cannot map coordinates cannot produce one either. A negative pair tells
    // the shell to use the pointer, which is what Windows does by default.
    Harness h;
    h.pane()->listArea = {};
    h.app.Execute(Cmd::ContextMenu);
    KITE_EXPECT_EQ(h.shell.lastContextMenuX, -1);
    KITE_EXPECT_EQ(h.shell.lastContextMenuY, -1);

    h.pane()->listArea = { 10.0f, 40.0f, 400.0f, 400.0f };
    h.host.canMapCoordinates = false;
    h.app.Execute(Cmd::ContextMenu);
    KITE_EXPECT_EQ(h.shell.lastContextMenuX, -1);
    KITE_EXPECT_EQ(h.shell.lastContextMenuY, -1);
}

KITE_TEST(app, the_shell_menu_is_asked_for_the_theme_kite_is_using) {
    // The menu is drawn by another process, which has no way of knowing that
    // Kite is dark while the desktop is light - so it is told, every time.
    Harness h;
    h.app.Execute(Cmd::ContextMenu);
    KITE_EXPECT(h.shell.lastContextMenuDark);

    h.app.Execute(Cmd::ToggleTheme);
    h.app.Execute(Cmd::ContextMenu);
    KITE_EXPECT_FALSE(h.shell.lastContextMenuDark);
}

KITE_TEST(app, a_shell_menu_that_cannot_be_shown_is_reported) {
    // The menu lives in another process now, so "it did not open" is a state the
    // user has to be told about instead of a right-click that does nothing.
    Harness h;
    h.shell.contextMenuShown = false;
    h.app.Execute(Cmd::ContextMenu);

    KITE_EXPECT_EQ(h.shell.contextMenuCalls, 1);
    KITE_EXPECT_EQ(h.app.statusMessage(), h.app.strings().Get("ui.shell_menu_failed"));
}

KITE_TEST(app, a_shell_menu_that_was_shown_leaves_no_error_behind) {
    Harness h;
    h.app.Execute(Cmd::ContextMenu);
    KITE_EXPECT_NE(h.app.statusMessage(), h.app.strings().Get("ui.shell_menu_failed"));
}

KITE_TEST(app, theme_and_language_can_be_toggled_at_runtime) {
    Harness h;
    KITE_EXPECT(h.app.theme().dark);
    h.app.Execute(Cmd::ToggleTheme);
    KITE_EXPECT_FALSE(h.app.theme().dark);

    KITE_EXPECT_EQ(h.app.strings().code(), std::string("en"));
    h.app.Execute(Cmd::ToggleLanguage);
    KITE_EXPECT_EQ(h.app.strings().code(), std::string("ja"));
}

KITE_TEST(app, the_shortcut_editor_writes_every_change_straight_to_keys_ini) {
    Harness h;
    h.app.Execute(Cmd::ShowKeySettings);
    KITE_EXPECT(h.app.keyEditor().visible());

    // Put Ctrl+Alt+Shift+F9 on "new folder", the way the user would.
    for (size_t i = 0; i < h.app.keyEditor().rows().size(); ++i) {
        if (h.app.keyEditor().rows()[i].cmd == Cmd::NewFolder) {
            h.app.keyEditor().SelectRow(static_cast<int>(i));
            break;
        }
    }
    h.app.OnKey(ParseChord("Enter"));
    h.app.OnKey(ParseChord("Ctrl+Alt+Shift+F9"));

    const std::string& written = test::FakeFiles()["C:\\home\\config\\keys.ini"];
    KITE_EXPECT_NE(written.find("file.new_folder=Ctrl+Alt+Shift+F9"), std::string::npos);

    // And the binding is live the moment the screen closes - which also says so.
    h.app.OnKey(ParseChord("Escape"));
    KITE_EXPECT_FALSE(h.app.keyEditor().visible());
    KITE_EXPECT_EQ(h.app.statusMessage(), h.app.strings().Get("ui.key_settings_saved"));
    KITE_EXPECT(h.app.OnKey(ParseChord("Ctrl+Alt+Shift+F9")));
    KITE_EXPECT_EQ(h.app.prompt().kind, PromptKind::NewFolder);
}

KITE_TEST(app, keys_pressed_in_the_shortcut_editor_do_not_reach_the_file_list) {
    Harness h;
    const size_t tabs = h.pane()->tabs.size();

    h.app.Execute(Cmd::ShowKeySettings);
    KITE_EXPECT(h.app.OnKey(ParseChord("Ctrl+T")));
    KITE_EXPECT_EQ(h.pane()->tabs.size(), tabs);

    // Its own chord is the exception: it closes the screen again.
    KITE_EXPECT(h.app.OnKey(ParseChord("Ctrl+F1")));
    KITE_EXPECT_FALSE(h.app.keyEditor().visible());
}

KITE_TEST(app, the_shortcut_editor_and_the_cheat_sheet_are_never_both_up) {
    Harness h;
    h.app.Execute(Cmd::ShowKeyHelp);
    h.app.Execute(Cmd::ShowKeySettings);
    KITE_EXPECT(h.app.keyEditor().visible());
    KITE_EXPECT_FALSE(h.app.keyHelpVisible());

    h.app.Execute(Cmd::ShowKeyHelp);
    KITE_EXPECT(h.app.keyHelpVisible());
    KITE_EXPECT_FALSE(h.app.keyEditor().visible());
}

KITE_TEST(app, typing_in_the_shortcut_editor_filters_instead_of_reaching_a_prompt) {
    Harness h;
    h.app.Execute(Cmd::ShowKeySettings);
    KITE_EXPECT(h.app.OnChar('t'));
    KITE_EXPECT(h.app.OnChar('a'));
    KITE_EXPECT(h.app.OnChar('b'));

    KITE_EXPECT_EQ(h.app.keyEditor().filter(), std::string("tab"));
    KITE_EXPECT_FALSE(h.app.prompt().active());
}

KITE_TEST(app, quit_asks_the_host_to_close) {
    Harness h;
    h.app.Execute(Cmd::Quit);
    KITE_EXPECT_EQ(h.host.closeCount, 1);
}

KITE_TEST(app, watches_follow_the_visible_tabs) {
    Harness h;
    KITE_EXPECT_EQ(h.watcher.active.size(), size_t{ 1 });
    KITE_EXPECT_EQ(h.watcher.active.begin()->second, std::string("C:\\home"));

    h.app.Execute(Cmd::SplitLeftRight);
    h.Settle();
    KITE_EXPECT_EQ(h.watcher.active.size(), size_t{ 2 });

    h.app.NavigateFocused("C:\\home\\alpha");
    h.Settle();
    bool sawAlpha = false;
    for (const auto& [id, watchedPath] : h.watcher.active) {
        if (watchedPath == "C:\\home\\alpha") sawAlpha = true;
    }
    KITE_EXPECT(sawAlpha);

    h.app.Execute(Cmd::ClosePane);
    KITE_EXPECT_EQ(h.watcher.active.size(), size_t{ 1 });
}

KITE_TEST(app, a_change_notification_re_lists_the_folder) {
    Harness h;
    const int before = h.files.listCalls;

    h.files.AddFile("C:\\home", "appeared.txt", 1, 1);
    h.watcher.Emit(h.tab()->watchId, "C:\\home");
    h.Settle();

    KITE_EXPECT(h.files.listCalls > before);
    KITE_EXPECT_EQ(h.tab()->ItemCount(), 6);
}

KITE_TEST(app, returning_to_an_unwatched_tab_re_lists_it) {
    // A background tab carries no watch, so whatever it is showing may have
    // gone stale while it was hidden.
    Harness h;
    h.app.Execute(Cmd::NewTab);
    h.app.NavigateFocused("C:\\home\\alpha");
    h.Settle();

    h.app.Execute(Cmd::Tab1);  // back to the C:\home tab
    h.Settle();
    const int before = h.files.listCalls;

    h.app.Execute(Cmd::Tab2);  // away again
    h.Settle();
    h.files.AddFile("C:\\home", "while_away.txt", 1, 1);

    h.app.Execute(Cmd::Tab1);  // and back
    h.Settle();
    KITE_EXPECT(h.files.listCalls > before);
    KITE_EXPECT_EQ(h.tab()->path, std::string("C:\\home"));
    KITE_EXPECT_EQ(h.tab()->ItemCount(), 6);
}

KITE_TEST(app, a_stale_notification_for_a_navigated_tab_is_ignored) {
    Harness h;
    const uint64_t watchId = h.tab()->watchId;
    h.app.NavigateFocused("C:\\home\\alpha");
    h.Settle();

    const int before = h.files.listCalls;
    h.watcher.Emit(watchId, "C:\\home");  // the folder it used to show
    h.Settle();
    KITE_EXPECT_EQ(h.files.listCalls, before);
}

KITE_TEST(app, a_missing_folder_surfaces_as_an_error_not_a_crash) {
    Harness h;
    h.app.NavigateFocused("C:\\does\\not\\exist");
    h.Settle();
    KITE_EXPECT_EQ(h.tab()->listing.status, fs::Status::NotFound);
    KITE_EXPECT_EQ(h.tab()->visible.size(), size_t{ 0 });
}

KITE_TEST(app, workspace_state_round_trips_through_the_config_files) {
    {
        Harness h;
        h.app.Execute(Cmd::SplitLeftRight);
        h.Settle();
        h.app.NavigateFocused("C:\\home\\beta");
        h.Settle();
        h.app.Execute(Cmd::AddBookmark);
        h.app.SaveAll();
    }

    // A second App over the same in-memory config must come back identical.
    test::FakeFileSystem files;
    test::PopulateStandardTree(files);
    test::FakeShell shell;
    test::FakeHost host;
    App reopened(files, shell, host);
    reopened.Init({});
    test::PumpUntilSettled(reopened);

    KITE_EXPECT_EQ(reopened.workspace().activeSession()->Panes().size(), size_t{ 2 });
    KITE_EXPECT_EQ(reopened.workspace().bookmarks.size(), size_t{ 1 });
    KITE_EXPECT_EQ(reopened.workspace().bookmarks[0].path, std::string("C:\\home\\beta"));
}

KITE_TEST(app, command_line_paths_open_as_extra_tabs) {
    test::ResetFakePlatform();
    test::FakeFileSystem files;
    test::PopulateStandardTree(files);
    test::FakeShell shell;
    test::FakeHost host;

    App app(files, shell, host);
    app.Init({ "C:\\home\\alpha", "C:\\home\\beta" });
    test::PumpUntilSettled(app);

    KITE_EXPECT_EQ(app.workspace().focusedPane()->tabs.size(), size_t{ 3 });
    KITE_EXPECT_EQ(app.workspace().focusedTab()->path, std::string("C:\\home\\beta"));
}

KITE_TEST(app, a_new_window_is_asked_for_the_folder_being_viewed) {
    // The window is another process, so all the controller can do is name the
    // folder it wants that process to start on.
    Harness h;
    KITE_EXPECT(h.app.OnKey(ParseChord("Ctrl+N")));
    KITE_EXPECT_EQ(h.host.newWindows.size(), size_t{ 1 });
    KITE_EXPECT_EQ(h.host.newWindows[0], std::string("C:\\home"));

    h.app.Execute(Cmd::OpenSelected);  // into alpha
    h.Settle();
    h.app.Execute(Cmd::NewWindow);
    KITE_EXPECT_EQ(h.host.newWindows.size(), size_t{ 2 });
    KITE_EXPECT_EQ(h.host.newWindows[1], std::string("C:\\home\\alpha"));
}

KITE_TEST(app, a_new_window_that_could_not_be_opened_is_reported) {
    // Launching the exe again can fail (it was replaced under a running Kite,
    // for one). A window that never appears has to say why, like the shell menu.
    Harness h;
    h.host.canOpenNewWindow = false;
    h.app.Execute(Cmd::NewWindow);
    KITE_EXPECT_EQ(h.app.statusMessage(), h.app.strings().Get("ui.new_window_failed"));
}

KITE_TEST(app, a_second_window_opens_where_it_was_pointed_and_not_on_the_saved_sessions) {
    Harness first;
    first.app.Execute(Cmd::NewSession);
    first.app.SaveAll();
    KITE_EXPECT_EQ(test::FakeFiles().count("C:\\home\\config\\sessions.ini"), size_t{ 1 });

    // Same config folder, standalone: the saved sessions are on disk and stay
    // there. Restoring them would put the requested folder several tabs deep in
    // a copy of the first window.
    test::FakeFileSystem files;
    test::PopulateStandardTree(files);
    test::FakeShell shell;
    test::FakeHost host;
    App second(files, shell, host);
    second.SetStandalone(true);
    second.Init({ "C:\\home\\alpha" });
    test::PumpUntilSettled(second);

    KITE_EXPECT_EQ(second.workspace().sessions.size(), size_t{ 1 });
    KITE_EXPECT_EQ(second.workspace().focusedPane()->tabs.size(), size_t{ 1 });
    KITE_EXPECT_EQ(second.workspace().focusedTab()->path, std::string("C:\\home\\alpha"));
}

KITE_TEST(app, a_second_window_never_writes_the_workspace_back) {
    Harness first;
    first.app.SaveAll();
    const std::string saved = test::FakeFiles()["C:\\home\\config\\sessions.ini"];

    test::FakeFileSystem files;
    test::PopulateStandardTree(files);
    test::FakeShell shell;
    test::FakeHost host;
    App second(files, shell, host);
    second.SetStandalone(true);
    second.Init({ "C:\\home\\alpha" });
    test::PumpUntilSettled(second);

    second.Execute(Cmd::NewTab);
    second.Execute(Cmd::SaveWorkspace);
    second.Shutdown();

    // Whichever window is closed last, the first one's layout is what survives.
    KITE_EXPECT_EQ(test::FakeFiles()["C:\\home\\config\\sessions.ini"], saved);
    // And saving is not silently ignored: it says the window does not save.
    KITE_EXPECT_EQ(second.statusMessage(), second.strings().Get("ui.standalone_no_save"));
}

KITE_TEST(app, a_second_window_lets_the_os_place_it_instead_of_landing_on_the_first) {
    Harness first;
    WindowPlacement p;
    p.x = 300;
    p.y = 200;
    p.w = 900;
    p.h = 600;
    first.app.SetPlacement(p);
    first.app.SaveAll();

    test::FakeFileSystem files;
    test::PopulateStandardTree(files);
    test::FakeShell shell;
    test::FakeHost host;
    App second(files, shell, host);
    second.SetStandalone(true);
    second.Init({ "C:\\home\\alpha" });
    test::PumpUntilSettled(second);

    // Size is worth inheriting; the position would put the new window exactly on
    // top of the one that opened it.
    KITE_EXPECT_EQ(second.placement().w, 900);
    KITE_EXPECT_EQ(second.placement().h, 600);
    KITE_EXPECT_EQ(second.placement().x, -1);
    KITE_EXPECT_EQ(second.placement().y, -1);
}

// --- text size --------------------------------------------------------------

KITE_TEST(app, the_text_size_commands_move_the_whole_theme_not_just_the_font) {
    Harness h;
    const float font = h.app.theme().fontSize;
    const float row = h.app.theme().rowHeight;

    h.app.Execute(Cmd::FontLarger);
    KITE_EXPECT(h.app.theme().fontSize > font);
    // A taller row is what keeps the bigger text from being clipped by it.
    KITE_EXPECT(h.app.theme().rowHeight > row);

    h.app.Execute(Cmd::FontSmaller);
    KITE_EXPECT_NEAR(h.app.theme().fontSize, font, 0.01f);
    KITE_EXPECT_NEAR(h.app.theme().rowHeight, row, 0.01f);
}

KITE_TEST(app, the_text_size_stops_at_both_ends_and_says_where_it_is) {
    Harness h;
    for (int i = 0; i < 40; ++i) h.app.Execute(Cmd::FontLarger);
    const float ceiling = h.app.theme().fontSize;
    h.app.Execute(Cmd::FontLarger);
    KITE_EXPECT_NEAR(h.app.theme().fontSize, ceiling, 0.001);
    // Pressing into the limit is not silence: the size is reported either way.
    KITE_EXPECT_FALSE(h.app.statusMessage().empty());

    for (int i = 0; i < 40; ++i) h.app.Execute(Cmd::FontSmaller);
    const float floor = h.app.theme().fontSize;
    KITE_EXPECT(floor < ceiling);
    h.app.Execute(Cmd::FontSmaller);
    KITE_EXPECT_NEAR(h.app.theme().fontSize, floor, 0.001);

    h.app.Execute(Cmd::FontReset);
    KITE_EXPECT_NEAR(h.app.fontScale(), 1.0f, 0.001);
    KITE_EXPECT_NEAR(h.app.theme().fontSize, Theme::Dark().fontSize, 0.001);
}

KITE_TEST(app, switching_the_theme_keeps_the_text_size) {
    // The theme is rebuilt from scratch on a toggle, which is where a scale
    // applied only once gets dropped.
    Harness h;
    h.app.Execute(Cmd::FontLarger);
    const float scaled = h.app.theme().rowHeight;

    h.app.Execute(Cmd::ToggleTheme);
    KITE_EXPECT_FALSE(h.app.theme().dark);
    KITE_EXPECT_NEAR(h.app.theme().rowHeight, scaled, 0.001);
}

KITE_TEST(app, the_text_size_and_the_folded_sidebar_sections_survive_a_restart) {
    {
        Harness h;
        h.app.Execute(Cmd::FontLarger);
        h.app.ToggleSidebarSection(SidebarSection::Drives);
        h.app.SaveAll();
    }

    test::FakeFileSystem files;
    test::PopulateStandardTree(files);
    test::FakeShell shell;
    test::FakeHost host;
    App reopened(files, shell, host);
    reopened.Init({});
    test::PumpUntilSettled(reopened);

    KITE_EXPECT_NEAR(reopened.fontScale(), 1.1f, 0.001);
    KITE_EXPECT(reopened.theme().fontSize > Theme::Dark().fontSize);
    KITE_EXPECT(reopened.sidebarCollapsed(SidebarSection::Drives));
    KITE_EXPECT_FALSE(reopened.sidebarCollapsed(SidebarSection::Bookmarks));
}

// --- sidebar order ----------------------------------------------------------

KITE_TEST(app, reordering_bookmarks_moves_the_number_shortcuts_with_them) {
    // The order is not decoration: Alt+Shift+1..8 counts through this same list,
    // so a bookmark that moved has to answer to its new number.
    Harness h;
    h.app.ToggleBookmark("C:\\home\\alpha");
    h.app.ToggleBookmark("C:\\home\\beta");
    KITE_EXPECT_EQ(h.app.workspace().bookmarks[0].path, std::string("C:\\home\\alpha"));

    KITE_EXPECT(h.app.MoveSidebarItem(SidebarSection::Bookmarks, 1, 0));
    KITE_EXPECT_EQ(h.app.workspace().bookmarks[0].path, std::string("C:\\home\\beta"));

    h.app.Execute(Cmd::Bookmark1);
    h.Settle();
    KITE_EXPECT_EQ(h.tab()->path, std::string("C:\\home\\beta"));
}

KITE_TEST(app, a_move_that_goes_nowhere_is_refused) {
    Harness h;
    h.app.ToggleBookmark("C:\\home\\alpha");
    KITE_EXPECT_FALSE(h.app.MoveSidebarItem(SidebarSection::Bookmarks, 0, 0));
    KITE_EXPECT_FALSE(h.app.MoveSidebarItem(SidebarSection::Bookmarks, 3, 0));
    KITE_EXPECT_FALSE(h.app.MoveSidebarItem(SidebarSection::Bookmarks, -1, 0));
    KITE_EXPECT_FALSE(h.app.MoveSidebarItem(SidebarSection::Count, 0, 0));
    KITE_EXPECT_EQ(h.app.workspace().bookmarks.size(), size_t{ 1 });
}

KITE_TEST(app, a_reordered_quick_access_list_survives_the_next_enumeration) {
    // Quick access and the drives are the OS's lists, handed back in the OS's
    // order every time they are asked for. Without the saved order laid back
    // over them, a drag would be undone by the next refresh - which happens on
    // every F5, not just at start-up.
    Harness h;
    h.files.quickAccess = { "C:\\home\\alpha", "C:\\home\\beta" };
    h.app.Execute(Cmd::Refresh);
    KITE_EXPECT_EQ(h.app.quickAccess()[0].path, std::string("C:\\home\\alpha"));

    KITE_EXPECT(h.app.MoveSidebarItem(SidebarSection::QuickAccess, 1, 0));
    KITE_EXPECT_EQ(h.app.quickAccess()[0].path, std::string("C:\\home\\beta"));

    h.app.Execute(Cmd::Refresh);
    KITE_EXPECT_EQ(h.app.quickAccess()[0].path, std::string("C:\\home\\beta"));
}

KITE_TEST(app, the_sidebar_order_round_trips_through_the_config_file) {
    {
        Harness h;
        h.files.quickAccess = { "C:\\home\\alpha", "C:\\home\\beta" };
        h.app.Execute(Cmd::Refresh);
        h.app.MoveSidebarItem(SidebarSection::QuickAccess, 1, 0);
        h.app.SaveAll();
    }

    test::FakeFileSystem files;
    test::PopulateStandardTree(files);
    files.quickAccess = { "C:\\home\\alpha", "C:\\home\\beta" };
    test::FakeShell shell;
    test::FakeHost host;
    App reopened(files, shell, host);
    reopened.Init({});
    test::PumpUntilSettled(reopened);

    KITE_EXPECT_EQ(reopened.quickAccess()[0].path, std::string("C:\\home\\beta"));
    KITE_EXPECT_EQ(reopened.quickAccess()[1].path, std::string("C:\\home\\alpha"));
}

KITE_TEST(app, an_item_the_saved_order_never_heard_of_follows_the_ones_it_did) {
    // A drive plugged in since the order was saved has no place in it. Landing
    // it at the end is at least predictable; slotting it into the middle by
    // enumeration order would move it every time the list changed.
    {
        Harness h;
        h.files.quickAccess = { "C:\\home\\alpha", "C:\\home\\beta" };
        h.app.Execute(Cmd::Refresh);
        h.app.MoveSidebarItem(SidebarSection::QuickAccess, 1, 0);
        h.app.SaveAll();
    }

    test::FakeFileSystem files;
    test::PopulateStandardTree(files);
    files.quickAccess = { "C:\\home\\alpha\\nested", "C:\\home\\alpha", "C:\\home\\beta" };
    test::FakeShell shell;
    test::FakeHost host;
    App reopened(files, shell, host);
    reopened.Init({});
    test::PumpUntilSettled(reopened);

    KITE_EXPECT_EQ(reopened.quickAccess()[0].path, std::string("C:\\home\\beta"));
    KITE_EXPECT_EQ(reopened.quickAccess()[1].path, std::string("C:\\home\\alpha"));
    KITE_EXPECT_EQ(reopened.quickAccess()[2].path, std::string("C:\\home\\alpha\\nested"));
}

KITE_TEST(app, the_sections_themselves_reorder_and_round_trip) {
    {
        Harness h;
        KITE_EXPECT_EQ(static_cast<int>(h.app.sidebarSections()[0]),
                       static_cast<int>(SidebarSection::QuickAccess));
        KITE_EXPECT(h.app.MoveSidebarSection(2, 0));  // drives to the top
        KITE_EXPECT_EQ(static_cast<int>(h.app.sidebarSections()[0]),
                       static_cast<int>(SidebarSection::Drives));
        h.app.SaveAll();
    }

    test::FakeFileSystem files;
    test::PopulateStandardTree(files);
    test::FakeShell shell;
    test::FakeHost host;
    App reopened(files, shell, host);
    reopened.Init({});
    test::PumpUntilSettled(reopened);

    KITE_EXPECT_EQ(reopened.sidebarSections().size(), size_t{ 3 });
    KITE_EXPECT_EQ(static_cast<int>(reopened.sidebarSections()[0]),
                   static_cast<int>(SidebarSection::Drives));
    KITE_EXPECT_EQ(static_cast<int>(reopened.sidebarSections()[1]),
                   static_cast<int>(SidebarSection::QuickAccess));
}

KITE_TEST(app, a_settings_file_that_names_only_one_section_still_shows_all_three) {
    // Hand-edited files, and files written by an older build, will not name
    // every section. Dropping the ones it misses would take them off the screen
    // with no way back short of editing the file again.
    test::ResetFakePlatform();
    test::FakeFiles()["C:\\home\\config\\settings.ini"] =
        "[sidebar]\nsection=drives\nsection=nonsense\nsection=drives\n";

    test::FakeFileSystem files;
    test::PopulateStandardTree(files);
    test::FakeShell shell;
    test::FakeHost host;
    App app(files, shell, host);
    app.Init({});
    test::PumpUntilSettled(app);

    KITE_EXPECT_EQ(app.sidebarSections().size(), size_t{ 3 });
    KITE_EXPECT_EQ(static_cast<int>(app.sidebarSections()[0]),
                   static_cast<int>(SidebarSection::Drives));
    // The rest follow in the built-in order.
    KITE_EXPECT_EQ(static_cast<int>(app.sidebarSections()[1]),
                   static_cast<int>(SidebarSection::QuickAccess));
    KITE_EXPECT_EQ(static_cast<int>(app.sidebarSections()[2]),
                   static_cast<int>(SidebarSection::Bookmarks));
}

KITE_TEST(app, folding_a_sidebar_section_is_a_toggle_and_asks_for_a_repaint) {
    Harness h;
    KITE_EXPECT_FALSE(h.app.sidebarCollapsed(SidebarSection::Bookmarks));
    const int before = h.host.invalidateCount;

    h.app.ToggleSidebarSection(SidebarSection::Bookmarks);
    KITE_EXPECT(h.app.sidebarCollapsed(SidebarSection::Bookmarks));
    KITE_EXPECT_EQ(h.host.invalidateCount, before + 1);
    // One section at a time: the others are untouched.
    KITE_EXPECT_FALSE(h.app.sidebarCollapsed(SidebarSection::QuickAccess));

    h.app.ToggleSidebarSection(SidebarSection::Bookmarks);
    KITE_EXPECT_FALSE(h.app.sidebarCollapsed(SidebarSection::Bookmarks));
}

// --- type-ahead -------------------------------------------------------------

KITE_TEST(app, a_letter_jumps_to_the_item_it_starts) {
    Harness h;
    h.Type("b");
    KITE_EXPECT_EQ(h.CursorName(), std::string("beta"));
    // Unlike Ctrl+F, nothing is hidden and nothing is selected on the way.
    KITE_EXPECT_EQ(h.tab()->ItemCount(), 5);
    KITE_EXPECT(h.tab()->filter.empty());
    KITE_EXPECT_EQ(h.tab()->MarkedCount(), 0);
    KITE_EXPECT_EQ(h.app.statusMessage(), std::string("Jump: b"));
}

KITE_TEST(app, the_same_letter_twice_walks_to_the_next_one) {
    Harness h;
    h.Type("i");
    KITE_EXPECT_EQ(h.CursorName(), std::string("image2.png"));
    h.Type("i");
    KITE_EXPECT_EQ(h.CursorName(), std::string("image10.png"));
}

KITE_TEST(app, a_letter_that_matches_nothing_says_so_and_stays_put) {
    Harness h;
    h.Type("q");
    KITE_EXPECT_EQ(h.CursorName(), std::string("alpha"));
    KITE_EXPECT_EQ(h.app.statusMessage(), std::string("Jump: q  (no match)"));
}

KITE_TEST(app, space_still_toggles_the_selection_when_no_name_is_being_typed) {
    Harness h;
    // The window sends both: consuming WM_KEYDOWN does not stop the WM_CHAR
    // TranslateMessage has already queued.
    h.app.OnKey(ParseChord("Space"));
    h.app.OnChar(' ');
    KITE_EXPECT_EQ(h.tab()->MarkedCount(), 1);
    KITE_EXPECT_EQ(h.CursorName(), std::string("beta"));
}

KITE_TEST(app, space_joins_the_name_while_one_is_being_typed) {
    Harness h;
    h.Type("n");
    KITE_EXPECT_EQ(h.CursorName(), std::string("notes.txt"));

    h.app.OnKey(ParseChord("Space"));
    h.app.OnChar(' ');
    // The toggle did not fire: a blank is a letter in plenty of file names, and
    // marking rows halfway through a name is not what was asked for.
    KITE_EXPECT_EQ(h.tab()->MarkedCount(), 0);
    KITE_EXPECT_EQ(h.CursorName(), std::string("notes.txt"));
    KITE_EXPECT_EQ(h.app.statusMessage(), std::string("Jump: n   (no match)"));
}

KITE_TEST(app, backspace_shortens_the_name_before_it_leaves_the_folder) {
    Harness h;
    h.app.OpenPath("C:\\home\\alpha", false);
    h.Settle();

    h.Type("i");
    KITE_EXPECT_EQ(h.CursorName(), std::string("inner.md"));
    h.app.OnKey(ParseChord("Backspace"));
    h.Settle();
    KITE_EXPECT_EQ(h.tab()->path, std::string("C:\\home\\alpha"));

    // Once the typed name has expired, Backspace is Go up again.
    test::FakeClockMs() += TypeAhead::kTimeoutMs + 1;
    h.app.OnKey(ParseChord("Backspace"));
    h.Settle();
    KITE_EXPECT_EQ(h.tab()->path, std::string("C:\\home"));
}

KITE_TEST(app, escape_drops_the_typed_name_before_the_selection) {
    Harness h;
    h.app.OnKey(ParseChord("Space"));  // marks alpha and steps on
    h.app.OnChar(' ');
    h.Type("i");
    KITE_EXPECT_EQ(h.tab()->MarkedCount(), 1);

    h.app.OnKey(ParseChord("Escape"));
    KITE_EXPECT_EQ(h.tab()->MarkedCount(), 1);
    h.app.OnKey(ParseChord("Escape"));
    KITE_EXPECT_EQ(h.tab()->MarkedCount(), 0);
}

KITE_TEST(app, any_other_command_ends_the_name_being_typed) {
    Harness h;
    h.Type("i");
    KITE_EXPECT_EQ(h.CursorName(), std::string("image2.png"));

    h.app.OnKey(ParseChord("Down"));
    KITE_EXPECT_EQ(h.CursorName(), std::string("image10.png"));

    // Had "i" survived, "im" would still be matching image10.png. It does not:
    // moving the cursor by hand is a different question from the one being
    // typed, and "m" on its own matches nothing here.
    h.Type("m");
    KITE_EXPECT_EQ(h.app.statusMessage(), std::string("Jump: m  (no match)"));
}

KITE_TEST(app, a_bound_letter_runs_its_command_without_also_jumping) {
    Harness h;
    // Space is the one default binding that produces a character, so it is what
    // this can be seen with: the toggle fires, and the blank it queues must not
    // start a search.
    h.app.OnKey(ParseChord("Space"));
    h.app.OnChar(' ');
    KITE_EXPECT(h.app.statusMessage().find("Jump") == std::string::npos);

    // The mark is still what happened, and the list never moved off beta.
    KITE_EXPECT_EQ(h.tab()->MarkedCount(), 1);
    KITE_EXPECT_EQ(h.CursorName(), std::string("beta"));
}

// ---------------------------------------------------------------------------
// Failures (ROADMAP P1-4)
//
// What these check throughout is that a refused operation says so. ErrorText()
// has no wording for every error code, so a bare SetStatus(err) can leave the
// screen exactly as it was - the operation silently did nothing, which reads as
// "that key does not work" rather than "the file is locked".
// ---------------------------------------------------------------------------

KITE_TEST(app, a_rename_that_fails_says_so_even_when_the_os_says_nothing) {
    Harness h;
    h.files.locked.push_back("C:\\home\\notes.txt");
    h.app.Execute(Cmd::CursorBottom);  // notes.txt
    KITE_EXPECT_EQ(h.CursorName(), std::string("notes.txt"));

    h.app.Execute(Cmd::Rename);
    h.app.OnKey(ParseChord("Ctrl+A"));
    h.Type("renamed.txt");
    h.app.OnKey(ParseChord("Enter"));
    h.Settle();

    KITE_EXPECT_EQ(h.app.statusMessage(), h.app.strings().Get("ui.rename_failed"));
    KITE_EXPECT_EQ(h.CursorName(), std::string("notes.txt"));
    // Nothing happened, so there is nothing to take back either.
    h.app.Execute(Cmd::Undo);
    KITE_EXPECT_EQ(h.app.statusMessage(), h.app.strings().Get("ui.undo_empty"));
}

KITE_TEST(app, what_the_os_said_is_kept_alongside_what_failed) {
    Harness h;
    h.files.locked.push_back("C:\\home\\notes.txt");
    h.files.lockedMessage = "The file is in use";
    h.app.Execute(Cmd::CursorBottom);
    h.app.Execute(Cmd::Rename);
    h.app.OnKey(ParseChord("Ctrl+A"));
    h.Type("renamed.txt");
    h.app.OnKey(ParseChord("Enter"));
    h.Settle();

    // Both halves: the OS knows why, and only Kite knows what was attempted.
    KITE_EXPECT(h.app.statusMessage().find("The file is in use") != std::string::npos);
    KITE_EXPECT_EQ(h.app.statusMessage().find(h.app.strings().Get("ui.rename_failed")),
                   size_t{ 0 });
}

KITE_TEST(app, a_folder_that_cannot_be_created_says_so) {
    Harness h;
    h.files.locked.push_back("C:\\home\\gamma");
    h.app.Execute(Cmd::NewFolder);
    h.Type("gamma");
    h.app.OnKey(ParseChord("Enter"));
    h.Settle();

    KITE_EXPECT_EQ(h.app.statusMessage(), h.app.strings().Get("ui.create_failed"));
    KITE_EXPECT(h.files.dirs.count("C:\\home\\gamma") == 0);
    h.app.Execute(Cmd::Undo);
    KITE_EXPECT_EQ(h.app.statusMessage(), h.app.strings().Get("ui.undo_empty"));
}

KITE_TEST(app, a_delete_that_fails_says_so_instead_of_reporting_done) {
    Harness h;
    h.files.locked.push_back("C:\\home\\notes.txt");
    h.app.Execute(Cmd::CursorBottom);
    h.app.Execute(Cmd::DeleteToRecycle);
    h.app.OnKey(ParseChord("Enter"));
    h.Settle();

    KITE_EXPECT_EQ(h.app.statusMessage(), h.app.strings().Get("ui.delete_failed"));
    KITE_EXPECT(h.files.Exists("C:\\home\\notes.txt"));
    // And no "deleting cannot be undone" marker: nothing was deleted, so the
    // next Ctrl+Z has to still reach whatever came before.
    h.app.Execute(Cmd::Undo);
    KITE_EXPECT_EQ(h.app.statusMessage(), h.app.strings().Get("ui.undo_empty"));
}

KITE_TEST(app, a_paste_that_fails_names_the_operation_that_failed) {
    // A cut and a copy have to read differently even when they fail the same
    // way: one of the two may already have vacated the source.
    for (int pass = 0; pass < 2; ++pass) {
        const bool cut = pass == 1;
        Harness h;
        h.app.Execute(Cmd::CursorBottom);  // notes.txt
        h.app.Execute(cut ? Cmd::Cut : Cmd::Copy);
        h.app.Execute(Cmd::CursorTop);
        h.app.Execute(Cmd::CursorDown);  // alpha
        h.app.Execute(Cmd::OpenSelected);
        h.Settle();
        KITE_EXPECT_EQ(h.tab()->path, std::string("C:\\home\\alpha"));

        h.files.locked.push_back("C:\\home\\alpha");
        h.app.Execute(Cmd::Paste);
        h.Settle();
        KITE_EXPECT_EQ(h.app.statusMessage(),
                       h.app.strings().Get(cut ? "ui.move_failed" : "ui.copy_failed"));
        KITE_EXPECT(h.files.Exists("C:\\home\\notes.txt"));
    }
}

KITE_TEST(app, an_undo_the_disk_refuses_is_not_reported_as_stale) {
    Harness h;
    h.app.Execute(Cmd::CursorBottom);  // notes.txt
    h.app.Execute(Cmd::Rename);
    h.app.OnKey(ParseChord("Ctrl+A"));
    h.Type("renamed.txt");
    h.app.OnKey(ParseChord("Enter"));
    h.Settle();
    KITE_EXPECT(h.files.Exists("C:\\home\\renamed.txt"));

    // Locked between the rename and the undo: the entry still describes the
    // disk, the disk just will not have it. That is a different answer from
    // "the item has changed since", which says there is nothing left to do.
    h.files.locked.push_back("C:\\home\\renamed.txt");
    h.files.lockedMessage = "The file is in use";
    h.app.Execute(Cmd::Undo);

    KITE_EXPECT_EQ(h.app.statusMessage().find(h.app.strings().Get("ui.undo_failed")), size_t{ 0 });
    KITE_EXPECT(h.app.statusMessage().find("The file is in use") != std::string::npos);
    // Spent either way: an undo the disk refused does not get more applicable
    // by being tried again.
    h.app.Execute(Cmd::Undo);
    KITE_EXPECT_EQ(h.app.statusMessage(), h.app.strings().Get("ui.undo_empty"));
}

KITE_TEST(app, a_folder_that_cannot_be_read_keeps_its_error_and_offers_no_rows) {
    Harness h;
    h.files.denied.push_back("C:\\home\\alpha");
    h.app.Execute(Cmd::CursorTop);
    h.app.Execute(Cmd::CursorDown);  // alpha
    h.app.Execute(Cmd::OpenSelected);
    h.Settle();

    KITE_EXPECT_EQ(h.tab()->path, std::string("C:\\home\\alpha"));
    KITE_EXPECT(h.tab()->listing.status == fs::Status::AccessDenied);
    KITE_EXPECT_EQ(h.tab()->ItemCount(), 0);
    // No ".." either: the screen shows the error in place of the list, and a row
    // that is drawn nowhere cannot be walked onto.
    KITE_EXPECT_FALSE(h.tab()->IsParentRow(0));
    // Leaving still works - that is what Alt+Up is for.
    h.app.Execute(Cmd::GoUp);
    h.Settle();
    KITE_EXPECT_EQ(h.tab()->path, std::string("C:\\home"));
    KITE_EXPECT(h.tab()->listing.status == fs::Status::Ok);
}

KITE_TEST(app, a_folder_that_goes_away_under_the_cursor_drops_its_rows) {
    Harness h;
    h.app.Execute(Cmd::CursorTop);
    h.app.Execute(Cmd::CursorDown);  // alpha
    h.app.Execute(Cmd::OpenSelected);
    h.Settle();
    KITE_EXPECT(h.tab()->ItemCount() > 0);

    // The drive was pulled out, or the folder deleted from elsewhere. The
    // refresh has to replace the rows rather than leave the old ones standing:
    // rows that point at nothing are worse than an error message.
    h.files.dirs.erase("C:\\home\\alpha");
    h.app.Execute(Cmd::Refresh);
    h.Settle();
    KITE_EXPECT(h.tab()->listing.status == fs::Status::NotFound);
    KITE_EXPECT_EQ(h.tab()->ItemCount(), 0);
}
