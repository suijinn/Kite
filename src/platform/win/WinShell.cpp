#include "platform/win/WinShell.h"

#include <objbase.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>

#include "core/base/PathUtil.h"
#include "core/fs/VirtualPath.h"
#include "platform/win/VirtualNames.h"
#include "platform/win/WinUtf.h"

namespace kite::win {
namespace {

PIDLIST_ABSOLUTE ParsePath(const std::string& path) {
    PIDLIST_ABSOLUTE pidl = nullptr;
    const std::wstring w = ToWide(path);
    if (FAILED(::SHParseDisplayName(w.c_str(), nullptr, &pidl, 0, nullptr))) return nullptr;
    return pidl;
}

bool ShellExecuteVerb(HWND hwnd, const std::string& path, const wchar_t* verb, DWORD mask) {
    const std::wstring w = ToWide(path);
    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = mask;
    info.hwnd = hwnd;
    info.lpVerb = verb;
    info.lpFile = w.c_str();
    info.nShow = SW_SHOWNORMAL;
    return ::ShellExecuteExW(&info) != FALSE;
}

}  // namespace

bool EnsureOle() {
    // Deferred on purpose: a cold start that never opens a shell menu and never
    // drags anything does not pay for COM or OLE initialization.
    static bool initialized = false;
    if (!initialized) {
        const HRESULT hr = ::OleInitialize(nullptr);
        initialized = SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE;
    }
    return initialized;
}

IDataObject* CreateShellDataObject(const std::vector<std::string>& paths) {
    if (paths.empty() || !EnsureOle()) return nullptr;

    std::vector<PIDLIST_ABSOLUTE> absolute;
    absolute.reserve(paths.size());
    for (const std::string& p : paths) {
        if (PIDLIST_ABSOLUTE pidl = ParsePath(p)) absolute.push_back(pidl);
    }
    if (absolute.empty()) return nullptr;

    IShellFolder* parent = nullptr;
    PCUITEMID_CHILD firstChild = nullptr;
    IDataObject* data = nullptr;

    if (SUCCEEDED(::SHBindToParent(absolute[0], IID_IShellFolder,
                                   reinterpret_cast<void**>(&parent), &firstChild))) {
        std::vector<PCUITEMID_CHILD> children;
        children.reserve(absolute.size());
        for (PIDLIST_ABSOLUTE pidl : absolute) children.push_back(::ILFindLastID(pidl));

        // Going through the shell (rather than hand-rolling a CF_HDROP object)
        // is what makes drops into Explorer, archivers and mail clients behave
        // exactly like a drag started from Explorer itself.
        parent->GetUIObjectOf(nullptr, static_cast<UINT>(children.size()),
                              reinterpret_cast<PCUITEMID_CHILD_ARRAY>(children.data()),
                              IID_IDataObject, nullptr, reinterpret_cast<void**>(&data));
        parent->Release();
    }

    for (PIDLIST_ABSOLUTE pidl : absolute) ::CoTaskMemFree(pidl);
    return data;
}

std::vector<std::string> ExtractDroppedPaths(IDataObject* data) {
    std::vector<std::string> out;
    if (!data) return out;

    FORMATETC format{};
    format.cfFormat = CF_HDROP;
    format.dwAspect = DVASPECT_CONTENT;
    format.lindex = -1;
    format.tymed = TYMED_HGLOBAL;

    STGMEDIUM medium{};
    if (FAILED(data->GetData(&format, &medium))) return out;

    if (auto drop = static_cast<HDROP>(::GlobalLock(medium.hGlobal))) {
        const UINT count = ::DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
        for (UINT i = 0; i < count; ++i) {
            const UINT length = ::DragQueryFileW(drop, i, nullptr, 0);
            std::wstring buffer(static_cast<size_t>(length) + 1, L'\0');
            if (::DragQueryFileW(drop, i, buffer.data(), length + 1)) {
                buffer.resize(length);
                out.push_back(ToUtf8(buffer));
            }
        }
        ::GlobalUnlock(medium.hGlobal);
    }
    ::ReleaseStgMedium(&medium);
    return out;
}

bool WinShell::ShowContextMenu(const std::string& folder, const std::vector<std::string>& paths,
                               int screenX, int screenY, bool extended, bool background,
                               bool dark) {
    // Everything about this call happens in kite_shellhost.exe. Nothing below
    // this line loads a shell extension, and nothing above it should either.
    std::vector<std::string> shellPaths;
    shellPaths.reserve(paths.size());
    for (const std::string& p : paths) shellPaths.push_back(ToShellPath(p));
    return menuHost_.ShowContextMenu(hwnd_, ToShellPath(folder), shellPaths, screenX, screenY,
                                     extended, background, dark);
}

bool WinShell::RestoreFromTrash(const std::vector<std::string>& paths) {
    if (paths.empty()) return false;
    std::vector<std::string> shellPaths;
    shellPaths.reserve(paths.size());
    for (const std::string& p : paths) shellPaths.push_back(ToShellPath(p));
    // "undelete" is the shell's own verb for this, and going through it means
    // the Recycle Bin's bookkeeping ($I record, original location, the entry
    // itself) is unwound by the code that wrote it.
    return menuHost_.InvokeVerb(hwnd_, ToShellPath(vfs::kRecycleBin), shellPaths, "undelete",
                                false);
}

bool WinShell::RestoreDeleted(const std::vector<std::string>& originalPaths) {
    if (originalPaths.empty()) return false;
    // The paths go through unchanged: these are the real paths the files had
    // before they were deleted, which is exactly what the bin recorded.
    return menuHost_.InvokeVerb(hwnd_, ToShellPath(vfs::kRecycleBin), originalPaths, "undelete",
                                true);
}

bool WinShell::Open(const std::string& path) {
    return ShellExecuteVerb(hwnd_, path, nullptr, SEE_MASK_FLAG_NO_UI);
}

bool WinShell::OpenWith(const std::string& path) {
    return ShellExecuteVerb(hwnd_, path, L"openas", SEE_MASK_INVOKEIDLIST);
}

bool WinShell::ShowProperties(const std::string& path) {
    if (!EnsureOle()) return false;
    return ShellExecuteVerb(hwnd_, path, L"properties",
                            SEE_MASK_INVOKEIDLIST | SEE_MASK_FLAG_NO_UI);
}

bool WinShell::RevealInExplorer(const std::string& path) {
    if (!EnsureOle()) return false;
    PIDLIST_ABSOLUTE pidl = ParsePath(path);
    if (!pidl) return false;
    const HRESULT hr = ::SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0);
    ::CoTaskMemFree(pidl);
    return SUCCEEDED(hr);
}

