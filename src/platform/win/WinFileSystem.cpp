#include "platform/win/WinFileSystem.h"

#include <windows.h>

#include <knownfolders.h>
#include <shellapi.h>
#include <shlobj.h>

#include <cstdio>

#include "core/app/ConfigDir.h"
#include "core/base/PathUtil.h"
#include "core/fs/VirtualPath.h"
#include "platform/win/VirtualNames.h"
#include "platform/win/WinPaths.h"
#include "platform/win/WinUtf.h"

namespace kite::win {
namespace {

int64_t ToUnixSeconds(const FILETIME& ft) {
    ULARGE_INTEGER v;
    v.LowPart = ft.dwLowDateTime;
    v.HighPart = ft.dwHighDateTime;
    if (v.QuadPart == 0) return 0;
    // FILETIME counts 100 ns ticks from 1601-01-01.
    return static_cast<int64_t>((v.QuadPart - 116444736000000000ULL) / 10000000ULL);
}

fs::Attr TranslateAttributes(DWORD a) {
    fs::Attr out = fs::Attr::None;
    if (a & FILE_ATTRIBUTE_DIRECTORY) out |= fs::Attr::Directory;
    if (a & FILE_ATTRIBUTE_HIDDEN) out |= fs::Attr::Hidden;
    if (a & FILE_ATTRIBUTE_SYSTEM) out |= fs::Attr::System;
    if (a & FILE_ATTRIBUTE_READONLY) out |= fs::Attr::ReadOnly;
    if (a & FILE_ATTRIBUTE_REPARSE_POINT) out |= fs::Attr::Link;
    if (a & FILE_ATTRIBUTE_COMPRESSED) out |= fs::Attr::Compressed;
    if (a & FILE_ATTRIBUTE_ENCRYPTED) out |= fs::Attr::Encrypted;
    if (a & FILE_ATTRIBUTE_OFFLINE) out |= fs::Attr::Offline;
    // Set by OneDrive / Box / Dropbox on files that exist only in the cloud.
    if (a & (FILE_ATTRIBUTE_RECALL_ON_OPEN | FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS)) {
        out |= fs::Attr::Placeholder;
    }
    return out;
}

fs::Status TranslateError(DWORD code) {
    switch (code) {
        case ERROR_FILE_NOT_FOUND:
        case ERROR_PATH_NOT_FOUND:
        case ERROR_INVALID_NAME:
        case ERROR_BAD_NET_NAME:
            return fs::Status::NotFound;
        case ERROR_ACCESS_DENIED:
        case ERROR_SHARING_VIOLATION:
        // A share that wants a logon reports one of these. They are the ones
        // Cmd::ConnectNetwork answers, so they must not be filed as "gone".
        case ERROR_LOGON_FAILURE:
        case ERROR_INVALID_PASSWORD:
        case ERROR_NO_SUCH_USER:
        case ERROR_SESSION_CREDENTIAL_CONFLICT:
            return fs::Status::AccessDenied;
        case ERROR_NOT_READY:
        case ERROR_BAD_NETPATH:
        case ERROR_NETWORK_UNREACHABLE:
        case ERROR_UNEXP_NET_ERR:
        case ERROR_DEV_NOT_EXIST:
        case ERROR_NO_NETWORK:
        case ERROR_NO_NET_OR_BAD_PATH:
        case ERROR_NETNAME_DELETED:
        // The drive was pulled out - during the enumeration, if the listing got
        // far enough to start one. Reported as "unavailable" rather than "gone"
        // because the folder is fine; what is missing is the medium under it.
        case ERROR_DEVICE_NOT_CONNECTED:
        case ERROR_DEVICE_REMOVED:
        case ERROR_NO_MEDIA_IN_DRIVE:
        case ERROR_UNRECOGNIZED_VOLUME:
        case ERROR_WRONG_DISK:
        case ERROR_MEDIA_CHANGED:
        case ERROR_INVALID_DRIVE:
        // A cloud folder whose provider is asleep, offline, or still thinking.
        // Explorer shows these as a sync error rather than an empty folder, and
        // so must Kite: "this folder is empty" about a folder full of files is
        // the one answer a filer must never give.
        case ERROR_CLOUD_FILE_PROVIDER_NOT_RUNNING:
        case ERROR_CLOUD_FILE_NETWORK_UNAVAILABLE:
        case ERROR_CLOUD_FILE_REQUEST_TIMEOUT:
        case ERROR_CLOUD_FILE_REQUEST_ABORTED:
        case ERROR_CLOUD_FILE_UNSUCCESSFUL:
        case ERROR_CLOUD_FILE_PROVIDER_TERMINATED:
        case ERROR_CLOUD_FILE_IN_USE:
        case ERROR_CLOUD_FILE_PINNED:
            return fs::Status::Unavailable;
        case ERROR_CLOUD_FILE_ACCESS_DENIED:
        case ERROR_CLOUD_FILE_AUTHENTICATION_FAILED:
            return fs::Status::AccessDenied;
        default:
            return fs::Status::Error;
    }
}

// The shares a server offers. FindFirstFile cannot answer this - "\\srv\*" is
// not a pattern the filesystem knows - so the enumeration has to come from the
// network provider instead. Called on a loader worker like every other listing:
// an unreachable host takes its full TCP timeout to say so.
fs::ListResult ListShares(const std::string& server) {
    fs::ListResult result;

    std::wstring remote = ToWide(server);
    while (!remote.empty() && (remote.back() == L'\\' || remote.back() == L'/')) remote.pop_back();

    NETRESOURCEW spec{};
    spec.dwScope = RESOURCE_GLOBALNET;
    spec.dwType = RESOURCETYPE_DISK;
    spec.dwDisplayType = RESOURCEDISPLAYTYPE_SERVER;
    spec.dwUsage = RESOURCEUSAGE_CONTAINER;
    spec.lpRemoteName = remote.data();

    HANDLE handle = nullptr;
    DWORD code = ::WNetOpenEnumW(RESOURCE_GLOBALNET, RESOURCETYPE_DISK, 0, &spec, &handle);
    if (code != NO_ERROR) {
        result.status = TranslateError(code);
        result.message = ErrorText(code);
        return result;
    }

    std::vector<char> buffer(16 * 1024);
    for (;;) {
        DWORD count = 0xFFFFFFFF;  // as many as fit
        DWORD bytes = static_cast<DWORD>(buffer.size());
        code = ::WNetEnumResourceW(handle, &count, buffer.data(), &bytes);
        if (code == ERROR_MORE_DATA) {
            // Not even one entry fit. `bytes` now says how much would - but a
            // provider that asks for no more than it was given would spin this
            // loop forever, on a worker thread, with no way to interrupt it.
            if (bytes <= buffer.size()) break;
            buffer.resize(bytes);
            continue;
        }
        if (code != NO_ERROR) break;

        const auto* items = reinterpret_cast<const NETRESOURCEW*>(buffer.data());
        for (DWORD i = 0; i < count; ++i) {
            if (!items[i].lpRemoteName) continue;
            const std::string full = ToUtf8(items[i].lpRemoteName);
            // Not path::FileName: a share is a root, so that would hand back the
            // whole "\\server\share" rather than the leaf.
            const size_t serverLen = path::UncServerLength(full);
            if (serverLen == 0 || full.size() <= serverLen + 1) continue;
            fs::Entry e;
            e.name = full.substr(serverLen + 1);
            while (!e.name.empty() && path::IsSep(e.name.back())) e.name.pop_back();
            if (e.name.empty()) continue;
            e.attrs = fs::Attr::Directory;
            // ADMIN$, C$, IPC$ - the shares Explorer keeps out of sight too.
            if (e.name.back() == '$') e.attrs |= fs::Attr::Hidden;
            result.entries.push_back(std::move(e));
        }
    }
    ::WNetCloseEnum(handle);

    // Anything but "that was the last one" means the walk was cut short, and a
    // half-listing presented as whole is worse than saying nothing worked.
    if (code != ERROR_NO_MORE_ITEMS) {
        result.entries.clear();
        result.status = TranslateError(code);
        result.message = ErrorText(code);
    }
    return result;
}

std::string KnownFolder(REFKNOWNFOLDERID id) {
    PWSTR raw = nullptr;
    if (FAILED(::SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &raw))) return {};
    std::string out = ToUtf8(raw);
    ::CoTaskMemFree(raw);
    return out;
}

// A double-null-terminated wide list, as the SHFileOperation API wants.
std::wstring MakeDoubleNullList(const std::vector<std::string>& paths) {
    std::wstring out;
    for (const std::string& p : paths) {
        out += ToWide(p);
        out.push_back(L'\0');
    }
    out.push_back(L'\0');
    return out;
}

// What SHFileOperation returned, in words.
//
// Below 0x71 the value is a plain Win32 error, but from there up it is one of
// the shell's own DE_* codes, and those collide with unrelated Win32 ones -
// DE_ACCESSDENIEDSRC is 0x78 = 120, which FormatMessage calls "this function is
// not supported on this system". Handing that to the user is worse than saying
// nothing, so the shell codes are mapped to the Win32 error that means the same
// thing (and are then worded by the OS, in the OS's language) and everything
// left over falls back to its number.
std::string FileOperationError(int code) {
    DWORD win32 = 0;
    switch (code) {
        case 0x74:  // DE_ROOTDIR: a drive root is not something you can move
        case 0x10074: win32 = ERROR_INVALID_PARAMETER; break;
        case 0x78: win32 = ERROR_ACCESS_DENIED; break;         // DE_ACCESSDENIEDSRC
        case 0x79:                                             // DE_PATHTOODEEP
        case 0x81: win32 = ERROR_FILENAME_EXCED_RANGE; break;  // DE_FILENAMETOOLONG
        case 0x7C: win32 = ERROR_FILE_NOT_FOUND; break;        // DE_INVALIDFILES
        case 0x85: win32 = ERROR_FILE_TOO_LARGE; break;        // DE_FILE_TOO_LARGE
        default: break;
    }
    if (win32 == 0 && code > 0 && code < 0x71) win32 = static_cast<DWORD>(code);
    if (win32 != 0) {
        std::string text = ErrorText(win32);
        if (!text.empty()) return text;
    }
    // No wording for it. The number is at least something to search for, and
    // hex is how the shell's own codes are written down.
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "SHFileOperation 0x%X", static_cast<unsigned>(code));
    return buffer;
}

