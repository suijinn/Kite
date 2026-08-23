#include "platform/win/ShellHostClient.h"

#include <cstdio>

#include "platform/win/ShellHostProtocol.h"
#include "platform/win/ShellPipe.h"

namespace kite::win {
namespace {

/// What the pump needs to know while a menu is on screen in the other process.
struct PumpState {
    HWND owner = nullptr;
    bool closeRequested = false;
};

/// Keeps `owner` painting while the menu is up in the host process.
///
/// The ranges are explicit because PeekMessage cannot express "everything
/// except". Three groups, each for a different reason:
///
///  - dispatched: paint, timers, non-client mouse, sizing, registered messages.
///    Without these the window is visibly frozen and Windows eventually paints
///    the ghost "not responding" copy over it.
///  - dropped: client-area mouse and keyboard. An in-process TrackPopupMenu
///    swallowed these, and letting them through would mean the click that
///    dismissed the menu also landed in the file list.
///  - left queued: WM_APP and up. Those are Kite's own notifications (a finished
///    listing, a completed drop); handling them here would re-enter App from
///    inside App::ShowContextMenuAt. They are still there when the menu closes.
bool PumpOwnerWindow(void* context) {
    auto* state = static_cast<PumpState*>(context);

    struct Range {
        UINT first;
        UINT last;
    };
    static constexpr Range kDispatch[] = {
        { 0x0000, WM_KEYFIRST - 1 },
        { WM_KEYLAST + 1, WM_MOUSEFIRST - 1 },
        { WM_MOUSELAST + 1, WM_APP - 1 },
        { 0xC000, 0xFFFF },
    };
    static constexpr Range kDrop[] = {
        { WM_MOUSEFIRST, WM_MOUSELAST },
        { WM_KEYFIRST, WM_KEYLAST },
    };

    MSG msg{};
    for (const Range& range : kDispatch) {
        while (::PeekMessageW(&msg, nullptr, range.first, range.last, PM_REMOVE)) {
            // WM_QUIT comes back from PeekMessage whatever the filter says, and
            // swallowing it would leave the main loop spinning forever.
            if (msg.message == WM_QUIT) {
                ::PostQuitMessage(static_cast<int>(msg.wParam));
                return false;
            }
            // Shutting down from inside a menu would tear the App down while
            // App::ShowContextMenuAt is still on the stack. Put it back and let
            // the real message loop have it.
            if (msg.message == WM_CLOSE) {
                state->closeRequested = true;
                ::PostMessageW(msg.hwnd, WM_CLOSE, msg.wParam, msg.lParam);
                return false;
            }
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
        }
    }
    for (const Range& range : kDrop) {
        while (::PeekMessageW(&msg, nullptr, range.first, range.last, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                ::PostQuitMessage(static_cast<int>(msg.wParam));
                return false;
            }
        }
    }

    // The window can still go away underneath us: a click on the title bar's
    // close button turns into a *sent* WM_CLOSE inside DefWindowProc.
    return state->owner == nullptr || ::IsWindow(state->owner) != FALSE;
}

/// Hands activation back to `owner` once the menu is gone.
void RestoreOwner(HWND owner, DWORD hostProcessId) {
    if (!owner || !::IsWindow(owner)) return;

    // A modal dialog disables its owner while it is up and re-enables it on the
    // way out. If the host died in between, that never happened and the window
    // would stay dead to input forever.
    if (!::IsWindowEnabled(owner)) ::EnableWindow(owner, TRUE);

    // Only take the foreground back if the host still holds it. A menu item like
    // "Edit" or "Extract here" has just launched something, and yanking the
    // foreground away from the window the user asked for would be worse than
    // leaving Kite's title bar looking inactive.
    const HWND foreground = ::GetForegroundWindow();
    if (foreground) {
        DWORD pid = 0;
        ::GetWindowThreadProcessId(foreground, &pid);
        if (pid != hostProcessId) return;
    }
    ::SetForegroundWindow(owner);
}

}  // namespace

bool ShellHostClient::ShowContextMenu(HWND owner, const std::string& container,
                                      const std::vector<std::string>& paths, int screenX,
                                      int screenY, bool extended, bool background, bool dark) {
    if (paths.empty()) return false;

    PumpState connectState{ owner, false };
    if (!host_.Ensure(&PumpOwnerWindow, &connectState)) return false;

    // Without this the host cannot raise its own menu: it is not the foreground
    // process, and only the process that is may hand that right over.
    ::AllowSetForegroundWindow(host_.processId());

    shellhost::Request request;
    request.container = container;
    request.paths = paths;
    request.screenX = screenX;
    request.screenY = screenY;
    request.extended = extended;
    request.background = background;
    request.dark = dark;
    request.ownerWindow = reinterpret_cast<uint64_t>(owner);

    const std::vector<uint8_t> frame = shellhost::EncodeRequest(request);
    if (frame.empty()) return false;

    PumpState state{ owner, false };
    if (WritePipeFrame(host_.pipe(), frame, &PumpOwnerWindow, &state) != PipeStatus::Ok) {
        host_.Stop();
        return false;
    }

    // No timeout: a menu stays open as long as the user leaves it open. The wait
    // ends when the host answers, when it dies, or when the pump gives up
    // because the window is going away.
    std::vector<uint8_t> payload;
    const PipeStatus status =
        ReadPipeFrame(host_.pipe(), payload, INFINITE, &PumpOwnerWindow, &state);
    const DWORD hostId = host_.processId();

    if (status != PipeStatus::Ok) {
        // Includes the case this whole exercise is about: a shell extension
        // faulting hard enough to take its process down. Kite only loses the
        // menu, and the next right-click starts a new host.
        host_.Stop();
        RestoreOwner(owner, hostId);
        return false;
    }

    shellhost::Response response;
    if (!shellhost::DecodeResponse(payload.data(), payload.size(), response)) {
        host_.Stop();
        RestoreOwner(owner, hostId);
        return false;
    }

    RestoreOwner(owner, hostId);
    return response.result != shellhost::Result::Failed;
}

bool ShellHostClient::InvokeVerb(HWND owner, const std::string& container,
                                 const std::vector<std::string>& paths, const std::string& verb,
                                 bool byOriginalPath) {
    if (paths.empty() || verb.empty()) return false;

    PumpState connectState{ owner, false };
    if (!host_.Ensure(&PumpOwnerWindow, &connectState)) return false;

    // The verb may raise a dialog of its own - "this file is too big for the
    // Recycle Bin", a name collision at the original location - and that dialog
    // has to be able to come to the front.
    ::AllowSetForegroundWindow(host_.processId());

    shellhost::VerbRequest request;
    request.container = container;
    request.paths = paths;
    request.verb = verb;
    request.byOriginalPath = byOriginalPath;
    request.ownerWindow = reinterpret_cast<uint64_t>(owner);

    const std::vector<uint8_t> frame = shellhost::EncodeVerbRequest(request);
    if (frame.empty()) return false;

    PumpState state{ owner, false };
    if (WritePipeFrame(host_.pipe(), frame, &PumpOwnerWindow, &state) != PipeStatus::Ok) {
        host_.Stop();
        return false;
    }

    // No timeout, for the same reason a menu has none: what is being waited on
    // may be a dialog the user has not answered yet.
    std::vector<uint8_t> payload;
    const PipeStatus status =
        ReadPipeFrame(host_.pipe(), payload, INFINITE, &PumpOwnerWindow, &state);
    RestoreOwner(owner, host_.processId());
    if (status != PipeStatus::Ok) {
        host_.Stop();
        return false;
    }

    shellhost::VerbResponse response;
    if (!shellhost::DecodeVerbResponse(payload.data(), payload.size(), response)) {
        host_.Stop();
        return false;
    }
    return response.ok;
}

bool ShellHostClient::Extract(HWND owner, const std::string& container,
                              const std::string& parsingName, std::string& extracted) {
    if (parsingName.empty()) return false;

    PumpState connectState{ owner, false };
    if (!host_.Ensure(&PumpOwnerWindow, &connectState)) return false;

    shellhost::ExtractRequest request;
    request.container = container;
    request.path = parsingName;

    const std::vector<uint8_t> frame = shellhost::EncodeExtractRequest(request);
    if (frame.empty()) return false;

    PumpState state{ owner, false };
    if (WritePipeFrame(host_.pipe(), frame, &PumpOwnerWindow, &state) != PipeStatus::Ok) {
        host_.Stop();
        return false;
    }

    // No timeout. Copying out of an archive is proportional to the file, and
    // cutting a big one short would leave a half file for something to open -
    // the same reason the icon path's timeout is not wanted here.
    std::vector<uint8_t> payload;
    const PipeStatus status =
        ReadPipeFrame(host_.pipe(), payload, INFINITE, &PumpOwnerWindow, &state);
    if (status != PipeStatus::Ok) {
        host_.Stop();
        return false;
    }

    shellhost::ExtractResponse response;
    if (!shellhost::DecodeExtractResponse(payload.data(), payload.size(), response)) {
        host_.Stop();
        return false;
    }
    if (!response.ok || response.path.empty()) return false;
    extracted = response.path;
    return true;
}

}  // namespace kite::win
