// Kite - the drawing interface the UI layer is written against.
//
// Everything on screen is painted through these primitives, so porting the UI
// to another platform means implementing this one class (see
// platform/win/D2DRenderer.cpp) rather than rewriting any views.
#pragma once

#include <string>
#include <string_view>

#include "core/base/Types.h"

namespace kite::ui {

enum class FontRole : uint8_t { Ui, UiSmall, UiBold, Mono };
enum class TextAlign : uint8_t { Left, Center, Right };

class Renderer {
public:
    virtual ~Renderer() = default;

    virtual void PushClip(const RectF& r) = 0;
    virtual void PopClip() = 0;

    virtual void FillRect(const RectF& r, const Color& c) = 0;
    virtual void FillRoundRect(const RectF& r, float radius, const Color& c) = 0;
    virtual void StrokeRect(const RectF& r, const Color& c, float width) = 0;
    virtual void DrawLine(float x1, float y1, float x2, float y2, const Color& c, float width) = 0;
    virtual void FillTriangle(PointF a, PointF b, PointF c, const Color& color) = 0;

    // Vertically centred inside `r`, clipped with an ellipsis when too long.
    virtual void DrawText(std::string_view utf8, const RectF& r, const Color& c, FontRole role,
                          TextAlign align) = 0;

    virtual float MeasureText(std::string_view utf8, FontRole role) = 0;
    virtual float LineHeight(FontRole role) = 0;

    // Size of the drawable area, in DIPs.
    virtual SizeF surfaceSize() const = 0;
};

}  // namespace kite::ui
