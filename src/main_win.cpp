// Kite - Windows entry point.
//
// The whole startup path is: read four small ini files, create a window,
// create a Direct2D target, ask a worker thread for the first listing. No COM,
// no shell namespace walk, no icon cache warm-up.
#include <windows.h>

#include <shellapi.h>

#include <string>
#include <vector>

#include "core/app/App.h"
#include "platform/win/WinDirectoryWatcher.h"
#include "platform/win/WinFileSystem.h"
#include "platform/win/WinIconProvider.h"
#include "platform/win/WinShell.h"
#include "platform/win/WinUtf.h"
#include "platform/win/WinWindow.h"
#include "ui/AppUi.h"

int APIENTRY wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    // Per-monitor v2 so Direct2D coordinates stay in DIPs across mixed-DPI
    // setups. Done in code rather than a manifest to keep the build simple.
    ::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    std::vector<std::string> startPaths;
    int argc = 0;
    if (LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc)) {
        for (int i = 1; i < argc; ++i) startPaths.push_back(kite::win::ToUtf8(argv[i]));
        ::LocalFree(argv);
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
    app.Init(startPaths);

    if (!window.Create(app.placement())) return 1;
    shell.SetWindow(window.handle());

    // Listings that completed before the window existed are still queued.
    app.PumpLoader();

    return window.Run();
}
