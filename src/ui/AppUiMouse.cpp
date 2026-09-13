// Everything the pointer does: hit-test dispatch, drags (tabs, sidebar items and
// sections, session chips, the splitter, the selection band) and the drop
// feedback an external file drag leaves behind.
//
// Split out of AppUi.cpp for size alone. It is one file because a drag is only
// ever half-written otherwise: the press arms it, a move promotes it, and the
// release finishes it, and those three have to agree about the same state.

#include <algorithm>
#include <cmath>

#include "core/fs/VirtualPath.h"
#include "core/input/Commands.h"
#include "ui/AppUi.h"

namespace kite::ui {
namespace {

// Where a dragged item should land once it has been lifted out of the list:
// everything that sat after its old slot has already shifted down by one.
int LiftedTarget(int from, int to) { return to > from ? to - 1 : to; }

}  // namespace

void AppUi::ScrollPane(Pane* pane, float deltaPixels) {
    if (!pane) return;
    Tab* tab = pane->activeTab();
    if (!tab) return;
    tab->scroll += deltaPixels;
    const float maxScroll = std::max(
        0.0f, static_cast<float>(tab->visible.size()) * pane->viewport.rowHeight - pane->viewport.listHeight);
    tab->scroll = std::clamp(tab->scroll, 0.0f, maxScroll);
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
    if (entry < 0) {
        // 「..」と塊の見出し ─ どちらも選べる実体を持たない。見出しは自分の当たり
        // 判定を持つので普通は来ないが、前のフレームの行を押していることがある。
        // ".." cannot be marked, so no modifier adds anything here. An unmodified
        // click still drops the selection the way clicking any other row does:
        // otherwise the menu, or a drag started here, would act on files the
        // pointer has long since left.
        if ((e.mods & (kModCtrl | kModShift)) == 0) tab->ClearMarks();
        tab->cursor = tab->SkipGroupRows(index, 1);
        tab->ResetAnchor();
        app_.EnsureCursorVisible();
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
    const RectF& area = pane->viewport.listArea;
    const bool overScrollbar = e.x >= area.r - kScrollbarWidth &&
                               static_cast<float>(tab->visible.size()) * pane->viewport.rowHeight > area.h();
    if (overScrollbar) return;

    // No threshold to cross first, unlike a tab or a file drag: there is nothing
    // else a press out here could turn into, and the band catches no row until
    // it actually reaches one.
    MarqueeDrag band;
    band.pane = pane;
    band.tab = tab;
    band.base = tab->marked;
    band.anchorX = e.x;
    band.anchorY = (e.y - area.t) + tab->scroll;
    band.x = e.x;
    band.y = e.y;
    drag_.start = { e.x, e.y };
    drag_.what = std::move(band);
}

// Which rows the band covers is answered by geometry every time, never
// accumulated: the marks are laid down again from the ones held when the sweep
// began, so pulling the band back releases exactly what it released, and marks
// set earlier with Space or Ctrl+click are not swept away with it.
void AppUi::UpdateMarquee(float x, float y) {
    MarqueeDrag* band = std::get_if<MarqueeDrag>(&drag_.what);
    if (!band) return;
    Tab* tab = band->pane ? band->pane->activeTab() : nullptr;
    // A listing that changed under the sweep (a watcher event, a tab switched
    // from the keyboard) makes the remembered marks meaningless; let go rather
    // than write them onto whatever is there now.
    if (!tab || tab != band->tab || tab->marked.size() != band->base.size()) {
        CancelDrag();
        return;
    }

    band->x = x;
    band->y = y;

    const RectF& body = band->pane->viewport.listArea;
    const float rowH = std::max(1.0f, band->pane->viewport.rowHeight);
    const float contentY = (y - body.t) + tab->scroll;
    const float top = std::min(band->anchorY, contentY);
    const float bottom = std::max(band->anchorY, contentY);

    tab->marked = band->base;

    // Half-open bands: a row is caught when the band actually overlaps it, so a
    // press in the empty space followed by a twitch selects nothing.
    const int rows = static_cast<int>(tab->visible.size());
    const int firstRow = static_cast<int>(std::floor(top / rowH));
    const int lastRow = static_cast<int>(std::ceil(bottom / rowH)) - 1;
    if (rows > 0 && lastRow >= firstRow && lastRow >= 0 && firstRow < rows) {
        tab->MarkRange(firstRow, lastRow, true);
        // The cursor follows the moving end, so Shift+arrow afterwards carries
        // on from where the pointer stopped instead of jumping back.
        const int under = std::clamp(static_cast<int>(std::floor(contentY / rowH)), 0, rows - 1);
        // 見出しの上には止まらない。掃いている向きへ 1 つ越える。
        tab->cursor = tab->SkipGroupRows(under, contentY >= band->anchorY ? 1 : -1);
        tab->ResetAnchor();
    }

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
        return true;
    }
    if (region && region->kind == Hit::KeyRow) {
        app_.keyEditor().SelectRow(region->index);
        // The second click is the one that arms capture, so a single click can
        // still just move the selection around.
        if (e.button == 0 && e.clicks >= 2) app_.keyEditor().BeginCapture(false);
        return true;
    }
    if (region && region->kind == Hit::KeyPanel) {
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

void AppUi::FinishTabDrag(const TabDrag& drag) {
    // Copied out first: CancelDrag() takes the state away, and App may close the
    // pane out from under us on the way through.
    Pane* from = drag.pane;
    Pane* to = drag.dropPane;
    const int index = drag.index;
    const int dropIndex = drag.dropIndex;
    const bool outside = drag.outside;
    CancelDrag();

    // Let go out past the edge: pull the tab into a window of its own. Decided
    // before anything else, because there is no pane under the pointer to fall
    // back on and the last one it passed over is not an answer.
    if (outside && from && index >= 0) {
        app_.DetachTabToNewWindow(from, index);
        return;
    }

    Session* session = app_.workspace().activeSession();
    if (!session || !from || !to || index < 0) return;

    if (to == from) {
        from->ReorderTab(index, LiftedTarget(index, dropIndex));
        return;
    }
    std::unique_ptr<Tab> moved = from->DetachTab(index);
    if (!moved) return;
    to->AttachTab(std::move(moved), dropIndex);
    // A pane with no tabs left has nothing to show; fold it away.
    if (from->empty()) session->ClosePane(from);
    app_.FocusPane(to);
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

// 掴んだ見出しがどの位置を求めているか。数えるのは名前以外の列だけで、名前の列に
// 触れても何も提案しない ─ そこは動かせない席。
bool AppUi::ResolveColumnDrop(float x, float y, int* outIndex, RectF* outMarker) const {
    for (const Region& candidate : regions_) {
        if (candidate.kind != Hit::ColumnHeader || candidate.index <= 0) continue;
        if (!candidate.rect.contains(x, y)) continue;

        const bool after = x > candidate.rect.center().x;
        *outIndex = candidate.index + (after ? 1 : 0);
        const float edge = after ? candidate.rect.r : candidate.rect.l;
        *outMarker = { edge - 1.0f, candidate.rect.t, edge + 2.0f, candidate.rect.b };
        return true;
    }
    return false;
}

// 4 種とも同じ 3 段を通るので、違うのは «どこへ訊くか» だけ。
void AppUi::ProposeReorder(ReorderDrag& drag, float x, float y) {
    bool (AppUi::*resolve)(float, float, int*, RectF*) const = nullptr;
    switch (drag.kind) {
        case ReorderKind::Sidebar: resolve = &AppUi::ResolveSidebarDrop; break;
        case ReorderKind::Section: resolve = &AppUi::ResolveSectionDrop; break;
        case ReorderKind::Session: resolve = &AppUi::ResolveSessionDrop; break;
        case ReorderKind::Column: resolve = &AppUi::ResolveColumnDrop; break;
    }
    int slot = -1;
    RectF edge{};
    // 自分の行から外れたところでは何も提案しない ─ そこで離せば順序は変わらない。
    const bool found = (this->*resolve)(x, y, &slot, &edge);
    drag.dropIndex = found ? slot : -1;
    drag.marker = found ? edge : RectF{};
}

void AppUi::FinishReorder(const ReorderDrag& drag) {
    const ReorderKind kind = drag.kind;
    const SidebarSection section = drag.section;
    const int from = drag.index;
    const int to = drag.dropIndex;
    CancelDrag();
    if (from < 0 || to < 0) return;

    switch (kind) {
        case ReorderKind::Sidebar:
            app_.MoveSidebarItem(section, from, LiftedTarget(from, to));
            break;
        case ReorderKind::Section:
            app_.MoveSidebarSection(from, LiftedTarget(from, to));
            break;
        case ReorderKind::Session:
            app_.MoveSession(from, LiftedTarget(from, to));
            break;
        case ReorderKind::Column:
            // 名前の列は動かせない席なので、0 は «掴めなかった» と同じ ─ 掴む側も
            // 落とす側も。
            if (from > 0 && to > 0) app_.MoveColumn(from, LiftedTarget(from, to));
            break;
    }
}

// 見出しを押しただけのときの答え。列の識別子は並べ替えの基準そのものなので、
// 表を引くだけで «その列で並べ替える» コマンドになる。
void AppUi::SortByColumn(int index) {
    const ColumnLayout& layout = app_.columns();
    if (index < 0 || index >= static_cast<int>(layout.columns.size())) return;
    static const Cmd kSortCommands[] = { Cmd::SortByName, Cmd::SortByExt, Cmd::SortBySize,
                                         Cmd::SortByDate, Cmd::SortByAge };
    app_.Execute(kSortCommands[static_cast<int>(layout.columns[static_cast<size_t>(index)].id)]);
}

// Which slot in the section being dragged the pointer is asking for, plus the
// boundary to draw the caret on. Only rows of that one section are considered:
// a bookmark has no meaning among the drives, and the sections are separately
// ordered lists rather than one list with headings in it.
bool AppUi::ResolveSidebarDrop(float x, float y, int* outIndex, RectF* outMarker) const {
    const ReorderDrag* drag = std::get_if<ReorderDrag>(&drag_.what);
    if (!drag || drag->section == SidebarSection::Count) return false;

    for (const Region& candidate : regions_) {
        if (candidate.kind != Hit::SidebarItem || candidate.section != drag->section) continue;
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

void AppUi::CancelDrag() {
    // 種別ごとのフィールドを 1 つずつ初期値へ戻す列だったころは、フィールドを
    // 足すたびにここが抜けた。型が 1 つなら、消し忘れようが無い。
    drag_ = {};
    pendingUnmark_ = false;
}

bool AppUi::DragHidesHover() const {
    // 掴んでいるものの行き先が読めなくなるドラッグ ─ 通りすがった行が光ると、
    // «どちらが答えか» が分からない。
    if (std::holds_alternative<SplitterDrag>(drag_.what)) return true;
    if (std::holds_alternative<MarqueeDrag>(drag_.what)) return true;
    if (std::holds_alternative<TabBarWidthDrag>(drag_.what)) return true;
    if (const TabDrag* tab = std::get_if<TabDrag>(&drag_.what)) return tab->started;
    if (const ReorderDrag* drag = std::get_if<ReorderDrag>(&drag_.what)) {
        // 列だけは外す ─ 幅も並べ替えも一覧の «上» で起きるので、行が光っても
        // 掴んでいるものと取り違えようが無い。
        return drag->started && drag->kind != ReorderKind::Column;
    }
    return false;
}

std::string AppUi::DropTargetAt(float x, float y) const { return DropTargetIn(Pick(x, y)); }

std::string AppUi::DropTargetIn(const Region* region) const {
    if (!region) return {};

    // The sidebar is a legitimate destination - dropping onto a bookmark or a
    // quick-access folder is often faster than navigating there.
    if (region->kind == Hit::SidebarItem || region->kind == Hit::Crumb) {
        return PathIn(region);
    }
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
    // One hit test for both answers: the path is what the drop would do, the
    // region is where to draw the frame saying so.
    const Region* region = Pick(x, y);
    dropPath_ = DropTargetIn(region);
    dropActive_ = !dropPath_.empty();

    if (!dropActive_ || !region) {
        dropHighlight_ = {};
        return;
    }
    // Outline the row when the target is a specific folder, the whole list
    // otherwise, so the distinction is visible at a glance.
    const Tab* under = region->pane ? region->pane->activeTab() : nullptr;
    const bool ontoRow = (region->kind == Hit::ListRow || region->kind == Hit::SidebarItem ||
                          region->kind == Hit::Crumb) &&
                         !(under && dropPath_ == under->path);
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
    // 入口で 1 回（App::OnKey と同じ）。ドラッグの提案も、押した先で走った
    // コマンドも、もう自分では頼まない。
    //
    // **降りるのは «何も変わらなかった» ときだけ** ─ 1 ピクセルごとに頼むと、
    // 動かしている間ずっと全面再描画になる。
    Redraw redraw(app_.host());

    if (e.type == MouseEvent::Type::Leave) {
        // The pointer went to another window: whatever was lit under it is not
        // under anything any more.
        if (mouseInside_) {
            mouseInside_ = false;
            hoverKind_ = Hit::None;
            hoverRect_ = {};
        } else {
            redraw.cancel();
        }
        return false;
    }

    mouseX_ = e.x;
    mouseY_ = e.y;
    mouseInside_ = true;

    // 押す・動かす・離す・回す。ドラッグは 3 つにまたがる 1 つの操作なので、
    // 「今おこなっていること」は `drag_` が 1 つだけ持ち、3 つともそれを見る。
    switch (e.type) {
        case MouseEvent::Type::Down: return OnPress(e);
        case MouseEvent::Type::Move: return OnDrag(e, redraw);
        case MouseEvent::Type::Up: return OnRelease(e);
        case MouseEvent::Type::Wheel: return OnWheel(e);
        default: break;
    }
    return false;
}

// 動かしている間。掴んでいるものが «今どこを求めているか» を引き直すのがここで、
// 押しただけのドラッグが本番になるのもここ 1 か所。
bool AppUi::OnDrag(const MouseEvent& e, Redraw& redraw) {
    constexpr float kDragThreshold = 6.0f;

    // 幅と分割線を動かしている間は、ポインタがどこを通ろうと関係が無い ─
    // 掴んだ線が指に付いてくる、それだけの話。
    if (SplitterDrag* split = std::get_if<SplitterDrag>(&drag_.what)) {
        SplitNode* n = split->node;
        if (!n) return true;
        const RectF& box = n->rect;
        const float span = (n->kind == SplitNode::Kind::LeftRight) ? box.w() : box.h();
        const float delta =
            (n->kind == SplitNode::Kind::LeftRight) ? (e.x - split->origin) : (e.y - split->origin);
        if (span > 1.0f) n->ratio = std::clamp(split->ratio + delta / span, 0.08f, 0.92f);
        return true;
    }

    if (const ColumnWidthDrag* column = std::get_if<ColumnWidthDrag>(&drag_.what)) {
        // 掴んでいるのは列の左端で、右端は動かない（右にある列は何も変わらない）。
        // だから幅は差でそのまま出る ─ 線はポインタにぴったり付いてくる。
        if (column->index > 0) app_.SetColumnWidth(column->index, column->right - e.x);
        return true;
    }

    if (const TabBarWidthDrag* bar = std::get_if<TabBarWidthDrag>(&drag_.what)) {
        // 掴んでいるのは右の縁で、バーの左端は動かない ─ 幅は差でそのまま出る
        // （列の縁と左右が逆なだけ）。ペインの半分で頭打ちにするのは、
        // レイアウトがそこで止めるから ─ 渡してしまうと、画面のバーはもう
        // 伸びないのに覚えている幅だけが増える。
        float width = e.x - bar->left;
        if (bar->max > 0.0f) width = std::min(width, bar->max);
        app_.SetTabBarWidth(width);
        return true;
    }

    const bool leftHeld = (e.buttons & kButtonLeft) != 0;
    const float moved = std::abs(e.x - drag_.start.x) + std::abs(e.y - drag_.start.y);

    if (!leftHeld && !std::holds_alternative<std::monostate>(drag_.what)) {
        // The button came up somewhere we did not see; do not get stuck.
        CancelDrag();
    } else if (std::holds_alternative<MarqueeDrag>(drag_.what)) {
        UpdateMarquee(e.x, e.y);
        return true;
    } else if (moved > kDragThreshold) {
        // 押しただけが本番になる 1 か所。
        if (TabDrag* tab = std::get_if<TabDrag>(&drag_.what)) {
            tab->started = true;
        } else if (ReorderDrag* drag = std::get_if<ReorderDrag>(&drag_.what)) {
            // 名前の列は動かせない。掴んだままでも並べ替えの答えは «押した» ままな
            // ので、離せば今までどおり名前順になる。
            if (drag->kind != ReorderKind::Column || drag->index > 0) drag->started = true;
        } else if (std::holds_alternative<FileDrag>(drag_.what)) {
            // Hand off to the OS. BeginFileDrag blocks until the drag ends, so
            // clear our own state first.
            const Tab* carried = app_.workspace().focusedTab();
            std::vector<std::string> paths =
                carried ? carried->SelectionPaths() : std::vector<std::string>{};
            CancelDrag();
            if (!paths.empty()) app_.host().BeginFileDrag(paths);
            return true;
        }
    }

    if (TabDrag* tab = std::get_if<TabDrag>(&drag_.what); tab && tab->started) {
        // Off the window entirely: the tab is asking for a window of its
        // own, so no slot in this one is being proposed.
        tab->outside = OutsideWindow(e.x, e.y);
        tab->dropPane = nullptr;
        tab->dropIndex = -1;
        tab->marker = {};

        Pane* pane = nullptr;
        int index = 0;
        if (!tab->outside && ResolveTabDrop(e.x, e.y, &pane, &index)) {
            tab->dropPane = pane;
            tab->dropIndex = index;
            // Draw the insertion caret on the boundary this slot means:
            // the leading edge of tab `index`, or the trailing edge of the
            // one before it when inserting at the end. Which edge that is
            // follows the bar's orientation.
            const bool vertical = (app_.tabBarPosition() == TabBarPosition::Left);
            for (const Region& candidate : regions_) {
                if (candidate.kind != Hit::TabItem || candidate.pane != pane) continue;
                const RectF& box = candidate.rect;
                if (candidate.index == index) {
                    tab->marker = vertical ? RectF{ box.l, box.t - 1.0f, box.r, box.t + 2.0f }
                                           : RectF{ box.l - 1.0f, box.t, box.l + 2.0f, box.b };
                } else if (candidate.index == index - 1 && tab->marker.empty()) {
                    tab->marker = vertical ? RectF{ box.l, box.b - 2.0f, box.r, box.b + 1.0f }
                                           : RectF{ box.r - 2.0f, box.t, box.r + 1.0f, box.b };
                }
            }
        }
        return true;
    }

    // The reorder drags all answer the same way. Off their own rows - past the
    // chips, over another section, on the bare sidebar - nothing is proposed,
    // and letting go there leaves the order exactly as it was.
    if (ReorderDrag* drag = std::get_if<ReorderDrag>(&drag_.what); drag && drag->started) {
        ProposeReorder(*drag, e.x, e.y);
        return true;
    }

    const Region* region = Pick(e.x, e.y);

    int shape = 0;
    if (region && region->kind == Hit::Splitter) {
        shape = (region->node->kind == SplitNode::Kind::LeftRight) ? 2 : 3;
    } else if (region && (region->kind == Hit::ColumnEdge || region->kind == Hit::TabBarEdge)) {
        // 分割線と同じ形。掴めば横に動くもの、というのは同じ話なので。
        shape = 2;
    }
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
    } else {
        redraw.cancel();
    }
    return false;
}

// 離したとき。押しただけで終わったものが «クリック» の意味に落ちるのがここ。
bool AppUi::OnRelease(const MouseEvent&) {
    // 掴んでいる間マウスを占有するもの ─ 離せば終わり、それ以上の意味は無い。
    if (std::holds_alternative<SplitterDrag>(drag_.what) ||
        std::holds_alternative<ColumnWidthDrag>(drag_.what) ||
        std::holds_alternative<TabBarWidthDrag>(drag_.what)) {
        CancelDrag();
        return true;
    }

    if (const TabDrag* tab = std::get_if<TabDrag>(&drag_.what)) {
        if (tab->started) {
            FinishTabDrag(*tab);
            return true;
        }
        CancelDrag();
        return false;
    }

    if (const ReorderDrag* drag = std::get_if<ReorderDrag>(&drag_.what)) {
        if (drag->started) {
            FinishReorder(*drag);
            return true;
        }
        // 動かなかったので、押しただけ ─ その種別ごとの «クリック» の意味になる。
        const ReorderKind kind = drag->kind;
        const int index = drag->index;
        const SidebarSection section = drag->section;
        const std::string path = drag->pendingPath;
        const bool newTab = drag->pendingNewTab;
        CancelDrag();
        switch (kind) {
            case ReorderKind::Column:
                // 見出しを押しただけ ─ その列で並べ替える。
                SortByColumn(index);
                return true;
            case ReorderKind::Sidebar:
                if (!path.empty()) app_.OpenPath(path, newTab);
                return true;
            case ReorderKind::Section:
                // A heading that was pressed and let go is a fold, not a move.
                app_.ToggleSidebarSection(section);
                return true;
            case ReorderKind::Session:
                // 押した瞬間にもう切り替わっている（`Hit::SessionChip`）ので、
                // 離すことに残っている意味は無い。
                return false;
        }
        return false;
    }

    const bool wasMarquee = std::holds_alternative<MarqueeDrag>(drag_.what);
    // Never moved far enough to be a drag, so the press on an already
    // marked row was the plain click it looked like: it means that row and
    // nothing else.
    const bool unmark = std::holds_alternative<FileDrag>(drag_.what) && pendingUnmark_;
    CancelDrag();
    if (unmark) {
        if (Tab* t = app_.workspace().focusedTab()) t->ClearMarks();
        return true;
    }
    return wasMarquee;
}

bool AppUi::OnWheel(const MouseEvent& e) {
    const Region* region = Pick(e.x, e.y);
    // The settings panel holds every row it has, so there is nothing to
    // scroll - but the list behind it must not scroll either.
    if (app_.settingsEditor().visible()) return true;
    if (app_.placePicker().visible()) {
        app_.placePicker().Scroll(static_cast<int>(-e.wheel * 3.0f));
        return true;
    }
    if (app_.commandPalette().visible()) {
        app_.commandPalette().Scroll(static_cast<int>(-e.wheel * 3.0f));
        return true;
    }
    if (app_.keyEditor().visible()) {
        app_.keyEditor().Scroll(static_cast<int>(-e.wheel * 3.0f));
        return true;
    }
    if (app_.keyHelpVisible()) {
        // The way to the rows a small window pushed off the bottom. The upper
        // bound belongs to the paint, which is the only place that knows how
        // many rows a column ended up with.
        keyHelpScroll_ = std::max(0, keyHelpScroll_ - static_cast<int>(e.wheel * 3.0f));
        return true;
    }
    if (region && sidebarRect_.contains(e.x, e.y)) {
        const float maxScroll = std::max(0.0f, sidebarContent_ - sidebarRect_.h());
        sidebarScroll_ = std::clamp(sidebarScroll_ - e.wheel * 60.0f, 0.0f, maxScroll);
        return true;
    }
    // Over the tab bar, and the bar has rows it is not showing: the wheel
    // moves those into view rather than the listing behind it. Only then -
    // a bar with everything on screen has nothing to answer with, so the
    // wheel goes on doing what it has always done there and scrolls the list.
    if (region && region->pane && IsTabBarHit(region->kind) &&
        region->pane->viewport.tabRows > region->pane->viewport.tabRowsPerPage) {
        Pane& pane = *region->pane;
        // A notch is worth three tabs either way: a row of the vertical bar
        // holds one tab, a row of the horizontal bar holds a screenful.
        const int step = (app_.tabBarPosition() == TabBarPosition::Left) ? 3 : 1;
        const int wanted =
            pane.tabScroll - static_cast<int>(std::lround(e.wheel * static_cast<float>(step)));
        pane.tabScroll = std::clamp(wanted, 0, pane.viewport.tabRows - pane.viewport.tabRowsPerPage);
        // The wheel is an answer to "show me somewhere else", so it takes
        // over from the active tab until that changes again.
        pane.tabScrollFor = pane.active;
        return true;
    }
    if (region && region->pane) {
        ScrollPane(region->pane, -e.wheel * app_.theme().rowHeight * 3.0f);
        return true;
    }
    return false;
}

// 押したとき。オーバーレイが出ていればそちらが先に全部を受ける。
bool AppUi::OnPress(const MouseEvent& e) {
    if (app_.settingsEditor().visible()) return HandleSettingsClick(e);
    if (app_.keyEditor().visible()) return HandleKeySettingsClick(e);
    if (app_.placePicker().visible()) return HandlePlaceClick(e);
    if (app_.commandPalette().visible()) return HandlePaletteClick(e);

    if (app_.keyHelpVisible()) {
        app_.Execute(Cmd::ShowKeyHelp);
        return true;
    }

    const Region* region = Pick(e.x, e.y);

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
                ReorderDrag drag;
                drag.kind = ReorderKind::Session;
                drag.index = region->index;
                drag_.start = { e.x, e.y };
                drag_.what = std::move(drag);
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
            ReorderDrag drag;
            drag.kind = ReorderKind::Section;
            drag.section = region->section;
            drag.index =
                (it == order.end()) ? -1 : static_cast<int>(std::distance(order.begin(), it));
            drag_.start = { e.x, e.y };
            drag_.what = std::move(drag);
            return true;
        }

        case Hit::SidebarItem: {
            const bool newTab = (e.mods & kModCtrl) != 0 || e.button == 2;
            if (e.button != 0) {
                // Only the left button can start a reorder, so the middle click
                // has nothing to wait for.
                app_.OpenPath(PathIn(region), newTab);
                return true;
            }
            // Arm a possible reorder. The folder opens on the release, not here:
            // navigating on the press would mean every drag also walked away
            // from the folder on screen.
            ReorderDrag drag;
            drag.kind = ReorderKind::Sidebar;
            drag.section = region->section;
            drag.index = region->index;
            drag.pendingPath = PathIn(region);
            drag.pendingNewTab = newTab;
            drag_.start = { e.x, e.y };
            drag_.what = std::move(drag);
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
                    TabDrag drag;
                    drag.pane = region->pane;
                    drag.index = region->index;
                    drag_.start = { e.x, e.y };
                    drag_.what = drag;
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
            app_.OpenPath(PathIn(region), (e.mods & kModCtrl) != 0);
            return true;

        case Hit::ColumnHeader: {
            app_.FocusPane(region->pane);
            if (e.button != 0) return true;
            // 並べ替えるのは離したとき。押した瞬間に並べ替えると、動かすつもりで
            // 掴んだ見出しが必ず一覧を並べ直す ─ タブのチップが押した瞬間に
            // 切り替わってよいのは、それが取り返しのつく答えだから。
            ReorderDrag drag;
            drag.kind = ReorderKind::Column;
            drag.index = region->index;
            drag_.start = { e.x, e.y };
            drag_.what = std::move(drag);
            return true;
        }

        case Hit::ColumnEdge: {
            app_.FocusPane(region->pane);
            if (e.button != 0) return true;
            // 縁のダブルクリックはその列の幅を既定へ。掴む場所がそのまま «戻す»
            // 場所でもある、というのはどの一覧でも同じ読み方で、しかも 1 回目の
            // 押下は幅を動かしていない（動かすのはドラッグのほう）。
            if (e.clicks >= 2) {
                app_.ResetColumnWidths(region->index);
                return true;
            }
            const RectF box = ColumnHeaderRect(region->pane, region->index);
            if (box.empty()) return true;
            drag_.start = { e.x, e.y };
            drag_.what = ColumnWidthDrag{ region->index, box.r };
            return true;
        }

        case Hit::TabBarEdge: {
            app_.FocusPane(region->pane);
            if (e.button != 0) return true;
            // 縁のダブルクリックで既定の幅へ。掴む場所がそのまま «戻す» 場所、
            // というのは列の縁と同じ読み方で、1 回目の押下は幅を動かしていない。
            if (e.clicks >= 2) {
                app_.ResetTabBarWidth();
                return true;
            }
            const RectF bar = TabBarRect(region->pane);
            if (bar.empty()) return true;
            // ペインの右端は一覧の右端。バーの左端からそこまでがペインの幅で、
            // レイアウトが幅を止めるのはその半分。
            const RectF list = region->pane ? region->pane->viewport.listArea : RectF{};
            drag_.start = { e.x, e.y };
            drag_.what =
                TabBarWidthDrag{ bar.l, list.empty() ? 0.0f : (list.r - bar.l) * 0.5f };
            return true;
        }

        case Hit::GroupRow: {
            app_.FocusPane(region->pane);
            // 見出しはその塊そのものなので、押せば塊が丸ごと選ばれる。カーソルは
            // 塊の先頭の項目へ ─ 見出しの上には止まらない。
            Tab* tab = region->pane ? region->pane->activeTab() : nullptr;
            const Tab::Group* group = tab ? tab->GroupAt(region->index) : nullptr;
            if (!tab || !group) return true;
            if ((e.mods & kModCtrl) == 0) tab->ClearMarks();
            tab->MarkRange(group->firstRow + 1, group->firstRow + group->count, true);
            tab->cursor = tab->SkipGroupRows(region->index, 1);
            tab->ResetAnchor();
            app_.EnsureCursorVisible();
            // 右ボタンは行と同じ ─ 選んだ相手についてのメニューが、選んだ直後に出る。
            if (e.button == 1) {
                app_.ShowContextMenuAt(e.screenX, e.screenY, (e.mods & kModShift) != 0);
            }
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
            drag_.start = { e.x, e.y };
            drag_.what = SplitterDrag{
                region->node, (region->node->kind == SplitNode::Kind::LeftRight) ? e.x : e.y,
                region->node->ratio
            };
            return true;

        case Hit::ListRow: {
            HandleListClick(*region, e);
            if (e.button == 0 && e.clicks == 1) {
                // Arm a possible file drag out of the selection.
                drag_.start = { e.x, e.y };
                drag_.what = FileDrag{};
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
            return true;

        default:
            break;
    }
    return false;
}

}  // namespace kite::ui
