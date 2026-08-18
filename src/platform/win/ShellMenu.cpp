#include "platform/win/ShellMenu.h"

#include <shellapi.h>  // CMIC_MASK_UNICODE resolves to SEE_MASK_UNICODE, declared here
#include <shlobj.h>

#include "platform/win/ShellFolder.h"
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

HRESULT GuardedCreateViewObject(IShellFolder* folder, HWND owner, void** out) {
    __try {
        return folder->CreateViewObject(owner, IID_IContextMenu, out);
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

// --- dark mode --------------------------------------------------------------
// Menus are drawn by user32 against uxtheme's app-wide colour policy, and there
// is no documented way to ask for the dark one. These four exports are what
// Explorer itself uses; they are ordinal-only, so each is resolved by number.
//
// The ordinals were reshuffled in 1903 (18362): 135 was AllowDarkModeForApp
// before it and SetPreferredAppMode after. Calling the wrong one passes an enum
// where a bool is expected, so anything older is left alone entirely - a light
// menu is a far better outcome than a wrong call into uxtheme.

enum class PreferredAppMode : int {
    Default = 0,
    AllowDark = 1,
    ForceDark = 2,
    ForceLight = 3,
};

using SetPreferredAppModeFn = PreferredAppMode(WINAPI*)(PreferredAppMode);
using AllowDarkModeForWindowFn = BOOL(WINAPI*)(HWND, BOOL);
using FlushMenuThemesFn = void(WINAPI*)();
using RefreshImmersiveColorPolicyStateFn = void(WINAPI*)();

struct DarkModeApi {
    SetPreferredAppModeFn setPreferredAppMode = nullptr;
    AllowDarkModeForWindowFn allowDarkModeForWindow = nullptr;
    FlushMenuThemesFn flushMenuThemes = nullptr;
    RefreshImmersiveColorPolicyStateFn refreshColorPolicy = nullptr;
    bool usable = false;
};

/// Reports the real build number.
///
/// GetVersionEx and friends lie to processes without the matching manifest
/// entry; this one does not, which is what makes the 1903 cut-off dependable.
DWORD WindowsBuild() {
    using RtlGetNtVersionNumbersFn = void(WINAPI*)(DWORD*, DWORD*, DWORD*);
    HMODULE ntdll = ::GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) return 0;
    auto get = reinterpret_cast<RtlGetNtVersionNumbersFn>(
        reinterpret_cast<void*>(::GetProcAddress(ntdll, "RtlGetNtVersionNumbers")));
    if (!get) return 0;
    DWORD major = 0;
    DWORD minor = 0;
    DWORD build = 0;
    get(&major, &minor, &build);
    // The top bits are a checked/free marker, not part of the number.
    build &= 0x0FFFFFFF;
    return (major > 10 || (major == 10 && minor >= 0)) ? build : 0;
}

const DarkModeApi& DarkMode() {
    static const DarkModeApi api = [] {
        DarkModeApi loaded;
        if (WindowsBuild() < 18362) return loaded;
        HMODULE uxtheme = ::LoadLibraryExW(L"uxtheme.dll", nullptr,
                                           LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!uxtheme) return loaded;

        auto proc = [uxtheme](int ordinal) {
            return reinterpret_cast<void*>(::GetProcAddress(uxtheme, MAKEINTRESOURCEA(ordinal)));
        };
        loaded.refreshColorPolicy =
            reinterpret_cast<RefreshImmersiveColorPolicyStateFn>(proc(104));
        loaded.allowDarkModeForWindow = reinterpret_cast<AllowDarkModeForWindowFn>(proc(133));
        loaded.setPreferredAppMode = reinterpret_cast<SetPreferredAppModeFn>(proc(135));
        loaded.flushMenuThemes = reinterpret_cast<FlushMenuThemesFn>(proc(136));
        loaded.usable = loaded.setPreferredAppMode && loaded.flushMenuThemes;
        // Deliberately not freeing uxtheme.dll: the pointers stay live for the
        // life of the process, and it is already loaded by user32 anyway.
        return loaded;
    }();
    return api;
}

PIDLIST_ABSOLUTE ParsePath(const std::string& path) {
    PIDLIST_ABSOLUTE pidl = nullptr;
    const std::wstring w = ToWide(path);
    if (FAILED(::SHParseDisplayName(w.c_str(), nullptr, &pidl, 0, nullptr))) return nullptr;
    return pidl;
}

/// Turns the request's paths into absolute PIDLs.
///
/// With no container, each path is parsed on its own - which is right for every
/// real file and folder. With one, the items are looked up inside it instead;
/// see Request::container for why the Recycle Bin cannot use the first way.
std::vector<PIDLIST_ABSOLUTE> ResolveTargets(const std::string& container,
                                             const std::vector<std::string>& paths) {
    if (!container.empty()) return ResolveItemsInFolder(container, paths);

    std::vector<PIDLIST_ABSOLUTE> absolute;
    absolute.reserve(paths.size());
    for (const std::string& p : paths) {
        if (PIDLIST_ABSOLUTE pidl = ParsePath(p)) absolute.push_back(pidl);
    }
    return absolute;
}

/// Binds the IContextMenu for a set of items that share a parent.
///
/// The parent is handed back too, because it owns the menu: releasing it while
/// the menu is still up is how a shell extension gets torn out from under its
/// own popup.
IContextMenu* ItemContextMenu(const std::vector<PIDLIST_ABSOLUTE>& absolute, HWND dialogOwner,
                              IShellFolder** parentOut) {
    PCUITEMID_CHILD firstChild = nullptr;
    IShellFolder* parent = nullptr;
    if (FAILED(::SHBindToParent(absolute[0], IID_IShellFolder,
                                reinterpret_cast<void**>(&parent), &firstChild)) ||
        !parent) {
        return nullptr;
    }

    // All selected items share a folder, so their last IDs are valid children
    // of the parent we just bound.
    std::vector<PCUITEMID_CHILD> children;
    children.reserve(absolute.size());
    for (PIDLIST_ABSOLUTE pidl : absolute) children.push_back(::ILFindLastID(pidl));

    // Handlers are instantiated here, not in QueryContextMenu, so this call is
    // already third-party code and needs the guard.
    IContextMenu* menu = nullptr;
    const HRESULT hr =
        GuardedGetUIObjectOf(parent, dialogOwner, static_cast<UINT>(children.size()),
                             reinterpret_cast<PCUITEMID_CHILD_ARRAY>(children.data()),
                             reinterpret_cast<void**>(&menu));
    if (FAILED(hr) || !menu) {
        parent->Release();
        return nullptr;
    }
    *parentOut = parent;
    return menu;
}

}  // namespace

