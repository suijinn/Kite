// Kite tests - stand-ins for everything the core talks to across the OS seam.
#pragma once

#include <algorithm>
#include <chrono>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include "core/app/App.h"
#include "core/app/Host.h"
#include "core/base/PathUtil.h"
#include "core/fs/DirectoryWatcher.h"
#include "core/fs/FileSystem.h"
#include "ui/Renderer.h"

namespace kite::test {

std::map<std::string, std::string>& FakeFiles();
uint64_t& FakeClockMs();

// Paths at or under this prefix refuse to be written. Empty means everything is
// writable, which is what ResetFakePlatform() puts it back to.
std::string& FakeReadOnlyPrefix();

void ResetFakePlatform();

// ---------------------------------------------------------------------------
// Filesystem
// ---------------------------------------------------------------------------

class FakeFileSystem final : public fs::IFileSystem {
public:
    // Directory path -> its direct children.
    std::map<std::string, std::vector<fs::Entry>> dirs;

    std::string home = "C:\\home";
    std::string config = "C:\\home\\config";

    // Recorded calls, for assertions.
    struct CopyCall {
        std::vector<std::string> paths;
        std::string destDir;
        bool move = false;
    };
    std::vector<CopyCall> copyCalls;
    std::vector<std::vector<std::string>> deleteCalls;
    std::vector<bool> deleteRecycle;
    int listCalls = 0;

    void AddDir(const std::string& path) {
        dirs[path];  // ensure it exists, even if empty
        const std::string parent = kite::path::Parent(path);
        if (parent.empty() || dirs.find(parent) == dirs.end()) return;
        fs::Entry entry;
        entry.name = kite::path::FileName(path);
        entry.attrs = fs::Attr::Directory;
        dirs[parent].push_back(entry);
    }

    void AddFile(const std::string& dir, const std::string& name, uint64_t size = 0,
                 int64_t mtime = 0, fs::Attr extra = fs::Attr::None) {
        fs::Entry entry;
        entry.name = name;
        entry.size = size;
        entry.mtime = mtime;
        entry.attrs = extra;
        dirs[dir].push_back(entry);
    }

    // Folders that answer "access denied" instead of listing - what a share
    // wanting a logon looks like from here.
    std::vector<std::string> denied;

    // Paths every write refuses to touch: what a file another process holds
    // open, or one on a drive that was pulled out, looks like from here.
    std::vector<std::string> locked;

    // What the OS said about it. Left empty on purpose in some tests: ErrorText()
    // has no wording for every code, so "failed with nothing to say" is a real
    // state the status line has to survive.
    std::string lockedMessage;

    bool Locked(const std::string& path) const {
        return std::find(locked.begin(), locked.end(), path) != locked.end();
    }

    bool Refuse(const std::string& path, std::string* err) const {
        if (!Locked(path)) return false;
        if (err) *err = lockedMessage;
        return true;
    }

    fs::ListResult List(const std::string& dir) override {
        ++listCalls;
        fs::ListResult result;
        if (std::find(denied.begin(), denied.end(), dir) != denied.end()) {
            result.status = fs::Status::AccessDenied;
            return result;
        }
        auto it = dirs.find(dir);
        if (it == dirs.end()) {
            result.status = fs::Status::NotFound;
            return result;
        }
        result.entries = it->second;
        return result;
    }

    bool Exists(const std::string& path, bool* isDir = nullptr) override {
        if (dirs.count(path)) {
            if (isDir) *isDir = true;
            return true;
        }
        const std::string parent = kite::path::Parent(path);
        const std::string leaf = kite::path::FileName(path);
        auto it = dirs.find(parent);
        if (it == dirs.end()) return false;
        for (const fs::Entry& e : it->second) {
            if (e.name == leaf) {
                if (isDir) *isDir = e.isDir();
                return true;
            }
        }
        return false;
    }

    std::vector<fs::Root> Roots() override {
        fs::Root root;
        root.path = "C:\\";
        root.label = "Fake (C:)";
        root.kind = fs::RootKind::Fixed;
        root.totalBytes = 1000;
        root.freeBytes = 400;
        return { root };
    }

    std::string HomeDir() override { return home; }
    std::string ConfigDir() override { return config; }

    // Paths the sidebar's quick access section lists, in enumeration order.
    // Set it to stand in for the known folders Windows hands back - the real one
    // returns them in its own order every time, which is what any saved order
    // has to be laid back over.
    std::vector<std::string> quickAccess;

