// Hit testing and the highlight that follows the pointer. AppUi lives above the
// abstract Renderer, so a frame can be inspected without a window: the fake
// renderer keeps the rectangles that were filled and this asks where they were.
#include "Fakes.h"
#include "TestFramework.h"
#include "ui/AppUi.h"

using namespace kite;

namespace {

// Everything a UI test needs, already settled on C:\home.
struct Fixture {
    test::FakeFileSystem files;
    test::FakeShell shell;
    test::FakeHost host;
    App app{ files, shell, host };
    ui::AppUi ui{ app };
    test::FakeRenderer renderer;

    Fixture() {
        test::ResetFakePlatform();
        test::PopulateStandardTree(files);
        app.Init({});
        test::PumpUntilSettled(app);
    }

    void Paint() {
        renderer.Clear();
        ui.Paint(renderer);
    }

    void Move(float x, float y) {
        ui::MouseEvent e;
        e.type = ui::MouseEvent::Type::Move;
        e.x = x;
        e.y = y;
        ui.OnMouse(e);
    }

    void Leave() {
        ui::MouseEvent e;
        e.type = ui::MouseEvent::Type::Leave;
        ui.OnMouse(e);
    }

    void Press(float x, float y, uint8_t mods = 0, int button = 0) {
        ui::MouseEvent e;
        e.type = ui::MouseEvent::Type::Down;
        e.x = x;
        e.y = y;
        e.button = button;
        e.mods = mods;
        ui.OnMouse(e);
    }

    // A move with the left button held, which is what a sweep is made of.
    void Drag(float x, float y) {
        ui::MouseEvent e;
        e.type = ui::MouseEvent::Type::Move;
        e.x = x;
        e.y = y;
        e.buttons = ui::kButtonLeft;
        ui.OnMouse(e);
    }

    void Release(float x, float y) {
        ui::MouseEvent e;
        e.type = ui::MouseEvent::Type::Up;
        e.x = x;
        e.y = y;
        ui.OnMouse(e);
    }

    // Press and let go without moving. Sidebar items act on the release, so a
    // bare Press is a grab, not a click.
    void Click(float x, float y, uint8_t mods = 0, int button = 0) {
        Press(x, y, mods, button);
        Release(x, y);
    }

    Tab* tab() { return app.workspace().focusedTab(); }

    // Well below the last row: the empty part of the list, where a band starts.
    PointF EmptyPoint() const {
        const Theme& th = app.theme();
        return { th.sidebarWidth + 60.0f, listTop() + th.rowHeight * 9.0f };
    }

    // Top of the first list row: the bars above it are all fixed height.
    float listTop() const {
        const Theme& th = app.theme();
        return th.sessionBarHeight + th.tabBarHeight + th.pathBarHeight + th.headerHeight;
    }

    // A point in the middle of list row `index`, well clear of the sidebar.
    PointF RowPoint(int index) const {
        const Theme& th = app.theme();
        return { th.sidebarWidth + 60.0f,
                 listTop() + th.rowHeight * (static_cast<float>(index) + 0.5f) };
    }
};

}  // namespace

KITE_TEST(appui, nothing_is_hovered_before_the_pointer_arrives) {
    Fixture f;
    f.Paint();
    KITE_EXPECT_EQ(f.renderer.CountFills(f.app.theme().rowHover), 0);
}

KITE_TEST(appui, the_row_under_the_pointer_is_lit) {
    Fixture f;
    f.Paint();

    const PointF p = f.RowPoint(1);
    f.Move(p.x, p.y);
    f.Paint();

    const std::vector<test::FakeRenderer::Fill> lit =
        f.renderer.FillsAt(f.app.theme().rowHover, p.x, p.y);
    KITE_EXPECT_EQ(lit.size(), size_t{ 1 });
    KITE_EXPECT_NEAR(lit[0].rect.h(), f.app.theme().rowHeight, 0.01f);
    KITE_EXPECT_NEAR(lit[0].rect.t, f.listTop() + f.app.theme().rowHeight, 0.01f);

    // And only that row.
    KITE_EXPECT_EQ(f.renderer.CountFills(f.app.theme().rowHover), 1);
}

