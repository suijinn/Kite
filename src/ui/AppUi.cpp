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

constexpr float kColExt = 58.0f;
constexpr float kColSize = 84.0f;
constexpr float kColDate = 124.0f;

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
    // 決め直すのは毎フレーム。入力欄は開いたり閉じたりするので、前のフレームの
    // 位置が残っていると、閉じた欄のあった場所に IME の窓が出る。
    caretInField_ = false;
    listCaretValid_ = false;
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

    // 入力欄が 1 つも出ていないフレームの答えは、フォーカスされた一覧のカーソル行。
    // 型入力ジャンプ（ROADMAP P3-4）は名前を IME で打てるので、変換窓の行き先が
    // 要る ─ 前に開いていた欄の跡地に出すよりは、今まさに探している行の脇に出す。
    if (!caretInField_ && listCaretValid_) caret_ = listCaret_;
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
        PaintThinScrollbar(r, { area.r - 5.0f, area.t + 3.0f, area.r - 2.0f, area.b - 3.0f },
                           layout.rows, layout.shownRows, layout.firstRow);
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

        // 入力欄が 1 つも出ていないときの IME の行き先（Paint() が拾う）。型入力
        // ジャンプは名前を IME で打てるので、変換窓は «今探している行» の脇に出す。
        if (isCursor && focused) {
            listCaret_ = { nameBox.l, row.t, nameBox.l, row.b };
            listCaretValid_ = true;
        }

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

    // A field drawn in place says what it is renaming by where it sits, but not
    // what it is being asked for - and an empty box on a borrowed row says least
    // of all. So the heading the bottom bar used to carry moves here, where it
    // costs the field no width.
    std::string right;
    const Prompt& p = app_.prompt();
    // 入力欄を持たない画面で変換している ─ 一覧の上での型入力ジャンプがこれ。行の
    // どれも書き換わらないので、ここで言わなければ打った文字はどこにも出ない
    // （打ちかけの文字列をステータス行に出しているのと同じ理由で、しかも変換中は
    // ジャンプすら起きていない）。欄が描いたのならそちらが答えなので出さない。
    if (app_.composition().active() && !caretInField_) {
        right = str.Format("ui.composing", { app_.composition().text });
    } else if (p.isInline() && p.kind != PromptKind::Path && !p.labelKey.empty()) {
        right = str.Get(p.labelKey);
    } else if (!app_.statusMessage().empty() && !app_.statusExpired()) {
        right = app_.statusMessage();
    }
    // カーソル行の名前を最後の控えに置いていたが、やめた ─ その名前は行そのものに
    // 書いてあり、しかもカーソルの枠がどの行かをすでに言っている。同じことを
    // 2 か所で言うために帯の半分を使っていたことになる。

    Tab* tab = app_.workspace().focusedTab();
    std::string left;
    std::string tail;
    if (tab) {
        left = str.Format("ui.status_items", { std::to_string(tab->ItemCount()) });
        const int marked = tab->MarkedCount();
        if (marked > 0) {
            left += "   " + str.Format("ui.status_selected",
                                       { std::to_string(marked), FormatSize(tab->MarkedBytes()) });
        }
        if (!tab->filter.empty()) left += "   [" + tab->filter + "]";
        // 容量は列挙が持ち帰った値（fs::ListResult）。訊く先が OS で冷えた共有では
        // 数秒返ってこないので、描くたびに訊ける値ではない。
        //
        // 出すのは «使用 / 総容量» で、空きだけではない ─ 「空き 40 GB」は多いのか
        // 少ないのかを言っておらず、読む側が総容量を覚えている前提になる。使用量は
        // 引き算で出す ─ 2 つの数と食い違う 3 つ目を持ち回らない。
        //
        // 総容量 0 は「まだ訊いていない」なので何も出さない ─ 仮想フォルダも共有の
        // 一覧もここに入る。「0 B / 0 B」と並べれば、容量が尽きたと読める。
        const uint64_t total = tab->listing.totalBytes;
        if (total > 0) {
            const uint64_t used = total - std::min(total, tab->listing.freeBytes);
            tail = str.Format("ui.status_usage", { FormatSize(used), FormatSize(total) });
        }
    }

    // 1 行に 2 つ置くので、後のものには前が使い残した幅しか渡さない ─ 矩形が
    // 重なっていれば文字も重なる。先に席を取るのは右で、あちらは «今何が起きたか» の
    // 答え。落とすのは空き容量から ─ 件数も選択も今の操作の答えだが、これは
    // いつも同じ顔でいる背景。
    const float rightWidth = right.empty() ? 0.0f : r.MeasureText(right, FontRole::UiSmall);
    const float leftLimit = area.r - kPad - (right.empty() ? 0.0f : rightWidth + kPad * 2.0f);
    if (!tail.empty()) {
        const std::string full = left.empty() ? tail : left + "   " + tail;
        if (area.l + kPad + r.MeasureText(full, FontRole::UiSmall) <= leftLimit) left = full;
    }
    r.DrawText(left, { area.l + kPad, area.t, std::max(area.l + kPad, leftLimit), area.b },
               th.textDim, FontRole::UiSmall, TextAlign::Left);
    r.DrawText(right, { area.l + kPad, area.t, area.r - kPad, area.b }, th.textDim,
               FontRole::UiSmall, TextAlign::Right);
}

