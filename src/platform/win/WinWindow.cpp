#include "platform/win/WinWindow.h"

#include <dwmapi.h>
#include <imm.h>
#include <windowsx.h>

#include <algorithm>

#include "platform/win/WinShell.h"
#include "platform/win/WinUtf.h"

namespace kite::win {
namespace {

constexpr wchar_t kClassName[] = L"KiteMainWindow";
constexpr UINT WM_KITE_WAKE = WM_APP + 1;
constexpr UINT WM_KITE_DROP = WM_APP + 2;
constexpr UINT_PTR kStatusTimerId = 1;
constexpr UINT_PTR kDragDropTimerId = 2;

// OleInitialize plus the shell's drag-drop helper costs well over 100 ms, so it
// is pushed past the first frame rather than run inside it. Nobody can start a
// drag this soon after launch.
constexpr UINT kDragDropDelayMs = 200;

uint8_t ButtonsFrom(WPARAM wparam) {
    uint8_t buttons = ui::kButtonNone;
    if (wparam & MK_LBUTTON) buttons |= ui::kButtonLeft;
    if (wparam & MK_RBUTTON) buttons |= ui::kButtonRight;
    if (wparam & MK_MBUTTON) buttons |= ui::kButtonMiddle;
    return buttons;
}

Key TranslateVirtualKey(WPARAM vk) {
    if (vk >= 'A' && vk <= 'Z') {
        return static_cast<Key>(static_cast<int>(Key::A) + static_cast<int>(vk - 'A'));
    }
    if (vk >= '0' && vk <= '9') {
        return static_cast<Key>(static_cast<int>(Key::Num0) + static_cast<int>(vk - '0'));
    }
    if (vk >= VK_F1 && vk <= VK_F12) {
        return static_cast<Key>(static_cast<int>(Key::F1) + static_cast<int>(vk - VK_F1));
    }
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) {
        return static_cast<Key>(static_cast<int>(Key::Num0) + static_cast<int>(vk - VK_NUMPAD0));
    }

    switch (vk) {
        case VK_LEFT: return Key::Left;
        case VK_RIGHT: return Key::Right;
        case VK_UP: return Key::Up;
        case VK_DOWN: return Key::Down;
        case VK_HOME: return Key::Home;
        case VK_END: return Key::End;
        case VK_PRIOR: return Key::PageUp;
        case VK_NEXT: return Key::PageDown;
        case VK_RETURN: return Key::Enter;
        case VK_ESCAPE: return Key::Escape;
        case VK_TAB: return Key::Tab;
        case VK_SPACE: return Key::Space;
        case VK_BACK: return Key::Backspace;
        case VK_DELETE: return Key::Delete;
        case VK_INSERT: return Key::Insert;
        case VK_APPS: return Key::Menu;
        case VK_OEM_MINUS: return Key::Minus;
        case VK_OEM_PLUS: return Key::Equal;
        case VK_OEM_4: return Key::LBracket;
        case VK_OEM_6: return Key::RBracket;
        case VK_OEM_5: return Key::Backslash;
        case VK_OEM_1: return Key::Semicolon;
        case VK_OEM_7: return Key::Quote;
        case VK_OEM_COMMA: return Key::Comma;
        case VK_OEM_PERIOD: return Key::Period;
        case VK_OEM_2: return Key::Slash;
        case VK_OEM_3: return Key::Grave;
        case VK_ADD: return Key::NumpadAdd;
        case VK_SUBTRACT: return Key::NumpadSub;
        case VK_MULTIPLY: return Key::NumpadMul;
        case VK_DIVIDE: return Key::NumpadDiv;
        default: return Key::None;
    }
}

uint8_t CurrentModifiers() {
    uint8_t mods = kModNone;
    if (::GetKeyState(VK_CONTROL) & 0x8000) mods |= kModCtrl;
    if (::GetKeyState(VK_SHIFT) & 0x8000) mods |= kModShift;
    if (::GetKeyState(VK_MENU) & 0x8000) mods |= kModAlt;
    return mods;
}

}  // namespace

WinWindow::WinWindow() = default;
WinWindow::~WinWindow() = default;

void WinWindow::Attach(App* app, ui::AppUi* appUi) {
    app_ = app;
    ui_ = appUi;
}