KITE_TEST(appui, the_highlight_goes_when_the_pointer_leaves_the_window) {
    Fixture f;
    const PointF p = f.RowPoint(2);
    f.Move(p.x, p.y);
    f.Paint();
    KITE_EXPECT_EQ(f.renderer.CountFills(f.app.theme().rowHover), 1);

    f.Leave();
    f.Paint();
    KITE_EXPECT_EQ(f.renderer.CountFills(f.app.theme().rowHover), 0);
}

// The pointer sitting still while the list moves under it is the case a
// remembered row index gets wrong: no mouse event says the content changed.
KITE_TEST(appui, the_highlight_stays_on_the_row_the_pointer_is_on_after_scrolling) {
    Fixture f;
    f.Paint();

    const PointF p = f.RowPoint(3);
    f.Move(p.x, p.y);
    f.Paint();

    ui::MouseEvent wheel;
    wheel.type = ui::MouseEvent::Type::Wheel;
    wheel.x = p.x;
    wheel.y = p.y;
    wheel.wheel = -1.0f;
    f.ui.OnMouse(wheel);
    f.Paint();

    const std::vector<test::FakeRenderer::Fill> lit =
        f.renderer.FillsAt(f.app.theme().rowHover, p.x, p.y);
    KITE_EXPECT_EQ(lit.size(), size_t{ 1 });
}

// A row that is already selected still has to answer "is this the one I am
// about to click", so the wash goes over the selection rather than under it.
KITE_TEST(appui, a_selected_row_is_still_lit_under_the_pointer) {
    Fixture f;
    f.Paint();
    f.app.Execute(Cmd::SelectAll);

    const PointF p = f.RowPoint(1);
    f.Move(p.x, p.y);
    f.Paint();

    KITE_EXPECT_EQ(f.renderer.FillsAt(f.app.theme().rowHover, p.x, p.y).size(), size_t{ 1 });
}

// Whatever is behind the shortcut sheet cannot be clicked, so it must not look
// pointable either.
KITE_TEST(appui, nothing_behind_an_overlay_is_lit) {
    Fixture f;
    const PointF p = f.RowPoint(1);
    f.Move(p.x, p.y);
    f.Paint();
    KITE_EXPECT_EQ(f.renderer.CountFills(f.app.theme().rowHover), 1);

    f.app.Execute(Cmd::ShowKeyHelp);
    f.Paint();
    KITE_EXPECT_EQ(f.renderer.CountFills(f.app.theme().rowHover), 0);
}

KITE_TEST(appui, the_sidebar_lights_under_the_pointer_too) {
    Fixture f;
    f.Paint();

    // First entry below the "quick access" heading; the sidebar starts 4 DIP
    // below the session bar and the heading takes one row.
    const Theme& th = f.app.theme();
    const float x = 60.0f;
    const float y = th.sessionBarHeight + 4.0f + th.rowHeight * 1.5f;
    f.Move(x, y);
    f.Paint();

    KITE_EXPECT_EQ(f.renderer.FillsAt(th.rowHover, x, y).size(), size_t{ 1 });
}

// --- right-click -------------------------------------------------------------

// The empty space belongs to the folder, not to the row the cursor was left on,
// and Shift asks for the extended verbs - the same rule as on a row. That flag
// was inverted here, so the extended menu came up on a plain right-click and
// Shift produced the plain one.
KITE_TEST(appui, right_clicking_the_empty_space_asks_the_folder_for_its_background_menu) {
    Fixture f;
    f.Paint();
    const PointF p = f.EmptyPoint();

    f.Press(p.x, p.y, 0, 1);
    KITE_EXPECT_EQ(f.shell.contextMenuCalls, 1);
    KITE_EXPECT(f.shell.lastContextMenuBackground);
    KITE_EXPECT_EQ(f.shell.lastContextMenuPaths.size(), size_t{ 1 });
    KITE_EXPECT_EQ(f.shell.lastContextMenuPaths.front(), f.tab()->path);
    KITE_EXPECT_FALSE(f.shell.lastContextMenuExtended);

    f.Press(p.x, p.y, kModShift, 1);
    KITE_EXPECT_EQ(f.shell.contextMenuCalls, 2);
    KITE_EXPECT(f.shell.lastContextMenuBackground);
    KITE_EXPECT(f.shell.lastContextMenuExtended);
}

