#include "ui/AppUi.h"

#include <algorithm>

#include "core/base/Format.h"
#include "core/base/PathUtil.h"
#include "core/input/Commands.h"
#include "ui/Glyphs.h"

namespace kite::ui {
namespace {

constexpr float kPad = 8.0f;
constexpr float kIconCell = 20.0f;
constexpr float kColExt = 58.0f;
constexpr float kColSize = 84.0f;
constexpr float kColDate = 124.0f;
constexpr float kScrollbarWidth = 10.0f;

std::string SortArrow(bool desc) { return desc ? "\xE2\x96\xBC" : "\xE2\x96\xB2"; }  // ▼ ▲

}  // namespace

AppUi::AppUi(App& app) : app_(app) {}

void AppUi::Add(const RectF& r, Hit kind, int index, Pane* pane, SplitNode* node,
                std::string path) {
    regions_.push_back({ r, kind, pane, node, index, std::move(path) });
}

const AppUi::Region* AppUi::Pick(float x, float y) const {
    for (auto it = regions_.rbegin(); it != regions_.rend(); ++it) {
        if (it->rect.contains(x, y)) return &*it;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Paint
// ---------------------------------------------------------------------------

void AppUi::Paint(Renderer& r) {
    regions_.clear();
    const Theme& th = app_.theme();
    const SizeF size = r.surfaceSize();
    const RectF full = { 0.0f, 0.0f, size.w, size.h };

    r.FillRect(full, th.windowBg);

    RectF rest = full;

    const RectF sessionBar = { rest.l, rest.t, rest.r, rest.t + th.sessionBarHeight };
    PaintSessionBar(r, sessionBar);
    rest.t = sessionBar.b;

    const RectF statusBar = { rest.l, rest.b - th.statusBarHeight, rest.r, rest.b };
    rest.b = statusBar.t;

    if (app_.prompt().active()) {
        const float h = th.statusBarHeight + 6.0f;
        const RectF promptBar = { rest.l, rest.b - h, rest.r, rest.b };
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
    PaintDragOverlay(r);

    if (app_.keyHelpVisible()) PaintKeyHelp(r, full);
    if (app_.keyEditor().visible()) PaintKeySettings(r, full);
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
}

// --- session bar ------------------------------------------------------------

void AppUi::PaintSessionBar(Renderer& r, const RectF& area) {
    const Theme& th = app_.theme();
    Workspace& ws = app_.workspace();

    r.FillRect(area, th.panelBg);
    r.FillRect({ area.l, area.b - 1.0f, area.r, area.b }, th.border);

    float x = kPad;
    for (size_t i = 0; i < ws.sessions.size(); ++i) {
        const bool active = (static_cast<int>(i) == ws.active);
        const std::string label = std::to_string(i + 1) + "  " + ws.sessions[i]->name;
        const float w = r.MeasureText(label, FontRole::UiSmall) + kPad * 2.0f;
        const RectF chip = { x, area.t + 3.0f, x + w, area.b - 3.0f };
        if (chip.r > area.r - 30.0f) break;

        if (active) r.FillRoundRect(chip, 4.0f, th.sessionActiveBg);
        r.DrawText(label, chip, active ? th.text : th.textDim, FontRole::UiSmall,
                   TextAlign::Center);
        Add(chip, Hit::SessionChip, static_cast<int>(i));
        x = chip.r + 4.0f;
    }

    const RectF add = { x, area.t + 3.0f, x + 22.0f, area.b - 3.0f };
    if (add.r < area.r - 8.0f) {
        glyph::Plus(r, add, th.textDim, 1.5f);
        Add(add, Hit::SessionAdd);
    }

    const std::string brand = app_.keyHelpVisible() ? "" : "Kite";
    r.DrawText(brand, { area.r - 60.0f, area.t, area.r - kPad, area.b }, th.textDim.alpha(0.5f),
               FontRole::UiSmall, TextAlign::Right);
}

// --- sidebar ----------------------------------------------------------------

void AppUi::PaintSidebar(Renderer& r, const RectF& area) {
    const Theme& th = app_.theme();
    const Strings& str = app_.strings();

    r.FillRect(area, th.panelBg);
    sidebarRect_ = area;
    r.PushClip(area);

    const float rowH = th.rowHeight;
    float y = area.t + 4.0f - sidebarScroll_;
    const Tab* current = const_cast<App&>(app_).workspace().focusedTab();
    const std::string currentPath = current ? current->path : std::string();

    auto section = [&](const char* key) {
        const RectF box = { area.l + kPad, y, area.r - kPad, y + rowH };
        r.DrawText(str.Get(key), box, th.textDim.alpha(0.75f), FontRole::UiSmall, TextAlign::Left);
        y += rowH;
    };

    auto item = [&](const std::string& label, const std::string& fullPath, int glyphKind) {
        const RectF row = { area.l + 2.0f, y, area.r - 2.0f, y + rowH };
        if (row.b > area.t && row.t < area.b) {
            const bool selected = !currentPath.empty() && currentPath == fullPath;
            if (selected) r.FillRoundRect(row, 4.0f, th.rowSelected);

            const RectF icon = { row.l + 6.0f, row.t, row.l + 6.0f + kIconCell, row.b };
            const Color iconColor = selected ? th.rowSelectedText : th.textFolder;
            // Bookmarks keep the star: it says "you pinned this", which no icon
            // the shell can supply would.
            const uint32_t shellIcon = glyphKind == 2 ? 0u : app_.IconFor(fullPath);
            if (shellIcon) {
                r.DrawIcon(shellIcon, icon.inset(1.0f));
            } else {
                switch (glyphKind) {
                    case 1: glyph::Drive(r, icon, iconColor); break;
                    case 2: glyph::Star(r, icon, th.accent); break;
                    case 3: glyph::Cloud(r, icon, iconColor); break;
                    default: glyph::Folder(r, icon, iconColor); break;
                }
            }
            r.DrawText(label, { icon.r + 6.0f, row.t, row.r - 6.0f, row.b },
                       selected ? th.rowSelectedText : th.text, FontRole::Ui, TextAlign::Left);
            Add(row, Hit::SidebarItem, 0, nullptr, nullptr, fullPath);
        }
        y += rowH;
    };

    section("ui.quick_access");
    for (const fs::Root& q : app_.quickAccess()) item(q.label, q.path, 0);

    y += 6.0f;
    section("ui.bookmarks");
    const std::vector<Bookmark>& marks = const_cast<App&>(app_).workspace().bookmarks;
    for (const Bookmark& mark : marks) item(mark.name, mark.path, 2);

    y += 6.0f;
    section("ui.drives");
    for (const fs::Root& d : app_.roots()) {
        const int kind = (d.kind == fs::RootKind::Cloud)     ? 3
                         : (d.kind == fs::RootKind::Network) ? 0
                                                             : 1;
        item(d.label, d.path, kind);

        // A thin capacity bar under fixed drives; free space is the number
        // people actually look for here.
        if (d.totalBytes > 0 && y - rowH > area.t && y < area.b) {
            const RectF track = { area.l + 34.0f, y + 1.0f, area.r - 12.0f, y + 4.0f };
            r.FillRoundRect(track, 1.5f, th.border);
            const float used = 1.0f - static_cast<float>(static_cast<double>(d.freeBytes) /
                                                         static_cast<double>(d.totalBytes));
            const RectF fill = { track.l, track.t, track.l + track.w() * std::clamp(used, 0.0f, 1.0f),
                                 track.b };
            r.FillRoundRect(fill, 1.5f, used > 0.9f ? th.textError : th.accent);
            y += 7.0f;
        }
    }

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

void AppUi::PaintPane(Renderer& r, Pane* pane, const RectF& area) {
    if (!pane) return;
    const Theme& th = app_.theme();
    const bool focused = (app_.workspace().focusedPane() == pane);

    r.PushClip(area);
    r.FillRect(area, th.listBg);

    RectF rest = area;
    const RectF tabBar = { rest.l, rest.t, rest.r, rest.t + th.tabBarHeight };
    PaintTabBar(r, pane, tabBar);
    rest.t = tabBar.b;

    Tab* tab = pane->activeTab();
    const RectF pathBar = { rest.l, rest.t, rest.r, rest.t + th.pathBarHeight };
    PaintPathBar(r, pane, tab, pathBar);
    rest.t = pathBar.b;

    PaintList(r, pane, tab, rest, focused);

    if (focused) {
        r.StrokeRect(area.inset(1.0f), th.paneFocusBorder.alpha(0.9f), 1.5f);
    }
    r.PopClip();
}

void AppUi::PaintTabBar(Renderer& r, Pane* pane, const RectF& area) {
    const Theme& th = app_.theme();
    r.FillRect(area, th.tabInactiveBg);
    // Registered first so the individual tabs, added below, win the hit test.
    Add(area, Hit::TabBar, 0, pane);

    float x = area.l;
    const float maxTabWidth = 190.0f;
    const float minTabWidth = 70.0f;
    const float avail = std::max(0.0f, area.w() - 28.0f);
    const float perTab = pane->tabs.empty()
                             ? maxTabWidth
                             : std::clamp(avail / static_cast<float>(pane->tabs.size()),
                                          minTabWidth, maxTabWidth);

    for (size_t i = 0; i < pane->tabs.size(); ++i) {
        const bool active = (static_cast<int>(i) == pane->active);
        const RectF tabRect = { x, area.t, x + perTab, area.b };
        if (tabRect.l > area.r - 20.0f) break;

        r.FillRect(tabRect, active ? th.tabActiveBg : th.tabInactiveBg);
        if (active) {
            r.FillRect({ tabRect.l, tabRect.t, tabRect.r, tabRect.t + 2.0f }, th.accent);
        }
        r.FillRect({ tabRect.r - 1.0f, tabRect.t + 5.0f, tabRect.r, tabRect.b - 5.0f }, th.border);

        const RectF close = { tabRect.r - 20.0f, tabRect.t + 6.0f, tabRect.r - 6.0f,
                              tabRect.b - 6.0f };
        const RectF label = { tabRect.l + 10.0f, tabRect.t, close.l - 4.0f, tabRect.b };
        r.DrawText(pane->tabs[i]->title(), label,
                   active ? th.tabActiveText : th.tabInactiveText, FontRole::Ui, TextAlign::Left);
        glyph::Cross(r, close, active ? th.tabActiveText.alpha(0.7f) : th.tabInactiveText.alpha(0.5f),
                     1.2f);

        Add(tabRect, Hit::TabItem, static_cast<int>(i), pane);
        Add(close, Hit::TabClose, static_cast<int>(i), pane);
        x = tabRect.r;
    }

    const RectF add = { x + 2.0f, area.t + 4.0f, x + 24.0f, area.b - 4.0f };
    if (add.r < area.r) {
        glyph::Plus(r, add, th.textDim, 1.4f);
        Add(add, Hit::TabAdd, 0, pane);
    }
    r.FillRect({ area.l, area.b - 1.0f, area.r, area.b }, th.border);
}

void AppUi::PaintPathBar(Renderer& r, Pane* pane, Tab* tab, const RectF& area) {
    const Theme& th = app_.theme();
    r.FillRect(area, th.tabActiveBg);
    if (!tab) return;

    // Breadcrumbs: split the path into cumulative prefixes.
    std::vector<std::pair<std::string, std::string>> crumbs;  // label, full path
    {
        std::string p = tab->path;
        while (!p.empty()) {
            crumbs.push_back({ path::DisplayName(p), p });
            const std::string up = path::Parent(p);
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
    if (tab->visible.empty()) {
        r.DrawText(str.Get(tab->filter.empty() ? "ui.empty" : "ui.no_match"),
                   body.inset(kPad, 8.0f), th.textDim, FontRole::Ui, TextAlign::Left);
        return;
    }

    const float rowH = th.rowHeight;
    const float maxScroll =
        std::max(0.0f, static_cast<float>(tab->visible.size()) * rowH - body.h());
    tab->scroll = std::clamp(tab->scroll, 0.0f, maxScroll);

    const int first = std::max(0, static_cast<int>(tab->scroll / rowH));
    const int last = std::min(static_cast<int>(tab->visible.size()) - 1,
                              first + static_cast<int>(body.h() / rowH) + 1);

    r.PushClip(body);
    for (int i = first; i <= last; ++i) {
        const float top = body.t + static_cast<float>(i) * rowH - tab->scroll;
        const RectF row = { body.l, top, body.r - kScrollbarWidth, top + rowH };

        const fs::Entry* entry = tab->EntryAt(i);
        const bool marked = entry && tab->marked[tab->visible[i]] != 0;
        const bool isCursor = (i == tab->cursor);

        if (i % 2 == 1) r.FillRect(row, th.listBgAlt);
        if (marked) r.FillRect(row, th.rowSelected);
        if (isCursor && focused) {
            r.StrokeRect({ row.l + 1.0f, row.t + 1.0f, row.r - 1.0f, row.b - 1.0f },
                         th.cursorBorder, 1.0f);
        } else if (isCursor) {
            r.FillRect(row, th.rowHover);
        }

        const RectF icon = { row.l + 6.0f, row.t, row.l + 6.0f + kIconCell, row.b };
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

        // The real icon arrives a frame or two later, and carries any overlay
        // (version control, cloud sync) with it. Until then the drawn glyph
        // holds the space, so rows never shift when it lands.
        const uint32_t shellIcon = app_.IconFor(path::Join(tab->path, e.name));
        if (shellIcon) {
            r.DrawIcon(shellIcon, icon.inset(1.0f));
        } else if (ghost) {
            glyph::Cloud(r, icon, th.textDim);
        } else if (e.isDir()) {
            glyph::Folder(r, icon, nameColor);
        } else {
            glyph::File(r, icon, nameColor.alpha(nameColor.a * 0.85f));
        }

        r.DrawText(e.name, nameBox, nameColor, FontRole::Ui, TextAlign::Left);

        if (wide && colExt.w() > 0.0f) {
            const std::string ext = e.isDir() ? std::string() : path::Extension(e.name);
            r.DrawText(ext, { colExt.l + 4.0f, row.t, colExt.r, row.b },
                       marked ? th.rowSelectedText : th.textDim, FontRole::UiSmall,
                       TextAlign::Left);
        }
        if (medium && colSize.w() > 0.0f) {
            const std::string sizeText = e.isDir() ? str.Get("ui.dir_marker") : FormatSize(e.size);
            r.DrawText(sizeText, { colSize.l, row.t, colSize.r - 6.0f, row.b },
                       marked ? th.rowSelectedText : th.textDim, FontRole::UiSmall,
                       TextAlign::Right);
        }
        if (wide && colDate.w() > 0.0f) {
            r.DrawText(FormatDateTime(e.mtime), { colDate.l + 6.0f, row.t, colDate.r, row.b },
                       marked ? th.rowSelectedText : th.textDim, FontRole::UiSmall,
                       TextAlign::Left);
        }

        Add(row, Hit::ListRow, i, pane);
    }
    r.PopClip();

    // A folder holding nothing still lists ".."; say so under it rather than
    // leaving the pane looking like it failed to load.
    if (tab->ItemCount() == 0) {
        const float top = body.t + rowH;
        r.DrawText(str.Get(tab->filter.empty() ? "ui.empty" : "ui.no_match"),
                   { body.l + kPad, top + 8.0f, body.r - kPad, body.b }, th.textDim, FontRole::Ui,
                   TextAlign::Left);
    }

    // --- scrollbar ---
    if (maxScroll > 0.0f) {
        const RectF track = { body.r - kScrollbarWidth, body.t, body.r, body.b };
        const float ratio = body.h() / (static_cast<float>(tab->visible.size()) * rowH);
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

    std::string right;
    if (!app_.statusMessage().empty() && !app_.statusExpired()) {
        right = app_.statusMessage();
    } else if (tab) {
        const fs::Entry* e = tab->CursorEntry();
        if (e) right = e->name;
    }
    r.DrawText(right, { area.r * 0.45f, area.t, area.r - kPad, area.b }, th.textDim,
               FontRole::UiSmall, TextAlign::Right);
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
        const RectF field = { area.l + kPad + labelW, area.t, area.r - kPad, area.b };
        r.DrawText(p.text, field, th.text, FontRole::Ui, TextAlign::Left);

        const float caretX =
            field.l + r.MeasureText(std::string_view(p.text).substr(0, p.caret), FontRole::Ui);
        r.FillRect({ caretX, field.t + 4.0f, caretX + 1.5f, field.b - 4.0f }, th.accent);
        caret_ = { caretX, field.t };
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

    const RectF titleBox = { panel.l + 20.0f, panel.t + 8.0f, panel.r - 20.0f, panel.t + 40.0f };
    r.DrawText(str.Get("ui.key_help_title"), titleBox, th.text, FontRole::UiBold, TextAlign::Left);
    r.DrawText(str.Get("ui.key_help_hint"), titleBox, th.textDim, FontRole::UiSmall,
               TextAlign::Right);

    const RectF content = { panel.l + 20.0f, titleBox.b + 4.0f, panel.r - 20.0f, panel.b - 12.0f };
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
        lines.push_back({ false, false, str.Label(info.labelKey), km.PrimaryChordText(info.id) });
    }

    const int columns = std::clamp(static_cast<int>(content.w() / 290.0f), 1, 4);
    const float colW = content.w() / static_cast<float>(columns);

    int perColumn = (static_cast<int>(lines.size()) + columns - 1) / columns;

    // Never leave a group heading stranded as the last line of a column.
    for (int i = perColumn - 1; i < static_cast<int>(lines.size()); i += perColumn) {
        if (lines[i].header) lines.insert(lines.begin() + i, { false, true, {}, {} });
    }
    perColumn = (static_cast<int>(lines.size()) + columns - 1) / columns;

    // Shrink the leading rather than overflow or scroll: the whole point of
    // this sheet is seeing every binding at once.
    const float lineH = std::min(18.0f, content.h() / static_cast<float>(std::max(1, perColumn)));

    for (size_t i = 0; i < lines.size(); ++i) {
        const Line& line = lines[i];
        if (line.spacer) continue;

        const int column = std::min(columns - 1, static_cast<int>(i) / perColumn);
        const float x = content.l + static_cast<float>(column) * colW;
        const float y = content.t + static_cast<float>(static_cast<int>(i) % perColumn) * lineH;
        const RectF row = { x, y, x + colW - 14.0f, y + lineH };

        if (line.header) {
            r.DrawText(line.label, row, th.accent, FontRole::UiBold, TextAlign::Left);
            continue;
        }
        const float chordW = std::min(112.0f, row.w() * 0.45f);
        r.DrawText(line.label, { row.l, row.t, row.r - chordW - 6.0f, row.b }, th.text,
                   FontRole::UiSmall, TextAlign::Left);
        r.DrawText(line.chord.empty() ? "-" : line.chord, { row.r - chordW, row.t, row.r, row.b },
                   line.chord.empty() ? th.textDim.alpha(0.4f) : th.textDim, FontRole::Mono,
                   TextAlign::Right);
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

    const RectF titleBox = { panel.l + 20.0f, panel.t + 8.0f, panel.r - 20.0f, panel.t + 38.0f };
    r.DrawText(str.Get("ui.key_settings_title"), titleBox, th.text, FontRole::UiBold,
               TextAlign::Left);
    r.DrawText(editor.filter().empty() ? str.Get("ui.key_settings_search_hint")
                                       : str.Format("ui.key_settings_filter", { editor.filter() }),
               titleBox, editor.filter().empty() ? th.textDim.alpha(0.6f) : th.text,
               FontRole::UiSmall, TextAlign::Right);

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

        const float chordW = std::min(190.0f, box.w() * 0.5f);
        r.DrawText(row.label, { box.l + 10.0f, box.t, box.r - chordW - 8.0f, box.b },
                   selected ? th.rowSelectedText : th.text, FontRole::Ui, TextAlign::Left);

        const bool capturingHere = selected && editor.capturing();
        const std::string chordText = capturingHere ? str.Get("ui.key_settings_capture")
                                      : row.chords.empty() ? str.Get("ui.key_settings_none")
                                                           : row.chords;
        const Color chordColor = capturingHere            ? th.accent
                                 : row.chords.empty()     ? th.textDim.alpha(0.45f)
                                 : selected               ? th.rowSelectedText
                                                          : th.textDim;
        r.DrawText(chordText, { box.r - chordW, box.t, box.r - 8.0f, box.b }, chordColor,
                   FontRole::Mono, TextAlign::Right);

        Add(box, Hit::KeyRow, i);
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
        // A right-click on an unmarked row selects it first, so the shell menu
        // acts on what the user is pointing at.
        const bool keep = (e.button == 1) && tab->marked[entry];
        if (!keep) tab->ClearMarks();
        tab->cursor = index;
        tab->ResetAnchor();
    }
    app_.EnsureCursorVisible();
    app_.host().Invalidate();
    return true;
}

// Clicks while the shortcut editor is up. Nothing behind it is reachable: the
// panel is modal in the same sense the shell's own menu is.
bool AppUi::HandleKeySettingsClick(const MouseEvent& e) {
    const Region* region = Pick(e.x, e.y);
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
        // Past the midpoint means "after this tab".
        if (x > region->rect.center().x) ++index;
    }
    *outPane = pane;
    *outIndex = std::clamp(index, 0, static_cast<int>(pane->tabs.size()));
    return true;
}

void AppUi::FinishTabDrag() {
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

void AppUi::CancelDrag() {
    drag_ = Drag::None;
    dragSplitter_ = nullptr;
    dragTabPane_ = nullptr;
    dragTabIndex_ = -1;
    dropTabPane_ = nullptr;
    dropTabIndex_ = -1;
    dropTabMarker_ = {};
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
        if (tab->IsParentRow(region->index)) return path::Parent(tab->path);
        // Only a folder swallows the drop; over a file it goes to the folder
        // being listed, which is what every file manager does.
        if (const fs::Entry* entry = tab->EntryAt(region->index)) {
            if (entry->isDir()) return path::Join(tab->path, entry->name);
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
        } else if (drag_ == Drag::PendingTab && moved > kDragThreshold) {
            drag_ = Drag::Tab;
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
            Pane* pane = nullptr;
            int index = 0;
            if (ResolveTabDrop(e.x, e.y, &pane, &index)) {
                dropTabPane_ = pane;
                dropTabIndex_ = index;
                // Draw the insertion caret on the boundary this slot means:
                // the left edge of tab `index`, or the right edge of the one
                // before it when inserting at the end.
                dropTabMarker_ = {};
                for (const Region& candidate : regions_) {
                    if (candidate.kind != Hit::TabItem || candidate.pane != pane) continue;
                    if (candidate.index == index) {
                        dropTabMarker_ = { candidate.rect.l - 1.0f, candidate.rect.t,
                                           candidate.rect.l + 2.0f, candidate.rect.b };
                    } else if (candidate.index == index - 1 && dropTabMarker_.empty()) {
                        dropTabMarker_ = { candidate.rect.r - 2.0f, candidate.rect.t,
                                           candidate.rect.r + 1.0f, candidate.rect.b };
                    }
                }
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
        return false;
    }

    if (e.type == MouseEvent::Type::Up) {
        if (drag_ == Drag::Tab) {
            FinishTabDrag();
            return true;
        }
        CancelDrag();
        return false;
    }

    if (e.type == MouseEvent::Type::Wheel) {
        if (app_.keyEditor().visible()) {
            app_.keyEditor().Scroll(static_cast<int>(-e.wheel * 3.0f));
            app_.host().Invalidate();
            return true;
        }
        if (region && sidebarRect_.contains(e.x, e.y)) {
            sidebarScroll_ = std::max(0.0f, sidebarScroll_ - e.wheel * 60.0f);
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

    if (app_.keyEditor().visible()) return HandleKeySettingsClick(e);

    if (app_.keyHelpVisible()) {
        app_.Execute(Cmd::ShowKeyHelp);
        return true;
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
            app_.Execute(static_cast<Cmd>(static_cast<int>(Cmd::Session1) + region->index));
            return true;
        case Hit::SessionAdd:
            app_.Execute(Cmd::NewSession);
            return true;

        case Hit::SidebarItem:
            app_.OpenPath(region->path, (e.mods & kModCtrl) != 0 || e.button == 2);
            return true;

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
                region->pane->Activate(region->index);
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
                // Right-click opens the extended menu directly; hold Shift for
                // the short Windows 11 one.
                app_.ShowContextMenuAt(e.screenX, e.screenY, (e.mods & kModShift) == 0);
            }
            return true;
        }

        case Hit::ListBackground:
            app_.FocusPane(region->pane);
            if (Tab* t = region->pane->activeTab()) t->ClearMarks();
            if (e.button == 1) {
                app_.ShowContextMenuAt(e.screenX, e.screenY, (e.mods & kModShift) == 0);
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
