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
        const int under = std::clamp(static_cast<int>(std::floor(contentY / rowH)), 0, rows - 1);
        // 見出しの上には止まらない。掃いている向きへ 1 つ越える。
        tab->cursor = tab->SkipGroupRows(under, contentY >= marqueeAnchorY_ ? 1 : -1);
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
        dragTabPane_->ReorderTab(dragTabIndex_, LiftedTarget(dragTabIndex_, dropTabIndex_));
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

void AppUi::FinishColumnDrag() {
    if (dragColumnIndex_ > 0 && dropColumnIndex_ > 0) {
        app_.MoveColumn(dragColumnIndex_, LiftedTarget(dragColumnIndex_, dropColumnIndex_));
    }
    CancelDrag();
    app_.host().Invalidate();
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

void AppUi::FinishSessionDrag() {
    if (dragSessionIndex_ >= 0 && dropSessionIndex_ >= 0) {
        app_.MoveSession(dragSessionIndex_, LiftedTarget(dragSessionIndex_, dropSessionIndex_));
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
        app_.MoveSidebarItem(dragSidebarSection_, dragSidebarIndex_,
                             LiftedTarget(dragSidebarIndex_, dropSidebarIndex_));
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
        app_.MoveSidebarSection(dragSectionIndex_,
                                LiftedTarget(dragSectionIndex_, dropSectionIndex_));
    }
    CancelDrag();
    app_.host().Invalidate();
}

void AppUi::CancelDrag() {
    drag_ = Drag::None;
    pendingUnmark_ = false;
    dragColumnIndex_ = -1;
    dropColumnIndex_ = -1;
    dropColumnMarker_ = {};
    resizeColumnIndex_ = -1;
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

std::string AppUi::DropTargetAt(float x, float y) const { return DropTargetIn(Pick(x, y)); }

std::string AppUi::DropTargetIn(const Region* region) const {
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

    if (drag_ == Drag::ColumnWidth && resizeColumnIndex_ > 0) {
        if (e.type == MouseEvent::Type::Move) {
            // 掴んでいるのは列の左端で、右端は動かない（右にある列は何も変わらない）。
            // だから幅は差でそのまま出る ─ 線はポインタにぴったり付いてくる。
            app_.SetColumnWidth(resizeColumnIndex_, resizeColumnRight_ - e.x);
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
        } else if (drag_ == Drag::PendingColumn && moved > kDragThreshold) {
            // 名前の列は動かせない。掴んだままでも並べ替えの答えは «押した» ままなので、
            // 離せば今までどおり名前順になる。
            if (dragColumnIndex_ > 0) drag_ = Drag::Column;
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

        // The three list drags all answer the same way. Off their own rows -
        // past the chips, over another section, on the bare sidebar - nothing is
        // proposed, and letting go there leaves the order exactly as it was.
        const auto propose = [&](bool (AppUi::*resolve)(float, float, int*, RectF*) const,
                                 int& index, RectF& marker) {
            int slot = -1;
            RectF edge{};
            const bool found = (this->*resolve)(e.x, e.y, &slot, &edge);
            index = found ? slot : -1;
            marker = found ? edge : RectF{};
            app_.host().Invalidate();
        };

        if (drag_ == Drag::Session) {
            propose(&AppUi::ResolveSessionDrop, dropSessionIndex_, dropSessionMarker_);
            return true;
        }
        if (drag_ == Drag::Column) {
            propose(&AppUi::ResolveColumnDrop, dropColumnIndex_, dropColumnMarker_);
            return true;
        }
        if (drag_ == Drag::Section) {
            propose(&AppUi::ResolveSectionDrop, dropSectionIndex_, dropSectionMarker_);
            return true;
        }
        if (drag_ == Drag::Sidebar) {
            propose(&AppUi::ResolveSidebarDrop, dropSidebarIndex_, dropSidebarMarker_);
            return true;
        }

        int shape = 0;
        if (region && region->kind == Hit::Splitter) {
            shape = (region->node->kind == SplitNode::Kind::LeftRight) ? 2 : 3;
        } else if (region && region->kind == Hit::ColumnEdge) {
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
        if (drag_ == Drag::Column) {
            FinishColumnDrag();
            return true;
        }
        if (drag_ == Drag::PendingColumn) {
            // 動かなかったので、見出しを押しただけ ─ その列で並べ替える。
            const int index = dragColumnIndex_;
            CancelDrag();
            SortByColumn(index);
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
            if (e.button != 0) return true;
            // 並べ替えるのは離したとき。押した瞬間に並べ替えると、動かすつもりで
            // 掴んだ見出しが必ず一覧を並べ直す ─ タブのチップが押した瞬間に
            // 切り替わってよいのは、それが取り返しのつく答えだから。
            drag_ = Drag::PendingColumn;
            dragColumnIndex_ = region->index;
            dragStartX_ = e.x;
            dragStartY_ = e.y;
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
            drag_ = Drag::ColumnWidth;
            resizeColumnIndex_ = region->index;
            resizeColumnRight_ = box.r;
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
            app_.host().Invalidate();
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