KITE_TEST(appui, a_row_follows_the_same_rule) {
    Fixture f;
    f.Paint();
    const PointF p = f.RowPoint(1);

    f.Press(p.x, p.y, 0, 1);
    KITE_EXPECT_FALSE(f.shell.lastContextMenuExtended);

    f.Press(p.x, p.y, kModShift, 1);
    KITE_EXPECT(f.shell.lastContextMenuExtended);
}

// --- sidebar sections -------------------------------------------------------
//
// The fake sidebar holds one quick-access entry (C:\home, which is also the
// folder on screen, so it is drawn selected), no bookmarks and one drive.

KITE_TEST(appui, clicking_a_sidebar_heading_folds_the_section_and_clicking_it_again_unfolds) {
    Fixture f;
    f.Paint();

    // Folding happens on the release: the press cannot tell a fold from the
    // start of a drag that moves the whole section somewhere else.
    const Theme& th = f.app.theme();
    const float heading = th.sessionBarHeight + 4.0f + th.rowHeight * 0.5f;
    f.Press(60.0f, heading);
    KITE_EXPECT_FALSE(f.app.sidebarCollapsed(SidebarSection::QuickAccess));

    f.Release(60.0f, heading);
    KITE_EXPECT(f.app.sidebarCollapsed(SidebarSection::QuickAccess));

    f.Paint();
    f.Click(60.0f, heading);
    KITE_EXPECT_FALSE(f.app.sidebarCollapsed(SidebarSection::QuickAccess));
}

KITE_TEST(appui, a_folded_section_stops_drawing_its_items) {
    // The quick-access entry is the folder being viewed, so it is the one thing
    // in the sidebar painted with the selection colour.
    Fixture f;
    f.Paint();
    KITE_EXPECT_EQ(f.renderer.CountFills(f.app.theme().rowSelected), 1);

    f.app.ToggleSidebarSection(SidebarSection::QuickAccess);
    f.Paint();
    KITE_EXPECT_EQ(f.renderer.CountFills(f.app.theme().rowSelected), 0);
}

// Folding a section shortens the sidebar, and the item below it moves up into
// the space. The hit test has to follow, or clicks land on what used to be there.
KITE_TEST(appui, folding_a_section_moves_the_ones_below_it_up) {
    Fixture f;
    f.app.ToggleSidebarSection(SidebarSection::QuickAccess);
    f.Paint();

    // With quick access folded, the row after the two remaining gaps and the
    // three headings is the drive - and opening it navigates there.
    const Theme& th = f.app.theme();
    const float y = th.sessionBarHeight + 4.0f + th.rowHeight * 3.5f + 12.0f;
    f.Click(60.0f, y);
    test::PumpUntilSettled(f.app);
    KITE_EXPECT_EQ(f.tab()->path, std::string("C:\\"));
}

// --- reordering by drag -----------------------------------------------------

namespace {

// Three bookmarks, in a known order, with the sidebar's other two sections
// folded away so the rows sit at predictable heights.
struct SidebarFixture : Fixture {
    SidebarFixture() {
        app.ToggleSidebarSection(SidebarSection::QuickAccess);
        app.ToggleSidebarSection(SidebarSection::Drives);
        app.ToggleBookmark("C:\\home\\alpha");
        app.ToggleBookmark("C:\\home\\beta");
        app.ToggleBookmark("C:\\home\\alpha\\nested");
        Paint();
    }

    // Middle of bookmark row `index`: the quick-access heading, a gap, and the
    // bookmarks heading come first.
    float BookmarkY(int index) const {
        const Theme& th = app.theme();
        return th.sessionBarHeight + 4.0f + 6.0f + th.rowHeight * (2.0f + index + 0.5f);
    }

    std::string BookmarkAt(int index) const { return app.workspace().bookmarks[index].path; }
};

}  // namespace

KITE_TEST(appui, dragging_a_bookmark_down_past_another_swaps_them) {
    SidebarFixture f;
    KITE_EXPECT_EQ(f.BookmarkAt(0), std::string("C:\\home\\alpha"));

    f.Press(60.0f, f.BookmarkY(0));
    f.Drag(60.0f, f.BookmarkY(1) + 4.0f);  // past the midpoint of the row below
    f.Release(60.0f, f.BookmarkY(1) + 4.0f);

    KITE_EXPECT_EQ(f.BookmarkAt(0), std::string("C:\\home\\beta"));
    KITE_EXPECT_EQ(f.BookmarkAt(1), std::string("C:\\home\\alpha"));
    KITE_EXPECT_EQ(f.BookmarkAt(2), std::string("C:\\home\\alpha\\nested"));
}