bool WinShell::OpenTerminal(const std::string& dir) {
    const std::wstring w = ToWide(dir);
    // Windows Terminal if it is installed, otherwise the classic console.
    HINSTANCE result = ::ShellExecuteW(hwnd_, nullptr, L"wt.exe", nullptr, w.c_str(), SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(result) > 32) return true;
    result = ::ShellExecuteW(hwnd_, nullptr, L"cmd.exe", nullptr, w.c_str(), SW_SHOWNORMAL);
    return reinterpret_cast<INT_PTR>(result) > 32;
}

bool WinShell::SetClipboardText(const std::string& utf8) {
    if (!::OpenClipboard(hwnd_)) return false;
    ::EmptyClipboard();

    const std::wstring w = ToWide(utf8);
    const size_t bytes = (w.size() + 1) * sizeof(wchar_t);
    HGLOBAL block = ::GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (block) {
        if (void* dest = ::GlobalLock(block)) {
            ::memcpy(dest, w.c_str(), bytes);
            ::GlobalUnlock(block);
            ::SetClipboardData(CF_UNICODETEXT, block);
        } else {
            ::GlobalFree(block);
        }
    }
    ::CloseClipboard();
    return block != nullptr;
}

bool WinShell::SetClipboardFiles(const std::vector<std::string>& paths, bool cut) {
    if (paths.empty() || !::OpenClipboard(hwnd_)) return false;
    ::EmptyClipboard();

    std::wstring list;
    for (const std::string& p : paths) {
        list += ToWide(p);
        list.push_back(L'\0');
    }
    list.push_back(L'\0');

    const size_t bytes = sizeof(DROPFILES) + list.size() * sizeof(wchar_t);
    HGLOBAL block = ::GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!block) {
        ::CloseClipboard();
        return false;
    }
    if (auto* drop = static_cast<DROPFILES*>(::GlobalLock(block))) {
        drop->pFiles = sizeof(DROPFILES);
        drop->pt = { 0, 0 };
        drop->fNC = FALSE;
        drop->fWide = TRUE;
        ::memcpy(reinterpret_cast<char*>(drop) + sizeof(DROPFILES), list.data(),
                 list.size() * sizeof(wchar_t));
        ::GlobalUnlock(block);
        ::SetClipboardData(CF_HDROP, block);
    }

    // Explorer reads this to tell a cut from a copy.
    const UINT effectFormat = ::RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT);
    if (HGLOBAL effectBlock = ::GlobalAlloc(GMEM_MOVEABLE, sizeof(DWORD))) {
        if (auto* effect = static_cast<DWORD*>(::GlobalLock(effectBlock))) {
            *effect = cut ? DROPEFFECT_MOVE : DROPEFFECT_COPY;
            ::GlobalUnlock(effectBlock);
            ::SetClipboardData(effectFormat, effectBlock);
        }
    }

    ::CloseClipboard();
    return true;
}

