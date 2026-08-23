// App::Execute — the single dispatch point.
//
// Split out of App.cpp only because of its size: one case per command means the
// table grows with every feature, and at 100+ entries it was more than half the
// file. Nothing else lives here, so "which file is a command run from" has one
// answer.

#include <algorithm>

#include "core/app/App.h"
#include "core/base/PathUtil.h"

namespace kite {
namespace {

// Text-size step. The same 0.1 the settings screen offers, so the two ways of
// changing the size cannot drift apart; the range itself (kFontScaleMin /
// kFontScaleMax) lives with SetFontScale in AppConfig.cpp.
constexpr float kFontScaleStep = 0.1f;

}  // namespace

void App::Execute(Cmd cmd) {
    Session* session = workspace_.activeSession();
    Pane* pane = workspace_.focusedPane();
    Tab* tab = workspace_.focusedTab();

    switch (cmd) {
        // --- application -----------------------------------------------------
        case Cmd::NewWindow:
            // 表示中のフォルダを引き継ぐ。開けなかったことは黙って捨てない
            // ─ 何も起きないのと、増えないのとは別の話。
            if (!host_.OpenNewWindow(tab ? tab->path : std::string())) {
                SetStatus(strings_.Get("ui.new_window_failed"));
            }
            break;
        case Cmd::Quit:
            host_.Close();
            break;
        case Cmd::ReloadConfig: {
            LoadConfig();
            SetStatus(strings_.Get("ui.config_reloaded"));
            host_.Invalidate();
            break;
        }
        case Cmd::OpenConfigFolder:
            shell_.Open({}, fs_.ConfigDir());
            break;
        case Cmd::ToggleTheme:
            darkTheme_ = !darkTheme_;
            ApplyTheme();
            dirty_ = true;
            host_.Invalidate();
            break;
        case Cmd::ToggleLanguage: {
            language_ = (strings_.code() == "ja") ? "en" : "ja";
            LoadLanguage();
            dirty_ = true;
            host_.Invalidate();
            break;
        }
        case Cmd::ShowKeyHelp: {
            const bool show = !keyHelp_;
            // The read-only sheet and the editor show the same table; two of them
            // on screen at once would only be confusing - and the same goes for
            // every other overlay, since they all swallow the keyboard whole.
            CloseAllOverlays();
            keyHelp_ = show;
            host_.Invalidate();
            break;
        }
        case Cmd::ShowKeySettings:
            if (keyEditor_.visible()) {
                CloseKeyEditor();
            } else {
                CloseAllOverlays();
                keyEditor_.Open(strings_, keymap_);
            }
            host_.Invalidate();
            break;
        case Cmd::ShowSettings:
            if (settingsEditor_.visible()) {
                settingsEditor_.Close();
            } else {
                CloseAllOverlays();
                // 開くたびに現在値から作り直す。設定は画面の外（Ctrl+Shift+M の
                // テーマ切り替え、Ctrl++ の文字サイズ）からも変わる。
                settingsEditor_.Open(strings_, CollectSettings());
                if (standalone_) SetStatus(strings_.Get("ui.settings_no_save"));
            }
            host_.Invalidate();
            break;
        case Cmd::ShowCommandPalette:
            if (commandPalette_.visible()) {
                commandPalette_.Close();
            } else {
                OpenCommandPalette();
            }
            host_.Invalidate();
            break;
        case Cmd::CancelOverlay:
            if (commandPalette_.visible()) {
                commandPalette_.Close();
            } else if (placePicker_.visible()) {
                placePicker_.Close();
            } else if (settingsEditor_.visible()) {
                settingsEditor_.Close();
            } else if (keyEditor_.visible()) {
                CloseKeyEditor();
            } else if (keyHelp_) {
                keyHelp_ = false;
            } else if (prompt_.active()) {
                CancelPrompt();
            } else if (tab) {
                tab->ClearMarks();
                if (!tab->filter.empty()) {
                    tab->filter.clear();
                    tab->Rebuild();
                }
            }
            host_.Invalidate();
            break;

        // --- navigation ------------------------------------------------------
        case Cmd::GoUp:
            NavigateToParent(false);
            break;
        case Cmd::GoBack:
        case Cmd::GoForward: {
            if (!tab) break;
            // The two stacks trade places; nothing else about the move differs.
            const bool back = (cmd == Cmd::GoBack);
            std::vector<std::string>& from = back ? tab->back : tab->forward;
            std::vector<std::string>& to = back ? tab->forward : tab->back;
            if (from.empty()) break;
            const std::string target = from.back();
            from.pop_back();
            to.push_back(tab->path);
            RetargetTab(*tab, target);
            host_.Invalidate();
            break;
        }
        case Cmd::GoHome:
            NavigateFocused(fs_.HomeDir());
            break;
        case Cmd::GoRoot: {
            if (!tab) break;
            std::string p = tab->path;
            for (std::string up = path::Parent(p); !up.empty(); up = path::Parent(p)) p = up;
            NavigateFocused(p);
            break;
        }
        case Cmd::Refresh:
            RefreshRoots();
            RefreshFocused();
            break;
        case Cmd::OpenSelected:
            if (tab) ActivateEntry(tab->cursor, false);
            break;
        case Cmd::OpenInNewTab:
            if (tab) ActivateEntry(tab->cursor, true);
            break;
        case Cmd::EditPath:
            // No label: the bar this takes over already showed the path as
            // breadcrumbs, so a word in front of it only takes room from it.
            //
            // Selected whole, the way every address bar on the desktop opens:
            // the usual next move is to paste or type a different path over it,
            // and the wash is also what says the row stopped being breadcrumbs.
            if (tab) {
                BeginPrompt(PromptKind::Path, "", EditablePath(*tab));
                prompt_.SelectAll();
            }
            break;
        case Cmd::FocusFilter:
            if (tab) BeginPrompt(PromptKind::Filter, "ui.filter_label", tab->filter);
            break;

        // --- cursor and selection --------------------------------------------
        case Cmd::CursorUp:        MoveCursor(-1, false); break;
        case Cmd::CursorDown:      MoveCursor(1, false); break;
        case Cmd::CursorPageUp:    MoveCursor(-(pane ? pane->rowsPerPage : 10), false); break;
        case Cmd::CursorPageDown:  MoveCursor(pane ? pane->rowsPerPage : 10, false); break;
        case Cmd::CursorTop:       MoveCursor(0, false, true); break;
        case Cmd::CursorBottom:
            MoveCursor(tab ? static_cast<int>(tab->visible.size()) - 1 : 0, false, true);
            break;
        case Cmd::ExtendUp:        MoveCursor(-1, true); break;
        case Cmd::ExtendDown:      MoveCursor(1, true); break;
        case Cmd::ExtendPageUp:    MoveCursor(-(pane ? pane->rowsPerPage : 10), true); break;
        case Cmd::ExtendPageDown:  MoveCursor(pane ? pane->rowsPerPage : 10, true); break;
        case Cmd::ExtendTop:       MoveCursor(0, true, true); break;
        case Cmd::ExtendBottom:
            MoveCursor(tab ? static_cast<int>(tab->visible.size()) - 1 : 0, true, true);
            break;
        case Cmd::ToggleSelection: {
            if (!tab || tab->visible.empty()) break;
            // Deliberately does not go through MoveCursor: toggling must
            // accumulate a multi-selection rather than reset it.
            const int index = tab->visible[tab->cursor];
            if (index >= 0) tab->marked[index] = tab->marked[index] ? 0 : 1;
            if (tab->cursor + 1 < static_cast<int>(tab->visible.size())) tab->cursor++;
            tab->ResetAnchor();
            EnsureCursorVisible();
            host_.Invalidate();
            break;
        }
        case Cmd::SelectAll:
            if (tab) {
                tab->MarkRange(0, static_cast<int>(tab->visible.size()) - 1, true);
                // Whatever range was being dragged out with Shift is over; the
                // next extension has to build on what is on screen now.
                tab->ResetAnchor();
                host_.Invalidate();
            }
            break;
        case Cmd::SelectNone:
            // Escape lands here, and in Explorer that is also how a cut is
            // called off. Nothing can un-cut the clipboard from outside it, but
            // the screen can at least stop claiming those rows are going
            // anywhere.
            ClearCutMarks();
            if (tab) tab->ClearMarks();
            host_.Invalidate();
            break;
        case Cmd::InvertSelection:
            if (tab) {
                for (int index : tab->visible) {
                    if (index < 0) continue;
                    tab->marked[index] = tab->marked[index] ? 0 : 1;
                }
                tab->ResetAnchor();
                host_.Invalidate();
            }
            break;

        // --- tabs ------------------------------------------------------------
        case Cmd::NewTab: {
            if (!pane) break;
            OpenTabIn(*pane, tab ? tab->path : fs_.HomeDir(), NewTabAt(*pane), defaultView_);
            dirty_ = true;
            host_.Invalidate();
            break;
        }
        case Cmd::CloseTab: {
            if (!pane) break;
            // A pane always keeps one tab (Pane::CloseTab refuses), so the last
            // one has to be answered here or the key stops answering at all.
            //
            // Split, the answer is the pane: closing the window because one of
            // several panes ran out of tabs throws away everything the other
            // panes were showing. Unsplit, it is the window, the way every
            // browser reads Ctrl+W - and that is not destructive, since the
            // workspace is written on the way out and comes back next start.
            //
            // Only the last step is skipped: the session keeps its own key
            // (Ctrl+Alt+W), because a session holds panes that are not on
            // screen and closing them by running one pane empty is a surprise.
            if (pane->tabs.size() <= 1) {
                if (session && session->Panes().size() > 1) {
                    // Reopenable, like any other tab closed with this key.
                    if (Tab* t = pane->activeTab()) workspace_.closedTabs.push_back(t->path);
                    session->ClosePane(pane);
                    dirty_ = true;
                    host_.Invalidate();
                    break;
                }
                host_.Close();
                break;
            }
            std::string closed;
            if (pane->CloseTab(pane->active, &closed)) {
                workspace_.closedTabs.push_back(closed);
                if (Tab* t = pane->activeTab()) RequestLoad(*t);
                dirty_ = true;
            }
            host_.Invalidate();
            break;
        }
        case Cmd::DuplicateTab: {
            if (!pane || !tab) break;
            // Always next to the original, whatever [ui] new_tab_position says:
            // that is not a placement preference, it is what duplicating means.
            OpenTabIn(*pane, tab->path, pane->active + 1, tab->view);
            dirty_ = true;
            host_.Invalidate();
            break;
        }
        case Cmd::ReopenTab: {
            if (!pane || workspace_.closedTabs.empty()) break;
            const std::string p = workspace_.closedTabs.back();
            workspace_.closedTabs.pop_back();
            OpenTabIn(*pane, p, NewTabAt(*pane), defaultView_);
            host_.Invalidate();
            break;
        }
        case Cmd::NextTab:
            if (pane && !pane->tabs.empty()) {
                GotoTab((pane->active + 1) % static_cast<int>(pane->tabs.size()));
            }
            break;
        case Cmd::PrevTab:
            if (pane && !pane->tabs.empty()) {
                const int n = static_cast<int>(pane->tabs.size());
                GotoTab((pane->active - 1 + n) % n);
            }
            break;
        // Stops at the ends rather than wrapping, the way MoveSessionLeft/Right
        // do: this is a direction along the bar, not "show me another one".
        case Cmd::MoveTabLeft:
        case Cmd::MoveTabRight: {
            if (!pane) break;
            const int to = pane->active + (cmd == Cmd::MoveTabLeft ? -1 : 1);
            if (to < 0 || to >= static_cast<int>(pane->tabs.size())) break;
            std::swap(pane->tabs[pane->active], pane->tabs[to]);
            pane->active = to;
            dirty_ = true;
            host_.Invalidate();
            break;
        }
        case Cmd::Tab1: GotoTab(0); break;
        case Cmd::Tab2: GotoTab(1); break;
        case Cmd::Tab3: GotoTab(2); break;
        case Cmd::Tab4: GotoTab(3); break;
        case Cmd::Tab5: GotoTab(4); break;
        case Cmd::Tab6: GotoTab(5); break;
        case Cmd::Tab7: GotoTab(6); break;
        case Cmd::Tab8: GotoTab(7); break;
        case Cmd::TabLast: GotoTab(-1); break;

        // --- panes -----------------------------------------------------------
        case Cmd::SplitLeftRight:
        case Cmd::SplitTopBottom: {
            if (!session || !pane) break;
            const SplitNode::Kind kind = (cmd == Cmd::SplitLeftRight)
                                             ? SplitNode::Kind::LeftRight
                                             : SplitNode::Kind::TopBottom;
            if (Pane* created = session->Split(pane, kind)) {
                if (Tab* t = created->activeTab()) {
                    t->view = tab ? tab->view : defaultView_;
                    RequestLoad(*t, true);
                }
                session->focus = created;
                dirty_ = true;
            }
            host_.Invalidate();
            break;
        }
        case Cmd::ClosePane:
            if (session && pane) {
                if (!session->ClosePane(pane)) {
                    SetStatus(strings_.Get("ui.cannot_close_last"));
                } else {
                    dirty_ = true;
                }
                host_.Invalidate();
            }
            break;
        case Cmd::FocusNextPane:
        case Cmd::FocusPrevPane: {
            if (!session) break;
            std::vector<Pane*> panes = session->Panes();
            if (panes.size() < 2) break;
            int index = 0;
            for (size_t i = 0; i < panes.size(); ++i) {
                if (panes[i] == pane) index = static_cast<int>(i);
            }
            const int n = static_cast<int>(panes.size());
            const int step = (cmd == Cmd::FocusNextPane) ? 1 : -1;
            FocusPane(panes[(index + step + n) % n]);
            break;
        }
        case Cmd::FocusPaneLeft:
            if (session && pane) FocusPane(session->PaneInDirection(pane, -1, 0));
            break;
        case Cmd::FocusPaneRight:
            if (session && pane) FocusPane(session->PaneInDirection(pane, 1, 0));
            break;
        case Cmd::FocusPaneUp:
            if (session && pane) FocusPane(session->PaneInDirection(pane, 0, -1));
            break;
        case Cmd::FocusPaneDown:
            if (session && pane) FocusPane(session->PaneInDirection(pane, 0, 1));
            break;
        case Cmd::OpenInOtherPane: {
            if (!session || !pane || !tab) break;
            std::vector<Pane*> panes = session->Panes();
            if (panes.size() < 2) break;
            const fs::Entry* e = tab->CursorEntry();
            const std::string target = (e && e->isDir()) ? fs::EntryPath(tab->path, *e)
                                                         : tab->path;
            Pane* other = nullptr;
            for (size_t i = 0; i < panes.size(); ++i) {
                if (panes[i] == pane) {
                    other = panes[(i + 1) % panes.size()];
                    break;
                }
            }
            if (!other) break;
            if (Tab* ot = other->activeTab()) RetargetTab(*ot, target);
            host_.Invalidate();
            break;
        }
        case Cmd::SyncOtherPane: {
            if (!session || !pane || !tab) break;
            for (Pane* p : session->Panes()) {
                if (p == pane) continue;
                if (Tab* ot = p->activeTab()) RetargetTab(*ot, tab->path);
            }
            host_.Invalidate();
            break;
        }
        case Cmd::SwapPanes:
            if (session && pane) {
                session->SwapWithSibling(pane);
                dirty_ = true;
                host_.Invalidate();
            }
            break;

        // --- sessions ---------------------------------------------------------
        case Cmd::NewSession: {
            const std::string name = strings_.Format(
                "ui.new_session", { std::to_string(workspace_.sessions.size() + 1) });
            Session* s = workspace_.AddSession(name, tab ? tab->path : fs_.HomeDir());
            for (Pane* p : s->Panes()) {
                for (std::unique_ptr<Tab>& t : p->tabs) t->view = defaultView_;
            }
            EnsureVisibleTabsLoaded();
            dirty_ = true;
            host_.Invalidate();
            break;
        }
        case Cmd::CloseSession:
            workspace_.CloseSession(workspace_.active);
            EnsureVisibleTabsLoaded();
            dirty_ = true;
            host_.Invalidate();
            break;
        case Cmd::RenameSession:
            if (session) BeginPrompt(PromptKind::SessionName, "ui.session_name_label", session->name);
            break;
        case Cmd::NextSession:
            if (!workspace_.sessions.empty()) {
                GotoSession((workspace_.active + 1) %
                            static_cast<int>(workspace_.sessions.size()));
            }
            break;
        case Cmd::PrevSession:
            if (!workspace_.sessions.empty()) {
                const int n = static_cast<int>(workspace_.sessions.size());
                GotoSession((workspace_.active - 1 + n) % n);
            }
            break;
        // No wrapping, unlike Next/PrevSession: those are "show me another one",
        // where coming back round is the point, while these are a direction along
        // the bar - a chip that leapt from the first slot to the last would only
        // ever be an accident (the same call Cmd::MoveTabLeft/Right makes).
        case Cmd::MoveSessionLeft:
            MoveSession(workspace_.active, workspace_.active - 1);
            break;
        case Cmd::MoveSessionRight:
            MoveSession(workspace_.active, workspace_.active + 1);
            break;
        case Cmd::SaveWorkspace:
            // 失敗したときは WriteConfigFile が出した「保存できません」をそのまま
            // 残す ─ 書けていないのに「保存しました」と答えるのが最悪の応答。
            if (standalone_) {
                SetStatus(strings_.Get("ui.standalone_no_save"));
            } else if (SaveAll()) {
                SetStatus(strings_.Get("ui.saved"));
            }
            break;
        case Cmd::Session1: GotoSession(0); break;
        case Cmd::Session2: GotoSession(1); break;
        case Cmd::Session3: GotoSession(2); break;
        case Cmd::Session4: GotoSession(3); break;
        case Cmd::Session5: GotoSession(4); break;
        case Cmd::Session6: GotoSession(5); break;
        case Cmd::Session7: GotoSession(6); break;
        case Cmd::Session8: GotoSession(7); break;

        // --- view -------------------------------------------------------------
        case Cmd::ToggleHidden:
            ToggleViewFlag(tab, &ViewState::showHidden);
            break;
        case Cmd::ToggleSidebar:
            sidebarVisible_ = !sidebarVisible_;
            dirty_ = true;
            host_.Invalidate();
            break;
        case Cmd::SortByName:
        case Cmd::SortByExt:
        case Cmd::SortBySize:
        case Cmd::SortByDate: {
            if (!tab) break;
            const SortKey key = (cmd == Cmd::SortByExt)    ? SortKey::Ext
                                : (cmd == Cmd::SortBySize) ? SortKey::Size
                                : (cmd == Cmd::SortByDate) ? SortKey::Date
                                                           : SortKey::Name;
            // Re-selecting the active column flips the direction, like Explorer.
            if (tab->view.sort == key) {
                tab->view.sortDesc = !tab->view.sortDesc;
            } else {
                tab->view.sort = key;
                tab->view.sortDesc = false;
            }
            defaultView_.sort = tab->view.sort;
            defaultView_.sortDesc = tab->view.sortDesc;
            RebuildFocused();
            dirty_ = true;
            break;
        }
        case Cmd::ToggleSortOrder:
            ToggleViewFlag(tab, &ViewState::sortDesc);
            break;
        case Cmd::ToggleDirsFirst:
            ToggleViewFlag(tab, &ViewState::dirsFirst);
            break;
        case Cmd::FontLarger:
            SetFontScale(fontScale_ + kFontScaleStep);
            break;
        case Cmd::FontSmaller:
            SetFontScale(fontScale_ - kFontScaleStep);
            break;
        case Cmd::FontReset:
            SetFontScale(1.0f);
            break;

        // --- bookmarks ---------------------------------------------------------
        case Cmd::AddBookmark:
            if (tab) {
                if (!HasBookmark(tab->path)) ToggleBookmark(tab->path);
                dirty_ = true;
            }
            break;
        case Cmd::RemoveBookmark:
            if (tab && HasBookmark(tab->path)) {
                ToggleBookmark(tab->path);
                dirty_ = true;
            }
            break;
        case Cmd::ShowPlaces:
            if (placePicker_.visible()) {
                placePicker_.Close();
            } else {
                OpenPlacePicker();
            }
            host_.Invalidate();
            break;
        case Cmd::Bookmark1: GotoBookmark(0); break;
        case Cmd::Bookmark2: GotoBookmark(1); break;
        case Cmd::Bookmark3: GotoBookmark(2); break;
        case Cmd::Bookmark4: GotoBookmark(3); break;
        case Cmd::Bookmark5: GotoBookmark(4); break;
        case Cmd::Bookmark6: GotoBookmark(5); break;
        case Cmd::Bookmark7: GotoBookmark(6); break;
        case Cmd::Bookmark8: GotoBookmark(7); break;

        // --- file operations ---------------------------------------------------
        case Cmd::NewFolder:
            if (ReadOnlyHere()) break;
            BeginPrompt(PromptKind::NewFolder, "ui.new_folder_label", {});
            break;
        case Cmd::NewFile:
            if (ReadOnlyHere()) break;
            BeginPrompt(PromptKind::NewFile, "ui.new_file_label", {});
            break;
        case Cmd::Rename: {
            if (!tab || ReadOnlyHere()) break;
            const fs::Entry* e = tab->CursorEntry();
            if (!e) break;
            BeginPrompt(PromptKind::Rename, "ui.rename_label", e->name);
            // The stem opens selected, so the first thing typed replaces the name
            // and leaves the extension alone - which is what renaming a file
            // almost always means, and what every shell that offers it does.
            //
            // A folder has no extension to protect, so the whole name is the stem.
            // Deliberately not path::Stem for those: "backup.2026" would keep a
            // ".2026" nobody thinks of as one.
            //
            // path::Stem already answers the whole name for the two cases where
            // there is nothing to keep back - a leading dot (".gitignore") and no
            // dot at all - so neither needs its own branch here.
            if (e->isDir()) {
                prompt_.SelectAll();
            } else {
                prompt_.SelectRange(0, path::Stem(e->name).size());
            }
            break;
        }
        case Cmd::DeleteToRecycle:
            if (!ReadOnlyHere()) DoDelete(false);
            break;
        case Cmd::Restore:
            DoRestore();
            break;
        case Cmd::DeletePermanent:
            if (!ReadOnlyHere()) DoDelete(true);
            break;
        case Cmd::Copy:
        case Cmd::Cut: {
            if (!tab) break;
            if (cmd == Cmd::Cut && ReadOnlyHere()) break;
            std::vector<std::string> paths = tab->SelectionPaths();
            if (paths.empty()) {
                SetStatus(strings_.Get("ui.no_selection"));
                break;
            }
            // Say which of the two happened, and say it at all: the clipboard
            // gives no sign of having been written, so a copy that silently
            // failed and one that worked look exactly alike from here.
            const bool cut = (cmd == Cmd::Cut);
            if (!shell_.SetClipboardFiles(paths, cut)) {
                // The marks stay: the write failed, so whatever was on the
                // clipboard before - possibly an earlier cut of ours - is still
                // there, and the rows drawn dimmed are still telling the truth.
                SetStatus(strings_.Get("ui.clipboard_failed"));
                break;
            }
            // Explorer dims what it cut, and so does Kite: the status line says
            // it once, and the rows go on saying it until the paste. A copy
            // clears the marks for the same reason - the clipboard has moved on.
            ClearCutMarks();
            if (cut) {
                cutPaths_ = paths;
                std::sort(cutPaths_.begin(), cutPaths_.end());
            }
            SetStatus(strings_.Get(cut ? "ui.cut_files" : "ui.copied_files"));
            break;
        }
        case Cmd::Paste:
            if (!ReadOnlyHere()) DoPaste();
            break;
        case Cmd::Undo:
            DoUndo();
            break;
        case Cmd::CopyPath:
        case Cmd::CopyName: {
            if (!tab) break;
            const bool full = (cmd == Cmd::CopyPath);
            const std::vector<std::string> paths = tab->SelectionPaths();
            std::string joined;
            for (size_t i = 0; i < paths.size(); ++i) {
                if (i) joined += "\r\n";
                joined += full ? paths[i] : path::FileName(paths[i]);
            }
            // Nothing selected still leaves "which folder is this" worth
            // answering, and the folder itself is the answer. A bare name has no
            // such fallback, so an empty selection copies nothing.
            if (full && joined.empty()) joined = tab->path;
            SetStatus(strings_.Get(shell_.SetClipboardText(joined) ? "ui.copied"
                                                                  : "ui.clipboard_failed"));
            break;
        }

        // --- shell -------------------------------------------------------------
        case Cmd::ContextMenu:
        case Cmd::ExtendedContextMenu: {
            // Position is supplied by the UI layer for mouse invocations; from
            // the keyboard it belongs on the row the user is looking at, not
            // wherever the pointer was left. A negative pair means "no anchor",
            // and the shell falls back to the pointer.
            int x = -1;
            int y = -1;
            CursorRowAnchor(x, y);
            ShowContextMenuAt(x, y, cmd == Cmd::ExtendedContextMenu);
            break;
        }
        case Cmd::FolderContextMenu:
        case Cmd::ExtendedFolderContextMenu:
            ShowFolderContextMenu(cmd == Cmd::ExtendedFolderContextMenu);
            break;
        case Cmd::Properties:
            if (tab) {
                const std::string p = tab->CursorPath();
                shell_.ShowProperties(p.empty() ? tab->path : p);
            }
            break;
        case Cmd::OpenWith:
            if (tab) {
                const std::string p = tab->CursorPath();
                if (!p.empty()) shell_.OpenWith(p);
            }
            break;
        case Cmd::RevealInExplorer:
            if (tab) {
                const std::string p = tab->CursorPath();
                shell_.RevealInExplorer(p.empty() ? tab->path : p);
            }
            break;
        case Cmd::OpenTerminal:
            // Silence here reads as a dead key: nothing on screen moves either
            // way, so a virtual folder - or a machine with no console at all -
            // has to say so.
            if (tab && !shell_.OpenTerminal(tab->path)) ReportFailure("ui.terminal_failed", {});
            break;
        case Cmd::ConnectNetwork: {
            if (!tab) break;
            // The connection is to the server or the share, never to a folder
            // inside it - that is the unit credentials are checked against.
            const std::string target = path::UncRoot(tab->path);
            if (target.empty()) {
                SetStatus(strings_.Get("ui.not_network_path"));
                break;
            }
            std::string err;
            if (!shell_.ConnectNetwork(target, &err)) {
                // An empty message means the user closed the dialog; they know
                // what happened, and saying it again reads as a failure report.
                if (!err.empty()) SetStatus(err);
                break;
            }
            SetStatus(strings_.Format("ui.network_connected", { target }));
            RefreshFocused();
            break;
        }

        case Cmd::None:
        case Cmd::Count:
            break;
    }

    // Cheap when nothing moved, and it keeps watches correct after any command
    // that adds, closes or retargets a pane or tab.
    SyncWatches();

    UpdateTitle();
}

}  // namespace kite
