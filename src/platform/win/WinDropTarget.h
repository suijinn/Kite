#pragma once

#include <windows.h>

#include <objidl.h>  // IDataObject; not pulled in under WIN32_LEAN_AND_MEAN
#include <oleidl.h>
#include <shlobj.h>

#include <functional>
#include <string>
#include <vector>

#include "ui/AppUi.h"

namespace kite::win {

// IDropTarget for the main window.
//
// The drop itself is not performed here: Drop() hands the work back to the
// window through `onDrop`, which queues it and returns immediately. Running a
// file operation inside Drop() would block the dragging application - Explorer
// included - for the whole copy.
class WinDropTarget final : public IDropTarget {
public:
    using DropHandler =
        std::function<void(std::vector<std::string> paths, std::string destDir, bool move)>;

    WinDropTarget(HWND hwnd, ui::AppUi& appUi, const float& dpiScale, DropHandler onDrop);

    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;

    // IDropTarget
    HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* data, DWORD keyState, POINTL point,
                                        DWORD* effect) override;
    HRESULT STDMETHODCALLTYPE DragOver(DWORD keyState, POINTL point, DWORD* effect) override;
    HRESULT STDMETHODCALLTYPE DragLeave() override;
    HRESULT STDMETHODCALLTYPE Drop(IDataObject* data, DWORD keyState, POINTL point,
                                   DWORD* effect) override;

private:
    PointF ToClient(POINTL screen) const;
    DWORD EffectFor(DWORD keyState, const std::string& destDir) const;

    LONG refCount_ = 1;
    HWND hwnd_ = nullptr;
    ui::AppUi& ui_;
    const float& dpiScale_;
    DropHandler onDrop_;

    IDropTargetHelper* helper_ = nullptr;
    std::vector<std::string> dragPaths_;
    std::string destDir_;
};

}  // namespace kite::win
