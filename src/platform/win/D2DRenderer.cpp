#include "platform/win/D2DRenderer.h"

#include <algorithm>

#include "platform/win/WinUtf.h"

namespace kite::win {
namespace {

template <typename T>
void SafeRelease(T*& p) {
    if (p) {
        p->Release();
        p = nullptr;
    }
}

D2D1_COLOR_F ToD2D(const Color& c) { return D2D1::ColorF(c.r, c.g, c.b, c.a); }
D2D1_RECT_F ToD2D(const RectF& r) { return D2D1::RectF(r.l, r.t, r.r, r.b); }
size_t FormatIndex(ui::FontRole role) { return static_cast<size_t>(role); }

}  // namespace

D2DRenderer::~D2DRenderer() {
    ReleaseDeviceResources();
    for (IDWriteTextFormat*& f : formats_) SafeRelease(f);
    SafeRelease(dwriteFactory_);
    SafeRelease(d2dFactory_);
}

bool D2DRenderer::Initialize(HWND hwnd, const Theme& theme, float dpi) {
    hwnd_ = hwnd;
    dpi_ = dpi;
    theme_ = theme;

    D2D1_FACTORY_OPTIONS options{};
    if (FAILED(::D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1),
                                   &options, reinterpret_cast<void**>(&d2dFactory_)))) {
        return false;
    }
    if (FAILED(::DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                     reinterpret_cast<IUnknown**>(&dwriteFactory_)))) {
        return false;
    }
    CreateTextFormats(theme);

    // Device and swap chain are created on the first frame, once the window is
    // actually visible.
    return true;
}

void D2DRenderer::CreateTextFormats(const Theme& theme) {
    for (IDWriteTextFormat*& f : formats_) SafeRelease(f);
    for (auto& cache : measureCache_) cache.clear();

    const std::wstring ui = ToWide(theme.fontFamily);
    const std::wstring mono = ToWide(theme.monoFamily);
    const float scale = theme.uiScale;

    struct Spec {
        const std::wstring* family;
        float size;
        DWRITE_FONT_WEIGHT weight;
    };
    const Spec specs[4] = {
        { &ui, theme.fontSize * scale, DWRITE_FONT_WEIGHT_NORMAL },
        { &ui, (theme.fontSize - 1.0f) * scale, DWRITE_FONT_WEIGHT_NORMAL },
        { &ui, theme.fontSize * scale, DWRITE_FONT_WEIGHT_SEMI_BOLD },
        { &mono, (theme.fontSize - 1.0f) * scale, DWRITE_FONT_WEIGHT_NORMAL },
    };

    for (size_t i = 0; i < 4; ++i) {
        IDWriteTextFormat* format = nullptr;
        // An empty locale name lets DirectWrite apply per-script defaults,
        // which is what keeps mixed Japanese / Latin file names readable.
        if (FAILED(dwriteFactory_->CreateTextFormat(specs[i].family->c_str(), nullptr,
                                                    specs[i].weight, DWRITE_FONT_STYLE_NORMAL,
                                                    DWRITE_FONT_STRETCH_NORMAL, specs[i].size, L"",
                                                    &format))) {
            if (FAILED(dwriteFactory_->CreateTextFormat(L"Segoe UI", nullptr, specs[i].weight,
                                                        DWRITE_FONT_STYLE_NORMAL,
                                                        DWRITE_FONT_STRETCH_NORMAL, specs[i].size,
                                                        L"", &format))) {
                continue;
            }
        }
        format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

        IDWriteInlineObject* ellipsis = nullptr;
        if (SUCCEEDED(dwriteFactory_->CreateEllipsisTrimmingSign(format, &ellipsis))) {
            DWRITE_TRIMMING trimming = { DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
            format->SetTrimming(&trimming, ellipsis);
            ellipsis->Release();
        }
        formats_[i] = format;
    }
}

bool D2DRenderer::CreateDeviceResources() {
    if (context_ && backBuffer_) return true;
    ReleaseDeviceResources();

    const UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;  // required by Direct2D
    HRESULT hr = ::D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, nullptr, 0,
                                     D3D11_SDK_VERSION, &d3dDevice_, nullptr, nullptr);
    if (FAILED(hr)) {
        // A machine without a usable 3D driver still gets a working window.
        hr = ::D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, nullptr, 0,
                                 D3D11_SDK_VERSION, &d3dDevice_, nullptr, nullptr);
    }
    if (FAILED(hr)) return false;

    IDXGIDevice1* dxgiDevice = nullptr;
    if (FAILED(d3dDevice_->QueryInterface(__uuidof(IDXGIDevice1),
                                          reinterpret_cast<void**>(&dxgiDevice)))) {
        return false;
    }
    // One frame of latency keeps input feeling immediate.
    dxgiDevice->SetMaximumFrameLatency(1);

    bool ok = SUCCEEDED(d2dFactory_->CreateDevice(dxgiDevice, &d2dDevice_)) &&
              SUCCEEDED(d2dDevice_->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE,
                                                        &context_));
    if (ok) {
        IDXGIAdapter* adapter = nullptr;
        IDXGIFactory2* dxgiFactory = nullptr;
        if (SUCCEEDED(dxgiDevice->GetAdapter(&adapter))) {
            adapter->GetParent(__uuidof(IDXGIFactory2), reinterpret_cast<void**>(&dxgiFactory));
            adapter->Release();
        }

        if (dxgiFactory) {
            RECT rc{};
            ::GetClientRect(hwnd_, &rc);

            DXGI_SWAP_CHAIN_DESC1 desc{};
            desc.Width = static_cast<UINT>(std::max<LONG>(1, rc.right - rc.left));
            desc.Height = static_cast<UINT>(std::max<LONG>(1, rc.bottom - rc.top));
            desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            desc.SampleDesc.Count = 1;
            desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            desc.BufferCount = 2;
            desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
            desc.Scaling = DXGI_SCALING_NONE;  // no blurry stretch while resizing
            desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

            ok = SUCCEEDED(dxgiFactory->CreateSwapChainForHwnd(d3dDevice_, hwnd_, &desc, nullptr,
                                                               nullptr, &swapChain_));
            // Kite draws its own everything; let it keep Alt+Enter.
            dxgiFactory->MakeWindowAssociation(hwnd_, DXGI_MWA_NO_ALT_ENTER);
            dxgiFactory->Release();
        } else {
            ok = false;
        }
    }
    dxgiDevice->Release();

    if (!ok || !BindBackBuffer()) {
        ReleaseDeviceResources();
        return false;
    }

    context_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    if (FAILED(context_->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 1), &brush_))) {
        ReleaseDeviceResources();
        return false;
    }
    return true;
}

