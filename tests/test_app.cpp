// End-to-end tests over the controller, driven the way the window drives it:
// real command dispatch, real key map, real async loader.
#include "Fakes.h"
#include "TestFramework.h"
#include "core/base/Version.h"

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

KITE_TEST(app, a_root_folder_has_no_parent_row_to_open) {
    Harness h;
    h.files.AddDir("C:\\");
    h.app.OpenPath("C:\\", false);
    h.Settle();
    KITE_EXPECT_EQ(h.tab()->path, std::string("C:\\"));
    KITE_EXPECT_FALSE(h.tab()->hasParentRow());

    h.app.Execute(Cmd::GoUp);
    h.Settle();
    KITE_EXPECT_EQ(h.tab()->path, std::string("C:\\"));
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
    h.app.Execute(Cmd::EditPath);
    KITE_EXPECT_EQ(h.app.prompt().kind, PromptKind::Path);
    KITE_EXPECT_EQ(h.app.prompt().text, std::string("C:\\home"));

    for (char c : std::string("\\beta")) h.app.OnChar(static_cast<uint32_t>(c));
    h.app.OnKey(ParseChord("Enter"));
    h.Settle();
    KITE_EXPECT_EQ(h.tab()->path, std::string("C:\\home\\beta"));
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
    // Caret sits before ".txt" so typing replaces the name only.
    KITE_EXPECT_EQ(h.app.prompt().caret, size_t{ 5 });
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

KITE_TEST(app, cut_marks_the_clipboard_as_a_move) {
    Harness h;
    h.app.Execute(Cmd::Cut);
    KITE_EXPECT(h.shell.clipboardCut);
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
    // still aimed at the folder, and still asks for the background menu.
    Harness h;
    h.app.Execute(Cmd::ExtendedFolderContextMenu);

    KITE_EXPECT(h.shell.lastContextMenuExtended);
    KITE_EXPECT(h.shell.lastContextMenuBackground);
    KITE_EXPECT_EQ(h.shell.lastContextMenuPaths.size(), size_t{ 1 });
    KITE_EXPECT_EQ(h.shell.lastContextMenuPaths.front(), h.tab()->path);
}

KITE_TEST(app, the_folder_menu_asks_for_the_background_menu) {
    // Which menu is asked for decides what the handlers offer. As an *item*, a
    // folder gets verbs that act on it from outside - TortoiseGit's "Git clone",
    // meaning "clone into this one" - which is nonsense for the folder already
    // being viewed, and was showing up inside working copies.
    Harness h;
    h.app.Execute(Cmd::FolderContextMenu);
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
