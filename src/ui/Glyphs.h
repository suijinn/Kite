// Kite - the small set of icons the file list needs, drawn from primitives.
//
// Deliberately not shell icons: SHGetFileInfo hits the shell (and therefore
// cloud providers) on every row, which is exactly the startup and stall cost
// Kite exists to avoid. Real per-type icons can be added later on a background
// thread behind the same call sites.
#pragma once

#include "ui/Renderer.h"

namespace kite::ui::glyph {

void Folder(Renderer& r, const RectF& box, const Color& c);
void File(Renderer& r, const RectF& box, const Color& c);
void Drive(Renderer& r, const RectF& box, const Color& c);
void Cloud(Renderer& r, const RectF& box, const Color& c);
void Star(Renderer& r, const RectF& box, const Color& c);
void ChevronDown(Renderer& r, const RectF& box, const Color& c);
void ChevronUp(Renderer& r, const RectF& box, const Color& c);
void ChevronRight(Renderer& r, const RectF& box, const Color& c);
void Cross(Renderer& r, const RectF& box, const Color& c, float weight);
void Plus(Renderer& r, const RectF& box, const Color& c, float weight);

}  // namespace kite::ui::glyph