    std::vector<fs::Root> QuickAccess() override {
        std::vector<fs::Root> out;
        for (const std::string& path : quickAccess.empty() ? std::vector<std::string>{ home }
                                                           : quickAccess) {
            fs::Root root;
            root.path = path;
            root.label = (path == home) ? "home" : kite::path::FileName(path);
            root.kind = fs::RootKind::Special;
            out.push_back(root);
        }
        return out;
    }

    bool MakeDirectory(const std::string& path, std::string* err) override {
        if (Refuse(path, err)) return false;
        AddDir(path);
        return true;
    }

    bool MakeFile(const std::string& path, std::string* err) override {
        if (Refuse(path, err)) return false;
        AddFile(kite::path::Parent(path), kite::path::FileName(path));
        return true;
    }

    bool Rename(const std::string& from, const std::string& to, std::string* err) override {
        if (Refuse(from, err)) return false;
        const std::string parent = kite::path::Parent(from);
        auto it = dirs.find(parent);
        if (it == dirs.end()) return false;
        for (fs::Entry& e : it->second) {
            if (e.name == kite::path::FileName(from)) {
                e.name = kite::path::FileName(to);
                return true;
            }
        }
        return false;
    }

    void Remove(const std::string& path) {
        auto it = dirs.find(kite::path::Parent(path));
        if (it != dirs.end()) {
            const std::string leaf = kite::path::FileName(path);
            it->second.erase(std::remove_if(it->second.begin(), it->second.end(),
                                            [&](const fs::Entry& e) { return e.name == leaf; }),
                             it->second.end());
        }
        dirs.erase(path);
    }

    bool Delete(const std::vector<std::string>& paths, bool toRecycleBin,
                std::string* err) override {
        for (const std::string& p : paths) {
            if (Refuse(p, err)) return false;
        }
        deleteCalls.push_back(paths);
        deleteRecycle.push_back(toRecycleBin);
        for (const std::string& p : paths) Remove(p);
        return true;
    }

    // A move really vacates the source and a collision really replaces, so
    // "what is at the destination afterwards" is a question worth asking here -
    // undo decides what it may touch by asking it before and after.
    bool CopyTo(const std::vector<std::string>& paths, const std::string& destDir, bool move,
                std::string* err) override {
        if (Refuse(destDir, err)) return false;
        for (const std::string& p : paths) {
            if (Refuse(p, err)) return false;
        }
        copyCalls.push_back({ paths, destDir, move });
        for (const std::string& p : paths) {
            const std::string leaf = kite::path::FileName(p);
            bool isDir = false;
            const bool sourceIsDir = Exists(p, &isDir) && isDir;
            Remove(kite::path::Join(destDir, leaf));
            if (sourceIsDir) {
                AddDir(kite::path::Join(destDir, leaf));
            } else {
                AddFile(destDir, leaf);
            }
            if (move) Remove(p);
        }
        return true;
    }
};

// ---------------------------------------------------------------------------
// Shell
// ---------------------------------------------------------------------------

class FakeShell final : public IShellIntegration {
public:
    std::vector<std::string> opened;
    std::vector<std::string> clipboardText;
    std::vector<std::string> clipboardFiles;
    bool clipboardCut = false;
    int contextMenuCalls = 0;
    bool lastContextMenuExtended = false;
    bool lastContextMenuBackground = false;
    bool lastContextMenuDark = false;
    std::vector<std::string> lastContextMenuPaths;
    int lastContextMenuX = 0;
    int lastContextMenuY = 0;
    // Set to false to act like a shell host that could not be started, or one
    // that died with a faulting extension inside it.
    bool contextMenuShown = true;
    std::string lastContextMenuFolder;

    bool ShowContextMenu(const std::string& folder, const std::vector<std::string>& paths,
                         int screenX, int screenY, bool extended, bool background,
                         bool dark) override {
        ++contextMenuCalls;
        lastContextMenuFolder = folder;
        lastContextMenuPaths = paths;
        lastContextMenuX = screenX;
        lastContextMenuY = screenY;
        lastContextMenuExtended = extended;
        lastContextMenuBackground = background;
        lastContextMenuDark = dark;
        return contextMenuShown;
    }