// COM for the duration of one operation, on whatever thread is running it.
//
// File operations no longer happen on the UI thread - they are handed to the
// FileOpQueue worker, and that thread starts life without an apartment. The
// shell's copy engine has to be able to load copy hooks and put up its own
// progress and conflict dialogs, so it gets one here.
//
// S_FALSE means the thread already had an apartment and this call only added a
// reference; that still has to be balanced. RPC_E_CHANGED_MODE means it has one
// of the other kind, which is not ours to take down.
class ComForThisCall {
public:
    ComForThisCall() {
        const HRESULT hr = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        owned_ = SUCCEEDED(hr);
    }
    ~ComForThisCall() {
        if (owned_) ::CoUninitialize();
    }
    ComForThisCall(const ComForThisCall&) = delete;
    ComForThisCall& operator=(const ComForThisCall&) = delete;

private:
    bool owned_ = false;
};

bool RunFileOperation(UINT op, const std::vector<std::string>& from,
                      const std::vector<std::string>& to, FILEOP_FLAGS flags, std::string* err) {
    ComForThisCall com;

    std::wstring fromList = MakeDoubleNullList(from);
    // A list rather than a single folder, because a duplicate names its own
    // destination per item (FOF_MULTIDESTFILES). One folder is the one-element
    // case of the same thing.
    std::wstring toList;
    if (!to.empty()) toList = MakeDoubleNullList(to);

    SHFILEOPSTRUCTW spec{};
    spec.wFunc = op;
    spec.pFrom = fromList.c_str();
    spec.pTo = toList.empty() ? nullptr : toList.c_str();
    spec.fFlags = flags;

    const int result = ::SHFileOperationW(&spec);
    // DE_OPCANCELLED and ERROR_CANCELLED are the shell reporting that the user
    // pressed Cancel in its own dialog, which is not a failure to complain
    // about - the same call answers with fAnyOperationsAborted when the cancel
    // came mid-way, and both spellings have to read the same from here.
    if (result == 0x75 || result == static_cast<int>(ERROR_CANCELLED)) {
        if (err) err->clear();
        return true;
    }
    if (result != 0) {
        if (err) *err = FileOperationError(result);
        return false;
    }
    if (spec.fAnyOperationsAborted) {
        if (err) err->clear();
        return true;  // user cancelled; not an error worth shouting about
    }
    return true;
}

