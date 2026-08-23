#include "core/app/App.h"

#include <algorithm>

#include "core/base/PathUtil.h"
#include "core/base/Platform.h"
#include "core/base/Utf8.h"
#include "core/base/Version.h"
#include "core/fs/VirtualPath.h"

namespace kite {
namespace {

constexpr uint64_t kStatusDurationMs = 4000;

// How far into the row the keyboard-invoked shell menu is anchored. The menu
// opens down and to the right of this point, so a small indent puts its corner
// under the item's icon rather than out on the pane border.
constexpr float kMenuAnchorIndent = 8.0f;

// Clipboard text on its way into a one-line field.
//
// Only the first line survives: the field holds one path, and a multi-line
// paste would otherwise arrive as one run-on string with the newlines invisible.
// Surrounding quotes go too - Explorer's "Copy as path" wraps its answer in
// them, and pasting that verbatim gives a path no filesystem has ever heard of.
std::string ClipboardToField(std::string_view text) {
    size_t begin = 0;
    size_t end = text.find_first_of("\r\n");
    if (end == std::string_view::npos) end = text.size();
    while (begin < end && (text[begin] == ' ' || text[begin] == '\t')) ++begin;
    while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t')) --end;
    if (end - begin >= 2 && text[begin] == '"' && text[end - 1] == '"') {
        ++begin;
        --end;
    }
    std::string out(text.substr(begin, end - begin));
    // Anything left below space would draw as a blank the caret walks through.
    out.erase(std::remove_if(out.begin(), out.end(),
                             [](char c) { return static_cast<unsigned char>(c) < 0x20; }),
              out.end());
    return out;
}

// A field on its way to the clipboard.
//
// Nothing selected still leaves an obvious thing to copy - the whole line - and
// copying cannot lose anything. Cut can, so it stays out of the way until there
// is a range to take.
//
// Shared by every field Kite draws: the prompt on a breadcrumb or a row, and the
// filter box of the two choosers. A field that answered Ctrl+C differently
// depending on which screen it was on would not be one field.
bool CopyFieldToClipboard(IShellIntegration& shell, TextField& field, bool cut) {
    std::string text;
    if (field.hasSelection()) {
        text = field.Selection();
    } else if (!cut) {
        text = field.text;
    }
    if (text.empty()) return false;
    if (!shell.SetClipboardText(text)) return false;
    return cut && field.DeleteSelection();
}

// The clipboard on its way into a field.
//
// Files come second: a path copied as text is the common case, and the shell
// hands back both formats when Explorer copied a file, so asking for text first
// is what keeps "Copy as path" pasting as itself.
bool PasteIntoField(IShellIntegration& shell, TextField& field) {
    std::string text;
    std::vector<std::string> files;
    if (!shell.GetClipboardText(text)) {
        if (!shell.GetClipboardFiles(files, nullptr) || files.empty()) return false;
        text = files.front();
    }
    return field.Insert(ClipboardToField(text));
}

// The three clipboard chords over a chooser's filter box.
//
// Taken here rather than inside PickerList because core reaches the clipboard
// only through IShellIntegration, and a list of rows has no business holding
// one. Both choosers wrap the same PickerList, so one template covers them.
//
// Left to the keymap these would reach Cmd::Copy and Cmd::Paste and act on the
// listing behind the panel - the same reason the prompt answers them itself.
template <class Picker>
bool PickerClipboardKey(IShellIntegration& shell, Picker& picker, const Chord& chord) {
    // Each of the three has an Insert/Delete spelling as well, the way the
    // prompt takes them.
    enum class Which { No, Copy, Cut, Paste };
    const Which which =
        (chord.mods == kModCtrl && chord.key == Key::C)        ? Which::Copy
        : (chord.mods == kModCtrl && chord.key == Key::Insert) ? Which::Copy
        : (chord.mods == kModCtrl && chord.key == Key::X)      ? Which::Cut
        : (chord.mods == kModShift && chord.key == Key::Delete) ? Which::Cut
        : (chord.mods == kModCtrl && chord.key == Key::V)      ? Which::Paste
        : (chord.mods == kModShift && chord.key == Key::Insert) ? Which::Paste
                                                                : Which::No;
    if (which == Which::No) return false;

    const bool edited =
        (which == Which::Paste)
            ? PasteIntoField(shell, picker.filterField())
            : CopyFieldToClipboard(shell, picker.filterField(), which == Which::Cut);
    if (edited) picker.FilterEdited();
    return true;
}

// Whether a character message is going to follow this chord.
//
// Only those chords set the swallow flag. Setting it for the rest would leave
// it standing until the next keystroke, which is long enough to eat a character
// that arrives without a key-down of its own - IME composition does exactly
// that, since the key-down it sends first is VK_PROCESSKEY and never reaches
// here as a chord at all.
bool ProducesChar(const Chord& c) {
    if ((c.mods & (kModCtrl | kModAlt)) != 0) return false;
    const int k = static_cast<int>(c.key);
    const auto within = [k](Key lo, Key hi) {
        return k >= static_cast<int>(lo) && k <= static_cast<int>(hi);
    };
    // Enter, Tab, Escape and Backspace are left out on purpose: what follows
    // them is below U+0020, which type-ahead refuses anyway.
    return within(Key::A, Key::Num9) || within(Key::Minus, Key::Grave) ||
           within(Key::NumpadAdd, Key::NumpadDiv) || c.key == Key::Space;
}

// The listing as type-ahead sees it: names only, and ".." left blank because it
// is the way out of the folder rather than an item to land on by name.
class TabRows : public TypeAhead::IRows {
public:
    explicit TabRows(const Tab& tab) : tab_(tab) {}

    int Count() const override { return static_cast<int>(tab_.visible.size()); }