bool D2DRenderer::BindBackBuffer() {
    if (!swapChain_ || !context_) return false;
    SafeRelease(backBuffer_);

    IDXGISurface* surface = nullptr;
    if (FAILED(swapChain_->GetBuffer(0, __uuidof(IDXGISurface),
                                     reinterpret_cast<void**>(&surface)))) {
        return false;
    }
    // Giving the target bitmap the real DPI is what makes every coordinate in
    // the UI layer a DIP rather than a pixel.
    const D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), dpi_, dpi_);

    const HRESULT hr = context_->CreateBitmapFromDxgiSurface(surface, &props, &backBuffer_);
    surface->Release();
    if (FAILED(hr)) return false;

    context_->SetTarget(backBuffer_);
    context_->SetDpi(dpi_, dpi_);
    return true;
}

void D2DRenderer::ReleaseDeviceResources() {
    if (context_) context_->SetTarget(nullptr);
    // Bitmaps belong to the device that is going away. The pixels still exist on
    // the other side of the icon provider, so this is a flag rather than a loss.
    for (auto& [id, bitmap] : icons_) SafeRelease(bitmap);
    icons_.clear();
    iconsLost_ = true;
    SafeRelease(brush_);
    SafeRelease(backBuffer_);
    SafeRelease(swapChain_);
    SafeRelease(context_);
    SafeRelease(d2dDevice_);
    SafeRelease(d3dDevice_);
    drawing_ = false;
    clipDepth_ = 0;
}

void D2DRenderer::UpdateTheme(const Theme& theme, float dpi) {
    const bool fontsChanged = theme.fontFamily != theme_.fontFamily ||
                              theme.monoFamily != theme_.monoFamily ||
                              theme.fontSize != theme_.fontSize || theme.uiScale != theme_.uiScale;
    theme_ = theme;
    if (dpi != dpi_) {
        dpi_ = dpi;
        if (context_) BindBackBuffer();
    }
    if (fontsChanged) CreateTextFormats(theme_);
}

