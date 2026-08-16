#include "platform/win/WinSingleInstance.h"

#include <cstdio>

#include "platform/win/WinPaths.h"
#include "platform/win/WinUtf.h"

namespace kite::win {
namespace {

// How long a forwarded launch waits for the existing window to answer. It is
// generous because the window may be busy inside a shell menu; what it must not
// be is unbounded, or a deadlocked Kite would take every later launch with it.
constexpr DWORD kForwardTimeoutMs = 5000;

// The one place the identity of "this copy of Kite" is computed. Case-folded
// because Windows paths are, so the same exe reached through a differently
// spelled path is still the same instance.
uint64_t InstanceKey() {
    static const uint64_t key = [] {
        std::wstring path = ModuleFilePath();
        uint64_t hash = 0xCBF29CE484222325ULL;  // FNV-1a, 64 bit
        for (wchar_t c : path) {
            if (c >= L'A' && c <= L'Z') c = static_cast<wchar_t>(c - L'A' + L'a');
            hash ^= static_cast<uint64_t>(c);
            hash *= 0x100000001B3ULL;
        }
        return hash;
    }();
    return key;
}

std::wstring NameWithKey(const wchar_t* prefix) {
    wchar_t buffer[128];
    ::swprintf_s(buffer, L"%s%016llx", prefix, static_cast<unsigned long long>(InstanceKey()));
    return buffer;
}

}  // namespace

const wchar_t* InstanceClassName(bool primary) {
    static const std::wstring main = NameWithKey(L"KiteMainWindow.");
    static const std::wstring standalone = NameWithKey(L"KiteExtraWindow.");
    return primary ? main.c_str() : standalone.c_str();
}

HANDLE AcquireInstanceMutex(bool& alreadyRunning) {
    alreadyRunning = false;
    const std::wstring name = NameWithKey(L"Local\\Kite.SingleInstance.");
    HANDLE mutex = ::CreateMutexW(nullptr, FALSE, name.c_str());
    // The mutex is never waited on - only its existence is the answer, so the
    // error has to be read before anything else touches the thread's last error.
    if (mutex && ::GetLastError() == ERROR_ALREADY_EXISTS) alreadyRunning = true;
    return mutex;
}

HWND FindExistingWindow(unsigned timeoutMs) {
    constexpr unsigned kStepMs = 25;
    for (unsigned waited = 0;; waited += kStepMs) {
        if (HWND found = ::FindWindowW(InstanceClassName(true), nullptr)) return found;
        if (waited >= timeoutMs) return nullptr;
        ::Sleep(kStepMs);
    }
}

bool ForwardPaths(HWND target, const std::vector<std::string>& paths) {
    if (!target) return false;

    std::string payload;
    for (const std::string& p : paths) {
        if (p.empty()) continue;
        if (!payload.empty()) payload.push_back('\n');
        payload += p;
    }

    // The receiver raises itself, and only the process that currently holds the
    // foreground can hand that right over - which, right now, is this one.
    DWORD pid = 0;
    ::GetWindowThreadProcessId(target, &pid);
    if (pid) ::AllowSetForegroundWindow(pid);

    COPYDATASTRUCT data{};
    data.dwData = kForwardPathsId;
    data.cbData = static_cast<DWORD>(payload.size());
    data.lpData = payload.empty() ? nullptr : const_cast<char*>(payload.data());

    DWORD_PTR result = 0;
    const LRESULT sent = ::SendMessageTimeoutW(
        target, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&data),
        SMTO_ABORTIFHUNG | SMTO_NORMAL, kForwardTimeoutMs, &result);
    return sent != 0 && result != 0;
}

std::vector<std::string> ParseForwardedPaths(const void* payload, size_t bytes) {
    std::vector<std::string> out;
    if (!payload || bytes == 0) return out;

    const char* text = static_cast<const char*>(payload);
    size_t begin = 0;
    for (size_t i = 0; i <= bytes; ++i) {
        // A trailing NUL is not part of any path: the sender counts bytes rather
        // than terminating, but nothing stops a sender from doing both.
        const bool end = (i == bytes) || text[i] == '\n' || text[i] == '\r' || text[i] == '\0';
        if (!end) continue;
        if (i > begin) out.emplace_back(text + begin, i - begin);
        begin = i + 1;
    }
    return out;
}

}  // namespace kite::win
