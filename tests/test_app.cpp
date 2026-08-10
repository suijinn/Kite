// End-to-end tests over the controller, driven the way the window drives it:
// real command dispatch, real key map, real async loader.
#include "Fakes.h"
#include "TestFramework.h"

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
    KITE_EXPECT(h.shell.lastContextMenuExtended);
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
