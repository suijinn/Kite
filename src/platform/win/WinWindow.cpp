#include "platform/win/WinWindow.h"

#include <dwmapi.h>
#include <imm.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>

#include "platform/win/Resources.h"
#include "platform/win/WinPaths.h"
#include "platform/win/WinShell.h"
#include "platform/win/WinSingleInstance.h"
#include "platform/win/WinUtf.h"

namespace kite::win {
namespace {

constexpr UINT WM_KITE_WAKE = WM_APP + 1;
constexpr UINT WM_KITE_DROP = WM_APP + 2;
constexpr UINT_PTR kStatusTimerId = 1;
constexpr UINT_PTR kDragDropTimerId = 2;

// OleInitialize plus the shell's drag-drop helper costs well over 100 ms, so it
// is pushed past the first frame rather than run inside it. Nobody can start a
// drag this soon after launch.
constexpr UINT kDragDropDelayMs = 200;

// ImmGetContext と ImmReleaseContext の対。IME を触る関数はどれも «無ければ帰る»
// 経路をいくつも持つので、解放は構造で保証する。
struct ImeContext {
    HWND hwnd = nullptr;
    HIMC imc = nullptr;

    ~ImeContext() {
        if (imc) ::ImmReleaseContext(hwnd, imc);
    }
};

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
    HINSTANCE instance = ::GetModuleHandleW(nullptr);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = &WinWindow::WindowProc;
    wc.hInstance = instance;
    wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;  // Direct2D paints every pixel
    // Derived from the exe's own path, so a second copy unpacked elsewhere is a
    // separate instance rather than a stranger's window to hand paths to. A
    // standalone window answers to a different name again: it is not the one a
    // forwarded launch should land in, because it saves nothing.
    const wchar_t* className = InstanceClassName(!app_ || !app_->standalone());
    wc.lpszClassName = className;
    // From our own resources, not the shell default. LoadIconW picks the large
    // size for the class icon and hIconSmall the 16 px one; leaving hIconSmall
    // unset makes Windows shrink the 256 px image for the caption and the
    // taskbar, which is exactly where the .ico's small entries were drawn for.
    wc.hIcon = ::LoadIconW(instance, MAKEINTRESOURCEW(IDI_APPICON));
    wc.hIconSm = static_cast<HICON>(::LoadImageW(instance, MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON,
                                                 ::GetSystemMetrics(SM_CXSMICON),
                                                 ::GetSystemMetrics(SM_CYSMICON), 0));
    ::RegisterClassExW(&wc);

    const int x = placement.x >= 0 ? placement.x : CW_USEDEFAULT;
    const int y = placement.y >= 0 ? placement.y : CW_USEDEFAULT;