KITE_TEST(appui, dragging_a_bookmark_to_the_top_puts_it_first) {
    SidebarFixture f;
    f.Press(60.0f, f.BookmarkY(2));
    f.Drag(60.0f, f.BookmarkY(0) - 4.0f);
    f.Release(60.0f, f.BookmarkY(0) - 4.0f);

    KITE_EXPECT_EQ(f.BookmarkAt(0), std::string("C:\\home\\alpha\\nested"));
    KITE_EXPECT_EQ(f.BookmarkAt(1), std::string("C:\\home\\alpha"));
}

// The press cannot open the folder, or every reorder would also walk away from
// the folder on screen. A press that never becomes a drag still opens it.
KITE_TEST(appui, a_sidebar_item_opens_on_the_release_not_on_the_press) {
    SidebarFixture f;
    const std::string before = f.tab()->path;

    f.Press(60.0f, f.BookmarkY(1));
    KITE_EXPECT_EQ(f.tab()->path, before);

    f.Release(60.0f, f.BookmarkY(1));
    test::PumpUntilSettled(f.app);
    KITE_EXPECT_EQ(f.tab()->path, std::string("C:\\home\\beta"));
}

KITE_TEST(appui, a_drag_that_ends_on_the_item_it_started_on_neither_moves_nor_opens_it) {
    SidebarFixture f;
    const std::string before = f.tab()->path;

    f.Press(60.0f, f.BookmarkY(1));
    f.Drag(60.0f, f.BookmarkY(1) + 8.0f);
    f.Release(60.0f, f.BookmarkY(1) + 8.0f);
    test::PumpUntilSettled(f.app);

    KITE_EXPECT_EQ(f.BookmarkAt(1), std::string("C:\\home\\beta"));
    KITE_EXPECT_EQ(f.tab()->path, before);
}

// --- reordering the sections themselves --------------------------------------

KITE_TEST(appui, dragging_a_heading_past_another_section_moves_the_whole_block) {
    SidebarFixture f;
    const std::vector<SidebarSection>& order = f.app.sidebarSections();
    KITE_EXPECT_EQ(static_cast<int>(order[0]), static_cast<int>(SidebarSection::QuickAccess));

    // Quick access and drives are folded here, so each is one heading row and
    // the bookmarks block is the heading plus its three rows.
    const Theme& th = f.app.theme();
    const float quickHeading = th.sessionBarHeight + 4.0f + th.rowHeight * 0.5f;
    const float bookmarksHeading = th.sessionBarHeight + 4.0f + 6.0f + th.rowHeight * 1.5f;

    f.Press(60.0f, bookmarksHeading);
    f.Drag(60.0f, quickHeading - 4.0f);  // above the middle of the quick access block
    f.Release(60.0f, quickHeading - 4.0f);

    KITE_EXPECT_EQ(static_cast<int>(order[0]), static_cast<int>(SidebarSection::Bookmarks));
    KITE_EXPECT_EQ(static_cast<int>(order[1]), static_cast<int>(SidebarSection::QuickAccess));
    KITE_EXPECT_EQ(static_cast<int>(order[2]), static_cast<int>(SidebarSection::Drives));
}

// The block is what the drop is measured against, not the heading row: with a
// section open, its heading is nowhere near the middle of the space it takes.
KITE_TEST(appui, a_section_dropped_over_an_open_neighbour_lands_by_that_blocks_middle) {
    SidebarFixture f;
    f.app.ToggleSidebarSection(SidebarSection::QuickAccess);  // unfold it again
    f.Paint();

    // Quick access is a heading and one entry, bookmarks a heading and three,
    // so the drives heading is the seventh row down, past both gaps.
    const Theme& th = f.app.theme();
    const float drivesHeading = th.sessionBarHeight + 4.0f + 12.0f + th.rowHeight * 6.5f;
    // Bookmarks sit second, four rows deep. Its first entry is above the middle
    // of that block, so the drives land in front of the whole thing.
    const float insideBookmarks = th.sessionBarHeight + 4.0f + 6.0f + th.rowHeight * 3.5f;

    f.Press(60.0f, drivesHeading);
    f.Drag(60.0f, insideBookmarks);
    f.Release(60.0f, insideBookmarks);

    const std::vector<SidebarSection>& order = f.app.sidebarSections();
    KITE_EXPECT_EQ(static_cast<int>(order[0]), static_cast<int>(SidebarSection::QuickAccess));
    KITE_EXPECT_EQ(static_cast<int>(order[1]), static_cast<int>(SidebarSection::Drives));
    KITE_EXPECT_EQ(static_cast<int>(order[2]), static_cast<int>(SidebarSection::Bookmarks));
}

