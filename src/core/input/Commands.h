// Kite - the command table.
//
// Every user-visible action in Kite is a Cmd. Nothing in the UI reacts to raw
// key input except text fields, so "rebind anything" comes for free: the key
// map turns a chord into a Cmd and App::Execute is the single dispatch point.
//
// Adding an action = one line in KITE_COMMAND_LIST + a case in App::Execute.
#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

namespace kite {

enum class CmdGroup : uint8_t {
    App, Navigate, Select, Tab, Pane, Session, View, Bookmark, File, Shell, Count
};

// X(enumerator, config name, i18n label key, group)
#define KITE_COMMAND_LIST(X)                                                                     \
    /* --- application ------------------------------------------------------ */                \
    X(Quit,               "app.quit",                "cmd.quit",               App)              \
    X(ReloadConfig,       "app.reload_config",       "cmd.reload_config",      App)              \
    X(OpenConfigFolder,   "app.open_config_folder",  "cmd.open_config_folder", App)              \
    X(ToggleTheme,        "app.toggle_theme",        "cmd.toggle_theme",       App)              \
    X(ToggleLanguage,     "app.toggle_language",     "cmd.toggle_language",    App)              \
    X(ShowKeyHelp,        "app.key_help",            "cmd.key_help",           App)              \
    X(CancelOverlay,      "app.cancel",              "cmd.cancel",             App)              \
    /* --- navigation ------------------------------------------------------- */                \
    X(GoUp,               "nav.up",                  "cmd.go_up",              Navigate)         \
    X(GoBack,             "nav.back",                "cmd.go_back",            Navigate)         \
    X(GoForward,          "nav.forward",             "cmd.go_forward",         Navigate)         \
    X(GoHome,             "nav.home",                "cmd.go_home",            Navigate)         \
    X(GoRoot,             "nav.root",                "cmd.go_root",            Navigate)         \
    X(Refresh,            "nav.refresh",             "cmd.refresh",            Navigate)         \
    X(OpenSelected,       "nav.open",                "cmd.open",               Navigate)         \
    X(OpenInNewTab,       "nav.open_new_tab",        "cmd.open_new_tab",       Navigate)         \
    X(EditPath,           "nav.edit_path",           "cmd.edit_path",          Navigate)         \
    X(FocusFilter,        "nav.filter",              "cmd.filter",             Navigate)         \
    /* --- cursor & selection ------------------------------------------------ */               \
    X(CursorUp,           "sel.up",                  "cmd.cursor_up",          Select)           \
    X(CursorDown,         "sel.down",                "cmd.cursor_down",        Select)           \
    X(CursorPageUp,       "sel.page_up",             "cmd.cursor_page_up",     Select)           \
    X(CursorPageDown,     "sel.page_down",           "cmd.cursor_page_down",   Select)           \
    X(CursorTop,          "sel.top",                 "cmd.cursor_top",         Select)           \
    X(CursorBottom,       "sel.bottom",              "cmd.cursor_bottom",      Select)           \
    X(ExtendUp,           "sel.extend_up",           "cmd.extend_up",          Select)           \
    X(ExtendDown,         "sel.extend_down",         "cmd.extend_down",        Select)           \
    X(ExtendPageUp,       "sel.extend_page_up",      "cmd.extend_page_up",     Select)           \
    X(ExtendPageDown,     "sel.extend_page_down",    "cmd.extend_page_down",   Select)           \
    X(ExtendTop,          "sel.extend_top",          "cmd.extend_top",         Select)           \
    X(ExtendBottom,       "sel.extend_bottom",       "cmd.extend_bottom",      Select)           \
    X(ToggleSelection,    "sel.toggle",              "cmd.toggle_selection",   Select)           \
    X(SelectAll,          "sel.all",                 "cmd.select_all",         Select)           \
    X(SelectNone,         "sel.none",                "cmd.select_none",        Select)           \
    X(InvertSelection,    "sel.invert",              "cmd.invert_selection",   Select)           \
    /* --- tabs -------------------------------------------------------------- */               \
    X(NewTab,             "tab.new",                 "cmd.new_tab",            Tab)              \
    X(CloseTab,           "tab.close",               "cmd.close_tab",          Tab)              \
    X(DuplicateTab,       "tab.duplicate",           "cmd.duplicate_tab",      Tab)              \
    X(ReopenTab,          "tab.reopen",              "cmd.reopen_tab",         Tab)              \
    X(NextTab,            "tab.next",                "cmd.next_tab",           Tab)              \
    X(PrevTab,            "tab.prev",                "cmd.prev_tab",           Tab)              \
    X(MoveTabLeft,        "tab.move_left",           "cmd.move_tab_left",      Tab)              \
    X(MoveTabRight,       "tab.move_right",          "cmd.move_tab_right",     Tab)              \
    X(Tab1,               "tab.goto_1",              "cmd.goto_tab_1",         Tab)              \
    X(Tab2,               "tab.goto_2",              "cmd.goto_tab_2",         Tab)              \
    X(Tab3,               "tab.goto_3",              "cmd.goto_tab_3",         Tab)              \
    X(Tab4,               "tab.goto_4",              "cmd.goto_tab_4",         Tab)              \
    X(Tab5,               "tab.goto_5",              "cmd.goto_tab_5",         Tab)              \
    X(Tab6,               "tab.goto_6",              "cmd.goto_tab_6",         Tab)              \
    X(Tab7,               "tab.goto_7",              "cmd.goto_tab_7",         Tab)              \
    X(Tab8,               "tab.goto_8",              "cmd.goto_tab_8",         Tab)              \
    X(TabLast,            "tab.goto_last",           "cmd.goto_tab_last",      Tab)              \
    /* --- panes ------------------------------------------------------------- */               \
    X(SplitLeftRight,     "pane.split_left_right",   "cmd.split_lr",           Pane)             \
    X(SplitTopBottom,     "pane.split_top_bottom",   "cmd.split_tb",           Pane)             \
    X(ClosePane,          "pane.close",              "cmd.close_pane",         Pane)             \
    X(FocusNextPane,      "pane.next",               "cmd.next_pane",          Pane)             \
    X(FocusPrevPane,      "pane.prev",               "cmd.prev_pane",          Pane)             \
    X(FocusPaneLeft,      "pane.focus_left",         "cmd.focus_pane_left",    Pane)             \
    X(FocusPaneRight,     "pane.focus_right",        "cmd.focus_pane_right",   Pane)             \
    X(FocusPaneUp,        "pane.focus_up",           "cmd.focus_pane_up",      Pane)             \
    X(FocusPaneDown,      "pane.focus_down",         "cmd.focus_pane_down",    Pane)             \
    X(OpenInOtherPane,    "pane.open_in_other",      "cmd.open_in_other",      Pane)             \
    X(SyncOtherPane,      "pane.sync_other",         "cmd.sync_other",         Pane)             \
    X(SwapPanes,          "pane.swap",               "cmd.swap_panes",         Pane)             \
    /* --- sessions ---------------------------------------------------------- */               \
    X(NewSession,         "session.new",             "cmd.new_session",        Session)          \
    X(CloseSession,       "session.close",           "cmd.close_session",      Session)          \
    X(RenameSession,      "session.rename",          "cmd.rename_session",     Session)          \
    X(NextSession,        "session.next",            "cmd.next_session",       Session)          \
    X(PrevSession,        "session.prev",            "cmd.prev_session",       Session)          \
    X(SaveWorkspace,      "session.save",            "cmd.save_workspace",     Session)          \
    X(Session1,           "session.goto_1",          "cmd.goto_session_1",     Session)          \
    X(Session2,           "session.goto_2",          "cmd.goto_session_2",     Session)          \
    X(Session3,           "session.goto_3",          "cmd.goto_session_3",     Session)          \
    X(Session4,           "session.goto_4",          "cmd.goto_session_4",     Session)          \
    X(Session5,           "session.goto_5",          "cmd.goto_session_5",     Session)          \
    X(Session6,           "session.goto_6",          "cmd.goto_session_6",     Session)          \
    X(Session7,           "session.goto_7",          "cmd.goto_session_7",     Session)          \
    X(Session8,           "session.goto_8",          "cmd.goto_session_8",     Session)          \
    /* --- view -------------------------------------------------------------- */               \
    X(ToggleHidden,       "view.toggle_hidden",      "cmd.toggle_hidden",      View)             \
    X(ToggleSidebar,      "view.toggle_sidebar",     "cmd.toggle_sidebar",     View)             \
    X(SortByName,         "view.sort_name",          "cmd.sort_name",          View)             \
    X(SortByExt,          "view.sort_ext",           "cmd.sort_ext",           View)             \
    X(SortBySize,         "view.sort_size",          "cmd.sort_size",          View)             \
    X(SortByDate,         "view.sort_date",          "cmd.sort_date",          View)             \
    X(ToggleSortOrder,    "view.toggle_sort_order",  "cmd.toggle_sort_order",  View)             \
    X(ToggleDirsFirst,    "view.toggle_dirs_first",  "cmd.toggle_dirs_first",  View)             \
    /* --- bookmarks --------------------------------------------------------- */               \
    X(AddBookmark,        "bookmark.add",            "cmd.add_bookmark",       Bookmark)         \
    X(RemoveBookmark,     "bookmark.remove",         "cmd.remove_bookmark",    Bookmark)         \
    X(Bookmark1,          "bookmark.goto_1",         "cmd.goto_bookmark_1",    Bookmark)         \
    X(Bookmark2,          "bookmark.goto_2",         "cmd.goto_bookmark_2",    Bookmark)         \
    X(Bookmark3,          "bookmark.goto_3",         "cmd.goto_bookmark_3",    Bookmark)         \
    X(Bookmark4,          "bookmark.goto_4",         "cmd.goto_bookmark_4",    Bookmark)         \
    X(Bookmark5,          "bookmark.goto_5",         "cmd.goto_bookmark_5",    Bookmark)         \
    X(Bookmark6,          "bookmark.goto_6",         "cmd.goto_bookmark_6",    Bookmark)         \
    X(Bookmark7,          "bookmark.goto_7",         "cmd.goto_bookmark_7",    Bookmark)         \
    X(Bookmark8,          "bookmark.goto_8",         "cmd.goto_bookmark_8",    Bookmark)         \
    /* --- file operations --------------------------------------------------- */               \
    X(NewFolder,          "file.new_folder",         "cmd.new_folder",         File)             \
    X(NewFile,            "file.new_file",           "cmd.new_file",           File)             \
    X(Rename,             "file.rename",             "cmd.rename",             File)             \
    X(DeleteToRecycle,    "file.delete",             "cmd.delete",             File)             \
    X(DeletePermanent,    "file.delete_permanent",   "cmd.delete_permanent",   File)             \
    X(Copy,               "file.copy",               "cmd.copy",               File)             \
    X(Cut,                "file.cut",                "cmd.cut",                File)             \
    X(Paste,              "file.paste",              "cmd.paste",              File)             \
    X(CopyPath,           "file.copy_path",          "cmd.copy_path",          File)             \
    X(CopyName,           "file.copy_name",          "cmd.copy_name",          File)             \
    /* --- shell ------------------------------------------------------------- */               \
    X(ContextMenu,        "shell.context_menu",      "cmd.context_menu",       Shell)            \
    X(ExtendedContextMenu,"shell.context_menu_ext",  "cmd.context_menu_ext",   Shell)            \
    X(Properties,         "shell.properties",        "cmd.properties",         Shell)            \
    X(OpenWith,           "shell.open_with",         "cmd.open_with",          Shell)            \
    X(RevealInExplorer,   "shell.reveal",            "cmd.reveal",             Shell)            \
    X(OpenTerminal,       "shell.terminal",          "cmd.terminal",           Shell)

enum class Cmd : uint16_t {
    None = 0,
#define KITE_X(id, name, label, group) id,
    KITE_COMMAND_LIST(KITE_X)
#undef KITE_X
    Count
};

struct CommandInfo {
    Cmd id;
    const char* name;      // stable identifier used in keys.ini
    const char* labelKey;  // i18n lookup key
    CmdGroup group;
};

const std::vector<CommandInfo>& AllCommands();
const CommandInfo* FindCommand(Cmd id);
Cmd CommandFromName(std::string_view name);
const char* CommandName(Cmd id);
const char* GroupLabelKey(CmdGroup g);

}  // namespace kite
