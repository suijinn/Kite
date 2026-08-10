#include "platform/win/ShellHostClient.h"

#include <cstdio>

#include "platform/win/ShellMenuProtocol.h"
#include "platform/win/ShellPipe.h"

namespace kite::win {
namespace {

constexpr wchar_t kHostExeName[] = L"kite_shellhost.exe";

// The host only has to start and connect; anything longer than this means it is
// not going to.
constexpr DWORD kConnectTimeoutMs = 10000;

// How long a host gets to notice its pipe closed and exit on its own before it
// is terminated. A host waiting on a read returns within a millisecond; one that
// is still inside TrackPopupMenu will not notice at all, and waiting longer for
// it would only delay Kite's own shutdown.
constexpr DWORD kExitGraceMs = 300;

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

ShellHostClient::~ShellHostClient() {
    CloseHost();
    // Closing the job kills anything still assigned to it, which covers a host
    // that stopped reading its pipe.
    if (job_) ::CloseHandle(job_);
}

std::wstring ShellHostClient::HostExecutablePath() {
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;) {
        const DWORD length =
            ::GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) return {};
        if (length < buffer.size()) {
            buffer.resize(length);
            break;
        }
        buffer.resize(buffer.size() * 2);
    }

    const size_t slash = buffer.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return {};
    buffer.resize(slash + 1);
    buffer += kHostExeName;
    return buffer;
}

void ShellHostClient::CloseHost() {
    if (pipe_ != INVALID_HANDLE_VALUE) {
        // The host is blocked in a read; breaking the pipe is how it is told to
        // quit.
        ::DisconnectNamedPipe(pipe_);
        ::CloseHandle(pipe_);
        pipe_ = INVALID_HANDLE_VALUE;
    }
    if (process_) {
        if (::WaitForSingleObject(process_, kExitGraceMs) != WAIT_OBJECT_0) {
            ::TerminateProcess(process_, 0);
        }
        ::CloseHandle(process_);
        process_ = nullptr;
    }
    processId_ = 0;
}

bool ShellHostClient::EnsureHost(HWND owner) {
    if (unavailable_) return false;

    // A host that exited on its own (idle timeout) or crashed looks the same
    // from here, and is handled the same way: forget it and start another.
    if (process_ && ::WaitForSingleObject(process_, 0) != WAIT_TIMEOUT) CloseHost();
    if (pipe_ != INVALID_HANDLE_VALUE && process_) return true;
    CloseHost();

    const std::wstring exe = HostExecutablePath();
    if (exe.empty() || ::GetFileAttributesW(exe.c_str()) == INVALID_FILE_ATTRIBUTES) {
        // Latched: a missing file will not appear between two right-clicks, and
        // retrying costs a failed CreateProcess every time.
        unavailable_ = true;
        return false;
    }

    // A fresh name per attempt so a previous host that has not finished dying
    // cannot collide with the new one.
    wchar_t pipeName[128] = {};
    ::swprintf(pipeName, ARRAYSIZE(pipeName), L"\\\\.\\pipe\\kite.shellhost.%lu.%u",
               ::GetCurrentProcessId(), ++generation_);

    // FILE_FLAG_FIRST_PIPE_INSTANCE: if anything already owns this name we fail
    // instead of quietly talking to it.
    pipe_ = ::CreateNamedPipeW(
        pipeName, PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 64 * 1024, 64 * 1024, 0, nullptr);
    if (pipe_ == INVALID_HANDLE_VALUE) return false;

    if (!job_) {
        job_ = ::CreateJobObjectW(nullptr, nullptr);
        if (job_) {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
            // KILL_ON_JOB_CLOSE so a host cannot outlive Kite even if Kite is
            // killed outright. SILENT_BREAKAWAY_OK so processes the *menu*
            // starts (7-Zip extracting, an editor opening a file) are not in the
            // job and therefore not killed when Kite exits.
            limits.BasicLimitInformation.LimitFlags =
                JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_SILENT_BREAKAWAY_OK;
            if (!::SetInformationJobObject(job_, JobObjectExtendedLimitInformation, &limits,
                                           sizeof(limits))) {
                ::CloseHandle(job_);
                job_ = nullptr;
            }
        }
    }

    std::wstring commandLine;
    commandLine += L'"';
    commandLine += exe;
    commandLine += L"\" ";
    commandLine += pipeName;

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION info{};
    // Suspended so it is inside the job before it can run - otherwise a host
    // that spawns and dies in that window is never accounted for. No handles
    // are inherited: the pipe is reached by name.
    if (!::CreateProcessW(exe.c_str(), commandLine.data(), nullptr, nullptr, FALSE,
                          CREATE_SUSPENDED, nullptr, nullptr, &startup, &info)) {
        ::CloseHandle(pipe_);
        pipe_ = INVALID_HANDLE_VALUE;
        return false;
    }
    if (job_) ::AssignProcessToJobObject(job_, info.hProcess);
    ::ResumeThread(info.hThread);
    ::CloseHandle(info.hThread);

    process_ = info.hProcess;
    processId_ = info.dwProcessId;

    PumpState state{ owner, false };
    if (WaitForPipeClient(pipe_, kConnectTimeoutMs, &PumpOwnerWindow, &state) != PipeStatus::Ok) {
        CloseHost();
        return false;
    }

    // The pipe lives in a namespace every session-local process can see, so
    // confirm the peer is the process we started and not a squatter.
    DWORD clientId = 0;
    if (!::GetNamedPipeClientProcessId(pipe_, &clientId) || clientId != processId_) {
        CloseHost();
        return false;
    }
    return true;
}

bool ShellHostClient::ShowContextMenu(HWND owner, const std::vector<std::string>& paths,
                                      int screenX, int screenY, bool extended) {
    if (paths.empty()) return false;
    if (!EnsureHost(owner)) return false;

    // Without this the host cannot raise its own menu: it is not the foreground
    // process, and only the process that is may hand that right over.
    ::AllowSetForegroundWindow(processId_);

    shellhost::Request request;
    request.paths = paths;
    request.screenX = screenX;
    request.screenY = screenY;
    request.extended = extended;
    request.ownerWindow = reinterpret_cast<uint64_t>(owner);

    const std::vector<uint8_t> frame = shellhost::EncodeRequest(request);
    if (frame.empty()) return false;

    PumpState state{ owner, false };
    if (WritePipeFrame(pipe_, frame, &PumpOwnerWindow, &state) != PipeStatus::Ok) {
        CloseHost();
        return false;
    }

    // No timeout: a menu stays open as long as the user leaves it open. The wait
    // ends when the host answers, when it dies, or when the pump gives up
    // because the window is going away.
    std::vector<uint8_t> payload;
    const PipeStatus status = ReadPipeFrame(pipe_, payload, INFINITE, &PumpOwnerWindow, &state);
    const DWORD hostId = processId_;

    if (status != PipeStatus::Ok) {
        // Includes the case this whole exercise is about: a shell extension
        // faulting hard enough to take its process down. Kite only loses the
        // menu, and the next right-click starts a new host.
        CloseHost();
        RestoreOwner(owner, hostId);
        return false;
    }

    shellhost::Response response;
    if (!shellhost::DecodeResponse(payload.data(), payload.size(), response)) {
        CloseHost();
        RestoreOwner(owner, hostId);
        return false;
    }

    RestoreOwner(owner, hostId);
    return response.result != shellhost::Result::Failed;
}

}  // namespace kite::win
