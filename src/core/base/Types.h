/// @file
/// @brief 幾何と色の基本値型。OS 非依存。

#pragma once

#include <algorithm>
#include <cstdint>

namespace kite {

/// @brief 2 次元の点。単位は DIP。
struct PointF {
    float x = 0.0f;  ///< X 座標
    float y = 0.0f;  ///< Y 座標
};

/// @brief 幅と高さの組。単位は DIP。
struct SizeF {
    float w = 0.0f;  ///< 幅
    float h = 0.0f;  ///< 高さ
};

/// @brief 軸平行な矩形。左上を含み右下を含まない半開区間として扱う。
struct RectF {
    float l = 0.0f;  ///< 左端
    float t = 0.0f;  ///< 上端
    float r = 0.0f;  ///< 右端（この座標自体は含まない）
    float b = 0.0f;  ///< 下端（この座標自体は含まない）

    /// @brief 幅を返す。
    /// @return 右端 - 左端。反転していれば負になる
    constexpr float w() const { return r - l; }

    /// @brief 高さを返す。
    /// @return 下端 - 上端。反転していれば負になる
    constexpr float h() const { return b - t; }

    /// @brief 面積を持たないかを判定する。
    /// @return 幅または高さが 0 以下なら true
    constexpr bool empty() const { return r <= l || b <= t; }

    /// @brief 点が矩形の内側にあるかを判定する。
    /// @param[in] x 判定する点の X 座標
    /// @param[in] y 判定する点の Y 座標
    /// @return 含まれていれば true。右端と下端は含まない
    constexpr bool contains(float x, float y) const { return x >= l && x < r && y >= t && y < b; }

    /// @brief 中心点を返す。
    /// @return 矩形の中心座標
    constexpr PointF center() const { return { (l + r) * 0.5f, (t + b) * 0.5f }; }

    /// @brief 四辺を同じ量だけ内側に縮めた矩形を返す。
    /// @param[in] d 縮める量。負の値を渡せば外側に広がる
    /// @return 縮めた結果の矩形。縮めすぎると empty() な矩形になりうる
    constexpr RectF inset(float d) const { return { l + d, t + d, r - d, b - d }; }

    /// @brief 水平・垂直で別々の量だけ内側に縮めた矩形を返す。
    /// @param[in] dx 左右を縮める量
    /// @param[in] dy 上下を縮める量
    /// @return 縮めた結果の矩形
    constexpr RectF inset(float dx, float dy) const { return { l + dx, t + dy, r - dx, b - dy }; }

    /// @brief 矩形を平行移動する。
    /// @param[in] dx X 方向の移動量
    /// @param[in] dy Y 方向の移動量
    /// @return 移動後の矩形
    constexpr RectF offset(float dx, float dy) const { return { l + dx, t + dy, r + dx, b + dy }; }

    /// @brief 他の矩形との共通部分を返す。
    /// @param[in] o 交差させる矩形
    /// @return 共通部分。重なりが無い場合は empty() が true になる矩形
    RectF intersect(const RectF& o) const {
        return { std::max(l, o.l), std::max(t, o.t), std::min(r, o.r), std::min(b, o.b) };
    }

    /// @brief 左上位置とサイズから矩形を作る。
    /// @param[in] x 左端
    /// @param[in] y 上端
    /// @param[in] width 幅
    /// @param[in] height 高さ
    /// @return 生成した矩形
    static constexpr RectF xywh(float x, float y, float width, float height) {
        return { x, y, x + width, y + height };
    }
};

/// @brief ストレートアルファの RGBA 色。各成分は 0.0〜1.0。
struct Color {
    float r = 0.0f;  ///< 赤成分
    float g = 0.0f;  ///< 緑成分
    float b = 0.0f;  ///< 青成分
    float a = 1.0f;  ///< アルファ成分。1.0 が不透明

    /// @brief 0xRRGGBB 形式の整数から色を作る。
    /// @param[in] v 0xRRGGBB。上位 8 ビットは無視される
    /// @param[in] alpha アルファ成分。既定は不透明
    /// @return 生成した色
    static constexpr Color hex(uint32_t v, float alpha = 1.0f) {
        return { ((v >> 16) & 0xFF) / 255.0f,
                 ((v >> 8) & 0xFF) / 255.0f,
                 (v & 0xFF) / 255.0f,
                 alpha };
    }

    /// @brief アルファ成分だけを差し替えた色を返す。
    /// @param[in] value 新しいアルファ成分
    /// @return RGB は元のまま、アルファだけ変えた色
    constexpr Color alpha(float value) const { return { r, g, b, value }; }

    /// @brief 2 色を線形補間する。
    /// @param[in] x 補間の始点となる色
    /// @param[in] y 補間の終点となる色
    /// @param[in] t 補間係数。0.0 で x、1.0 で y。範囲外はクランプしない
    /// @return 補間した色
    static Color mix(const Color& x, const Color& y, float t) {
        return { x.r + (y.r - x.r) * t,
                 x.g + (y.g - x.g) * t,
                 x.b + (y.b - x.b) * t,
                 x.a + (y.a - x.a) * t };
    }
};

}  // namespace kite