// Text, selection, composition and caret for one editable field.
//
// One implementation for every field Kite draws - the prompt on a breadcrumb, a
// row or a session chip, and the filter box of the two choosers. Two of them
// would drift: the selection would end up under the text on one screen and over
// it on the other, and the composition would sit somewhere else again.
//
// The font is a parameter because the field takes the place of whatever it is
// editing: a session chip is set in the small face, so a field measured in the
// normal one would size its chip to a width the text never fills. `caretInset`
// is how far the caret stops short of the box, which differs with how tall the
// box is around the letters.
void AppUi::PaintTextField(Renderer& r, const RectF& box, const TextField& f, FontRole role,
                           float caretInset, std::string_view placeholder,
                           size_t placeholderUntil) {
    const Theme& th = app_.theme();
    const Composition& comp = app_.composition();

    // 変換中の文字列は、確定を待たずに «入力欄の中身» として描く。IME に描かせると、
    // その窓は自前のフォントと行送りで文字を置くので、同じ行の中で数ピクセルずれた
    // 位置に出て、確定した瞬間に跳ぶ。
    //
    // 見せるのは «確定した後の形» ─ 選択があればそれは置き換えられるので、変換中から
    // 消しておく。Enter を押して初めて消えるのでは、何が起きるのかが押すまで分からない。
    std::string shown = f.text;
    size_t at = f.caret;
    if (comp.active()) {
        at = f.selBegin();
        shown.erase(at, f.selEnd() - f.selBegin());
        shown.insert(at, comp.text);
    }
    // 幅は必ず «画面に出ている 1 本の文字列» の接頭辞から測る。断片を別々に測って
    // 足すと、詰め（カーニング）が入った瞬間に全体の幅と合わなくなる。
    auto widthTo = [&](size_t bytes) {
        return box.l + r.MeasureText(std::string_view(shown).substr(0, bytes), role);
    };

    CompositionRun run;
    if (comp.active()) {
        run.from = widthTo(at);
        run.to = widthTo(at + comp.text.size());
        if (comp.hasTarget()) {
            run.targetFrom = widthTo(at + comp.targetBegin);
            run.targetTo = widthTo(at + comp.targetEnd);
        }
    }

    // The selection goes under the text rather than recolouring it: one
    // DrawText call cannot paint a run in two colours, and splitting the string
    // into three would measure each piece on its own - which drifts apart from
    // the whole once kerning is involved.
    if (f.hasSelection() && !comp.active()) {
        r.FillRect({ widthTo(f.selBegin()), box.t + 3.0f, widthTo(f.selEnd()), box.b - 3.0f },
                   th.textSelection);
    }
    PaintCompositionBack(r, box, run, role);

    if (!shown.empty()) r.DrawText(shown, box, th.text, role, TextAlign::Left);
    PaintCompositionMarks(r, box, run, role);

    // The caret is the text colour, not the accent: it is a letter-shaped mark
    // in a run of letters, and every field on the desktop draws it that way.
    // The accent says where the keyboard is; the bar's own border already does.
    const float caretX = widthTo(comp.active() ? at + comp.caret : f.caret);

    // Nothing typed yet - which on the command palette means the ">" and nothing
    // else, since that marker is always in the field. Drawn past the caret so the
    // two never land on the same pixel.
    if (!placeholder.empty() && shown.size() <= placeholderUntil) {
        r.DrawText(placeholder, { caretX + 12.0f, box.t, box.r, box.b }, th.textDim.alpha(0.6f),
                   role, TextAlign::Left);
    }
    r.FillRect({ caretX, box.t + caretInset, caretX + 1.5f, box.b - caretInset }, th.text);
    caret_ = { caretX, box.t, caretX, box.b };
    caretInField_ = true;
}

// The prompt's field, wherever it is drawn.
void AppUi::PaintPromptField(Renderer& r, const RectF& field, FontRole role) {
    PaintTextField(r, field, app_.prompt(), role, 4.0f);
}

// 変換中の文字列に印を付ける «行» ─ 器の下端ではなく、文字が実際に載っている高さ。
//
// チューザの入力欄は行の 1.6 倍の高さがあり、文字はその真ん中に置かれるので、器の
// 下辺に下線を引くと下線だけが文字から離れて浮く。
static RectF CompositionLine(Renderer& r, const RectF& field, FontRole role) {
    const float h = std::min(field.h(), r.LineHeight(role));
    const float mid = field.center().y;
    return { field.l, mid - h * 0.5f, field.r, mid + h * 0.5f };
}

// 変換中の文字列の下敷き。文字より先に塗るので、下線とは別の関数になっている。
//
// 注目節（今まさに変換している節）だけを一段強く見せる ─ 節が 2 つ以上あるとき、
// どれを変換しているのかは IME の窓を消した以上ここでしか分からない。色に
// textSelection を使うのは、入力欄の中で «今この範囲の話をしている» と言う色が
// すでにそれだから（一覧の行の選択とは別の問いなので、そちらの無彩色ではない）。
void AppUi::PaintCompositionBack(Renderer& r, const RectF& field, const CompositionRun& run,
                                 FontRole role) {
    if (!run.active() || !run.hasTarget()) return;
    const RectF line = CompositionLine(r, field, role);
    r.FillRect({ run.targetFrom, line.t, run.targetTo, line.b }, app_.theme().textSelection);
}

// 下線 1 本が «まだ確定していない» の共通語彙。注目節はもう一段太く、アクセント色で
// 引く ─ 変換対象がどれかは、下敷きだけでは縞の乗った行の上で読み取りにくい。
void AppUi::PaintCompositionMarks(Renderer& r, const RectF& field, const CompositionRun& run,
                                  FontRole role) {
    if (!run.active()) return;
    const Theme& th = app_.theme();
    const RectF line = CompositionLine(r, field, role);
    r.FillRect({ run.from, line.b - 1.0f, run.to, line.b }, th.textDim);
    if (run.hasTarget()) {
        r.FillRect({ run.targetFrom, line.b - 2.0f, run.targetTo, line.b }, th.accent);
    }
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

}  // namespace kite::ui
