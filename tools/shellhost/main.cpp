// kite_shellhost.exe - the sacrificial process.
//
// Kite's own process never loads a shell extension. Everything third-party
// (Box, Google Drive, 7-Zip, TortoiseGit and whatever else the machine has
// registered) is instantiated here instead, so a handler that corrupts the heap
// or faults on a background thread costs the user a context menu rather than
// their file manager. Three kinds of handler arrive that way: context menus
// (IContextMenu), icon overlays (IShellIconOverlayIdentifier) and namespace
// extensions (the folders that appear under "PC" and friends).
//
// It is started by kite.exe on the first request, reads requests from the named
// pipe given on the command line, and answers one response per request. It quits
// when the pipe breaks - which is also how it learns that Kite is gone.
//
// kite.exe runs three of these. A menu host sits inside TrackPopupMenu for as
// long as the menu is open, so icons - and the listing of a virtual folder - get
// instances of their own rather than waiting behind it.
#include <windows.h>

#include <objbase.h>
#include <shellapi.h>

#include <string>
#include <unordered_map>
#include <vector>

#include "platform/win/ShellFolder.h"
#include "platform/win/ShellHostProtocol.h"
#include "platform/win/ShellIcons.h"
#include "platform/win/ShellMenu.h"
#include "platform/win/ShellPipe.h"

using namespace kite;

namespace {

constexpr wchar_t kClassName[] = L"KiteShellMenuHost";

// Idle long enough and the host exits, handing back whatever the extension DLLs
// are holding. kite.exe starts another one the next time it needs a menu, so
// this is invisible apart from that first menu being slower again.
constexpr DWORD kIdleTimeoutMs = 60000;

/// Runs whatever the shell put in our queue while we wait.
///
/// A top-level window whose thread does not pump is worse than useless: a
/// system-wide SendMessage broadcast (WM_SETTINGCHANGE and friends) blocks the
/// sender until it times out on us. Modeless UI an extension left behind - a
/// properties sheet, say - needs the same treatment.
bool PumpMessages(void*) {
    MSG msg{};
    while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) return false;
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }
    return true;
}

LRESULT CALLBACK HostWindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    // Owner-drawn menu items from shell extensions are drawn through here.
    LRESULT result = 0;
    if (kite::win::ForwardContextMenuMessage(message, wparam, lparam, &result)) return result;
    return ::DefWindowProcW(hwnd, message, wparam, lparam);
}

/// Creates the window that owns the menu.
///
/// TrackPopupMenuEx insists on a window belonging to the calling thread, so
/// Kite's own window cannot be used. This one is 1x1 and fully transparent:
/// invisible, click-through, and absent from the taskbar and Alt+Tab, but still
/// a real top-level window that can take the foreground - which a popup menu
/// needs in order to close when the user clicks away from it.
HWND CreateHostWindow() {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &HostWindowProc;
    wc.hInstance = ::GetModuleHandleW(nullptr);
    wc.lpszClassName = kClassName;
    ::RegisterClassExW(&wc);

    HWND hwnd = ::CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_TRANSPARENT, kClassName,
                                  L"Kite shell host", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr,
                                  wc.hInstance, nullptr);
    if (hwnd) ::SetLayeredWindowAttributes(hwnd, 0, 0, LWA_ALPHA);
    return hwnd;
}

/// Reports whether this process still has UI on screen.
///
/// "Properties" is modeless: InvokeCommand returns while the sheet is still up,
/// and the sheet lives in this process. Exiting on the idle timer would make it
/// vanish a minute after it opened.
bool HasVisibleUi(HWND helper) {
    struct Scan {
        DWORD processId;
        HWND helper;
        bool found;
    } scan{ ::GetCurrentProcessId(), helper, false };

    ::EnumWindows(
        [](HWND hwnd, LPARAM param) -> BOOL {
            auto* state = reinterpret_cast<Scan*>(param);
            if (hwnd == state->helper || !::IsWindowVisible(hwnd)) return TRUE;
            DWORD pid = 0;
            ::GetWindowThreadProcessId(hwnd, &pid);
            if (pid != state->processId) return TRUE;
            state->found = true;
            return FALSE;
        },
        reinterpret_cast<LPARAM>(&scan));
    return scan.found;
}