void D2DRenderer::Resize(UINT pixelWidth, UINT pixelHeight) {
    if (!swapChain_ || !context_) return;

    context_->SetTarget(nullptr);
    SafeRelease(backBuffer_);

    const HRESULT hr = swapChain_->ResizeBuffers(0, std::max<UINT>(1, pixelWidth),
                                                 std::max<UINT>(1, pixelHeight),
                                                 DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(hr)) {
        ReleaseDeviceResources();
        return;
    }
    BindBackBuffer();
}

bool D2DRenderer::BeginFrame() {
    if (!CreateDeviceResources()) return false;
    clipDepth_ = 0;
    context_->BeginDraw();
    context_->SetTransform(D2D1::Matrix3x2F::Identity());
    drawing_ = true;
    return true;
}

bool D2DRenderer::EndFrame() {
    if (!drawing_) return false;
    while (clipDepth_ > 0) {
        context_->PopAxisAlignedClip();
        --clipDepth_;
    }
    drawing_ = false;

    const HRESULT drawResult = context_->EndDraw();
    if (drawResult == D2DERR_RECREATE_TARGET) {
        ReleaseDeviceResources();
        return false;
    }

    const HRESULT presentResult = swapChain_->Present(1, 0);
    if (presentResult == DXGI_ERROR_DEVICE_REMOVED || presentResult == DXGI_ERROR_DEVICE_RESET) {
        ReleaseDeviceResources();
        return false;
    }
    return SUCCEEDED(drawResult) && SUCCEEDED(presentResult);
}

ID2D1SolidColorBrush* D2DRenderer::Brush(const Color& c) {
    brush_->SetColor(ToD2D(c));
    return brush_;
}

void D2DRenderer::PushClip(const RectF& r) {
    if (!drawing_) return;
    context_->PushAxisAlignedClip(ToD2D(r), D2D1_ANTIALIAS_MODE_ALIASED);
    ++clipDepth_;
}

void D2DRenderer::PopClip() {
    if (!drawing_ || clipDepth_ == 0) return;
    context_->PopAxisAlignedClip();
    --clipDepth_;
}

void D2DRenderer::FillRect(const RectF& r, const Color& c) {
    if (!drawing_ || c.a <= 0.0f || r.empty()) return;
    context_->FillRectangle(ToD2D(r), Brush(c));
}

void D2DRenderer::FillRoundRect(const RectF& r, float radius, const Color& c) {
    if (!drawing_ || c.a <= 0.0f || r.empty()) return;
    context_->FillRoundedRectangle(D2D1::RoundedRect(ToD2D(r), radius, radius), Brush(c));
}

void D2DRenderer::StrokeRect(const RectF& r, const Color& c, float width) {
    if (!drawing_ || c.a <= 0.0f) return;
    // Inset by half the stroke so the outline stays inside `r`.
    context_->DrawRectangle(ToD2D(r.inset(width * 0.5f)), Brush(c), width);
}

void D2DRenderer::DrawLine(float x1, float y1, float x2, float y2, const Color& c, float width) {
    if (!drawing_ || c.a <= 0.0f) return;
    context_->DrawLine(D2D1::Point2F(x1, y1), D2D1::Point2F(x2, y2), Brush(c), width);
}

void D2DRenderer::FillTriangle(PointF a, PointF b, PointF c, const Color& color) {
    if (!drawing_ || color.a <= 0.0f) return;

    ID2D1PathGeometry* geometry = nullptr;
    if (FAILED(d2dFactory_->CreatePathGeometry(&geometry))) return;
    ID2D1GeometrySink* sink = nullptr;
    if (SUCCEEDED(geometry->Open(&sink))) {
        sink->BeginFigure(D2D1::Point2F(a.x, a.y), D2D1_FIGURE_BEGIN_FILLED);
        const D2D1_POINT_2F rest[2] = { D2D1::Point2F(b.x, b.y), D2D1::Point2F(c.x, c.y) };
        sink->AddLines(rest, 2);
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        sink->Close();
        sink->Release();
        context_->FillGeometry(geometry, Brush(color));
    }
    geometry->Release();
}

