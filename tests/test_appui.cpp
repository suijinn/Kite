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
