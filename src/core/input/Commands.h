/// @file
/// @brief コマンド表。
///
/// Kite の利用者から見える操作はすべて Cmd。テキスト入力欄を除き UI は生のキー入力に
/// 一切反応しないので、「どの操作にもキーを割り当てられる」が構造として保証される。
/// キーマップが和音を Cmd に変え、App::Execute が唯一のディスパッチ点になる。
///
/// 操作の追加は KITE_COMMAND_LIST に 1 行と App::Execute に case を 1 つ。

#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

namespace kite {

/// @brief コマンドの分類。ショートカット一覧の見出しに使う。
enum class CmdGroup : uint8_t {
    App,       ///< アプリケーション全体
    Navigate,  ///< 移動
    Select,    ///< カーソルと選択
    Tab,       ///< タブ
    Pane,      ///< ペイン
    Session,   ///< セッション
    View,      ///< 表示
    Bookmark,  ///< ブックマーク
    File,      ///< ファイル操作
    Shell,     ///< シェル連携
    Count      ///< 列挙の終端。有効な分類ではない
};

/// @brief 全コマンドの定義表。
///
/// `X(列挙子, 設定ファイル上の名前, i18n ラベルキー, 分類)` を並べたもの。
/// Cmd 列挙と CommandInfo の表がここから生成されるため、両者がずれることはない。
///
/// @note ラベルキーは core/i18n/Strings.cpp の `kEn` と `kJa` の両方に実体が
///       必要。片方でも欠けると test_strings.cpp が失敗する
#define KITE_COMMAND_LIST(X)                                                                     \
    /* --- application ------------------------------------------------------ */                \
    X(NewWindow,          "app.new_window",          "cmd.new_window",         App)              \
    X(Quit,               "app.quit",                "cmd.quit",               App)              \
    X(ReloadConfig,       "app.reload_config",       "cmd.reload_config",      App)              \
    X(OpenConfigFolder,   "app.open_config_folder",  "cmd.open_config_folder", App)              \
    X(ToggleTheme,        "app.toggle_theme",        "cmd.toggle_theme",       App)              \
    X(ToggleLanguage,     "app.toggle_language",     "cmd.toggle_language",    App)              \
    X(ShowKeyHelp,        "app.key_help",            "cmd.key_help",           App)              \
    X(ShowKeySettings,    "app.key_settings",        "cmd.key_settings",       App)              \
    X(ShowSettings,       "app.settings",            "cmd.settings",           App)              \
    X(ShowCommandPalette, "app.command_palette",     "cmd.command_palette",    App)              \
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
    /* nav.places: 画面が «行き先» に育ったので、名前も分類もそちらに揃えてある。 */               \
    /* 旧名 bookmark.list は CommandFromName() が別名として読み続ける（Commands.cpp）。*/            \
    X(ShowPlaces,         "nav.places",              "cmd.show_places",        Navigate)         \
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
    X(MoveSessionLeft,    "session.move_left",       "cmd.move_session_left",  Session)          \
    X(MoveSessionRight,   "session.move_right",      "cmd.move_session_right", Session)          \
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
    X(FontLarger,         "view.font_larger",        "cmd.font_larger",        View)             \
    X(FontSmaller,        "view.font_smaller",       "cmd.font_smaller",       View)             \
    X(FontReset,          "view.font_reset",         "cmd.font_reset",         View)             \
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
    X(Restore,            "file.restore",            "cmd.restore",            File)             \
    X(DeletePermanent,    "file.delete_permanent",   "cmd.delete_permanent",   File)             \
    X(Copy,               "file.copy",               "cmd.copy",               File)             \
    X(Cut,                "file.cut",                "cmd.cut",                File)             \
    X(Paste,              "file.paste",              "cmd.paste",              File)             \
    X(CopyPath,           "file.copy_path",          "cmd.copy_path",          File)             \
    X(CopyName,           "file.copy_name",          "cmd.copy_name",          File)             \
    X(Undo,               "file.undo",               "cmd.undo",               File)             \
    /* --- shell ------------------------------------------------------------- */               \
    X(ContextMenu,        "shell.context_menu",      "cmd.context_menu",       Shell)            \
    X(ExtendedContextMenu,"shell.context_menu_ext",  "cmd.context_menu_ext",   Shell)            \
    X(FolderContextMenu,  "shell.context_menu_dir",  "cmd.context_menu_dir",   Shell)            \
    X(ExtendedFolderContextMenu, "shell.context_menu_dir_ext", "cmd.context_menu_dir_ext",       \
                                                                              Shell)            \
    X(Properties,         "shell.properties",        "cmd.properties",         Shell)            \
    X(OpenWith,           "shell.open_with",         "cmd.open_with",          Shell)            \
    X(RevealInExplorer,   "shell.reveal",            "cmd.reveal",             Shell)            \
    X(OpenTerminal,       "shell.terminal",          "cmd.terminal",           Shell)            \
    X(ConnectNetwork,     "shell.connect_network",   "cmd.connect_network",    Shell)            \

/// @brief コマンド識別子。KITE_COMMAND_LIST から生成される。
///
/// 先頭の `None` は「割り当て無し」、末尾の `Count` は列挙の終端を表す。
/// その間の列挙子は KITE_COMMAND_LIST の記述順に並ぶ。
enum class Cmd : uint16_t {
    None = 0,
#define KITE_X(id, name, label, group) id,
    KITE_COMMAND_LIST(KITE_X)
#undef KITE_X
    Count
};

/// @brief 1 コマンド分のメタ情報。
struct CommandInfo {
    Cmd id;                ///< コマンド識別子
    const char* name;      ///< keys.ini で使う安定した名前
    const char* labelKey;  ///< i18n の検索キー
    CmdGroup group;        ///< 分類
};

/// @brief 全コマンドのメタ情報を定義順に返す。
/// @return コマンド表への参照。プロセス生存中は有効
const std::vector<CommandInfo>& AllCommands();

/// @brief 識別子からメタ情報を引く。
/// @param[in] id 探すコマンド識別子
/// @return 対応するメタ情報。Cmd::None や範囲外なら nullptr
const CommandInfo* FindCommand(Cmd id);

/// @brief 設定ファイル上の名前からコマンドを引く。
/// @param[in] name keys.ini に書かれる名前。大文字小文字は区別しない
/// @return 対応するコマンド。未知の名前なら Cmd::None
/// @note 名前を変えたコマンドの**旧名も読む**（Commands.cpp の別名表）。既存の
///       keys.ini がその行を持っているので、読まなければ利用者の割り当てが静かに
///       既定へ戻る。書き出すのは常に現在の名前（KeyMap::ToIni）
Cmd CommandFromName(std::string_view name);

/// @brief コマンドの設定ファイル上の名前を返す。
/// @param[in] id 対象のコマンド
/// @return 名前。未知のコマンドでは空文字列
const char* CommandName(Cmd id);

/// @brief 分類の見出しに使う i18n キーを返す。
/// @param[in] g 対象の分類
/// @return i18n の検索キー
const char* GroupLabelKey(CmdGroup g);

}  // namespace kite