KITE_TEST(appui, a_heading_dragged_and_put_back_neither_moves_nor_folds) {
    SidebarFixture f;
    const Theme& th = f.app.theme();
    const float bookmarksHeading = th.sessionBarHeight + 4.0f + 6.0f + th.rowHeight * 1.5f;

    f.Press(60.0f, bookmarksHeading);
    f.Drag(60.0f, bookmarksHeading + 10.0f);
    f.Release(60.0f, bookmarksHeading + 10.0f);

    KITE_EXPECT_EQ(static_cast<int>(f.app.sidebarSections()[1]),
                   static_cast<int>(SidebarSection::Bookmarks));
    // A drag is not a click, so the fold must not have happened on the way.
    KITE_EXPECT_FALSE(f.app.sidebarCollapsed(SidebarSection::Bookmarks));
}

// A bookmark has no place among the drives, so a drag that wanders out of its
// own section proposes nothing rather than dropping into the neighbour.
KITE_TEST(appui, dragging_out_of_the_section_drops_nowhere) {
    Fixture f;
    f.app.ToggleBookmark("C:\\home\\alpha");
    f.app.ToggleBookmark("C:\\home\\beta");
    f.Paint();

    const Theme& th = f.app.theme();
    // Quick access is open here, so the bookmark rows sit below its one entry.
    const float firstBookmark =
        th.sessionBarHeight + 4.0f + 6.0f + th.rowHeight * 3.5f;
    f.Press(60.0f, firstBookmark);
    // Down into the drive rows, well past the bookmarks.
    f.Drag(60.0f, firstBookmark + th.rowHeight * 6.0f);
    f.Release(60.0f, firstBookmark + th.rowHeight * 6.0f);

    KITE_EXPECT_EQ(f.app.workspace().bookmarks[0].path, std::string("C:\\home\\alpha"));
    KITE_EXPECT_EQ(f.app.workspace().bookmarks[1].path, std::string("C:\\home\\beta"));
}

// --- selection band ---------------------------------------------------------
//
// C:\home lists six rows - "..", two folders and three files - in a window with
// room for far more, so everything below them is the empty space a band starts
// from.

KITE_TEST(appui, a_band_swept_up_from_the_empty_space_selects_the_rows_it_crosses) {
    Fixture f;
    f.Paint();

    const PointF start = f.EmptyPoint();
    f.Press(start.x, start.y);
    KITE_EXPECT_EQ(f.tab()->MarkedCount(), 0);

    f.Drag(start.x, f.RowPoint(2).y);
    // Row 2 down to the last one: three files and a folder, "beta" included.
    KITE_EXPECT_EQ(f.tab()->MarkedCount(), 4);

    f.Release(start.x, f.RowPoint(2).y);
    KITE_EXPECT_EQ(f.tab()->MarkedCount(), 4);
}

// The band is laid down from scratch every move, so pulling it back has to let
// go of exactly what it caught.
KITE_TEST(appui, pulling_the_band_back_releases_what_it_caught) {
    Fixture f;
    f.Paint();

    const PointF start = f.EmptyPoint();
    f.Press(start.x, start.y);
    f.Drag(start.x, f.RowPoint(1).y);
    KITE_EXPECT_EQ(f.tab()->MarkedCount(), 5);

    f.Drag(start.x, start.y);
    KITE_EXPECT_EQ(f.tab()->MarkedCount(), 0);
}

