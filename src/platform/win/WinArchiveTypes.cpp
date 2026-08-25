#include "platform/win/WinArchiveTypes.h"

#include <windows.h>

#include "platform/win/WinUtf.h"

namespace kite::win {
namespace {

// Everything a Windows might plausibly walk into, asked one by one. There is no
// cheap way to run the question the other way round - "which extensions point at
// a namespace handler" would mean enumerating the thousands of keys under HKCR -
// and the set of formats anyone ships a folder view for is small and slow to
// move, so a candidate list costs one registry open each and stays honest: what
// decides the answer is the registry, not this array.
//
// ".tar.gz" is not here and does not need to be: path::Extension() answers "gz",
// and the handler behind .gz shows the tar's contents directly (verified - a
// sample.tar.gz lists the directory inside the tar, not an intermediate .tar).
constexpr const char* kCandidates[] = {
    // Folders on every Windows since XP (zipfldr / cabview).
    "zip", "cab",
    // Windows 11 23H2 and later, through the libarchive-backed "ArchiveFolder".
    // Absent on Windows 10, where each of these falls through to whatever the
    // user installed - which is the fallback, not a failure.
    "tar", "tgz", "gz", "bz2", "tbz", "tbz2", "xz", "txz", "lzma",
    "zst", "tzst", "7z", "rar",
};

// HKEY_CLASSES_ROOT rather than HKCU/HKLM by hand: the merged view is the one
// the shell itself reads, so a per-user association answers here the same way it
// answers a double-click in Explorer.
bool ReadDefaultValue(const std::wstring& subkey, std::wstring& out) {
    HKEY key = nullptr;
    if (::RegOpenKeyExW(HKEY_CLASSES_ROOT, subkey.c_str(), 0, KEY_READ, &key) != ERROR_SUCCESS) {
        return false;
    }
    wchar_t buffer[256] = {};
    DWORD bytes = sizeof(buffer) - sizeof(wchar_t);
    DWORD type = 0;
    const LSTATUS status =
        ::RegQueryValueExW(key, nullptr, nullptr, &type, reinterpret_cast<BYTE*>(buffer), &bytes);
    ::RegCloseKey(key);
    if (status != ERROR_SUCCESS || type != REG_SZ) return false;
    out.assign(buffer);
    return !out.empty();
}

bool KeyExists(const std::wstring& subkey) {
    HKEY key = nullptr;
    if (::RegOpenKeyExW(HKEY_CLASSES_ROOT, subkey.c_str(), 0, KEY_READ, &key) != ERROR_SUCCESS) {
        return false;
    }
    ::RegCloseKey(key);
    return true;
}

// A namespace extension is a CLSID with a ShellFolder key and something to load.
// An ordinary handler - Windows.IsoFile, 7-Zip.7z - has neither, which is the
// whole reason to ask rather than to list: the same extension answers
// differently on two machines.
bool OpensAsFolder(const char* extension) {
    std::wstring progId;
    if (!ReadDefaultValue(L"." + ToWide(extension), progId)) return false;
    std::wstring clsid;
    if (!ReadDefaultValue(progId + L"\\CLSID", clsid)) return false;
    const std::wstring root = L"CLSID\\" + clsid;
    return KeyExists(root + L"\\ShellFolder") && KeyExists(root + L"\\InProcServer32");
}

}  // namespace

std::vector<std::string> ShellFolderExtensions() {
    std::vector<std::string> found;
    for (const char* candidate : kCandidates) {
        if (OpensAsFolder(candidate)) found.emplace_back(candidate);
    }
    return found;
}

}  // namespace kite::win