    // App::Init settles the caption before there is a window to carry it, so the
    // stored one goes on at creation. Setting it afterwards would show the bare
    // fallback for a frame first.
    hwnd_ = ::CreateWindowExW(0, className, title_.empty() ? L"Kite" : title_.c_str(),
                              WS_OVERLAPPEDWINDOW, x, y, placement.w, placement.h, nullptr,
                              nullptr, wc.hInstance, this);
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
    title_ = ToWide(utf8);
    if (hwnd_) ::SetWindowTextW(hwnd_, title_.c_str());
}

void WinWindow::Close() {
    if (hwnd_) ::PostMessageW(hwnd_, WM_CLOSE, 0, 0);
}

bool WinWindow::OpenNewWindow(const std::string& dir) {
    const std::wstring exe = ModuleFilePath();
    if (exe.empty()) return false;

    // The flag is not decoration: it says "a second window was asked for", which
    // is what tells the new process to start on the folder it was given instead
    // of restoring the saved sessions - and, since P1-3, what keeps it from
    // handing that folder to this window as a tab and exiting.
    std::wstring commandLine = L"\"" + exe + L"\" " + ToWide(kNewWindowFlag);
    if (!dir.empty()) commandLine += L" " + QuoteArgument(ToWide(dir));

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    // CreateProcessW writes into the command line, so it cannot be the literal
    // buffer of a const wstring.
    std::vector<wchar_t> mutableLine(commandLine.begin(), commandLine.end());
    mutableLine.push_back(L'\0');
    if (!::CreateProcessW(exe.c_str(), mutableLine.data(), nullptr, nullptr, FALSE, 0, nullptr,
                          nullptr, &startup, &process)) {
        return false;
    }
    // Nothing here waits on the new window: it owns itself from now on.
    ::CloseHandle(process.hThread);
    ::CloseHandle(process.hProcess);
    return true;
}

// --- IME --------------------------------------------------------------------
//
// 未確定の文字列を描くのは Kite 自身で、IME には描かせない（App の Composition）。
// IME の変換窓は自前のフォントと行送りで文字を置くので、入力欄の中の文字とは数
// ピクセルずれた位置に出て、確定した瞬間に跳ぶ ─ 利用者から「確定していない状態だと
// 文字の位置がずれる」と報告された形がこれ。
//
// **引き取るのは常に。画面によって IME に描かせたり描かせなかったりしない。**
// 変換窓を出すかどうかを言えるのは WM_IME_SETCONTEXT の ISC_SHOWUICOMPOSITIONWINDOW
// だけで、そのメッセージが来るのはフォーカスが移るときだけ ─ «入力欄が開いている間は
// 消す» という切り替え方ができない。半端に分けると、欄を開いてから打った文字が
// こちらと IME の窓の 2 か所に出る。
//
// だから入力欄が無い画面（一覧の上での型入力ジャンプ）にも描く先が要る。そちらは
// ステータス行が «変換中» として出す（AppUi::PaintStatusBar）。

// キャレットの矩形を IME に教える。位置を決めるのは毎フレームの描画なので、変換の
// 打鍵ごとに読み直す。
void WinWindow::UpdateImeWindow() {
    if (!hwnd_) return;
    HIMC imc = ::ImmGetContext(hwnd_);
    if (!imc) return;
    const ImeContext context{ hwnd_, imc };

    const POINT caret{ std::lround(imeCaret_.l * dpiScale_),
                       std::lround(imeCaret_.t * dpiScale_) };
    const LONG bottom = std::lround(imeCaret_.b * dpiScale_);

    COMPOSITIONFORM form{};
    form.dwStyle = CFS_POINT;
    form.ptCurrentPos = caret;
    ::ImmSetCompositionWindow(imc, &form);

    // 候補一覧には «この矩形を避けろ» と言う。変換している当の文字の上に一覧が乗ると、
    // 選んでいる相手が見えない ─ 変換窓を消した以上、隠れて困るのはこちらの文字。
    CANDIDATEFORM candidate{};
    candidate.dwIndex = 0;
    candidate.dwStyle = CFS_EXCLUDE;
    candidate.ptCurrentPos = caret;
    candidate.rcArea = { caret.x, caret.y, caret.x + 1, bottom };
    ::ImmSetCandidateWindow(imc, &candidate);

    // 変換窓を消せない IME のための保険。ISC_SHOWUICOMPOSITIONWINDOW を落としても
    // 自前の窓を出すものがあり、そのときフォントが既定のままだと «ずれる» が戻る。
    if (app_) {
        LOGFONTW font{};
        font.lfHeight = -std::lround(app_->theme().fontSize * dpiScale_);
        font.lfCharSet = DEFAULT_CHARSET;
        const std::wstring family = ToWide(app_->theme().fontFamily);
        // LF_FACESIZE には終端を含めて収める。溢れる名前は既定のフォントに任せる。
        if (family.size() < LF_FACESIZE) {
            std::copy(family.begin(), family.end(), font.lfFaceName);
            ::ImmSetCompositionFontW(imc, &font);
        }
    }
}

// 変換中の文字列と、キャレット・注目節の位置を読み取って App へ渡す。
//
// IME が数えるのは UTF-16 の符号単位なので、渡す前に «その手前までを UTF-8 にした
// 長さ» へ直す。core は全面 UTF-8 で、変換の途中経過だけ別の数え方にする理由が無い。
void WinWindow::ReadComposition() {
    HIMC imc = ::ImmGetContext(hwnd_);
    if (!imc) return;
    const ImeContext context{ hwnd_, imc };
    const LONG bytes = ::ImmGetCompositionStringW(imc, GCS_COMPSTR, nullptr, 0);
    if (bytes <= 0) {
        if (app_) app_->EndComposition();
        return;
    }
    std::wstring wide(static_cast<size_t>(bytes) / sizeof(wchar_t), L'\0');
    ::ImmGetCompositionStringW(imc, GCS_COMPSTR, wide.data(), static_cast<DWORD>(bytes));

    auto utf8Length = [&wide](size_t units) {
        return ToUtf8(std::wstring_view(wide).substr(0, std::min(units, wide.size()))).size();
    };

    const LONG cursor = ::ImmGetCompositionStringW(imc, GCS_CURSORPOS, nullptr, 0);
    const size_t caret = utf8Length(cursor > 0 ? static_cast<size_t>(cursor) : 0);

    // 注目節 ─ 変換対象になっている 1 節。属性は 1 符号単位に 1 バイトで返る。
    size_t targetBegin = 0;
    size_t targetEnd = 0;
    const LONG attrBytes = ::ImmGetCompositionStringW(imc, GCS_COMPATTR, nullptr, 0);
    if (attrBytes > 0) {
        std::vector<uint8_t> attrs(static_cast<size_t>(attrBytes));
        ::ImmGetCompositionStringW(imc, GCS_COMPATTR, attrs.data(),
                                   static_cast<DWORD>(attrBytes));
        size_t first = attrs.size();
        size_t last = 0;
        for (size_t i = 0; i < attrs.size(); ++i) {
            if (attrs[i] != ATTR_TARGET_CONVERTED && attrs[i] != ATTR_TARGET_NOTCONVERTED) {
                continue;
            }
            if (first > i) first = i;
            last = i + 1;
        }
        if (first < last) {
            targetBegin = utf8Length(first);
            targetEnd = utf8Length(last);
        }
    }

    if (app_) app_->SetComposition(ToUtf8(wide), caret, targetBegin, targetEnd);
}

// 確定した文字列を、WM_CHAR と同じ道で 1 文字ずつ流し込む。
//
// 自分で WM_IME_COMPOSITION を消費している以上、DefWindowProc は WM_CHAR を作らない。
// ここで渡さなければ、変換した文字はどこにも入らない。
void WinWindow::CommitComposition() {
    HIMC imc = ::ImmGetContext(hwnd_);
    if (!imc) return;
    const ImeContext context{ hwnd_, imc };
    const LONG bytes = ::ImmGetCompositionStringW(imc, GCS_RESULTSTR, nullptr, 0);
    if (bytes <= 0) return;
    std::wstring wide(static_cast<size_t>(bytes) / sizeof(wchar_t), L'\0');
    ::ImmGetCompositionStringW(imc, GCS_RESULTSTR, wide.data(), static_cast<DWORD>(bytes));

    // 未確定の文字列は先に捨てる。確定した文字が入るのはそのあとで、順序が逆だと
    // 1 フレームだけ同じ文字が 2 つ並ぶ。
    if (app_) app_->EndComposition();

    for (size_t i = 0; i < wide.size(); ++i) {
        uint32_t codepoint = wide[i];
        if (codepoint >= 0xD800 && codepoint <= 0xDBFF && i + 1 < wide.size()) {
            const uint32_t low = wide[i + 1];
            if (low >= 0xDC00 && low <= 0xDFFF) {
                codepoint = 0x10000 + ((codepoint - 0xD800) << 10) + (low - 0xDC00);
                ++i;
            }
        }
        if (app_) app_->OnChar(codepoint);
    }
}

// 変換を打ち切る。入力欄が消えた（外を押して畳まれた、オーバーレイが閉じた）のに
// IME だけが変換を続けていると、次に欄を開いた瞬間にその文字列が現れる。
void WinWindow::CancelComposition() {
    if (!hwnd_) return;
    HIMC imc = ::ImmGetContext(hwnd_);
    if (!imc) return;
    {
        const ImeContext context{ hwnd_, imc };
        ::ImmNotifyIME(imc, NI_COMPOSITIONSTR, CPS_CANCEL, 0);
    }
    composing_ = false;
    compositionInField_ = false;
    if (app_) app_->EndComposition();
}

void WinWindow::SetCursorShape(int shape) {
    cursorShape_ = shape;
    if (hwnd_) ::SetCursor(::LoadCursorW(nullptr, shape == 2   ? IDC_SIZEWE
                                                  : shape == 3 ? IDC_SIZENS
                                                  : shape == 1 ? IDC_HAND
                                                               : IDC_ARROW));
}

bool WinWindow::ClientToScreen(float x, float y, int& screenX, int& screenY) {
    if (!hwnd_) return false;
    POINT pt{ std::lround(x * dpiScale_), std::lround(y * dpiScale_) };
    if (!::ClientToScreen(hwnd_, &pt)) return false;
    screenX = pt.x;
    screenY = pt.y;
    return true;
}

void WinWindow::Wake() {
    // Called from DirectoryLoader worker threads.
    if (hwnd_) ::PostMessageW(hwnd_, WM_KITE_WAKE, 0, 0);
}

// --- painting ---------------------------------------------------------------

void WinWindow::Paint() {
    if (!rendererReady_ || !ui_ || !app_) return;

    // Cmd::ToggleTheme changes the theme inside the core, which has no way to
    // reach the caption; catching it here keeps the frame in step with the
    // client area instead of only matching at start-up.
    ApplyDarkTitleBar();

    // Same idea for the text formats: Cmd::FontLarger and friends move the size
    // in the core, and DirectWrite formats are built once at that size. Cheap on
    // every frame - UpdateTheme compares first and only rebuilds on a change.
    renderer_.UpdateTheme(app_->theme(), dpiScale_ * 96.0f);

    if (icons_) {
        // Icons are drawn into a cell as tall as a row, so that - scaled by DPI -
        // is the size worth asking the shell for.
        icons_->SetPixelSize(
            static_cast<uint32_t>(std::lround(app_->theme().rowHeight * 0.8f * dpiScale_)));
        // Before BeginFrame: this uploads bitmaps, which must not happen between
        // BeginDraw and EndDraw.
        icons_->Pump(renderer_);
    }

    if (!renderer_.BeginFrame()) return;
    ui_->Paint(renderer_);
    if (!renderer_.EndFrame()) {
        // Device lost: the next WM_PAINT rebuilds and redraws.
        ::InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    // The paint just decided which rows are on screen, and that is what queues
    // icon requests; they go out on the next Pump. Without asking for that frame
    // here, a folder opened from the keyboard keeps its fallback glyphs until
    // the user happens to move the mouse.
    if (icons_ && icons_->hasPendingRequests()) ::InvalidateRect(hwnd_, nullptr, FALSE);

    // 描き終わって初めてキャレットの位置が決まる。変換中はそのたびに IME へ教え直す
    // ─ 打つほど右へ伸びるので、始めたときの位置に置いたままでは候補一覧が離れていく。
    imeCaret_ = ui_->caretRect();
    if (composing_) {
        // 入力欄が消えたのに IME だけが変換を続けている（外を押して畳まれた、
        // オーバーレイが閉じた）。打ちかけの文字列は捨てる ─ 入力欄の «外を押したら
        // 畳む。打った名前は捨てる» をそのまま変換にも当てる。一覧の上で始まった
        // 変換は巻き添えにしない（そちらに畳まれる欄は無い）。
        if (compositionInField_ && !app_->acceptsText()) {
            CancelComposition();
        } else {
            UpdateImeWindow();
        }
    }

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
    // Called on every frame, so do the work only when the theme actually moved:
    // DwmSetWindowAttribute redraws the frame, and doing that 60 times a second
    // makes the caption flicker.
    const int wanted = app_->theme().dark ? 1 : 0;
    if (titleBarDark_ == wanted) return;
    titleBarDark_ = wanted;

    const BOOL dark = wanted ? TRUE : FALSE;
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

    // No shell-menu relay here on purpose. Owner-drawn menu items are drawn by
    // the extension that owns them, inside kite_shellhost.exe, against that
    // process's own window. This one never talks to an IContextMenu.
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
            // A wake means some worker finished something. PumpLoader only
            // repaints when a listing arrived, and icons come back on the same
            // signal without going through it - without this they would sit in
            // the queue until an unrelated event happened to redraw the window.
            Invalidate();
            return 0;

        case WM_KITE_DROP:
            RunPendingDrop();
            return 0;

        // A second launch handed its command line over instead of opening a
        // window of its own (ROADMAP P1-3). Anything else that arrives here is
        // some other program's message and is left alone.
        case WM_COPYDATA: {
            const auto* data = reinterpret_cast<const COPYDATASTRUCT*>(lparam);
            if (!app_ || !data || data->dwData != kForwardPathsId) break;
            app_->OpenForwardedPaths(ParseForwardedPaths(data->lpData, data->cbData));
            // Raising the window is the point even when no path came with it:
            // launching Kite while Kite is running has to put it in front of the
            // caller, or the launch looks like it did nothing at all.
            if (::IsIconic(hwnd_)) ::ShowWindow(hwnd_, SW_RESTORE);
            ::SetForegroundWindow(hwnd_);
            return TRUE;
        }

        case WM_TIMER:
            if (wparam == kStatusTimerId) {
                ::KillTimer(hwnd_, kStatusTimerId);
                Invalidate();
            } else if (wparam == kDragDropTimerId) {
                ::KillTimer(hwnd_, kDragDropTimerId);
                EnableDragAndDrop();
            }
            return 0;

        case WM_ACTIVATE:
            // The focus ring and the cursor row are drawn differently when the
            // keyboard is somewhere else, so the core is told. Note this also
            // fires while a shell menu is up: the host's window takes the
            // foreground, and Kite showing itself as inactive is the truth.
            if (app_) app_->SetWindowActive(LOWORD(wparam) != WA_INACTIVE);
            break;

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

        // --- IME: the composition is drawn by Kite, in the field being typed
        // into (see the block above WinWindow::UpdateImeWindow) ---
        case WM_IME_SETCONTEXT:
            // 自分で描く以上、IME の変換窓は要らない。落とさないと同じ文字が 2 か所に
            // 出る（こちらの入力欄と、IME の窓と）。候補一覧のほうは残す。
            return ::DefWindowProcW(hwnd_, message, wparam,
                                    lparam & ~static_cast<LPARAM>(ISC_SHOWUICOMPOSITIONWINDOW));

        case WM_IME_STARTCOMPOSITION:
            // まだ 1 文字も来ていないので、位置は直前の描画が決めたキャレットのまま
            // でよい。以後は Paint() が毎フレーム教え直す。
            composing_ = true;
            // 入力欄の中で始まった変換だけが、その欄と運命を共にする（Paint() 参照）。
            compositionInField_ = app_ && app_->acceptsText();
            UpdateImeWindow();
            return 0;

        case WM_IME_COMPOSITION: {
            // 確定が先。節を 1 つ確定して残りの変換を続ける IME があるので、
            // GCS_RESULTSTR と GCS_COMPSTR は同時に立ちうる ─ どちらか一方にしない。
            if (lparam & GCS_RESULTSTR) CommitComposition();
            if (lparam & GCS_COMPSTR) {
                ReadComposition();
            } else if (!(lparam & GCS_RESULTSTR) && app_) {
                // 変換そのものが取り消された（lparam が空で来る）。
                app_->EndComposition();
            }
            return 0;
        }

        case WM_IME_ENDCOMPOSITION:
            composing_ = false;
            compositionInField_ = false;
            if (app_) app_->EndComposition();
            break;

        case WM_IME_CHAR:
            // 確定した文字は GCS_RESULTSTR で受け取っている。ここを通すと二重に入る。
            return 0;

        // --- mouse ---
        case WM_MOUSEMOVE:
            // Windows sends no "the pointer left" message unless it is asked to,
            // once, per departure. Without it the row under the pointer would
            // stay lit after the mouse has moved on to another window.
            if (!mouseTracked_) {
                TRACKMOUSEEVENT track{ sizeof(track), TME_LEAVE, hwnd_, 0 };
                mouseTracked_ = ::TrackMouseEvent(&track) != FALSE;
            }
            DispatchMouse(ui::MouseEvent::Type::Move, 0, 0, wparam, lparam);
            return 0;
        case WM_MOUSELEAVE:
            mouseTracked_ = false;
            if (ui_) {
                ui::MouseEvent leave;
                leave.type = ui::MouseEvent::Type::Leave;
                ui_->OnMouse(leave);
            }
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
