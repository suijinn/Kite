/// @file
/// @brief Direct2D 1.1 による ui::Renderer 実装。

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

/// @brief DXGI フリップスワップチェーンに描く ui::Renderer 実装。
///
/// D3D11 デバイス → ID2D1Device → ID2D1DeviceContext という経路をとる。
///
/// @note 先に ID2D1HwndRenderTarget を試したが、少なくとも手元の Intel ドライバでは
///       EndDraw が S_OK を返しながら画面に何も出なかった。40 行の最小再現コードでも
///       同じだったため、Microsoft が Windows 8 以降推奨しているこちらの経路のみを
///       使う。リサイズ時の見た目も良い。「単純化」して戻さないこと
class D2DRenderer final : public ui::Renderer {
public:
    /// @brief COM オブジェクトをすべて解放する。
    ~D2DRenderer() override;

    /// @brief ファクトリとフォントを用意する。
    /// @param[in] hwnd 描画先のウィンドウ
    /// @param[in] theme フォント名とサイズを取り出すテーマ
    /// @param[in] dpi ウィンドウの DPI
    /// @return 成功したら true
    /// @note デバイスとスワップチェーンはここでは作らない。表示前のウィンドウに
    ///       結び付けると、コンポジタが後で捨てるサーフェスに描き続けることになる
    bool Initialize(HWND hwnd, const Theme& theme, float dpi);

    /// @brief テーマまたは DPI の変更を反映する。
    /// @param[in] theme 新しいテーマ
    /// @param[in] dpi 新しい DPI
    void UpdateTheme(const Theme& theme, float dpi);

    /// @brief 描画対象の大きさを変更する。
    /// @param[in] pixelWidth 新しい幅（ピクセル）
    /// @param[in] pixelHeight 新しい高さ（ピクセル）
    void Resize(UINT pixelWidth, UINT pixelHeight);

    /// @brief 1 フレームの描画を開始する。
    /// @return 開始できたら true。デバイスを用意できなければ false
    bool BeginFrame();

    /// @brief 1 フレームの描画を終えて画面に出す。
    /// @return 成功したら true。デバイスが失われた場合は false を返し、
    ///         資源を破棄する（次フレームで再構築される）
    bool EndFrame();

    /// @copydoc ui::Renderer::PushClip
    void PushClip(const RectF& r) override;

    /// @copydoc ui::Renderer::PopClip
    void PopClip() override;

    /// @copydoc ui::Renderer::FillRect
    void FillRect(const RectF& r, const Color& c) override;

    /// @copydoc ui::Renderer::FillRoundRect
    void FillRoundRect(const RectF& r, float radius, const Color& c) override;

    /// @copydoc ui::Renderer::StrokeRect
    void StrokeRect(const RectF& r, const Color& c, float width) override;

    /// @copydoc ui::Renderer::DrawLine
    void DrawLine(float x1, float y1, float x2, float y2, const Color& c, float width) override;

    /// @copydoc ui::Renderer::FillTriangle
    void FillTriangle(PointF a, PointF b, PointF c, const Color& color) override;

    /// @copydoc ui::Renderer::DrawIcon
    void DrawIcon(uint32_t iconId, const RectF& box) override;

    /// @brief アイコンの画素を受け取り、描画に使えるようにする。
    /// @param[in] iconId 識別子。同じ値で呼ぶと差し替わる
    /// @param[in] width 幅（ピクセル）
    /// @param[in] height 高さ（ピクセル）
    /// @param[in] bgra 乗算済みアルファの BGRA。width * height * 4 バイト
    /// @return 受け取れたら true
    /// @note デバイスが失われるとビットマップも消える。呼び出し側は画素を保持し、
    ///       iconsLost() が立ったら入れ直すこと
    bool UploadIcon(uint32_t iconId, uint32_t width, uint32_t height, const uint8_t* bgra);

    /// @brief 受け取ったアイコンをすべて捨てる。
    /// @note ホストが入れ替わり、識別子の意味が変わったときに呼ぶ。以降 DrawIcon()
    ///       は入れ直されるまで何も描かない
    void ClearIcons();

    /// @brief アイコンを入れ直す必要があるかを返す。
    /// @return デバイス消失でビットマップを失っていれば true
    bool iconsLost() const { return iconsLost_; }

    /// @brief アイコンを入れ直したことを記録する。
    void ClearIconsLost() { iconsLost_ = false; }

    /// @copydoc ui::Renderer::DrawText
    void DrawText(std::string_view utf8, const RectF& r, const Color& c, ui::FontRole role,
                  ui::TextAlign align) override;

    /// @copydoc ui::Renderer::MeasureText
    /// @note 計測は一時的な IDWriteTextLayout を作るため毎行毎フレームには重すぎる。
    ///       結果は変わらないので文字列をキーにキャッシュしている
    float MeasureText(std::string_view utf8, ui::FontRole role) override;

    /// @copydoc ui::Renderer::LineHeight
    float LineHeight(ui::FontRole role) override;

    /// @copydoc ui::Renderer::surfaceSize
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
    bool iconsLost_ = false;

    std::unordered_map<std::string, float> measureCache_[4];
    std::unordered_map<uint32_t, ID2D1Bitmap1*> icons_;
};

}  // namespace kite::win