bool WinWindow::Create(const WindowPlacement& placement) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = &WinWindow::WindowProc;
    wc.hInstance = ::GetModuleHandleW(nullptr);
    wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;  // Direct2D paints every pixel
    wc.lpszClassName = kClassName;
    wc.hIcon = ::LoadIconW(nullptr, IDI_APPLICATION);
    ::RegisterClassExW(&wc);

    const int x = placement.x >= 0 ? placement.x : CW_USEDEFAULT;
    const int y = placement.y >= 0 ? placement.y : CW_USEDEFAULT;

    hwnd_ = ::CreateWindowExW(0, kClassName, L"Kite", WS_OVERLAPPEDWINDOW, x, y, placement.w,
                              placement.h, nullptr, nullptr, wc.hInstance, this);
    if (!hwnd_) return false;

    dpiScale_ = static_cast<float>(::GetDpiForWindow(hwnd_)) / 96.0f;
    ApplyDarkTitleBar();

    if (!renderer_.Initialize(hwnd_, app_->theme(), dpiScale_ * 96.0f)) return false;
    rendererReady_ = true;

    // Shown only once the first frame can be drawn, so there is no white flash.
    ::ShowWindow(hwnd_, placement.maximized ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL);
    ::UpdateWindow(hwnd_);
    return true;
}