    // Restore reports how it was called and whether it claims to have worked.
    // Nothing is moved: the fake filesystem has no bin to move things out of,
    // and what the tests check is who asked for what.
    std::vector<std::vector<std::string>> restoreCalls;
    bool restoreSucceeds = true;

    bool RestoreFromTrash(const std::vector<std::string>& paths) override {
        restoreCalls.push_back(paths);
        return restoreSucceeds;
    }

    // Undo of a delete names its targets by the path they had before, so the
    // two are recorded apart: which one was used is part of what is checked.
    std::vector<std::vector<std::string>> restoreDeletedCalls;
    bool restoreDeletedSucceeds = true;

    bool RestoreDeleted(const std::vector<std::string>& originalPaths) override {
        restoreDeletedCalls.push_back(originalPaths);
        return restoreDeletedSucceeds;
    }
    bool Open(const std::string& path) override {
        opened.push_back(path);
        return true;
    }
    bool OpenWith(const std::string&) override { return true; }
    bool ShowProperties(const std::string&) override { return true; }
    bool RevealInExplorer(const std::string&) override { return true; }
    // Which folder a terminal was asked for is the whole point of the command,
    // so it is recorded; the switch lets a test see what a machine with no
    // console does.
    std::vector<std::string> terminalDirs;
    bool terminalSucceeds = true;

    bool OpenTerminal(const std::string& dir) override {
        terminalDirs.push_back(dir);
        return terminalSucceeds;
    }

    bool SetClipboardText(const std::string& utf8) override {
        clipboardText.push_back(utf8);
        return true;
    }
    bool SetClipboardFiles(const std::vector<std::string>& paths, bool cut) override {
        clipboardFiles = paths;
        clipboardCut = cut;
        return true;
    }
    bool GetClipboardFiles(std::vector<std::string>& paths, bool* cut) override {
        if (clipboardFiles.empty()) return false;
        paths = clipboardFiles;
        if (cut) *cut = clipboardCut;
        return true;
    }

    // What a paste finds. Kept apart from clipboardText, which records what Kite
    // put there - a test that seeds one and asserts on the other would pass
    // without either side working.
    std::string clipboardTextIn;
    bool hasClipboardText = false;

    bool GetClipboardText(std::string& utf8) override {
        if (!hasClipboardText) return false;
        utf8 = clipboardTextIn;
        return true;
    }

    void SetIncomingText(const std::string& text) {
        clipboardTextIn = text;
        hasClipboardText = true;
    }

    // Places a sign-in was asked for, in order, and whether the OS said yes.
    std::vector<std::string> connectCalls;
    bool connectSucceeds = true;

    bool ConnectNetwork(const std::string& uncRoot, std::string* err) override {
        connectCalls.push_back(uncRoot);
        if (connectSucceeds) return true;
        if (err) *err = "refused";
        return false;
    }
};

// ---------------------------------------------------------------------------
// Window
// ---------------------------------------------------------------------------

class FakeHost final : public IHost {
public:
    int invalidateCount = 0;
    int closeCount = 0;
    std::string title;
    std::vector<std::string> lastDrag;
    // Folders a new window was asked for, in order.
    std::vector<std::string> newWindows;
    // Set to false to act like an exe that could not be launched again.
    bool canOpenNewWindow = true;
    // A window that has never been shown cannot map coordinates; set this to
    // false to exercise the "no anchor available" path.
    bool canMapCoordinates = true;
    // The offset a client point picks up on the way to screen coordinates.
    // Non-zero so a test cannot pass by leaving the client point untouched.
    int screenOriginX = 100;
    int screenOriginY = 200;
    float lastClientX = 0.0f;
    float lastClientY = 0.0f;

    void Invalidate() override { ++invalidateCount; }
    void SetTitle(const std::string& utf8) override { title = utf8; }
    void Close() override { ++closeCount; }
    bool OpenNewWindow(const std::string& dir) override {
        newWindows.push_back(dir);
        return canOpenNewWindow;
    }
    void SetImePosition(float, float) override {}
    void SetCursorShape(int) override {}
    bool ClientToScreen(float x, float y, int& screenX, int& screenY) override {
        lastClientX = x;
        lastClientY = y;
        if (!canMapCoordinates) return false;
        screenX = screenOriginX + static_cast<int>(x);
        screenY = screenOriginY + static_cast<int>(y);
        return true;
    }
    bool BeginFileDrag(const std::vector<std::string>& paths) override {
        lastDrag = paths;
        return true;
    }
    void Wake() override {}
};

// ---------------------------------------------------------------------------
// Renderer
//
// Keeps the filled rectangles and where each run of text ended up, and throws
// the rest away. Glyph shapes are the renderer's business - but the box the ink
// lands in is the layout's, and two of them on the same spot is a bug no colour
// check can see.
// ---------------------------------------------------------------------------

class FakeRenderer final : public ui::Renderer {
public:
    struct Fill {
        RectF rect;
        Color color;
        size_t seq = 0;  ///< 何番目の描画か。重なりは前後関係でしか判定できない
    };

