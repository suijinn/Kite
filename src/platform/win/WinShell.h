#pragma once

#include <windows.h>

// WIN32_LEAN_AND_MEAN keeps ole2.h out of windows.h, so the COM interfaces we
// hand around have to be pulled in explicitly.
#include <objidl.h>

#include <string>
#include <vector>

#include "core/app/Host.h"

namespace kite::win {

// Shell integration for Windows.
//
// This is the one place Kite loads foreign code: IContextMenu handlers from
// Box, Google Drive, 7-Zip and friends run inside our process while a menu is
// up. Every call into them is wrapped in an SEH guard so a faulting extension
// costs the menu rather than the app. Moving the whole thing into a helper
// process is the planned next step; the interface would not change.
class WinShell final : public IShellIntegration {
public:
    void SetWindow(HWND hwnd) { hwnd_ = hwnd; }

    void ShowContextMenu(const std::vector<std::string>& paths, int screenX, int screenY,
                         bool extended) override;
    bool Open(const std::string& path) override;
    bool OpenWith(const std::string& path) override;
    bool ShowProperties(const std::string& path) override;
    bool RevealInExplorer(const std::string& path) override;
    bool OpenTerminal(const std::string& dir) override;

    bool SetClipboardText(const std::string& utf8) override;
    bool SetClipboardFiles(const std::vector<std::string>& paths, bool cut) override;
    bool GetClipboardFiles(std::vector<std::string>& paths, bool* cut) override;

private:
    HWND hwnd_ = nullptr;
};

// Owner-drawn shell menus need the owning window to relay a few messages.
// Call this first in the window procedure; it returns true when it handled the
// message and filled `result`.
bool ForwardContextMenuMessage(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam,
                               LRESULT* result);

// OLE initialization, deferred so a cold start that never opens a menu or drags
// anything does not pay for it. Safe to call repeatedly.
bool EnsureOle();

// Builds a shell IDataObject for `paths`, which must all live in one folder.
// Caller owns the reference. Returns nullptr on failure.
IDataObject* CreateShellDataObject(const std::vector<std::string>& paths);

// Reads the file list out of a dropped/pasted data object (CF_HDROP).
std::vector<std::string> ExtractDroppedPaths(IDataObject* data);

}  // namespace kite::win
