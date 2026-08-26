// Kite - Windows implementation of core/base/Platform.h.
#include <windows.h>

#include <shlobj.h>

#include "core/base/PathUtil.h"
#include "core/base/Platform.h"
#include "platform/win/WinPaths.h"
#include "platform/win/WinUtf.h"

namespace kite::plat {

bool ReadTextFile(const std::string& utf8Path, std::string& out) {
    const std::wstring w = win::ToExtendedPath(utf8Path);
    HANDLE file = ::CreateFileW(w.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER size{};
    if (!::GetFileSizeEx(file, &size) || size.QuadPart > (32 << 20)) {
        ::CloseHandle(file);
        return false;
    }
    out.resize(static_cast<size_t>(size.QuadPart));
    DWORD read = 0;
    const bool ok = out.empty() ||
                    (::ReadFile(file, out.data(), static_cast<DWORD>(out.size()), &read, nullptr) &&
                     read == out.size());
    ::CloseHandle(file);
    if (!ok) out.clear();
    return ok;
}

bool WriteTextFile(const std::string& utf8Path, std::string_view data) {
    const std::wstring w = win::ToExtendedPath(utf8Path);
    HANDLE file = ::CreateFileW(w.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const bool ok = data.empty() ||
                    (::WriteFile(file, data.data(), static_cast<DWORD>(data.size()), &written,
                                 nullptr) &&
                     written == data.size());
    ::CloseHandle(file);
    return ok;
}

bool EnsureDirectory(const std::string& utf8Path) {
    const std::wstring w = win::ToWide(utf8Path);
    const int result = ::SHCreateDirectoryExW(nullptr, w.c_str(), nullptr);
    return result == ERROR_SUCCESS || result == ERROR_ALREADY_EXISTS ||
           result == ERROR_FILE_EXISTS;
}

uint64_t NowMs() { return ::GetTickCount64(); }

int64_t NowUnixSeconds() {
    FILETIME ft{};
    ::GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER ticks{};
    ticks.LowPart = ft.dwLowDateTime;
    ticks.HighPart = ft.dwHighDateTime;
    // FILETIME は 1601-01-01 起点の 100 ns 刻み。列挙が持ち帰る mtime と同じ物差しに
    // するため、ここで Unix 秒へ直す（fs::Entry::mtime も同じ変換を通っている）。
    constexpr uint64_t kTicksPerSecond = 10000000ull;
    constexpr int64_t kEpochDelta = 11644473600ll;  // 1601 から 1970 までの秒数
    return static_cast<int64_t>(ticks.QuadPart / kTicksPerSecond) - kEpochDelta;
}

std::string PreferredLanguage() {
    wchar_t buffer[LOCALE_NAME_MAX_LENGTH] = {};
    ULONG count = 0;
    ULONG chars = LOCALE_NAME_MAX_LENGTH;
    if (::GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, buffer, &chars) && count > 0) {
        const std::string full = win::ToUtf8(buffer);  // first entry, e.g. "ja-JP"
        const size_t dash = full.find('-');
        return dash == std::string::npos ? full : full.substr(0, dash);
    }
    return "en";
}

}  // namespace kite::plat