    /// One shell icon. The pixels are the platform's business, so all that is kept
    /// is which identifier went where - enough to tell a row that got the real
    /// icon from a row that fell back to a drawn glyph.
    struct Icon {
        RectF rect;
        uint32_t id = 0;
        size_t seq = 0;
    };

    /// One run of text, with the box its glyphs actually cover - the layout rect
    /// trimmed to the width of the string and pushed to whichever end the
    /// alignment asked for. A right-aligned line in a wide rect leaves most of
    /// that rect empty, and nothing is overlapping when something else is drawn
    /// there.
    struct Text {
        RectF ink;
        RectF rect;
        std::string text;
        ui::FontRole role = ui::FontRole::Ui;
        size_t seq = 0;
    };

    std::vector<Fill> fills;
    std::vector<Text> texts;
    std::vector<Icon> icons;
    SizeF size{ 1200.0f, 800.0f };

    void PushClip(const RectF&) override {}
    void PopClip() override {}
    void FillRect(const RectF& r, const Color& c) override { fills.push_back({ r, c, seq_++ }); }
    void FillRoundRect(const RectF& r, float, const Color& c) override {
        fills.push_back({ r, c, seq_++ });
    }
    void StrokeRect(const RectF&, const Color&, float) override {}
    void DrawLine(float, float, float, float, const Color&, float) override {}
    void FillTriangle(PointF, PointF, PointF, const Color&) override {}
    void DrawIcon(uint32_t id, const RectF& r) override { icons.push_back({ r, id, seq_++ }); }
    void DrawText(std::string_view utf8, const RectF& r, const Color& c, ui::FontRole role,
                  ui::TextAlign align) override {
        // The same conditions the real renderer draws nothing under.
        if (utf8.empty() || c.a <= 0.0f || r.w() <= 1.0f) return;
        const float w = std::min(MeasureText(utf8, role), r.w());
        RectF ink = { r.l, r.t, r.l + w, r.b };
        if (align == ui::TextAlign::Right) ink = { r.r - w, r.t, r.r, r.b };
        if (align == ui::TextAlign::Center) {
            const float mid = (r.l + r.r) * 0.5f;
            ink = { mid - w * 0.5f, r.t, mid + w * 0.5f, r.b };
        }
        texts.push_back({ ink, r, std::string(utf8), role, seq_++ });
    }
    // Proportional enough for layout code to behave as it would on screen; the
    // exact number only has to be stable.
    float MeasureText(std::string_view utf8, ui::FontRole) override {
        return static_cast<float>(utf8.size()) * 7.0f;
    }
    float LineHeight(ui::FontRole) override { return 16.0f; }
    SizeF surfaceSize() const override { return size; }

    void Clear() {
        fills.clear();
        texts.clear();
        icons.clear();
        seq_ = 0;
    }

    /// The icon drawn over a point, if any - which is how a test asks "did this row
    /// get a shell icon".
    const Icon* IconAt(float x, float y) const {
        for (const Icon& i : icons) {
            if (i.rect.contains(x, y)) return &i;
        }
        return nullptr;
    }

    /// Everything drawn after the last fill of this colour - the way to ask about
    /// one overlay on its own, since the frame behind it was painted first and is
    /// still sitting under the same coordinates.
    std::vector<Text> TextsAfterFill(const Color& c) const {
        size_t after = 0;
        for (const Fill& f : fills) {
            if (SameColor(f.color, c)) after = f.seq;
        }
        std::vector<Text> out;
        for (const Text& t : texts) {
            if (t.seq > after) out.push_back(t);
        }
        return out;
    }

