/// @file
/// @brief UI 層が前提とする描画インターフェース。
///
/// 画面上のものはすべてこのプリミティブ経由で描かれる。UI を別プラットフォームへ
/// 移すときは、ビューを書き換えるのではなくこのクラスを実装する
/// （platform/win/D2DRenderer.cpp を参照）。

#pragma once

#include <string>
#include <string_view>

#include "core/base/Types.h"

namespace kite::ui {

/// @brief 文字の用途別フォント。
enum class FontRole : uint8_t {
    Ui,       ///< 標準 UI フォント
    UiSmall,  ///< 補助情報用の小さめフォント
    UiBold,   ///< 見出し用の太字フォント
    Mono,     ///< 等幅フォント。キー表記などの桁を揃える用途
};

/// @brief 文字の水平方向の揃え。
enum class TextAlign : uint8_t {
    Left,    ///< 左揃え
    Center,  ///< 中央揃え
    Right,   ///< 右揃え
};

/// @brief 描画プリミティブのインターフェース。座標はすべて DIP。
class Renderer {
public:
    virtual ~Renderer() = default;

    /// @brief 以降の描画を矩形で切り取る。
    /// @param[in] r 切り取る領域
    /// @note PopClip() と対で呼ぶこと
    virtual void PushClip(const RectF& r) = 0;

    /// @brief 直近の PushClip() を取り消す。
    virtual void PopClip() = 0;

    /// @brief 矩形を塗りつぶす。
    /// @param[in] r 塗る領域
    /// @param[in] c 塗る色
    virtual void FillRect(const RectF& r, const Color& c) = 0;

    /// @brief 角丸矩形を塗りつぶす。
    /// @param[in] r 塗る領域
    /// @param[in] radius 角の半径
    /// @param[in] c 塗る色
    virtual void FillRoundRect(const RectF& r, float radius, const Color& c) = 0;

    /// @brief 矩形の輪郭を描く。
    /// @param[in] r 輪郭を描く領域
    /// @param[in] c 線の色
    /// @param[in] width 線の太さ
    /// @note 線は領域の内側に収まるよう半分だけ内側へ寄せて描く
    virtual void StrokeRect(const RectF& r, const Color& c, float width) = 0;

    /// @brief 直線を描く。
    /// @param[in] x1 始点の X 座標
    /// @param[in] y1 始点の Y 座標
    /// @param[in] x2 終点の X 座標
    /// @param[in] y2 終点の Y 座標
    /// @param[in] c 線の色
    /// @param[in] width 線の太さ
    virtual void DrawLine(float x1, float y1, float x2, float y2, const Color& c,
                          float width) = 0;

    /// @brief 三角形を塗りつぶす。
    /// @param[in] a 頂点 1
    /// @param[in] b 頂点 2
    /// @param[in] c 頂点 3
    /// @param[in] color 塗る色
    virtual void FillTriangle(PointF a, PointF b, PointF c, const Color& color) = 0;

    /// @brief 文字列を描く。
    /// @param[in] utf8 描く文字列
    /// @param[in] r 描画領域。この中で垂直中央に配置する
    /// @param[in] c 文字色
    /// @param[in] role 使うフォント
    /// @param[in] align 水平方向の揃え
    /// @note 領域に収まらない場合は末尾を省略記号にする
    virtual void DrawText(std::string_view utf8, const RectF& r, const Color& c, FontRole role,
                          TextAlign align) = 0;

    /// @brief 文字列の描画幅を測る。
    /// @param[in] utf8 測る文字列
    /// @param[in] role 使うフォント
    /// @return 幅（DIP）
    virtual float MeasureText(std::string_view utf8, FontRole role) = 0;

    /// @brief フォントの行高を返す。
    /// @param[in] role 対象のフォント
    /// @return 行高（DIP）
    virtual float LineHeight(FontRole role) = 0;

    /// @brief 描画対象の大きさを返す。
    /// @return 描画面のサイズ（DIP）
    virtual SizeF surfaceSize() const = 0;
};

}  // namespace kite::ui