bool DirectoryExists(const std::string& path) {
    if (path.empty()) return false;
    const DWORD attrs = ::GetFileAttributesW(ToExtendedPath(path).c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool LooksLikeCloudLabel(const std::string& label) {
    static const char* kMarkers[] = { "Google Drive", "Box", "Dropbox", "OneDrive",
                                      "iCloud", "pCloud", "MEGA" };
    for (const char* marker : kMarkers) {
        if (label.find(marker) != std::string::npos) return true;
    }
    return false;
}

// How much room the volume a folder sits on has, and how much of it is left.
// Both stay 0 when the question cannot be answered.
//
// Asked here, beside the enumeration, because both wait on the same slow thing:
// on a cold share this takes as long as FindFirstFile does. Here it is a loader
// worker that pays, and the walk has just brought the connection up; asked from
// the status bar it would be the UI thread, once per frame.
void ReadVolumeSpace(const std::string& dir, fs::ListResult& out) {
    // Both numbers come from the quota-aware pair - what this user may still
    // write, out of what this user may write at all. Mixing in the volume-wide
    // total would make the difference between them, which is what the status
    // bar shows as used, larger than anything this account ever wrote.
    ULARGE_INTEGER avail{}, total{};
    if (!::GetDiskFreeSpaceExW(ToExtendedPath(dir).c_str(), &avail, &total, nullptr)) return;
    out.freeBytes = avail.QuadPart;
    out.totalBytes = total.QuadPart;
}

}  // namespace

WinFileSystem::WinFileSystem() {
    // Stops Windows from popping a "no disk in drive" dialog when we probe an
    // empty card reader on a worker thread.
    ::SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);

    home_ = KnownFolder(FOLDERID_Profile);

    // Portable first: a "config" folder next to the exe means the whole of Kite
    // is that one folder, which is how the zip is meant to be used. Nothing is
    // created here - only an existing folder switches Kite over, so extracting
    // the zip somewhere unwritable (Program Files) is the user's own doing
    // rather than something a first run silently arranged.
    const std::string exeDir = ToUtf8(ModuleDirectory());
    const std::string portable = exeDir.empty() ? std::string() : path::Join(exeDir, "config");

    const std::string appData = KnownFolder(FOLDERID_RoamingAppData);
    const std::string roaming = appData.empty() ? path::Join(home_, "Kite")
                                                : path::Join(appData, "Kite");

    configDir_ = config::Choose({ { portable, DirectoryExists(portable) },
                                  { roaming, DirectoryExists(roaming) } });
}

