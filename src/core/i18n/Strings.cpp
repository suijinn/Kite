#include "core/i18n/Strings.h"

#include <cctype>

namespace kite {
namespace {

struct Row {
    const char* key;
    const char* text;
};

const Row kEn[] = {
    // --- command groups ---
    { "group.app", "Application" },
    { "group.navigate", "Navigation" },
    { "group.select", "Cursor & Selection" },
    { "group.tab", "Tabs" },
    { "group.pane", "Panes" },
    { "group.session", "Sessions" },
    { "group.view", "View" },
    { "group.bookmark", "Bookmarks" },
    { "group.file", "File operations" },
    { "group.shell", "Shell" },
    { "group.other", "Other" },

    // --- commands ---
    { "cmd.quit", "Quit" },
    { "cmd.reload_config", "Reload configuration" },
    { "cmd.open_config_folder", "Open configuration folder" },
    { "cmd.toggle_theme", "Toggle dark / light theme" },
    { "cmd.toggle_language", "Switch language" },
    { "cmd.key_help", "Keyboard shortcuts" },
    { "cmd.key_settings", "Edit keyboard shortcuts" },
    { "cmd.cancel", "Cancel / close overlay" },

    { "cmd.go_up", "Go to parent folder" },
    { "cmd.go_back", "Back" },
    { "cmd.go_forward", "Forward" },
    { "cmd.go_home", "Home folder" },
    { "cmd.go_root", "Drive root" },
    { "cmd.refresh", "Refresh" },
    { "cmd.open", "Open" },
    { "cmd.open_new_tab", "Open in new tab" },
    { "cmd.edit_path", "Edit path" },
    { "cmd.filter", "Filter" },

    { "cmd.cursor_up", "Cursor up" },
    { "cmd.cursor_down", "Cursor down" },
    { "cmd.cursor_page_up", "Cursor page up" },
    { "cmd.cursor_page_down", "Cursor page down" },
    { "cmd.cursor_top", "Cursor to first item" },
    { "cmd.cursor_bottom", "Cursor to last item" },
    { "cmd.extend_up", "Extend selection up" },
    { "cmd.extend_down", "Extend selection down" },
    { "cmd.extend_page_up", "Extend selection page up" },
    { "cmd.extend_page_down", "Extend selection page down" },
    { "cmd.extend_top", "Extend selection to top" },
    { "cmd.extend_bottom", "Extend selection to bottom" },
    { "cmd.toggle_selection", "Toggle selection" },
    { "cmd.select_all", "Select all" },
    { "cmd.select_none", "Clear selection" },
    { "cmd.invert_selection", "Invert selection" },

    { "cmd.new_tab", "New tab" },
    { "cmd.close_tab", "Close tab" },
    { "cmd.duplicate_tab", "Duplicate tab" },
    { "cmd.reopen_tab", "Reopen closed tab" },
    { "cmd.next_tab", "Next tab" },
    { "cmd.prev_tab", "Previous tab" },
    { "cmd.move_tab_left", "Move tab left" },
    { "cmd.move_tab_right", "Move tab right" },
    { "cmd.goto_tab_n", "Go to tab {n}" },
    { "cmd.goto_tab_last", "Go to last tab" },

    { "cmd.split_lr", "Split left / right" },
    { "cmd.split_tb", "Split top / bottom" },
    { "cmd.close_pane", "Close pane" },
    { "cmd.next_pane", "Next pane" },
    { "cmd.prev_pane", "Previous pane" },
    { "cmd.focus_pane_left", "Focus pane to the left" },
    { "cmd.focus_pane_right", "Focus pane to the right" },
    { "cmd.focus_pane_up", "Focus pane above" },
    { "cmd.focus_pane_down", "Focus pane below" },
    { "cmd.open_in_other", "Open in other pane" },
    { "cmd.sync_other", "Sync other pane to this folder" },
    { "cmd.swap_panes", "Swap panes" },

    { "cmd.new_session", "New session" },
    { "cmd.close_session", "Close session" },
    { "cmd.rename_session", "Rename session" },
    { "cmd.next_session", "Next session" },
    { "cmd.prev_session", "Previous session" },
    { "cmd.save_workspace", "Save workspace" },
    { "cmd.goto_session_n", "Go to session {n}" },

    { "cmd.toggle_hidden", "Show hidden files" },
    { "cmd.toggle_sidebar", "Toggle sidebar" },
    { "cmd.sort_name", "Sort by name" },
    { "cmd.sort_ext", "Sort by extension" },
    { "cmd.sort_size", "Sort by size" },
    { "cmd.sort_date", "Sort by date" },
    { "cmd.toggle_sort_order", "Reverse sort order" },
    { "cmd.toggle_dirs_first", "Folders first" },

    { "cmd.add_bookmark", "Add bookmark" },
    { "cmd.remove_bookmark", "Remove bookmark" },
    { "cmd.goto_bookmark_n", "Go to bookmark {n}" },

    { "cmd.new_folder", "New folder" },
    { "cmd.new_file", "New file" },
    { "cmd.rename", "Rename" },
    { "cmd.delete", "Delete (Recycle Bin)" },
    { "cmd.delete_permanent", "Delete permanently" },
    { "cmd.copy", "Copy" },
    { "cmd.cut", "Cut" },
    { "cmd.paste", "Paste" },
    { "cmd.copy_path", "Copy full path" },
    { "cmd.copy_name", "Copy name" },

    { "cmd.context_menu", "Context menu" },
    { "cmd.context_menu_ext", "Extended context menu" },
    { "cmd.properties", "Properties" },
    { "cmd.open_with", "Open with..." },
    { "cmd.reveal", "Show in Explorer" },
    { "cmd.terminal", "Open terminal here" },

    // --- interface ---
    { "ui.name", "Name" },
    { "ui.ext", "Ext" },
    { "ui.size", "Size" },
    { "ui.modified", "Modified" },
    { "ui.quick_access", "Quick access" },
    { "ui.bookmarks", "Bookmarks" },
    { "ui.drives", "Drives" },
    { "ui.loading", "Loading..." },
    { "ui.empty", "This folder is empty" },
    { "ui.no_match", "No item matches the filter" },
    { "ui.err_denied", "Access denied" },
    { "ui.err_missing", "Folder not found" },
    { "ui.err_unavailable", "Location unavailable" },
    { "ui.err_generic", "Could not read this folder" },
    { "ui.status_items", "{0} items" },
    { "ui.status_selected", "{0} selected ({1})" },
    { "ui.filter_label", "Filter:" },
    { "ui.path_label", "Path:" },
    { "ui.rename_label", "Rename:" },
    { "ui.new_folder_label", "New folder name:" },
    { "ui.new_file_label", "New file name:" },
    { "ui.session_name_label", "Session name:" },
    { "ui.confirm_delete", "Delete {0} item(s)? Enter = yes, Esc = no" },
    { "ui.confirm_delete_perm", "PERMANENTLY delete {0} item(s)? Enter = yes, Esc = no" },
    { "ui.key_help_title", "Keyboard shortcuts" },
    { "ui.key_help_hint", "Ctrl+F1 to rebind, or edit keys.ini. Esc closes." },
    { "ui.key_settings_title", "Keyboard shortcut settings" },
    { "ui.key_settings_hint",
      "Enter: replace   Insert: add   Delete: clear   Ctrl+R: default   Esc: close" },
    { "ui.key_settings_hint_capture", "Press the new key combination.   Esc: cancel" },
    { "ui.key_settings_capture", "press a key..." },
    { "ui.key_settings_filter", "Search: {0}" },
    { "ui.key_settings_search_hint", "Type to search" },
    { "ui.key_settings_none", "(none)" },
    { "ui.key_settings_count", "{0} commands" },
    { "ui.key_settings_assigned", "Assigned {0}" },
    { "ui.key_settings_taken", "{0} taken from \"{1}\"" },
    { "ui.key_settings_cleared", "Cleared every key for \"{0}\"" },
    { "ui.key_settings_reset", "\"{0}\" restored to its default" },
    { "ui.key_settings_reset_all", "Every shortcut restored to its default" },
    { "ui.key_settings_cancelled", "Left unchanged" },
    { "ui.key_settings_saved", "Shortcuts saved to keys.ini" },
    { "ui.new_session", "Session {0}" },
    { "ui.dir_marker", "<DIR>" },
    { "ui.cloud_marker", "cloud" },
    { "ui.copied", "Copied to clipboard" },
    { "ui.clipboard_empty", "Clipboard holds no files" },
    { "ui.done", "Done" },
    { "ui.saved", "Workspace saved" },
    { "ui.config_reloaded", "Configuration reloaded" },
    { "ui.no_selection", "Nothing selected" },
    { "ui.cannot_close_last", "The last pane cannot be closed" },
    { "ui.tab_placeholder", "New tab" },
    { "ui.drop_invalid", "Cannot drop here" },
    { "ui.drop_copy", "Copy to {0}" },
    { "ui.drop_move", "Move to {0}" },
    { "ui.watching_off", "Auto-refresh unavailable for this location" },
    { "ui.shell_menu_failed", "The shell menu could not be shown" },
};

const Row kJa[] = {
    { "group.app", "アプリケーション" },
    { "group.navigate", "移動" },
    { "group.select", "カーソル・選択" },
    { "group.tab", "タブ" },
    { "group.pane", "ペイン" },
    { "group.session", "セッション" },
    { "group.view", "表示" },
    { "group.bookmark", "ブックマーク" },
    { "group.file", "ファイル操作" },
    { "group.shell", "シェル連携" },
    { "group.other", "その他" },

    { "cmd.quit", "終了" },
    { "cmd.reload_config", "設定を再読み込み" },
    { "cmd.open_config_folder", "設定フォルダを開く" },
    { "cmd.toggle_theme", "ダーク／ライト切り替え" },
    { "cmd.toggle_language", "言語を切り替え" },
    { "cmd.key_help", "ショートカット一覧" },
    { "cmd.key_settings", "ショートカットキーの設定" },
    { "cmd.cancel", "キャンセル／オーバーレイを閉じる" },

    { "cmd.go_up", "親フォルダへ" },
    { "cmd.go_back", "戻る" },
    { "cmd.go_forward", "進む" },
    { "cmd.go_home", "ホームフォルダ" },
    { "cmd.go_root", "ドライブのルート" },
    { "cmd.refresh", "再読み込み" },
    { "cmd.open", "開く" },
    { "cmd.open_new_tab", "新しいタブで開く" },
    { "cmd.edit_path", "パスを編集" },
    { "cmd.filter", "絞り込み" },

    { "cmd.cursor_up", "カーソルを上へ" },
    { "cmd.cursor_down", "カーソルを下へ" },
    { "cmd.cursor_page_up", "カーソルを1画面上へ" },
    { "cmd.cursor_page_down", "カーソルを1画面下へ" },
    { "cmd.cursor_top", "先頭へ" },
    { "cmd.cursor_bottom", "末尾へ" },
    { "cmd.extend_up", "上へ選択を拡張" },
    { "cmd.extend_down", "下へ選択を拡張" },
    { "cmd.extend_page_up", "1画面上へ選択を拡張" },
    { "cmd.extend_page_down", "1画面下へ選択を拡張" },
    { "cmd.extend_top", "先頭まで選択を拡張" },
    { "cmd.extend_bottom", "末尾まで選択を拡張" },
    { "cmd.toggle_selection", "選択を反転（1件）" },
    { "cmd.select_all", "すべて選択" },
    { "cmd.select_none", "選択を解除" },
    { "cmd.invert_selection", "選択を反転" },

    { "cmd.new_tab", "新しいタブ" },
    { "cmd.close_tab", "タブを閉じる" },
    { "cmd.duplicate_tab", "タブを複製" },
    { "cmd.reopen_tab", "閉じたタブを復元" },
    { "cmd.next_tab", "次のタブ" },
    { "cmd.prev_tab", "前のタブ" },
    { "cmd.move_tab_left", "タブを左へ移動" },
    { "cmd.move_tab_right", "タブを右へ移動" },
    { "cmd.goto_tab_n", "タブ {n} へ" },
    { "cmd.goto_tab_last", "最後のタブへ" },

    { "cmd.split_lr", "左右に分割（縦分割）" },
    { "cmd.split_tb", "上下に分割（横分割）" },
    { "cmd.close_pane", "ペインを閉じる" },
    { "cmd.next_pane", "次のペイン" },
    { "cmd.prev_pane", "前のペイン" },
    { "cmd.focus_pane_left", "左のペインへ" },
    { "cmd.focus_pane_right", "右のペインへ" },
    { "cmd.focus_pane_up", "上のペインへ" },
    { "cmd.focus_pane_down", "下のペインへ" },
    { "cmd.open_in_other", "反対側のペインで開く" },
    { "cmd.sync_other", "反対側のペインを同じ場所に" },
    { "cmd.swap_panes", "ペインを入れ替え" },

    { "cmd.new_session", "新しいセッション" },
    { "cmd.close_session", "セッションを閉じる" },
    { "cmd.rename_session", "セッション名を変更" },
    { "cmd.next_session", "次のセッション" },
    { "cmd.prev_session", "前のセッション" },
    { "cmd.save_workspace", "ワークスペースを保存" },
    { "cmd.goto_session_n", "セッション {n} へ" },

    { "cmd.toggle_hidden", "隠しファイルを表示" },
    { "cmd.toggle_sidebar", "サイドバーの表示切り替え" },
    { "cmd.sort_name", "名前順" },
    { "cmd.sort_ext", "拡張子順" },
    { "cmd.sort_size", "サイズ順" },
    { "cmd.sort_date", "更新日時順" },
    { "cmd.toggle_sort_order", "昇順／降順を反転" },
    { "cmd.toggle_dirs_first", "フォルダを先頭に" },

    { "cmd.add_bookmark", "ブックマークに追加" },
    { "cmd.remove_bookmark", "ブックマークから削除" },
    { "cmd.goto_bookmark_n", "ブックマーク {n} へ" },

    { "cmd.new_folder", "新しいフォルダ" },
    { "cmd.new_file", "新しいファイル" },
    { "cmd.rename", "名前の変更" },
    { "cmd.delete", "削除（ごみ箱へ）" },
    { "cmd.delete_permanent", "完全に削除" },
    { "cmd.copy", "コピー" },
    { "cmd.cut", "切り取り" },
    { "cmd.paste", "貼り付け" },
    { "cmd.copy_path", "フルパスをコピー" },
    { "cmd.copy_name", "名前をコピー" },

    { "cmd.context_menu", "コンテキストメニュー" },
    { "cmd.context_menu_ext", "拡張コンテキストメニュー" },
    { "cmd.properties", "プロパティ" },
    { "cmd.open_with", "プログラムから開く" },
    { "cmd.reveal", "エクスプローラーで表示" },
    { "cmd.terminal", "ここでターミナルを開く" },

    { "ui.name", "名前" },
    { "ui.ext", "拡張子" },
    { "ui.size", "サイズ" },
    { "ui.modified", "更新日時" },
    { "ui.quick_access", "クイックアクセス" },
    { "ui.bookmarks", "ブックマーク" },
    { "ui.drives", "ドライブ" },
    { "ui.loading", "読み込み中..." },
    { "ui.empty", "このフォルダは空です" },
    { "ui.no_match", "条件に一致する項目がありません" },
    { "ui.err_denied", "アクセスが拒否されました" },
    { "ui.err_missing", "フォルダが見つかりません" },
    { "ui.err_unavailable", "この場所は利用できません" },
    { "ui.err_generic", "このフォルダを読み取れませんでした" },
    { "ui.status_items", "{0} 項目" },
    { "ui.status_selected", "{0} 件選択 ({1})" },
    { "ui.filter_label", "絞り込み:" },
    { "ui.path_label", "パス:" },
    { "ui.rename_label", "名前の変更:" },
    { "ui.new_folder_label", "新しいフォルダ名:" },
    { "ui.new_file_label", "新しいファイル名:" },
    { "ui.session_name_label", "セッション名:" },
    { "ui.confirm_delete", "{0} 件を削除しますか？ Enter=はい / Esc=いいえ" },
    { "ui.confirm_delete_perm", "{0} 件を完全に削除しますか？ Enter=はい / Esc=いいえ" },
    { "ui.key_help_title", "キーボードショートカット" },
    { "ui.key_help_hint", "Ctrl+F1 または keys.ini で変更できます。Esc で閉じます。" },
    { "ui.key_settings_title", "ショートカットキーの設定" },
    { "ui.key_settings_hint",
      "Enter: 変更   Insert: 追加   Delete: 解除   Ctrl+R: 既定に戻す   Esc: 閉じる" },
    { "ui.key_settings_hint_capture", "新しいキーを押してください。   Esc: 中止" },
    { "ui.key_settings_capture", "キーを押す..." },
    { "ui.key_settings_filter", "検索: {0}" },
    { "ui.key_settings_search_hint", "文字を入力すると絞り込めます" },
    { "ui.key_settings_none", "（なし）" },
    { "ui.key_settings_count", "{0} コマンド" },
    { "ui.key_settings_assigned", "{0} を割り当てました" },
    { "ui.key_settings_taken", "{0} を「{1}」から移しました" },
    { "ui.key_settings_cleared", "「{0}」の割り当てをすべて解除しました" },
    { "ui.key_settings_reset", "「{0}」を既定の割り当てに戻しました" },
    { "ui.key_settings_reset_all", "すべてのショートカットを既定に戻しました" },
    { "ui.key_settings_cancelled", "変更しませんでした" },
    { "ui.key_settings_saved", "keys.ini に保存しました" },
    { "ui.new_session", "セッション {0}" },
    { "ui.dir_marker", "<DIR>" },
    { "ui.cloud_marker", "クラウド" },
    { "ui.copied", "クリップボードにコピーしました" },
    { "ui.clipboard_empty", "クリップボードにファイルがありません" },
    { "ui.done", "完了" },
    { "ui.saved", "ワークスペースを保存しました" },
    { "ui.config_reloaded", "設定を再読み込みしました" },
    { "ui.no_selection", "選択されていません" },
    { "ui.cannot_close_last", "最後のペインは閉じられません" },
    { "ui.tab_placeholder", "新しいタブ" },
    { "ui.drop_invalid", "ここにはドロップできません" },
    { "ui.drop_copy", "{0} にコピー" },
    { "ui.drop_move", "{0} に移動" },
    { "ui.watching_off", "この場所は自動更新できません" },
    { "ui.shell_menu_failed", "シェルメニューを表示できませんでした" },
};

}  // namespace

void Strings::Load(std::string_view code) {
    map_.clear();
    // English is always loaded first so a partial translation still renders.
    for (const Row& r : kEn) map_[r.key] = r.text;

    if (code == "ja") {
        code_ = "ja";
        for (const Row& r : kJa) map_[r.key] = r.text;
    } else {
        code_ = "en";
    }
}

void Strings::ApplyOverrides(const Ini& ini) {
    if (const Ini::Section* sec = ini.Find("strings")) {
        for (const Ini::Entry& e : sec->entries) map_[e.key] = e.value;
    }
}

const std::string& Strings::Get(std::string_view key) const {
    auto it = map_.find(std::string(key));
    if (it != map_.end()) return it->second;

    // Cache the key itself so we can return a stable reference.
    static thread_local std::string fallback;
    fallback.assign(key);
    return fallback;
}

std::string Strings::Label(std::string_view key) const {
    auto it = map_.find(std::string(key));
    if (it != map_.end()) return it->second;

    // "cmd.goto_tab_3" -> "cmd.goto_tab_n" with {n} = 3
    if (key.size() >= 2 && std::isdigit(static_cast<unsigned char>(key.back())) &&
        key[key.size() - 2] == '_') {
        std::string generic(key.substr(0, key.size() - 1));
        generic += 'n';
        auto g = map_.find(generic);
        if (g != map_.end()) {
            std::string out = g->second;
            const size_t at = out.find("{n}");
            if (at != std::string::npos) out.replace(at, 3, key.substr(key.size() - 1));
            return out;
        }
    }
    return std::string(key);
}

std::string Strings::Format(std::string_view key,
                            std::initializer_list<std::string_view> args) const {
    std::string out = Get(key);
    int index = 0;
    for (std::string_view a : args) {
        const std::string token = "{" + std::to_string(index++) + "}";
        for (size_t at = out.find(token); at != std::string::npos; at = out.find(token, at)) {
            out.replace(at, token.size(), a);
            at += a.size();
        }
    }
    return out;
}

std::vector<std::string> Strings::AvailableCodes() { return { "en", "ja" }; }

}  // namespace kite
