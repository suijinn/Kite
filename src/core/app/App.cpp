#include "core/app/App.h"

#include <algorithm>

#include "core/base/Format.h"
#include "core/base/PathUtil.h"
#include "core/base/Platform.h"
#include "core/base/Utf8.h"

namespace kite {
namespace {

constexpr uint64_t kStatusDurationMs = 4000;

const char* SortKeyName(SortKey k) {
    switch (k) {
        case SortKey::Ext: return "ext";
        case SortKey::Size: return "size";
        case SortKey::Date: return "date";
        default: return "name";
    }
}

SortKey SortKeyFromName(const std::string& s) {
    if (s == "ext") return SortKey::Ext;
    if (s == "size") return SortKey::Size;
    if (s == "date") return SortKey::Date;
    return SortKey::Name;
}

}  // namespace

App::App(fs::IFileSystem& filesystem, IShellIntegration& shell, IHost& host,
         fs::IDirectoryWatcher* watcher)
    : fs_(filesystem), shell_(shell), host_(host), watcher_(watcher) {
    theme_ = Theme::Dark();
}

App::~App() = default;

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

bool App::Init(const std::vector<std::string>& startPaths) {
    LoadConfig();
    loader_ = std::make_unique<fs::DirectoryLoader>(fs_, host_, 2);
    RefreshRoots();
    LoadWorkspace(startPaths);
    EnsureVisibleTabsLoaded();
    return true;
}

void App::Shutdown() {
    SaveAll();
    loader_.reset();  // joins workers before anything else is torn down
}

std::string App::ConfigPath(const char* file) const {
    return path::Join(fs_.ConfigDir(), file);
}

void App::RefreshRoots() {
    roots_ = fs_.Roots();
    quickAccess_ = fs_.QuickAccess();
}

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

void App::LoadConfig() {
    plat::EnsureDirectory(fs_.ConfigDir());

    std::string text;
    settings_ = Ini();
    if (plat::ReadTextFile(ConfigPath("settings.ini"), text)) settings_.Parse(text);

    darkTheme_ = settings_.GetStr("ui", "theme", "dark") != "light";
    theme_ = darkTheme_ ? Theme::Dark() : Theme::Light();
    theme_.ApplyIni(settings_);

    language_ = settings_.GetStr("ui", "language", "auto");
    std::string code = language_;
    if (code == "auto" || code.empty()) code = plat::PreferredLanguage();
    strings_.Load(code);

    // Optional user translation file, e.g. lang.ja.ini - lets a new locale ship
    // without touching the binary.
    Ini langIni;
    const std::string langFile = path::Join(fs_.ConfigDir(), "lang." + strings_.code() + ".ini");
    if (plat::ReadTextFile(langFile, text)) {
        langIni.Parse(text);
        strings_.ApplyOverrides(langIni);
    }

    sidebarVisible_ = settings_.GetBool("ui", "sidebar", true);

    defaultView_.showHidden = settings_.GetBool("view", "show_hidden", false);
    defaultView_.dirsFirst = settings_.GetBool("view", "dirs_first", true);
    defaultView_.sortDesc = settings_.GetBool("view", "sort_desc", false);
    defaultView_.sort = SortKeyFromName(settings_.GetStr("view", "sort", "name"));

    placement_.x = settings_.GetInt("window", "x", -1);
    placement_.y = settings_.GetInt("window", "y", -1);
    placement_.w = settings_.GetInt("window", "w", 1180);
    placement_.h = settings_.GetInt("window", "h", 720);
    placement_.maximized = settings_.GetBool("window", "maximized", false);

    keymap_.LoadDefaults();
    Ini keysIni;
    if (plat::ReadTextFile(ConfigPath("keys.ini"), text)) {
        keysIni.Parse(text);
        std::vector<std::string> warnings;
        keymap_.ApplyIni(keysIni, &warnings);
        if (!warnings.empty()) SetStatus(warnings.front());
    } else {
        WriteDefaultKeysFile();
    }

    workspace_.bookmarks.clear();
    Ini bookmarksIni;
    if (plat::ReadTextFile(ConfigPath("bookmarks.ini"), text)) {
        bookmarksIni.Parse(text);
        if (const Ini::Section* sec = bookmarksIni.Find("bookmarks")) {
            for (const Ini::Entry& e : sec->entries) {
                workspace_.bookmarks.push_back({ e.key, e.value });
            }
        }
    }
}

void App::WriteDefaultKeysFile() {
    std::string out =
        "# Kite key bindings.\n"
        "#\n"
        "# Syntax:  <command> = <chord>\n"
        "#   Chords combine Ctrl / Shift / Alt with a key name, e.g. Ctrl+Shift+T.\n"
        "#   Repeat a command on several lines to give it several chords.\n"
        "#   Use \"<command> = none\" to remove every binding for that command.\n"
        "#\n"
        "# This file was generated with the current defaults; edit freely and\n"
        "# reload with Ctrl+Alt+C.\n\n";
    out += keymap_.ToIni().Serialize();
    plat::WriteTextFile(ConfigPath("keys.ini"), out);
}

void App::SaveSettings() {
    settings_.Set("ui", "theme", darkTheme_ ? "dark" : "light");
    settings_.Set("ui", "language", language_);
    settings_.SetBool("ui", "sidebar", sidebarVisible_);

    settings_.SetBool("view", "show_hidden", defaultView_.showHidden);
    settings_.SetBool("view", "dirs_first", defaultView_.dirsFirst);
    settings_.SetBool("view", "sort_desc", defaultView_.sortDesc);
    settings_.Set("view", "sort", SortKeyName(defaultView_.sort));

    settings_.SetInt("window", "x", placement_.x);
    settings_.SetInt("window", "y", placement_.y);
    settings_.SetInt("window", "w", placement_.w);
    settings_.SetInt("window", "h", placement_.h);
    settings_.SetBool("window", "maximized", placement_.maximized);

    plat::WriteTextFile(ConfigPath("settings.ini"), settings_.Serialize());

    Ini bookmarksIni;
    for (const Bookmark& b : workspace_.bookmarks) {
        bookmarksIni.Append("bookmarks", b.name, b.path);
    }
    plat::WriteTextFile(ConfigPath("bookmarks.ini"), bookmarksIni.Serialize());
}

void App::LoadWorkspace(const std::vector<std::string>& startPaths) {
    std::string text;
    Ini ws;
    if (plat::ReadTextFile(ConfigPath("sessions.ini"), text)) ws.Parse(text);

    for (int i = 0;; ++i) {
        const std::string sec = "session." + std::to_string(i);
        if (!ws.Find(sec)) break;
        const std::string name = ws.GetStr(sec, "name", "Session " + std::to_string(i + 1));
        const std::string layout = ws.GetStr(sec, "layout");
        std::unique_ptr<Session> s = Session::Deserialize(name, layout);
        if (s) workspace_.sessions.push_back(std::move(s));
    }

    if (workspace_.sessions.empty()) {
        const std::string home = fs_.HomeDir();
        workspace_.AddSession(strings_.Format("ui.new_session", { "1" }), home);
    }
    workspace_.active = std::clamp(ws.GetInt("workspace", "active", 0), 0,
                                   static_cast<int>(workspace_.sessions.size()) - 1);

    // Seed every tab's view state from the saved defaults.
    for (const std::unique_ptr<Session>& s : workspace_.sessions) {
        for (Pane* p : s->Panes()) {
            for (std::unique_ptr<Tab>& t : p->tabs) t->view = defaultView_;
        }
    }

    // Command-line paths open as extra tabs in the focused pane.
    if (!startPaths.empty()) {
        if (Pane* p = workspace_.focusedPane()) {
            for (const std::string& sp : startPaths) {
                Tab* t = p->AddTab(path::Normalize(sp));
                t->view = defaultView_;
            }
        }
    }
}

void App::SaveWorkspaceFile() {
    Ini ws;
    ws.SetInt("workspace", "active", workspace_.active);
    for (size_t i = 0; i < workspace_.sessions.size(); ++i) {
        const std::string sec = "session." + std::to_string(i);
        ws.Set(sec, "name", workspace_.sessions[i]->name);
        ws.Set(sec, "layout", workspace_.sessions[i]->Serialize());
    }
    plat::WriteTextFile(ConfigPath("sessions.ini"), ws.Serialize());
}

void App::SaveAll() {
    SaveSettings();
    SaveWorkspaceFile();
    dirty_ = false;
}

// ---------------------------------------------------------------------------
// Loading
// ---------------------------------------------------------------------------

void App::RequestLoad(Tab& tab, bool force) {
    if (!loader_) return;
    if (!force && (tab.loaded || tab.loadToken != 0)) return;
    tab.loadToken = loader_->Request(tab.path);
}

void App::EnsureVisibleTabsLoaded() {
    Session* s = workspace_.activeSession();
    if (!s) return;
    for (Pane* p : s->Panes()) {
        if (Tab* t = p->activeTab()) RequestLoad(*t);
    }
    SyncWatches();
}

void App::SyncWatches() {
    if (!watcher_) return;

    // Only what is on screen gets a watch; a background tab is re-listed when
    // it comes forward instead.
    std::unordered_map<uint64_t, std::string> desired;
    std::vector<Tab*> newlyWatched;

    if (Session* s = workspace_.activeSession()) {
        for (Pane* p : s->Panes()) {
            Tab* t = p->activeTab();
            if (!t) continue;
            if (t->watchId == 0) t->watchId = NextWatchId();
            desired[t->watchId] = t->path;

            auto previous = watched_.find(t->watchId);
            if (previous == watched_.end() || previous->second != t->path) {
                newlyWatched.push_back(t);
            }
        }
    }

    for (const auto& [id, path] : watched_) {
        auto it = desired.find(id);
        if (it == desired.end() || it->second != path) watcher_->Unwatch(id);
    }

    for (Tab* t : newlyWatched) {
        watcher_->Watch(t->watchId, t->path);
        // Nothing was reporting changes for this folder until now, so anything
        // already on screen for it may be stale. A tab that has not loaded yet
        // has a request in flight and needs no second one.
        if (t->loaded) RequestLoad(*t, true);
    }

    watched_ = std::move(desired);
}

void App::PumpLoader() {
    // Filesystem notifications arrive on the same wake-up as finished listings.
    if (watcher_) {
        std::vector<fs::ChangeEvent> changes;
        watcher_->Drain(changes);
        for (const fs::ChangeEvent& change : changes) {
            for (const std::unique_ptr<Session>& s : workspace_.sessions) {
                for (Pane* p : s->Panes()) {
                    Tab* t = p->activeTab();
                    // Re-list only if the tab is still showing the folder that
                    // changed; it may have navigated away in the meantime.
                    if (t && t->watchId == change.watchId && t->path == change.path) {
                        RequestLoad(*t, true);
                    }
                }
            }
        }
    }

    if (!loader_) return;
    std::vector<fs::LoadedListing> done;
    loader_->Drain(done);
    if (done.empty()) return;

    for (fs::LoadedListing& l : done) {
        for (const std::unique_ptr<Session>& s : workspace_.sessions) {
            for (Pane* p : s->Panes()) {
                for (std::unique_ptr<Tab>& t : p->tabs) {
                    if (t->loadToken != l.token) continue;
                    t->listing = std::move(l.result);
                    t->loadToken = 0;
                    t->loaded = true;
                    t->marked.assign(t->listing.entries.size(), 0);
                    t->Rebuild();
                }
            }
        }
    }
    EnsureCursorVisible();
    host_.Invalidate();
}

// ---------------------------------------------------------------------------
// Navigation
// ---------------------------------------------------------------------------

void App::FocusPane(Pane* pane) {
    Session* s = workspace_.activeSession();
    if (!s || !pane) return;
    s->focus = pane;
    if (Tab* t = pane->activeTab()) RequestLoad(*t);
    SyncWatches();
    host_.Invalidate();
}

void App::NavigateFocused(const std::string& raw) {
    Tab* t = workspace_.focusedTab();
    if (!t) return;
    const std::string target = path::Normalize(raw);
    if (target.empty()) return;

    if (target != t->path) {
        t->back.push_back(t->path);
        t->forward.clear();
        t->path = target;
    }
    t->filter.clear();
    t->cursor = 0;
    t->anchor = 0;
    t->scroll = 0.0f;
    t->listing.entries.clear();
    t->visible.clear();
    t->marked.clear();
    t->loaded = false;
    RequestLoad(*t, true);
    SyncWatches();
    dirty_ = true;
    host_.Invalidate();
}

void App::OpenPath(const std::string& p, bool newTab) {
    Pane* pane = workspace_.focusedPane();
    if (!pane) return;
    if (newTab) {
        Tab* t = pane->AddTab(path::Normalize(p));
        t->view = defaultView_;
        RequestLoad(*t, true);
        dirty_ = true;
        host_.Invalidate();
    } else {
        NavigateFocused(p);
    }
}

void App::ActivateEntry(int visibleIndex, bool newTab) {
    Tab* t = workspace_.focusedTab();
    if (!t) return;
    if (visibleIndex < 0 || visibleIndex >= static_cast<int>(t->visible.size())) return;

    const fs::Entry& e = t->listing.entries[t->visible[visibleIndex]];
    const std::string full = path::Join(t->path, e.name);
    if (e.isDir()) {
        OpenPath(full, newTab);
    } else {
        // Never read the file here: a cloud placeholder must be hydrated by the
        // shell, on the shell's terms.
        shell_.Open(full);
    }
}

void App::RefreshFocused() {
    if (Tab* t = workspace_.focusedTab()) {
        RequestLoad(*t, true);
        host_.Invalidate();
    }
}

// ---------------------------------------------------------------------------
// Drag & drop
// ---------------------------------------------------------------------------

bool App::IsValidDropTarget(const std::vector<std::string>& paths, const std::string& destDir) {
    if (destDir.empty() || paths.empty()) return false;
    const std::string dest = path::Normalize(destDir);

    for (const std::string& p : paths) {
        const std::string src = path::Normalize(p);
        if (src.empty()) return false;
        if (utf8::EqualsIgnoreCaseAscii(src, dest)) return false;  // onto itself

        // Refuse to drop a folder into its own subtree - that is the one
        // mistake here that can destroy data.
        if (dest.size() > src.size() &&
            utf8::EqualsIgnoreCaseAscii(dest.substr(0, src.size()), src) &&
            path::IsSep(dest[src.size()])) {
            return false;
        }
    }
    return true;
}

bool App::PerformDrop(const std::vector<std::string>& paths, const std::string& destDir,
                      bool move) {
    if (!IsValidDropTarget(paths, destDir)) {
        SetStatus(strings_.Get("ui.drop_invalid"));
        host_.Invalidate();
        return false;
    }

    // Moving something into the folder it already lives in is a no-op, not an
    // error; drop those silently rather than letting the shell complain.
    std::vector<std::string> sources;
    for (const std::string& p : paths) {
        if (move && utf8::EqualsIgnoreCaseAscii(path::Parent(p), destDir)) continue;
        sources.push_back(p);
    }
    if (sources.empty()) return false;

    std::string err;
    if (!fs_.CopyTo(sources, destDir, move, &err)) {
        if (!err.empty()) SetStatus(err);
        return false;
    }

    // The watcher normally picks this up, but a folder that could not be
    // watched still has to refresh.
    RefreshTabsShowing(destDir);
    if (move) {
        for (const std::string& p : sources) RefreshTabsShowing(path::Parent(p));
    }
    SetStatus(strings_.Get("ui.done"));
    host_.Invalidate();
    return true;
}

void App::RefreshTabsShowing(const std::string& dir) {
    if (dir.empty()) return;
    Session* s = workspace_.activeSession();
    if (!s) return;
    for (Pane* p : s->Panes()) {
        if (Tab* t = p->activeTab()) {
            if (utf8::EqualsIgnoreCaseAscii(t->path, dir)) RequestLoad(*t, true);
        }
    }
}

void App::RebuildFocused() {
    if (Tab* t = workspace_.focusedTab()) {
        t->Rebuild();
        EnsureCursorVisible();
        host_.Invalidate();
    }
}

void App::EnsureCursorVisible() {
    Pane* p = workspace_.focusedPane();
    if (!p) return;
    Tab* t = p->activeTab();
    if (!t) return;

    const float rowH = p->rowHeight > 0.0f ? p->rowHeight : theme_.rowHeight;
    const float viewH = p->listHeight > 0.0f ? p->listHeight : rowH * 10.0f;
    const float top = static_cast<float>(t->cursor) * rowH;

    if (top < t->scroll) {
        t->scroll = top;
    } else if (top + rowH > t->scroll + viewH) {
        t->scroll = top + rowH - viewH;
    }
    const float maxScroll =
        std::max(0.0f, static_cast<float>(t->visible.size()) * rowH - viewH);
    t->scroll = std::clamp(t->scroll, 0.0f, maxScroll);
}

void App::MoveCursor(int delta, bool extend, bool absolute) {
    Tab* t = workspace_.focusedTab();
    if (!t || t->visible.empty()) return;

    int target = absolute ? delta : t->cursor + delta;
    target = std::clamp(target, 0, static_cast<int>(t->visible.size()) - 1);

    if (extend) {
        t->ClearMarks();
        t->MarkRange(t->anchor, target, true);
    } else {
        t->ClearMarks();
        t->anchor = target;
    }
    t->cursor = target;
    EnsureCursorVisible();
    host_.Invalidate();
}

// ---------------------------------------------------------------------------
// Status
// ---------------------------------------------------------------------------

void App::SetStatus(const std::string& message) {
    statusMessage_ = message;
    statusUntilMs_ = plat::NowMs() + kStatusDurationMs;
}

bool App::statusExpired() const { return plat::NowMs() > statusUntilMs_; }

// ---------------------------------------------------------------------------
// Bookmarks
// ---------------------------------------------------------------------------

bool App::HasBookmark(const std::string& p) const {
    for (const Bookmark& b : workspace_.bookmarks) {
        if (utf8::EqualsIgnoreCaseAscii(b.path, p)) return true;
    }
    return false;
}

void App::ToggleBookmark(const std::string& p) {
    for (size_t i = 0; i < workspace_.bookmarks.size(); ++i) {
        if (utf8::EqualsIgnoreCaseAscii(workspace_.bookmarks[i].path, p)) {
            workspace_.bookmarks.erase(workspace_.bookmarks.begin() + i);
            dirty_ = true;
            host_.Invalidate();
            return;
        }
    }
    workspace_.bookmarks.push_back({ path::DisplayName(p), p });
    dirty_ = true;
    host_.Invalidate();
}

// ---------------------------------------------------------------------------
// Shell
// ---------------------------------------------------------------------------

void App::ShowContextMenuAt(int screenX, int screenY, bool extended) {
    Tab* t = workspace_.focusedTab();
    if (!t) return;
    std::vector<std::string> paths = t->SelectionPaths();
    if (paths.empty()) paths.push_back(t->path);
    shell_.ShowContextMenu(paths, screenX, screenY, extended);
    // The menu may have renamed, deleted or created something.
    RefreshFocused();
}

// ---------------------------------------------------------------------------
// Prompts
// ---------------------------------------------------------------------------

void App::BeginPrompt(PromptKind kind, const char* labelKey, const std::string& initial) {
    prompt_.kind = kind;
    prompt_.labelKey = labelKey;
    prompt_.text = initial;
    prompt_.caret = initial.size();
    prompt_.pendingPaths.clear();
    host_.Invalidate();
}

void App::CancelPrompt() {
    if (prompt_.kind == PromptKind::Filter) {
        if (Tab* t = workspace_.focusedTab()) {
            t->filter.clear();
            t->Rebuild();
            EnsureCursorVisible();
        }
    }
    prompt_ = Prompt{};
    host_.Invalidate();
}

void App::ApplyPrompt() {
    Tab* t = workspace_.focusedTab();
    const PromptKind kind = prompt_.kind;
    const std::string text = prompt_.text;
    std::vector<std::string> pending = prompt_.pendingPaths;
    prompt_ = Prompt{};

    std::string err;
    switch (kind) {
        case PromptKind::Path:
            NavigateFocused(text);
            break;

        case PromptKind::Filter:
            if (t) ActivateEntry(t->cursor, false);
            break;

        case PromptKind::Rename: {
            if (!t || text.empty()) break;
            const fs::Entry* e = t->CursorEntry();
            if (!e || text == e->name) break;
            const std::string from = path::Join(t->path, e->name);
            const std::string to = path::Join(t->path, text);
            if (!fs_.Rename(from, to, &err)) {
                SetStatus(err);
            } else {
                RefreshFocused();
            }
            break;
        }

        case PromptKind::NewFolder: {
            if (!t || text.empty()) break;
            if (!fs_.MakeDirectory(path::Join(t->path, text), &err)) {
                SetStatus(err);
            } else {
                RefreshFocused();
            }
            break;
        }

        case PromptKind::NewFile: {
            if (!t || text.empty()) break;
            if (!fs_.MakeFile(path::Join(t->path, text), &err)) {
                SetStatus(err);
            } else {
                RefreshFocused();
            }
            break;
        }

        case PromptKind::SessionName: {
            if (Session* s = workspace_.activeSession()) {
                if (!text.empty()) s->name = text;
                dirty_ = true;
            }
            break;
        }

        case PromptKind::ConfirmDelete:
        case PromptKind::ConfirmDeletePermanent: {
            const bool recycle = (kind == PromptKind::ConfirmDelete);
            if (!fs_.Delete(pending, recycle, &err)) {
                SetStatus(err);
            } else {
                SetStatus(strings_.Get("ui.done"));
            }
            RefreshFocused();
            break;
        }

        case PromptKind::None:
            break;
    }
    host_.Invalidate();
}

bool App::HandlePromptKey(const Chord& chord) {
    if (!prompt_.active()) return false;

    // While filtering, the list must stay drivable from the keyboard.
    if (prompt_.kind == PromptKind::Filter) {
        const Cmd c = keymap_.Lookup(chord);
        if (c == Cmd::CursorUp || c == Cmd::CursorDown || c == Cmd::CursorPageUp ||
            c == Cmd::CursorPageDown || c == Cmd::CursorTop || c == Cmd::CursorBottom) {
            return false;
        }
    }

    auto syncFilter = [&] {
        if (prompt_.kind != PromptKind::Filter) return;
        if (Tab* t = workspace_.focusedTab()) {
            t->filter = prompt_.text;
            t->Rebuild();
            EnsureCursorVisible();
        }
    };

    switch (chord.key) {
        case Key::Escape:
            CancelPrompt();
            return true;
        case Key::Enter:
            ApplyPrompt();
            return true;
        case Key::Backspace: {
            if (prompt_.isConfirm()) return true;
            if (prompt_.caret > 0) {
                const size_t start = utf8::PrevBoundary(prompt_.text, prompt_.caret);
                prompt_.text.erase(start, prompt_.caret - start);
                prompt_.caret = start;
                syncFilter();
            }
            host_.Invalidate();
            return true;
        }
        case Key::Delete: {
            if (prompt_.isConfirm()) return true;
            if (prompt_.caret < prompt_.text.size()) {
                const size_t end = utf8::NextBoundary(prompt_.text, prompt_.caret);
                prompt_.text.erase(prompt_.caret, end - prompt_.caret);
                syncFilter();
            }
            host_.Invalidate();
            return true;
        }
        case Key::Left:
            prompt_.caret = utf8::PrevBoundary(prompt_.text, prompt_.caret);
            host_.Invalidate();
            return true;
        case Key::Right:
            prompt_.caret = utf8::NextBoundary(prompt_.text, prompt_.caret);
            host_.Invalidate();
            return true;
        case Key::Home:
            prompt_.caret = 0;
            host_.Invalidate();
            return true;
        case Key::End:
            prompt_.caret = prompt_.text.size();
            host_.Invalidate();
            return true;
        default:
            break;
    }
    // Swallow everything else so a stray shortcut cannot fire mid-edit.
    return true;
}

bool App::OnChar(uint32_t cp) {
    if (!prompt_.active() || prompt_.isConfirm()) return false;
    if (cp < 0x20 || cp == 0x7F) return false;

    std::string encoded;
    utf8::Encode(cp, encoded);
    prompt_.text.insert(prompt_.caret, encoded);
    prompt_.caret += encoded.size();

    if (prompt_.kind == PromptKind::Filter) {
        if (Tab* t = workspace_.focusedTab()) {
            t->filter = prompt_.text;
            t->Rebuild();
            EnsureCursorVisible();
        }
    }
    host_.Invalidate();
    return true;
}

bool App::OnKey(const Chord& chord) {
    if (keyHelp_) {
        // Any key closes the cheat sheet except the one that re-opens it.
        const Cmd c = keymap_.Lookup(chord);
        if (c == Cmd::ShowKeyHelp) return true;
        keyHelp_ = false;
        host_.Invalidate();
        if (chord.key == Key::Escape) return true;
    }
    if (HandlePromptKey(chord)) return true;

    const Cmd cmd = keymap_.Lookup(chord);
    if (cmd == Cmd::None) return false;
    Execute(cmd);
    return true;
}

// ---------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------

void App::GotoTab(int index) {
    Pane* p = workspace_.focusedPane();
    if (!p) return;
    if (index < 0) index = static_cast<int>(p->tabs.size()) - 1;
    if (index >= static_cast<int>(p->tabs.size())) return;
    p->Activate(index);
    if (Tab* t = p->activeTab()) RequestLoad(*t);
    host_.Invalidate();
}

void App::GotoSession(int index) {
    if (index < 0 || index >= static_cast<int>(workspace_.sessions.size())) return;
    workspace_.ActivateSession(index);
    EnsureVisibleTabsLoaded();
    dirty_ = true;
    host_.Invalidate();
}

void App::GotoBookmark(int index) {
    if (index < 0 || index >= static_cast<int>(workspace_.bookmarks.size())) return;
    NavigateFocused(workspace_.bookmarks[index].path);
}

void App::DoDelete(bool permanent) {
    Tab* t = workspace_.focusedTab();
    if (!t) return;
    std::vector<std::string> paths = t->SelectionPaths();
    if (paths.empty()) {
        SetStatus(strings_.Get("ui.no_selection"));
        return;
    }
    BeginPrompt(permanent ? PromptKind::ConfirmDeletePermanent : PromptKind::ConfirmDelete,
                permanent ? "ui.confirm_delete_perm" : "ui.confirm_delete", {});
    prompt_.pendingPaths = paths;
    prompt_.text = std::to_string(paths.size());
}

void App::DoPaste() {
    Tab* t = workspace_.focusedTab();
    if (!t) return;
    std::vector<std::string> paths;
    bool cut = false;
    if (!shell_.GetClipboardFiles(paths, &cut) || paths.empty()) {
        SetStatus(strings_.Get("ui.clipboard_empty"));
        return;
    }
    std::string err;
    if (!fs_.CopyTo(paths, t->path, cut, &err)) {
        SetStatus(err);
    }
    RefreshFocused();
}

void App::Execute(Cmd cmd) {
    Session* session = workspace_.activeSession();
    Pane* pane = workspace_.focusedPane();
    Tab* tab = workspace_.focusedTab();

    switch (cmd) {
        // --- application -----------------------------------------------------
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
            shell_.Open(fs_.ConfigDir());
            break;
        case Cmd::ToggleTheme:
            darkTheme_ = !darkTheme_;
            theme_ = darkTheme_ ? Theme::Dark() : Theme::Light();
            theme_.ApplyIni(settings_);
            dirty_ = true;
            host_.Invalidate();
            break;
        case Cmd::ToggleLanguage: {
            language_ = (strings_.code() == "ja") ? "en" : "ja";
            strings_.Load(language_);
            dirty_ = true;
            host_.Invalidate();
            break;
        }
        case Cmd::ShowKeyHelp:
            keyHelp_ = !keyHelp_;
            host_.Invalidate();
            break;
        case Cmd::CancelOverlay:
            if (keyHelp_) {
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
        case Cmd::GoUp: {
            if (!tab) break;
            const std::string parent = path::Parent(tab->path);
            if (parent.empty()) break;
            const std::string leaving = path::FileName(tab->path);
            NavigateFocused(parent);
            // Land the cursor on the folder we just came out of.
            if (Tab* t = workspace_.focusedTab()) t->pendingFocusName = leaving;
            break;
        }
        case Cmd::GoBack: {
            if (!tab || tab->back.empty()) break;
            const std::string target = tab->back.back();
            tab->back.pop_back();
            tab->forward.push_back(tab->path);
            tab->path = target;
            tab->loaded = false;
            tab->cursor = 0;
            tab->scroll = 0.0f;
            RequestLoad(*tab, true);
            host_.Invalidate();
            break;
        }
        case Cmd::GoForward: {
            if (!tab || tab->forward.empty()) break;
            const std::string target = tab->forward.back();
            tab->forward.pop_back();
            tab->back.push_back(tab->path);
            tab->path = target;
            tab->loaded = false;
            tab->cursor = 0;
            tab->scroll = 0.0f;
            RequestLoad(*tab, true);
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
            if (tab) BeginPrompt(PromptKind::Path, "ui.path_label", tab->path);
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
            tab->marked[index] = tab->marked[index] ? 0 : 1;
            if (tab->cursor + 1 < static_cast<int>(tab->visible.size())) tab->cursor++;
            tab->anchor = tab->cursor;
            EnsureCursorVisible();
            host_.Invalidate();
            break;
        }
        case Cmd::SelectAll:
            if (tab) {
                tab->MarkRange(0, static_cast<int>(tab->visible.size()) - 1, true);
                host_.Invalidate();
            }
            break;
        case Cmd::SelectNone:
            if (tab) {
                tab->ClearMarks();
                host_.Invalidate();
            }
            break;
        case Cmd::InvertSelection:
            if (tab) {
                for (int index : tab->visible) tab->marked[index] = tab->marked[index] ? 0 : 1;
                host_.Invalidate();
            }
            break;

        // --- tabs ------------------------------------------------------------
        case Cmd::NewTab: {
            if (!pane) break;
            Tab* t = pane->AddTab(tab ? tab->path : fs_.HomeDir());
            t->view = defaultView_;
            RequestLoad(*t, true);
            dirty_ = true;
            host_.Invalidate();
            break;
        }
        case Cmd::CloseTab: {
            if (!pane) break;
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
            Tab* t = pane->AddTab(tab->path, pane->active + 1);
            t->view = tab->view;
            RequestLoad(*t, true);
            dirty_ = true;
            host_.Invalidate();
            break;
        }
        case Cmd::ReopenTab: {
            if (!pane || workspace_.closedTabs.empty()) break;
            const std::string p = workspace_.closedTabs.back();
            workspace_.closedTabs.pop_back();
            Tab* t = pane->AddTab(p);
            t->view = defaultView_;
            RequestLoad(*t, true);
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
        case Cmd::MoveTabLeft:
            if (pane && pane->active > 0) {
                std::swap(pane->tabs[pane->active], pane->tabs[pane->active - 1]);
                pane->active--;
                dirty_ = true;
                host_.Invalidate();
            }
            break;
        case Cmd::MoveTabRight:
            if (pane && pane->active + 1 < static_cast<int>(pane->tabs.size())) {
                std::swap(pane->tabs[pane->active], pane->tabs[pane->active + 1]);
                pane->active++;
                dirty_ = true;
                host_.Invalidate();
            }
            break;
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
            const std::string target = (e && e->isDir()) ? path::Join(tab->path, e->name)
                                                         : tab->path;
            Pane* other = nullptr;
            for (size_t i = 0; i < panes.size(); ++i) {
                if (panes[i] == pane) {
                    other = panes[(i + 1) % panes.size()];
                    break;
                }
            }
            if (!other) break;
            if (Tab* ot = other->activeTab()) {
                ot->path = target;
                ot->loaded = false;
                ot->cursor = 0;
                ot->scroll = 0.0f;
                RequestLoad(*ot, true);
            }
            host_.Invalidate();
            break;
        }
        case Cmd::SyncOtherPane: {
            if (!session || !pane || !tab) break;
            for (Pane* p : session->Panes()) {
                if (p == pane) continue;
                if (Tab* ot = p->activeTab()) {
                    ot->path = tab->path;
                    ot->loaded = false;
                    ot->cursor = 0;
                    ot->scroll = 0.0f;
                    RequestLoad(*ot, true);
                }
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
        case Cmd::SaveWorkspace:
            SaveAll();
            SetStatus(strings_.Get("ui.saved"));
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
            if (tab) {
                tab->view.showHidden = !tab->view.showHidden;
                defaultView_.showHidden = tab->view.showHidden;
                RebuildFocused();
                dirty_ = true;
            }
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
            if (tab) {
                tab->view.sortDesc = !tab->view.sortDesc;
                defaultView_.sortDesc = tab->view.sortDesc;
                RebuildFocused();
                dirty_ = true;
            }
            break;
        case Cmd::ToggleDirsFirst:
            if (tab) {
                tab->view.dirsFirst = !tab->view.dirsFirst;
                defaultView_.dirsFirst = tab->view.dirsFirst;
                RebuildFocused();
                dirty_ = true;
            }
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
            BeginPrompt(PromptKind::NewFolder, "ui.new_folder_label", {});
            break;
        case Cmd::NewFile:
            BeginPrompt(PromptKind::NewFile, "ui.new_file_label", {});
            break;
        case Cmd::Rename: {
            if (!tab) break;
            const fs::Entry* e = tab->CursorEntry();
            if (!e) break;
            BeginPrompt(PromptKind::Rename, "ui.rename_label", e->name);
            // Preselect the stem so typing replaces the name, not the extension.
            if (!e->isDir()) prompt_.caret = path::Stem(e->name).size();
            break;
        }
        case Cmd::DeleteToRecycle: DoDelete(false); break;
        case Cmd::DeletePermanent: DoDelete(true); break;
        case Cmd::Copy:
        case Cmd::Cut: {
            if (!tab) break;
            std::vector<std::string> paths = tab->SelectionPaths();
            if (paths.empty()) {
                SetStatus(strings_.Get("ui.no_selection"));
                break;
            }
            shell_.SetClipboardFiles(paths, cmd == Cmd::Cut);
            SetStatus(strings_.Get("ui.copied"));
            break;
        }
        case Cmd::Paste:
            DoPaste();
            break;
        case Cmd::CopyPath: {
            if (!tab) break;
            std::vector<std::string> paths = tab->SelectionPaths();
            std::string joined;
            for (size_t i = 0; i < paths.size(); ++i) {
                if (i) joined += "\r\n";
                joined += paths[i];
            }
            if (joined.empty()) joined = tab->path;
            shell_.SetClipboardText(joined);
            SetStatus(strings_.Get("ui.copied"));
            break;
        }
        case Cmd::CopyName: {
            if (!tab) break;
            std::vector<std::string> paths = tab->SelectionPaths();
            std::string joined;
            for (size_t i = 0; i < paths.size(); ++i) {
                if (i) joined += "\r\n";
                joined += path::FileName(paths[i]);
            }
            shell_.SetClipboardText(joined);
            SetStatus(strings_.Get("ui.copied"));
            break;
        }

        // --- shell -------------------------------------------------------------
        case Cmd::ContextMenu:
        case Cmd::ExtendedContextMenu:
            // Position is supplied by the UI layer for mouse invocations; from
            // the keyboard we let the platform place it on the focused row.
            ShowContextMenuAt(-1, -1, cmd == Cmd::ExtendedContextMenu);
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
            if (tab) shell_.OpenTerminal(tab->path);
            break;

        case Cmd::None:
        case Cmd::Count:
            break;
    }

    // Cheap when nothing moved, and it keeps watches correct after any command
    // that adds, closes or retargets a pane or tab.
    SyncWatches();

    // Only touch the caption when it actually changed: SetWindowText on every
    // cursor keystroke makes the taskbar entry flicker.
    if (Tab* t = workspace_.focusedTab()) {
        Session* s = workspace_.activeSession();
        std::string title = t->path + "  \xE2\x80\x94  " +
                            (s ? s->name : std::string("Kite")) + "  \xE2\x80\x94  Kite";
        if (title != lastTitle_) {
            lastTitle_ = std::move(title);
            host_.SetTitle(lastTitle_);
        }
    }
}

}  // namespace kite