fs::ListResult WinFileSystem::ListVirtual(const std::string& dir) {
    fs::ListResult result;

    shellhost::FolderResponse answer;
    if (!folders_.List(ToShellParsingName(dir), answer)) {
        // The host could not be started, did not answer in time, or died with a
        // namespace extension inside it. All three read the same from here, and
        // none of them is "the folder is empty".
        result.status = fs::Status::Unavailable;
        return result;
    }

    switch (answer.status) {
        case shellhost::FolderStatus::Ok: result.status = fs::Status::Ok; break;
        case shellhost::FolderStatus::NotFound: result.status = fs::Status::NotFound; break;
        case shellhost::FolderStatus::AccessDenied: result.status = fs::Status::AccessDenied; break;
        case shellhost::FolderStatus::Unavailable: result.status = fs::Status::Unavailable; break;
        default: result.status = fs::Status::Error; break;
    }
    result.message = answer.message;
    result.title = answer.title;
    if (result.status != fs::Status::Ok) return result;

    // Nothing inside an archive is on the filesystem, whatever the shell says
    // about it: the parsing name reads like a path ("C:\a.zip\notes.txt") and
    // some namespace handlers do set SFGAO_FILESYSTEM on it, but FindFirstFile
    // cannot open it and no watcher can watch it. Deciding it here rather than
    // trusting the bit keeps one answer for "is this a real folder".
    const bool insideArchive = !vfs::ArchiveFileOf(dir).empty();

    result.entries.reserve(answer.entries.size());
    for (const shellhost::FolderEntry& e : answer.entries) {
        using Bit = shellhost::FolderAttr;
        const bool fileSystem =
            !insideArchive && (e.attrs & static_cast<uint32_t>(Bit::FileSystem)) != 0;

        fs::Entry entry;
        entry.name = e.name;
        entry.size = e.size;
        entry.mtime = e.mtime;
        // A drive under "PC" comes back as "C:\\" and is simply that folder from
        // here on: the fast path lists it, the watcher watches it, and nothing
        // downstream has to know it was reached through the shell namespace.
        entry.address = FromShellParsingName(e.parsing, fileSystem);
        if (e.attrs & static_cast<uint32_t>(Bit::Directory)) entry.attrs |= fs::Attr::Directory;
        if (e.attrs & static_cast<uint32_t>(Bit::Hidden)) entry.attrs |= fs::Attr::Hidden;
        if (e.attrs & static_cast<uint32_t>(Bit::Link)) entry.attrs |= fs::Attr::Link;
        result.entries.push_back(std::move(entry));
    }
    return result;
}