    std::string_view NameAt(int index) const override {
        const fs::Entry* e = tab_.EntryAt(index);
        return e ? std::string_view(e->name) : std::string_view();
    }

private:
    const Tab& tab_;
};

// Whether each source already has a namesake in the destination, in the order
// the sources were given. Taken before a transfer so the same question can be
// asked again afterwards: the shell resolves a collision either by renaming its
// copy or by overwriting, and neither outcome is visible from the return value.
std::vector<bool> DestinationsExist(fs::IFileSystem& fs, const std::vector<std::string>& sources,
                                    const std::string& destDir) {
    std::vector<bool> out;
    out.reserve(sources.size());
    for (const std::string& s : sources) {
        out.push_back(fs.Exists(path::Join(destDir, path::FileName(s))));
    }
    return out;
}

// Whether an item already sits in this folder. Both spellings go through
// Normalize() so that the answer does not turn on which separator or which case
// the clipboard happened to hand over.
bool LivesIn(const std::string& path, const std::string& dir) {
    return utf8::EqualsIgnoreCaseAscii(path::Normalize(path::Parent(path)), path::Normalize(dir));
}

// Move one element, taking `to` as the index it should end up at once the
// element has been lifted out - the same convention Pane::ReorderTab uses.
template <typename T>
bool MoveInVector(std::vector<T>& items, int from, int to) {
    const int count = static_cast<int>(items.size());
    if (from < 0 || from >= count) return false;
    to = std::clamp(to, 0, count - 1);
    if (from == to) return false;

    T moved = std::move(items[static_cast<size_t>(from)]);
    items.erase(items.begin() + from);
    items.insert(items.begin() + to, std::move(moved));
    return true;
}

std::vector<std::string> PathsOf(const std::vector<fs::Root>& roots) {
    std::vector<std::string> out;
    out.reserve(roots.size());
    for (const fs::Root& r : roots) out.push_back(r.path);
    return out;
}

// Put `roots` back into the order the user dragged them into. Anything the
// saved order does not name - a drive plugged in since, a cloud folder that
// appeared - keeps its enumeration order and follows behind, rather than
// landing in an arbitrary spot in the middle.
void ApplySavedOrder(std::vector<fs::Root>& roots, const std::vector<std::string>& order) {
    if (order.empty() || roots.empty()) return;

    std::vector<fs::Root> sorted;
    sorted.reserve(roots.size());
    for (const std::string& path : order) {
        auto it = std::find_if(roots.begin(), roots.end(), [&](const fs::Root& r) {
            return utf8::EqualsIgnoreCaseAscii(r.path, path);
        });
        if (it == roots.end()) continue;  // no longer present; its slot just closes up
        sorted.push_back(std::move(*it));
        roots.erase(it);
    }
    for (fs::Root& r : roots) sorted.push_back(std::move(r));
    roots = std::move(sorted);
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
    // Before any command has run, so the window does not sit there captioned
    // with the bare class name until the user touches something.
    UpdateTitle();
    return true;
}

void App::Shutdown() {
    SaveAll();
    loader_.reset();  // joins workers before anything else is torn down
}

uint32_t App::IconFor(const std::string& path) {
    if (!icons_ || !shellIcons_) return 0;
    return icons_->IconFor(path);
}

void App::RefreshRoots() {
    roots_ = fs_.Roots();
    quickAccess_ = fs_.QuickAccess();
    // The platform names these places in the OS's language; Kite's own may be a
    // different one. Its answer is what is on screen everywhere else, so it is
    // the one that goes here too - and it follows the language toggle, which
    // the shell's name would not.
    for (fs::Root& r : quickAccess_) {
        if (const char* key = vfs::LabelKey(r.path)) r.label = strings_.Get(key);
    }
    // Both lists come straight from the OS, in the OS's order. Any dragging the
    // user did lives in the saved order and has to be laid back over them here -
    // this runs again whenever the drive list changes, not just at start-up.
    ApplySavedOrder(quickAccess_, quickAccessOrder_);
    ApplySavedOrder(roots_, driveOrder_);
}

bool App::MoveSidebarSection(int from, int to) {
    if (!MoveInVector(sidebarSections_, from, to)) return false;
    dirty_ = true;
    host_.Invalidate();
    return true;
}

int App::SidebarItemCount(SidebarSection section) const {
    switch (section) {
        case SidebarSection::QuickAccess: return static_cast<int>(quickAccess_.size());
        case SidebarSection::Bookmarks: return static_cast<int>(workspace_.bookmarks.size());
        case SidebarSection::Drives: return static_cast<int>(roots_.size());
        default: return 0;
    }
}

bool App::MoveSidebarItem(SidebarSection section, int from, int to) {
    bool moved = false;
    switch (section) {
        case SidebarSection::QuickAccess:
            moved = MoveInVector(quickAccess_, from, to);
            if (moved) quickAccessOrder_ = PathsOf(quickAccess_);
            break;
        case SidebarSection::Bookmarks:
            // No separate order to record: bookmarks.ini is written in this
            // order, and Alt+Shift+1..8 counts through the same list.
            moved = MoveInVector(workspace_.bookmarks, from, to);
            break;
        case SidebarSection::Drives:
            moved = MoveInVector(roots_, from, to);
            if (moved) driveOrder_ = PathsOf(roots_);
            break;
        default:
            return false;
    }
    if (!moved) return false;
    dirty_ = true;
    host_.Invalidate();
    return true;
}

// Only one overlay is ever on screen: they all swallow every keystroke, so two
// of them would leave the keyboard with two owners. Closing them one at a time
// at each opening is how the fifth one ends up forgotten in one of the places.
void App::CloseAllOverlays() {
    keyHelp_ = false;
    keyEditor_.Close();
    settingsEditor_.Close();
    placePicker_.Close();
    commandPalette_.Close();
}

// Point a tab somewhere else and go and get it.
//
// The history is deliberately not touched: going back, opening a folder in the
// other pane and syncing the panes all move a tab the same way, and only the
// caller knows whether the place being left is worth remembering.
void App::RetargetTab(Tab& tab, const std::string& path) {
    tab.path = path;
    tab.loaded = false;
    tab.cursor = 0;
    tab.scroll = 0.0f;
    RequestLoad(tab, true);
}

// A new tab always arrives the same way: added, given a view state, and sent to
// the loader at once. Forced, because the tab is new and has nothing to keep.
Tab* App::OpenTabIn(Pane& pane, const std::string& path, int at, const ViewState& view) {
    Tab* t = pane.AddTab(path, at);
    t->view = view;
    RequestLoad(*t, true);
    return t;
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
            // Nothing to hang a notification on: the shell namespace has no
            // directory handle. F5 is how these are refreshed.
            if (vfs::IsVirtual(t->path)) continue;
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
        if (completeToken_ != 0 && l.token == completeToken_) {
            completeToken_ = 0;
            // Handed over even when the listing failed: a folder that does not
            // exist has no candidates, and saying so once stops the request
            // from being made again on the next keystroke.
            complete_.SetListing(l.path, l.result.entries);
            continue;
        }
        for (const std::unique_ptr<Session>& s : workspace_.sessions) {
            for (Pane* p : s->Panes()) {
                for (std::unique_ptr<Tab>& t : p->tabs) {
                    if (t->loadToken != l.token) continue;
                    t->listing = std::move(l.result);
                    t->loadToken = 0;
                    t->loaded = true;
                    t->marked.assign(t->listing.entries.size(), 0);
                    t->Rebuild();
                    // "Access denied" on a share is usually not a verdict but a
                    // question that has not been asked yet. The listing itself
                    // cannot ask it - a credential dialog raised from a worker
                    // thread would also fire while the address bar is being
                    // typed into - so name the key that does.
                    if (t.get() == workspace_.focusedTab() &&
                        t->listing.status == fs::Status::AccessDenied &&
                        !path::UncRoot(t->path).empty()) {
                        SetStatus(strings_.Format("ui.network_auth_hint",
                                                  { keymap_.ChordText(Cmd::ConnectNetwork) }));
                    }
                }
            }
        }
    }
    // The answer that just arrived may already be for the wrong folder - the
    // user kept typing while it was in flight - so ask again from here.
    RequestCompletion();
    EnsureCursorVisible();
    UpdateTitle();
    host_.Invalidate();
}

void App::UpdateTitle() {
    Tab* t = workspace_.focusedTab();
    if (!t) return;
    Session* s = workspace_.activeSession();
    // The build ends the caption rather than starting it: it is what someone
    // filing a report needs, and the folder is what everyone else reads.
    std::string title = DisplayPath(*t) + "  \xE2\x80\x94  " + (s ? s->name : std::string("Kite")) +
                        "  \xE2\x80\x94  Kite " + version::kDisplay;
    // Only touch the caption when it actually changed: SetWindowText on every
    // cursor keystroke makes the taskbar entry flicker.
    if (title == lastTitle_) return;
    lastTitle_ = std::move(title);
    host_.SetTitle(lastTitle_);
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

// Pulling a tab out of the bar and letting go outside the window.
//
// What travels is the folder, not the tab: the new window is another process
// (see Cmd::NewWindow), so the history and the view state stay behind and go
// with the tab we drop here. Nothing else can cross a process boundary.
bool App::DetachTabToNewWindow(Pane* pane, int index) {
    Session* session = workspace_.activeSession();
    if (!session || !pane) return false;
    if (index < 0 || index >= static_cast<int>(pane->tabs.size())) return false;

    // The only tab of the only pane has nowhere to go: the window it would open
    // is the window it is already in, and this one cannot be left empty. Say so
    // rather than opening a second copy of the same folder.
    const bool lastInPane = pane->tabs.size() <= 1;
    if (lastInPane && session->Panes().size() <= 1) {
        SetStatus(strings_.Get("ui.cannot_detach_last"));
        host_.Invalidate();
        return false;
    }

    const std::string path = pane->tabs[index]->path;
    if (!host_.OpenNewWindow(path)) {
        // Drop nothing when the window never opened - the tab is all there is.
        SetStatus(strings_.Get("ui.new_window_failed"));
        host_.Invalidate();
        return false;
    }

    // Not recorded in closedTabs: the tab moved, it did not close. Ctrl+Shift+T
    // would otherwise hand back a folder that is open in the new window.
    if (lastInPane) {
        session->ClosePane(pane);
    } else {
        pane->CloseTab(index, nullptr);
        if (Tab* t = pane->activeTab()) RequestLoad(*t);
    }
    dirty_ = true;
    SyncWatches();
    UpdateTitle();
    host_.Invalidate();
    return true;
}

void App::SetWindowActive(bool active) {
    if (windowActive_ == active) return;
    windowActive_ = active;
    // Coming back from another window is the one moment Kite can find out that
    // its cut was overtaken by someone else's copy.
    if (active) SyncCutMarks();
    // The focus ring and the cursor row are drawn from this, so what is on
    // screen is now wrong. Guarded above because the platform layer gets told
    // about activation far more often than it actually changes.
    host_.Invalidate();
}

void App::NavigateFocused(const std::string& raw) {
    Tab* t = workspace_.focusedTab();
    if (!t) return;
    const std::string target = ArchiveTarget(path::Normalize(raw));
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
        OpenTabIn(*pane, ArchiveTarget(path::Normalize(p)), NewTabAt(*pane), defaultView_);
        dirty_ = true;
        host_.Invalidate();
    } else {
        NavigateFocused(p);
    }
}

void App::OpenForwardedPaths(const std::vector<std::string>& paths) {
    if (paths.empty()) return;
    Pane* pane = workspace_.focusedPane();
    if (!pane) return;

    // Appended in the order they arrived, ignoring [ui] new_tab_position: these
    // are the second launch's command-line arguments, and someone who asked for
    // three folders side by side asked for that order. AfterCurrent would stack
    // them backwards, each one landing in front of the last.
    Tab* last = nullptr;
    for (const std::string& p : paths) {
        if (p.empty()) continue;
        last = OpenTabIn(*pane, path::Normalize(p), -1, defaultView_);
    }
    if (!last) return;

    pane->Activate(static_cast<int>(pane->tabs.size()) - 1);
    dirty_ = true;
    SyncWatches();
    UpdateTitle();
    host_.Invalidate();
}

void App::NavigateToParent(bool newTab) {
    Tab* t = workspace_.focusedTab();
    if (!t) return;
    const std::string parent = vfs::ParentOf(t->path);
    if (parent.empty()) return;
    const std::string leaving = path::FileName(t->path);
    OpenPath(parent, newTab);
    // Land the cursor on the folder we just came out of.
    if (Tab* now = workspace_.focusedTab()) now->pendingFocusName = leaving;
}

void App::ActivateEntry(int visibleIndex, bool newTab) {
    Tab* t = workspace_.focusedTab();
    if (!t) return;
    if (t->IsParentRow(visibleIndex)) {
        NavigateToParent(newTab);
        return;
    }
    const fs::Entry* entry = t->EntryAt(visibleIndex);
    if (!entry) return;

    const fs::Entry& e = *entry;
    const std::string full = fs::EntryPath(t->path, e);
    if (e.isDir()) {
        OpenPath(full, newTab);
        return;
    }
    // A shortcut to a folder is a folder as far as opening goes. Handing the
    // .lnk to the shell instead makes it open the target in Explorer, which is
    // the one thing Kite is here to replace - and the tab the user was standing
    // in stays where it was.
    //
    // Not in a virtual listing, though: what an item there holds is not
    // necessarily the path an operation acts on - a deleted .lnk is a hidden $R
    // copy - and following it would walk out of the bin into a live folder.
    std::string target;
    if (!vfs::IsVirtual(t->path) && ShortcutFolder(full, target)) {
        OpenPath(target, newTab);
        return;
    }
    // A zip is a folder too, for the same reason and with the same caveat: the
    // listing comes from the shell namespace, so this only applies to a file
    // that really is on disk. Inside a virtual listing the row's path may not
    // be the path anything acts on.
    if (!vfs::IsVirtual(t->path) && openArchives_ && vfs::IsArchiveName(full)) {
        OpenPath(vfs::ArchivePath(full), newTab);
        return;
    }
    // Never read the file here: a cloud placeholder must be hydrated by the
    // shell, on the shell's terms. The container travels along for the same
    // reason the context menu needs it: inside an archive the row's path is a
    // spelling with no file behind it, and only the folder can find the item.
    // Reported either way, because both answers are otherwise invisible. A
    // failure moves nothing and opens no dialog, so "nothing happened" is all
    // the user sees - which is exactly how the archive case came in. And what
    // opens out of an archive is a copy, which is worth saying before someone
    // edits it and expects the archive to have changed.
    const std::string container = ShellMenuContainer();
    if (!shell_.Open(container, full)) {
        ReportFailure("ui.open_failed", {});
    } else if (!container.empty()) {
        SetStatus(strings_.Get("ui.opened_copy"));
    }
}

bool App::IsCut(const std::string& path) const {
    // Sorted when it was filled: a cut of ten thousand files is one Ctrl+X, and
    // every visible row asks this question on every frame.
    return !cutPaths_.empty() &&
           std::binary_search(cutPaths_.begin(), cutPaths_.end(), path);
}

void App::ClearCutMarks() {
    if (cutPaths_.empty()) return;
    cutPaths_.clear();
    cutPaths_.shrink_to_fit();
    host_.Invalidate();
}

void App::SyncCutMarks() {
    if (cutPaths_.empty()) return;
    std::vector<std::string> onClipboard;
    bool cut = false;
    if (!shell_.GetClipboardFiles(onClipboard, &cut)) {
        // No files on the clipboard at all - text, an image, or nothing. Either
        // way the cut is over. A momentary failure to read looks the same from
        // here, and losing the dimming is the cheaper of the two mistakes.
        ClearCutMarks();
        return;
    }
    if (cut && onClipboard.size() == cutPaths_.size()) {
        // Case-folded because the answer comes back through the OS, which is
        // free to spell a path with the casing on disk rather than the casing
        // the listing used.
        std::vector<std::string> mine;
        std::vector<std::string> theirs;
        mine.reserve(cutPaths_.size());
        theirs.reserve(onClipboard.size());
        for (const std::string& p : cutPaths_) mine.push_back(utf8::ToLowerAscii(p));
        for (const std::string& p : onClipboard) theirs.push_back(utf8::ToLowerAscii(p));
        std::sort(mine.begin(), mine.end());
        std::sort(theirs.begin(), theirs.end());
        if (mine == theirs) return;
    }
    ClearCutMarks();
}

bool App::ShortcutFolder(const std::string& path, std::string& target) {
    // Asked by extension first so that opening an ordinary file costs nothing:
    // resolving means COM, a file read and a stat, per double-click.
    if (path::Extension(path) != "lnk") return false;
    std::string resolved;
    if (!shell_.ResolveShortcut(path, resolved) || resolved.empty()) return false;
    bool isDir = false;
    // A link to a file stays the shell's business: it may name a program, and
    // running it is exactly what the shell does with the .lnk itself.
    if (!fs_.Exists(resolved, &isDir) || !isDir) return false;
    target = resolved;
    return true;
}

std::string App::ArchiveTarget(const std::string& path) {
    if (!openArchives_ || vfs::IsVirtual(path) || !vfs::IsArchiveName(path)) return path;
    // Only now is it worth asking the disk. A folder can be called "backup.zip"
    // and opening it must stay an ordinary walk into an ordinary folder, but
    // the question costs a stat, so the extension is asked first - the same
    // order ShortcutFolder() follows for .lnk.
    bool isDir = false;
    if (!fs_.Exists(path, &isDir) || isDir) return path;
    return vfs::ArchivePath(path);
}

void App::RefreshFocused() {
    if (Tab* t = workspace_.focusedTab()) {
        RequestLoad(*t, true);
        // Overlays show a file's state (committed, synced, locked), and every
        // caller here has just changed something - including the shell menu,
        // which is where a commit or a sync is started from. Nothing tells us
        // when a handler changes its mind, so a refresh asks again.
        if (icons_ && shellIcons_) icons_->Invalidate();
        host_.Invalidate();
    }
}

// ---------------------------------------------------------------------------
// Drag & drop
// ---------------------------------------------------------------------------

bool App::IsValidDropTarget(const std::vector<std::string>& paths, const std::string& destDir) {
    if (destDir.empty() || paths.empty()) return false;
    // "PC" and the Recycle Bin are lists, not places: there is no folder under
    // them for a file to land in.
    if (vfs::IsVirtual(destDir)) return false;
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

    // Items already in the destination cannot be handed to the shell as they
    // are: a move there is a no-op, and a copy would collide with itself. The
    // copy becomes a duplicate under a new name, the same as pasting into the
    // folder the items were copied from.
    std::vector<std::string> sources;
    std::vector<std::string> here;
    for (const std::string& p : paths) {
        if (LivesIn(p, destDir)) {
            if (!move) here.push_back(p);
            continue;
        }
        sources.push_back(p);
    }
    if (sources.empty() && here.empty()) return false;

    if (!here.empty() && !DuplicateInPlace(here)) return false;
    if (sources.empty()) {
        RefreshTabsShowing(destDir);
        host_.Invalidate();
        return true;
    }

    const std::vector<bool> existedBefore = DestinationsExist(fs_, sources, destDir);

    std::string err;
    if (!fs_.CopyTo(sources, destDir, move, &err)) {
        ReportFailure(move ? "ui.move_failed" : "ui.copy_failed", err);
        return false;
    }
    RecordTransfer(sources, destDir, existedBefore, move);

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

// Hidden files, sort direction and folders-first all read the same way: flip it
// here, and let the next tab open with the same answer. One routine rather than
// three so that the second half - seeding defaultView_ - cannot be left out of
// one of them.
void App::ToggleViewFlag(Tab* tab, bool ViewState::* flag) {
    if (!tab) return;
    const bool value = !(tab->view.*flag);
    tab->view.*flag = value;
    defaultView_.*flag = value;
    RebuildFocused();
    dirty_ = true;
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
        t->ExtendTo(target);
    } else {
        // Marks deliberately survive a plain move. Space marks one row and steps
        // on; if walking to the next row wiped that mark, only neighbours could
        // ever be selected together. SelectNone (and any click on a row) clears.
        t->cursor = target;
        t->ResetAnchor();
    }
    EnsureCursorVisible();
    host_.Invalidate();
}

// ---------------------------------------------------------------------------
// Status
// ---------------------------------------------------------------------------

void App::SetStatus(const std::string& message) {
    statusMessage_ = message;
    statusUntilMs_ = plat::NowMs() + kStatusDurationMs;
    // A message nobody asked to be drawn is not a message. Most callers move
    // something else as well and would repaint anyway, but the ones that answer
    // without touching the listing - Ctrl+C, Ctrl+X, an empty undo - leave the
    // screen exactly as it was, so their answer used to sit there invisible
    // until an unrelated keystroke happened to redraw. The expiry timer has the
    // same dependency: the window only arms it while painting.
    host_.Invalidate();
}

bool App::ReadOnlyHere() {
    const Tab* t = workspace_.focusedTab();
    if (!t || !vfs::IsVirtual(t->path)) return false;
    // Kite does not write into the shell namespace. "PC" and the Recycle Bin
    // are not folders - there is nowhere in them to create a name, and the
    // paths their items carry are not the paths the shell operates on (a
    // deleted file's is its hidden $R copy, and renaming that would strand it).
    // The verbs that do work there are on the shell's own context menu.
    //
    // An archive gets its own sentence: it *is* a folder to look at, so being
    // told it is "a list, not a folder" answers a question nobody asked. What
    // is missing there is the writing, and the way out is the same menu.
    SetStatus(strings_.Get(vfs::ArchiveFileOf(t->path).empty() ? "ui.vfolder_read_only"
                                                              : "ui.archive_read_only"));
    return true;
}

std::string App::DisplayName(const Tab& tab) const {
    if (const char* key = vfs::LabelKey(tab.path)) return strings_.Get(key);
    return tab.title();
}

std::string App::EditablePath(const Tab& tab) const {
    // "virtual:" is Kite's word for a place, not a spelling anyone types. Inside
    // an archive the body is the shell's own ("C:\a\pack.zip\docs", which is what
    // Explorer prints too) and ArchiveTarget() reads it back, so the bar accepts
    // exactly what the bar shows. The three named places keep their own id: it
    // is not a path either, but it is the only spelling that gets back there.
    if (!vfs::ArchiveFileOf(tab.path).empty()) {
        return tab.path.substr(sizeof(vfs::kPrefix) - 1);
    }
    return tab.path;
}

std::string App::DisplayPath(const Tab& tab) const {
    if (!vfs::IsVirtual(tab.path)) return tab.path;
    // Answering with the folder's name would hide where inside the archive the
    // tab is standing - and unlike "PC", that has somewhere to be inside of.
    if (!vfs::ArchiveFileOf(tab.path).empty()) return EditablePath(tab);
    return DisplayName(tab);
}

void App::ReportFailure(const char* key, const std::string& detail) {
    std::string message = strings_.Get(key);
    if (!detail.empty()) message += "  (" + detail + ")";
    SetStatus(message);
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
    // A cursor parked on ".." has no selection to offer, so this falls through to
    // the folder being listed - and to the menu an empty-space right-click gives,
    // which is a different menu from the one the folder gets as an item.
    std::vector<std::string> paths = t->SelectionPaths();
    if (paths.empty()) {
        // Still offered inside the Recycle Bin, and worth having there: the
        // background menu answers for the bin itself, which is where "Empty
        // Recycle Bin" lives.
        ShowShellMenu({ t->path }, screenX, screenY, extended, true);
        return;
    }
    ShowShellMenu(paths, screenX, screenY, extended, false);
}

void App::ShowFolderContextMenu(bool extended) {
    Tab* t = workspace_.focusedTab();
    if (!t || t->path.empty()) return;
    // Deliberately ignores the selection: this command exists precisely because
    // a selected file otherwise hides the folder the user is looking at.
    int x = -1;
    int y = -1;
    CursorRowAnchor(x, y);
    // The item menu, which is the folder's own full menu - Open, Send to, Copy,
    // Delete, 7-Zip, Restore previous versions. The background menu was tried
    // here and is the wrong one: it answers for the space inside the folder, so
    // it carries New and Paste and nothing that acts on the folder itself, and
    // the result reads as a menu with most of its entries missing. What the
    // background menu was meant to keep out - "Git clone into this folder" -
    // turned out to follow CMF_EXTENDEDVERBS, not this choice, and is already
    // where Explorer keeps it: behind Shift.
    //
    // Inside the Recycle Bin the target here is the bin itself, and its item
    // menu is the useful one: "Empty Recycle Bin" and Properties.
    ShowShellMenu({ t->path }, x, y, extended, false);
}

void App::ShowBackgroundContextMenu(int screenX, int screenY, bool extended) {
    Tab* t = workspace_.focusedTab();
    if (!t || t->path.empty()) return;
    // The cursor row is not what was clicked, so it must not answer: pointing at
    // the empty space means the folder itself, as the space inside it.
    ShowShellMenu({ t->path }, screenX, screenY, extended, true);
}

void App::DoRestore() {
    Tab* t = workspace_.focusedTab();
    if (!t || t->path != vfs::kRecycleBin) {
        SetStatus(strings_.Get("ui.restore_not_trash"));
        return;
    }
    const std::vector<std::string> paths = t->SelectionPaths();
    if (paths.empty()) {
        SetStatus(strings_.Get("ui.no_selection"));
        return;
    }
    // Not pushed onto the undo stack. The inverse of "restore" is "delete", and
    // offering Ctrl+Z for that would put a file the user just rescued back in
    // the bin - through a keystroke they press to undo mistakes.
    if (!shell_.RestoreFromTrash(paths)) {
        ReportFailure("ui.restore_failed", {});
        return;
    }
    SetStatus(strings_.Format("ui.restored", { std::to_string(paths.size()) }));
    RefreshFocused();
}

// Only a virtual folder names itself here. A real folder's items are found by
// parsing their own paths, which is both correct and free; handing the shell a
// container would make it enumerate the folder again for every right-click.
std::string App::ShellMenuContainer() {
    const Tab* t = workspace_.focusedTab();
    if (!t || !vfs::IsVirtual(t->path)) return {};
    return t->path;
}

void App::ShowShellMenu(const std::vector<std::string>& paths, int screenX, int screenY,
                        bool extended, bool background) {
    if (paths.empty()) return;
    if (!shell_.ShowContextMenu(ShellMenuContainer(), paths, screenX, screenY, extended,
                                      background, theme_.dark)) {
        // The menu runs in a separate process; losing it means that process
        // could not be started, or a shell extension took it down. Say so rather
        // than letting a right-click look like it did nothing.
        SetStatus(strings_.Get("ui.shell_menu_failed"));
        return;
    }
    // The menu may have renamed, deleted or created something.
    RefreshFocused();
}

bool App::CursorRowAnchor(int& screenX, int& screenY) {
    Pane* pane = workspace_.focusedPane();
    if (!pane || pane->listArea.empty()) return false;
    const Tab* t = pane->activeTab();
    if (!t) return false;

    const RectF& area = pane->listArea;
    const float rowH = pane->rowHeight > 0.0f ? pane->rowHeight : theme_.rowHeight;

    // Bottom-left of the cursor row, which is where Windows itself drops the
    // menu for a focused list item. An off-screen or absent cursor falls back to
    // the top of the list rather than to a point outside the pane.
    float y = area.t;
    if (t->cursor >= 0 && t->cursor < static_cast<int>(t->visible.size())) {
        y = area.t + static_cast<float>(t->cursor + 1) * rowH - t->scroll;
    }
    y = std::clamp(y, area.t, area.b);

    return host_.ClientToScreen(area.l + kMenuAnchorIndent, y, screenX, screenY);
}

// ---------------------------------------------------------------------------
// Prompts
// ---------------------------------------------------------------------------

void App::BeginPrompt(PromptKind kind, const char* labelKey, const std::string& initial) {
    prompt_.kind = kind;
    prompt_.labelKey = labelKey;
    prompt_.text = initial;
    prompt_.SetCaret(initial.size());
    prompt_.pendingPaths.clear();
    composition_ = Composition{};
    complete_.Reset();
    completeToken_ = 0;
    completeRequested_.clear();
    // Primed but folded: the address bar opens on the folder that is already on
    // screen, and listing its children before a single key is pressed would put
    // a menu over the list for a question nobody asked.
    if (kind == PromptKind::Path) complete_.SetInput(initial);
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
    // 欄そのものが消えるので、そこで変換していた未確定文字列も行き場を失う。
    composition_ = Composition{};
    complete_.Reset();
    completeToken_ = 0;
    completeRequested_.clear();
    host_.Invalidate();
}

// ---------------------------------------------------------------------------
// IME composition
//
// The composition string is drawn by Kite, in the field it is being typed into,
// which is why it has to live here at all: the platform hands it over instead of
// letting the IME paint its own window over the client area.
// ---------------------------------------------------------------------------

bool App::acceptsText() const {
    // 設定画面は値を選ぶ画面で、打鍵は残らず飲み込む ─ 文字を入れる先が無い。
    if (settingsEditor_.visible()) return false;
    // 和音の取り込み中は、押された «キー» そのものが答え。変換に渡せば、
    // 割り当てたい打鍵が IME に食われて設定にならない。
    if (keyEditor_.visible()) return !keyEditor_.capturing();
    if (placePicker_.visible() || commandPalette_.visible()) return true;
    // 削除の確認は Yes/No で、あそこの文字列は件数であって編集するものではない。
    return prompt_.active() && !prompt_.isConfirm();
}

void App::SetComposition(std::string text, size_t caret, size_t targetBegin, size_t targetEnd) {
    if (text.empty()) {
        EndComposition();
        return;
    }
    composition_.text = std::move(text);
    const size_t size = composition_.text.size();
    composition_.caret = std::min(caret, size);
    composition_.targetBegin = std::min(targetBegin, size);
    composition_.targetEnd = std::min(targetEnd, size);
    host_.Invalidate();
}

void App::EndComposition() {
    if (!composition_.active()) return;
    composition_ = Composition{};
    host_.Invalidate();
}

// Feed the completion whatever is in the field now.
//
// Completion always finishes the tail of the text, so it only makes sense while
// the caret is sitting at the end of it. With the caret parked in the middle,
// candidates for the tail would be answering a question the user is not asking,
// and adopting one would overwrite what comes after the caret.
void App::SyncCompletion(bool open) {
    if (prompt_.kind != PromptKind::Path) return;
    if (prompt_.caret != prompt_.text.size()) {
        complete_.Close();
        return;
    }
    complete_.SetInput(prompt_.text);
    if (open) complete_.Open();
    RequestCompletion();
}

void App::RequestCompletion() {
    if (!loader_ || !complete_.wantsListing()) return;
    // Never from here. Listing a virtual folder starts the shell host, and this
    // runs on every keystroke - the address bar would spawn that process, and
    // keep asking it questions, to offer candidates nobody types by hand.
    if (vfs::IsVirtual(complete_.dir())) return;
    // One request per directory. Typing "C:\\Users\\ab" walks through three
    // prefixes of one folder, and re-listing it for each keystroke would put a
    // network share's latency on every letter.
    if (completeToken_ != 0 && completeRequested_ == complete_.dir()) return;
    completeRequested_ = complete_.dir();
    completeToken_ = loader_->Request(complete_.dir());
}

bool App::MoveCompletion(int delta) {
    if (prompt_.kind != PromptKind::Path) return false;
    if (!complete_.open()) {
        // The first Tab is what opens the list, so nothing has been enumerated
        // yet. Ask now; the keystroke that follows finds the candidates there.
        SyncCompletion(true);
    }
    // Still folded means the text is not something completion answers for - the
    // caret is not at the end. The candidates from before are stale, so taking
    // one now would overwrite the tail the caret is sitting in front of.
    if (!complete_.open()) return false;
    if (!complete_.Move(delta)) return false;
    prompt_.text = complete_.text();
    prompt_.SetCaret(prompt_.text.size());
    host_.Invalidate();
    return true;
}

void App::CancelInlineEdit() {
    if (!prompt_.isInline()) return;
    CancelPrompt();
}

void App::ChooseCompletion(int index) {
    if (prompt_.kind != PromptKind::Path) return;
    if (!complete_.Select(index)) return;
    prompt_.text = complete_.text();
    prompt_.SetCaret(prompt_.text.size());
    ApplyPrompt();
}

void App::ApplyPrompt() {
    Tab* t = workspace_.focusedTab();
    const PromptKind kind = prompt_.kind;
    const std::string text = prompt_.text;
    std::vector<std::string> pending = prompt_.pendingPaths;
    prompt_ = Prompt{};
    // 欄そのものが消えるので、そこで変換していた未確定文字列も行き場を失う。
    composition_ = Composition{};
    complete_.Reset();
    completeToken_ = 0;
    completeRequested_.clear();

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
                ReportFailure("ui.rename_failed", err);
            } else {
                undo_.Push({ UndoKind::Rename, { to }, { from } });
                RefreshFocused();
            }
            break;
        }

        // A folder and a file are created the same way and answered for the same
        // way; only which of the two the filesystem is asked for differs.
        case PromptKind::NewFolder:
        case PromptKind::NewFile: {
            if (!t || text.empty()) break;
            const std::string created = path::Join(t->path, text);
            const bool made = (kind == PromptKind::NewFolder)
                                  ? fs_.MakeDirectory(created, &err)
                                  : fs_.MakeFile(created, &err);
            if (!made) {
                ReportFailure("ui.create_failed", err);
            } else {
                undo_.Push({ UndoKind::Create, { created }, {} });
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
                ReportFailure("ui.delete_failed", err);
            } else {
                // ごみ箱へ入れたなら覚えるのは**消される前のパス**。ごみ箱の中で
                // 名乗る名前（隠された $R の写し）は誰も見ていないし、それを控える
                // には削除のたびにごみ箱を引き当て直すことになる ─ 一度も Ctrl+Z を
                // 押さない人までその代金を払う。
                //
                // 完全削除のほうは戻せないので印だけを積む。積まずに黙っていると、
                // 次の Ctrl+Z がそれを飛び越えて、消えたファイルはそのままに
                // その前の名前変更だけを巻き戻す。
                undo_.Push({ recycle ? UndoKind::Delete : UndoKind::Erase,
                             recycle ? pending : std::vector<std::string>{},
                             {} });
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

    // The field is a text field, so it answers the clipboard keys itself. Left
    // to the keymap they would reach Cmd::Copy and Cmd::Paste instead and act on
    // the listing behind the field - copying files while the caret sits in a
    // half-typed path is never what the keystroke meant.
    auto copyField = [&](bool cut) {
        if (CopyFieldToClipboard(shell_, prompt_, cut)) {
            syncFilter();
            SyncCompletion(true);
        }
        host_.Invalidate();
    };

    auto pasteField = [&] {
        if (!PasteIntoField(shell_, prompt_)) return;
        syncFilter();
        SyncCompletion(true);
        host_.Invalidate();
    };

    switch (chord.key) {
        case Key::Escape:
            // The candidate list goes first. Its whole point is to be dismissed
            // without losing what has been typed so far.
            if (complete_.open()) {
                complete_.Close();
                host_.Invalidate();
                return true;
            }
            CancelPrompt();
            return true;
        case Key::Enter:
            ApplyPrompt();
            return true;
        case Key::Tab:
        case Key::Down:
        case Key::Up: {
            if (prompt_.kind != PromptKind::Path) break;
            const bool back =
                (chord.key == Key::Up) || (chord.key == Key::Tab && (chord.mods & kModShift) != 0);
            MoveCompletion(back ? -1 : 1);
            return true;
        }
        case Key::Backspace:
            if (prompt_.isConfirm()) return true;
            break;
        case Key::Delete:
            if (prompt_.isConfirm()) return true;
            // Shift+Delete is the other spelling of cut, the way Shift+Insert is
            // the other spelling of paste.
            if ((chord.mods & kModShift) != 0) {
                copyField(true);
                return true;
            }
            break;
        case Key::C:
        case Key::X:
            if (chord.mods != kModCtrl || prompt_.isConfirm()) break;
            copyField(chord.key == Key::X);
            return true;
        case Key::V:
            if (chord.mods != kModCtrl || prompt_.isConfirm()) break;
            pasteField();
            return true;
        case Key::Insert:
            if (prompt_.isConfirm()) break;
            if (chord.mods == kModCtrl) {
                copyField(false);
            } else if (chord.mods == kModShift) {
                pasteField();
            }
            return true;
        default:
            break;
    }

    // What is left is the field itself - the caret, the selection, and the
    // one-character deletes. The counting lives in TextField, shared with the
    // choosers' filter box: two spellings of Shift+Left would be two fields.
    if (!prompt_.isConfirm()) {
        switch (prompt_.HandleKey(chord)) {
            case TextField::Edit::Changed:
                syncFilter();
                SyncCompletion(true);
                host_.Invalidate();
                return true;
            case TextField::Edit::Moved:
                SyncCompletion(false);
                host_.Invalidate();
                return true;
            case TextField::Edit::None:
                break;
        }
    }

    // Swallow everything else so a stray shortcut cannot fire mid-edit.
    return true;
}

bool App::OnChar(uint32_t cp) {
    // 設定画面は絞り込みを持たないが、打鍵は残らず飲み込む ─ 出しっぱなしの
    // 画面の裏でプロンプトが文字を受け取っては困る。
    if (settingsEditor_.visible()) return true;
    // Both choosers take a character the same way; a ">" typed at the front of
    // either one is a request for the other screen.
    const auto typeIntoPicker = [&](auto& picker) {
        if (!picker.HandleChar(cp)) return false;
        SyncPickerMode();
        host_.Invalidate();
        return true;
    };
    if (placePicker_.visible()) return typeIntoPicker(placePicker_);
    if (commandPalette_.visible()) return typeIntoPicker(commandPalette_);
    if (keyEditor_.visible()) {
        if (!keyEditor_.HandleChar(cp, strings_, keymap_)) return false;
        host_.Invalidate();
        return true;
    }
    if (!prompt_.active()) {
        // The keystroke that ran a command is spent. Consumed rather than passed
        // on, so a bound letter does not also beep at the window.
        if (swallowChar_) {
            swallowChar_ = false;
            return true;
        }
        return TypeAheadChar(cp);
    }
    if (prompt_.isConfirm()) return false;
    if (cp < 0x20 || cp == 0x7F) return false;

    std::string encoded;
    utf8::Encode(cp, encoded);
    prompt_.DeleteSelection();
    prompt_.text.insert(prompt_.caret, encoded);
    prompt_.SetCaret(prompt_.caret + encoded.size());

    if (prompt_.kind == PromptKind::Filter) {
        if (Tab* t = workspace_.focusedTab()) {
            t->filter = prompt_.text;
            t->Rebuild();
            EnsureCursorVisible();
        }
    }
    SyncCompletion(true);
    host_.Invalidate();
    return true;
}

bool App::TypeAheadChar(uint32_t cp) {
    Tab* t = workspace_.focusedTab();
    if (!t || t->visible.empty()) return false;

    const TabRows rows(*t);
    const TypeAhead::Jump jump = typeAhead_.Type(cp, rows, t->cursor, plat::NowMs());
    if (!jump.taken) return false;

    // Nothing else on screen says what is being searched for: no row disappears
    // the way the filter makes them, and a miss moves nothing at all. Without
    // this, a mistyped letter is indistinguishable from a dead keyboard.
    if (jump.row >= 0) {
        MoveCursor(jump.row, false, true);
        SetStatus(strings_.Format("ui.type_ahead", { typeAhead_.text() }));
    } else {
        // What was attempted, not what is kept: the letter that missed is
        // exactly the one the user needs to see, and it is the one the buffer
        // deliberately threw away.
        std::string tried = typeAhead_.text();
        utf8::Encode(cp, tried);
        SetStatus(strings_.Format("ui.type_ahead_no_match", { tried }));
    }
    return true;
}

bool App::OnKey(const Chord& chord) {
    // Whether a name is being typed right now has to be read before anything
    // below gets the chance to clear it.
    const bool typing = typeAhead_.active(plat::NowMs());
    swallowChar_ = false;

    if (settingsEditor_.visible()) {
        // 開いたキーがそのまま閉じるキーになる。ショートカットの設定へ抜ける道も
        // 開けておく ─ 設定画面から「キーの設定」へ行けないのは不親切なだけ。
        const Cmd c = keymap_.Lookup(chord);
        if (c == Cmd::ShowSettings || c == Cmd::ShowKeySettings || c == Cmd::ShowKeyHelp) {
            Execute(c);
            return true;
        }
        const bool consumed = settingsEditor_.HandleKey(chord, strings_);
        ApplyPendingSetting();
        host_.Invalidate();
        return consumed;
    }
    if (keyEditor_.visible()) {
        // Its own chord still toggles it shut, so the key that opened the screen
        // is also the key that leaves it. Everything else the editor decides -
        // including whether it is currently swallowing the whole keyboard to
        // capture a new binding.
        if (!keyEditor_.capturing() && keymap_.Lookup(chord) == Cmd::ShowKeySettings) {
            Execute(Cmd::ShowKeySettings);
            return true;
        }
        const bool consumed = keyEditor_.HandleKey(chord, keymap_, strings_);
        SaveKeysIfChanged();
        // Escape closes the screen from the inside; take the same exit as the
        // command would have, so the confirmation is not lost.
        if (!keyEditor_.visible()) CloseKeyEditor();
        host_.Invalidate();
        return consumed;
    }
    if (placePicker_.visible()) {
        // Its own chord still toggles it shut, so the key that opened the list is
        // also the key that leaves it - the same exit the other overlays have.
        //
        // The other mode's chord crosses over rather than being swallowed: the
        // two screens are one window, and Ctrl+Shift+P means "commands" wherever
        // it is pressed. The same reading VS Code gives it.
        const Cmd chosen = keymap_.Lookup(chord);
        if (chosen == Cmd::ShowPlaces || chosen == Cmd::ShowCommandPalette) {
            Execute(chosen);
            return true;
        }
        // The filter box is a text field, so the clipboard chords are its own -
        // and PickerList cannot read the clipboard from where it sits.
        if (PickerClipboardKey(shell_, placePicker_, chord)) {
            // A pasted ">" counts the same as a typed one.
            SyncPickerMode();
            host_.Invalidate();
            return true;
        }
        switch (placePicker_.HandleKey(chord)) {
            case PlacePicker::Action::Open: ChoosePlace(false); break;
            case PlacePicker::Action::OpenNewTab: ChoosePlace(true); break;
            case PlacePicker::Action::Close: placePicker_.Close(); break;
            case PlacePicker::Action::None: SyncPickerMode(); break;
        }
        // Everything reaching here was swallowed: picking a folder is not a state
        // to fire unrelated shortcuts from.
        host_.Invalidate();
        return true;
    }
    if (commandPalette_.visible()) {
        // Its own chord still toggles it shut, the same exit the other overlays
        // have, and the places list's chord crosses over the same way it does in
        // the other direction. Everything else the palette decides.
        const Cmd chosen = keymap_.Lookup(chord);
        if (chosen == Cmd::ShowCommandPalette || chosen == Cmd::ShowPlaces) {
            Execute(chosen);
            return true;
        }
        if (PickerClipboardKey(shell_, commandPalette_, chord)) {
            SyncPickerMode();
            host_.Invalidate();
            return true;
        }
        switch (commandPalette_.HandleKey(chord)) {
            case CommandPalette::Action::Run: RunPaletteCommand(); break;
            case CommandPalette::Action::Close: commandPalette_.Close(); break;
            // Backspacing the ">" away is the way back to the places list.
            case CommandPalette::Action::None: SyncPickerMode(); break;
        }
        // Swallowed like the bookmark list: a chord reaching the keymap from here
        // would run one command while another is being picked.
        host_.Invalidate();
        return true;
    }
    if (keyHelp_) {
        // Any key closes the cheat sheet except the one that re-opens it.
        const Cmd c = keymap_.Lookup(chord);
        if (c == Cmd::ShowKeyHelp) return true;
        keyHelp_ = false;
        host_.Invalidate();
        // The key that closed the sheet is spent on closing it: its character
        // must not fall through and move the cursor in the list behind.
        swallowChar_ = ProducesChar(chord);
        if (chord.key == Key::Escape) return true;
    }
    if (HandlePromptKey(chord)) return true;

    // While a name is being typed, these three belong to the string being typed
    // rather than to their commands - the same reading the prompt gives them.
    // Space is only swallowed here: the WM_CHAR it already queued is what puts
    // the blank into the buffer, since a space is a letter in plenty of names.
    if (typing && chord.mods == kModNone) {
        switch (chord.key) {
            case Key::Space:
                return true;
            case Key::Backspace:
                typeAhead_.Erase(plat::NowMs());
                SetStatus(typeAhead_.text().empty()
                              ? std::string()
                              : strings_.Format("ui.type_ahead", { typeAhead_.text() }));
                return true;
            case Key::Escape:
                // Escape drops what was typed first and clears the selection on
                // the second press, the way it does everywhere else in Kite.
                typeAhead_.Clear();
                SetStatus({});
                return true;
            default:
                break;
        }
    }

    const Cmd cmd = keymap_.Lookup(chord);
    if (cmd == Cmd::None) return false;
    // Doing anything else ends the name being typed, and the character this
    // chord is about to produce belongs to the command, not to the list.
    typeAhead_.Clear();
    swallowChar_ = ProducesChar(chord);
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

bool App::MoveSession(int from, int to) {
    if (!workspace_.ReorderSession(from, to)) return false;
    dirty_ = true;
    host_.Invalidate();
    return true;
}

void App::GotoBookmark(int index) {
    if (index < 0 || index >= static_cast<int>(workspace_.bookmarks.size())) return;
    NavigateFocused(workspace_.bookmarks[index].path);
}

// Every tab of the active session except the one being looked at.
//
// The pane index is the position in Session::Panes(), which is display order and
// stable for as long as the chooser is up - nothing can move a pane while an
// overlay is swallowing the keyboard. The names come from here rather than from
// the chooser because a tab's display name is App's business (a virtual folder is
// called whatever the listing brought back).
std::vector<PlacePicker::OpenTab> App::CollectOpenTabs() const {
    std::vector<PlacePicker::OpenTab> out;
    const Session* session = workspace_.activeSession();
    if (!session) return out;

    const std::vector<Pane*> panes = session->Panes();
    for (size_t p = 0; p < panes.size(); ++p) {
        const Pane* pane = panes[p];
        if (!pane) continue;
        const bool focused = (pane == session->focus);
        for (size_t t = 0; t < pane->tabs.size(); ++t) {
            const Tab* tab = pane->tabs[t].get();
            if (!tab) continue;
            // Where the cursor already is. "Go here" is not an answer to
            // "go where", and offering it would put the initial selection on a
            // row whose Enter does nothing.
            if (focused && static_cast<int>(t) == pane->active) continue;
            PlacePicker::OpenTab open;
            open.pane = static_cast<int>(p);
            open.tab = static_cast<int>(t);
            open.focused = focused;
            open.name = DisplayName(*tab);
            open.path = tab->path;
            out.push_back(std::move(open));
        }
    }
    return out;
}

bool App::OpenPlacePicker() {
    std::vector<PlacePicker::OpenTab> tabs = CollectOpenTabs();
    if (workspace_.bookmarks.empty() && tabs.empty() && roots_.empty()) {
        // An empty panel is the same as a key that does nothing, except it also
        // has to be dismissed. Say what is missing and how to add it - with no
        // bookmarks and nothing else open, that is the only thing left to say. On
        // a real machine the drives keep this from ever happening.
        SetStatus(strings_.Get("ui.goto_none"));
        return false;
    }
    CloseAllOverlays();
    const Tab* tab = workspace_.focusedTab();
    placePicker_.Open(strings_, keymap_, workspace_.bookmarks, tabs, roots_,
                      tab ? tab->path : std::string());
    return true;
}

void App::OpenCommandPalette() {
    CloseAllOverlays();
    // 開くたびに作り直す。ラベルは言語で変わり、和音は Ctrl+F1 で変わり、
    // 番号で指す 8 個に添える行き先は Ctrl+D で変わる。
    commandPalette_.Open(strings_, keymap_, workspace_.bookmarks);
}

// Which chooser the leading ">" asks for.
//
// The two screens are one window with two modes, the way VS Code's Ctrl+P is:
// type ">" and it is the command list, delete it and it is the places list. The
// marker lives in the field as a character, so the field says which mode it is in
// and Backspace is the way back - no extra key, no label of its own.
//
// Mixing the rows instead was the alternative, and it makes the frequent side pay:
// "d" would bring up file.delete next to Downloads, and a hundred-odd commands
// would dilute the handful of places anyone actually goes to (ROADMAP P3-12). A
// mode costs one character and keeps both lists honest.
void App::SyncPickerMode() {
    const std::string_view marker = CommandPalette::kPrefix;
    auto commandMode = [&](const std::string& text) {
        return text.compare(0, marker.size(), marker) == 0;
    };

    if (placePicker_.visible()) {
        // Copied before the swap: Open() resets the field, and what was typed has
        // to survive it - otherwise ">" eats whatever came before it.
        const TextField carried = placePicker_.field();
        if (!commandMode(carried.text)) return;
        placePicker_.Close();
        OpenCommandPalette();
        commandPalette_.filterField() = carried;
        commandPalette_.FilterEdited();
        host_.Invalidate();
        return;
    }

    if (!commandPalette_.visible()) return;
    const TextField carried = commandPalette_.field();
    if (commandMode(carried.text)) return;
    commandPalette_.Close();
    // Nowhere to go: the places list says so and stays shut rather than coming up
    // empty. Closed is the honest answer, and the status line carries the reason.
    if (!OpenPlacePicker()) {
        host_.Invalidate();
        return;
    }
    placePicker_.filterField() = carried;
    placePicker_.FilterEdited();
    host_.Invalidate();
}

void App::ChoosePlace(bool newTab) {
    // Copied before the screen goes, because the screen is what holds the answer
    // to "which one" - Close() drops the rows.
    const PlacePicker::Row* selected = placePicker_.selectedRow();
    const PlacePicker::Row row = selected ? *selected : PlacePicker::Row{};
    const bool hasRow = (selected != nullptr);

    // Closed first: what happens next may have something to say in the status line,
    // and a panel still covering the list would be answering a question the user
    // has already finished asking.
    placePicker_.Close();
    if (!hasRow) {
        host_.Invalidate();
        return;
    }

    if (row.kind == PlacePicker::Kind::Tab) {
        Session* session = workspace_.activeSession();
        const std::vector<Pane*> panes = session ? session->Panes() : std::vector<Pane*>{};
        if (row.pane >= 0 && row.pane < static_cast<int>(panes.size())) {
            Pane* pane = panes[static_cast<size_t>(row.pane)];
            // Activated before the focus moves, so the load FocusPane asks for is
            // the tab that was chosen rather than the one that pane was showing.
            pane->Activate(row.tab);
            FocusPane(pane);
        }
        host_.Invalidate();
        return;
    }

    if (!row.path.empty()) OpenPath(row.path, newTab);
    host_.Invalidate();
}

void App::RunPaletteCommand() {
    const Cmd cmd = commandPalette_.selectedCommand();
    // Closed before the command runs, not after: what runs may open a prompt on a
    // row, another overlay, or a shell menu, and every one of those would come up
    // underneath a panel that is no longer answering anything. The palette's own
    // command is not in the list, so this cannot loop back in here.
    commandPalette_.Close();
    if (cmd != Cmd::None) Execute(cmd);
    host_.Invalidate();
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
    const std::string dest = t->path;

    // What was copied from this very folder is a case of its own, and one the
    // shell cannot be handed as it stands: the name would collide with itself,
    // and the whole batch fails over it. Explorer's answer to that is a
    // duplicate under a new name; a cut, having nowhere to go, is a no-op.
    std::vector<std::string> incoming;
    std::vector<std::string> here;
    for (const std::string& p : paths) {
        if (LivesIn(p, dest)) {
            here.push_back(p);
        } else {
            incoming.push_back(p);
        }
    }

    bool ok = true;
    bool acted = false;
    if (!cut && !here.empty()) {
        ok = DuplicateInPlace(here);
        acted = ok;
    }

    if (!incoming.empty()) {
        const std::vector<bool> existedBefore = DestinationsExist(fs_, incoming, dest);
        std::string err;
        if (!fs_.CopyTo(incoming, dest, cut, &err)) {
            ReportFailure(cut ? "ui.move_failed" : "ui.copy_failed", err);
            ok = false;
        } else {
            RecordTransfer(incoming, dest, existedBefore, cut);
            acted = true;
        }
    }

    // Spent only if something actually moved. A cut whose every item is already
    // here did nothing, and the fade still has a folder to be pasted into.
    if (ok && acted) ClearCutMarks();
    RefreshFocused();
}

bool App::DuplicateInPlace(const std::vector<std::string>& sources) {
    if (sources.empty()) return true;

    std::vector<std::string> targets;
    for (const std::string& src : sources) {
        const std::string dir = path::Parent(src);
        // A folder keeps its whole name: ".2026" in "backup.2026" is nobody's
        // extension, the same call Cmd::Rename makes when it selects the stem.
        bool isDir = false;
        fs_.Exists(src, &isDir);

        std::string dest;
        // Bounded, because the answer comes from the disk: a folder that claims
        // every name is taken would otherwise spin here on the UI thread.
        for (int attempt = 0; attempt < 1000; ++attempt) {
            std::string candidate =
                path::Join(dir, path::DuplicateName(path::FileName(src), attempt, isDir));
            if (fs_.Exists(candidate)) continue;
            // Names chosen earlier in this same batch are not on disk yet, and
            // two items can reach for one: with "a_copy.txt" already there, both
            // it and "a.txt" arrive at "a_copy2.txt".
            bool planned = false;
            for (const std::string& taken : targets) {
                if (utf8::EqualsIgnoreCaseAscii(taken, candidate)) {
                    planned = true;
                    break;
                }
            }
            if (planned) continue;
            dest = std::move(candidate);
            break;
        }
        if (dest.empty()) {
            ReportFailure("ui.copy_failed", {});
            return false;
        }
        targets.push_back(std::move(dest));
    }

    std::string err;
    if (!fs_.CopyAs(sources, targets, &err)) {
        ReportFailure("ui.copy_failed", err);
        return false;
    }

    // The same boundary as any other transfer - what was not there before and is
    // there now. Every name here was picked because nothing held it, so all that
    // is left to ask is whether the copy arrived.
    UndoAction action;
    action.kind = UndoKind::Copy;
    for (const std::string& target : targets) {
        if (fs_.Exists(target)) action.targets.push_back(target);
    }
    const size_t made = action.targets.size();
    // Nothing arrived: the shell was told to go ahead and the user said no in its
    // own dialog. That is not a failure, and not something to count out loud.
    if (made == 0) return true;
    undo_.Push(std::move(action));

    // The listing says so too, but the new row can be anywhere in it - the name
    // is one Kite chose, so it is worth saying that it chose one.
    SetStatus(strings_.Format("ui.duplicated", { std::to_string(made) }));
    return true;
}

void App::RecordTransfer(const std::vector<std::string>& sources, const std::string& destDir,
                         const std::vector<bool>& existedBefore, bool move) {
    UndoAction action;
    action.kind = move ? UndoKind::Move : UndoKind::Copy;
    for (size_t i = 0; i < sources.size(); ++i) {
        // Already there before, so whatever sits at that name now is either the
        // file that was always there or one the shell overwrote - not something
        // this operation is entitled to move away or throw out.
        if (i < existedBefore.size() && existedBefore[i]) continue;
        const std::string dest = path::Join(destDir, path::FileName(sources[i]));
        if (!fs_.Exists(dest)) continue;
        action.targets.push_back(dest);
        action.origins.push_back(sources[i]);
    }
    if (action.targets.empty()) return;
    if (!move) action.origins.clear();  // a copy has nowhere to go back to
    undo_.Push(std::move(action));
}

void App::DoUndo() {
    const UndoAction* next = undo_.top();
    if (!next) {
        SetStatus(strings_.Get("ui.undo_empty"));
        return;
    }
    if (next->kind == UndoKind::Erase) {
        // The mark stays put rather than being popped: everything under it was
        // dropped when it went on, so stepping past it would reach nothing, and
        // saying so every time is the only honest answer left.
        SetStatus(strings_.Get("ui.undo_no_erase"));
        return;
    }

    const UndoAction action = *next;
    undo_.Pop();

    std::vector<std::string> refresh;
    auto note = [&refresh](const std::string& dir) {
        if (dir.empty()) return;
        for (const std::string& d : refresh) {
            if (utf8::EqualsIgnoreCaseAscii(d, dir)) return;
        }
        refresh.push_back(dir);
    };

    std::string err;
    bool acted = false;
    const char* messageKey = "ui.undone_move";

    switch (action.kind) {
        case UndoKind::Rename: {
            messageKey = "ui.undone_rename";
            if (action.targets.empty() || action.origins.empty()) break;
            // Renamed again from somewhere else, or the old name taken back in
            // the meantime: this entry no longer describes the disk.
            if (!fs_.Exists(action.targets[0]) || fs_.Exists(action.origins[0])) break;
            if (!fs_.Rename(action.targets[0], action.origins[0], &err)) break;
            note(path::Parent(action.origins[0]));
            note(path::Parent(action.targets[0]));
            acted = true;
            break;
        }

        case UndoKind::Create:
        case UndoKind::Copy: {
            messageKey = action.kind == UndoKind::Create ? "ui.undone_create" : "ui.undone_copy";
            std::vector<std::string> alive;
            for (const std::string& p : action.targets) {
                if (fs_.Exists(p)) alive.push_back(p);
            }
            if (alive.empty()) break;
            // To the Recycle Bin, never permanently. A folder created an hour
            // ago can have been filled since, and undo is not a delete key.
            if (!fs_.Delete(alive, true, &err)) break;
            for (const std::string& p : alive) note(path::Parent(p));
            acted = true;
            break;
        }

        case UndoKind::Move: {
            // Grouped by the folder each item came from: one call per folder,
            // not per file, or the shell's progress dialog opens and closes once
            // for every item that was moved.
            std::vector<std::pair<std::string, std::vector<std::string>>> groups;
            for (size_t i = 0; i < action.targets.size() && i < action.origins.size(); ++i) {
                if (!fs_.Exists(action.targets[i])) continue;
                const std::string parent = path::Parent(action.origins[i]);
                if (parent.empty()) continue;
                auto it = groups.begin();
                for (; it != groups.end(); ++it) {
                    if (utf8::EqualsIgnoreCaseAscii(it->first, parent)) break;
                }
                if (it == groups.end()) {
                    groups.push_back({ parent, {} });
                    it = groups.end() - 1;
                }
                it->second.push_back(action.targets[i]);
                note(path::Parent(action.targets[i]));
                note(parent);
            }
            // All of them or none of the message: a half-moved undo that reports
            // success leaves the user believing the folders are back as they
            // were, which is the one thing they must not believe.
            acted = !groups.empty();
            for (const auto& group : groups) {
                if (fs_.CopyTo(group.second, group.first, true, &err)) continue;
                acted = false;
                break;
            }
            break;
        }

        case UndoKind::Delete: {
            messageKey = "ui.undone_delete";
            if (action.targets.empty()) break;
            // Everything is handed over, including paths something else has
            // since put back: the shell is what knows whether the bin still
            // holds a matching item, and asking it twice about the same folder
            // is what looking first would cost.
            if (!shell_.RestoreDeleted(action.targets)) break;
            for (const std::string& p : action.targets) note(path::Parent(p));
            acted = true;
            break;
        }

        case UndoKind::Erase:
            break;  // handled above
    }

    for (const std::string& dir : refresh) RefreshTabsShowing(dir);
    // The entry is spent either way - an undo that cannot be applied is not one
    // that gets more applicable by being tried again.
    if (acted) {
        SetStatus(strings_.Get(messageKey));
    } else if (err.empty()) {
        SetStatus(strings_.Get("ui.undo_stale"));  // the disk moved on; nothing to undo
    } else {
        ReportFailure("ui.undo_failed", err);
    }
    host_.Invalidate();
}

}  // namespace kite
