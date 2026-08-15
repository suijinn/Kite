#include "ui/Glyphs.h"

#include <algorithm>

namespace kite::ui::glyph {
namespace {

// Shrinks `box` to a centred square so glyphs stay proportional in any cell.
RectF Square(const RectF& box, float scale) {
    const float size = std::min(box.w(), box.h()) * scale;
    const PointF c = box.center();
    return { c.x - size * 0.5f, c.y - size * 0.5f, c.x + size * 0.5f, c.y + size * 0.5f };
}

}  // namespace

void Folder(Renderer& r, const RectF& box, const Color& c) {
    const RectF s = Square(box, 0.78f);
    const float tabW = s.w() * 0.44f;
    const float tabH = s.h() * 0.16f;
    // Back flap, then the body slightly lower to suggest depth.
    r.FillRoundRect({ s.l, s.t + s.h() * 0.10f, s.l + tabW, s.t + s.h() * 0.10f + tabH * 2.0f },
                    1.5f, c.alpha(c.a * 0.85f));
    r.FillRoundRect({ s.l, s.t + s.h() * 0.24f, s.r, s.b }, 2.0f, c);
}

void File(Renderer& r, const RectF& box, const Color& c) {
    const RectF s = Square(box, 0.72f);
    const float w = s.w() * 0.80f;
    const float fold = w * 0.36f;
    const RectF body = { s.l + (s.w() - w) * 0.5f, s.t, s.l + (s.w() - w) * 0.5f + w, s.b };

    r.FillRoundRect({ body.l, body.t, body.r, body.b }, 1.5f, c.alpha(c.a * 0.55f));
    // Folded corner, drawn in the background colour's stead as a lighter wedge.
    r.FillTriangle({ body.r - fold, body.t }, { body.r, body.t }, { body.r, body.t + fold },
                   c);
}

void Drive(Renderer& r, const RectF& box, const Color& c) {
    const RectF s = Square(box, 0.72f);
    const float h = s.h() * 0.62f;
    const RectF body = { s.l, s.t + (s.h() - h) * 0.5f, s.r, s.t + (s.h() - h) * 0.5f + h };
    r.FillRoundRect(body, 2.0f, c.alpha(c.a * 0.55f));
    const float dot = body.h() * 0.22f;
    r.FillRoundRect({ body.r - dot * 2.2f, body.center().y - dot * 0.5f, body.r - dot * 1.2f,
                      body.center().y + dot * 0.5f },
                    dot * 0.5f, c);
}

void Cloud(Renderer& r, const RectF& box, const Color& c) {
    const RectF s = Square(box, 0.80f);
    const float baseTop = s.t + s.h() * 0.52f;
    r.FillRoundRect({ s.l, baseTop, s.r, s.t + s.h() * 0.82f }, s.h() * 0.15f, c);
    r.FillRoundRect({ s.l + s.w() * 0.18f, s.t + s.h() * 0.26f, s.l + s.w() * 0.68f, baseTop + 2.0f },
                    s.h() * 0.20f, c);
}

void Star(Renderer& r, const RectF& box, const Color& c) {
    // A compact four-point star: two overlapping triangles read well at 12 px.
    const RectF s = Square(box, 0.70f);
    const PointF ctr = s.center();
    r.FillTriangle({ ctr.x, s.t }, { s.l, s.b }, { s.r, s.b }, c);
    r.FillTriangle({ ctr.x, s.b }, { s.l, s.t + s.h() * 0.35f }, { s.r, s.t + s.h() * 0.35f }, c);
}

void ChevronDown(Renderer& r, const RectF& box, const Color& c) {
    const RectF s = Square(box, 0.5f);
    r.FillTriangle({ s.l, s.t }, { s.r, s.t }, { s.center().x, s.b }, c);
}

void ChevronUp(Renderer& r, const RectF& box, const Color& c) {
    const RectF s = Square(box, 0.5f);
    r.FillTriangle({ s.l, s.b }, { s.r, s.b }, { s.center().x, s.t }, c);
}

void ChevronRight(Renderer& r, const RectF& box, const Color& c) {
    const RectF s = Square(box, 0.5f);
    r.FillTriangle({ s.l, s.t }, { s.r, s.center().y }, { s.l, s.b }, c);
}

void ChevronLeft(Renderer& r, const RectF& box, const Color& c) {
    const RectF s = Square(box, 0.5f);
    r.FillTriangle({ s.r, s.t }, { s.r, s.b }, { s.l, s.center().y }, c);
}

void Cross(Renderer& r, const RectF& box, const Color& c, float weight) {
    const RectF s = Square(box, 0.55f);
    r.DrawLine(s.l, s.t, s.r, s.b, c, weight);
    r.DrawLine(s.r, s.t, s.l, s.b, c, weight);
}

void Plus(Renderer& r, const RectF& box, const Color& c, float weight) {
    const RectF s = Square(box, 0.55f);
    const PointF ctr = s.center();
    r.DrawLine(s.l, ctr.y, s.r, ctr.y, c, weight);
    r.DrawLine(ctr.x, s.t, ctr.x, s.b, c, weight);
}

}  // namespace kite::ui::glyph
