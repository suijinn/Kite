#include "platform/win/ShellIcons.h"

#include <shellapi.h>
#include <shlobj.h>

#include "platform/win/WinUtf.h"

namespace kite::win {
namespace {

/// SEH guard around the call that instantiates other people's code.
///
/// Takes raw pointers only, so it holds nothing needing C++ unwinding and may
/// legally use __try / __except. Same arrangement as ShellMenu.cpp.
DWORD_PTR GuardedGetFileInfo(const wchar_t* path, SHFILEINFOW* info, UINT flags) {
    __try {
        return ::SHGetFileInfoW(path, 0, info, sizeof(*info), flags);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

DWORD_PTR GuardedGetPidlInfo(const void* pidl, SHFILEINFOW* info, UINT flags) {
    __try {
        return ::SHGetFileInfoW(reinterpret_cast<LPCWSTR>(pidl), 0, info, sizeof(*info),
                                flags | SHGFI_PIDL);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

// True for the names the shell parses but the filesystem does not: "::{CLSID}"
// and the "shell:Name" monikers. SHGetFileInfo takes a path, so these have to
// go through a PIDL instead - without this, "PC" and the Recycle Bin draw with
// Kite's fallback glyph while every row inside them gets a real icon.
bool IsShellName(const std::string& path) {
    if (path.size() >= 2 && path[0] == ':' && path[1] == ':') return true;
    return path.compare(0, 6, "shell:") == 0;
}

// Fills `info` for a name only the shell can resolve. Returns false if it will
// not resolve, which is the same answer a missing file gives.
bool ShellNameInfo(const std::wstring& wide, SHFILEINFOW* info, UINT flags) {
    PIDLIST_ABSOLUTE pidl = nullptr;
    if (FAILED(::SHParseDisplayName(wide.c_str(), nullptr, &pidl, 0, nullptr)) || !pidl) {
        return false;
    }
    const DWORD_PTR ok = GuardedGetPidlInfo(pidl, info, flags);
    ::CoTaskMemFree(pidl);
    return ok != 0;
}

/// A 32-bit top-down DIB to draw an icon into, released on scope exit.
class IconSurface {
public:
    /// Creates a `size` x `size` surface. Check bits() before using it.
    IconSurface(HDC dc, uint32_t size) {
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(info.bmiHeader);
        info.bmiHeader.biWidth = static_cast<LONG>(size);
        // Negative height: rows run top-down, matching every consumer downstream.
        info.bmiHeader.biHeight = -static_cast<LONG>(size);
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;

        dib_ = ::CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits_, nullptr, 0);
        if (!dib_) bits_ = nullptr;
        if (bits_) ::memset(bits_, 0, static_cast<size_t>(size) * size * 4);
    }

    ~IconSurface() {
        if (dib_) ::DeleteObject(dib_);
    }

    IconSurface(const IconSurface&) = delete;
    IconSurface& operator=(const IconSurface&) = delete;

    /// Draws `icon` into the surface with the given DrawIconEx flag.
    bool Draw(HDC dc, HICON icon, uint32_t size, UINT flag) {
        if (!bits_) return false;
        HGDIOBJ previous = ::SelectObject(dc, dib_);
        const BOOL ok = ::DrawIconEx(dc, 0, 0, icon, static_cast<int>(size),
                                     static_cast<int>(size), 0, nullptr, flag);
        ::SelectObject(dc, previous);
        return ok != FALSE;
    }

    const uint8_t* bits() const { return static_cast<const uint8_t*>(bits_); }

private:
    HBITMAP dib_ = nullptr;
    void* bits_ = nullptr;
};

/// Copies an HICON into premultiplied BGRA.
///
/// DrawIconEx blends a 32-bit icon with AlphaBlend, which expects premultiplied
/// source pixels and writes `src + dst * (1 - a)`. Over a zero-filled (fully
/// transparent) target that is exactly the premultiplied source - which is what
/// Direct2D wants - so no conversion pass is needed afterwards.
bool CopyIconPixels(HICON icon, uint32_t size, IconBitmap& out) {
    HDC screen = ::GetDC(nullptr);
    if (!screen) return false;
    HDC dc = ::CreateCompatibleDC(screen);
    ::ReleaseDC(nullptr, screen);
    if (!dc) return false;

    const size_t bytes = static_cast<size_t>(size) * size * 4;
    bool ok = false;
    {
        IconSurface colour(dc, size);
        if (colour.bits() && colour.Draw(dc, icon, size, DI_NORMAL)) {
            out.width = size;
            out.height = size;
            out.bgra.assign(colour.bits(), colour.bits() + bytes);

            bool anyAlpha = false;
            for (size_t i = 3; i < bytes; i += 4) {
                if (out.bgra[i] != 0) {
                    anyAlpha = true;
                    break;
                }
            }
            // A legacy icon with no alpha channel comes out fully transparent,
            // which would draw as nothing at all. Its shape lives in the AND
            // mask instead: black where the icon is opaque.
            if (!anyAlpha) {
                IconSurface mask(dc, size);
                if (mask.bits() && mask.Draw(dc, icon, size, DI_MASK)) {
                    const uint8_t* maskBits = mask.bits();
                    for (size_t i = 0; i < bytes; i += 4) {
                        out.bgra[i + 3] = maskBits[i] ? 0 : 255;
                    }
                } else {
                    // No mask either: treat it as fully opaque rather than
                    // handing back an invisible icon.
                    for (size_t i = 3; i < bytes; i += 4) out.bgra[i] = 255;
                }
            }
            ok = true;
        }
    }

    ::DeleteDC(dc);
    return ok;
}

}  // namespace

bool LoadShellIcon(const std::string& path, uint32_t pixelSize, IconBitmap& out) {
    if (path.empty()) return false;
    const std::wstring wide = ToWide(path);

    // Two sizes exist to be asked for; anything else is a scale of one of them.
    // Taking the larger source for a mid-size cell keeps the result sharp, which
    // matters because the list is drawn at whatever the row height works out to.
    //
    // Not named `small`: rpcndr.h, which arrives with shlobj.h, defines that as
    // a macro for `char`.
    const bool wantSmall = pixelSize <= 16;
    UINT flags = SHGFI_ICON | SHGFI_ADDOVERLAYS | (wantSmall ? SHGFI_SMALLICON : SHGFI_LARGEICON);

    const bool shellName = IsShellName(path);
    auto ask = [&](SHFILEINFOW* info, UINT with) {
        if (shellName) return ShellNameInfo(wide, info, with);
        if (GuardedGetFileInfo(wide.c_str(), info, with) != 0) return true;
        // Not a path the filesystem knows - but it may still be a name the
        // shell parses. Items inside an archive spell themselves exactly like
        // paths ("C:\a.zip\notes.txt") while no file sits at that spelling, so
        // without this every row inside a zip draws the fallback glyph. Only
        // paid when the cheap question has already failed.
        return ShellNameInfo(wide, info, with);
    };

    SHFILEINFOW info{};
    if (!ask(&info, flags)) {
        // A handler that faulted, or a path the shell will not bind to. Retry
        // once without overlays: a plain icon beats an empty cell, and this is
        // also the path a badly behaved overlay handler ends up on.
        info = SHFILEINFOW{};
        flags &= ~static_cast<UINT>(SHGFI_ADDOVERLAYS);
        if (!ask(&info, flags)) return false;
    }
    if (!info.hIcon) return false;

    const uint32_t size = wantSmall ? 16u : 32u;
    const bool ok = CopyIconPixels(info.hIcon, size, out);
    ::DestroyIcon(info.hIcon);
    return ok;
}

}  // namespace kite::win