// Marks made before the sweep are the base it is drawn on, not something it
// wipes: Ctrl on the band means the same thing it means on a click.
KITE_TEST(appui, ctrl_keeps_what_was_already_selected) {
    Fixture f;
    f.Paint();

    const PointF row = f.RowPoint(1);
    f.Press(row.x, row.y, kModCtrl);
    f.Release(row.x, row.y);
    KITE_EXPECT_EQ(f.tab()->MarkedCount(), 1);

    const PointF start = f.EmptyPoint();
    f.Press(start.x, start.y, kModCtrl);
    f.Drag(start.x, f.RowPoint(4).y);
    // Rows 4 and 5, plus the one picked out by hand.
    KITE_EXPECT_EQ(f.tab()->MarkedCount(), 3);

    // And without Ctrl the same sweep starts from nothing.
    f.Release(start.x, f.RowPoint(4).y);
    f.Press(start.x, start.y);
    f.Drag(start.x, f.RowPoint(4).y);
    KITE_EXPECT_EQ(f.tab()->MarkedCount(), 2);
}

// A press in the empty space is also how the selection is dropped, and a hand
// that shifts a pixel while clicking must not turn that into a selection.
KITE_TEST(appui, a_click_in_the_empty_space_that_barely_moves_selects_nothing) {
    Fixture f;
    f.Paint();
    f.app.Execute(Cmd::SelectAll);
    KITE_EXPECT_NE(f.tab()->MarkedCount(), 0);

    const PointF start = f.EmptyPoint();
    f.Press(start.x, start.y);
    f.Drag(start.x + 2.0f, start.y - 3.0f);
    f.Release(start.x + 2.0f, start.y - 3.0f);

    KITE_EXPECT_EQ(f.tab()->MarkedCount(), 0);
}

// ".." is a way out of the folder, not an item, so a band that runs off the top
// of the list picks up everything except it.
KITE_TEST(appui, the_parent_row_is_never_caught_by_the_band) {
    Fixture f;
    f.Paint();

    const PointF start = f.EmptyPoint();
    f.Press(start.x, start.y);
    f.Drag(start.x, f.listTop() - 40.0f);

    KITE_EXPECT_EQ(f.tab()->MarkedCount(), f.tab()->ItemCount());
    KITE_EXPECT(f.tab()->IsParentRow(0));
}

// The band itself is drawn only while it is being swept. Sampled below the last
// row, where the cursor row's wash - the same colour - cannot reach.
KITE_TEST(appui, the_band_is_drawn_while_it_is_swept_and_gone_afterwards) {
    Fixture f;
    f.Paint();

    const Color band = f.app.theme().accent.alpha(0.16f);
    const PointF start = f.EmptyPoint();
    const float sample = f.listTop() + f.app.theme().rowHeight * 7.0f;

    f.Press(start.x, start.y);
    f.Drag(start.x + 200.0f, f.RowPoint(2).y);
    f.Paint();
    KITE_EXPECT_EQ(f.renderer.FillsAt(band, start.x + 100.0f, sample).size(), size_t{ 1 });

    f.Release(start.x + 200.0f, f.RowPoint(2).y);
    f.Paint();
    KITE_EXPECT_EQ(f.renderer.FillsAt(band, start.x + 100.0f, sample).size(), size_t{ 0 });
}

// Nothing is lit under the pointer during a sweep: the rows it covers already
// say so as selected, and a second wash on one of them says nothing more.
KITE_TEST(appui, no_row_is_lit_while_a_band_is_being_swept) {
    Fixture f;
    f.Paint();

    const PointF start = f.EmptyPoint();
    f.Press(start.x, start.y);
    f.Drag(start.x, f.RowPoint(2).y);
    f.Paint();
    KITE_EXPECT_EQ(f.renderer.CountFills(f.app.theme().rowHover), 0);

    f.Release(start.x, f.RowPoint(2).y);
    f.Move(f.RowPoint(2).x, f.RowPoint(2).y);
    f.Paint();
    KITE_EXPECT_EQ(f.renderer.CountFills(f.app.theme().rowHover), 1);
}

// Moving the mouse redraws the window, so it may only ask for one when the
// answer would differ: a pointer wandering inside one row changes nothing.
KITE_TEST(appui, only_crossing_into_another_item_asks_for_a_repaint) {
    Fixture f;
    f.Paint();

    const PointF p = f.RowPoint(1);
    f.Move(p.x, p.y);
    const int afterFirstMove = f.host.invalidateCount;

    f.Move(p.x + 12.0f, p.y + 2.0f);
    KITE_EXPECT_EQ(f.host.invalidateCount, afterFirstMove);

    f.Move(p.x, p.y + f.app.theme().rowHeight);
    KITE_EXPECT_EQ(f.host.invalidateCount, afterFirstMove + 1);
}
