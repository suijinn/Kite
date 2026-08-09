#pragma once

#include <windows.h>

#include <memory>
#include <string>

#include "core/app/App.h"
#include "core/app/Host.h"
#include "platform/win/D2DRenderer.h"
#include "platform/win/WinDropTarget.h"
#include "ui/AppUi.h"

namespace kite::win {

// The Win32 window: message pump, input translation and the IHost services the
// controller needs. Nothing here knows what a tab or a bookmark is.
class WinWindow final : public IHost {
public:
    WinWindow();
    ~WinWindow() override;

    void Attach(App* app, ui::AppUi* appUi);
    bool Create(const WindowPlacement& placement);
    int Run();

    HWND handle() const { return hwnd_; }

    // IHost
    void Invalidate() override;
    void SetTitle(const std::string& utf8) override;
    void Close() override;
    void SetImePosition(float x, float y) override;
    void SetCursorShape(int shape) override;
    bool BeginFileDrag(const std::vector<std::string>& paths) override;
    void Wake() override;  // called from loader threads

private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
    LRESULT Handle(UINT message, WPARAM wparam, LPARAM lparam);

    void Paint();
    void DispatchMouse(ui::MouseEvent::Type type, int button, int clicks, WPARAM wparam,
                       LPARAM lparam, bool screenCoords = false);
    void ApplyDarkTitleBar();
    void SavePlacement();
    void EnableDragAndDrop();
    void RunPendingDrop();

    HWND hwnd_ = nullptr;
    App* app_ = nullptr;
    ui::AppUi* ui_ = nullptr;
    D2DRenderer renderer_;

    float dpiScale_ = 1.0f;
    bool rendererReady_ = false;
    bool closing_ = false;
    uint32_t highSurrogate_ = 0;
    PointF imeCaret_{ 0.0f, 0.0f };
    int cursorShape_ = 0;

    // Drag & drop. OLE is initialized after the first frame so it never sits on
    // the startup path.
    WinDropTarget* dropTarget_ = nullptr;
    bool dragDropScheduled_ = false;
    bool dragInProgress_ = false;

    struct PendingDrop {
        std::vector<std::string> paths;
        std::string destDir;
        bool move = false;
        bool valid = false;
    } pendingDrop_;
};

}  // namespace kite::win
