// Kite - the controller. Owns all state, executes every command, and knows
// nothing about pixels or Win32. The UI layer reads from it and feeds it
// events; the platform layer supplies the filesystem, shell and window.
#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/app/Host.h"
#include "core/base/Ini.h"
#include "core/fs/DirectoryLoader.h"
#include "core/fs/DirectoryWatcher.h"
#include "core/fs/FileSystem.h"
#include "core/i18n/Strings.h"
#include "core/input/Commands.h"
#include "core/input/KeyMap.h"
#include "core/model/Workspace.h"
#include "core/theme/Theme.h"

namespace kite {

enum class PromptKind : uint8_t {
    None,
    Path,
    Filter,
    Rename,
    NewFolder,
    NewFile,
    SessionName,
    ConfirmDelete,
    ConfirmDeletePermanent,
};

struct Prompt {
    PromptKind kind = PromptKind::None;
    std::string labelKey;
    std::string text;
    size_t caret = 0;  // byte offset into `text`
    std::vector<std::string> pendingPaths;

    bool active() const { return kind != PromptKind::None; }
    bool isConfirm() const {
        return kind == PromptKind::ConfirmDelete || kind == PromptKind::ConfirmDeletePermanent;
    }
};

struct WindowPlacement {
    int x = -1, y = -1, w = 1180, h = 720;
    bool maximized = false;
};

class App {
public:
    // `watcher` is optional: without one Kite simply does not auto-refresh,
    // which is also how the unit tests run it.
    App(fs::IFileSystem& filesystem, IShellIntegration& shell, IHost& host,
        fs::IDirectoryWatcher* watcher = nullptr);
    ~App();

    App(const App&) = delete;
    App& operator=(const App&) = delete;

    bool Init(const std::vector<std::string>& startPaths);
    void Shutdown();

    // --- input ---------------------------------------------------------------
    // Returns true when the key was consumed.
    bool OnKey(const Chord& chord);
    bool OnChar(uint32_t codepoint);
    void Execute(Cmd cmd);

    // --- background work -----------------------------------------------------
    void PumpLoader();

    // --- accessors used by the UI layer --------------------------------------
    Workspace& workspace() { return workspace_; }
    const Workspace& workspace() const { return workspace_; }
    const Theme& theme() const { return theme_; }
    const Strings& strings() const { return strings_; }
    const KeyMap& keys() const { return keymap_; }
    fs::IFileSystem& filesystem() { return fs_; }
    IShellIntegration& shell() { return shell_; }
    IHost& host() { return host_; }

    const Prompt& prompt() const { return prompt_; }
    Prompt& prompt() { return prompt_; }
    bool keyHelpVisible() const { return keyHelp_; }
    bool sidebarVisible() const { return sidebarVisible_; }
    const std::string& statusMessage() const { return statusMessage_; }
    bool statusExpired() const;

    const std::vector<fs::Root>& roots() const { return roots_; }
    const std::vector<fs::Root>& quickAccess() const { return quickAccess_; }

    const WindowPlacement& placement() const { return placement_; }
    void SetPlacement(const WindowPlacement& p) { placement_ = p; }

    // --- operations the UI triggers directly ---------------------------------
    void FocusPane(Pane* pane);
    void OpenPath(const std::string& path, bool newTab);
    void NavigateFocused(const std::string& path);
    void ActivateEntry(int visibleIndex, bool newTab);
    void EnsureCursorVisible();
    void SetStatus(const std::string& message);
    void RefreshFocused();
    void ShowContextMenuAt(int screenX, int screenY, bool extended);
    void ToggleBookmark(const std::string& path);
    bool HasBookmark(const std::string& path) const;

    // --- drag & drop ---------------------------------------------------------
    // Copies or moves `paths` into `destDir`. Rejects the no-op cases (dropping
    // a folder into itself or into its own subtree) before touching the disk.
    bool PerformDrop(const std::vector<std::string>& paths, const std::string& destDir, bool move);

    // True when `destDir` is a legal drop destination for `paths`.
    static bool IsValidDropTarget(const std::vector<std::string>& paths,
                                  const std::string& destDir);

    // Re-registers filesystem watches for the tabs currently on screen.
    void SyncWatches();

    void SaveAll();

    std::string ConfigPath(const char* file) const;

private:
    void LoadConfig();
    void LoadWorkspace(const std::vector<std::string>& startPaths);
    void SaveWorkspaceFile();
    void SaveSettings();
    void WriteDefaultKeysFile();
    void RefreshRoots();

    void RequestLoad(Tab& tab, bool force = false);
    void EnsureVisibleTabsLoaded();
    void RefreshTabsShowing(const std::string& dir);

    void MoveCursor(int delta, bool extend, bool absolute = false);
    void ApplyPrompt();
    void CancelPrompt();
    void BeginPrompt(PromptKind kind, const char* labelKey, const std::string& initial);
    bool HandlePromptKey(const Chord& chord);

    void GotoTab(int index);
    void GotoSession(int index);
    void GotoBookmark(int index);
    void DoDelete(bool permanent);
    void DoPaste();
    void RebuildFocused();

    fs::IFileSystem& fs_;
    IShellIntegration& shell_;
    IHost& host_;
    fs::IDirectoryWatcher* watcher_ = nullptr;
    std::unordered_map<uint64_t, std::string> watched_;

    Workspace workspace_;
    KeyMap keymap_;
    Strings strings_;
    Theme theme_;
    Ini settings_;
    std::unique_ptr<fs::DirectoryLoader> loader_;

    std::vector<fs::Root> roots_;
    std::vector<fs::Root> quickAccess_;

    Prompt prompt_;
    bool keyHelp_ = false;
    bool sidebarVisible_ = true;
    bool darkTheme_ = true;
    std::string language_ = "auto";
    ViewState defaultView_;

    std::string statusMessage_;
    uint64_t statusUntilMs_ = 0;
    std::string lastTitle_;

    WindowPlacement placement_;
    bool dirty_ = false;
};

}  // namespace kite