    // Every fill of this colour that covers the given point.
    std::vector<Fill> FillsAt(const Color& c, float x, float y) const {
        std::vector<Fill> out;
        for (const Fill& f : fills) {
            if (SameColor(f.color, c) && f.rect.contains(x, y)) out.push_back(f);
        }
        return out;
    }

    int CountFills(const Color& c) const {
        int n = 0;
        for (const Fill& f : fills) {
            if (SameColor(f.color, c)) ++n;
        }
        return n;
    }

    static bool SameColor(const Color& a, const Color& b) {
        return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
    }

    /// Half-open on both axes: two boxes that share an edge are next to each
    /// other, not on top of each other. The tolerance is there because a stack of
    /// rows is built by repeated addition, so the bottom of one and the top of
    /// the next differ in the last bit rather than being equal.
    static bool Overlaps(const RectF& a, const RectF& b) {
        const float slack = 0.01f;
        return a.l + slack < b.r && b.l + slack < a.r && a.t + slack < b.b && b.t + slack < a.b;
    }

private:
    size_t seq_ = 0;
};

// ---------------------------------------------------------------------------
// Icon provider
// ---------------------------------------------------------------------------

// Answers on the first ask, unlike the real one, which returns 0 and queues the
// path for a worker. Tests here are about who gets asked and what gets drawn, and
// a provider that needs a second frame to answer only adds a pump to every one of
// them. Each distinct path gets its own identifier, so "these two rows drew the
// same icon" is a question that can be asked.
class FakeIconProvider final : public IIconProvider {
public:
    std::vector<std::string> asked;  ///< 渡されたパス。重複も順序もそのまま
    int invalidateCalls = 0;

    uint32_t IconFor(const std::string& path) override {
        asked.push_back(path);
        const auto it = ids_.find(path);
        if (it != ids_.end()) return it->second;
        const uint32_t id = static_cast<uint32_t>(ids_.size()) + 1;
        ids_[path] = id;
        return id;
    }

    void Invalidate() override {
        ++invalidateCalls;
        ids_.clear();
    }

    bool WasAsked(const std::string& path) const {
        return std::find(asked.begin(), asked.end(), path) != asked.end();
    }

private:
    std::map<std::string, uint32_t> ids_;
};

// ---------------------------------------------------------------------------
// Directory watcher
// ---------------------------------------------------------------------------

class FakeWatcher final : public fs::IDirectoryWatcher {
public:
    std::map<uint64_t, std::string> active;
    std::vector<fs::ChangeEvent> queued;

    void Watch(uint64_t watchId, const std::string& path) override { active[watchId] = path; }
    void Unwatch(uint64_t watchId) override { active.erase(watchId); }
    void Drain(std::vector<fs::ChangeEvent>& out) override {
        for (fs::ChangeEvent& e : queued) out.push_back(e);
        queued.clear();
    }

    void Emit(uint64_t watchId, const std::string& path) { queued.push_back({ watchId, path }); }
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// DirectoryLoader really uses worker threads, so tests pump the same way the
// window does rather than pretending the load is synchronous.
inline bool PumpUntilSettled(App& app, int timeoutMs = 4000) {
    for (int elapsed = 0; elapsed <= timeoutMs; elapsed += 2) {
        app.PumpLoader();

        // The address bar's candidates come through the same loader, so a test
        // that types and then checks the offer has to wait for that too.
        bool settled = !app.pathComplete().wantsListing();
        if (Session* session = app.workspace().activeSession()) {
            for (Pane* pane : session->Panes()) {
                for (const std::unique_ptr<Tab>& tab : pane->tabs) {
                    if (tab->loadToken != 0) settled = false;
                }
            }
        }
        if (settled) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
}

// A filesystem with a small, predictable tree already in it.
inline void PopulateStandardTree(FakeFileSystem& fs) {
    fs.dirs["C:\\home"];
    fs.AddDir("C:\\home\\alpha");
    fs.AddDir("C:\\home\\beta");
    fs.AddDir("C:\\home\\alpha\\nested");
    fs.AddFile("C:\\home", "notes.txt", 120, 1000);
    fs.AddFile("C:\\home", "image2.png", 4096, 2000);
    fs.AddFile("C:\\home", "image10.png", 2048, 3000);
    fs.AddFile("C:\\home", ".hidden", 10, 500, fs::Attr::Hidden);
    fs.AddFile("C:\\home\\alpha", "inner.md", 64, 1500);
}

}  // namespace kite::test
