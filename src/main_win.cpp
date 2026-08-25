// Kite - Windows entry point.
//
// The whole startup path is: read four small ini files and a handful of registry
// keys, create a window, create a Direct2D target, ask a worker thread for the
// first listing. No COM, no shell namespace walk, no icon cache warm-up - the
// registry reads name keys, they do not load the handlers behind them.
#include <windows.h>

#include <shellapi.h>

#include <string>
#include <utility>
#include <vector>

#include "core/app/App.h"
#include "core/fs/VirtualPath.h"
#include "platform/win/WinArchiveTypes.h"
#include "platform/win/WinDirectoryWatcher.h"
#include "platform/win/WinFileSystem.h"
#include "platform/win/WinIconProvider.h"
#include "platform/win/WinShell.h"
#include "platform/win/WinSingleInstance.h"
#include "platform/win/WinUtf.h"
#include "platform/win/WinWindow.h"
#include "ui/AppUi.h"

namespace {

// How long a second launch waits for the first one's window to exist. The mutex
// appears before the window does, so a launch that lands inside that gap has to
// give the other process a moment rather than deciding it is not there.
constexpr unsigned kExistingWindowWaitMs = 2000;

}  // namespace

int APIENTRY wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    // Per-monitor v2 so Direct2D coordinates stay in DIPs across mixed-DPI
    // setups. Done in code rather than a manifest to keep the build simple.
    ::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    std::vector<std::string> startPaths;
    bool standalone = false;
    int argc = 0;
    if (LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc)) {
        for (int i = 1; i < argc; ++i) {
            std::string arg = kite::win::ToUtf8(argv[i]);
            if (arg == kite::win::kNewWindowFlag) {
                standalone = true;
                continue;
            }
            startPaths.push_back(std::move(arg));
        }
        ::LocalFree(argv);
    }

    // A second launch normally becomes tabs in the window that already exists.
    // --new-window is what says otherwise, and it is now the only way to ask for
    // a genuinely separate window.
    HANDLE instanceMutex = nullptr;
    if (!standalone) {
        bool alreadyRunning = false;
        instanceMutex = kite::win::AcquireInstanceMutex(alreadyRunning);
        if (alreadyRunning) {
            HWND existing = kite::win::FindExistingWindow(kExistingWindowWaitMs);
            if (existing && kite::win::ForwardPaths(existing, startPaths)) {
                if (instanceMutex) ::CloseHandle(instanceMutex);
                return 0;
            }
            // Nothing answered - the other process is wedged, or gone without
            // releasing the name. Opening a window of our own is worse than
            // ideal; a double-click that does nothing at all is worse still.
        }
    }

    // Before anything can list a folder, and so before any loader worker exists:
    // which archives count as folders is a fact about this machine, read once
    // and read-only from here on. An empty answer means the registry could not
    // be asked at all, not that nothing opens - core keeps its own zip/cab
    // default for that case.
    if (std::vector<std::string> archives = kite::win::ShellFolderExtensions(); !archives.empty()) {
        kite::vfs::SetArchiveExtensions(std::move(archives));
    }

    kite::win::WinFileSystem filesystem;
    kite::win::WinShell shell;
    kite::win::WinWindow window;
    // Declared after the window so it is destroyed first: its worker thread
    // calls back into the window's Wake().
    kite::win::WinDirectoryWatcher watcher(window);
    // Same reason as the watcher: its worker thread calls the window's Wake().
    kite::win::WinIconProvider icons(window);

    kite::App app(filesystem, shell, window, &watcher);
    kite::ui::AppUi appUi(app);

    app.SetIconProvider(&icons);
    window.Attach(&app, &appUi);
    window.SetIconProvider(&icons);
    // Before Init: it decides whether the saved sessions are read at all.
    app.SetStandalone(standalone);
    app.Init(startPaths);

    if (!window.Create(app.placement())) return 1;
    shell.SetWindow(window.handle());

    // Listings that completed before the window existed are still queued.
    app.PumpLoader();

    const int code = window.Run();
    // Held until here on purpose: releasing the name earlier would let a launch
    // during shutdown decide no instance exists and start a second full one.
    if (instanceMutex) ::CloseHandle(instanceMutex);
    return code;
}
