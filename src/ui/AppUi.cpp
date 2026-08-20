#include "ui/AppUi.h"

#include <algorithm>
#include <cmath>

#include "core/base/Format.h"
#include "core/base/PathUtil.h"
#include "core/base/Version.h"
#include "core/fs/VirtualPath.h"
#include "core/input/Commands.h"
#include "ui/Glyphs.h"

namespace kite::ui {
namespace {

constexpr float kPad = 8.0f;
constexpr float kColExt = 58.0f;
constexpr float kColSize = 84.0f;
constexpr float kColDate = 124.0f;
constexpr float kScrollbarWidth = 10.0f;
// How wide a filtered chooser's panel is allowed to get (the bookmark list, the
// command palette). One number for both: they are the same screen with different
// rows, and a palette that changed size on the way to the bookmark list would
// move the field being typed into.
constexpr float kPickerMaxWidth = 860.0f;

std::string SortArrow(bool desc) { return desc ? "\xE2\x96\xBC" : "\xE2\x96\xB2"; }  // ▼ ▲

// Width of the cell an icon is drawn in. Tied to the row height rather than
// fixed, because that is what the shell icon is requested at (rowHeight * 0.8):
// a constant here would leave a 16 px icon marooned in a row twice that tall
// once the text size is turned up.
float IconCell(const Theme& theme) { return theme.rowHeight * 0.9f; }

}  // namespace

AppUi::AppUi(App& app) : app_(app) {}

void AppUi::Add(const RectF& r, Hit kind, int index, Pane* pane, SplitNode* node,
                std::string path) {
    regions_.push_back({ r, kind, pane, node, index, SidebarSection::Count, std::move(path) });
}

void AppUi::AddSidebar(const RectF& r, Hit kind, SidebarSection section, int index,
                       std::string path) {
    regions_.push_back({ r, kind, nullptr, nullptr, index, section, std::move(path) });
}

const AppUi::Region* AppUi::Pick(float x, float y) const {
    for (auto it = regions_.rbegin(); it != regions_.rend(); ++it) {
        if (it->rect.contains(x, y)) return &*it;
    }
    return nullptr;
}

// Whether a point is off the window altogether. The pointer keeps reporting
// while the button is held (the platform captures it), so this stays answerable
// out past the edge, where the numbers go negative or run past the surface.
//
// A frame that has never been painted has no size to compare against; say "in"
// there rather than treating the whole plane as outside.
bool AppUi::OutsideWindow(float x, float y) const {
    if (surface_.w <= 0.0f || surface_.h <= 0.0f) return false;
    return x < 0.0f || y < 0.0f || x >= surface_.w || y >= surface_.h;
}

// Is the pointer on this thing, as far as highlighting goes?
//
// Nothing is lit while something is being dragged: during a splitter or tab
// drag the pointer sweeps across rows it is not pointing at, and during a file
// drag from outside, the drop feedback is the answer to "where would this land"
// - a second highlight next to it only muddles it. A selection band is the same
// case seen from the other side: the rows it covers are already washed as
// selected, and lighting one more of them says nothing extra. Note the pending
// states are deliberately not included, so pressing on a row does not blink it.
bool AppUi::PointerOver(const RectF& box) const {
    if (!mouseInside_ || dropActive_) return false;
    if (drag_ == Drag::Splitter || drag_ == Drag::Tab || drag_ == Drag::Marquee ||
        drag_ == Drag::Sidebar || drag_ == Drag::Section || drag_ == Drag::Session) {
        return false;
    }
    return box.contains(mouseX_, mouseY_);
}

// As above, for everything that an overlay covers. The shortcut sheet and the
// key editor take every click, so nothing behind them may look pointable.
bool AppUi::Hovered(const RectF& box) const {
    if (app_.keyHelpVisible() || app_.keyEditor().visible() || app_.settingsEditor().visible() ||
        app_.placePicker().visible() || app_.commandPalette().visible()) {
        return false;
    }
    // The completion popup covers only part of the window, so it is not an
    // overlay in the sense above - but a row lit under it is just as unclickable
    // as one lit under the shortcut sheet.
    if (!completionRect_.empty() && completionRect_.contains(mouseX_, mouseY_)) return false;
    return PointerOver(box);
}

// Everything the tab bar registers, the tabs themselves included: the wheel is
// answering "the pointer is over the bar", and it is over the bar whether or not
// it happens to be over a tab.
bool AppUi::IsTabBarHit(Hit kind) {
    return kind == Hit::TabBar || kind == Hit::TabItem || kind == Hit::TabClose ||
           kind == Hit::TabAdd;
}

// ---------------------------------------------------------------------------
// Paint
// ---------------------------------------------------------------------------

void AppUi::Paint(Renderer& r) {
    regions_.clear();
    const Theme& th = app_.theme();
    const SizeF size = r.surfaceSize();
    const RectF full = { 0.0f, 0.0f, size.w, size.h };
    surface_ = size;

    r.FillRect(full, th.windowBg);

    RectF rest = full;

    // Laid out before it is painted: the bar is as tall as the number of rows
    // the chips wrapped into, and nothing below it can be placed until that is
    // known.
    const float sessionH = LayoutSessionBar(r, rest);
    const RectF sessionBar = { rest.l, rest.t, rest.r, rest.t + sessionH };
    PaintSessionBar(r, sessionBar);
    rest.t = sessionBar.b;

    const RectF statusBar = { rest.l, rest.b - th.statusBarHeight, rest.r, rest.b };
    rest.b = statusBar.t;

    completionRect_ = {};

    // Every field that edits one nameable thing is drawn on that thing: the path
    // in the breadcrumb bar (PaintPathBar), a name in its list row, a session
    // name in its chip (PaintList / PaintSessionBar). Nothing is reserved for
    // them here - the row they take over is already on screen, so opening one
    // moves nothing.
    //
    // What is left at the bottom is what has no single place to sit: the filter
    // is about the whole listing, and the delete confirmation is a question
    // rather than a name.
    if (app_.prompt().active() && !app_.prompt().isInline()) {
        const RectF promptBar = { rest.l, rest.b - (th.statusBarHeight + 6.0f), rest.r, rest.b };
        PaintPrompt(r, promptBar);
        rest.b = promptBar.t;
    }

    if (app_.sidebarVisible()) {
        const RectF side = { rest.l, rest.t, rest.l + th.sidebarWidth, rest.b };
        PaintSidebar(r, side);
        rest.l = side.r;
        r.FillRect({ rest.l, rest.t, rest.l + 1.0f, rest.b }, th.border);
        rest.l += 1.0f;
    }

    if (Session* s = app_.workspace().activeSession()) {
        PaintNode(r, s->root.get(), rest);
    }

    PaintStatusBar(r, statusBar);
    PaintCompletion(r);
    PaintDragOverlay(r);

    if (app_.keyHelpVisible()) {
        PaintKeyHelp(r, full);
    } else {
        // Every opening starts at the top. The sheet is read from the beginning,
        // and where it was left last time is not something anyone remembers.
        keyHelpScroll_ = 0;
    }
    if (app_.keyEditor().visible()) PaintKeySettings(r, full);
    if (app_.settingsEditor().visible()) PaintSettings(r, full);
    if (app_.placePicker().visible()) PaintPlaces(r, full);
    if (app_.commandPalette().visible()) PaintCommandPalette(r, full);
}

void AppUi::PaintDragOverlay(Renderer& r) {
    const Theme& th = app_.theme();

    // Where an external file drop would land.
    if (dropActive_ && !dropHighlight_.empty()) {
        r.FillRect(dropHighlight_, th.accent.alpha(0.18f));
        r.StrokeRect(dropHighlight_, th.accent, 2.0f);
    }

    // Where the dragged tab would be inserted.
    if (drag_ == Drag::Tab && !dropTabMarker_.empty()) {
        r.FillRect(dropTabMarker_, th.accent);
    }

    // The same caret for a sidebar item, laid across the row boundary rather
    // than down the side of a tab, and for a whole section on its block edge.
    if (drag_ == Drag::Sidebar && !dropSidebarMarker_.empty()) {
        r.FillRect(dropSidebarMarker_, th.accent);
    }
    if (drag_ == Drag::Section && !dropSectionMarker_.empty()) {
        r.FillRect(dropSectionMarker_, th.accent);
    }
    // And down the side of a session chip, the way a horizontal tab bar draws it:
    // the chips are ordered along the row they wrapped into.
    if (drag_ == Drag::Session && !dropSessionMarker_.empty()) {
        r.FillRect(dropSessionMarker_, th.accent);
    }

    // The selection band. Drawn last and clipped to its own list, so sweeping
    // past the edge of the pane does not paint over the bars or the neighbour.
    if (drag_ == Drag::Marquee && marqueePane_) {
        const Tab* tab = marqueePane_->activeTab();
        if (tab) {
            const RectF& body = marqueePane_->listArea;
            const float anchorY = body.t + marqueeAnchorY_ - tab->scroll;
            const RectF band =
                RectF{ std::min(marqueeAnchorX_, marqueeX_), std::min(anchorY, marqueeY_),
                       std::max(marqueeAnchorX_, marqueeX_), std::max(anchorY, marqueeY_) }
                    .intersect(body);
            if (!band.empty()) {
                r.FillRect(band, th.accent.alpha(0.16f));
                r.StrokeRect(band, th.accent.alpha(0.7f), 1.0f);
            }
        }
    }
}

// --- session bar ------------------------------------------------------------

// Where every session chip goes, and how tall the bar has to be to hold them.
//
// Chips wrap onto further rows rather than being cut off at the right edge. A
// session that cannot be seen cannot be clicked, and the bar exists to say what
// is open - one that silently stops listing after the sixth session is worse
// than no bar at all.
//
// Which chip holds the name being typed, if any. Asked by both the layout and
// the paint pass: sizing one chip to the field while drawing the field into
// another is how a name ends up clipped by the box that was measured for the old
// one. Renaming answers about the active session (App::ApplyPrompt reads
// activeSession()), so that is the chip that opens.
bool AppUi::SessionChipEditing(int index) const {
    return app_.prompt().kind == PromptKind::SessionName &&
           index == app_.workspace().active;
}

// Returns the height of the bar, in whole rows.
float AppUi::LayoutSessionBar(Renderer& r, const RectF& area) {
    const Theme& th = app_.theme();
    Workspace& ws = app_.workspace();

    sessionChips_.clear();
    sessionAdd_ = {};
    sessionBrand_ = {};

    const float rowH = th.sessionBarHeight;
    constexpr float kAddWidth = 22.0f;
    constexpr float kGap = 4.0f;

    // The build stamp keeps the top-right corner instead of trailing the last
    // chip: it is the line someone gets asked to read out, and a landmark that
    // moves down a row every time a session is added is not a landmark. Every
    // row wraps early by its width, not just the first - which row is on top
    // changes once the bar is scrolled. Dropped when the shortcut overlay is up:
    // that panel repeats the stamp in its own heading.
    const std::string brand = std::string("Kite ") + version::kDisplay;
    const float brandW = r.MeasureText(brand, FontRole::UiSmall) + kPad * 2.0f;
    const bool showBrand = !app_.keyHelpVisible() && (area.w() - brandW) > 240.0f;
    const float rowRight = area.r - kPad - (showBrand ? brandW : 0.0f);

    // First pass: which row each chip lands on, and where along it.
    struct Slot {
        int row;
        float x;
        float w;
    };
    std::vector<Slot> slots;
    slots.reserve(ws.sessions.size());

    int row = 0;
    float x = kPad;
    for (size_t i = 0; i < ws.sessions.size(); ++i) {
        // While the name is being typed the chip is sized to what is in the field,
        // not to the name on file - a chip that kept its old width would clip the
        // very text being entered. The slack is for the caret standing past the
        // last letter, and keeps an emptied field from collapsing to nothing.
        const bool editing = SessionChipEditing(static_cast<int>(i));
        const std::string label = std::to_string(i + 1) + "  " +
                                  (editing ? app_.prompt().text : ws.sessions[i]->name);
        float w = r.MeasureText(label, FontRole::UiSmall) + kPad * 2.0f;
        if (editing) w += 24.0f;
        // The first chip on a row is placed whatever its width: a name long
        // enough to overflow the window on its own still gets a row, and the
        // text is clipped, rather than the loop wrapping for ever.
        if (x > kPad && area.l + x + w > rowRight) {
            ++row;
            x = kPad;
        }
        slots.push_back({ row, x, w });
        x += w + kGap;
    }
    if (x > kPad && area.l + x + kAddWidth > rowRight) {
        ++row;
        x = kPad;
    }
    const int totalRows = row + 1;

    // The bar is a header, not a panel: past a quarter of the window it would be
    // eating the listing it is supposed to label. Beyond that the rows scroll,
    // and which rows are on screen is derived from the active session rather
    // than remembered - a scroll position of our own could only ever disagree
    // with the selection, the same reasoning as the completion popup's.
    const int maxRows = std::max(1, static_cast<int>((r.surfaceSize().h * 0.25f) / rowH));
    const int shownRows = std::min(totalRows, maxRows);
    int firstRow = 0;
    if (shownRows < totalRows) {
        const int activeRow = (ws.active >= 0 && ws.active < static_cast<int>(slots.size()))
                                  ? slots[static_cast<size_t>(ws.active)].row
                                  : 0;
        firstRow = std::clamp(activeRow - shownRows + 1, 0, totalRows - shownRows);
    }

    auto place = [&](int slotRow, float slotX, float w) {
        const float top = area.t + static_cast<float>(slotRow - firstRow) * rowH;
        return RectF{ area.l + slotX, top + 3.0f, area.l + slotX + w, top + rowH - 3.0f };
    };
    auto onScreen = [&](int slotRow) {
        return slotRow >= firstRow && slotRow < firstRow + shownRows;
    };

    for (size_t i = 0; i < slots.size(); ++i) {
        if (!onScreen(slots[i].row)) continue;
        sessionChips_.push_back({ place(slots[i].row, slots[i].x, slots[i].w), static_cast<int>(i) });
    }
    // The add button follows the last chip. If that row is one of the scrolled
    // ones it simply is not there; Cmd::NewSession still is.
    if (onScreen(row)) sessionAdd_ = place(row, x, kAddWidth);
    if (showBrand) {
        sessionBrand_ = { area.r - brandW, area.t, area.r - kPad, area.t + rowH };
    }
    return static_cast<float>(shownRows) * rowH;
}

void AppUi::PaintSessionBar(Renderer& r, const RectF& area) {
    const Theme& th = app_.theme();
    Workspace& ws = app_.workspace();

    r.FillRect(area, th.panelBg);
    r.FillRect({ area.l, area.b - 1.0f, area.r, area.b }, th.border);

    for (const Chip& chip : sessionChips_) {
        if (chip.index >= static_cast<int>(ws.sessions.size())) continue;
        const bool active = (chip.index == ws.active);
        const std::string number = std::to_string(chip.index + 1) + "  ";

        if (SessionChipEditing(chip.index)) {
            // The number stays: it is which Alt+<digit> comes here, and a chip
            // that shed its number while being typed into reads as a different
            // chip. Drawn after the field, which fills the whole box.
            const float numberW = r.MeasureText(number, FontRole::UiSmall);
            PaintInlineField(r, chip.box, FontRole::UiSmall, numberW);
            r.DrawText(number, chip.box.inset(3.0f, 0.0f), th.textDim, FontRole::UiSmall,
                       TextAlign::Left);
            continue;
        }

        const std::string label =
            number + ws.sessions[static_cast<size_t>(chip.index)]->name;

        if (active) r.FillRoundRect(chip.box, 4.0f, th.sessionActiveBg);
        if (Hovered(chip.box)) r.FillRoundRect(chip.box, 4.0f, th.rowHover);
        r.DrawText(label, chip.box, active ? th.text : th.textDim, FontRole::UiSmall,
                   TextAlign::Center);
        Add(chip.box, Hit::SessionChip, chip.index);
    }

    if (!sessionAdd_.empty()) {
        glyph::Plus(r, sessionAdd_, Hovered(sessionAdd_) ? th.text : th.textDim, 1.5f);
        Add(sessionAdd_, Hit::SessionAdd);
    }

    if (!sessionBrand_.empty()) {
        r.DrawText(std::string("Kite ") + version::kDisplay, sessionBrand_,
                   th.textDim.alpha(0.5f), FontRole::UiSmall, TextAlign::Right);
    }
}

// --- sidebar ----------------------------------------------------------------

void AppUi::PaintSidebar(Renderer& r, const RectF& area) {
    const Theme& th = app_.theme();
    const Strings& str = app_.strings();

    r.FillRect(area, th.panelBg);
    sidebarRect_ = area;
    // Clamped against the previous frame's height: a section folded away shrinks
    // the content under a scroll offset that was valid a moment ago.
    sidebarScroll_ = std::clamp(sidebarScroll_, 0.0f, std::max(0.0f, sidebarContent_ - area.h()));
    r.PushClip(area);

    const float rowH = th.rowHeight;
    const float iconCell = IconCell(th);
    const float top = area.t + 4.0f;
    float y = top - sidebarScroll_;
    const Tab* current = const_cast<App&>(app_).workspace().focusedTab();
    const std::string currentPath = current ? current->path : std::string();

    // A section is greyed out while it is the one being carried, heading and
    // rows alike: it is the whole block that moves, not the heading on its own.
    auto carrying = [&](SidebarSection id) {
        return drag_ == Drag::Section && dragSection_ == id;
    };

    // Returns whether the items under this heading are to be laid out at all.
    auto section = [&](const char* key, SidebarSection id) {
        const bool collapsed = app_.sidebarCollapsed(id);
        const RectF row = { area.l + 2.0f, y, area.r - 2.0f, y + rowH };
        if (row.b > area.t && row.t < area.b) {
            // A heading is a control here, so it lights like one. Without that,
            // the only hint that it folds is that the arrow moved after a click.
            if (Hovered(row)) r.FillRoundRect(row, 4.0f, th.rowHover);
            const Color headingColor = th.textDim.alpha(carrying(id) ? 0.35f : 0.75f);
            const RectF mark = { row.l + 4.0f, row.t, row.l + 4.0f + 12.0f, row.b };
            if (collapsed) {
                glyph::ChevronRight(r, mark, headingColor);
            } else {
                glyph::ChevronDown(r, mark, headingColor);
            }
            r.DrawText(str.Get(key), { mark.r + 4.0f, row.t, row.r - kPad, row.b }, headingColor,
                       FontRole::UiSmall, TextAlign::Left);
            AddSidebar(row, Hit::SidebarSectionHeader, id, 0);
        }
        y += rowH;
        return !collapsed;
    };

    auto item = [&](SidebarSection id, int index, const std::string& label,
                    const std::string& fullPath, int glyphKind) {
        const RectF row = { area.l + 2.0f, y, area.r - 2.0f, y + rowH };
        if (row.b > area.t && row.t < area.b) {
            const bool selected = !currentPath.empty() && currentPath == fullPath;
            if (selected) r.FillRoundRect(row, 4.0f, th.rowSelected);
            if (Hovered(row)) r.FillRoundRect(row, 4.0f, th.rowHover);

            const RectF icon = { row.l + 6.0f, row.t, row.l + 6.0f + iconCell, row.b };
            const Color iconColor = selected ? th.rowSelectedText : th.textFolder;
            // Every row asks the shell, bookmarks included. They used to keep a
            // star on the grounds that it says "you pinned this" - but that is
            // what the heading above them says, and the star said nothing about
            // *what* was pinned. The shell icon does, and it carries the overlay
            // with it: which bookmark is a synced OneDrive folder, which is under
            // version control, which is a network share nobody can reach. Those
            // are the questions a row of identical stars could not answer.
            const uint32_t shellIcon = app_.IconFor(fullPath);
            if (shellIcon) {
                r.DrawIcon(shellIcon, icon.inset(1.0f));
            } else {
                // Until it lands - and for good under [ui] shell_icons = false -
                // the drawn glyph holds the space, so rows never shift.
                switch (glyphKind) {
                    case 1: glyph::Drive(r, icon, iconColor); break;
                    case 3: glyph::Cloud(r, icon, iconColor); break;
                    default: glyph::Folder(r, icon, iconColor); break;
                }
            }
            // The row being carried keeps its place until the drop, but says so:
            // with the pointer several rows away, the marker alone does not tell
            // you what is being moved.
            const bool carried = (drag_ == Drag::Sidebar && dragSidebarSection_ == id &&
                                  dragSidebarIndex_ == index) ||
                                 carrying(id);
            const Color textColor = selected ? th.rowSelectedText : th.text;
            r.DrawText(label, { icon.r + 6.0f, row.t, row.r - 6.0f, row.b },
                       carried ? textColor.alpha(textColor.a * 0.45f) : textColor, FontRole::Ui,
                       TextAlign::Left);
            AddSidebar(row, Hit::SidebarItem, id, index, fullPath);
        }
        y += rowH;
    };

    // The order the three come in is the user's, so this is a loop over that
    // order rather than three calls in the order they were written.
    const std::vector<SidebarSection>& order = app_.sidebarSections();
    for (size_t s = 0; s < order.size(); ++s) {
        if (s > 0) y += 6.0f;
        switch (order[s]) {
            case SidebarSection::QuickAccess:
                if (section("ui.quick_access", SidebarSection::QuickAccess)) {
                    const std::vector<fs::Root>& quick = app_.quickAccess();
                    for (size_t i = 0; i < quick.size(); ++i) {
                        item(SidebarSection::QuickAccess, static_cast<int>(i), quick[i].label,
                             quick[i].path, 0);
                    }
                }
                break;

            case SidebarSection::Bookmarks:
                if (section("ui.bookmarks", SidebarSection::Bookmarks)) {
                    const std::vector<Bookmark>& marks =
                        const_cast<App&>(app_).workspace().bookmarks;
                    for (size_t i = 0; i < marks.size(); ++i) {
                        // A bookmark is a folder, so it falls back to one - the
                        // same placeholder quick access uses, for the same reason.
                        item(SidebarSection::Bookmarks, static_cast<int>(i), marks[i].name,
                             marks[i].path, 0);
                    }
                }
                break;

            case SidebarSection::Drives:
                if (section("ui.drives", SidebarSection::Drives)) {
                    const std::vector<fs::Root>& drives = app_.roots();
                    for (size_t i = 0; i < drives.size(); ++i) {
                        const fs::Root& d = drives[i];
                        const int kind = (d.kind == fs::RootKind::Cloud)     ? 3
                                         : (d.kind == fs::RootKind::Network) ? 0
                                                                             : 1;
                        item(SidebarSection::Drives, static_cast<int>(i), d.label, d.path, kind);

                        // A thin capacity bar under fixed drives; free space is
                        // the number people actually look for here.
                        if (d.totalBytes == 0) continue;
                        if (y - rowH > area.t && y < area.b) {
                            const RectF track = { area.l + 34.0f, y + 1.0f, area.r - 12.0f,
                                                  y + 4.0f };
                            r.FillRoundRect(track, 1.5f, th.border);
                            const float used =
                                1.0f - static_cast<float>(static_cast<double>(d.freeBytes) /
                                                          static_cast<double>(d.totalBytes));
                            const RectF fill = { track.l, track.t,
                                                 track.l + track.w() * std::clamp(used, 0.0f, 1.0f),
                                                 track.b };
                            r.FillRoundRect(fill, 1.5f, used > 0.9f ? th.textError : th.accent);
                        }
                        // Outside the visibility test: the bar takes its space
                        // whether or not it is on screen, or the rows below it
                        // shift as the list scrolls.
                        y += 7.0f;
                    }
                }
                break;

            default:
                break;
        }
    }

    sidebarContent_ = y + sidebarScroll_ - top;
    r.PopClip();
}

// --- split tree -------------------------------------------------------------

void AppUi::PaintNode(Renderer& r, SplitNode* node, const RectF& area) {
    if (!node) return;
    node->rect = area;

    if (node->leaf()) {
        node->splitterRect = {};
        PaintPane(r, node->pane.get(), area);
        return;
    }

    const Theme& th = app_.theme();
    const float sw = th.splitterWidth;

    if (node->kind == SplitNode::Kind::LeftRight) {
        const float split = area.l + std::round((area.w() - sw) * node->ratio);
        const RectF a = { area.l, area.t, split, area.b };
        const RectF divider = { split, area.t, split + sw, area.b };
        const RectF b = { divider.r, area.t, area.r, area.b };
        PaintNode(r, node->a.get(), a);
        PaintNode(r, node->b.get(), b);
        r.FillRect(divider, th.border);
        node->splitterRect = divider;
        Add(divider, Hit::Splitter, 0, nullptr, node);
    } else {
        const float split = area.t + std::round((area.h() - sw) * node->ratio);
        const RectF a = { area.l, area.t, area.r, split };
        const RectF divider = { area.l, split, area.r, split + sw };
        const RectF b = { area.l, divider.b, area.r, area.b };
        PaintNode(r, node->a.get(), a);
        PaintNode(r, node->b.get(), b);
        r.FillRect(divider, th.border);
        node->splitterRect = divider;
        Add(divider, Hit::Splitter, 0, nullptr, node);
    }
}

// Where the keyboard is, in one colour. Two separate questions get answered by
// it: which pane the keys go to, and whether they go to Kite at all - a ring
// that keeps its accent while another window is in front says the first and lies
// about the second.
Color AppUi::FocusColor(bool focused) const {
    const Theme& th = app_.theme();
    if (!focused) return th.border;
    return app_.windowActive() ? th.paneFocusBorder : th.paneFocusIdle;
}

void AppUi::PaintPane(Renderer& r, Pane* pane, const RectF& area) {
    if (!pane) return;
    const Theme& th = app_.theme();
    const bool focused = (app_.workspace().focusedPane() == pane);

    r.PushClip(area);
    r.FillRect(area, th.listBg);

    RectF rest = area;
    const TabLayout tabs = LayoutTabBar(*pane, area);
    // 縦置きのバーはペインの高さいっぱいを取り、パスバーはその右から始まる。
    // パスバーの下に潜り込ませると、タブの列だけが一覧より低い位置から始まって
    // ペインの左上に用途の無い角ができる。
    RectF tabBar;
    if (tabs.vertical) {
        tabBar = { rest.l, rest.t, rest.l + tabs.thickness, rest.b };
        rest.l = tabBar.r;
    } else {
        tabBar = { rest.l, rest.t, rest.r, rest.t + tabs.thickness };
        rest.t = tabBar.b;
    }
    PaintTabBar(r, pane, tabBar, focused, tabs);

    Tab* tab = pane->activeTab();
    const RectF pathBar = { rest.l, rest.t, rest.r, rest.t + th.pathBarHeight };
    PaintPathBar(r, pane, tab, pathBar, focused);
    rest.t = pathBar.b;

    PaintList(r, pane, tab, rest, focused);

    if (focused) {
        r.StrokeRect(area.inset(1.0f), FocusColor(true), 2.0f);
    } else {
        // The ring alone stops being findable once a session is split three or
        // four ways; laying the unfocused panes back a shade means the live one
        // is the bright one, which reads without looking for anything.
        r.FillRect(area, th.paneInactiveScrim);
    }
    r.PopClip();
}

// How the tabs of one pane fold into rows, and how thick that makes the bar.
//
// Laid out horizontally, tabs shrink until they hit a floor, and then wrap. Up
// to that floor the result is exactly the single row it always was; past it, the
// row that used to run off the right edge - taking every tab on it out of reach
// of the mouse - becomes a second row.
//
// The add button is why the span is measured against `avail` rather than the
// full width: a full row can never use more than `avail`, so the last row always
// keeps room for the plus at its end. The vertical bar reserves a row for the
// same reason and in the same way.
//
// Vertically there is nothing to shrink - a tab is one line of text tall - so the
// wrapping half does not apply, and a second column is not the answer either:
// every column costs the listing its width, which is the thing the window exists
// to show. One column that scrolls is what is left.
//
// Where it is scrolled to is the pane's to keep (`tabScroll`), because the wheel
// can move it. The active tab still has the last word, but only when it changes:
// switching tabs scrolls the new one into view, and between those moments the
// bar stays where the wheel left it. Pulling it back every frame instead would
// make the wheel do nothing at all - the tabs would spring back the moment the
// pointer stopped.
AppUi::TabLayout AppUi::LayoutTabBar(Pane& pane, const RectF& area) const {
    const Theme& th = app_.theme();
    constexpr float kMaxTabWidth = 190.0f;
    constexpr float kMinTabWidth = 70.0f;

    TabLayout out;
    out.vertical = (app_.tabBarPosition() == TabBarPosition::Left);
    const int count = static_cast<int>(pane.tabs.size());

    if (out.vertical) {
        out.perTab = th.tabBarHeight;
        // Never past half the pane: the same line the horizontal bar draws at
        // half the height, for the same reason.
        out.thickness = std::min(th.tabBarWidth, area.w() * 0.5f);
        out.perRow = 1;
        out.rows = std::max(1, count);
        const int fit = std::max(1, static_cast<int>((area.h() - out.perTab) / out.perTab));
        out.shownRows = std::min(out.rows, fit);
    } else {
        const float avail = std::max(kMinTabWidth, area.w() - 28.0f);
        out.perTab = count == 0 ? kMaxTabWidth
                                : std::clamp(avail / static_cast<float>(count), kMinTabWidth,
                                             kMaxTabWidth);
        out.perRow = std::max(1, static_cast<int>(avail / out.perTab));
        out.rows = std::max(1, (std::max(count, 1) + out.perRow - 1) / out.perRow);

        // Half the pane is the point where the tab bar stops labelling a listing
        // and starts replacing it.
        const int maxRows = std::max(1, static_cast<int>((area.h() * 0.5f) / th.tabBarHeight));
        out.shownRows = std::min(out.rows, maxRows);
        out.thickness = static_cast<float>(out.shownRows) * th.tabBarHeight;
    }

    // Switching tabs brings the new one into view; nothing else moves the bar on
    // its own. `tabScrollFor` is what separates the two - it says which active
    // tab the current position was already fitted to.
    const int maxFirst = std::max(0, out.rows - out.shownRows);
    if (pane.tabScrollFor != pane.active) {
        const int activeRow = std::clamp(pane.active, 0, std::max(0, count - 1)) / out.perRow;
        if (activeRow < pane.tabScroll) pane.tabScroll = activeRow;
        if (activeRow >= pane.tabScroll + out.shownRows) {
            pane.tabScroll = activeRow - out.shownRows + 1;
        }
        pane.tabScrollFor = pane.active;
    }
    // Closing tabs shortens the bar under a position that was valid for the
    // longer one, so this cannot be left to whoever moves the scroll.
    pane.tabScroll = std::clamp(pane.tabScroll, 0, maxFirst);
    out.firstRow = pane.tabScroll;

    // Written back for the wheel: it has to know how far the bar can go, and only
    // the layout knows how many rows there are and how many of them fit.
    pane.tabRows = out.rows;
    pane.tabRowsPerPage = out.shownRows;
    return out;
}

void AppUi::PaintTabBar(Renderer& r, Pane* pane, const RectF& area, bool focused,
                        const TabLayout& layout) {
    const Theme& th = app_.theme();
    r.FillRect(area, th.tabInactiveBg);
    // Registered first so the individual tabs, added below, win the hit test.
    Add(area, Hit::TabBar, 0, pane);

    // The two axes of the layout: how far one tab advances along its row, and how
    // far one row advances across the bar. Swapping the pair is the whole of the
    // difference between the two orientations - the indices below are the same.
    const float rowStep = layout.vertical ? layout.perTab : th.tabBarHeight;
    const float slotStep = layout.vertical ? layout.thickness : layout.perTab;
    float x = area.l;
    float rowTop = area.t;

    for (size_t i = 0; i < pane->tabs.size(); ++i) {
        const int row = static_cast<int>(i) / layout.perRow;
        if (row < layout.firstRow) continue;
        if (row >= layout.firstRow + layout.shownRows) break;

        const int column = static_cast<int>(i) % layout.perRow;
        rowTop = area.t + static_cast<float>(row - layout.firstRow) * rowStep;
        x = area.l + static_cast<float>(column) * slotStep;

        const bool active = (static_cast<int>(i) == pane->active);
        const RectF tabRect = { x, rowTop, x + slotStep, rowTop + rowStep };

        r.FillRect(tabRect, active ? th.tabActiveBg : th.tabInactiveBg);
        if (Hovered(tabRect)) r.FillRect(tabRect, th.rowHover);
        if (active) {
            // Lit only in the pane holding the keyboard: with every pane's active
            // tab wearing the accent, the accent stopped meaning anything. It
            // goes on the edge the tabs run along - the top of a row of them, the
            // near side of a column - so the mark reads as belonging to the
            // strip rather than pointing out of it.
            r.FillRect(layout.vertical
                           ? RectF{ tabRect.l, tabRect.t, tabRect.l + 2.0f, tabRect.b }
                           : RectF{ tabRect.l, tabRect.t, tabRect.r, tabRect.t + 2.0f },
                       FocusColor(focused));
        }
        // The hairline between neighbours, laid across whichever way they touch.
        r.FillRect(layout.vertical
                       ? RectF{ tabRect.l + 8.0f, tabRect.b - 1.0f, tabRect.r - 8.0f, tabRect.b }
                       : RectF{ tabRect.r - 1.0f, tabRect.t + 5.0f, tabRect.r, tabRect.b - 5.0f },
                   th.border);

        const RectF close = { tabRect.r - 20.0f, tabRect.t + 6.0f, tabRect.r - 6.0f,
                              tabRect.b - 6.0f };
        const RectF label = { tabRect.l + 10.0f, tabRect.t, close.l - 4.0f, tabRect.b };
        r.DrawText(app_.DisplayName(*pane->tabs[i]), label,
                   active ? th.tabActiveText : th.tabInactiveText, FontRole::Ui, TextAlign::Left);
        // The cross is drawn faint so a row of tabs does not read as a row of
        // buttons; under the pointer it has to be unambiguous, since this is
        // the one control here that destroys something.
        const bool overClose = Hovered(close);
        if (overClose) r.FillRoundRect(close.inset(-2.0f), 3.0f, th.rowHover);
        glyph::Cross(r, close,
                     overClose ? th.text
                     : active  ? th.tabActiveText.alpha(0.7f)
                               : th.tabInactiveText.alpha(0.5f),
                     1.2f);

        Add(tabRect, Hit::TabItem, static_cast<int>(i), pane);
        Add(close, Hit::TabClose, static_cast<int>(i), pane);
        x = tabRect.r;
    }

    // The plus follows the last tab - beside it in a row, below it in a column.
    // When the rows are scrolled and the last tab's row is not one of the visible
    // ones it is simply absent; Cmd::NewTab and the middle click on a folder both
    // still make tabs.
    const int lastRow = std::max(0, static_cast<int>(pane->tabs.size()) - 1) / layout.perRow;
    if (lastRow >= layout.firstRow && lastRow < layout.firstRow + layout.shownRows) {
        const float addLeft = layout.vertical ? area.l : x;
        const float addTop =
            (layout.vertical && !pane->tabs.empty()) ? rowTop + rowStep : rowTop;
        const RectF add = { addLeft + 2.0f, addTop + 4.0f, addLeft + 24.0f,
                            addTop + rowStep - 4.0f };
        if (add.r < area.r && add.b <= area.b) {
            glyph::Plus(r, add, Hovered(add) ? th.text : th.textDim, 1.4f);
            Add(add, Hit::TabAdd, 0, pane);
        }
    }

    if (layout.vertical) {
        // One line down the far side, where the bar meets the listing. The tabs
        // already carry the lines between themselves.
        r.FillRect({ area.r - 1.0f, area.t, area.r, area.b }, th.border);

        // And, when there are tabs it is not showing, the thumb that says so.
        // Without it the column looks the same whether it holds five tabs or
        // fifty: the wheel moves it, and nothing on screen says there was
        // anywhere to move to, or where in the strip the visible part sits.
        //
        // The thin kind, as in the shortcut editor's list rather than the wide
        // one beside a listing - it shares its lane with the tabs, and the close
        // cross ends 6 px in from the edge, which is what leaves room for it.
        if (layout.rows > layout.shownRows) {
            const RectF track = { area.r - 5.0f, area.t + 3.0f, area.r - 2.0f, area.b - 3.0f };
            const float ratio =
                static_cast<float>(layout.shownRows) / static_cast<float>(layout.rows);
            const float thumbH = std::min(track.h(), std::max(24.0f, track.h() * ratio));
            const float t = static_cast<float>(layout.firstRow) /
                            static_cast<float>(std::max(1, layout.rows - layout.shownRows));
            const float thumbTop = track.t + (track.h() - thumbH) * t;
            r.FillRoundRect({ track.l, thumbTop, track.r, thumbTop + thumbH }, 1.5f,
                            th.scrollThumb);
        }
    } else {
        // A line under every row, not just the bar: stacked rows of tabs run into
        // one another otherwise, and the row a tab belongs to is what says which
        // of its neighbours come before and after it.
        for (int row = 0; row < layout.shownRows; ++row) {
            const float bottom = area.t + static_cast<float>(row + 1) * rowStep;
            r.FillRect({ area.l, bottom - 1.0f, area.r, bottom }, th.border);
        }
    }
}

// The breadcrumbs, and the address bar they turn into.
//
// One bar, two states: the crumbs are the readable form of the path and the
// field is the writable one, so they take the same place rather than stacking
// two rows that say the same thing. Ctrl+L (or a click on the empty part of the
// bar) swaps one for the other, and because the bar was already there, nothing
// below it moves.
void AppUi::PaintPathBar(Renderer& r, Pane* pane, Tab* tab, const RectF& area, bool focused) {
    const Theme& th = app_.theme();

    // Only the focused pane can be the one being typed into - the prompt is a
    // single field, and it edits the focused tab's path.
    if (focused && app_.prompt().kind == PromptKind::Path) {
        r.FillRect(area, th.overlayBg);
        PaintPromptField(r, { area.l + kPad, area.t, area.r - kPad, area.b });
        // Accent while it holds the keyboard, which is what the colour means
        // everywhere else in Kite.
        r.FillRect({ area.l, area.b - 1.0f, area.r, area.b }, th.accent);
        LayoutCompletion(r, area);
        // Registered while editing too, so that a click landing on the field is
        // told apart from a click landing anywhere else - which puts it away.
        Add(area, Hit::AddressBar, 0, pane);
        return;
    }

    r.FillRect(area, th.tabActiveBg);
    if (!tab) return;

    // Registered first so the crumbs, added below, win the hit test: clicking a
    // crumb goes there, clicking the space after them starts editing.
    Add(area, Hit::AddressBar, 0, pane);

    // Breadcrumbs: split the path into cumulative prefixes. The walk goes
    // through vfs::ParentOf, so it does not stop at "C:\\" - above a drive is
    // "PC", which is a place the crumbs can now take you to.
    std::vector<std::pair<std::string, std::string>> crumbs;  // label, full path
    {
        std::string p = tab->path;
        while (!p.empty()) {
            std::string label;
            if (const char* key = vfs::LabelKey(p)) {
                label = app_.strings().Get(key);
            } else if (p == tab->path) {
                // The tab knows the name the listing brought back with it, which
                // is the only source for a nested namespace extension.
                label = app_.DisplayName(*tab);
            } else if (vfs::IsVirtual(p)) {
                label = vfs::TrailingName(p);
            } else {
                label = path::DisplayName(p);
            }
            crumbs.push_back({ std::move(label), p });
            const std::string up = vfs::ParentOf(p);
            if (up == p) break;
            p = up;
        }
        std::reverse(crumbs.begin(), crumbs.end());
    }

    float x = area.l + kPad;
    for (size_t i = 0; i < crumbs.size(); ++i) {
        const float w = r.MeasureText(crumbs[i].first, FontRole::Ui) + 10.0f;
        const RectF box = { x, area.t + 2.0f, x + w, area.b - 2.0f };
        if (box.r > area.r - 24.0f) {
            r.DrawText("...", { x, area.t, area.r - kPad, area.b }, th.textDim, FontRole::Ui,
                       TextAlign::Left);
            break;
        }
        const bool last = (i + 1 == crumbs.size());
        // Nothing else says a breadcrumb can be clicked.
        if (Hovered(box)) r.FillRoundRect(box.inset(0.0f, 1.0f), 3.0f, th.rowHover);
        r.DrawText(crumbs[i].first, box, last ? th.text : th.textDim, FontRole::Ui,
                   TextAlign::Center);
        Add(box, Hit::Crumb, 0, pane, nullptr, crumbs[i].second);
        x = box.r;

        if (!last) {
            const RectF sep = { x, area.t, x + 12.0f, area.b };
            glyph::ChevronRight(r, sep, th.textDim.alpha(0.6f));
            x = sep.r;
        }
    }

    if (const_cast<App&>(app_).HasBookmark(tab->path)) {
        glyph::Star(r, { area.r - 22.0f, area.t + 4.0f, area.r - 6.0f, area.b - 4.0f }, th.accent);
    }
    r.FillRect({ area.l, area.b - 1.0f, area.r, area.b }, th.border);
}

void AppUi::PaintList(Renderer& r, Pane* pane, Tab* tab, const RectF& area, bool focused) {
    const Theme& th = app_.theme();
    const Strings& str = app_.strings();

    // --- column header ---
    const RectF header = { area.l, area.t, area.r, area.t + th.headerHeight };
    r.FillRect(header, th.panelBg);
    r.FillRect({ header.l, header.b - 1.0f, header.r, header.b }, th.border);

    const bool wide = area.w() > 420.0f;
    const bool medium = area.w() > 300.0f;
    float right = header.r - (kScrollbarWidth + 2.0f);
    RectF colDate{}, colSize{}, colExt{};
    if (wide) {
        colDate = { right - kColDate, header.t, right, header.b };
        right = colDate.l;
    }
    if (medium) {
        colSize = { right - kColSize, header.t, right, header.b };
        right = colSize.l;
    }
    if (wide) {
        colExt = { right - kColExt, header.t, right, header.b };
        right = colExt.l;
    }
    const RectF colName = { header.l + kPad, header.t, right, header.b };

    auto headerCell = [&](const RectF& box, const char* key, SortKey key_id, TextAlign align) {
        if (box.w() <= 0.0f) return;
        const bool activeSort = tab && tab->view.sort == key_id;
        std::string label = str.Get(key);
        if (activeSort) label += " " + SortArrow(tab->view.sortDesc);
        r.DrawText(label, box.inset(4.0f, 0.0f), activeSort ? th.text : th.textDim,
                   FontRole::UiSmall, align);
        Add(box, Hit::ColumnHeader, static_cast<int>(key_id), pane);
    };
    headerCell(colName, "ui.name", SortKey::Name, TextAlign::Left);
    if (wide) headerCell(colExt, "ui.ext", SortKey::Ext, TextAlign::Left);
    if (medium) headerCell(colSize, "ui.size", SortKey::Size, TextAlign::Right);
    if (wide) headerCell(colDate, "ui.modified", SortKey::Date, TextAlign::Left);

    // --- rows ---
    const RectF body = { area.l, header.b, area.r, area.b };
    r.FillRect(body, th.listBg);
    Add(body, Hit::ListBackground, 0, pane);

    if (!tab) return;

    pane->listHeight = body.h();
    pane->rowHeight = th.rowHeight;
    pane->rowsPerPage = std::max(1, static_cast<int>(body.h() / th.rowHeight) - 1);
    pane->listArea = body;

    if (tab->loadToken != 0 && !tab->loaded) {
        r.DrawText(str.Get("ui.loading"), body.inset(kPad, 8.0f), th.textDim, FontRole::Ui,
                   TextAlign::Left);
        return;
    }
    if (tab->listing.status != fs::Status::Ok) {
        const char* key = "ui.err_generic";
        switch (tab->listing.status) {
            case fs::Status::NotFound: key = "ui.err_missing"; break;
            case fs::Status::AccessDenied: key = "ui.err_denied"; break;
            case fs::Status::Unavailable: key = "ui.err_unavailable"; break;
            default: break;
        }
        std::string message = str.Get(key);
        if (!tab->listing.message.empty()) message += "  (" + tab->listing.message + ")";
        r.DrawText(message, body.inset(kPad, 8.0f), th.textError, FontRole::Ui, TextAlign::Left);
        return;
    }
    // A name being created has no row of its own yet - the file does not exist
    // until Enter - so one is borrowed: an extra slot directly below the cursor,
    // pushing the rows under it down. Below rather than on top of the cursor so
    // that nothing already read moves, and next to the cursor rather than at
    // either end because that is where the eye already is. Where the finished
    // item actually lands is the sort order's answer, and cannot be known before
    // the name is typed.
    const PromptKind promptKind = app_.prompt().kind;
    const bool creating =
        focused && (promptKind == PromptKind::NewFolder || promptKind == PromptKind::NewFile);
    const int phantom =
        creating ? std::clamp(tab->cursor + 1, 0, static_cast<int>(tab->visible.size())) : -1;
    const bool renaming = focused && promptKind == PromptKind::Rename;

    if (tab->visible.empty() && phantom < 0) {
        r.DrawText(str.Get(tab->filter.empty() ? "ui.empty" : "ui.no_match"),
                   body.inset(kPad, 8.0f), th.textDim, FontRole::Ui, TextAlign::Left);
        return;
    }

    const float rowH = th.rowHeight;
    // Slots, not entries: the borrowed row takes up one of them, so everything
    // that measures the list in rows - the scroll range, which rows are on
    // screen, the striping - counts slots.
    const int slots = static_cast<int>(tab->visible.size()) + (phantom >= 0 ? 1 : 0);
    const float maxScroll = std::max(0.0f, static_cast<float>(slots) * rowH - body.h());
    tab->scroll = std::clamp(tab->scroll, 0.0f, maxScroll);

    // The borrowed row is one past the cursor, which can itself be the last row
    // in view - and a field nobody can see is worse than the bar at the bottom
    // it replaced. Scrolled here rather than when the prompt opens because only
    // this function knows how tall the list came out.
    if (phantom >= 0) {
        const float top = static_cast<float>(phantom) * rowH;
        if (top + rowH > tab->scroll + body.h()) tab->scroll = top + rowH - body.h();
        if (top < tab->scroll) tab->scroll = top;
        tab->scroll = std::clamp(tab->scroll, 0.0f, maxScroll);
    }

    const int first = std::max(0, static_cast<int>(tab->scroll / rowH));
    const int last = std::min(slots - 1, first + static_cast<int>(body.h() / rowH) + 1);

    // Asked once for the whole list: the last row's rectangle can hang below the
    // list, and the pointer being down there is not the pointer being on it.
    const bool pointerInList = Hovered(body);

    r.PushClip(body);
    for (int slot = first; slot <= last; ++slot) {
        const float top = body.t + static_cast<float>(slot) * rowH - tab->scroll;
        const RectF row = { body.l, top, body.r - kScrollbarWidth, top + rowH };

        if (slot == phantom) {
            // Striped along with the rest so the borrowed row reads as one of
            // them rather than as a panel dropped over the list.
            if (slot % 2 == 1) r.FillRect(row, th.listBgAlt);
            const RectF icon = { row.l + 6.0f, row.t, row.l + 6.0f + IconCell(th), row.b };
            // The glyph is what the label at the bottom used to say: a folder is
            // being made, or a file is. Nothing else on the row can tell them
            // apart while the name is still empty.
            if (promptKind == PromptKind::NewFolder) {
                glyph::Folder(r, icon, th.textFolder);
            } else {
                glyph::File(r, icon, th.text.alpha(0.85f));
            }
            PaintInlineField(r, { icon.r + 3.0f, row.t + 2.0f, colName.r, row.b - 2.0f });
            continue;
        }
        // Past the borrowed row every slot names the entry one before it.
        const int i = (phantom >= 0 && slot > phantom) ? slot - 1 : slot;

        const fs::Entry* entry = tab->EntryAt(i);
        const bool marked = entry && tab->marked[tab->visible[i]] != 0;
        const bool isCursor = (i == tab->cursor);

        if (slot % 2 == 1) r.FillRect(row, th.listBgAlt);
        if (marked) r.FillRect(row, th.rowSelected);
        // Over the selection rather than under it: a marked row still has to
        // answer "is this the one I am about to click".
        if (pointerInList && row.contains(mouseX_, mouseY_)) r.FillRect(row, th.rowHover);
        if (isCursor && focused) {
            // A wash under the outline, because a 1px line is easy to lose
            // against a striped row - but only while the window has the
            // keyboard, so a Kite sitting in the background never looks live.
            if (app_.windowActive()) {
                r.FillRect(row, th.accent.alpha(0.16f));
                r.StrokeRect({ row.l + 1.0f, row.t + 1.0f, row.r - 1.0f, row.b - 1.0f },
                             th.cursorBorder, 1.0f);
            } else {
                r.StrokeRect({ row.l + 1.0f, row.t + 1.0f, row.r - 1.0f, row.b - 1.0f },
                             th.paneFocusIdle, 1.0f);
            }
        } else if (isCursor) {
            // Same treatment as an inactive window, and for the same reason:
            // the cursor is here, the keys are not. It used to be a faint wash,
            // which now says something else - that the pointer is on this row.
            r.StrokeRect({ row.l + 1.0f, row.t + 1.0f, row.r - 1.0f, row.b - 1.0f },
                         th.paneFocusIdle, 1.0f);
        }

        const RectF icon = { row.l + 6.0f, row.t, row.l + 6.0f + IconCell(th), row.b };
        const RectF nameBox = { icon.r + 6.0f, row.t, colName.r, row.b };

        if (!entry) {
            // The ".." row. No shell icon is asked for: the arrow says "up" more
            // plainly than the parent folder's own icon would, and the row is a
            // move, not a thing that can be selected, renamed or dragged.
            glyph::ChevronUp(r, icon, th.textFolder);
            r.DrawText(str.Get("ui.parent_dir"), nameBox, th.textFolder, FontRole::Ui,
                       TextAlign::Left);
            if (medium && colSize.w() > 0.0f) {
                r.DrawText(str.Get("ui.dir_marker"), { colSize.l, row.t, colSize.r - 6.0f, row.b },
                           th.textDim, FontRole::UiSmall, TextAlign::Right);
            }
            Add(row, Hit::ListRow, i, pane);
            continue;
        }

        const fs::Entry& e = *entry;
        Color nameColor = marked ? th.rowSelectedText : (e.isDir() ? th.textFolder : th.text);
        const bool ghost = fs::Has(e.attrs, fs::Attr::Placeholder) ||
                           fs::Has(e.attrs, fs::Attr::Offline);
        if (e.isHidden()) nameColor = nameColor.alpha(0.55f);

        // Cut, and waiting for a paste. Explorer says this by fading the whole
        // row, and it is the only place the clipboard's contents are visible at
        // all - Ctrl+X changes nothing else on screen. A wash of the row would
        // have to compete with the selection and the cursor, both of which are
        // still legitimate answers about this same row.
        const std::string full = fs::EntryPath(tab->path, e);
        const float ink = app_.IsCut(full) ? 0.45f : 1.0f;
        if (ink < 1.0f) nameColor = nameColor.alpha(nameColor.a * ink);

        // The real icon arrives a frame or two later, and carries any overlay
        // (version control, cloud sync) with it. Until then the drawn glyph
        // holds the space, so rows never shift when it lands.
        const uint32_t shellIcon = app_.IconFor(full);
        if (shellIcon) {
            r.DrawIcon(shellIcon, icon.inset(1.0f), ink);
        } else if (ghost) {
            glyph::Cloud(r, icon, th.textDim.alpha(th.textDim.a * ink));
        } else if (e.isDir()) {
            glyph::Folder(r, icon, nameColor);
        } else {
            glyph::File(r, icon, nameColor.alpha(nameColor.a * 0.85f));
        }

        // The field replaces the name it is editing rather than covering it: two
        // spellings of the same name, one under the other, is what the bar at the
        // bottom looked like.
        const bool editingRow = renaming && isCursor;
        if (!editingRow) r.DrawText(e.name, nameBox, nameColor, FontRole::Ui, TextAlign::Left);

        // The other columns fade with the name. Leaving them bright would split
        // one row into two answers about whether it is going anywhere.
        Color detail = marked ? th.rowSelectedText : th.textDim;
        if (ink < 1.0f) detail = detail.alpha(detail.a * ink);
        if (wide && colExt.w() > 0.0f) {
            const std::string ext = e.isDir() ? std::string() : path::Extension(e.name);
            r.DrawText(ext, { colExt.l + 4.0f, row.t, colExt.r, row.b }, detail,
                       FontRole::UiSmall, TextAlign::Left);
        }
        if (medium && colSize.w() > 0.0f) {
            const std::string sizeText = e.isDir() ? str.Get("ui.dir_marker") : FormatSize(e.size);
            r.DrawText(sizeText, { colSize.l, row.t, colSize.r - 6.0f, row.b }, detail,
                       FontRole::UiSmall, TextAlign::Right);
        }
        if (wide && colDate.w() > 0.0f) {
            r.DrawText(FormatDateTime(e.mtime), { colDate.l + 6.0f, row.t, colDate.r, row.b },
                       detail, FontRole::UiSmall, TextAlign::Left);
        }

        Add(row, Hit::ListRow, i, pane);
        // After the row, so the field wins the hit test over it: Pick answers
        // from the back, and a click inside the field must not also move the
        // cursor - which is what decides who gets renamed.
        if (editingRow) {
            PaintInlineField(r, { nameBox.l - 3.0f, row.t + 2.0f, colName.r, row.b - 2.0f });
        }
    }
    r.PopClip();

    // A folder holding nothing still lists ".."; say so under it rather than
    // leaving the pane looking like it failed to load. Not while a name is being
    // typed: the borrowed row is sitting in exactly that space, and "this folder
    // is empty" is about to stop being true anyway.
    if (tab->ItemCount() == 0 && phantom < 0) {
        const float top = body.t + rowH;
        r.DrawText(str.Get(tab->filter.empty() ? "ui.empty" : "ui.no_match"),
                   { body.l + kPad, top + 8.0f, body.r - kPad, body.b }, th.textDim, FontRole::Ui,
                   TextAlign::Left);
    }

    // --- scrollbar ---
    if (maxScroll > 0.0f) {
        const RectF track = { body.r - kScrollbarWidth, body.t, body.r, body.b };
        const float ratio = body.h() / (static_cast<float>(slots) * rowH);
        const float thumbH = std::max(24.0f, track.h() * ratio);
        const float t = tab->scroll / maxScroll;
        const float thumbTop = track.t + (track.h() - thumbH) * t;
        r.FillRoundRect({ track.l + 2.0f, thumbTop, track.r - 2.0f, thumbTop + thumbH }, 3.0f,
                        th.scrollThumb);
    }
}

// --- status / prompt / help -------------------------------------------------

void AppUi::PaintStatusBar(Renderer& r, const RectF& area) {
    const Theme& th = app_.theme();
    const Strings& str = app_.strings();

    r.FillRect(area, th.panelBg);
    r.FillRect({ area.l, area.t, area.r, area.t + 1.0f }, th.border);

    Tab* tab = app_.workspace().focusedTab();
    std::string left;
    if (tab) {
        left = str.Format("ui.status_items", { std::to_string(tab->ItemCount()) });
        const int marked = tab->MarkedCount();
        if (marked > 0) {
            left += "   " + str.Format("ui.status_selected",
                                       { std::to_string(marked), FormatSize(tab->MarkedBytes()) });
        }
        if (!tab->filter.empty()) left += "   [" + tab->filter + "]";
    }
    r.DrawText(left, { area.l + kPad, area.t, area.r * 0.6f, area.b }, th.textDim, FontRole::UiSmall,
               TextAlign::Left);

    // A field drawn in place says what it is renaming by where it sits, but not
    // what it is being asked for - and an empty box on a borrowed row says least
    // of all. So the heading the bottom bar used to carry moves here, where it
    // costs the field no width. It outranks the cursor's name: that row is what
    // the field is already sitting on.
    std::string right;
    const Prompt& p = app_.prompt();
    if (p.isInline() && p.kind != PromptKind::Path && !p.labelKey.empty()) {
        right = str.Get(p.labelKey);
    } else if (!app_.statusMessage().empty() && !app_.statusExpired()) {
        right = app_.statusMessage();
    } else if (tab) {
        const fs::Entry* e = tab->CursorEntry();
        if (e) right = e->name;
    }
    r.DrawText(right, { area.r * 0.45f, area.t, area.r - kPad, area.b }, th.textDim,
               FontRole::UiSmall, TextAlign::Right);
}

// Text, selection and caret for one editable field.
//
// The font is a parameter because the field takes the place of whatever it is
// editing: a session chip is set in the small face, so a field measured in the
// normal one would size its chip to a width the text never fills.
void AppUi::PaintPromptField(Renderer& r, const RectF& field, FontRole role) {
    const Theme& th = app_.theme();
    const Prompt& p = app_.prompt();

    // The selection goes under the text rather than recolouring it: one
    // DrawText call cannot paint a run in two colours, and splitting the string
    // into three would measure each piece on its own - which drifts apart from
    // the whole once kerning is involved.
    if (p.hasSelection()) {
        const float from =
            r.MeasureText(std::string_view(p.text).substr(0, p.selBegin()), role);
        const float to = r.MeasureText(std::string_view(p.text).substr(0, p.selEnd()), role);
        r.FillRect({ field.l + from, field.t + 3.0f, field.l + to, field.b - 3.0f },
                   th.textSelection);
    }

    r.DrawText(p.text, field, th.text, role, TextAlign::Left);

    // The caret is the text colour, not the accent: it is a letter-shaped mark
    // in a run of letters, and every field on the desktop draws it that way.
    // The accent says where the keyboard is; the bar's own border already does.
    const float caretX =
        field.l + r.MeasureText(std::string_view(p.text).substr(0, p.caret), role);
    r.FillRect({ caretX, field.t + 4.0f, caretX + 1.5f, field.b - 4.0f }, th.text);
    caret_ = { caretX, field.t };
}

// A field drawn over the thing it is editing - a list row, or a session chip.
//
// The same vocabulary the bar at the bottom uses (panel fill, accent edge), just
// boxed instead of spanning the window: what is being renamed is said by where
// the box is, which is the whole reason for editing in place. The region is
// registered so that a click inside the field is told apart from a click
// anywhere else, which folds it away.
//
// `indent` leaves room at the left for something drawn inside the same box but
// outside the text - the session chip's number.
void AppUi::PaintInlineField(Renderer& r, const RectF& box, FontRole role, float indent) {
    const Theme& th = app_.theme();
    r.FillRect(box, th.overlayBg);
    r.StrokeRect(box, th.accent, 1.0f);
    PaintPromptField(r, { box.l + 3.0f + indent, box.t, box.r - 3.0f, box.b }, role);
    Add(box, Hit::PromptField);
}

void AppUi::PaintPrompt(Renderer& r, const RectF& area) {
    const Theme& th = app_.theme();
    const Strings& str = app_.strings();
    const Prompt& p = app_.prompt();

    r.FillRect(area, th.overlayBg);
    r.FillRect({ area.l, area.t, area.r, area.t + 1.0f }, th.accent);

    std::string label = p.isConfirm() ? str.Format(p.labelKey, { p.text }) : str.Get(p.labelKey);
    const float labelW = r.MeasureText(label, FontRole::Ui) + kPad;
    r.DrawText(label, { area.l + kPad, area.t, area.l + kPad + labelW, area.b },
               p.isConfirm() ? th.textError : th.textDim, FontRole::Ui, TextAlign::Left);

    if (!p.isConfirm()) {
        PaintPromptField(r, { area.l + kPad + labelW, area.t, area.r - kPad, area.b });
    }
}

// Measure the candidate popup, without drawing it.
//
// Drawing has to wait until every pane is down - the list is what it hangs over
// - but the rectangle is needed before the rows underneath are painted, or one
// of them lights up under a pointer that is really over the popup. Measuring
// here, from the bar the popup drops out of, puts it in hand before the list
// below it is drawn.
void AppUi::LayoutCompletion(Renderer& r, const RectF& promptArea) {
    const PathComplete& pc = app_.pathComplete();
    completionTop_ = 0;
    completionRows_ = 0;
    if (!pc.open() || pc.matches().empty()) return;

    const Theme& th = app_.theme();
    const float rowH = th.rowHeight;
    const int count = static_cast<int>(pc.matches().size());

    // The popup drops out of the bar and stops at the status bar - a folder with
    // 200 subfolders would otherwise cover the whole window. It may hang over a
    // pane below its own; that pane is painted after this one, so the rows it
    // covers still know not to light up.
    const float span = (r.surfaceSize().h - th.statusBarHeight) - promptArea.b;
    completionRows_ = std::clamp(std::min(count, 10), 0, std::max(0, static_cast<int>(span / rowH)));
    if (completionRows_ <= 0) return;

    // Scrolling is derived from the selection rather than kept as state: the
    // only way to move through this list is to move the selection.
    const int sel = pc.selected();
    if (sel >= completionRows_) completionTop_ = sel - completionRows_ + 1;
    completionTop_ = std::clamp(completionTop_, 0, std::max(0, count - completionRows_));

    float widest = 0.0f;
    for (int i = 0; i < completionRows_; ++i) {
        widest = std::max(widest, r.MeasureText(pc.TextAt(completionTop_ + i), FontRole::Ui));
    }
    const float width =
        std::clamp(widest + kPad * 4.0f, 260.0f, std::max(260.0f, promptArea.w() - kPad * 2.0f));

    const float height = rowH * static_cast<float>(completionRows_);
    completionRect_ = { promptArea.l + kPad, promptArea.b, promptArea.l + kPad + width,
                        promptArea.b + height };
}

void AppUi::PaintCompletion(Renderer& r) {
    if (completionRect_.empty() || completionRows_ <= 0) return;

    const Theme& th = app_.theme();
    const PathComplete& pc = app_.pathComplete();
    const float rowH = th.rowHeight;

    r.FillRect(completionRect_, th.overlayBg);
    r.StrokeRect(completionRect_, th.border, 1.0f);

    for (int i = 0; i < completionRows_; ++i) {
        const int index = completionTop_ + i;
        if (index >= static_cast<int>(pc.matches().size())) break;
        const RectF row = { completionRect_.l, completionRect_.t + rowH * static_cast<float>(i),
                            completionRect_.r, completionRect_.t + rowH * static_cast<float>(i + 1) };

        const bool selected = (index == pc.selected());
        if (selected) {
            r.FillRect(row, th.rowSelected);
        } else if (PointerOver(row)) {
            r.FillRect(row, th.rowHover);
        }
        // The whole path, not just the leaf: with ".." or forward slashes in
        // what was typed, the name alone does not say where it would land.
        r.DrawText(pc.TextAt(index), { row.l + kPad, row.t, row.r - kPad, row.b },
                   selected ? th.rowSelectedText : th.textFolder, FontRole::Ui, TextAlign::Left);
        Add(row, Hit::CompletionRow, index);
    }
}

void AppUi::PaintKeyHelp(Renderer& r, const RectF& area) {
    const Theme& th = app_.theme();
    const Strings& str = app_.strings();
    const KeyMap& km = app_.keys();

    r.FillRect(area, th.overlayScrim);

    const RectF panel = area.inset(std::max(24.0f, area.w() * 0.06f),
                                   std::max(24.0f, area.h() * 0.06f));
    r.FillRoundRect(panel, 8.0f, th.overlayBg);
    r.StrokeRect(panel, th.border, 1.0f);

    // Three pieces share this line, and each of them used to be handed a rect
    // reaching the far side of it. Wide enough, that reads as three columns;
    // narrow enough, they are drawn on top of each other and none of the three
    // can be read. Each gets only the room left over by the one before it, and
    // what does not fit is left out rather than piled on.
    const RectF titleBox = { panel.l + 20.0f, panel.t + 8.0f, panel.r - 20.0f, panel.t + 40.0f };
    const std::string title = str.Get("ui.key_help_title");
    const float titleW = r.MeasureText(title, FontRole::UiBold) + kPad * 2.0f;

    const std::string hint = str.Get("ui.key_help_hint");
    const float hintW = r.MeasureText(hint, FontRole::UiSmall);
    float rest = titleBox.r;
    if (titleW + hintW <= titleBox.w()) {
        r.DrawText(hint, { titleBox.l + titleW, titleBox.t, titleBox.r, titleBox.b }, th.textDim,
                   FontRole::UiSmall, TextAlign::Right);
        rest = titleBox.r - hintW - kPad;
    }

    // This panel covers the session bar, and with it the build stamp. It comes
    // along here because the overlay is the closest thing Kite has to an about
    // box - and it is the first thing dropped, being the one nobody came for.
    const std::string stamp = std::string("Kite ") + version::kDisplay;
    if (titleBox.l + titleW + r.MeasureText(stamp, FontRole::UiSmall) <= rest) {
        r.DrawText(stamp, { titleBox.l + titleW, titleBox.t, rest, titleBox.b },
                   th.textDim.alpha(0.7f), FontRole::UiSmall, TextAlign::Left);
    }
    r.DrawText(title, { titleBox.l, titleBox.t, titleBox.l + titleW, titleBox.b }, th.text,
               FontRole::UiBold, TextAlign::Left);

    // Never inverted, however little room the window leaves: the panel is a
    // fraction of a window that has no minimum size, and a clip whose bottom is
    // above its top is not a small clip.
    const RectF content = { panel.l + 20.0f, titleBox.b + 4.0f,
                            std::max(panel.l + 21.0f, panel.r - 20.0f),
                            std::max(titleBox.b + 5.0f, panel.b - 12.0f) };
    r.PushClip(content);

    // Flatten the command table into printable lines first, so column breaking
    // is a simple index calculation instead of interleaved bookkeeping.
    struct Line {
        bool header = false;
        bool spacer = false;
        std::string label;
        std::string chord;
    };
    std::vector<Line> lines;
    lines.reserve(AllCommands().size() + 24);

    CmdGroup lastGroup = CmdGroup::Count;
    for (const CommandInfo& info : AllCommands()) {
        if (info.group != lastGroup) {
            lastGroup = info.group;
            if (!lines.empty()) lines.push_back({ false, true, {}, {} });
            lines.push_back({ true, false, str.Get(GroupLabelKey(info.group)), {} });
        }
        // Every chord, not the first one: a command with two keys on it that
        // only ever printed one made the second impossible to check from here.
        lines.push_back({ false, false, str.Label(info.labelKey), km.ChordText(info.id) });
    }

    // The chord column is as wide as the widest line of chords actually needs,
    // and the number of columns follows from that. A fixed width was fine while
    // every cell held one chord; "Ctrl+Shift+Tab, Ctrl+PageUp" does not fit in
    // it, and a cheat sheet that cuts the answer in half is worse than no sheet.
    float widest = 0.0f;
    for (const Line& line : lines) {
        if (!line.chord.empty()) widest = std::max(widest, r.MeasureText(line.chord, FontRole::Mono));
    }
    // Past this, one long line would push the whole sheet down to two columns.
    widest = std::min(widest, 210.0f);

    const int columns = std::clamp(static_cast<int>(content.w() / (widest + 160.0f)), 1, 4);
    const float colW = content.w() / static_cast<float>(columns);

    // How tall the sheet has to be for a given set of lines, with the group
    // headings kept off the bottom of a column.
    const auto reflow = [&columns](std::vector<Line>& all) {
        int rows = (static_cast<int>(all.size()) + columns - 1) / columns;
        for (int i = rows - 1; i < static_cast<int>(all.size()); i += rows) {
            if (all[i].header) all.insert(all.begin() + i, { false, true, {}, {} });
        }
        return (static_cast<int>(all.size()) + columns - 1) / columns;
    };

    // Shrink the leading rather than scroll - but only down to where the glyphs
    // still clear each other. A small window used to drive this to a couple of
    // pixels, and the sheet became one solid block of overlapping text.
    const float minLine = r.LineHeight(FontRole::UiSmall) * 0.85f;
    int perColumn = reflow(lines);
    float lineH = std::clamp(content.h() / static_cast<float>(std::max(1, perColumn)), minLine,
                             18.0f);

    if (static_cast<float>(perColumn) * lineH > content.h()) {
        // Still over. The blank line between groups is the cheapest thing on the
        // sheet, so it goes before anything anybody came here to read does.
        lines.erase(std::remove_if(lines.begin(), lines.end(),
                                   [](const Line& l) { return l.spacer; }),
                    lines.end());
        perColumn = reflow(lines);
        lineH = std::clamp(content.h() / static_cast<float>(std::max(1, perColumn)), minLine,
                           18.0f);
    }

    // Whatever is left over is reachable with the wheel. Cutting it off silently
    // would be the sheet answering half the question and saying so nowhere.
    const int shownRows = std::max(1, static_cast<int>(content.h() / lineH));
    keyHelpScroll_ = std::clamp(keyHelpScroll_, 0, std::max(0, perColumn - shownRows));

    for (size_t i = 0; i < lines.size(); ++i) {
        const Line& line = lines[i];
        if (line.spacer) continue;

        const int column = std::min(columns - 1, static_cast<int>(i) / perColumn);
        const int rowInColumn = static_cast<int>(i) % perColumn - keyHelpScroll_;
        if (rowInColumn < 0 || rowInColumn >= shownRows) continue;

        const float x = content.l + static_cast<float>(column) * colW;
        const float y = content.t + static_cast<float>(rowInColumn) * lineH;
        const RectF row = { x, y, x + colW - 14.0f, y + lineH };

        if (line.header) {
            r.DrawText(line.label, row, th.accent, FontRole::UiBold, TextAlign::Left);
            continue;
        }
        const float chordW = std::min(widest + 2.0f, row.w() * 0.6f);
        r.DrawText(line.label, { row.l, row.t, row.r - chordW - 6.0f, row.b }, th.text,
                   FontRole::UiSmall, TextAlign::Left);
        r.DrawText(line.chord.empty() ? "-" : line.chord, { row.r - chordW, row.t, row.r, row.b },
                   line.chord.empty() ? th.textDim.alpha(0.4f) : th.textDim, FontRole::Mono,
                   TextAlign::Right);
    }

    // The same thin mark the vertical tab bar uses, in the gutter every column
    // already leaves at its right. Without it a sheet with rows below the fold
    // looks exactly like one showing everything.
    if (perColumn > shownRows) {
        const RectF track = { content.r - 3.0f, content.t, content.r, content.b };
        const float ratio = static_cast<float>(shownRows) / static_cast<float>(perColumn);
        const float thumbH = std::max(24.0f, track.h() * ratio);
        const float t = static_cast<float>(keyHelpScroll_) /
                        static_cast<float>(std::max(1, perColumn - shownRows));
        const float top = track.t + (track.h() - thumbH) * t;
        r.FillRoundRect({ track.l, top, track.r, top + thumbH }, 1.5f, th.scrollThumb);
    }
    r.PopClip();
}

void AppUi::PaintKeySettings(Renderer& r, const RectF& area) {
    const Theme& th = app_.theme();
    const Strings& str = app_.strings();
    KeyEditor& editor = app_.keyEditor();

    r.FillRect(area, th.overlayScrim);

    // One column of rows, so a wide window gets a centred panel rather than a
    // sheet of mostly empty space.
    const float width = std::clamp(area.w() - 48.0f, 160.0f, 640.0f);
    const float height = std::max(120.0f, area.h() - std::max(32.0f, area.h() * 0.08f) * 2.0f);
    const float left = std::round(area.center().x - width * 0.5f);
    const float top = std::round(area.center().y - height * 0.5f);
    const RectF panel = { left, top, left + width, top + height };

    r.FillRoundRect(panel, 8.0f, th.overlayBg);
    r.StrokeRect(panel, th.border, 1.0f);
    Add(panel, Hit::KeyPanel);

    // Title on the left, what is being searched for on the right. Both used to
    // own the whole line, which is fine until the window is narrow enough for
    // them to meet in the middle - so the second one only gets what the first
    // leaves, and drops out entirely rather than landing on top of it.
    const RectF titleBox = { panel.l + 20.0f, panel.t + 8.0f, panel.r - 20.0f, panel.t + 38.0f };
    const std::string title = str.Get("ui.key_settings_title");
    const float titleW = r.MeasureText(title, FontRole::UiBold) + kPad * 2.0f;
    r.DrawText(title, { titleBox.l, titleBox.t, titleBox.l + titleW, titleBox.b }, th.text,
               FontRole::UiBold, TextAlign::Left);

    const std::string search =
        editor.filter().empty() ? str.Get("ui.key_settings_search_hint")
                                : str.Format("ui.key_settings_filter", { editor.filter() });
    if (titleW + r.MeasureText(search, FontRole::UiSmall) <= titleBox.w()) {
        r.DrawText(search, { titleBox.l + titleW, titleBox.t, titleBox.r, titleBox.b },
                   editor.filter().empty() ? th.textDim.alpha(0.6f) : th.text, FontRole::UiSmall,
                   TextAlign::Right);
    }

    // The line under the title carries whatever the last action did - which
    // command lost a chord, above all. Silence there means nothing happened.
    const RectF messageBox = { panel.l + 20.0f, titleBox.b, panel.r - 20.0f, titleBox.b + 20.0f };
    if (editor.capturing()) {
        r.DrawText(str.Get("ui.key_settings_hint_capture"), messageBox, th.accent, FontRole::UiSmall,
                   TextAlign::Left);
    } else if (!editor.message().empty()) {
        r.DrawText(editor.message(), messageBox, th.text, FontRole::UiSmall, TextAlign::Left);
    } else {
        r.DrawText(str.Format("ui.key_settings_count",
                              { std::to_string(editor.commandCount()) }),
                   messageBox, th.textDim.alpha(0.7f), FontRole::UiSmall, TextAlign::Left);
    }

    const RectF footer = { panel.l + 20.0f, panel.b - 26.0f, panel.r - 20.0f, panel.b - 6.0f };
    r.DrawText(str.Get("ui.key_settings_hint"), footer, th.textDim, FontRole::UiSmall,
               TextAlign::Left);

    const RectF body = { panel.l + 12.0f, messageBox.b + 4.0f, panel.r - 12.0f, footer.t - 4.0f };
    r.FillRect({ body.l, body.t, body.r, body.t + 1.0f }, th.border);

    const float rowH = th.rowHeight;
    const int pageRows = std::max(1, static_cast<int>((body.h() - 4.0f) / rowH));
    // Told every frame: the panel is sized from the window, so the number of
    // rows a PageDown should cover is only known here.
    editor.SetPageRows(pageRows);

    const std::vector<KeyEditor::Row>& rows = editor.rows();
    const int first = editor.scroll();
    const int last = std::min(static_cast<int>(rows.size()) - 1, first + pageRows - 1);

    r.PushClip(body);
    for (int i = first; i <= last; ++i) {
        const KeyEditor::Row& row = rows[i];
        const float y = body.t + 3.0f + static_cast<float>(i - first) * rowH;
        const RectF box = { body.l + 4.0f, y, body.r - 6.0f, y + rowH };

        if (row.header) {
            r.DrawText(row.label, box.inset(6.0f, 0.0f), th.accent, FontRole::UiBold,
                       TextAlign::Left);
            continue;
        }

        const bool selected = (i == editor.cursor());
        if (selected) r.FillRoundRect(box, 4.0f, th.rowSelected);
        // PointerOver, not Hovered: this panel is the overlay Hovered() blocks.
        else if (PointerOver(box)) r.FillRoundRect(box, 4.0f, th.rowHover);

        // The mouse way to add a chord instead of replacing one. The row itself
        // and the double click are both "replace", so adding needs a target of
        // its own - and its space is held on every row rather than made when the
        // pointer arrives, or the chord beside it would shift as you cross it.
        const RectF add = { box.r - 26.0f, box.t + 5.0f, box.r - 10.0f, box.b - 5.0f };

        // Wide enough for two or three chords side by side. The labels are short
        // and the panel is not: what gets cut off here cannot be read, and a
        // shortcut nobody can read is one nobody can pick out and remove.
        const float chordW = std::min(240.0f, box.w() * 0.5f);
        r.DrawText(row.label, { box.l + 10.0f, box.t, add.l - chordW - 14.0f, box.b },
                   selected ? th.rowSelectedText : th.text, FontRole::Ui, TextAlign::Left);

        const bool capturingHere = selected && editor.capturing();
        const std::string chordText = capturingHere ? str.Get("ui.key_settings_capture")
                                      : row.chords.empty() ? str.Get("ui.key_settings_none")
                                                           : row.chords;
        const Color chordColor = capturingHere            ? th.accent
                                 : row.chords.empty()     ? th.textDim.alpha(0.45f)
                                 : selected               ? th.rowSelectedText
                                                          : th.textDim;
        const float chordRight = add.l - 6.0f;

        // One chord of several, marked out inside the line rather than drawn as
        // its own control: the mark goes under the text, the way the selection in
        // an input field does, so the row still reads as one answer. The pieces
        // are measured separately only to place it - the font is monospaced here,
        // so the parts do add up to the whole.
        std::vector<RectF> chips;
        if (!capturingHere && selected && row.chordTexts.size() > 1) {
            const float sep = r.MeasureText(", ", FontRole::Mono);
            float x = chordRight - r.MeasureText(row.chords, FontRole::Mono);
            for (size_t c = 0; c < row.chordTexts.size(); ++c) {
                const float w = r.MeasureText(row.chordTexts[c], FontRole::Mono);
                const RectF chip = { x - 3.0f, box.t + 4.0f, x + w + 3.0f, box.b - 4.0f };
                if (static_cast<int>(c) == editor.chordCursor()) {
                    r.FillRoundRect(chip, 3.0f, th.accent.alpha(0.28f));
                } else if (PointerOver(chip)) {
                    r.FillRoundRect(chip, 3.0f, th.rowHover);
                }
                chips.push_back(chip);
                x += w + sep;
            }
        }

        r.DrawText(chordText, { chordRight - chordW, box.t, chordRight, box.b }, chordColor,
                   FontRole::Mono, TextAlign::Right);

        Add(box, Hit::KeyRow, i);
        // After the row, so the pointer finds the chord rather than the line it
        // sits on: Pick answers with the last thing registered.
        for (size_t c = 0; c < chips.size(); ++c) Add(chips[c], Hit::KeyChord, static_cast<int>(c));

        // Drawn faint on the row being pointed at or selected, the way the tab
        // cross is: a plus on all forty rows at once reads as a column of
        // buttons rather than a list of shortcuts. While the row is waiting for
        // a key it is gone - there is nothing to add to a capture in progress.
        if (!capturingHere && (selected || PointerOver(box))) {
            const bool overAdd = PointerOver(add);
            if (overAdd) r.FillRoundRect(add.inset(-2.0f), 3.0f, th.rowHover);
            glyph::Plus(r, add,
                        overAdd    ? th.text
                        : selected ? th.rowSelectedText.alpha(0.7f)
                                   : th.textDim.alpha(0.55f),
                        1.2f);
            Add(add, Hit::KeyAdd, i);
        }
    }
    r.PopClip();

    if (static_cast<int>(rows.size()) > pageRows) {
        const RectF track = { body.r - 4.0f, body.t + 3.0f, body.r - 1.0f, body.b - 3.0f };
        const float ratio = static_cast<float>(pageRows) / static_cast<float>(rows.size());
        const float thumbH = std::max(24.0f, track.h() * ratio);
        const float t = static_cast<float>(first) /
                        static_cast<float>(std::max(1, static_cast<int>(rows.size()) - pageRows));
        const float thumbTop = track.t + (track.h() - thumbH) * t;
        r.FillRoundRect({ track.l, thumbTop, track.r, thumbTop + thumbH }, 1.5f, th.scrollThumb);
    }
}

// The settings screen.
//
// Same panel, same row rhythm and the same modal manners as the shortcut editor
// - the two are siblings, and one of them being a different kind of window would
// only be something else to learn. What differs is what a row holds: a value
// with an arrow on each side, rather than a key combination to capture.
void AppUi::PaintSettings(Renderer& r, const RectF& area) {
    const Theme& th = app_.theme();
    const Strings& str = app_.strings();
    SettingsEditor& editor = app_.settingsEditor();

    r.FillRect(area, th.overlayScrim);

    const std::vector<SettingsEditor::Row>& rows = editor.rows();
    const float rowH = th.rowHeight;
    const float chrome = 38.0f + 20.0f + 8.0f + 26.0f + 12.0f;  // title, note, rule, footer, pad
    const float wanted = chrome + rowH * static_cast<float>(rows.size());

    const float width = std::clamp(area.w() - 48.0f, 200.0f, 560.0f);
    const float height = std::min(wanted, std::max(120.0f, area.h() - 48.0f));
    const float left = std::round(area.center().x - width * 0.5f);
    const float top = std::round(area.center().y - height * 0.5f);
    const RectF panel = { left, top, left + width, top + height };

    r.FillRoundRect(panel, 8.0f, th.overlayBg);
    r.StrokeRect(panel, th.border, 1.0f);
    Add(panel, Hit::SettingsPanel);

    const RectF titleBox = { panel.l + 20.0f, panel.t + 8.0f, panel.r - 20.0f, panel.t + 38.0f };
    r.DrawText(str.Get("ui.settings_title"), titleBox, th.text, FontRole::UiBold, TextAlign::Left);

    // Where the answers end up. A settings screen that writes a file the user
    // may not have noticed should say which file, once, where it is read.
    const RectF noteBox = { panel.l + 20.0f, titleBox.b, panel.r - 20.0f, titleBox.b + 20.0f };
    r.DrawText(str.Get(app_.standalone() ? "ui.settings_no_save" : "ui.settings_file"), noteBox,
               th.textDim.alpha(0.7f), FontRole::UiSmall, TextAlign::Left);

    const RectF footer = { panel.l + 20.0f, panel.b - 26.0f, panel.r - 20.0f, panel.b - 6.0f };
    r.DrawText(str.Get("ui.settings_hint"), footer, th.textDim, FontRole::UiSmall, TextAlign::Left);

    const RectF body = { panel.l + 12.0f, noteBox.b + 4.0f, panel.r - 12.0f, footer.t - 4.0f };
    r.FillRect({ body.l, body.t, body.r, body.t + 1.0f }, th.border);

    r.PushClip(body);
    for (size_t i = 0; i < rows.size(); ++i) {
        const SettingsEditor::Row& row = rows[i];
        const float y = body.t + 3.0f + static_cast<float>(i) * rowH;
        const RectF box = { body.l + 4.0f, y, body.r - 6.0f, y + rowH };
        if (box.t >= body.b) break;

        if (row.header) {
            r.DrawText(row.label, box.inset(6.0f, 0.0f), th.accent, FontRole::UiBold,
                       TextAlign::Left);
            continue;
        }

        const bool selected = (static_cast<int>(i) == editor.cursor());
        if (selected) r.FillRoundRect(box, 4.0f, th.rowSelected);
        // PointerOver, not Hovered: this panel is the overlay Hovered() blocks.
        else if (PointerOver(box)) r.FillRoundRect(box, 4.0f, th.rowHover);

        const float valueW = std::min(230.0f, box.w() * 0.55f);
        const RectF valueBox = { box.r - valueW, box.t, box.r - 8.0f, box.b };
        const RectF prev = { valueBox.l, valueBox.t, valueBox.l + 14.0f, valueBox.b };
        const RectF next = { valueBox.r - 14.0f, valueBox.t, valueBox.r, valueBox.b };

        r.DrawText(row.label, { box.l + 10.0f, box.t, valueBox.l - 8.0f, box.b },
                   selected ? th.rowSelectedText : th.text, FontRole::Ui, TextAlign::Left);

        // The arrows are the whole affordance: without them a value on the right
        // reads as a report rather than as something this row can change.
        const Color edge = th.textDim.alpha(0.35f);
        glyph::ChevronLeft(r, prev, row.atFirst ? edge : th.textDim);
        glyph::ChevronRight(r, next, row.atLast ? edge : th.textDim);
        r.DrawText(row.value, { prev.r, box.t, next.l, box.b },
                   selected ? th.rowSelectedText : th.text, FontRole::Ui, TextAlign::Center);

        Add(box, Hit::SettingsRow, static_cast<int>(i));
        Add(prev, Hit::SettingsPrev, static_cast<int>(i));
        Add(next, Hit::SettingsNext, static_cast<int>(i));
    }
    r.PopClip();
}

// Clicks while the settings screen is up. Nothing behind it is reachable, the
// same as the shortcut editor.
bool AppUi::HandleSettingsClick(const MouseEvent& e) {
    const Region* region = Pick(e.x, e.y);
    if (region && (region->kind == Hit::SettingsRow || region->kind == Hit::SettingsPrev ||
                   region->kind == Hit::SettingsNext)) {
        SettingsEditor& editor = app_.settingsEditor();
        editor.SelectRow(region->index);
        if (e.button == 0) {
            // The arrows step; the rest of the row cycles. Pressing a row over
            // and over has to keep doing something, so that one wraps - the same
            // split the keyboard makes between the arrow keys and Enter.
            if (region->kind == Hit::SettingsPrev) {
                editor.Adjust(-1, app_.strings());
            } else if (region->kind == Hit::SettingsNext) {
                editor.Adjust(1, app_.strings());
            } else {
                editor.Adjust(1, app_.strings(), true);
            }
            app_.ApplyPendingSetting();
        }
        app_.host().Invalidate();
        return true;
    }
    if (region && region->kind == Hit::SettingsPanel) {
        app_.host().Invalidate();
        return true;
    }
    // Outside the panel: same as pressing Escape.
    app_.Execute(Cmd::ShowSettings);
    return true;
}

// One filtered chooser's worth of chrome: the panel, its title and count, the
// field being typed into, and the rect the rows go in.
//
// The places list (Ctrl+P) and the command palette (Ctrl+Shift+P) are the same
// screen with different rows, so the frame is drawn once here rather than twice.
// PickerList already shares the counting behind them, and the reason is the same
// one the tab bar has for sharing its wrapping between the two orientations: two
// copies of a rule mean one of them is always the stale one.
//
// Two things this frame decides, both learned from the palette:
//
//   * **The window alone decides the panel, so every chooser is the same size.**
//     Not the row count, filtered or otherwise. Sized to its rows, a panel crept
//     towards the centre of the window on every keystroke - and the two screens
//     ended up different heights, so choosing "bookmark.list" from the palette
//     moved the field out from under the fingers that had just typed into it. The
//     field is at one place in this app, and it stays there.
//   * **The filter is a field, not small print in the title row.** It is the one
//     thing on the panel being edited, so it gets the body font, a caret, and a
//     border in the accent colour - the same "the keyboard is here" the address
//     bar draws. The caret shows on an empty field too: nothing else says the
//     panel is waiting to be typed into.
AppUi::PickerFrame AppUi::PaintPickerFrame(Renderer& r, const RectF& area,
                                           const PickerChrome& chrome, Hit panelHit) {
    const Theme& th = app_.theme();
    const float rowH = th.rowHeight;
    // Taller than a list row: this one holds text being typed, and a row is the
    // height of text being read. Derived from the row height so the font scale
    // reaches it, the way it reaches everything else that holds letters.
    const float fieldH = std::max(30.0f, rowH * 1.6f);
    const float width = std::clamp(area.w() - 48.0f, 200.0f, kPickerMaxWidth);
    const float height = std::max(140.0f, area.h() - 48.0f);
    const float left = std::round(area.center().x - width * 0.5f);
    const float top = std::round(area.center().y - height * 0.5f);
    const RectF panel = { left, top, left + width, top + height };

    r.FillRoundRect(panel, 8.0f, th.overlayBg);
    r.StrokeRect(panel, th.border, 1.0f);
    Add(panel, panelHit);

    // Title on the left, how many of how many on the right - and the count only
    // gets what the title leaves, dropping out rather than landing on top of it
    // when the window is narrow. With a filter typed, the count is the only thing
    // that says the rest are still there.
    const RectF titleBox = { panel.l + 20.0f, panel.t + 8.0f, panel.r - 20.0f, panel.t + 38.0f };
    const float titleW = r.MeasureText(chrome.title, FontRole::UiBold) + kPad * 2.0f;
    r.DrawText(chrome.title, { titleBox.l, titleBox.t, titleBox.l + titleW, titleBox.b }, th.text,
               FontRole::UiBold, TextAlign::Left);
    if (titleW + r.MeasureText(chrome.count, FontRole::UiSmall) <= titleBox.w()) {
        r.DrawText(chrome.count, { titleBox.l + titleW, titleBox.t, titleBox.r, titleBox.b },
                   th.textDim.alpha(0.8f), FontRole::UiSmall, TextAlign::Right);
    }

    const RectF field = { panel.l + 16.0f, titleBox.b, panel.r - 16.0f, titleBox.b + fieldH };
    const RectF fieldBox = field.inset(0.0f, 3.0f);
    r.FillRoundRect(fieldBox, 4.0f, th.listBg);
    r.StrokeRect(fieldBox, th.accent, 1.0f);

    const RectF inner = fieldBox.inset(10.0f, 0.0f);
    if (chrome.filter.empty()) {
        // Past the caret, so the two do not sit on the same pixel.
        r.DrawText(chrome.placeholder, { inner.l + 12.0f, inner.t, inner.r, inner.b },
                   th.textDim.alpha(0.6f), FontRole::Ui, TextAlign::Left);
    } else {
        r.DrawText(chrome.filter, inner, th.text, FontRole::Ui, TextAlign::Left);
    }
    // The caret is the text colour, not the accent: it is a letter-shaped mark in
    // a run of letters, and the border already says where the keyboard is.
    const float caretX = inner.l + r.MeasureText(chrome.filter, FontRole::Ui) + 1.0f;
    r.FillRect({ caretX, fieldBox.t + 5.0f, caretX + 1.5f, fieldBox.b - 5.0f }, th.text);

    const RectF footer = { panel.l + 20.0f, panel.b - 26.0f, panel.r - 20.0f, panel.b - 6.0f };
    r.DrawText(chrome.hint, footer, th.textDim, FontRole::UiSmall, TextAlign::Left);

    const RectF body = { panel.l + 12.0f, field.b + 8.0f, panel.r - 12.0f, footer.t - 4.0f };
    r.FillRect({ body.l, body.t, body.r, body.t + 1.0f }, th.border);

    PickerFrame frame;
    frame.panel = panel;
    frame.body = body;
    frame.pageRows = std::max(1, static_cast<int>((body.h() - 4.0f) / rowH));
    return frame;
}

// The thin marker down the side of a chooser's rows. Cannot be grabbed, the same
// as the listing's own bar (ROADMAP P3-11 makes both draggable at once).
void AppUi::PaintPickerScrollbar(Renderer& r, const RectF& body, int rows, int pageRows,
                                 int first) {
    if (rows <= pageRows) return;
    const Theme& th = app_.theme();
    const RectF track = { body.r - 4.0f, body.t + 3.0f, body.r - 1.0f, body.b - 3.0f };
    const float ratio = static_cast<float>(pageRows) / static_cast<float>(rows);
    const float thumbH = std::max(24.0f, track.h() * ratio);
    const float t = static_cast<float>(first) / static_cast<float>(std::max(1, rows - pageRows));
    const float thumbTop = track.t + (track.h() - thumbH) * t;
    r.FillRoundRect({ track.l, thumbTop, track.r, thumbTop + thumbH }, 1.5f, th.scrollThumb);
}

// The list of places (Ctrl+P).
//
// A sibling of the shortcut editor, the settings screen and the command palette:
// same panel, same row rhythm, same modal manners. It exists because the numbered
// shortcuts stop at eight - past that, this is the only way to a bookmark or a tab
// without the mouse. A row holds a name, where it goes, which kind of place it is,
// and the number key that would have reached it.
void AppUi::PaintPlaces(Renderer& r, const RectF& area) {
    const Theme& th = app_.theme();
    const Strings& str = app_.strings();
    PlacePicker& picker = app_.placePicker();

    r.FillRect(area, th.overlayScrim);

    const std::vector<PlacePicker::Row>& rows = picker.rows();
    const float rowH = th.rowHeight;

    PickerChrome chrome;
    chrome.title = str.Get("ui.goto_title");
    chrome.count =
        str.Format("ui.goto_count",
                   { std::to_string(rows.size()) + " / " + std::to_string(picker.total()) });
    chrome.filter = picker.filter();
    chrome.placeholder = str.Get("ui.goto_search_hint");
    chrome.hint = str.Get("ui.goto_hint");
    const PickerFrame frame = PaintPickerFrame(r, area, chrome, Hit::PlacePanel);
    const RectF body = frame.body;
    // Told every frame: the panel is sized from the window, so the number of rows
    // a PageDown should cover is only known here.
    picker.SetPageRows(frame.pageRows);

    if (rows.empty()) {
        r.DrawText(str.Get("ui.goto_empty"), body.inset(10.0f, 8.0f),
                   th.textDim.alpha(0.7f), FontRole::Ui, TextAlign::Left);
        return;
    }

    const int first = picker.scroll();
    const int last = std::min(static_cast<int>(rows.size()) - 1, first + frame.pageRows - 1);

    // The kind column is measured over the rows about to be drawn, the way the
    // palette measures its own: a fixed width cuts the longest label in half.
    float kindCol = 0.0f;
    for (int i = first; i <= last; ++i) {
        kindCol = std::max(kindCol, r.MeasureText(rows[i].kindLabel, FontRole::UiSmall));
    }
    kindCol = std::min(kindCol, 140.0f);

    r.PushClip(body);
    for (int i = first; i <= last; ++i) {
        const PlacePicker::Row& row = rows[i];
        const float y = body.t + 3.0f + static_cast<float>(i - first) * rowH;
        const RectF box = { body.l + 4.0f, y, body.r - 6.0f, y + rowH };

        const bool selected = (i == picker.cursor());
        if (selected) r.FillRoundRect(box, 4.0f, th.rowSelected);
        // PointerOver, not Hovered: this panel is the overlay Hovered() blocks.
        else if (PointerOver(box)) r.FillRoundRect(box, 4.0f, th.rowHover);

        // The number key that also reaches this row, on the eight that have one.
        // A wide enough row only: on a narrow one the name is what was come for,
        // and the shortcut is the piece that can be looked up elsewhere.
        float right = box.r - 10.0f;
        if (!row.chords.empty() && box.w() > 300.0f) {
            const float w = std::min(150.0f, r.MeasureText(row.chords, FontRole::Mono));
            r.DrawText(row.chords, { right - w, box.t, right, box.b },
                       selected ? th.rowSelectedText : th.textDim, FontRole::Mono,
                       TextAlign::Right);
            right -= w + 12.0f;
        }

        // Bookmark or open tab. Without it the same folder can appear twice with
        // nothing to say why - and it is the tab rows that behave differently on
        // Enter, so the difference has to be visible before it is pressed.
        if (box.w() > 360.0f && right - box.l - kindCol > 140.0f) {
            r.DrawText(row.kindLabel, { right - kindCol, box.t, right, box.b },
                       th.textDim.alpha(selected ? 0.9f : 0.6f), FontRole::UiSmall,
                       TextAlign::Right);
            right -= kindCol + 12.0f;
        }

        const float nameLeft = box.l + 10.0f;
        const float nameW = std::min(r.MeasureText(row.name, FontRole::Ui) + 2.0f,
                                     std::max(0.0f, (right - nameLeft) * 0.6f));
        r.DrawText(row.name, { nameLeft, box.t, nameLeft + nameW, box.b },
                   selected ? th.rowSelectedText : th.text, FontRole::Ui, TextAlign::Left);

        // Only what the name left over, and gone entirely when that is too little
        // to read - two strings sharing one rectangle overlap, they do not shrink.
        // Right-aligned so a long path shows its tail, which is the half that says
        // which of two same-named folders this is.
        const RectF pathBox = { nameLeft + nameW + 10.0f, box.t, right, box.b };
        if (pathBox.w() >= 60.0f) {
            r.DrawText(row.path, pathBox, th.textDim.alpha(selected ? 0.9f : 0.7f),
                       FontRole::UiSmall, TextAlign::Right);
        }

        Add(box, Hit::PlaceRow, i);
    }
    r.PopClip();

    PaintPickerScrollbar(r, body, static_cast<int>(rows.size()), frame.pageRows, first);
}

// Clicks while the bookmark list is up. Nothing behind it is reachable, the same
// as the other two overlays.
bool AppUi::HandlePlaceClick(const MouseEvent& e) {
    const Region* region = Pick(e.x, e.y);
    if (region && region->kind == Hit::PlaceRow) {
        app_.placePicker().SelectRow(region->index);
        // One click goes, unlike the shortcut editor's rows: there is nothing here
        // for a first click to disambiguate, and someone who pressed a bookmark
        // has already decided. Ctrl reads as it does in the listing - a new tab.
        if (e.button == 0) app_.ChoosePlace((e.mods & kModCtrl) != 0);
        app_.host().Invalidate();
        return true;
    }
    if (region && region->kind == Hit::PlacePanel) {
        app_.host().Invalidate();
        return true;
    }
    // Outside the panel: same as pressing Escape.
    app_.Execute(Cmd::ShowPlaces);
    return true;
}

// The command palette.
//
// The bookmark list's sibling, drawn in the same frame (PaintPickerFrame): the
// field, the fixed height and the count all come from there. It exists because a
// key that has not been learned - or that a background app has taken - leaves a
// command unreachable, and the F1 sheet only reads them out.
//
// A row holds four things: what the command is called, the name it has in
// keys.ini, which group it belongs to, and the chords that also run it. All four
// are what the filter matches, which is the reason all four are on the row -
// filtering on something the screen never shows teaches nobody it can be typed.
void AppUi::PaintCommandPalette(Renderer& r, const RectF& area) {
    const Theme& th = app_.theme();
    const Strings& str = app_.strings();
    CommandPalette& palette = app_.commandPalette();

    r.FillRect(area, th.overlayScrim);

    const std::vector<CommandPalette::Row>& rows = palette.rows();
    const float rowH = th.rowHeight;

    PickerChrome chrome;
    chrome.title = str.Get("ui.command_palette_title");
    chrome.count = str.Format(
        "ui.command_palette_count",
        { std::to_string(rows.size()) + " / " + std::to_string(palette.total()) });
    chrome.filter = palette.filter();
    chrome.placeholder = str.Get("ui.command_palette_search_hint");
    chrome.hint = str.Get("ui.command_palette_hint");
    const PickerFrame frame = PaintPickerFrame(r, area, chrome, Hit::PalettePanel);
    const RectF body = frame.body;
    // Told every frame: the panel is sized from the window, so the number of rows
    // a PageDown should cover is only known here.
    palette.SetPageRows(frame.pageRows);

    if (rows.empty()) {
        r.DrawText(str.Get("ui.command_palette_empty"), body.inset(10.0f, 8.0f),
                   th.textDim.alpha(0.7f), FontRole::Ui, TextAlign::Left);
        return;
    }

    const int first = palette.scroll();
    const int last = std::min(static_cast<int>(rows.size()) - 1, first + frame.pageRows - 1);

    // The three right-hand columns are measured from the rows about to be drawn,
    // the way the F1 sheet measures its chord column: a fixed width either cuts
    // the longest answer in half or leaves a gap on every other row. Measured over
    // the visible rows only, so scrolling can settle the columns tighter.
    float chordCol = 0.0f;
    float nameCol = 0.0f;
    float groupCol = 0.0f;
    const std::string unbound = str.Get("ui.command_palette_unbound");
    for (int i = first; i <= last; ++i) {
        const CommandPalette::Row& row = rows[i];
        chordCol = std::max(chordCol, row.chords.empty()
                                          ? r.MeasureText(unbound, FontRole::UiSmall)
                                          : r.MeasureText(row.chords, FontRole::Mono));
        nameCol = std::max(nameCol, r.MeasureText(row.name, FontRole::Mono));
        groupCol = std::max(groupCol, r.MeasureText(row.group, FontRole::UiSmall));
    }
    chordCol = std::min(chordCol, 180.0f);
    nameCol = std::min(nameCol, 210.0f);
    groupCol = std::min(groupCol, 130.0f);

    r.PushClip(body);
    for (int i = first; i <= last; ++i) {
        const CommandPalette::Row& row = rows[i];
        const float y = body.t + 3.0f + static_cast<float>(i - first) * rowH;
        const RectF box = { body.l + 4.0f, y, body.r - 6.0f, y + rowH };

        const bool selected = (i == palette.cursor());
        if (selected) r.FillRoundRect(box, 4.0f, th.rowSelected);
        // PointerOver, not Hovered: this panel is the overlay Hovered() blocks.
        else if (PointerOver(box)) r.FillRoundRect(box, 4.0f, th.rowHover);

        const float labelLeft = box.l + 10.0f;
        // What is left for the label decides what the row can afford. Columns are
        // dropped, never stacked - two strings sharing one rectangle overlap, they
        // do not shrink. The order they go in is "furthest from why the row was
        // read": the group repeats down the whole list, the keys.ini name is a
        // second spelling of a label that is already there, and the chords - the
        // answer to "what runs this, and does anything?" - are the last to go.
        float right = box.r - 10.0f;
        const auto affords = [&](float column) { return right - labelLeft - column > 120.0f; };

        if (affords(chordCol)) {
            const bool bound = !row.chords.empty();
            const std::string& text = bound ? row.chords : unbound;
            const FontRole font = bound ? FontRole::Mono : FontRole::UiSmall;
            r.DrawText(text, { right - chordCol, box.t, right, box.b },
                       selected ? th.rowSelectedText : th.textDim.alpha(bound ? 1.0f : 0.6f), font,
                       TextAlign::Right);
            right -= chordCol + 14.0f;
        }
        if (affords(groupCol)) {
            r.DrawText(row.group, { right - groupCol, box.t, right, box.b },
                       th.textDim.alpha(selected ? 0.9f : 0.7f), FontRole::UiSmall,
                       TextAlign::Right);
            right -= groupCol + 14.0f;
        }
        if (affords(nameCol)) {
            // The name from keys.ini, in the mono font the chords use: it is a
            // spelling to be copied into a file, not prose. Dim, because the label
            // beside it is what the row is called.
            r.DrawText(row.name, { right - nameCol, box.t, right, box.b },
                       th.textDim.alpha(selected ? 0.9f : 0.8f), FontRole::Mono,
                       TextAlign::Right);
            right -= nameCol + 14.0f;
        }

        r.DrawText(row.label, { labelLeft, box.t, std::max(labelLeft + 1.0f, right), box.b },
                   selected ? th.rowSelectedText : th.text, FontRole::Ui, TextAlign::Left);

        Add(box, Hit::PaletteRow, i);
    }
    r.PopClip();

    PaintPickerScrollbar(r, body, static_cast<int>(rows.size()), frame.pageRows, first);
}

// Clicks while the palette is up. Nothing behind it is reachable, the same as the
// other three overlays.
bool AppUi::HandlePaletteClick(const MouseEvent& e) {
    const Region* region = Pick(e.x, e.y);
    if (region && region->kind == Hit::PaletteRow) {
        app_.commandPalette().SelectRow(region->index);
        // One click runs it, the way a bookmark row goes on one click: there is
        // nothing here for a first click to disambiguate, and someone who pressed
        // a command has already decided.
        if (e.button == 0) app_.RunPaletteCommand();
        app_.host().Invalidate();
        return true;
    }
    if (region && region->kind == Hit::PalettePanel) {
        app_.host().Invalidate();
        return true;
    }
    // Outside the panel: same as pressing Escape.
    app_.Execute(Cmd::ShowCommandPalette);
    return true;
}

// ---------------------------------------------------------------------------
// Mouse
// ---------------------------------------------------------------------------

void AppUi::ScrollPane(Pane* pane, float deltaPixels) {
    if (!pane) return;
    Tab* tab = pane->activeTab();
    if (!tab) return;
    tab->scroll += deltaPixels;
    const float maxScroll = std::max(
        0.0f, static_cast<float>(tab->visible.size()) * pane->rowHeight - pane->listHeight);
    tab->scroll = std::clamp(tab->scroll, 0.0f, maxScroll);
    app_.host().Invalidate();
}

bool AppUi::HandleListClick(const Region& region, const MouseEvent& e) {
    Pane* pane = region.pane;
    if (!pane) return false;
    app_.FocusPane(pane);

    Tab* tab = pane->activeTab();
    if (!tab) return false;
    const int index = region.index;
    if (index < 0 || index >= static_cast<int>(tab->visible.size())) return false;

    if (e.button == 0 && e.clicks >= 2) {
        tab->cursor = index;
        app_.ActivateEntry(index, (e.mods & kModCtrl) != 0);
        return true;
    }

    const int entry = tab->visible[index];
    if (entry == Tab::kParentRow) {
        // ".." cannot be marked, so no modifier adds anything here. An unmodified
        // click still drops the selection the way clicking any other row does:
        // otherwise the menu, or a drag started here, would act on files the
        // pointer has long since left.
        if ((e.mods & (kModCtrl | kModShift)) == 0) tab->ClearMarks();
        tab->cursor = index;
        tab->ResetAnchor();
        app_.EnsureCursorVisible();
        app_.host().Invalidate();
        return true;
    }
    if (e.mods & kModCtrl) {
        tab->marked[entry] = tab->marked[entry] ? 0 : 1;
        tab->cursor = index;
        tab->ResetAnchor();
    } else if (e.mods & kModShift) {
        tab->ExtendTo(index);
    } else {
        // A press on a row that is already marked leaves the marks alone,
        // whichever button it was: the right button is about to open a menu for
        // the whole selection, and the left one may be the start of a drag,
        // which carries the selection too. Dropping them here is what made a
        // multi-file drag arrive as a single file.
        //
        // The left button still owes an answer - a plain click on one of several
        // marked rows means "just this one" - so the answer waits for the
        // release that turns out not to be a drag.
        const bool keep = tab->marked[entry] != 0;
        if (!keep) {
            tab->ClearMarks();
        } else if (e.button == 0) {
            pendingUnmark_ = true;
        }
        tab->cursor = index;
        tab->ResetAnchor();
    }
    app_.EnsureCursorVisible();
    app_.host().Invalidate();
    return true;
}

// A press on the empty part of a list starts a selection band. Rows are not a
// starting point: pressing one is how a file is dragged out, and taking that
// away to gain a band would trade a daily operation for an occasional one.
// That is also what every file manager does, Explorer included, and it means
// the band is a tool for the space under a short listing - a long one is what
// Shift+click is for.
void AppUi::BeginMarquee(Pane* pane, const MouseEvent& e) {
    Tab* tab = pane ? pane->activeTab() : nullptr;
    if (!tab) return;

    // Ctrl adds to what is already selected, exactly as it does for a click.
    if ((e.mods & kModCtrl) == 0) tab->ClearMarks();

    // The strip beside the rows belongs to the scrollbar whenever one is drawn.
    // It is empty list space as far as hit testing goes, but sweeping a
    // selection out of the thumb is not what anyone reaching for it wants.
    const RectF& area = pane->listArea;
    const bool overScrollbar = e.x >= area.r - kScrollbarWidth &&
                               static_cast<float>(tab->visible.size()) * pane->rowHeight > area.h();
    if (overScrollbar) return;

    // No threshold to cross first, unlike a tab or a file drag: there is nothing
    // else a press out here could turn into, and the band catches no row until
    // it actually reaches one.
    drag_ = Drag::Marquee;
    marqueePane_ = pane;
    marqueeTab_ = tab;
    marqueeBase_ = tab->marked;
    marqueeAnchorX_ = e.x;
    marqueeAnchorY_ = (e.y - area.t) + tab->scroll;
    marqueeX_ = e.x;
    marqueeY_ = e.y;
}

// Which rows the band covers is answered by geometry every time, never
// accumulated: the marks are laid down again from the ones held when the sweep
// began, so pulling the band back releases exactly what it released, and marks
// set earlier with Space or Ctrl+click are not swept away with it.
void AppUi::UpdateMarquee(float x, float y) {
    Tab* tab = marqueePane_ ? marqueePane_->activeTab() : nullptr;
    // A listing that changed under the sweep (a watcher event, a tab switched
    // from the keyboard) makes the remembered marks meaningless; let go rather
    // than write them onto whatever is there now.
    if (!tab || tab != marqueeTab_ || tab->marked.size() != marqueeBase_.size()) {
        CancelDrag();
        app_.host().Invalidate();
        return;
    }

    marqueeX_ = x;
    marqueeY_ = y;

    const RectF& body = marqueePane_->listArea;
    const float rowH = std::max(1.0f, marqueePane_->rowHeight);
    const float contentY = (y - body.t) + tab->scroll;
    const float top = std::min(marqueeAnchorY_, contentY);
    const float bottom = std::max(marqueeAnchorY_, contentY);

    tab->marked = marqueeBase_;

    // Half-open bands: a row is caught when the band actually overlaps it, so a
    // press in the empty space followed by a twitch selects nothing.
    const int rows = static_cast<int>(tab->visible.size());
    const int firstRow = static_cast<int>(std::floor(top / rowH));
    const int lastRow = static_cast<int>(std::ceil(bottom / rowH)) - 1;
    if (rows > 0 && lastRow >= firstRow && lastRow >= 0 && firstRow < rows) {
        tab->MarkRange(firstRow, lastRow, true);
        // The cursor follows the moving end, so Shift+arrow afterwards carries
        // on from where the pointer stopped instead of jumping back.
        tab->cursor = std::clamp(static_cast<int>(std::floor(contentY / rowH)), 0, rows - 1);
        tab->ResetAnchor();
    }

    app_.host().Invalidate();
}

// Clicks while the shortcut editor is up. Nothing behind it is reachable: the
// panel is modal in the same sense the shell's own menu is.
bool AppUi::HandleKeySettingsClick(const MouseEvent& e) {
    const Region* region = Pick(e.x, e.y);
    if (region && region->kind == Hit::KeyAdd) {
        // One click, unlike the row: this control means one thing, so there is
        // nothing for a first click to disambiguate.
        if (e.button == 0) {
            app_.keyEditor().SelectRow(region->index);
            app_.keyEditor().BeginCapture(true);
        }
        app_.host().Invalidate();
        return true;
    }
    if (region && region->kind == Hit::KeyChord) {
        // First click points at one of the chords on the line, second one takes
        // it away - the row's own "select, then act" rhythm, and the reason a
        // stray click on a shortcut cannot delete it.
        if (e.button == 0) {
            if (region->index == app_.keyEditor().chordCursor()) {
                app_.RemoveKeyBinding(region->index);
            } else {
                app_.keyEditor().SelectChord(region->index, app_.strings());
            }
        }
        app_.host().Invalidate();
        return true;
    }
    if (region && region->kind == Hit::KeyRow) {
        app_.keyEditor().SelectRow(region->index);
        // The second click is the one that arms capture, so a single click can
        // still just move the selection around.
        if (e.button == 0 && e.clicks >= 2) app_.keyEditor().BeginCapture(false);
        app_.host().Invalidate();
        return true;
    }
    if (region && region->kind == Hit::KeyPanel) {
        app_.host().Invalidate();
        return true;
    }
    // Outside the panel: same as pressing Escape.
    app_.Execute(Cmd::ShowKeySettings);
    return true;
}

// Which pane and insertion slot a dragged tab would land in.
bool AppUi::ResolveTabDrop(float x, float y, Pane** outPane, int* outIndex) const {
    const Region* region = Pick(x, y);
    if (!region || !region->pane) return false;

    Pane* pane = region->pane;
    int index = static_cast<int>(pane->tabs.size());

    if (region->kind == Hit::TabItem || region->kind == Hit::TabClose) {
        index = region->index;
        // Past the midpoint means "after this tab" - along whichever axis the
        // tabs are ordered on.
        const bool vertical = (app_.tabBarPosition() == TabBarPosition::Left);
        const PointF middle = region->rect.center();
        if (vertical ? (y > middle.y) : (x > middle.x)) ++index;
    }
    *outPane = pane;
    *outIndex = std::clamp(index, 0, static_cast<int>(pane->tabs.size()));
    return true;
}

void AppUi::FinishTabDrag() {
    // Let go out past the edge: pull the tab into a window of its own. Decided
    // before anything else, because there is no pane under the pointer to fall
    // back on and the last one it passed over is not an answer.
    if (dropTabOutside_ && dragTabPane_ && dragTabIndex_ >= 0) {
        Pane* from = dragTabPane_;
        const int index = dragTabIndex_;
        // App may close the pane out from under us, so let go of it first.
        CancelDrag();
        app_.DetachTabToNewWindow(from, index);
        app_.host().Invalidate();
        return;
    }

    Session* session = app_.workspace().activeSession();
    if (!session || !dragTabPane_ || !dropTabPane_ || dragTabIndex_ < 0) {
        CancelDrag();
        return;
    }

    if (dropTabPane_ == dragTabPane_) {
        int target = dropTabIndex_;
        // Removing the tab first shifts everything after it down by one.
        if (target > dragTabIndex_) --target;
        dragTabPane_->ReorderTab(dragTabIndex_, target);
    } else {
        std::unique_ptr<Tab> moved = dragTabPane_->DetachTab(dragTabIndex_);
        if (moved) {
            Pane* destination = dropTabPane_;
            destination->AttachTab(std::move(moved), dropTabIndex_);
            // A pane with no tabs left has nothing to show; fold it away.
            if (dragTabPane_->empty()) session->ClosePane(dragTabPane_);
            dragTabPane_ = nullptr;
            app_.FocusPane(destination);
        }
    }
    CancelDrag();
    app_.host().Invalidate();
}

// Which slot in the session bar a carried chip is asking for, plus the boundary
// to draw the caret on.
//
// Only the chips themselves count. Every slot is still reachable - the halves of
// a chip are "before it" and "after it", so the two ends are the left half of the
// first and the right half of the last - and off the chips there is no answer to
// give: the bar wraps, so the empty space at the end of a row is as much "before
// the next row" as it is "after this one".
bool AppUi::ResolveSessionDrop(float x, float y, int* outIndex, RectF* outMarker) const {
    for (const Region& candidate : regions_) {
        if (candidate.kind != Hit::SessionChip) continue;
        if (!candidate.rect.contains(x, y)) continue;

        const bool after = x > candidate.rect.center().x;
        *outIndex = candidate.index + (after ? 1 : 0);
        const float edge = after ? candidate.rect.r : candidate.rect.l;
        *outMarker = { edge - 1.0f, candidate.rect.t, edge + 2.0f, candidate.rect.b };
        return true;
    }
    return false;
}

void AppUi::FinishSessionDrag() {
    if (dragSessionIndex_ >= 0 && dropSessionIndex_ >= 0) {
        int target = dropSessionIndex_;
        // Lifting the chip out first shifts everything after it down by one.
        if (target > dragSessionIndex_) --target;
        app_.MoveSession(dragSessionIndex_, target);
    }
    CancelDrag();
    app_.host().Invalidate();
}

// Which slot in the section being dragged the pointer is asking for, plus the
// boundary to draw the caret on. Only rows of that one section are considered:
// a bookmark has no meaning among the drives, and the sections are separately
// ordered lists rather than one list with headings in it.
bool AppUi::ResolveSidebarDrop(float x, float y, int* outIndex, RectF* outMarker) const {
    if (dragSidebarSection_ == SidebarSection::Count) return false;

    for (const Region& candidate : regions_) {
        if (candidate.kind != Hit::SidebarItem || candidate.section != dragSidebarSection_) continue;
        if (!candidate.rect.contains(x, y)) continue;

        // Past the midpoint means "after this one", which is also how both ends
        // are reached: the top half of the first row and the bottom half of the
        // last one. Nothing outside the section's own rows counts, so a drag
        // that wanders into the neighbouring section proposes nothing rather
        // than quietly landing back where it came from.
        const bool after = y > candidate.rect.center().y;
        *outIndex = candidate.index + (after ? 1 : 0);
        const float edge = after ? candidate.rect.b : candidate.rect.t;
        *outMarker = { candidate.rect.l, edge - 1.0f, candidate.rect.r, edge + 1.0f };
        return true;
    }
    return false;
}

void AppUi::FinishSidebarDrag() {
    if (dragSidebarSection_ != SidebarSection::Count && dragSidebarIndex_ >= 0 &&
        dropSidebarIndex_ >= 0) {
        int target = dropSidebarIndex_;
        // Lifting the item out first shifts everything after it down by one.
        if (target > dragSidebarIndex_) --target;
        app_.MoveSidebarItem(dragSidebarSection_, dragSidebarIndex_, target);
    }
    CancelDrag();
    app_.host().Invalidate();
}

// The rectangle a whole section occupies: its heading plus every row under it.
// Built from the regions the last frame laid down rather than remembered, so a
// folded section is simply its heading and nothing else.
RectF AppUi::SectionBlock(SidebarSection section) const {
    RectF block{};
    for (const Region& candidate : regions_) {
        const bool mine = (candidate.kind == Hit::SidebarSectionHeader ||
                           candidate.kind == Hit::SidebarItem) &&
                          candidate.section == section;
        if (!mine) continue;
        if (block.empty()) {
            block = candidate.rect;
        } else {
            block.l = std::min(block.l, candidate.rect.l);
            block.t = std::min(block.t, candidate.rect.t);
            block.r = std::max(block.r, candidate.rect.r);
            block.b = std::max(block.b, candidate.rect.b);
        }
    }
    return block;
}

// Which slot in the section order a carried heading is asking for. Measured
// against whole blocks, not the headings alone: with quick access open, its
// heading is nowhere near the middle of the space it takes up, and dropping
// "below the bookmarks" has to mean below the bookmarks' rows too.
bool AppUi::ResolveSectionDrop(float x, float y, int* outIndex, RectF* outMarker) const {
    if (!sidebarRect_.contains(x, y)) return false;

    const std::vector<SidebarSection>& order = app_.sidebarSections();
    for (size_t i = 0; i < order.size(); ++i) {
        const RectF block = SectionBlock(order[i]);
        if (block.empty() || y < block.t || y >= block.b) continue;

        const bool after = y > block.center().y;
        *outIndex = static_cast<int>(i) + (after ? 1 : 0);
        const float edge = after ? block.b : block.t;
        *outMarker = { sidebarRect_.l + 2.0f, edge - 1.0f, sidebarRect_.r - 2.0f, edge + 1.0f };
        return true;
    }
    return false;
}

void AppUi::FinishSectionDrag() {
    if (dragSectionIndex_ >= 0 && dropSectionIndex_ >= 0) {
        int target = dropSectionIndex_;
        // Lifting the section out first shifts everything after it up by one.
        if (target > dragSectionIndex_) --target;
        app_.MoveSidebarSection(dragSectionIndex_, target);
    }
    CancelDrag();
    app_.host().Invalidate();
}

void AppUi::CancelDrag() {
    drag_ = Drag::None;
    pendingUnmark_ = false;
    dragSessionIndex_ = -1;
    dropSessionIndex_ = -1;
    dropSessionMarker_ = {};
    dragSection_ = SidebarSection::Count;
    dragSectionIndex_ = -1;
    dropSectionIndex_ = -1;
    dropSectionMarker_ = {};
    dragSidebarSection_ = SidebarSection::Count;
    dragSidebarIndex_ = -1;
    dropSidebarIndex_ = -1;
    dropSidebarMarker_ = {};
    pendingSidebarPath_.clear();
    dragSplitter_ = nullptr;
    marqueePane_ = nullptr;
    marqueeTab_ = nullptr;
    marqueeBase_.clear();
    marqueeBase_.shrink_to_fit();
    dragTabPane_ = nullptr;
    dragTabIndex_ = -1;
    dropTabPane_ = nullptr;
    dropTabIndex_ = -1;
    dropTabMarker_ = {};
    dropTabOutside_ = false;
}

std::string AppUi::DropTargetAt(float x, float y) const {
    const Region* region = Pick(x, y);
    if (!region) return {};

    // The sidebar is a legitimate destination - dropping onto a bookmark or a
    // quick-access folder is often faster than navigating there.
    if (region->kind == Hit::SidebarItem) return region->path;
    if (region->kind == Hit::Crumb) return region->path;
    if (!region->pane) return {};

    const Tab* tab = region->pane->activeTab();
    if (!tab) return {};

    if (region->kind == Hit::ListRow) {
        // Dropping onto ".." moves things up a level - the one direction the
        // list itself cannot offer as a target.
        if (tab->IsParentRow(region->index)) return vfs::ParentOf(tab->path);
        // Only a folder swallows the drop; over a file it goes to the folder
        // being listed, which is what every file manager does.
        if (const fs::Entry* entry = tab->EntryAt(region->index)) {
            if (entry->isDir()) return fs::EntryPath(tab->path, *entry);
        }
    }
    return tab->path;
}

void AppUi::SetDropFeedback(float x, float y) {
    const Region* region = Pick(x, y);
    dropPath_ = DropTargetAt(x, y);
    dropActive_ = !dropPath_.empty();

    if (!dropActive_ || !region) {
        dropHighlight_ = {};
        return;
    }
    // Outline the row when the target is a specific folder, the whole list
    // otherwise, so the distinction is visible at a glance.
    const bool ontoRow = (region->kind == Hit::ListRow || region->kind == Hit::SidebarItem ||
                          region->kind == Hit::Crumb) &&
                         dropPath_ != (region->pane && region->pane->activeTab()
                                           ? region->pane->activeTab()->path
                                           : std::string());
    if (ontoRow) {
        dropHighlight_ = region->rect;
    } else if (region->pane) {
        if (SplitNode* leaf = app_.workspace().activeSession()
                                  ? app_.workspace().activeSession()->LeafOf(region->pane)
                                  : nullptr) {
            dropHighlight_ = leaf->rect.inset(1.0f);
        } else {
            dropHighlight_ = region->rect;
        }
    } else {
        dropHighlight_ = region->rect;
    }
}

void AppUi::ClearDropFeedback() {
    dropActive_ = false;
    dropHighlight_ = {};
    dropPath_.clear();
}

bool AppUi::OnMouse(const MouseEvent& e) {
    if (e.type == MouseEvent::Type::Leave) {
        // The pointer went to another window: whatever was lit under it is not
        // under anything any more.
        if (mouseInside_) {
            mouseInside_ = false;
            hoverKind_ = Hit::None;
            hoverRect_ = {};
            app_.host().Invalidate();
        }
        return false;
    }

    mouseX_ = e.x;
    mouseY_ = e.y;
    mouseInside_ = true;

    // Splitter dragging owns the mouse while active.
    if (drag_ == Drag::Splitter && dragSplitter_) {
        if (e.type == MouseEvent::Type::Move) {
            SplitNode* n = dragSplitter_;
            const RectF& box = n->rect;
            const float span = (n->kind == SplitNode::Kind::LeftRight) ? box.w() : box.h();
            const float delta = (n->kind == SplitNode::Kind::LeftRight) ? (e.x - dragOrigin_)
                                                                       : (e.y - dragOrigin_);
            if (span > 1.0f) {
                n->ratio = std::clamp(dragRatio_ + delta / span, 0.08f, 0.92f);
                app_.host().Invalidate();
            }
            return true;
        }
        if (e.type == MouseEvent::Type::Up) CancelDrag();
        return true;
    }

    const Region* region = Pick(e.x, e.y);

    if (e.type == MouseEvent::Type::Move) {
        constexpr float kDragThreshold = 6.0f;
        const bool leftHeld = (e.buttons & kButtonLeft) != 0;
        const float moved = std::abs(e.x - dragStartX_) + std::abs(e.y - dragStartY_);

        if (!leftHeld && drag_ != Drag::None) {
            // The button came up somewhere we did not see; do not get stuck.
            CancelDrag();
        } else if (drag_ == Drag::Marquee) {
            UpdateMarquee(e.x, e.y);
            return true;
        } else if (drag_ == Drag::PendingTab && moved > kDragThreshold) {
            drag_ = Drag::Tab;
        } else if (drag_ == Drag::PendingSidebar && moved > kDragThreshold) {
            drag_ = Drag::Sidebar;
        } else if (drag_ == Drag::PendingSection && moved > kDragThreshold) {
            drag_ = Drag::Section;
        } else if (drag_ == Drag::PendingSession && moved > kDragThreshold) {
            drag_ = Drag::Session;
        } else if (drag_ == Drag::PendingFile && moved > kDragThreshold) {
            // Hand off to the OS. BeginFileDrag blocks until the drag ends, so
            // clear our own state first.
            Tab* tab = app_.workspace().focusedTab();
            std::vector<std::string> paths = tab ? tab->SelectionPaths() : std::vector<std::string>{};
            CancelDrag();
            if (!paths.empty()) {
                app_.host().BeginFileDrag(paths);
                app_.host().Invalidate();
            }
            return true;
        }

        if (drag_ == Drag::Tab) {
            // Off the window entirely: the tab is asking for a window of its
            // own, so no slot in this one is being proposed.
            dropTabOutside_ = OutsideWindow(e.x, e.y);
            dropTabPane_ = nullptr;
            dropTabIndex_ = -1;
            dropTabMarker_ = {};

            Pane* pane = nullptr;
            int index = 0;
            if (!dropTabOutside_ && ResolveTabDrop(e.x, e.y, &pane, &index)) {
                dropTabPane_ = pane;
                dropTabIndex_ = index;
                // Draw the insertion caret on the boundary this slot means:
                // the leading edge of tab `index`, or the trailing edge of the
                // one before it when inserting at the end. Which edge that is
                // follows the bar's orientation.
                const bool vertical = (app_.tabBarPosition() == TabBarPosition::Left);
                for (const Region& candidate : regions_) {
                    if (candidate.kind != Hit::TabItem || candidate.pane != pane) continue;
                    const RectF& box = candidate.rect;
                    if (candidate.index == index) {
                        dropTabMarker_ = vertical
                                             ? RectF{ box.l, box.t - 1.0f, box.r, box.t + 2.0f }
                                             : RectF{ box.l - 1.0f, box.t, box.l + 2.0f, box.b };
                    } else if (candidate.index == index - 1 && dropTabMarker_.empty()) {
                        dropTabMarker_ = vertical
                                             ? RectF{ box.l, box.b - 2.0f, box.r, box.b + 1.0f }
                                             : RectF{ box.r - 2.0f, box.t, box.r + 1.0f, box.b };
                    }
                }
            }
            app_.host().Invalidate();
            return true;
        }

        if (drag_ == Drag::Session) {
            int index = -1;
            RectF marker{};
            if (ResolveSessionDrop(e.x, e.y, &index, &marker)) {
                dropSessionIndex_ = index;
                dropSessionMarker_ = marker;
            } else {
                // Off the chips - nothing is proposed, so letting go here leaves
                // the order alone.
                dropSessionIndex_ = -1;
                dropSessionMarker_ = {};
            }
            app_.host().Invalidate();
            return true;
        }

        if (drag_ == Drag::Section) {
            int index = -1;
            RectF marker{};
            if (ResolveSectionDrop(e.x, e.y, &index, &marker)) {
                dropSectionIndex_ = index;
                dropSectionMarker_ = marker;
            } else {
                dropSectionIndex_ = -1;
                dropSectionMarker_ = {};
            }
            app_.host().Invalidate();
            return true;
        }

        if (drag_ == Drag::Sidebar) {
            int index = -1;
            RectF marker{};
            if (ResolveSidebarDrop(e.x, e.y, &index, &marker)) {
                dropSidebarIndex_ = index;
                dropSidebarMarker_ = marker;
            } else {
                // Off the section - over a heading, another section, or the
                // list. Nothing is proposed, so letting go here changes nothing.
                dropSidebarIndex_ = -1;
                dropSidebarMarker_ = {};
            }
            app_.host().Invalidate();
            return true;
        }

        const int shape = (region && region->kind == Hit::Splitter)
                              ? (region->node->kind == SplitNode::Kind::LeftRight ? 2 : 3)
                              : 0;
        if (shape != cursorShape_) {
            cursorShape_ = shape;
            app_.host().SetCursorShape(shape);
        }

        // Repaint only when the pointer crossed into a different thing. Every
        // pixel of movement inside one row would otherwise redraw the window.
        // The rectangle is the identity here: two items never share one, and it
        // needs no ownership of whatever the region pointed at.
        const Hit kind = region ? region->kind : Hit::None;
        const RectF rect = region ? region->rect : RectF{};
        if (kind != hoverKind_ || rect.l != hoverRect_.l || rect.t != hoverRect_.t ||
            rect.r != hoverRect_.r || rect.b != hoverRect_.b) {
            hoverKind_ = kind;
            hoverRect_ = rect;
            app_.host().Invalidate();
        }
        return false;
    }

    if (e.type == MouseEvent::Type::Up) {
        if (drag_ == Drag::Tab) {
            FinishTabDrag();
            return true;
        }
        if (drag_ == Drag::Sidebar) {
            FinishSidebarDrag();
            return true;
        }
        if (drag_ == Drag::Section) {
            FinishSectionDrag();
            return true;
        }
        if (drag_ == Drag::Session) {
            FinishSessionDrag();
            return true;
        }
        if (drag_ == Drag::PendingSidebar) {
            // Never moved far enough to be a drag, so it was a click after all.
            const std::string path = pendingSidebarPath_;
            const bool newTab = pendingSidebarNewTab_;
            CancelDrag();
            if (!path.empty()) app_.OpenPath(path, newTab);
            return true;
        }
        if (drag_ == Drag::PendingSection) {
            // Same: a heading that was pressed and let go is a fold, not a move.
            const SidebarSection section = dragSection_;
            CancelDrag();
            app_.ToggleSidebarSection(section);
            return true;
        }
        const bool wasMarquee = (drag_ == Drag::Marquee);
        // Never moved far enough to be a drag, so the press on an already
        // marked row was the plain click it looked like: it means that row and
        // nothing else.
        const bool unmark = (drag_ == Drag::PendingFile) && pendingUnmark_;
        CancelDrag();
        if (unmark) {
            if (Tab* t = app_.workspace().focusedTab()) t->ClearMarks();
            app_.host().Invalidate();
            return true;
        }
        if (wasMarquee) {
            app_.host().Invalidate();
            return true;
        }
        return false;
    }

    if (e.type == MouseEvent::Type::Wheel) {
        // The settings panel holds every row it has, so there is nothing to
        // scroll - but the list behind it must not scroll either.
        if (app_.settingsEditor().visible()) return true;
        if (app_.placePicker().visible()) {
            app_.placePicker().Scroll(static_cast<int>(-e.wheel * 3.0f));
            app_.host().Invalidate();
            return true;
        }
        if (app_.commandPalette().visible()) {
            app_.commandPalette().Scroll(static_cast<int>(-e.wheel * 3.0f));
            app_.host().Invalidate();
            return true;
        }
        if (app_.keyEditor().visible()) {
            app_.keyEditor().Scroll(static_cast<int>(-e.wheel * 3.0f));
            app_.host().Invalidate();
            return true;
        }
        if (app_.keyHelpVisible()) {
            // The way to the rows a small window pushed off the bottom. The upper
            // bound belongs to the paint, which is the only place that knows how
            // many rows a column ended up with.
            keyHelpScroll_ = std::max(0, keyHelpScroll_ - static_cast<int>(e.wheel * 3.0f));
            app_.host().Invalidate();
            return true;
        }
        if (region && sidebarRect_.contains(e.x, e.y)) {
            const float maxScroll = std::max(0.0f, sidebarContent_ - sidebarRect_.h());
            sidebarScroll_ = std::clamp(sidebarScroll_ - e.wheel * 60.0f, 0.0f, maxScroll);
            app_.host().Invalidate();
            return true;
        }
        // Over the tab bar, and the bar has rows it is not showing: the wheel
        // moves those into view rather than the listing behind it. Only then -
        // a bar with everything on screen has nothing to answer with, so the
        // wheel goes on doing what it has always done there and scrolls the list.
        if (region && region->pane && IsTabBarHit(region->kind) &&
            region->pane->tabRows > region->pane->tabRowsPerPage) {
            Pane& pane = *region->pane;
            // A notch is worth three tabs either way: a row of the vertical bar
            // holds one tab, a row of the horizontal bar holds a screenful.
            const int step = (app_.tabBarPosition() == TabBarPosition::Left) ? 3 : 1;
            const int wanted =
                pane.tabScroll - static_cast<int>(std::lround(e.wheel * static_cast<float>(step)));
            pane.tabScroll = std::clamp(wanted, 0, pane.tabRows - pane.tabRowsPerPage);
            // The wheel is an answer to "show me somewhere else", so it takes
            // over from the active tab until that changes again.
            pane.tabScrollFor = pane.active;
            app_.host().Invalidate();
            return true;
        }
        if (region && region->pane) {
            ScrollPane(region->pane, -e.wheel * app_.theme().rowHeight * 3.0f);
            return true;
        }
        return false;
    }

    if (e.type != MouseEvent::Type::Down) return false;

    if (app_.settingsEditor().visible()) return HandleSettingsClick(e);
    if (app_.keyEditor().visible()) return HandleKeySettingsClick(e);
    if (app_.placePicker().visible()) return HandlePlaceClick(e);
    if (app_.commandPalette().visible()) return HandlePaletteClick(e);

    if (app_.keyHelpVisible()) {
        app_.Execute(Cmd::ShowKeyHelp);
        return true;
    }

    // A press anywhere but the field itself puts an in-place field away. Clicking
    // elsewhere is already an answer to something else, and a half-typed name has
    // no business staying open across it. The click then goes on to do whatever it
    // was going to do - including opening the bar of another pane, which is why
    // this runs before the dispatch below rather than inside it.
    //
    // Explorer commits a rename on an outside click instead. Not here: the click
    // that would commit it usually lands on a row, and that same click moves the
    // cursor - which is what picks the file being renamed (App::ApplyPrompt reads
    // CursorEntry). Committing would rename on a mis-click, and rename the wrong
    // thing while doing it. Enter is the only word for yes.
    if (app_.prompt().isInline()) {
        const bool onField =
            region && (region->kind == Hit::PromptField ||
                       (region->kind == Hit::AddressBar &&
                        region->pane == app_.workspace().focusedPane()) ||
                       region->kind == Hit::CompletionRow);
        if (!onField) app_.CancelInlineEdit();
    }

    // Mouse back / forward buttons.
    if (e.button == 3) {
        app_.Execute(Cmd::GoBack);
        return true;
    }
    if (e.button == 4) {
        app_.Execute(Cmd::GoForward);
        return true;
    }

    if (!region) return false;

    switch (region->kind) {
        case Hit::SessionChip:
            // Not Cmd::Session1 + index: there are only eight of those, and now
            // that the bar wraps, the ninth chip is on screen and clickable.
            app_.GotoSession(region->index);
            // 名前の書かれたチップをダブルクリックすれば名前を変えられる、は
            // どの UI でも同じ読み方。キーボード側の和音（既定 Ctrl+Alt+R）は
            // 常駐ソフトに奪われていることがあり、そのとき唯一の道が消える。
            // 名前を変える先は「今アクティブなセッション」なので、1 度目の
            // クリックで既にそのチップが選ばれている順序に頼っている。
            if (e.button == 0 && e.clicks >= 2) {
                app_.Execute(Cmd::RenameSession);
                return true;
            }
            if (e.button == 0) {
                // Arm a possible reorder; it only becomes a drag once the pointer
                // actually moves. Not armed on the double click above: the field
                // is open on that chip now, and dragging the box being typed into
                // is not something anyone means.
                drag_ = Drag::PendingSession;
                dragSessionIndex_ = region->index;
                dropSessionIndex_ = -1;
                dropSessionMarker_ = {};
                dragStartX_ = e.x;
                dragStartY_ = e.y;
            }
            return true;
        case Hit::SessionAdd:
            app_.Execute(Cmd::NewSession);
            return true;

        case Hit::SidebarSectionHeader: {
            if (e.button != 0) return true;
            // Arm a possible section move. Folding happens on the release, so
            // that dragging a heading somewhere else does not also fold it on
            // the way out.
            const std::vector<SidebarSection>& order = app_.sidebarSections();
            const auto it = std::find(order.begin(), order.end(), region->section);
            drag_ = Drag::PendingSection;
            dragSection_ = region->section;
            dragSectionIndex_ =
                (it == order.end()) ? -1 : static_cast<int>(std::distance(order.begin(), it));
            dropSectionIndex_ = -1;
            dropSectionMarker_ = {};
            dragStartX_ = e.x;
            dragStartY_ = e.y;
            return true;
        }

        case Hit::SidebarItem: {
            const bool newTab = (e.mods & kModCtrl) != 0 || e.button == 2;
            if (e.button != 0) {
                // Only the left button can start a reorder, so the middle click
                // has nothing to wait for.
                app_.OpenPath(region->path, newTab);
                return true;
            }
            // Arm a possible reorder. The folder opens on the release, not here:
            // navigating on the press would mean every drag also walked away
            // from the folder on screen.
            drag_ = Drag::PendingSidebar;
            dragSidebarSection_ = region->section;
            dragSidebarIndex_ = region->index;
            dropSidebarIndex_ = -1;
            dropSidebarMarker_ = {};
            pendingSidebarPath_ = region->path;
            pendingSidebarNewTab_ = newTab;
            dragStartX_ = e.x;
            dragStartY_ = e.y;
            return true;
        }

        case Hit::TabBar:
            app_.FocusPane(region->pane);
            return true;

        case Hit::TabItem:
            app_.FocusPane(region->pane);
            if (e.button == 2) {
                std::string closed;
                if (region->pane->CloseTab(region->index, &closed)) {
                    app_.workspace().closedTabs.push_back(closed);
                }
            } else {
                // Through App rather than Pane::Activate: a tab that dropped its
                // listing while its session was in the background needs the
                // re-enumeration that comes with it.
                app_.GotoTab(region->index);
                if (e.button == 0) {
                    // Arm a possible reorder; it only becomes a drag once the
                    // pointer actually moves.
                    drag_ = Drag::PendingTab;
                    dragTabPane_ = region->pane;
                    dragTabIndex_ = region->index;
                    dragStartX_ = e.x;
                    dragStartY_ = e.y;
                }
            }
            app_.FocusPane(region->pane);
            return true;
        case Hit::TabClose: {
            app_.FocusPane(region->pane);
            std::string closed;
            if (region->pane->CloseTab(region->index, &closed)) {
                app_.workspace().closedTabs.push_back(closed);
            }
            app_.FocusPane(region->pane);
            return true;
        }
        case Hit::TabAdd:
            app_.FocusPane(region->pane);
            app_.Execute(Cmd::NewTab);
            return true;

        case Hit::Crumb:
            app_.FocusPane(region->pane);
            app_.OpenPath(region->path, (e.mods & kModCtrl) != 0);
            return true;

        case Hit::ColumnHeader: {
            app_.FocusPane(region->pane);
            static const Cmd kSortCommands[] = { Cmd::SortByName, Cmd::SortByExt, Cmd::SortBySize,
                                                 Cmd::SortByDate };
            if (region->index >= 0 && region->index < 4) app_.Execute(kSortCommands[region->index]);
            return true;
        }

        case Hit::AddressBar:
            // Inside the field being edited: leave it alone. (Another pane's bar
            // was already folded away above, and falls through to open there.)
            if (app_.prompt().kind == PromptKind::Path) return true;
            // Otherwise this is the space after the last crumb, and the click
            // opens that pane's path for editing - hence the focus first.
            app_.FocusPane(region->pane);
            if (e.button == 0) app_.Execute(Cmd::EditPath);
            return true;

        case Hit::PromptField:
            // Swallowed, and nothing more: the press has already been spared the
            // fold above, and moving the caret by clicking is something no field
            // in Kite does yet - the address bar included. Whenever it arrives it
            // belongs to all of them at once.
            return true;

        case Hit::CompletionRow:
            // Only the left button. A right-click here would otherwise fall
            // through to the list underneath, which is not what was aimed at.
            if (e.button == 0) app_.ChooseCompletion(region->index);
            return true;

        case Hit::Splitter:
            drag_ = Drag::Splitter;
            dragSplitter_ = region->node;
            dragRatio_ = region->node->ratio;
            dragOrigin_ = (region->node->kind == SplitNode::Kind::LeftRight) ? e.x : e.y;
            return true;

        case Hit::ListRow: {
            HandleListClick(*region, e);
            if (e.button == 0 && e.clicks == 1) {
                // Arm a possible file drag out of the selection.
                drag_ = Drag::PendingFile;
                dragStartX_ = e.x;
                dragStartY_ = e.y;
            }
            if (e.button == 1) {
                // Shift adds the extended verbs, as it does in Explorer. Those
                // are the entries the shell hides on purpose, and handlers fill
                // them with things that do not belong on a plain right-click.
                app_.ShowContextMenuAt(e.screenX, e.screenY, (e.mods & kModShift) != 0);
            }
            return true;
        }

        case Hit::ListBackground:
            app_.FocusPane(region->pane);
            if (e.button == 0 && e.clicks == 1) {
                // Clears the marks itself unless Ctrl is down, so a press here
                // still drops the selection even if the band catches nothing.
                BeginMarquee(region->pane, e);
            } else if (Tab* t = region->pane->activeTab()) {
                t->ClearMarks();
            }
            if (e.button == 1) {
                // The background menu, not whatever the cursor happens to be
                // parked on: the click was on the space inside the folder, which
                // is what Explorer answers with New and Paste. Shift asks for the
                // extended verbs here as it does on a row.
                app_.ShowBackgroundContextMenu(e.screenX, e.screenY,
                                               (e.mods & kModShift) != 0);
            } else if (e.button == 0 && e.clicks >= 2) {
                // Double-clicking past the last row goes up, the way it does in
                // every file manager that has ever offered it.
                app_.Execute(Cmd::GoUp);
            }
            app_.host().Invalidate();
            return true;

        default:
            break;
    }
    return false;
}

}  // namespace kite::ui
