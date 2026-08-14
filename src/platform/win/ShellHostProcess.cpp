#include "platform/win/ShellHostProcess.h"

#include <cstdio>

#include "platform/win/WinPaths.h"

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

}  // namespace

ShellHostProcess::~ShellHostProcess() {
    Stop();
    // Closing the job kills anything still assigned to it, which covers a host
    // that stopped reading its pipe.
    if (job_) ::CloseHandle(job_);
}

std::wstring ShellHostProcess::ExecutablePath() {
    const std::wstring dir = ModuleDirectory();
    if (dir.empty()) return {};
    return dir + L'\\' + kHostExeName;
}

void ShellHostProcess::Stop() {
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

bool ShellHostProcess::Ensure(PipePump pump, void* context) {
    if (unavailable_) return false;

    // A host that exited on its own (idle timeout) or crashed looks the same
    // from here, and is handled the same way: forget it and start another.
    if (process_ && ::WaitForSingleObject(process_, 0) != WAIT_TIMEOUT) Stop();
    if (pipe_ != INVALID_HANDLE_VALUE && process_) return true;
    Stop();

    const std::wstring exe = ExecutablePath();
    if (exe.empty() || ::GetFileAttributesW(exe.c_str()) == INVALID_FILE_ATTRIBUTES) {
        // Latched: a missing file will not appear between two requests, and
        // retrying costs a failed CreateProcess every time.
        unavailable_ = true;
        return false;
    }

    // A fresh name per attempt so a previous host that has not finished dying
    // cannot collide with the new one. The address of this object separates the
    // menu host's names from the icon host's.
    wchar_t pipeName[128] = {};
    ::swprintf(pipeName, ARRAYSIZE(pipeName), L"\\\\.\\pipe\\kite.shellhost.%lu.%llx.%u",
               ::GetCurrentProcessId(), static_cast<unsigned long long>(
                                            reinterpret_cast<uintptr_t>(this)),
               ++generation_);

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

    if (WaitForPipeClient(pipe_, kConnectTimeoutMs, pump, context) != PipeStatus::Ok) {
        Stop();
        return false;
    }

    // The pipe lives in a namespace every session-local process can see, so
    // confirm the peer is the process we started and not a squatter.
    DWORD clientId = 0;
    if (!::GetNamedPipeClientProcessId(pipe_, &clientId) || clientId != processId_) {
        Stop();
        return false;
    }
    return true;
}

}  // namespace kite::win
