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

    fs::ListResult List(const std::string& dir) override {
        ++listCalls;
        fs::ListResult result;
        auto it = dirs.find(dir);
        if (it == dirs.end()) {
            result.status = fs::Status::NotFound;
            return result;
        }
        result.entries = it->second;
        return result;
    }

    bool Exists(const std::string& path, bool* isDir) override {
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

    std::vector<fs::Root> QuickAccess() override {
        fs::Root root;
        root.path = home;
        root.label = "home";
        root.kind = fs::RootKind::Special;
        return { root };
    }

    bool MakeDirectory(const std::string& path, std::string*) override {
        AddDir(path);
        return true;
    }

    bool MakeFile(const std::string& path, std::string*) override {
        AddFile(kite::path::Parent(path), kite::path::FileName(path));
        return true;
    }

    bool Rename(const std::string& from, const std::string& to, std::string*) override {
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

    bool Delete(const std::vector<std::string>& paths, bool toRecycleBin, std::string*) override {
        deleteCalls.push_back(paths);
        deleteRecycle.push_back(toRecycleBin);
        for (const std::string& p : paths) {
            auto it = dirs.find(kite::path::Parent(p));
            if (it == dirs.end()) continue;
            const std::string leaf = kite::path::FileName(p);
            it->second.erase(std::remove_if(it->second.begin(), it->second.end(),
                                            [&](const fs::Entry& e) { return e.name == leaf; }),
                             it->second.end());
            dirs.erase(p);
        }
        return true;
    }

    bool CopyTo(const std::vector<std::string>& paths, const std::string& destDir, bool move,
                std::string*) override {
        copyCalls.push_back({ paths, destDir, move });
        for (const std::string& p : paths) {
            AddFile(destDir, kite::path::FileName(p));
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

    bool ShowContextMenu(const std::vector<std::string>& paths, int screenX, int screenY,
                         bool extended, bool background, bool dark) override {
        ++contextMenuCalls;
        lastContextMenuPaths = paths;
        lastContextMenuX = screenX;
        lastContextMenuY = screenY;
        lastContextMenuExtended = extended;
        lastContextMenuBackground = background;
        lastContextMenuDark = dark;
        return contextMenuShown;
    }
    bool Open(const std::string& path) override {
        opened.push_back(path);
        return true;
    }
    bool OpenWith(const std::string&) override { return true; }
    bool ShowProperties(const std::string&) override { return true; }
    bool RevealInExplorer(const std::string&) override { return true; }
    bool OpenTerminal(const std::string&) override { return true; }

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
// Keeps the filled rectangles and throws the rest away. What a UI test can ask
// about a frame is "was this painted, where, in what colour" - text and glyph
// shapes are the renderer's business, not the layout's.
// ---------------------------------------------------------------------------

class FakeRenderer final : public ui::Renderer {
public:
    struct Fill {
        RectF rect;
        Color color;
    };

    std::vector<Fill> fills;
    SizeF size{ 1200.0f, 800.0f };

    void PushClip(const RectF&) override {}
    void PopClip() override {}
    void FillRect(const RectF& r, const Color& c) override { fills.push_back({ r, c }); }
    void FillRoundRect(const RectF& r, float, const Color& c) override { fills.push_back({ r, c }); }
    void StrokeRect(const RectF&, const Color&, float) override {}
    void DrawLine(float, float, float, float, const Color&, float) override {}
    void FillTriangle(PointF, PointF, PointF, const Color&) override {}
    void DrawIcon(uint32_t, const RectF&) override {}
    void DrawText(std::string_view, const RectF&, const Color&, ui::FontRole,
                  ui::TextAlign) override {}
    // Proportional enough for layout code to behave as it would on screen; the
    // exact number only has to be stable.
    float MeasureText(std::string_view utf8, ui::FontRole) override {
        return static_cast<float>(utf8.size()) * 7.0f;
    }
    float LineHeight(ui::FontRole) override { return 16.0f; }
    SizeF surfaceSize() const override { return size; }

    void Clear() { fills.clear(); }

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

        bool settled = true;
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