bool WinShell::GetClipboardFiles(std::vector<std::string>& paths, bool* cut) {
    if (!::IsClipboardFormatAvailable(CF_HDROP)) return false;
    if (!::OpenClipboard(hwnd_)) return false;

    bool ok = false;
    if (HANDLE data = ::GetClipboardData(CF_HDROP)) {
        HDROP drop = static_cast<HDROP>(data);
        const UINT count = ::DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
        for (UINT i = 0; i < count; ++i) {
            const UINT length = ::DragQueryFileW(drop, i, nullptr, 0);
            std::wstring buffer(length + 1, L'\0');
            if (::DragQueryFileW(drop, i, buffer.data(), length + 1)) {
                buffer.resize(length);
                paths.push_back(ToUtf8(buffer));
            }
        }
        ok = !paths.empty();
    }

    if (cut) {
        *cut = false;
        const UINT effectFormat = ::RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT);
        if (HANDLE data = ::GetClipboardData(effectFormat)) {
            if (auto* effect = static_cast<DWORD*>(::GlobalLock(data))) {
                *cut = (*effect & DROPEFFECT_MOVE) != 0;
                ::GlobalUnlock(data);
            }
        }
    }

    ::CloseClipboard();
    return ok;
}

bool WinShell::GetClipboardText(std::string& utf8) {
    if (!::IsClipboardFormatAvailable(CF_UNICODETEXT)) return false;
    if (!::OpenClipboard(hwnd_)) return false;

    bool ok = false;
    if (HANDLE data = ::GetClipboardData(CF_UNICODETEXT)) {
        if (const auto* text = static_cast<const wchar_t*>(::GlobalLock(data))) {
            utf8 = ToUtf8(text);
            ::GlobalUnlock(data);
            ok = true;
        }
    }
    ::CloseClipboard();
    return ok;
}

bool WinShell::ConnectNetwork(const std::string& uncRoot, std::string* err) {
    if (uncRoot.empty()) return false;

    // A share can be connected to directly; a bare server has no share to name,
    // and IPC$ is the one every SMB server offers for exactly this - it is what
    // carries the logon, after which the share enumeration succeeds.
    std::string target = uncRoot;
    while (!target.empty() && path::IsSep(target.back())) target.pop_back();
    if (path::IsUncServer(target)) target += "\\IPC$";

    std::wstring remote = ToWide(target);
    NETRESOURCEW spec{};
    spec.dwType = RESOURCETYPE_DISK;
    spec.lpRemoteName = remote.data();

    // No local name: the connection stays deviceless, so nothing appears as a
    // drive letter and nothing is left behind for the next logon to restore.
    const DWORD code = ::WNetAddConnection3W(hwnd_, &spec, nullptr, nullptr,
                                             CONNECT_INTERACTIVE | CONNECT_PROMPT);
    if (code == NO_ERROR || code == ERROR_ALREADY_ASSIGNED ||
        code == ERROR_SESSION_CREDENTIAL_CONFLICT) {
        return true;
    }
    // ERROR_CANCELLED is the user closing the credential dialog; there is
    // nothing to report about a decision they just made.
    if (err && code != ERROR_CANCELLED) *err = ErrorText(code);
    return false;
}

}  // namespace kite::win