/// Shows one menu and reports what came of it.
shellhost::Result HandleRequest(HWND window, const shellhost::Request& request) {
    // Per request rather than once at start-up: Kite's theme can be toggled
    // between two right-clicks, and this process outlives that.
    kite::win::SetShellMenuDarkMode(window, request.dark);

    // Put the helper window under the menu so that dialogs which position
    // themselves relative to their owner land somewhere sensible.
    POINT anchor{ request.screenX, request.screenY };
    if (anchor.x < 0 || anchor.y < 0) ::GetCursorPos(&anchor);
    ::SetWindowPos(window, HWND_TOPMOST, anchor.x, anchor.y, 1, 1, SWP_NOACTIVATE);
    ::ShowWindow(window, SW_SHOWNA);

    const shellhost::Result result = kite::win::ShowShellContextMenu(
        window, reinterpret_cast<HWND>(request.ownerWindow), request.container, request.paths,
        request.screenX, request.screenY, request.extended, request.background);

    // Hidden again straight away: while it is visible it is the foreground
    // window, and Kite's title bar stays greyed out for as long as that lasts.
    ::ShowWindow(window, SW_HIDE);
    return result;
}

/// Hands out one id per distinct bitmap, for the life of the connection.
///
/// Overlays make an icon depend on the path, not just the file type, so the
/// caller has to ask per file. The pixels, though, repeat endlessly: a folder of
/// a thousand unmodified .cpp files in a working copy yields two bitmaps. Keying
/// by content means each one crosses the pipe once.
class IconIdTable {
public:
    /// Returns the id for these pixels, and whether they are new to this table.
    uint32_t Intern(const win::IconBitmap& bitmap, bool& isNew) {
        const uint64_t key = Hash(bitmap);
        auto it = ids_.find(key);
        if (it != ids_.end()) {
            isNew = false;
            return it->second;
        }
        const uint32_t id = ++lastId_;
        ids_.emplace(key, id);
        isNew = true;
        return id;
    }

private:
    /// FNV-1a over the pixels. A collision would show the wrong icon, so the
    /// dimensions go in too; 64 bits makes it a non-issue at these volumes.
    static uint64_t Hash(const win::IconBitmap& bitmap) {
        uint64_t hash = 1469598103934665603ull;
        auto mix = [&hash](uint8_t byte) {
            hash ^= byte;
            hash *= 1099511628211ull;
        };
        mix(static_cast<uint8_t>(bitmap.width));
        mix(static_cast<uint8_t>(bitmap.height));
        for (uint8_t byte : bitmap.bgra) mix(byte);
        return hash;
    }

    std::unordered_map<uint64_t, uint32_t> ids_;
    uint32_t lastId_ = 0;
};

/// Fetches one batch of icons.
shellhost::IconResponse HandleIconRequest(const shellhost::IconRequest& request,
                                          IconIdTable& table) {
    shellhost::IconResponse response;
    response.ids.reserve(request.paths.size());

    for (const std::string& path : request.paths) {
        win::IconBitmap bitmap;
        if (!win::LoadShellIcon(path, request.pixelSize, bitmap)) {
            // 0 means "no icon"; the caller keeps drawing its own glyph there.
            response.ids.push_back(0);
            continue;
        }
        bool isNew = false;
        const uint32_t id = table.Intern(bitmap, isNew);
        response.ids.push_back(id);
        if (isNew) {
            shellhost::IconImage image;
            image.id = id;
            image.width = bitmap.width;
            image.height = bitmap.height;
            image.bgra = std::move(bitmap.bgra);
            response.images.push_back(std::move(image));
        }
    }
    return response;
}

}  // namespace