fs::ListResult WinFileSystem::List(const std::string& dir) {
    fs::ListResult result;
    if (dir.empty()) {
        result.status = fs::Status::NotFound;
        return result;
    }
    if (vfs::IsVirtual(dir)) return ListVirtual(dir);
    if (path::IsUncServer(dir)) return ListShares(dir);

    std::wstring pattern = ToExtendedPath(dir);
    if (!pattern.empty() && pattern.back() != L'\\' && pattern.back() != L'/') {
        pattern.push_back(L'\\');
    }
    pattern += L'*';

    WIN32_FIND_DATAW find{};
    // FindExInfoBasic skips the 8.3 name lookup and LARGE_FETCH batches the
    // round trips - together they matter a lot on network and cloud folders.
    HANDLE handle = ::FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &find,
                                       FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
    if (handle == INVALID_HANDLE_VALUE) {
        const DWORD code = ::GetLastError();
        if (code == ERROR_FILE_NOT_FOUND) {  // genuinely empty
            ReadVolumeSpace(dir, result);
            return result;
        }
        result.status = TranslateError(code);
        result.message = ErrorText(code);
        return result;
    }

    result.entries.reserve(256);
    do {
        if (find.cFileName[0] == L'.' &&
            (find.cFileName[1] == L'\0' || (find.cFileName[1] == L'.' && find.cFileName[2] == L'\0'))) {
            continue;
        }
        fs::Entry e;
        e.name = ToUtf8(find.cFileName);
        e.attrs = TranslateAttributes(find.dwFileAttributes);
        if (!e.isDir()) {
            e.size = (static_cast<uint64_t>(find.nFileSizeHigh) << 32) | find.nFileSizeLow;
        }
        e.mtime = ToUnixSeconds(find.ftLastWriteTime);
        result.entries.push_back(std::move(e));
    } while (::FindNextFileW(handle, &find));

    // Why the walk stopped has to be asked before FindClose, which overwrites
    // it. Anything but "that was the last one" means the medium went away under
    // the enumeration - a USB drive pulled out, a share that dropped - and a
    // half listing presented as whole reads as "the rest was deleted", which is
    // the one wrong answer worth going out of the way to avoid (ListShares does
    // the same for the same reason).
    const DWORD stop = ::GetLastError();
    ::FindClose(handle);
    // ERROR_SUCCESS is accepted alongside it: a driver that forgets to set the
    // code has not said the enumeration was cut short, and throwing away a good
    // listing on the strength of a zero would be the worse mistake.
    if (stop != ERROR_NO_MORE_FILES && stop != ERROR_SUCCESS) {
        result.entries.clear();
        result.status = TranslateError(stop);
        result.message = ErrorText(stop);
        return result;
    }
    ReadVolumeSpace(dir, result);
    return result;
}

