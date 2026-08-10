#include "platform/win/ShellMenu.h"

#include <shellapi.h>  // CMIC_MASK_UNICODE resolves to SEE_MASK_UNICODE, declared here
#include <shlobj.h>

#include "platform/win/WinUtf.h"

namespace kite::win {
namespace {

constexpr UINT kMenuFirstId = 0x1000;
constexpr UINT kMenuLastId = 0x7FFF;

// Set only while a shell menu is on screen; read by ForwardContextMenuMessage.
IContextMenu2* g_contextMenu2 = nullptr;
IContextMenu3* g_contextMenu3 = nullptr;

// --- SEH guards -------------------------------------------------------------
// These take raw pointers only, so they hold nothing that needs C++ unwinding
// and can legally use __try / __except. A caught fault costs the menu but
// leaves the host process able to serve the next request without a respawn.

HRESULT GuardedGetUIObjectOf(IShellFolder* parent, HWND owner, UINT count,
                             PCUITEMID_CHILD_ARRAY children, void** out) {
    __try {
        return parent->GetUIObjectOf(owner, count, children, IID_IContextMenu, nullptr, out);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return E_FAIL;
    }
}

HRESULT GuardedQueryContextMenu(IContextMenu* menu, HMENU hmenu, UINT flags) {
    __try {
        return menu->QueryContextMenu(hmenu, 0, kMenuFirstId, kMenuLastId, flags);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return E_FAIL;
    }
}

int GuardedTrackPopupMenu(HMENU hmenu, int x, int y, HWND owner) {
    __try {
        return static_cast<int>(::TrackPopupMenuEx(
            hmenu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON, x, y, owner,
            nullptr));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

HRESULT GuardedInvoke(IContextMenu* menu, CMINVOKECOMMANDINFOEX* info) {
    __try {
        return menu->InvokeCommand(reinterpret_cast<CMINVOKECOMMANDINFO*>(info));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return E_FAIL;
    }
}

PIDLIST_ABSOLUTE ParsePath(const std::string& path) {
    PIDLIST_ABSOLUTE pidl = nullptr;
    const std::wstring w = ToWide(path);
    if (FAILED(::SHParseDisplayName(w.c_str(), nullptr, &pidl, 0, nullptr))) return nullptr;
    return pidl;
}

}  // namespace

bool ForwardContextMenuMessage(UINT message, WPARAM wparam, LPARAM lparam, LRESULT* result) {
    if (!g_contextMenu2 && !g_contextMenu3) return false;

    switch (message) {
        case WM_INITMENUPOPUP:
        case WM_DRAWITEM:
        case WM_MEASUREITEM:
        case WM_MENUCHAR:
            break;
        default:
            return false;
    }

    if (g_contextMenu3) {
        LRESULT handled = 0;
        if (SUCCEEDED(g_contextMenu3->HandleMenuMsg2(message, wparam, lparam, &handled))) {
            *result = handled;
            return true;
        }
    }
    if (g_contextMenu2) {
        if (SUCCEEDED(g_contextMenu2->HandleMenuMsg(message, wparam, lparam))) {
            *result = (message == WM_INITMENUPOPUP) ? 0 : TRUE;
            return true;
        }
    }
    return false;
}

shellhost::Result ShowShellContextMenu(HWND menuOwner, HWND dialogOwner,
                                       const std::vector<std::string>& paths, int screenX,
                                       int screenY, bool extended) {
    if (paths.empty() || !menuOwner) return shellhost::Result::Failed;
    if (!dialogOwner || !::IsWindow(dialogOwner)) dialogOwner = menuOwner;

    if (screenX < 0 || screenY < 0) {
        // Keyboard invocation: anchor on the pointer, which is where Windows
        // itself puts a VK_APPS menu when it has nothing better.
        POINT pt{};
        ::GetCursorPos(&pt);
        screenX = pt.x;
        screenY = pt.y;
    }

    std::vector<PIDLIST_ABSOLUTE> absolute;
    absolute.reserve(paths.size());
    for (const std::string& p : paths) {
        if (PIDLIST_ABSOLUTE pidl = ParsePath(p)) absolute.push_back(pidl);
    }
    if (absolute.empty()) return shellhost::Result::Failed;

    IShellFolder* parent = nullptr;
    PCUITEMID_CHILD firstChild = nullptr;
    if (FAILED(::SHBindToParent(absolute[0], IID_IShellFolder, reinterpret_cast<void**>(&parent),
                                &firstChild))) {
        for (PIDLIST_ABSOLUTE pidl : absolute) ::CoTaskMemFree(pidl);
        return shellhost::Result::Failed;
    }

    // All selected items share a folder, so their last IDs are valid children
    // of the parent we just bound.
    std::vector<PCUITEMID_CHILD> children;
    children.reserve(absolute.size());
    for (PIDLIST_ABSOLUTE pidl : absolute) children.push_back(::ILFindLastID(pidl));

    shellhost::Result result = shellhost::Result::Failed;
    IContextMenu* menu = nullptr;
    // Handlers are instantiated here, not in QueryContextMenu, so this call is
    // already third-party code and needs the guard.
    const HRESULT hr = GuardedGetUIObjectOf(
        parent, dialogOwner, static_cast<UINT>(children.size()),
        reinterpret_cast<PCUITEMID_CHILD_ARRAY>(children.data()), reinterpret_cast<void**>(&menu));

    if (SUCCEEDED(hr) && menu) {
        HMENU hmenu = ::CreatePopupMenu();
        UINT flags = CMF_NORMAL;
        // CMF_EXTENDEDVERBS is what "Show more options" toggles in Explorer.
        // Kite defaults to it so the full menu is one action away.
        if (extended) flags |= CMF_EXTENDEDVERBS;
        // CMF_CANRENAME is deliberately absent: the shell's rename needs an
        // IShellView to edit in, which no host window can supply. Kite renames
        // through its own prompt (Cmd::Rename).

        if (SUCCEEDED(GuardedQueryContextMenu(menu, hmenu, flags))) {
            menu->QueryInterface(IID_IContextMenu2, reinterpret_cast<void**>(&g_contextMenu2));
            menu->QueryInterface(IID_IContextMenu3, reinterpret_cast<void**>(&g_contextMenu3));

            ::SetForegroundWindow(menuOwner);
            const int chosen = GuardedTrackPopupMenu(hmenu, screenX, screenY, menuOwner);

            if (g_contextMenu2) {
                g_contextMenu2->Release();
                g_contextMenu2 = nullptr;
            }
            if (g_contextMenu3) {
                g_contextMenu3->Release();
                g_contextMenu3 = nullptr;
            }

            result = shellhost::Result::None;
            if (chosen >= static_cast<int>(kMenuFirstId)) {
                const UINT offset = static_cast<UINT>(chosen) - kMenuFirstId;
                CMINVOKECOMMANDINFOEX info{};
                info.cbSize = sizeof(info);
                info.fMask = CMIC_MASK_UNICODE | CMIC_MASK_PTINVOKE;
                // The caller's own window, so progress and confirmation dialogs
                // are owned by something visible rather than by our 1x1 helper.
                info.hwnd = dialogOwner;
                info.lpVerb = MAKEINTRESOURCEA(offset);
                info.lpVerbW = MAKEINTRESOURCEW(offset);
                info.nShow = SW_SHOWNORMAL;
                info.ptInvoke = { screenX, screenY };
                if (SUCCEEDED(GuardedInvoke(menu, &info))) result = shellhost::Result::Invoked;
            }
        }
        ::DestroyMenu(hmenu);
        menu->Release();
    }

    parent->Release();
    for (PIDLIST_ABSOLUTE pidl : absolute) ::CoTaskMemFree(pidl);
    return result;
}

}  // namespace kite::win