bool D2DRenderer::UploadIcon(uint32_t iconId, uint32_t width, uint32_t height,
                             const uint8_t* bgra) {
    if (!context_ || iconId == 0 || width == 0 || height == 0 || !bgra) return false;

    auto existing = icons_.find(iconId);
    if (existing != icons_.end()) {
        SafeRelease(existing->second);
        icons_.erase(existing);
    }

    // The host hands over premultiplied BGRA, which is the one layout D2D takes
    // without a conversion pass. The bitmap is created at 96 dpi so its size in
    // DIPs equals its size in pixels; DrawBitmap then scales it to whatever the
    // row height works out to.
    const D2D1_BITMAP_PROPERTIES1 properties = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_NONE,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.0f, 96.0f);

    ID2D1Bitmap1* bitmap = nullptr;
    const HRESULT hr = context_->CreateBitmap(D2D1::SizeU(width, height), bgra, width * 4,
                                              &properties, &bitmap);
    if (FAILED(hr) || !bitmap) return false;

    icons_.emplace(iconId, bitmap);
    return true;
}

void D2DRenderer::DrawIcon(uint32_t iconId, const RectF& box) {
    if (!drawing_ || iconId == 0 || box.empty()) return;
    auto it = icons_.find(iconId);
    if (it == icons_.end() || !it->second) return;

    const D2D1_SIZE_F size = it->second->GetSize();
    if (size.width <= 0.0f || size.height <= 0.0f) return;

    // Square-fit, centred. Shell icons are square, but the cell rarely is.
    const float scale = std::min(box.w() / size.width, box.h() / size.height);
    const float w = size.width * scale;
    const float h = size.height * scale;
    const float x = std::round(box.center().x - w * 0.5f);
    const float y = std::round(box.center().y - h * 0.5f);

    // Linear filtering: the source is 16 or 32 px and the cell is whatever DPI
    // and the row height make it, so the common case is a slight downscale.
    context_->DrawBitmap(it->second, D2D1::RectF(x, y, x + w, y + h), 1.0f,
                         D2D1_INTERPOLATION_MODE_LINEAR, nullptr);
}

IDWriteTextFormat* D2DRenderer::FormatFor(ui::FontRole role) const {
    IDWriteTextFormat* f = formats_[FormatIndex(role)];
    return f ? f : formats_[0];
}

void D2DRenderer::DrawText(std::string_view utf8, const RectF& r, const Color& c,
                           ui::FontRole role, ui::TextAlign align) {
    if (!drawing_ || utf8.empty() || c.a <= 0.0f || r.w() <= 1.0f) return;
    IDWriteTextFormat* format = FormatFor(role);
    if (!format) return;

    switch (align) {
        case ui::TextAlign::Center: format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER); break;
        case ui::TextAlign::Right: format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING); break;
        default: format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING); break;
    }

    const std::wstring wide = ToWide(utf8);
    context_->DrawTextW(wide.c_str(), static_cast<UINT32>(wide.size()), format, ToD2D(r), Brush(c),
                        D2D1_DRAW_TEXT_OPTIONS_CLIP, DWRITE_MEASURING_MODE_NATURAL);
}

float D2DRenderer::MeasureText(std::string_view utf8, ui::FontRole role) {
    if (utf8.empty()) return 0.0f;
    const size_t index = FormatIndex(role);
    std::unordered_map<std::string, float>& cache = measureCache_[index];

    const std::string key(utf8);
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;

    float width = 0.0f;
    if (IDWriteTextFormat* format = FormatFor(role)) {
        const std::wstring wide = ToWide(utf8);
        IDWriteTextLayout* layout = nullptr;
        if (SUCCEEDED(dwriteFactory_->CreateTextLayout(wide.c_str(),
                                                       static_cast<UINT32>(wide.size()), format,
                                                       10000.0f, 100.0f, &layout))) {
            DWRITE_TEXT_METRICS metrics{};
            if (SUCCEEDED(layout->GetMetrics(&metrics))) {
                width = metrics.widthIncludingTrailingWhitespace;
            }
            layout->Release();
        }
    }

    // Bounded so a folder full of pathological names cannot grow this forever.
    if (cache.size() > 4096) cache.clear();
    cache.emplace(key, width);
    return width;
}

float D2DRenderer::LineHeight(ui::FontRole role) {
    IDWriteTextFormat* format = FormatFor(role);
    return format ? format->GetFontSize() * 1.35f : 16.0f;
}

SizeF D2DRenderer::surfaceSize() const {
    if (!context_ || !backBuffer_) return { 0.0f, 0.0f };
    const D2D1_SIZE_F size = context_->GetSize();
    return { size.width, size.height };
}

}  // namespace kite::win