bool WinFileSystem::Exists(const std::string& p, bool* isDir) {
    // Answered without asking anyone. The question is only ever "can this be
    // opened as a folder", and starting the shell host to confirm what the
    // three built-in places already are would be a process per keystroke.
    if (vfs::IsVirtual(p)) {
        if (isDir) *isDir = true;
        return true;
    }
    const DWORD attrs = ::GetFileAttributesW(ToExtendedPath(p).c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) return false;
    if (isDir) *isDir = (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
    return true;
}

std::vector<fs::Root> WinFileSystem::Roots() {
    std::vector<fs::Root> out;
    const DWORD mask = ::GetLogicalDrives();

    for (int i = 0; i < 26; ++i) {
        if (!(mask & (1u << i))) continue;
        const wchar_t letter = static_cast<wchar_t>(L'A' + i);
        const std::wstring root = std::wstring(1, letter) + L":\\";

        fs::Root entry;
        entry.path = ToUtf8(root);

        switch (::GetDriveTypeW(root.c_str())) {
            case DRIVE_FIXED: entry.kind = fs::RootKind::Fixed; break;
            case DRIVE_REMOVABLE: entry.kind = fs::RootKind::Removable; break;
            case DRIVE_REMOTE: entry.kind = fs::RootKind::Network; break;
            case DRIVE_CDROM: entry.kind = fs::RootKind::Optical; break;
            case DRIVE_RAMDISK: entry.kind = fs::RootKind::Ram; break;
            default: entry.kind = fs::RootKind::Unknown; break;
        }

        std::string label;
        if (entry.kind != fs::RootKind::Network) {
            wchar_t name[MAX_PATH] = {};
            if (::GetVolumeInformationW(root.c_str(), name, MAX_PATH, nullptr, nullptr, nullptr,
                                        nullptr, 0)) {
                label = ToUtf8(name);
            }
            // Only ask for capacity where the answer is cheap; a disconnected
            // network drive would block the caller for seconds.
            if (entry.kind == fs::RootKind::Fixed || entry.kind == fs::RootKind::Removable) {
                ULARGE_INTEGER freeBytes{}, totalBytes{}, totalFree{};
                if (::GetDiskFreeSpaceExW(root.c_str(), &freeBytes, &totalBytes, &totalFree)) {
                    entry.freeBytes = freeBytes.QuadPart;
                    entry.totalBytes = totalBytes.QuadPart;
                }
            }
        } else {
            wchar_t remote[512] = {};
            DWORD length = 512;
            const std::wstring device = std::wstring(1, letter) + L":";
            if (::WNetGetConnectionW(device.c_str(), remote, &length) == NO_ERROR) {
                label = ToUtf8(remote);
            }
        }

        const std::string drive = std::string(1, static_cast<char>('A' + i)) + ":";
        entry.label = label.empty() ? drive : (label + " (" + drive + ")");
        if (LooksLikeCloudLabel(label)) entry.kind = fs::RootKind::Cloud;
        out.push_back(std::move(entry));
    }
    return out;
}

std::string WinFileSystem::HomeDir() { return home_; }
std::string WinFileSystem::ConfigDir() { return configDir_; }

std::vector<fs::Root> WinFileSystem::QuickAccess() {
    struct Spec {
        const KNOWNFOLDERID* id;
        const char* fallbackLabel;
    };
    const Spec specs[] = {
        { &FOLDERID_Profile, "Home" },     { &FOLDERID_Desktop, "Desktop" },
        { &FOLDERID_Documents, "Documents" }, { &FOLDERID_Downloads, "Downloads" },
        { &FOLDERID_Pictures, "Pictures" },   { &FOLDERID_Music, "Music" },
        { &FOLDERID_Videos, "Videos" },
    };

    std::vector<fs::Root> out;
    // The shell namespace places come first, in the order Explorer's own tree
    // puts them. They carry no label: naming them is App::RefreshRoots's job,
    // because the name has to follow Kite's language rather than Windows's.
    for (const char* id : { vfs::kComputer, vfs::kRecycleBin, vfs::kNetwork }) {
        fs::Root entry;
        entry.path = id;
        entry.kind = fs::RootKind::Special;
        out.push_back(std::move(entry));
    }

    for (const Spec& spec : specs) {
        const std::string p = KnownFolder(*spec.id);
        if (p.empty()) continue;
        fs::Root entry;
        entry.path = p;
        entry.label = path::DisplayName(p);
        if (entry.label.empty()) entry.label = spec.fallbackLabel;
        entry.kind = fs::RootKind::Special;
        out.push_back(std::move(entry));
    }

    // Cloud folders that mount as a directory rather than a drive letter.
    const char* kCloudDirs[] = { "OneDrive", "Box", "Dropbox", "Google Drive" };
    for (const char* name : kCloudDirs) {
        const std::string p = path::Join(home_, name);
        bool isDir = false;
        if (Exists(p, &isDir) && isDir) {
            fs::Root entry;
            entry.path = p;
            entry.label = name;
            entry.kind = fs::RootKind::Cloud;
            out.push_back(std::move(entry));
        }
    }
    return out;
}

bool WinFileSystem::MakeDirectory(const std::string& p, std::string* err) {
    if (::CreateDirectoryW(ToExtendedPath(p).c_str(), nullptr)) return true;
    if (err) *err = ErrorText(::GetLastError());
    return false;
}

bool WinFileSystem::MakeFile(const std::string& p, std::string* err) {
    HANDLE handle = ::CreateFileW(ToExtendedPath(p).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        if (err) *err = ErrorText(::GetLastError());
        return false;
    }
    ::CloseHandle(handle);
    return true;
}

bool WinFileSystem::Rename(const std::string& from, const std::string& to, std::string* err) {
    if (::MoveFileExW(ToExtendedPath(from).c_str(), ToExtendedPath(to).c_str(),
                      MOVEFILE_COPY_ALLOWED)) {
        return true;
    }
    if (err) *err = ErrorText(::GetLastError());
    return false;
}

bool WinFileSystem::Delete(const std::vector<std::string>& paths, bool toRecycleBin,
                           std::string* err) {
    if (paths.empty()) return true;
    FILEOP_FLAGS flags = FOF_NOCONFIRMMKDIR;
    if (toRecycleBin) flags |= FOF_ALLOWUNDO;
    // Kite asks for confirmation itself, so suppress the shell's own prompt.
    flags |= FOF_NOCONFIRMATION;
    return RunFileOperation(FO_DELETE, paths, {}, flags, err);
}

bool WinFileSystem::CopyTo(const std::vector<std::string>& paths, const std::string& destDir,
                           bool move, std::string* err) {
    if (paths.empty()) return true;
    // Keep the shell's progress and conflict UI here: reimplementing it badly
    // is how filers lose data.
    const FILEOP_FLAGS flags = FOF_NOCONFIRMMKDIR | FOF_ALLOWUNDO;
    return RunFileOperation(move ? FO_MOVE : FO_COPY, paths, { destDir }, flags, err);
}

bool WinFileSystem::CopyAs(const std::vector<std::string>& paths,
                           const std::vector<std::string>& destPaths, std::string* err) {
    if (paths.empty()) return true;
    // Equal lengths are the whole contract of FOF_MULTIDESTFILES: the shell pairs
    // the two lists by position, so a short one would hand a name to the wrong
    // file. Refused rather than trimmed - there is no half of this worth doing.
    if (paths.size() != destPaths.size()) {
        if (err) *err = ErrorText(ERROR_INVALID_PARAMETER);
        return false;
    }
    // No FOF_NOCONFIRMATION: nothing held these names a moment ago, but another
    // window can have taken one since, and that is the shell's dialog to show.
    const FILEOP_FLAGS flags = FOF_NOCONFIRMMKDIR | FOF_ALLOWUNDO | FOF_MULTIDESTFILES;
    return RunFileOperation(FO_COPY, paths, destPaths, flags, err);
}

}  // namespace kite::win
