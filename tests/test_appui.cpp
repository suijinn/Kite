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