int WinWindow::Run() {
    MSG msg{};
    while (::GetMessageW(&msg, nullptr, 0, 0) > 0) {
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

// --- IHost ------------------------------------------------------------------

void WinWindow::Invalidate() {
    if (hwnd_) ::InvalidateRect(hwnd_, nullptr, FALSE);
}

void WinWindow::SetTitle(const std::string& utf8) {
    if (hwnd_) ::SetWindowTextW(hwnd_, ToWide(utf8).c_str());
}

void WinWindow::Close() {
    if (hwnd_) ::PostMessageW(hwnd_, WM_CLOSE, 0, 0);
}

void WinWindow::SetImePosition(float x, float y) { imeCaret_ = { x, y }; }

void WinWindow::SetCursorShape(int shape) {
    cursorShape_ = shape;
    if (hwnd_) ::SetCursor(::LoadCursorW(nullptr, shape == 2   ? IDC_SIZEWE
                                                  : shape == 3 ? IDC_SIZENS
                                                  : shape == 1 ? IDC_HAND
                                                               : IDC_ARROW));
}

void WinWindow::Wake() {
    // Called from DirectoryLoader worker threads.
    if (hwnd_) ::PostMessageW(hwnd_, WM_KITE_WAKE, 0, 0);
}

// --- painting ---------------------------------------------------------------

void WinWindow::Paint() {
    if (!rendererReady_ || !ui_ || !app_) return;

    if (!renderer_.BeginFrame()) return;
    ui_->Paint(renderer_);
    if (!renderer_.EndFrame()) {
        // Device lost: the next WM_PAINT rebuilds and redraws.
        ::InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    const PointF caret = ui_->caretPosition();
    imeCaret_ = caret;

    // Scheduled rather than run here: OLE must stay off the startup path.
    if (!dragDropScheduled_) {
        dragDropScheduled_ = true;
        ::SetTimer(hwnd_, kDragDropTimerId, kDragDropDelayMs, nullptr);
    }

    // Keep repainting while a transient status message is on screen.
    if (!app_->statusMessage().empty() && !app_->statusExpired()) {
        ::SetTimer(hwnd_, kStatusTimerId, 500, nullptr);
    }
}

void WinWindow::EnableDragAndDrop() {
    if (dropTarget_ || !ui_ || !EnsureOle()) return;

    dropTarget_ = new WinDropTarget(
        hwnd_, *ui_, dpiScale_,
        [this](std::vector<std::string> paths, std::string destDir, bool move) {
            pendingDrop_ = { std::move(paths), std::move(destDir), move, true };
            ::PostMessageW(hwnd_, WM_KITE_DROP, 0, 0);
        });

    if (FAILED(::RegisterDragDrop(hwnd_, dropTarget_))) {
        dropTarget_->Release();
        dropTarget_ = nullptr;
    }
}

void WinWindow::RunPendingDrop() {
    if (!pendingDrop_.valid || !app_) return;
    PendingDrop drop = std::move(pendingDrop_);
    pendingDrop_ = PendingDrop{};
    app_->PerformDrop(drop.paths, drop.destDir, drop.move);
}

bool WinWindow::BeginFileDrag(const std::vector<std::string>& paths) {
    if (paths.empty() || dragInProgress_ || !EnsureOle()) return false;

    IDataObject* data = CreateShellDataObject(paths);
    if (!data) return false;

    // DoDragDrop runs its own modal loop; the button is already down, so the
    // capture has to go back or the loop never sees the mouse.
    ::ReleaseCapture();
    dragInProgress_ = true;

    DWORD effect = DROPEFFECT_NONE;
    const HRESULT hr = ::SHDoDragDrop(hwnd_, data, nullptr,
                                      DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK,
                                      &effect);
    dragInProgress_ = false;
    data->Release();

    // A move leaves the source folder stale; the watcher usually reports it,
    // but a folder that could not be watched still needs the nudge.
    if (SUCCEEDED(hr) && effect == DROPEFFECT_MOVE && app_) app_->RefreshFocused();
    return SUCCEEDED(hr) && effect != DROPEFFECT_NONE;
}

void WinWindow::ApplyDarkTitleBar() {
    if (!hwnd_ || !app_) return;
    const BOOL dark = app_->theme().dark ? TRUE : FALSE;
    // DWMWA_USE_IMMERSIVE_DARK_MODE
    ::DwmSetWindowAttribute(hwnd_, 20, &dark, sizeof(dark));
}

void WinWindow::SavePlacement() {
    if (!hwnd_ || !app_) return;
    WINDOWPLACEMENT wp{};
    wp.length = sizeof(wp);
    if (!::GetWindowPlacement(hwnd_, &wp)) return;

    WindowPlacement placement;
    placement.maximized = (wp.showCmd == SW_SHOWMAXIMIZED);
    placement.x = wp.rcNormalPosition.left;
    placement.y = wp.rcNormalPosition.top;
    placement.w = wp.rcNormalPosition.right - wp.rcNormalPosition.left;
    placement.h = wp.rcNormalPosition.bottom - wp.rcNormalPosition.top;
    app_->SetPlacement(placement);
}

// --- input ------------------------------------------------------------------

void WinWindow::DispatchMouse(ui::MouseEvent::Type type, int button, int clicks, WPARAM wparam,
                              LPARAM lparam, bool screenCoords) {
    if (!ui_) return;

    POINT pt{ GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
    if (screenCoords) ::ScreenToClient(hwnd_, &pt);

    ui::MouseEvent e;
    e.type = type;
    e.x = static_cast<float>(pt.x) / dpiScale_;
    e.y = static_cast<float>(pt.y) / dpiScale_;
    e.button = button;
    e.buttons = ButtonsFrom(wparam);
    e.clicks = clicks;
    e.mods = CurrentModifiers();
    if (type == ui::MouseEvent::Type::Wheel) {
        e.wheel = static_cast<float>(GET_WHEEL_DELTA_WPARAM(wparam)) / WHEEL_DELTA;
    }

    POINT screen{ pt.x, pt.y };
    ::ClientToScreen(hwnd_, &screen);
    e.screenX = screen.x;
    e.screenY = screen.y;

    ui_->OnMouse(e);
}

LRESULT CALLBACK WinWindow::WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    WinWindow* self = nullptr;
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        self = static_cast<WinWindow*>(create->lpCreateParams);
        self->hwnd_ = hwnd;
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<WinWindow*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    // Owner-drawn shell extension menus need these relayed before anything else.
    LRESULT shellResult = 0;
    if (ForwardContextMenuMessage(hwnd, message, wparam, lparam, &shellResult)) {
        return shellResult;
    }

    if (!self) return ::DefWindowProcW(hwnd, message, wparam, lparam);
    return self->Handle(message, wparam, lparam);
}

LRESULT WinWindow::Handle(UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
        case WM_ERASEBKGND:
            return 1;  // Direct2D covers the whole client area

        case WM_PAINT: {
            PAINTSTRUCT ps{};
            ::BeginPaint(hwnd_, &ps);
            Paint();
            ::EndPaint(hwnd_, &ps);
            return 0;
        }

        case WM_SIZE:
            if (rendererReady_ && wparam != SIZE_MINIMIZED) {
                renderer_.Resize(LOWORD(lparam), HIWORD(lparam));
                Invalidate();
            }
            return 0;

        case WM_DPICHANGED: {
            dpiScale_ = static_cast<float>(HIWORD(wparam)) / 96.0f;
            if (rendererReady_) renderer_.UpdateTheme(app_->theme(), dpiScale_ * 96.0f);
            const RECT* suggested = reinterpret_cast<RECT*>(lparam);
            ::SetWindowPos(hwnd_, nullptr, suggested->left, suggested->top,
                           suggested->right - suggested->left, suggested->bottom - suggested->top,
                           SWP_NOZORDER | SWP_NOACTIVATE);
            Invalidate();
            return 0;
        }

        case WM_KITE_WAKE:
            if (app_) app_->PumpLoader();
            return 0;

        case WM_KITE_DROP:
            RunPendingDrop();
            return 0;

        case WM_TIMER:
            if (wparam == kStatusTimerId) {
                ::KillTimer(hwnd_, kStatusTimerId);
                Invalidate();
            } else if (wparam == kDragDropTimerId) {
                ::KillTimer(hwnd_, kDragDropTimerId);
                EnableDragAndDrop();
            }
            return 0;

        case WM_SETCURSOR:
            if (LOWORD(lparam) == HTCLIENT && cursorShape_ != 0) {
                SetCursorShape(cursorShape_);
                return TRUE;
            }
            break;

        // --- keyboard ---
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN: {
            if (!app_) break;
            Chord chord;
            chord.key = TranslateVirtualKey(wparam);
            chord.mods = CurrentModifiers();
            if (chord.valid() && app_->OnKey(chord)) {
                // Also stops the Alt-key system beep for consumed chords.
                return 0;
            }
            break;
        }
        case WM_SYSCHAR:
            // Swallowed so Alt+<letter> shortcuts do not beep.
            return 0;

        case WM_CHAR: {
            if (!app_) break;
            const uint32_t unit = static_cast<uint32_t>(wparam);
            uint32_t codepoint = unit;
            if (unit >= 0xD800 && unit <= 0xDBFF) {
                highSurrogate_ = unit;
                return 0;
            }
            if (unit >= 0xDC00 && unit <= 0xDFFF && highSurrogate_ != 0) {
                codepoint = 0x10000 + ((highSurrogate_ - 0xD800) << 10) + (unit - 0xDC00);
                highSurrogate_ = 0;
            }
            if (app_->OnChar(codepoint)) return 0;
            break;
        }

        // --- IME: keep the candidate window next to the text being typed ---
        case WM_IME_STARTCOMPOSITION:
        case WM_IME_COMPOSITION: {
            if (HIMC imc = ::ImmGetContext(hwnd_)) {
                COMPOSITIONFORM form{};
                form.dwStyle = CFS_POINT;
                form.ptCurrentPos.x = static_cast<LONG>(imeCaret_.x * dpiScale_);
                form.ptCurrentPos.y = static_cast<LONG>(imeCaret_.y * dpiScale_);
                ::ImmSetCompositionWindow(imc, &form);
                ::ImmReleaseContext(hwnd_, imc);
            }
            break;
        }

        // --- mouse ---
        case WM_MOUSEMOVE:
            DispatchMouse(ui::MouseEvent::Type::Move, 0, 0, wparam, lparam);
            return 0;
        case WM_LBUTTONDOWN:
            ::SetCapture(hwnd_);
            DispatchMouse(ui::MouseEvent::Type::Down, 0, 1, wparam, lparam);
            return 0;
        case WM_LBUTTONDBLCLK:
            DispatchMouse(ui::MouseEvent::Type::Down, 0, 2, wparam, lparam);
            return 0;
        case WM_LBUTTONUP:
            ::ReleaseCapture();
            DispatchMouse(ui::MouseEvent::Type::Up, 0, 1, wparam, lparam);
            return 0;
        case WM_RBUTTONDOWN:
            DispatchMouse(ui::MouseEvent::Type::Down, 1, 1, wparam, lparam);
            return 0;
        case WM_MBUTTONDOWN:
            DispatchMouse(ui::MouseEvent::Type::Down, 2, 1, wparam, lparam);
            return 0;
        case WM_XBUTTONDOWN:
            DispatchMouse(ui::MouseEvent::Type::Down,
                          GET_XBUTTON_WPARAM(wparam) == XBUTTON1 ? 3 : 4, 1, wparam, lparam);
            return TRUE;
        case WM_MOUSEWHEEL:
            // Wheel coordinates arrive in screen space.
            DispatchMouse(ui::MouseEvent::Type::Wheel, 0, 0, wparam, lparam, true);
            return 0;

        case WM_CLOSE:
            if (!closing_) {
                closing_ = true;
                SavePlacement();
                if (dropTarget_) {
                    ::RevokeDragDrop(hwnd_);
                    dropTarget_->Release();
                    dropTarget_ = nullptr;
                }
                if (app_) app_->Shutdown();
            }
            ::DestroyWindow(hwnd_);
            return 0;

        case WM_DESTROY:
            ::PostQuitMessage(0);
            return 0;

        default:
            break;
    }
    return ::DefWindowProcW(hwnd_, message, wparam, lparam);
}

}  // namespace kite::win