bool SetShellMenuDarkMode(HWND menuOwner, bool dark) {
    const DarkModeApi& api = DarkMode();
    if (!api.usable) return false;

    // ForceDark / ForceLight rather than AllowDark: the menu follows Kite's own
    // theme, which the user may have set against the system's.
    static int applied = -1;
    const int wanted = dark ? 1 : 0;
    if (applied != wanted) {
        applied = wanted;
        api.setPreferredAppMode(dark ? PreferredAppMode::ForceDark : PreferredAppMode::ForceLight);
        if (api.refreshColorPolicy) api.refreshColorPolicy();
        // Without this the change only reaches menus created after the next
        // theme broadcast, which for a popup menu means "never".
        api.flushMenuThemes();
    }
    if (api.allowDarkModeForWindow && menuOwner) {
        api.allowDarkModeForWindow(menuOwner, dark ? TRUE : FALSE);
    }
    return true;
}

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
                                       const std::string& container,
                                       const std::vector<std::string>& paths, int screenX,
                                       int screenY, bool extended, bool background) {
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

    std::vector<PIDLIST_ABSOLUTE> absolute = ResolveTargets(container, paths);
    if (absolute.empty()) return shellhost::Result::Failed;

    // Two different shell objects answer to "the menu for this folder", and the
    // choice is what the user ends up reading:
    //
    //  - the item menu (GetUIObjectOf on the parent) is what right-clicking the
    //    folder in its parent's listing gives. Its verbs act *on* the folder, so
    //    handlers offer things like TortoiseGit's "Git clone" - a folder as a
    //    destination to clone into, which is wrong for the folder being viewed.
    //  - the background menu (CreateViewObject on the folder itself) is what
    //    Explorer shows on empty space inside the folder: New, Paste, and the
    //    verbs that act *inside* it.
    shellhost::Result result = shellhost::Result::Failed;
    IShellFolder* parent = nullptr;
    IContextMenu* menu = nullptr;
    HRESULT hr = E_FAIL;

    if (background) {
        IShellFolder* folder = nullptr;
        hr = ::SHBindToObject(nullptr, absolute[0], nullptr, IID_IShellFolder,
                              reinterpret_cast<void**>(&folder));
        if (SUCCEEDED(hr) && folder) {
            hr = GuardedCreateViewObject(folder, dialogOwner, reinterpret_cast<void**>(&menu));
            folder->Release();
        }
    } else {
        menu = ItemContextMenu(absolute, dialogOwner, &parent);
        hr = menu ? S_OK : E_FAIL;
    }

    if (SUCCEEDED(hr) && menu) {
        HMENU hmenu = ::CreatePopupMenu();
        UINT flags = CMF_NORMAL;
        // CMF_EXTENDEDVERBS is the set Explorer reveals when Shift is held, and
        // handlers stock it on that understanding - TortoiseGit files
        // "Git Clone..." there, which reads as an offer to clone into the folder
        // you are standing in. Kite once passed this by default and that is
        // exactly what people saw, so it now follows Shift, like the shell does.
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
                // A background verb has no item to work from, only the folder it
                // was raised in: "New > Text Document" creates its file in the
                // working directory, and several third-party verbs read it too.
                const std::string directory = background ? paths[0] : std::string();
                const std::wstring directoryW = ToWide(directory);
                CMINVOKECOMMANDINFOEX info{};
                info.cbSize = sizeof(info);
                info.fMask = CMIC_MASK_UNICODE | CMIC_MASK_PTINVOKE;
                if (background) {
                    // CMIC_MASK_UNICODE means the shell reads the wide member;
                    // the narrow one is left as UTF-8 for handlers that ignore
                    // the flag, where it is exact for every ASCII path.
                    info.lpDirectory = directory.c_str();
                    info.lpDirectoryW = directoryW.c_str();
                }
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

    if (parent) parent->Release();
    for (PIDLIST_ABSOLUTE pidl : absolute) ::CoTaskMemFree(pidl);
    return result;
}

shellhost::VerbResponse InvokeShellVerb(HWND dialogOwner, const std::string& container,
                                        const std::vector<std::string>& paths,
                                        const std::string& verb, bool byOriginalPath) {
    shellhost::VerbResponse response;
    if (paths.empty() || verb.empty()) return response;

    std::vector<PIDLIST_ABSOLUTE> absolute =
        byOriginalPath ? ResolveTrashItemsByOrigin(container, paths)
                       : ResolveTargets(container, paths);
    if (absolute.empty()) return response;

    IShellFolder* parent = nullptr;
    IContextMenu* menu = ItemContextMenu(absolute, dialogOwner, &parent);
    if (menu) {
        // The verb has to be queried for before it can be invoked: the handler
        // populates its verb table in QueryContextMenu, and a menu that was
        // never asked to build itself answers to nothing. The HMENU is thrown
        // away - nothing is shown - but the call is what makes "undelete" real.
        HMENU hmenu = ::CreatePopupMenu();
        if (SUCCEEDED(GuardedQueryContextMenu(menu, hmenu, CMF_NORMAL))) {
            CMINVOKECOMMANDINFOEX info{};
            info.cbSize = sizeof(info);
            info.fMask = CMIC_MASK_UNICODE;
            // Kite's own window, so the shell's confirmation and progress
            // dialogs are owned by something the user can see.
            info.hwnd = dialogOwner;
            const std::wstring wide = ToWide(verb);
            info.lpVerb = verb.c_str();
            info.lpVerbW = wide.c_str();
            info.nShow = SW_SHOWNORMAL;
            if (SUCCEEDED(GuardedInvoke(menu, &info))) {
                response.ok = true;
                response.applied = static_cast<uint32_t>(absolute.size());
            }
        }
        ::DestroyMenu(hmenu);
        menu->Release();
    }

    if (parent) parent->Release();
    for (PIDLIST_ABSOLUTE pidl : absolute) ::CoTaskMemFree(pidl);
    return response;
}

}  // namespace kite::win
