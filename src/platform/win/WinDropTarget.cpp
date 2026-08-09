#include "platform/win/WinDropTarget.h"

#include "core/base/PathUtil.h"
#include "core/base/Utf8.h"
#include "platform/win/WinShell.h"

namespace kite::win {
namespace {

// Explorer's rule: within one volume a plain drag moves, across volumes it
// copies. Matching it avoids nasty surprises.
bool SameVolume(const std::string& a, const std::string& b) {
    if (a.size() >= 2 && b.size() >= 2 && a[1] == ':' && b[1] == ':') {
        return utf8::EqualsIgnoreCaseAscii(a.substr(0, 2), b.substr(0, 2));
    }
    // UNC or anything unusual: treat as different volumes, i.e. copy.
    return false;
}

}  // namespace

WinDropTarget::WinDropTarget(HWND hwnd, ui::AppUi& appUi, const float& dpiScale,
                             DropHandler onDrop)
    : hwnd_(hwnd), ui_(appUi), dpiScale_(dpiScale), onDrop_(std::move(onDrop)) {
    // Optional: gives us Explorer's translucent drag image and "+ Copy" badge.
    ::CoCreateInstance(CLSID_DragDropHelper, nullptr, CLSCTX_INPROC_SERVER,
                       IID_IDropTargetHelper, reinterpret_cast<void**>(&helper_));
}

HRESULT STDMETHODCALLTYPE WinDropTarget::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_IDropTarget) {
        *ppv = static_cast<IDropTarget*>(this);
        AddRef();
        return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE WinDropTarget::AddRef() {
    return static_cast<ULONG>(::InterlockedIncrement(&refCount_));
}

ULONG STDMETHODCALLTYPE WinDropTarget::Release() {
    const LONG remaining = ::InterlockedDecrement(&refCount_);
    if (remaining == 0) {
        if (helper_) helper_->Release();
        delete this;
    }
    return static_cast<ULONG>(remaining);
}

PointF WinDropTarget::ToClient(POINTL screen) const {
    POINT pt{ screen.x, screen.y };
    ::ScreenToClient(hwnd_, &pt);
    const float scale = dpiScale_ > 0.0f ? dpiScale_ : 1.0f;
    return { static_cast<float>(pt.x) / scale, static_cast<float>(pt.y) / scale };
}

DWORD WinDropTarget::EffectFor(DWORD keyState, const std::string& destDir) const {
    if (dragPaths_.empty() || destDir.empty()) return DROPEFFECT_NONE;
    if (!App::IsValidDropTarget(dragPaths_, destDir)) return DROPEFFECT_NONE;

    if (keyState & MK_CONTROL) return DROPEFFECT_COPY;
    if (keyState & MK_SHIFT) return DROPEFFECT_MOVE;
    return SameVolume(dragPaths_.front(), destDir) ? DROPEFFECT_MOVE : DROPEFFECT_COPY;
}

HRESULT STDMETHODCALLTYPE WinDropTarget::DragEnter(IDataObject* data, DWORD keyState, POINTL point,
                                                   DWORD* effect) {
    dragPaths_ = ExtractDroppedPaths(data);

    const PointF client = ToClient(point);
    destDir_ = ui_.DropTargetAt(client.x, client.y);
    ui_.SetDropFeedback(client.x, client.y);

    if (effect) *effect = EffectFor(keyState, destDir_);
    if (helper_) {
        POINT pt{ point.x, point.y };
        helper_->DragEnter(hwnd_, data, &pt, effect ? *effect : DROPEFFECT_NONE);
    }
    ::InvalidateRect(hwnd_, nullptr, FALSE);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE WinDropTarget::DragOver(DWORD keyState, POINTL point, DWORD* effect) {
    const PointF client = ToClient(point);
    const std::string destination = ui_.DropTargetAt(client.x, client.y);

    if (destination != destDir_) {
        destDir_ = destination;
        ui_.SetDropFeedback(client.x, client.y);
        ::InvalidateRect(hwnd_, nullptr, FALSE);
    }
    if (effect) *effect = EffectFor(keyState, destDir_);
    if (helper_) {
        POINT pt{ point.x, point.y };
        helper_->DragOver(&pt, effect ? *effect : DROPEFFECT_NONE);
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE WinDropTarget::DragLeave() {
    dragPaths_.clear();
    destDir_.clear();
    ui_.ClearDropFeedback();
    if (helper_) helper_->DragLeave();
    ::InvalidateRect(hwnd_, nullptr, FALSE);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE WinDropTarget::Drop(IDataObject* data, DWORD keyState, POINTL point,
                                              DWORD* effect) {
    const PointF client = ToClient(point);
    dragPaths_ = ExtractDroppedPaths(data);
    destDir_ = ui_.DropTargetAt(client.x, client.y);

    const DWORD resolved = EffectFor(keyState, destDir_);
    if (helper_) {
        POINT pt{ point.x, point.y };
        helper_->Drop(data, &pt, resolved);
    }
    ui_.ClearDropFeedback();

    if (resolved != DROPEFFECT_NONE && onDrop_) {
        onDrop_(dragPaths_, destDir_, resolved == DROPEFFECT_MOVE);
    }
    if (effect) *effect = resolved;

    dragPaths_.clear();
    destDir_.clear();
    ::InvalidateRect(hwnd_, nullptr, FALSE);
    return S_OK;
}

}  // namespace kite::win
