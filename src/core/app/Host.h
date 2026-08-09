// Kite - services the platform window provides to the OS-independent App.
#pragma once

#include <string>
#include <vector>

#include "core/fs/DirectoryLoader.h"

namespace kite {

// Shell integration. Implemented by platform/win/WinShell.cpp.
//
// NOTE: ShowContextMenu currently loads third-party shell extension DLLs into
// this process. Box / Google Drive / Dropbox extensions are a known source of
// crashes, so the plan is to move this behind an out-of-process host
// (kite_shellhost.exe) speaking over a pipe. The interface is already shaped
// for that: nothing but paths and a screen position crosses the boundary.
class IShellIntegration {
public:
    virtual ~IShellIntegration() = default;

    // `extended` requests the full ("Show more options") menu in one step,
    // which is the whole point of this app on Windows 11.
    virtual void ShowContextMenu(const std::vector<std::string>& paths, int screenX, int screenY,
                                 bool extended) = 0;

    virtual bool Open(const std::string& path) = 0;
    virtual bool OpenWith(const std::string& path) = 0;
    virtual bool ShowProperties(const std::string& path) = 0;
    virtual bool RevealInExplorer(const std::string& path) = 0;
    virtual bool OpenTerminal(const std::string& dir) = 0;

    virtual bool SetClipboardText(const std::string& utf8) = 0;
    virtual bool SetClipboardFiles(const std::vector<std::string>& paths, bool cut) = 0;
    virtual bool GetClipboardFiles(std::vector<std::string>& paths, bool* cut) = 0;
};

// The window itself.
class IHost : public fs::IWakeSink {
public:
    ~IHost() override = default;

    virtual void Invalidate() = 0;
    virtual void SetTitle(const std::string& utf8) = 0;
    virtual void Close() = 0;

    // Where the text caret is, in client DIPs - used to place the IME
    // candidate window so Japanese input is not stranded in a corner.
    virtual void SetImePosition(float x, float y) = 0;

    virtual void SetCursorShape(int shape) = 0;  // 0 arrow, 1 hand, 2 h-resize, 3 v-resize

    // Starts an OS drag carrying `paths`. Blocks until the drag finishes, the
    // way every platform's drag API does. Returns true when something was
    // actually dropped somewhere.
    virtual bool BeginFileDrag(const std::vector<std::string>& paths) = 0;
};

}  // namespace kite
