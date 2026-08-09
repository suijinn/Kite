// Kite - basic geometry / color value types (platform independent).
#pragma once

#include <algorithm>
#include <cstdint>

namespace kite {

struct PointF {
    float x = 0.0f;
    float y = 0.0f;
};

struct SizeF {
    float w = 0.0f;
    float h = 0.0f;
};

struct RectF {
    float l = 0.0f, t = 0.0f, r = 0.0f, b = 0.0f;

    constexpr float w() const { return r - l; }
    constexpr float h() const { return b - t; }
    constexpr bool empty() const { return r <= l || b <= t; }
    constexpr bool contains(float x, float y) const { return x >= l && x < r && y >= t && y < b; }
    constexpr PointF center() const { return { (l + r) * 0.5f, (t + b) * 0.5f }; }

    constexpr RectF inset(float d) const { return { l + d, t + d, r - d, b - d }; }
    constexpr RectF inset(float dx, float dy) const { return { l + dx, t + dy, r - dx, b - dy }; }
    constexpr RectF offset(float dx, float dy) const { return { l + dx, t + dy, r + dx, b + dy }; }

    RectF intersect(const RectF& o) const {
        return { std::max(l, o.l), std::max(t, o.t), std::min(r, o.r), std::min(b, o.b) };
    }

    static constexpr RectF xywh(float x, float y, float width, float height) {
        return { x, y, x + width, y + height };
    }
};

struct Color {
    float r = 0.0f, g = 0.0f, b = 0.0f, a = 1.0f;

    // 0xRRGGBB
    static constexpr Color hex(uint32_t v, float alpha = 1.0f) {
        return { ((v >> 16) & 0xFF) / 255.0f,
                 ((v >> 8) & 0xFF) / 255.0f,
                 (v & 0xFF) / 255.0f,
                 alpha };
    }
    constexpr Color alpha(float value) const { return { r, g, b, value }; }

    static Color mix(const Color& x, const Color& y, float t) {
        return { x.r + (y.r - x.r) * t,
                 x.g + (y.g - x.g) * t,
                 x.b + (y.b - x.b) * t,
                 x.a + (y.a - x.a) * t };
    }
};

}  // namespace kite
