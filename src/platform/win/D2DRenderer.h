#pragma once

#include <windows.h>

#include <d2d1_1.h>
#include <d3d11.h>
#include <dwrite.h>
#include <dxgi1_2.h>

#include <string>
#include <unordered_map>

#include "core/theme/Theme.h"
#include "ui/Renderer.h"

namespace kite::win {

// Direct2D 1.1 implementation of ui::Renderer, presenting through a DXGI flip
// swap chain (D3D11 device -> ID2D1Device -> ID2D1DeviceContext).
//
// The older ID2D1HwndRenderTarget path was tried first and its EndDraw returns
// S_OK while nothing ever reaches the screen on at least one Intel driver, so
// the modern path is the only one used. It is also the one Microsoft has
// recommended since Windows 8 and it composites better while resizing.
//
// Text goes through per-role IDWriteTextFormat objects that already carry
// ellipsis trimming and vertical centring, so drawing a row cell is one call.
class D2DRenderer final : public ui::Renderer {
public:
    ~D2DRenderer() override;

    bool Initialize(HWND hwnd, const Theme& theme, float dpi);
    void UpdateTheme(const Theme& theme, float dpi);
    void Resize(UINT pixelWidth, UINT pixelHeight);

    bool BeginFrame();
    // Returns false when the device was lost; resources are dropped and the
    // next frame rebuilds them.
    bool EndFrame();

    void PushClip(const RectF& r) override;
    void PopClip() override;
    void FillRect(const RectF& r, const Color& c) override;
    void FillRoundRect(const RectF& r, float radius, const Color& c) override;
    void StrokeRect(const RectF& r, const Color& c, float width) override;
    void DrawLine(float x1, float y1, float x2, float y2, const Color& c, float width) override;
    void FillTriangle(PointF a, PointF b, PointF c, const Color& color) override;
    void DrawText(std::string_view utf8, const RectF& r, const Color& c, ui::FontRole role,
                  ui::TextAlign align) override;
    float MeasureText(std::string_view utf8, ui::FontRole role) override;
    float LineHeight(ui::FontRole role) override;
    SizeF surfaceSize() const override;

private:
    bool CreateDeviceResources();
    void ReleaseDeviceResources();
    bool BindBackBuffer();
    void CreateTextFormats(const Theme& theme);
    IDWriteTextFormat* FormatFor(ui::FontRole role) const;
    ID2D1SolidColorBrush* Brush(const Color& c);

    HWND hwnd_ = nullptr;
    float dpi_ = 96.0f;
    Theme theme_;

    ID2D1Factory1* d2dFactory_ = nullptr;
    IDWriteFactory* dwriteFactory_ = nullptr;

    ID3D11Device* d3dDevice_ = nullptr;
    ID2D1Device* d2dDevice_ = nullptr;
    ID2D1DeviceContext* context_ = nullptr;
    IDXGISwapChain1* swapChain_ = nullptr;
    ID2D1Bitmap1* backBuffer_ = nullptr;
    ID2D1SolidColorBrush* brush_ = nullptr;

    IDWriteTextFormat* formats_[4] = {};
    int clipDepth_ = 0;
    bool drawing_ = false;

    // Measuring builds a transient IDWriteTextLayout, far too slow to repeat
    // per row per frame; widths are stable, so cache them by string.
    std::unordered_map<std::string, float> measureCache_[4];
};

}  // namespace kite::win