int APIENTRY wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    int argc = 0;
    LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    if (!argv) return 1;
    const std::wstring pipeName = argc >= 2 ? argv[1] : L"";
    ::LocalFree(argv);
    // Started by hand rather than by Kite: there is nothing to talk to.
    if (pipeName.empty()) return 1;

    // Menus scale like Explorer's only if this process is DPI-aware the same way
    // the one that asked for them is.
    ::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // Listing "PC" touches every drive letter. Without this, an empty card
    // reader raises the OS "there is no disk in the drive" dialog - on a
    // process with no visible window, in front of whatever the user was doing.
    ::SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);

    HANDLE pipe = ::CreateFileW(pipeName.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    if (pipe == INVALID_HANDLE_VALUE) return 1;

    // Shell extensions are apartment-threaded and many of them assume OLE is up.
    // This is the 100 ms that kite.exe used to pay on its own UI thread.
    const HRESULT ole = ::OleInitialize(nullptr);
    HWND window = CreateHostWindow();

    if (SUCCEEDED(ole) && window) {
        IconIdTable iconIds;
        for (;;) {
            std::vector<uint8_t> payload;
            const kite::win::PipeStatus status =
                kite::win::ReadPipeFrame(pipe, payload, kIdleTimeoutMs, &PumpMessages, nullptr);

            if (status == kite::win::PipeStatus::Timeout) {
                if (HasVisibleUi(window)) continue;
                break;
            }
            // Closed covers the case that matters most: Kite exited, so we must
            // not stay behind.
            if (status != kite::win::PipeStatus::Ok) break;

            // A frame we cannot parse means the stream is out of step with the
            // other side. There is no recovering from that, so hang up.
            shellhost::MessageKind kind = shellhost::MessageKind::Menu;
            if (!shellhost::DecodeKind(payload.data(), payload.size(), kind)) break;

            std::vector<uint8_t> reply;
            if (kind == shellhost::MessageKind::Menu) {
                shellhost::Request request;
                if (!shellhost::DecodeRequest(payload.data(), payload.size(), request)) break;

                shellhost::Response response;
                response.result = HandleRequest(window, request);
                reply = shellhost::EncodeResponse(response);
            } else if (kind == shellhost::MessageKind::Verb) {
                shellhost::VerbRequest request;
                if (!shellhost::DecodeVerbRequest(payload.data(), payload.size(), request)) break;
                reply = shellhost::EncodeVerbResponse(kite::win::InvokeShellVerb(
                    reinterpret_cast<HWND>(request.ownerWindow), request.container, request.paths,
                    request.verb, request.byOriginalPath));
            } else if (kind == shellhost::MessageKind::Folder) {
                shellhost::FolderRequest request;
                if (!shellhost::DecodeFolderRequest(payload.data(), payload.size(), request)) {
                    break;
                }
                shellhost::FolderResponse response =
                    kite::win::EnumerateShellFolder(request.path);
                reply = shellhost::EncodeFolderResponse(response);
                if (reply.empty()) {
                    // Too large to send. Saying so beats sending part of it -
                    // a half listing reads as "the rest was deleted".
                    shellhost::FolderResponse failure;
                    failure.status = shellhost::FolderStatus::Error;
                    reply = shellhost::EncodeFolderResponse(failure);
                }
            } else {
                shellhost::IconRequest request;
                if (!shellhost::DecodeIconRequest(payload.data(), payload.size(), request)) break;
                reply = shellhost::EncodeIconResponse(HandleIconRequest(request, iconIds));
            }
            if (reply.empty()) break;

            if (kite::win::WritePipeFrame(pipe, reply, &PumpMessages, nullptr) !=
                kite::win::PipeStatus::Ok) {
                break;
            }
        }
    }

    if (window) ::DestroyWindow(window);
    ::CloseHandle(pipe);
    if (SUCCEEDED(ole)) ::OleUninitialize();
    return 0;
}
