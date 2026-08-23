// Hit testing and the highlight that follows the pointer. AppUi lives above the
// abstract Renderer, so a frame can be inspected without a window: the fake
// renderer keeps the rectangles that were filled and this asks where they were.
#include "Fakes.h"
#include "TestFramework.h"
#include "ui/AppUi.h"
#include "core/base/Format.h"
#include "core/fs/VirtualPath.h"

using namespace kite;

namespace {

// Top of everything below the session bar. The address bar is not a bar of the
// window's own: it is the focused pane's breadcrumb row, inside the pane.
float ContentTop(const Theme& th) { return th.sessionBarHeight; }

// The breadcrumb row of a pane whose top-left is at (left, ContentTop).
RectF PathBarOf(const Theme& th, float left, float right) {
    const float top = ContentTop(th) + th.tabBarHeight;
    return { left, top, right, top + th.pathBarHeight };
}

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

    void Press(float x, float y, uint8_t mods = 0, int button = 0, int clicks = 1) {
        ui::MouseEvent e;
        e.type = ui::MouseEvent::Type::Down;
        e.x = x;
        e.y = y;
        e.button = button;
        e.mods = mods;
        e.clicks = clicks;
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

    // What Windows actually delivers: a plain click, then a second press marked
    // as the double. Anything reading e.clicks has to survive the first one.
    void DoubleClick(float x, float y) {
        Click(x, y);
        Press(x, y, 0, 0, 2);
        Release(x, y);
    }

    // One notch of the wheel. Positive is away from the user, which scrolls up.
    void Wheel(float x, float y, float notches) {
        ui::MouseEvent e;
        e.type = ui::MouseEvent::Type::Wheel;
        e.x = x;
        e.y = y;
        e.wheel = notches;
        ui.OnMouse(e);
    }

    Tab* tab() { return app.workspace().focusedTab(); }

    Pane* pane() { return app.workspace().focusedPane(); }

    // Where a run of text landed this frame. The sidebar is painted before the
    // panes and the panes before the status bar, so the first match for a name
    // that appears in more than one of them is the leftmost one on screen.
    const test::FakeRenderer::Text* TextNamed(const std::string& s) const {
        for (const test::FakeRenderer::Text& t : renderer.texts) {
            if (t.text == s) return &t;
        }
        return nullptr;
    }

    // A run drawn in the bottom bar that has `part` somewhere in it. The bar
    // puts several answers in one string, so an exact match cannot ask about
    // just one of them.
    const test::FakeRenderer::Text* StatusTextWith(const std::string& part) const {
        const float top = renderer.size.h - app.theme().statusBarHeight - 0.01f;
        for (const test::FakeRenderer::Text& t : renderer.texts) {
            if (t.ink.t >= top && t.text.find(part) != std::string::npos) return &t;
        }
        return nullptr;
    }

    // Everything painted in the bottom bar this frame.
    std::vector<test::FakeRenderer::Text> StatusTexts() const {
        const float top = renderer.size.h - app.theme().statusBarHeight - 0.01f;
        std::vector<test::FakeRenderer::Text> out;
        for (const test::FakeRenderer::Text& t : renderer.texts) {
            if (t.ink.t >= top) out.push_back(t);
        }
        return out;
    }

    // Thumbs painted inside the tab bar. The listing has a scrollbar of its own
    // in the same colour, so the ones out in the pane do not count.
    std::vector<RectF> TabBarThumbs() {
        const RectF bar = { paneRect().l, paneRect().t, pane()->listArea.l, paneRect().b };
        std::vector<RectF> out;
        for (const test::FakeRenderer::Fill& fill : renderer.fills) {
            if (!test::FakeRenderer::SameColor(fill.color, app.theme().scrollThumb)) continue;
            if (fill.rect.l >= bar.l && fill.rect.r <= bar.r) out.push_back(fill.rect);
        }
        return out;
    }

    int ThumbsInTabBar() { return static_cast<int>(TabBarThumbs().size()); }

    RectF TabBarThumb() {
        const std::vector<RectF> thumbs = TabBarThumbs();
        return thumbs.empty() ? RectF{} : thumbs.front();
    }

    // The focused pane's own rectangle, as the split tree recorded it last frame.
    // Everything inside the pane is placed from this, so the tests do not have to
    // repeat the arithmetic that puts the pane next to the sidebar.
    RectF paneRect() {
        Session* session = app.workspace().activeSession();
        SplitNode* leaf = session ? session->LeafOf(app.workspace().focusedPane()) : nullptr;
        return leaf ? leaf->rect : RectF{};
    }

    // Move the tab bar to the left of the list. Driven through the settings
    // screen rather than the ini, so the path the user actually takes is the one
    // under test.
    void UseVerticalTabBar() {
        app.Execute(Cmd::ShowSettings);
        SettingsEditor& editor = app.settingsEditor();
        for (size_t i = 0; i < editor.rows().size(); ++i) {
            if (editor.rows()[i].id == SettingId::TabBarPos) {
                editor.SelectRow(static_cast<int>(i));
                break;
            }
        }
        editor.Adjust(1, app.strings());
        app.ApplyPendingSetting();
        app.Execute(Cmd::ShowSettings);  // close
        Paint();
    }

    // Well below the last row: the empty part of the list, where a band starts.
    PointF EmptyPoint() const {
        const Theme& th = app.theme();
        return { th.sidebarWidth + 60.0f, listTop() + th.rowHeight * 9.0f };
    }

    // Top of the first list row: the bars above it are all fixed height.
    float listTop() const {
        const Theme& th = app.theme();
        return ContentTop(th) + th.tabBarHeight + th.pathBarHeight + th.headerHeight;
    }

    // Row `index` of the shortcut editor. Its panel is a fixed shape centred in
    // the window, so where a row lands can be worked out from out here rather
    // than fished back out of the paint.
    RectF KeyRowRect(int index) const {
        const float w = std::clamp(renderer.size.w - 48.0f, 160.0f, 640.0f);
        const float h = std::max(120.0f, renderer.size.h -
                                             std::max(32.0f, renderer.size.h * 0.08f) * 2.0f);
        const float left = std::round(renderer.size.w * 0.5f - w * 0.5f);
        const float top = std::round(renderer.size.h * 0.5f - h * 0.5f);
        // Title, message line, and the rule under them.
        const float bodyTop = top + 38.0f + 20.0f + 4.0f;
        const float rowH = app.theme().rowHeight;
        const float y = bodyTop + 3.0f +
                        static_cast<float>(index - app.keyEditor().scroll()) * rowH;
        return { left + 16.0f, y, left + w - 18.0f, y + rowH };
    }

    // The row's "add a key" button, at its right end.
    PointF KeyAddPoint(int index) const {
        const RectF row = KeyRowRect(index);
        return { row.r - 18.0f, (row.t + row.b) * 0.5f };
    }

    // Which row of the shortcut editor holds a given command.
    int KeyRowOf(Cmd id) const {
        const std::vector<KeyEditor::Row>& rows = app.keyEditor().rows();
        for (size_t i = 0; i < rows.size(); ++i) {
            if (rows[i].cmd == id) return static_cast<int>(i);
        }
        return -1;
    }

    // A point in the middle of list row `index`, well clear of the sidebar.
    PointF RowPoint(int index) const {
        const Theme& th = app.theme();
        return { th.sidebarWidth + 60.0f,
                 listTop() + th.rowHeight * (static_cast<float>(index) + 0.5f) };
    }
};

}  // namespace

// Pressing a row used to drop every other mark before the drag had a chance to
// start, so a selection of six files arrived at the destination as one.
KITE_TEST(appui, dragging_from_a_marked_row_carries_the_whole_selection) {
    Fixture f;
    f.Paint();
    f.Click(f.RowPoint(1).x, f.RowPoint(1).y, kModCtrl);
    f.Click(f.RowPoint(2).x, f.RowPoint(2).y, kModCtrl);
    KITE_EXPECT_EQ(f.tab()->MarkedCount(), 2);

    const PointF start = f.RowPoint(1);
    f.Press(start.x, start.y);
    f.Drag(start.x + 40.0f, start.y + 40.0f);

    KITE_EXPECT_EQ(f.host.lastDrag.size(), size_t{ 2 });
}

// The other half of that bargain: a press that turns out to be a plain click
// still means "just this one", it just has to wait for the release to say so.
KITE_TEST(appui, a_click_on_a_marked_row_drops_the_rest_of_the_selection) {
    Fixture f;
    f.Paint();
    f.Click(f.RowPoint(1).x, f.RowPoint(1).y, kModCtrl);
    f.Click(f.RowPoint(2).x, f.RowPoint(2).y, kModCtrl);

    const PointF p = f.RowPoint(1);
    f.Press(p.x, p.y);
    KITE_EXPECT_EQ(f.tab()->MarkedCount(), 2);  // still whole, in case this is a drag
    f.Release(p.x, p.y);
    KITE_EXPECT_EQ(f.tab()->MarkedCount(), 0);
    KITE_EXPECT_EQ(f.tab()->cursor, 1);
}

// Ctrl+X changes nothing else on screen, so the fade is the only lasting sign
// that the clipboard is holding a move.
KITE_TEST(appui, a_cut_row_is_drawn_faded) {
    Fixture f;
    f.Paint();
    const test::FakeRenderer::Text* before = f.TextNamed("alpha");
    KITE_EXPECT(before != nullptr);
    const float full = before ? before->color.a : 0.0f;

    f.app.Execute(Cmd::Cut);  // the cursor starts on alpha, below ".."
    f.Paint();

    const test::FakeRenderer::Text* cut = f.TextNamed("alpha");
    const test::FakeRenderer::Text* other = f.TextNamed("beta");
    KITE_EXPECT(cut != nullptr);
    KITE_EXPECT(other != nullptr);
    if (cut && other) {
        KITE_EXPECT(cut->color.a < full);
        // And only that row: the fade names one item, not the folder.
        KITE_EXPECT_NEAR(other->color.a, full, 0.01f);
    }
}

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
    const float y = ContentTop(th) + 4.0f + th.rowHeight * 1.5f;
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
    const float heading = ContentTop(th) + 4.0f + th.rowHeight * 0.5f;
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
    const float y = ContentTop(th) + 4.0f + th.rowHeight * 3.5f + 12.0f;
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
        return ContentTop(th) + 4.0f + 6.0f + th.rowHeight * (2.0f + index + 0.5f);
    }

    std::string BookmarkAt(int index) const { return app.workspace().bookmarks[index].path; }
};

}  // namespace

// A bookmark row used to draw a star instead of asking the shell. The star said
// "you pinned this" - which the heading above the rows already says - and said
// nothing about what was pinned, so a synced cloud folder, a repository with
// local changes and an unreachable share were three identical stars.
KITE_TEST(appui, a_bookmark_row_draws_the_shell_icon_of_what_it_points_at) {
    test::FakeIconProvider icons;
    SidebarFixture f;
    f.app.SetIconProvider(&icons);
    f.Paint();

    // Read out of the frame first, so nothing below asks the provider again -
    // IconFor() records every call, and an assertion should not be one of them.
    std::vector<uint32_t> drawn;
    for (int i = 0; i < 3; ++i) {
        KITE_EXPECT(icons.WasAsked(f.BookmarkAt(i)));
        // The icon cell sits at the left of the row, ahead of the label.
        const test::FakeRenderer::Icon* icon = f.renderer.IconAt(14.0f, f.BookmarkY(i));
        KITE_EXPECT(icon != nullptr);
        drawn.push_back(icon ? icon->id : 0u);
    }

    for (uint32_t id : drawn) KITE_EXPECT_NE(id, 0u);
    // Three bookmarks on three folders are three different icons - the whole
    // point of asking per path rather than per kind.
    KITE_EXPECT_NE(drawn[0], drawn[1]);
    KITE_EXPECT_NE(drawn[1], drawn[2]);
    KITE_EXPECT_NE(drawn[0], drawn[2]);
}

// With the shell switched off ([ui] shell_icons = false, or before the first
// answer arrives) there is nothing to ask, and the drawn glyph has to hold the
// exact same space - or every label shifts sideways as the icons land.
KITE_TEST(appui, a_bookmark_row_keeps_its_layout_whether_or_not_the_shell_answers) {
    SidebarFixture f;  // no icon provider installed yet
    f.Paint();
    KITE_EXPECT(f.renderer.icons.empty());
    const test::FakeRenderer::Text* before = f.TextNamed("alpha");
    KITE_EXPECT(before != nullptr);
    const RectF was = before->ink;

    test::FakeIconProvider icons;
    f.app.SetIconProvider(&icons);
    f.Paint();
    KITE_EXPECT_FALSE(f.renderer.icons.empty());

    const test::FakeRenderer::Text* after = f.TextNamed("alpha");
    KITE_EXPECT(after != nullptr);
    if (after) {
        KITE_EXPECT_NEAR(after->ink.l, was.l, 0.01f);
        KITE_EXPECT_NEAR(after->ink.t, was.t, 0.01f);
    }
}

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
    const float quickHeading = ContentTop(th) + 4.0f + th.rowHeight * 0.5f;
    const float bookmarksHeading = ContentTop(th) + 4.0f + 6.0f + th.rowHeight * 1.5f;

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
    const float drivesHeading = ContentTop(th) + 4.0f + 12.0f + th.rowHeight * 6.5f;
    // Bookmarks sit second, four rows deep. Its first entry is above the middle
    // of that block, so the drives land in front of the whole thing.
    const float insideBookmarks = ContentTop(th) + 4.0f + 6.0f + th.rowHeight * 3.5f;

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
    const float bookmarksHeading = ContentTop(th) + 4.0f + 6.0f + th.rowHeight * 1.5f;

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
        ContentTop(th) + 4.0f + 6.0f + th.rowHeight * 3.5f;
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

// The candidate list hangs over the pane rather than pushing it aside: the rows
// underneath must not move while candidates come and go.
KITE_TEST(appui, the_completion_popup_hangs_over_the_list_and_is_clickable) {
    Fixture f;
    f.app.Execute(Cmd::EditPath);
    f.app.OnKey(ParseChord("End"));  // the bar opens selected; add to the path
    for (char c : std::string("\\a")) f.app.OnChar(static_cast<uint32_t>(c));
    test::PumpUntilSettled(f.app);
    KITE_EXPECT_EQ(f.app.pathComplete().matches().size(), size_t{ 1 });

    const Theme& th = f.app.theme();
    f.Paint();

    // It drops out of the bar it is being typed into - the breadcrumb row.
    const float barBottom = PathBarOf(th, 0.0f, f.renderer.size.w).b;
    const PointF row = { th.sidebarWidth + 60.0f, barBottom + th.rowHeight * 0.5f };
    const std::vector<test::FakeRenderer::Fill> popup =
        f.renderer.FillsAt(th.overlayBg, row.x, row.y);
    KITE_EXPECT_EQ(popup.size(), size_t{ 1 });
    KITE_EXPECT_NEAR(popup[0].rect.t, barBottom, 0.01f);
    KITE_EXPECT_NEAR(popup[0].rect.h(), th.rowHeight, 0.01f);

    // The pointer is over the popup, so nothing behind it may look pointable.
    f.Move(row.x, row.y);
    f.Paint();
    KITE_EXPECT_EQ(f.renderer.CountFills(th.rowHover), 1);

    f.Press(row.x, row.y);
    test::PumpUntilSettled(f.app);
    KITE_EXPECT_EQ(f.tab()->path, std::string("C:\\home\\alpha"));
    KITE_EXPECT_EQ(f.app.prompt().kind, PromptKind::None);
}

// One bar in two states. The crumbs and the field take the same row, so going
// from reading the path to editing it moves nothing.
KITE_TEST(appui, the_breadcrumb_row_turns_into_the_address_field) {
    Fixture f;
    const Theme& th = f.app.theme();
    const RectF bar = PathBarOf(th, th.sidebarWidth + 1.0f, f.renderer.size.w);

    auto barPainted = [&](const Color& c) {
        for (const test::FakeRenderer::Fill& fill : f.renderer.fills) {
            if (!test::FakeRenderer::SameColor(fill.color, c)) continue;
            if (std::abs(fill.rect.t - bar.t) < 0.01f && std::abs(fill.rect.b - bar.b) < 0.01f) {
                return true;
            }
        }
        return false;
    };

    // Crumbs to begin with; no field, no caret.
    f.Paint();
    KITE_EXPECT(barPainted(th.tabActiveBg));
    KITE_EXPECT_FALSE(barPainted(th.overlayBg));

    const PointF row = f.RowPoint(1);
    f.Move(row.x, row.y);
    f.Paint();
    const std::vector<test::FakeRenderer::Fill> before =
        f.renderer.FillsAt(th.rowHover, row.x, row.y);
    KITE_EXPECT_EQ(before.size(), size_t{ 1 });
    const float rowTop = before[0].rect.t;

    // The space after the last crumb is Ctrl+L.
    f.Press(bar.r - 60.0f, bar.t + bar.h() * 0.5f);
    KITE_EXPECT_EQ(f.app.prompt().kind, PromptKind::Path);
    KITE_EXPECT_EQ(f.app.prompt().text, f.tab()->path);

    f.Move(row.x, row.y);
    f.Paint();
    KITE_EXPECT(barPainted(th.overlayBg));
    KITE_EXPECT_FALSE(barPainted(th.tabActiveBg));

    // And the list did not move by so much as a pixel.
    const std::vector<test::FakeRenderer::Fill> after =
        f.renderer.FillsAt(th.rowHover, row.x, row.y);
    KITE_EXPECT_EQ(after.size(), size_t{ 1 });
    KITE_EXPECT_NEAR(after[0].rect.t, rowTop, 0.01f);

    // Escaping puts the crumbs back.
    f.app.OnKey(ParseChord("Escape"));
    f.Paint();
    KITE_EXPECT(barPainted(th.tabActiveBg));
}

// A crumb still navigates: only the empty space after them starts editing.
KITE_TEST(appui, a_crumb_click_still_navigates) {
    Fixture f;
    const Theme& th = f.app.theme();
    const RectF bar = PathBarOf(th, th.sidebarWidth + 1.0f, f.renderer.size.w);
    f.Paint();

    // The leftmost crumb is "PC" now - the walk runs through vfs::ParentOf, so
    // the chain no longer stops at the drive.
    f.Press(bar.l + 8.0f, bar.t + bar.h() * 0.5f);
    test::PumpUntilSettled(f.app);
    KITE_EXPECT_EQ(f.app.prompt().kind, PromptKind::None);
    KITE_EXPECT_EQ(f.tab()->path, std::string(vfs::kComputer));
}

// The box an in-place field drew itself into, found by the panel fill under the
// text. Returns an empty rect if the text is not sitting on one.
RectF FieldBoxUnder(Fixture& f, const RectF& ink) {
    const std::vector<test::FakeRenderer::Fill> under =
        f.renderer.FillsAt(f.app.theme().overlayBg, ink.l + 2.0f, ink.center().y);
    return under.empty() ? RectF{} : under.back().rect;
}

// A field spanning the window just above the status bar - which is what the
// prompts that have no single row to sit on still get.
bool BarAtBottom(Fixture& f) {
    const Theme& th = f.app.theme();
    for (const test::FakeRenderer::Fill& fill : f.renderer.fills) {
        if (!test::FakeRenderer::SameColor(fill.color, th.overlayBg)) continue;
        if (fill.rect.l == 0.0f && fill.rect.r == f.renderer.size.w &&
            std::abs(fill.rect.b - (f.renderer.size.h - th.statusBarHeight)) < 0.01f) {
            return true;
        }
    }
    return false;
}

// The prompts that ask about the whole list stay under it, where they always
// were. The ones that name one thing do not: they are drawn on that thing.
KITE_TEST(appui, only_the_prompts_without_a_place_of_their_own_come_up_at_the_bottom) {
    Fixture f;

    f.app.Execute(Cmd::FocusFilter);
    f.Paint();
    KITE_EXPECT(BarAtBottom(f));

    f.app.OnKey(ParseChord("Escape"));
    f.app.Execute(Cmd::DeleteToRecycle);
    f.Paint();
    KITE_EXPECT_EQ(f.app.prompt().kind, PromptKind::ConfirmDelete);
    KITE_EXPECT(BarAtBottom(f));

    f.app.OnKey(ParseChord("Escape"));
    f.app.Execute(Cmd::NewFolder);
    f.Paint();
    KITE_EXPECT_FALSE(BarAtBottom(f));

    f.app.OnKey(ParseChord("Escape"));
    f.app.Execute(Cmd::Rename);
    f.Paint();
    KITE_EXPECT_EQ(f.app.prompt().kind, PromptKind::Rename);
    KITE_EXPECT_FALSE(BarAtBottom(f));

    f.app.OnKey(ParseChord("Escape"));
    f.app.Execute(Cmd::RenameSession);
    f.Paint();
    KITE_EXPECT_FALSE(BarAtBottom(f));
}

// Ctrl+L opens with the path selected, and the wash is drawn behind the text in
// the field's own colour - not the neutral one the list rows use.
KITE_TEST(appui, the_selected_text_is_washed_behind_the_field) {
    Fixture f;
    const Theme& th = f.app.theme();

    f.Paint();
    const int before = f.renderer.CountFills(th.textSelection);

    f.app.Execute(Cmd::EditPath);
    KITE_EXPECT(f.app.prompt().hasSelection());
    f.Paint();
    KITE_EXPECT_EQ(f.renderer.CountFills(th.textSelection), before + 1);

    const RectF bar = PathBarOf(th, th.sidebarWidth + 1.0f, f.renderer.size.w);
    bool inBar = false;
    for (const test::FakeRenderer::Fill& fill : f.renderer.fills) {
        if (!test::FakeRenderer::SameColor(fill.color, th.textSelection)) continue;
        if (fill.rect.t >= bar.t && fill.rect.b <= bar.b && fill.rect.w() > 1.0f) inBar = true;
    }
    KITE_EXPECT(inBar);
}

// A press anywhere else is an answer to something else: the field goes away and
// takes the half-typed path with it.
KITE_TEST(appui, clicking_outside_the_field_folds_the_address_bar) {
    Fixture f;
    const Theme& th = f.app.theme();
    const RectF bar = PathBarOf(th, th.sidebarWidth + 1.0f, f.renderer.size.w);
    const std::string was = f.tab()->path;

    f.app.Execute(Cmd::EditPath);
    f.Paint();

    // Inside the field is not "outside": the bar stays open.
    f.Press(bar.r - 60.0f, bar.t + bar.h() * 0.5f);
    KITE_EXPECT_EQ(f.app.prompt().kind, PromptKind::Path);

    // A row of the list is.
    const PointF row = f.RowPoint(1);
    f.Press(row.x, row.y);
    KITE_EXPECT_EQ(f.app.prompt().kind, PromptKind::None);
    KITE_EXPECT_EQ(f.tab()->path, was);

    // And the click still did what it was for - it selected that row.
    KITE_EXPECT_EQ(f.tab()->cursor, 1);

    // The same goes for the empty space below the rows, and for the sidebar.
    f.app.Execute(Cmd::EditPath);
    f.Paint();
    const PointF empty = f.EmptyPoint();
    f.Press(empty.x, empty.y);
    KITE_EXPECT_EQ(f.app.prompt().kind, PromptKind::None);

    f.app.Execute(Cmd::EditPath);
    f.Paint();
    f.Press(60.0f, ContentTop(th) + 4.0f + th.rowHeight * 1.5f);
    KITE_EXPECT_EQ(f.app.prompt().kind, PromptKind::None);
}

// Picking a candidate with the mouse is not "clicking outside" either.
KITE_TEST(appui, clicking_a_candidate_does_not_count_as_clicking_away) {
    Fixture f;
    const Theme& th = f.app.theme();
    f.app.Execute(Cmd::EditPath);
    f.app.OnKey(ParseChord("End"));
    for (char c : std::string("\\a")) f.app.OnChar(static_cast<uint32_t>(c));
    test::PumpUntilSettled(f.app);
    f.Paint();

    const float barBottom = PathBarOf(th, 0.0f, f.renderer.size.w).b;
    f.Press(th.sidebarWidth + 60.0f, barBottom + th.rowHeight * 0.5f);
    test::PumpUntilSettled(f.app);
    KITE_EXPECT_EQ(f.tab()->path, std::string("C:\\home\\alpha"));
}

// --- wrapping ---------------------------------------------------------------
//
// Both bars used to stop drawing at the right edge, which left the tabs and
// sessions past that point on screen in name only: nothing to click, and no sign
// that anything had been left out.

KITE_TEST(appui, the_tab_bar_stays_one_row_while_the_tabs_fit) {
    Fixture f;
    for (int i = 0; i < 4; ++i) f.app.Execute(Cmd::NewTab);
    test::PumpUntilSettled(f.app);
    f.Paint();

    const Theme& th = f.app.theme();
    Pane* pane = f.app.workspace().focusedPane();
    KITE_EXPECT_NEAR(pane->listArea.t,
                     ContentTop(th) + th.tabBarHeight + th.pathBarHeight + th.headerHeight, 0.01f);
}

KITE_TEST(appui, the_tab_bar_grows_a_row_instead_of_running_off_the_edge) {
    Fixture f;
    // Wide enough for 14 tabs at the floor width, so twenty need a second row.
    for (int i = 0; i < 19; ++i) f.app.Execute(Cmd::NewTab);
    test::PumpUntilSettled(f.app);
    f.Paint();

    const Theme& th = f.app.theme();
    Pane* pane = f.app.workspace().focusedPane();
    KITE_EXPECT_EQ(pane->tabs.size(), size_t{ 20 });
    KITE_EXPECT_NEAR(pane->listArea.t,
                     ContentTop(th) + th.tabBarHeight * 2.0f + th.pathBarHeight + th.headerHeight,
                     0.01f);
}

KITE_TEST(appui, a_tab_on_the_second_row_can_be_clicked) {
    Fixture f;
    for (int i = 0; i < 19; ++i) f.app.Execute(Cmd::NewTab);
    test::PumpUntilSettled(f.app);
    f.Paint();

    const Theme& th = f.app.theme();
    // First tab of the second row: the leftmost column, one bar height down.
    const float x = th.sidebarWidth + 1.0f + 30.0f;
    const float y = ContentTop(th) + th.tabBarHeight * 1.5f;
    f.Click(x, y);
    test::PumpUntilSettled(f.app);

    Pane* pane = f.app.workspace().focusedPane();
    KITE_EXPECT_EQ(pane->active, 14);
}

KITE_TEST(appui, the_session_bar_wraps_and_the_ninth_session_still_switches) {
    Fixture f;
    // Nine and beyond is the interesting part: only eight of them have a
    // Cmd::SessionN, so a chip that dispatched by arithmetic used to run off the
    // end of the command table.
    for (int i = 0; i < 11; ++i) f.app.Execute(Cmd::NewSession);
    test::PumpUntilSettled(f.app);
    f.Paint();

    const Theme& th = f.app.theme();
    KITE_EXPECT_EQ(f.app.workspace().sessions.size(), size_t{ 12 });
    // The top of the pane area is the bottom of however many rows the bar took;
    // the split tree records it every frame.
    KITE_EXPECT(f.app.workspace().activeSession()->root->rect.t > th.sessionBarHeight);

    // The last session is the active one, so its chip is the one wearing the
    // active fill - which is how the test finds a chip without repeating the
    // layout arithmetic.
    const float barBottom = f.app.workspace().activeSession()->root->rect.t;
    RectF chip{};
    int found = 0;
    for (const test::FakeRenderer::Fill& fill : f.renderer.fills) {
        if (fill.rect.b > barBottom) continue;  // the same grey is used down in the list
        if (fill.rect.h() < 4.0f) continue;     // and by the hairline under the bar
        if (test::FakeRenderer::SameColor(fill.color, th.sessionActiveBg)) {
            chip = fill.rect;
            ++found;
        }
    }
    KITE_EXPECT_EQ(found, 1);
    KITE_EXPECT(chip.t >= th.sessionBarHeight);  // wrapped onto a later row

    f.app.Execute(Cmd::Session1);
    KITE_EXPECT_EQ(f.app.workspace().active, 0);
    f.Paint();

    f.Click(chip.center().x, chip.center().y);
    test::PumpUntilSettled(f.app);
    KITE_EXPECT_EQ(f.app.workspace().active, 11);
}

// --- editing in place -------------------------------------------------------
//
// Every field that edits one nameable thing is drawn on that thing. What these
// check is the placement, because that is the whole point: a field at the bottom
// of the window works exactly as well and tells nobody what it is renaming.

KITE_TEST(appui, renaming_puts_the_field_on_the_row_being_renamed) {
    Fixture f;
    f.Paint();

    const std::string name = f.tab()->CursorEntry()->name;
    const test::FakeRenderer::Text* onRow = f.TextNamed(name);
    KITE_EXPECT(onRow != nullptr);
    const RectF was = onRow->ink;

    f.app.Execute(Cmd::Rename);
    f.Paint();

    // The field opens holding the same name, so finding that text again finds the
    // field - and it has to be on the row, not down at the bottom of the window.
    const test::FakeRenderer::Text* inField = f.TextNamed(name);
    KITE_EXPECT(inField != nullptr);
    KITE_EXPECT_NEAR(inField->ink.t, was.t, 3.0f);
    KITE_EXPECT_NEAR(inField->ink.l, was.l, 4.0f);

    // And it reads as a field rather than as text sitting loose on the row.
    KITE_EXPECT_FALSE(FieldBoxUnder(f, inField->ink).empty());
}

KITE_TEST(appui, creating_borrows_a_row_below_the_cursor) {
    Fixture f;
    f.app.Execute(Cmd::CursorDown);
    f.app.Execute(Cmd::CursorDown);
    f.Paint();

    const int cursor = f.tab()->cursor;
    // One row above the cursor and one below it: the borrowed row goes between
    // them, so only the lower one is expected to move.
    const std::string above = f.tab()->EntryAt(cursor - 1)->name;
    const std::string below = f.tab()->EntryAt(cursor + 1)->name;
    const float aboveWas = f.TextNamed(above)->ink.t;
    const float belowWas = f.TextNamed(below)->ink.t;
    const size_t dirsWas = f.files.dirs.size();

    f.app.Execute(Cmd::NewFolder);
    f.Paint();

    KITE_EXPECT_NEAR(f.TextNamed(above)->ink.t, aboveWas, 0.01f);
    KITE_EXPECT_NEAR(f.TextNamed(below)->ink.t, belowWas + f.app.theme().rowHeight, 0.01f);

    // Nothing has been created yet - the row is on loan until Enter.
    KITE_EXPECT_EQ(f.files.dirs.size(), dirsWas);

    // The gap that opened up is a field, and it is empty: an empty box on a
    // borrowed row is what says "type a name here".
    const RectF gap = { f.pane()->listArea.l + 40.0f, belowWas + 4.0f,
                        f.pane()->listArea.l + 60.0f, belowWas + 12.0f };
    KITE_EXPECT_FALSE(FieldBoxUnder(f, gap).empty());
    KITE_EXPECT(f.app.prompt().text.empty());
}

KITE_TEST(appui, the_session_name_is_edited_inside_its_chip) {
    Fixture f;
    f.Paint();

    const Theme& th = f.app.theme();
    const std::string name = f.app.workspace().activeSession()->name;

    f.app.Execute(Cmd::RenameSession);
    f.Paint();

    const test::FakeRenderer::Text* inField = f.TextNamed(name);
    KITE_EXPECT(inField != nullptr);
    // Inside the bar at the top, which is the only place the name it edits is
    // ever written.
    KITE_EXPECT(inField->ink.b <= th.sessionBarHeight + 0.01f);

    const RectF box = FieldBoxUnder(f, inField->ink);
    KITE_EXPECT_FALSE(box.empty());

    // The chip is sized to what is in the field, not to the name on file: a chip
    // that kept its old width would clip the very text being typed into it.
    for (char c : std::string("XXXXXXXXXX")) f.app.OnChar(static_cast<uint32_t>(c));
    f.Paint();
    const test::FakeRenderer::Text* grown = f.TextNamed(name + "XXXXXXXXXX");
    KITE_EXPECT(grown != nullptr);
    KITE_EXPECT(FieldBoxUnder(f, grown->ink).w() > box.w());
}

KITE_TEST(appui, a_press_outside_an_in_place_field_folds_it_and_one_inside_does_not) {
    Fixture f;
    f.Paint();

    const int cursor = f.tab()->cursor;
    const std::string name = f.tab()->CursorEntry()->name;
    f.app.Execute(Cmd::Rename);
    f.Paint();
    const RectF box = FieldBoxUnder(f, f.TextNamed(name)->ink);
    KITE_EXPECT_FALSE(box.empty());

    // Inside: the press is spent on the field. In particular it must not move the
    // cursor, because the cursor is what picks the file being renamed.
    f.Click(box.center().x, box.center().y);
    test::PumpUntilSettled(f.app);
    KITE_EXPECT_EQ(f.app.prompt().kind, PromptKind::Rename);
    KITE_EXPECT_EQ(f.tab()->cursor, cursor);

    // Outside: folded, and nothing renamed on the way out - a mis-click is not a
    // yes. The click still does what it was going to do.
    f.Paint();
    const float otherRow = box.t + f.app.theme().rowHeight * 2.0f;
    f.Click(box.center().x, otherRow);
    test::PumpUntilSettled(f.app);
    KITE_EXPECT_EQ(f.app.prompt().kind, PromptKind::None);
    KITE_EXPECT_NE(f.tab()->cursor, cursor);
    KITE_EXPECT_EQ(f.files.dirs.count("C:\\home\\alpha"), size_t{ 1 });
}

KITE_TEST(appui, double_clicking_a_session_chip_renames_the_session_it_names) {
    Fixture f;
    f.app.Execute(Cmd::NewSession);
    test::PumpUntilSettled(f.app);
    f.Paint();

    const Theme& th = f.app.theme();
    KITE_EXPECT_EQ(f.app.workspace().sessions.size(), size_t{ 2 });
    KITE_EXPECT_EQ(f.app.workspace().active, 1);

    // The chip wearing the active fill is the second session's, which is how the
    // test finds one without repeating the bar's layout arithmetic. Its box does
    // not move when the selection does: the labels are unchanged.
    const float barBottom = f.app.workspace().activeSession()->root->rect.t;
    RectF chip{};
    for (const test::FakeRenderer::Fill& fill : f.renderer.fills) {
        if (fill.rect.b > barBottom) continue;  // the same grey is used in the list
        if (fill.rect.h() < 4.0f) continue;     // and by the hairline under the bar
        if (test::FakeRenderer::SameColor(fill.color, th.sessionActiveBg)) chip = fill.rect;
    }
    KITE_EXPECT_FALSE(chip.empty());

    f.app.Execute(Cmd::Session1);
    KITE_EXPECT_EQ(f.app.workspace().active, 0);
    f.Paint();

    // One click still only switches. Opening a text field every time a session is
    // picked would put an edit under the pointer nobody asked for.
    f.Click(chip.center().x, chip.center().y);
    test::PumpUntilSettled(f.app);
    KITE_EXPECT_EQ(f.app.workspace().active, 1);
    KITE_EXPECT_FALSE(f.app.prompt().active());

    // The second click renames, and it is the chip that was hit - the first click
    // of the pair is what made that session the active one.
    f.app.Execute(Cmd::Session1);
    f.DoubleClick(chip.center().x, chip.center().y);
    test::PumpUntilSettled(f.app);
    KITE_EXPECT_EQ(f.app.workspace().active, 1);
    KITE_EXPECT_EQ(f.app.prompt().kind, PromptKind::SessionName);
    KITE_EXPECT_EQ(f.app.prompt().text, f.app.workspace().sessions[1]->name);
}

// --- reordering the session chips -------------------------------------------
//
// The same grammar as a tab drag, one bar up: press, move, and the caret says
// which side of which chip letting go would put it on.

namespace {

// The chip of session `index`, found by the label it draws rather than by
// repeating the bar's layout arithmetic. The number is part of the label, so
// this is also what proves the chips renumbered after a move.
RectF SessionChipBox(Fixture& f, int index) {
    const std::string label =
        std::to_string(index + 1) + "  " + f.app.workspace().sessions[index]->name;
    const test::FakeRenderer::Text* text = f.TextNamed(label);
    return text ? text->rect : RectF{};
}

}  // namespace

KITE_TEST(appui, dragging_a_session_chip_onto_an_earlier_one_reorders_the_bar) {
    Fixture f;
    f.app.Execute(Cmd::NewSession);
    f.app.Execute(Cmd::NewSession);
    test::PumpUntilSettled(f.app);
    f.Paint();

    KITE_EXPECT_EQ(f.app.workspace().sessions.size(), size_t{ 3 });
    KITE_EXPECT_EQ(f.app.workspace().active, 2);
    const std::string carried = f.app.workspace().sessions[2]->name;

    const RectF from = SessionChipBox(f, 2);
    const RectF onto = SessionChipBox(f, 0);
    KITE_EXPECT_FALSE(from.empty());
    KITE_EXPECT_FALSE(onto.empty());

    f.Press(from.center().x, from.center().y);
    // The left half of the first chip means "before it", which is the only way
    // to reach the front of the bar.
    f.Drag(onto.l + onto.w() * 0.25f, onto.center().y);
    f.Release(onto.l + onto.w() * 0.25f, onto.center().y);
    test::PumpUntilSettled(f.app);

    KITE_EXPECT_EQ(f.app.workspace().sessions[0]->name, carried);
    // Reordering is not switching: the same session is still the active one.
    KITE_EXPECT_EQ(f.app.workspace().active, 0);
    KITE_EXPECT_EQ(f.app.workspace().sessions.size(), size_t{ 3 });
}

KITE_TEST(appui, a_chip_dropped_on_the_far_side_of_its_neighbour_lands_after_it) {
    Fixture f;
    f.app.Execute(Cmd::NewSession);
    f.app.Execute(Cmd::NewSession);
    test::PumpUntilSettled(f.app);
    f.app.Execute(Cmd::Session1);
    test::PumpUntilSettled(f.app);
    f.Paint();

    const std::string carried = f.app.workspace().sessions[0]->name;
    const RectF from = SessionChipBox(f, 0);
    const RectF onto = SessionChipBox(f, 1);

    f.Press(from.center().x, from.center().y);
    f.Drag(onto.r - onto.w() * 0.25f, onto.center().y);
    f.Release(onto.r - onto.w() * 0.25f, onto.center().y);
    test::PumpUntilSettled(f.app);

    // Past the midpoint is "after this one" - and after lifting the chip out,
    // that slot is the one it came from plus one, not plus two.
    KITE_EXPECT_EQ(f.app.workspace().sessions[1]->name, carried);
    KITE_EXPECT_EQ(f.app.workspace().active, 1);
}

KITE_TEST(appui, a_chip_let_go_away_from_the_bar_leaves_the_order_alone) {
    Fixture f;
    f.app.Execute(Cmd::NewSession);
    test::PumpUntilSettled(f.app);
    f.Paint();

    const std::string first = f.app.workspace().sessions[0]->name;
    const RectF from = SessionChipBox(f, 1);

    // Down into the listing, where no chip can answer "which side of what".
    f.Press(from.center().x, from.center().y);
    f.Drag(from.center().x, from.center().y + 300.0f);
    f.Release(from.center().x, from.center().y + 300.0f);
    test::PumpUntilSettled(f.app);

    KITE_EXPECT_EQ(f.app.workspace().sessions[0]->name, first);
    KITE_EXPECT_EQ(f.app.workspace().active, 1);
}

KITE_TEST(appui, a_press_on_a_chip_that_never_moves_is_still_just_a_click) {
    Fixture f;
    f.app.Execute(Cmd::NewSession);
    test::PumpUntilSettled(f.app);
    f.app.Execute(Cmd::Session1);
    f.Paint();

    const std::string first = f.app.workspace().sessions[0]->name;
    const RectF chip = SessionChipBox(f, 1);

    f.Click(chip.center().x, chip.center().y);
    test::PumpUntilSettled(f.app);

    KITE_EXPECT_EQ(f.app.workspace().active, 1);
    KITE_EXPECT_EQ(f.app.workspace().sessions[0]->name, first);
}

// --- the vertical tab bar ---------------------------------------------------
//
// The same layout with the two axes swapped: tabs run down the left of the pane
// instead of across the top of it, and everything that reads a position - the
// hit test, the drop caret, the scroll - has to follow.

KITE_TEST(appui, the_vertical_bar_takes_the_left_of_the_pane_and_nothing_above_the_list) {
    Fixture f;
    f.UseVerticalTabBar();

    const Theme& th = f.app.theme();
    const RectF pane = f.paneRect();
    // The list starts past the column, and no longer below a bar: the path bar
    // is the first thing at the top of what is left.
    KITE_EXPECT_NEAR(f.pane()->listArea.l, pane.l + th.tabBarWidth, 0.01f);
    KITE_EXPECT_NEAR(f.pane()->listArea.t, pane.t + th.pathBarHeight + th.headerHeight, 0.01f);
}

KITE_TEST(appui, a_tab_further_down_the_column_can_be_clicked) {
    Fixture f;
    f.UseVerticalTabBar();
    for (int i = 0; i < 3; ++i) f.app.Execute(Cmd::NewTab);
    test::PumpUntilSettled(f.app);
    f.Paint();

    const Theme& th = f.app.theme();
    const RectF pane = f.paneRect();
    f.Click(pane.l + 40.0f, pane.t + th.tabBarHeight * 2.5f);
    test::PumpUntilSettled(f.app);

    KITE_EXPECT_EQ(f.pane()->active, 2);
}

KITE_TEST(appui, dragging_a_tab_up_the_column_reorders_it) {
    Fixture f;
    f.UseVerticalTabBar();
    f.app.Execute(Cmd::NewTab);
    test::PumpUntilSettled(f.app);
    f.app.OpenPath("C:\\home\\alpha", false);
    test::PumpUntilSettled(f.app);
    f.Paint();
    KITE_EXPECT_EQ(f.pane()->tabs.size(), size_t{ 2 });
    KITE_EXPECT_EQ(f.pane()->tabs[1]->path, std::string("C:\\home\\alpha"));

    // Carry the second tab over the top half of the first: up and down are what
    // "before" and "after" mean here, so the same drag that used to run left
    // along a row now runs up a column.
    const Theme& th = f.app.theme();
    const RectF pane = f.paneRect();
    const float x = pane.l + 40.0f;
    f.Press(x, pane.t + th.tabBarHeight * 1.5f);
    f.Drag(x, pane.t + th.tabBarHeight * 0.25f);
    f.Release(x, pane.t + th.tabBarHeight * 0.25f);
    test::PumpUntilSettled(f.app);

    KITE_EXPECT_EQ(f.pane()->tabs[0]->path, std::string("C:\\home\\alpha"));
}

KITE_TEST(appui, the_column_scrolls_to_the_active_tab_rather_than_running_off_the_bottom) {
    Fixture f;
    f.UseVerticalTabBar();
    // Far more than the column can hold, so the ones at the top are off screen.
    for (int i = 0; i < 39; ++i) f.app.Execute(Cmd::NewTab);
    test::PumpUntilSettled(f.app);
    f.Paint();
    KITE_EXPECT_EQ(f.pane()->tabs.size(), size_t{ 40 });
    KITE_EXPECT_EQ(f.pane()->active, 39);

    // The tab at the top of the column is not the first one any more - it has
    // been scrolled down to the tab that has the keyboard.
    const Theme& th = f.app.theme();
    const RectF pane = f.paneRect();
    f.Click(pane.l + 40.0f, pane.t + th.tabBarHeight * 0.5f);
    test::PumpUntilSettled(f.app);
    KITE_EXPECT(f.pane()->active > 0);
    KITE_EXPECT(f.pane()->active < 39);
}

KITE_TEST(appui, the_wheel_moves_the_column_to_the_tabs_above_and_it_stays_there) {
    Fixture f;
    f.UseVerticalTabBar();
    for (int i = 0; i < 39; ++i) f.app.Execute(Cmd::NewTab);
    test::PumpUntilSettled(f.app);
    f.Paint();

    const Theme& th = f.app.theme();
    const RectF pane = f.paneRect();
    const float x = pane.l + 40.0f;
    const float top = pane.t + th.tabBarHeight * 0.5f;

    // Whatever is at the top of the column before the wheel is touched.
    f.Click(x, top);
    test::PumpUntilSettled(f.app);
    const int before = f.pane()->active;
    f.Paint();

    f.Wheel(x, pane.t + th.tabBarHeight * 3.0f, 1.0f);
    // Two frames, because the point of remembering the position is that the
    // next layout does not quietly put it back where the active tab is.
    f.Paint();
    f.Paint();

    f.Click(x, top);
    test::PumpUntilSettled(f.app);
    KITE_EXPECT(f.pane()->active < before);
}

KITE_TEST(appui, switching_tabs_pulls_the_column_back_to_the_active_one) {
    Fixture f;
    f.UseVerticalTabBar();
    for (int i = 0; i < 39; ++i) f.app.Execute(Cmd::NewTab);
    test::PumpUntilSettled(f.app);
    f.Paint();

    const Theme& th = f.app.theme();
    const RectF pane = f.paneRect();
    const float x = pane.l + 40.0f;
    for (int i = 0; i < 20; ++i) f.Wheel(x, pane.t + th.tabBarHeight * 3.0f, 1.0f);
    f.Paint();
    // Wound right back to the first tab, with the active one off the bottom.
    KITE_EXPECT_EQ(f.pane()->tabScroll, 0);
    KITE_EXPECT(f.pane()->active >= f.pane()->tabRowsPerPage);

    // The wheel only holds until the selection moves; a new tab is a new answer.
    f.app.Execute(Cmd::NewTab);
    test::PumpUntilSettled(f.app);
    f.Paint();
    Pane* p = f.pane();
    KITE_EXPECT(p->tabScroll > 0);
    KITE_EXPECT(p->active >= p->tabScroll);
    KITE_EXPECT(p->active < p->tabScroll + p->tabRowsPerPage);
}

KITE_TEST(appui, the_column_shows_a_thumb_only_when_it_is_holding_tabs_back) {
    Fixture f;
    f.UseVerticalTabBar();
    for (int i = 0; i < 3; ++i) f.app.Execute(Cmd::NewTab);
    test::PumpUntilSettled(f.app);
    f.Paint();
    // Four tabs in a column that holds twenty-odd: nothing is being held back,
    // so a thumb here would be a control for a scroll that cannot happen. (The
    // listing has a scrollbar of its own, hence counting only inside the bar.)
    KITE_EXPECT_EQ(f.ThumbsInTabBar(), 0);

    for (int i = 0; i < 39; ++i) f.app.Execute(Cmd::NewTab);
    test::PumpUntilSettled(f.app);
    f.Paint();
    KITE_EXPECT_EQ(f.ThumbsInTabBar(), 1);
}

KITE_TEST(appui, the_thumb_sits_where_the_column_is_scrolled_to) {
    Fixture f;
    f.UseVerticalTabBar();
    for (int i = 0; i < 39; ++i) f.app.Execute(Cmd::NewTab);
    test::PumpUntilSettled(f.app);
    f.Paint();

    // The last tab is the active one, so the column is at the bottom and so is
    // the thumb.
    const RectF pane = f.paneRect();
    const RectF atBottom = f.TabBarThumb();
    KITE_EXPECT(atBottom.b > pane.t + pane.h() * 0.5f);

    const Theme& th = f.app.theme();
    for (int i = 0; i < 20; ++i) {
        f.Wheel(pane.l + 40.0f, pane.t + th.tabBarHeight * 3.0f, 1.0f);
    }
    f.Paint();
    const RectF atTop = f.TabBarThumb();
    KITE_EXPECT(atTop.t < atBottom.t);
    KITE_EXPECT_NEAR(atTop.t, pane.t + 3.0f, 0.01f);
}

KITE_TEST(appui, nothing_behind_the_settings_screen_is_lit) {
    Fixture f;
    const PointF p = f.RowPoint(1);
    f.Move(p.x, p.y);
    f.Paint();
    KITE_EXPECT_EQ(f.renderer.CountFills(f.app.theme().rowHover), 1);

    f.app.Execute(Cmd::ShowSettings);
    f.Paint();
    KITE_EXPECT_EQ(f.renderer.CountFills(f.app.theme().rowHover), 0);
}

// The shortcut editor's two verbs both need a way in from the mouse. The row
// itself is "replace" - a second click on it arms the same capture Enter does -
// so "add" gets a control of its own rather than a modifier nobody would guess.
KITE_TEST(appui, the_plus_on_a_shortcut_row_adds_a_key_rather_than_replacing_it) {
    Fixture f;
    f.app.Execute(Cmd::ShowKeySettings);
    f.Paint();

    // "New folder" has exactly one chord by default, so two afterwards can only
    // mean the old one survived.
    const int index = f.KeyRowOf(Cmd::NewFolder);
    KITE_EXPECT(index >= 0);
    f.app.keyEditor().SelectRow(index);
    f.Paint();

    const PointF plus = f.KeyAddPoint(index);
    f.Press(plus.x, plus.y);
    KITE_EXPECT(f.app.keyEditor().capturing());

    f.app.OnKey(ParseChord("Ctrl+Alt+Shift+F9"));
    KITE_EXPECT_EQ(f.app.keys().ChordsFor(Cmd::NewFolder).size(), size_t{ 2 });
    KITE_EXPECT_EQ(f.app.keys().Lookup(ParseChord("Ctrl+Shift+N")), Cmd::NewFolder);
}

// The plus is a target inside the row, so the rest of the row has to go on
// meaning what it meant: one click selects, and nothing starts capturing.
KITE_TEST(appui, clicking_a_shortcut_row_beside_the_plus_only_selects_it) {
    Fixture f;
    f.app.Execute(Cmd::ShowKeySettings);
    f.Paint();

    // The list opens at the top, so the row clicked has to be one of the ones
    // actually on screen.
    const std::vector<KeyEditor::Row>& rows = f.app.keyEditor().rows();
    int index = -1;
    for (size_t i = 0; i < rows.size(); ++i) {
        if (!rows[i].header && static_cast<int>(i) != f.app.keyEditor().cursor()) {
            index = static_cast<int>(i);
            break;
        }
    }
    KITE_EXPECT(index >= 0);

    const RectF row = f.KeyRowRect(index);
    f.Press(row.l + 20.0f, (row.t + row.b) * 0.5f);

    KITE_EXPECT_EQ(f.app.keyEditor().cursor(), index);
    KITE_EXPECT_FALSE(f.app.keyEditor().capturing());
}

// Picking one chord out of a line that holds several. The pieces are placed by
// measuring, and the fake renderer measures 7 DIP per character, so where each
// one sits is worked out here the same way the paint does it.
KITE_TEST(appui, clicking_a_chord_twice_removes_that_one_binding) {
    Fixture f;
    f.app.Execute(Cmd::ShowKeySettings);
    f.Paint();

    // "Refresh" carries F5 and Ctrl+R by default.
    const int index = f.KeyRowOf(Cmd::Refresh);
    KITE_EXPECT(index >= 0);
    f.app.keyEditor().SelectRow(index);
    f.Paint();

    const RectF row = f.KeyRowRect(index);
    const float chordRight = row.r - 32.0f;  // the plus, and the gap before it
    const float left = chordRight - 7.0f * static_cast<float>(std::string("F5, Ctrl+R").size());
    const PointF f5 = { left + 7.0f, (row.t + row.b) * 0.5f };

    f.Press(f5.x, f5.y);
    KITE_EXPECT_EQ(f.app.keyEditor().chordCursor(), 0);
    // Pointing at it is not removing it: one stray click must not cost a key.
    KITE_EXPECT_EQ(f.app.keys().Lookup(ParseChord("F5")), Cmd::Refresh);

    f.Paint();
    f.Press(f5.x, f5.y);
    KITE_EXPECT_EQ(f.app.keys().Lookup(ParseChord("F5")), Cmd::None);
    KITE_EXPECT_EQ(f.app.keys().Lookup(ParseChord("Ctrl+R")), Cmd::Refresh);
}

// The shortcut sheet in a window too small for it. Three things share its title
// line and every row holds two more, and each of them used to be handed a rect
// running to the far edge - which reads as columns only while there is room for
// columns.
KITE_TEST(appui, nothing_on_the_shortcut_sheet_is_drawn_on_top_of_anything_else) {
    Fixture f;
    f.renderer.size = { 560.0f, 420.0f };
    f.app.Execute(Cmd::ShowKeyHelp);
    f.Paint();

    const std::vector<test::FakeRenderer::Text> sheet =
        f.renderer.TextsAfterFill(f.app.theme().overlayScrim);
    KITE_EXPECT(sheet.size() > 10);

    for (size_t i = 0; i < sheet.size(); ++i) {
        for (size_t j = i + 1; j < sheet.size(); ++j) {
            if (test::FakeRenderer::Overlaps(sheet[i].ink, sheet[j].ink)) {
                KITE_FAIL("\"" + sheet[i].text + "\" and \"" + sheet[j].text + "\" overlap");
            }
        }
    }
}

// Squeezing the leading is how the sheet fits; squeezing it past the height of
// the letters is how it stopped being readable at all.
KITE_TEST(appui, the_shortcut_sheet_keeps_its_rows_a_line_apart) {
    Fixture f;
    f.renderer.size = { 560.0f, 420.0f };
    f.app.Execute(Cmd::ShowKeyHelp);
    f.Paint();

    std::vector<float> tops;
    for (const test::FakeRenderer::Text& t : f.renderer.TextsAfterFill(f.app.theme().overlayScrim)) {
        tops.push_back(t.ink.t);
    }
    std::sort(tops.begin(), tops.end());

    const float least = f.renderer.LineHeight(ui::FontRole::UiSmall) * 0.85f;
    for (size_t i = 1; i < tops.size(); ++i) {
        const float gap = tops[i] - tops[i - 1];
        if (gap > 0.01f) KITE_EXPECT(gap >= least - 0.01f);
    }
}

// What a small window pushes off the bottom is still reachable, and the sheet
// says so rather than simply ending.
KITE_TEST(appui, the_wheel_moves_the_shortcut_sheet_and_not_the_list_behind_it) {
    Fixture f;
    f.renderer.size = { 560.0f, 420.0f };
    f.app.Execute(Cmd::ShowKeyHelp);
    f.Paint();

    KITE_EXPECT(f.renderer.CountFills(f.app.theme().scrollThumb) > 0);
    const size_t before = f.renderer.TextsAfterFill(f.app.theme().overlayScrim).size();
    const float listScroll = f.tab()->scroll;

    f.Wheel(280.0f, 210.0f, -3.0f);
    f.Paint();

    KITE_EXPECT_NEAR(f.tab()->scroll, listScroll, 0.01f);
    // Different rows are on screen now, so the sheet did move.
    const std::vector<test::FakeRenderer::Text> after =
        f.renderer.TextsAfterFill(f.app.theme().overlayScrim);
    KITE_EXPECT(after.size() > 10);
    bool moved = false;
    for (const test::FakeRenderer::Text& t : after) {
        if (t.text == f.app.strings().Get("ui.key_help_title")) continue;
        moved = true;
        break;
    }
    KITE_EXPECT(moved);
    KITE_EXPECT(before > 10);
}

// A full-size window still shows the whole sheet in one go: the scroll is the
// answer to a small window, not a new way of reading it.
KITE_TEST(appui, a_big_window_shows_the_whole_shortcut_sheet_at_once) {
    Fixture f;
    f.renderer.size = { 1600.0f, 1000.0f };
    f.app.Execute(Cmd::ShowKeyHelp);
    f.Paint();

    KITE_EXPECT_EQ(f.renderer.CountFills(f.app.theme().scrollThumb), 0);
}

// The panel is a fraction of the window and the window has no floor, so the
// sheet has to survive being asked for in a space it cannot have.
KITE_TEST(appui, the_shortcut_sheet_survives_a_window_with_no_room_in_it) {
    Fixture f;
    f.renderer.size = { 220.0f, 120.0f };
    f.app.Execute(Cmd::ShowKeyHelp);
    f.Paint();

    for (const test::FakeRenderer::Text& t : f.renderer.TextsAfterFill(f.app.theme().overlayScrim)) {
        KITE_EXPECT(t.ink.r >= t.ink.l);
        KITE_EXPECT(t.ink.b >= t.ink.t);
    }
}

// --- the bookmark list -------------------------------------------------------

namespace {

// Long names and long paths, which is what makes the three-things-on-one-row
// layout of this panel worth checking.
void GiveBookmarks(App& app) {
    app.workspace().bookmarks.clear();
    app.workspace().bookmarks.push_back({ "alpha", "C:\\home\\alpha" });
    app.workspace().bookmarks.push_back({ "beta", "C:\\home\\beta" });
    app.workspace().bookmarks.push_back(
        { "a rather long bookmark name", "C:\\home\\alpha\\nested" });
    for (int i = 4; i <= 12; ++i) {
        app.workspace().bookmarks.push_back(
            { "mark" + std::to_string(i), "C:\\a\\deep\\path\\that\\keeps\\going\\p" +
                                              std::to_string(i) });
    }
}

}  // namespace

// A row carries three things - the name, where it goes, and the number key that
// also reaches it - and each of them used to be handed a rect running to the far
// edge. Rects that overlap put the text on top of each other; nothing shrinks.
KITE_TEST(appui, nothing_on_the_bookmark_list_is_drawn_on_top_of_anything_else) {
    Fixture f;
    GiveBookmarks(f.app);
    f.renderer.size = { 560.0f, 420.0f };
    f.app.Execute(Cmd::ShowPlaces);
    f.Paint();

    const std::vector<test::FakeRenderer::Text> panel =
        f.renderer.TextsAfterFill(f.app.theme().overlayScrim);
    KITE_EXPECT(panel.size() > 5);

    for (size_t i = 0; i < panel.size(); ++i) {
        for (size_t j = i + 1; j < panel.size(); ++j) {
            if (test::FakeRenderer::Overlaps(panel[i].ink, panel[j].ink)) {
                KITE_FAIL("\"" + panel[i].text + "\" and \"" + panel[j].text + "\" overlap");
            }
        }
    }
}

// Narrow enough that the path and the shortcut cannot both fit: what does not
// fit is dropped, not stacked.
KITE_TEST(appui, the_bookmark_list_survives_a_window_with_no_room_in_it) {
    Fixture f;
    GiveBookmarks(f.app);
    f.renderer.size = { 240.0f, 160.0f };
    f.app.Execute(Cmd::ShowPlaces);
    f.Paint();

    const std::vector<test::FakeRenderer::Text> panel =
        f.renderer.TextsAfterFill(f.app.theme().overlayScrim);
    for (size_t i = 0; i < panel.size(); ++i) {
        KITE_EXPECT(panel[i].ink.r >= panel[i].ink.l);
        for (size_t j = i + 1; j < panel.size(); ++j) {
            if (test::FakeRenderer::Overlaps(panel[i].ink, panel[j].ink)) {
                KITE_FAIL("\"" + panel[i].text + "\" and \"" + panel[j].text + "\" overlap");
            }
        }
    }
}

// One click goes, unlike the shortcut editor's rows: whoever pressed a bookmark
// has already decided which one they want.
KITE_TEST(appui, pressing_a_bookmark_row_goes_there_and_closes_the_panel) {
    Fixture f;
    GiveBookmarks(f.app);
    f.renderer.size = { 900.0f, 640.0f };
    f.app.Execute(Cmd::ShowPlaces);
    f.Paint();

    // Down the middle of the panel, which is rows all the way. Pressing outside
    // would also close it, so the proof is that the folder actually changed to
    // one of the bookmarks - C:\home, where this started, is not among them.
    f.Press(450.0f, 320.0f);
    test::PumpUntilSettled(f.app);

    KITE_EXPECT_FALSE(f.app.placePicker().visible());
    bool landed = false;
    for (const Bookmark& b : f.app.workspace().bookmarks) {
        if (b.path == f.tab()->path) landed = true;
    }
    KITE_EXPECT(landed);
}

// Pressing outside is the same as Escape, and the list behind the panel takes
// nothing from that press.
KITE_TEST(appui, pressing_outside_the_bookmark_list_closes_it) {
    Fixture f;
    GiveBookmarks(f.app);
    f.renderer.size = { 900.0f, 640.0f };
    f.app.Execute(Cmd::ShowPlaces);
    f.Paint();

    const std::string before = f.tab()->path;
    f.Press(6.0f, 630.0f);
    test::PumpUntilSettled(f.app);

    KITE_EXPECT_FALSE(f.app.placePicker().visible());
    KITE_EXPECT_EQ(f.tab()->path, before);
}

// The wheel belongs to the panel while it is up, exactly as it does for the
// shortcut editor.
KITE_TEST(appui, the_wheel_moves_the_bookmark_list_and_not_the_list_behind_it) {
    Fixture f;
    GiveBookmarks(f.app);
    f.renderer.size = { 560.0f, 300.0f };
    // Away from the quick access row for the home folder first: the panel opens
    // on the row for the folder being looked at, and starting part-way down would
    // leave nothing to say about which direction the wheel moved it.
    f.app.NavigateFocused("C:\\home\\alpha\\nested");
    test::PumpUntilSettled(f.app);
    f.app.Execute(Cmd::ShowPlaces);
    f.Paint();

    const float listScroll = f.tab()->scroll;
    KITE_EXPECT_EQ(f.app.placePicker().scroll(), 0);

    f.Wheel(280.0f, 150.0f, -3.0f);
    f.Paint();

    KITE_EXPECT_NEAR(f.tab()->scroll, listScroll, 0.01f);
    KITE_EXPECT(f.app.placePicker().scroll() > 0);
}

// The palette's rows carry three things - the label, the group and the chords -
// and the same rule applies as on the bookmark list: what does not fit is dropped,
// never stacked. Rects that overlap put the text on top of each other.
KITE_TEST(appui, nothing_on_the_command_palette_is_drawn_on_top_of_anything_else) {
    Fixture f;
    f.renderer.size = { 560.0f, 420.0f };
    f.app.Execute(Cmd::ShowCommandPalette);
    f.Paint();

    const std::vector<test::FakeRenderer::Text> panel =
        f.renderer.TextsAfterFill(f.app.theme().overlayScrim);
    KITE_EXPECT(panel.size() > 5);

    for (size_t i = 0; i < panel.size(); ++i) {
        for (size_t j = i + 1; j < panel.size(); ++j) {
            if (test::FakeRenderer::Overlaps(panel[i].ink, panel[j].ink)) {
                KITE_FAIL("\"" + panel[i].text + "\" and \"" + panel[j].text + "\" overlap");
            }
        }
    }
}

// Narrow enough that the group and the chords cannot both fit next to the label.
KITE_TEST(appui, the_command_palette_survives_a_window_with_no_room_in_it) {
    Fixture f;
    f.renderer.size = { 240.0f, 160.0f };
    f.app.Execute(Cmd::ShowCommandPalette);
    f.Paint();

    const std::vector<test::FakeRenderer::Text> panel =
        f.renderer.TextsAfterFill(f.app.theme().overlayScrim);
    for (size_t i = 0; i < panel.size(); ++i) {
        KITE_EXPECT(panel[i].ink.r >= panel[i].ink.l);
        for (size_t j = i + 1; j < panel.size(); ++j) {
            if (test::FakeRenderer::Overlaps(panel[i].ink, panel[j].ink)) {
                KITE_FAIL("\"" + panel[i].text + "\" and \"" + panel[j].text + "\" overlap");
            }
        }
    }
}

// One click runs it, the way a bookmark row goes on one click. Filtered down to a
// single command first, so what the press lands on is known - the row is found by
// the label it drew, which is also the proof that the label made it to the screen.
KITE_TEST(appui, pressing_a_command_palette_row_runs_it_and_closes_the_panel) {
    Fixture f;
    f.renderer.size = { 900.0f, 640.0f };
    f.app.Execute(Cmd::ShowCommandPalette);
    for (char c : std::string("view.toggle_hidden")) f.app.OnChar(static_cast<uint32_t>(c));
    f.Paint();

    const bool hidden = f.tab()->view.showHidden;
    const std::string label = f.app.strings().Get("cmd.toggle_hidden");
    bool pressed = false;
    for (const test::FakeRenderer::Text& t : f.renderer.TextsAfterFill(f.app.theme().overlayScrim)) {
        if (t.text != label) continue;
        f.Press(t.ink.l + 2.0f, (t.ink.t + t.ink.b) * 0.5f);
        pressed = true;
        break;
    }
    KITE_EXPECT(pressed);
    test::PumpUntilSettled(f.app);

    KITE_EXPECT_FALSE(f.app.commandPalette().visible());
    KITE_EXPECT_EQ(f.tab()->view.showHidden, !hidden);
}

// Pressing outside is the same as Escape, and nothing behind the panel takes that
// press - least of all a command.
KITE_TEST(appui, pressing_outside_the_command_palette_closes_it) {
    Fixture f;
    f.renderer.size = { 900.0f, 640.0f };
    f.app.Execute(Cmd::ShowCommandPalette);
    f.Paint();

    const std::string before = f.tab()->path;
    const size_t tabs = f.app.workspace().focusedPane()->tabs.size();
    f.Press(6.0f, 630.0f);
    test::PumpUntilSettled(f.app);

    KITE_EXPECT_FALSE(f.app.commandPalette().visible());
    KITE_EXPECT_EQ(f.tab()->path, before);
    KITE_EXPECT_EQ(f.app.workspace().focusedPane()->tabs.size(), tabs);
}

// Typing must not move the panel. Sized to the matches, it shrank towards the
// centre of the window on every keystroke - and the field being typed into moved
// with it, which is the one thing that has to stay still.
KITE_TEST(appui, filtering_the_command_palette_does_not_move_the_field) {
    Fixture f;
    f.renderer.size = { 900.0f, 640.0f };
    f.app.Execute(Cmd::ShowCommandPalette);
    f.Paint();

    const std::string title = f.app.strings().Get("ui.command_palette_title");
    const auto titleInk = [&]() {
        for (const test::FakeRenderer::Text& t : f.renderer.texts) {
            if (t.text == title) return t.ink;
        }
        return RectF{};
    };
    const RectF before = titleInk();
    KITE_EXPECT(before.w() > 0.0f);

    // Down to a handful of rows: the old panel would have collapsed around them.
    for (char c : std::string("session")) f.app.OnChar(static_cast<uint32_t>(c));
    f.Paint();

    const RectF after = titleInk();
    KITE_EXPECT_NEAR(after.l, before.l, 0.01f);
    KITE_EXPECT_NEAR(after.t, before.t, 0.01f);
}

// The keys.ini name is on the row. The filter matches it, so leaving it off the
// screen made the one spelling that does not move with the language invisible.
KITE_TEST(appui, a_command_palette_row_shows_its_keys_ini_name) {
    Fixture f;
    f.renderer.size = { 900.0f, 640.0f };
    f.app.Execute(Cmd::ShowCommandPalette);
    for (char c : std::string("view.toggle_hidden")) f.app.OnChar(static_cast<uint32_t>(c));
    f.Paint();

    bool shown = false;
    for (const test::FakeRenderer::Text& t : f.renderer.TextsAfterFill(f.app.theme().overlayScrim)) {
        if (t.text == "view.toggle_hidden") shown = true;
    }
    KITE_EXPECT(shown);
}

// The typed text is drawn in the body font, not the small print the bookmark list
// uses for its filter: this field is being edited rather than read.
KITE_TEST(appui, the_command_palette_field_is_drawn_at_body_size) {
    Fixture f;
    f.renderer.size = { 900.0f, 640.0f };
    f.app.Execute(Cmd::ShowCommandPalette);
    for (char c : std::string("tab")) f.app.OnChar(static_cast<uint32_t>(c));
    f.Paint();

    bool found = false;
    for (const test::FakeRenderer::Text& t : f.renderer.TextsAfterFill(f.app.theme().overlayScrim)) {
        // ">" と一緒に 1 本の文字列として描く。断片を別々に測って足すと、詰めが
        // 入った瞬間にキャレットの位置が文字とずれる。
        if (t.text != ">tab") continue;
        found = true;
        KITE_EXPECT(t.role == ui::FontRole::Ui);
    }
    KITE_EXPECT(found);
}

// The wheel belongs to the panel while it is up. Every command is in this list, so
// there is always more of it than a window shows.
KITE_TEST(appui, the_wheel_moves_the_command_palette_and_not_the_list_behind_it) {
    Fixture f;
    f.renderer.size = { 560.0f, 300.0f };
    f.app.Execute(Cmd::ShowCommandPalette);
    f.Paint();

    const float listScroll = f.tab()->scroll;
    KITE_EXPECT_EQ(f.app.commandPalette().scroll(), 0);

    f.Wheel(280.0f, 150.0f, -3.0f);
    f.Paint();

    KITE_EXPECT_NEAR(f.tab()->scroll, listScroll, 0.01f);
    KITE_EXPECT(f.app.commandPalette().scroll() > 0);
}


// The bookmark list is drawn in the same frame as the palette, so it holds still
// while being typed at too - the panel is sized from the unfiltered count.
KITE_TEST(appui, filtering_the_bookmark_list_does_not_move_the_field) {
    Fixture f;
    GiveBookmarks(f.app);
    f.renderer.size = { 900.0f, 640.0f };
    f.app.Execute(Cmd::ShowPlaces);
    f.Paint();

    const std::string title = f.app.strings().Get("ui.goto_title");
    const auto titleInk = [&]() {
        for (const test::FakeRenderer::Text& t : f.renderer.texts) {
            if (t.text == title) return t.ink;
        }
        return RectF{};
    };
    const RectF before = titleInk();
    KITE_EXPECT(before.w() > 0.0f);

    for (char c : std::string("mark1")) f.app.OnChar(static_cast<uint32_t>(c));
    f.Paint();

    const RectF after = titleInk();
    KITE_EXPECT_NEAR(after.l, before.l, 0.01f);
    KITE_EXPECT_NEAR(after.t, before.t, 0.01f);
}

// Both choosers are the same panel in the same place, whatever they hold. The
// bookmark list used to be sized to its bookmarks, so choosing "bookmark.list"
// from the palette dropped the field somewhere else on screen - out from under the
// fingers that had just been typing into it.
KITE_TEST(appui, the_bookmark_list_and_the_palette_share_one_panel) {
    Fixture f;
    GiveBookmarks(f.app);
    f.renderer.size = { 900.0f, 640.0f };

    // The panel is the fill in the overlay's own background colour.
    const auto panelOf = [&]() {
        RectF found{};
        for (const test::FakeRenderer::Fill& fill : f.renderer.fills) {
            if (test::FakeRenderer::SameColor(fill.color, f.app.theme().overlayBg)) {
                found = fill.rect;
            }
        }
        return found;
    };

    f.app.Execute(Cmd::ShowCommandPalette);
    f.Paint();
    const RectF palette = panelOf();
    KITE_EXPECT(palette.w() > 0.0f);

    // Straight from one to the other, the way running bookmark.list does it.
    f.app.Execute(Cmd::ShowCommandPalette);
    f.app.Execute(Cmd::ShowPlaces);
    f.Paint();
    const RectF bookmarks = panelOf();

    KITE_EXPECT_NEAR(bookmarks.l, palette.l, 0.01f);
    KITE_EXPECT_NEAR(bookmarks.t, palette.t, 0.01f);
    KITE_EXPECT_NEAR(bookmarks.r, palette.r, 0.01f);
    KITE_EXPECT_NEAR(bookmarks.b, palette.b, 0.01f);
}

// One bookmark or fifty, the panel does not change - the count decides nothing
// about the frame.
KITE_TEST(appui, the_bookmark_list_panel_does_not_follow_the_bookmark_count) {
    const auto panelWith = [](int bookmarks) {
        Fixture f;
        f.renderer.size = { 900.0f, 640.0f };
        for (int i = 0; i < bookmarks; ++i) {
            f.app.workspace().bookmarks.push_back(
                { "mark" + std::to_string(i), "C:\\home\\alpha" });
        }
        f.app.Execute(Cmd::ShowPlaces);
        f.Paint();
        RectF found{};
        for (const test::FakeRenderer::Fill& fill : f.renderer.fills) {
            if (test::FakeRenderer::SameColor(fill.color, f.app.theme().overlayBg)) {
                found = fill.rect;
            }
        }
        return found;
    };

    const RectF one = panelWith(1);
    const RectF many = panelWith(40);
    KITE_EXPECT(one.h() > 0.0f);
    KITE_EXPECT_NEAR(one.t, many.t, 0.01f);
    KITE_EXPECT_NEAR(one.b, many.b, 0.01f);
}

// And its filter is a field at body size, not small print in the title row.
KITE_TEST(appui, the_bookmark_list_field_is_drawn_at_body_size) {
    Fixture f;
    GiveBookmarks(f.app);
    f.renderer.size = { 900.0f, 640.0f };
    f.app.Execute(Cmd::ShowPlaces);
    for (char c : std::string("mark")) f.app.OnChar(static_cast<uint32_t>(c));
    f.Paint();

    bool found = false;
    for (const test::FakeRenderer::Text& t : f.renderer.TextsAfterFill(f.app.theme().overlayScrim)) {
        if (t.text != "mark") continue;
        found = true;
        KITE_EXPECT(t.role == ui::FontRole::Ui);
    }
    KITE_EXPECT(found);
}

// The caret follows the caret, not the end of the string. It used to be pinned
// to the tail because the filter had nowhere else to put it; now that the field
// takes arrow keys, a caret drawn past the text would point at the wrong letter.
KITE_TEST(appui, the_chooser_caret_sits_where_the_caret_is) {
    Fixture f;
    GiveBookmarks(f.app);
    f.renderer.size = { 900.0f, 640.0f };
    f.app.Execute(Cmd::ShowPlaces);
    for (char c : std::string("mark")) f.app.OnChar(static_cast<uint32_t>(c));
    f.Paint();
    const RectF atEnd = f.ui.caretRect();

    f.app.OnKey(ParseChord("Home"));
    f.Paint();
    const RectF atStart = f.ui.caretRect();

    KITE_EXPECT(atStart.l < atEnd.l);
    KITE_EXPECT_NEAR(atStart.t, atEnd.t, 0.01f);
}

// A selection in the filter is drawn the way every other field draws one: a
// band under the text, in the field's own colour.
KITE_TEST(appui, the_chooser_field_paints_its_selection) {
    Fixture f;
    GiveBookmarks(f.app);
    f.renderer.size = { 900.0f, 640.0f };
    f.app.Execute(Cmd::ShowPlaces);
    for (char c : std::string("mark")) f.app.OnChar(static_cast<uint32_t>(c));
    f.Paint();
    const int before = f.renderer.CountFills(f.app.theme().textSelection);

    f.app.OnKey(ParseChord("Ctrl+A"));
    f.Paint();
    KITE_EXPECT(f.renderer.CountFills(f.app.theme().textSelection) > before);
}

// Carried off the window and let go: the tab asks for a window of its own.
// Coordinates outside the surface keep arriving because the platform captures
// the pointer while the button is held.
KITE_TEST(appui, a_tab_dropped_outside_the_window_opens_a_window_of_its_own) {
    Fixture f;
    f.app.Execute(Cmd::NewTab);
    test::PumpUntilSettled(f.app);
    f.app.OpenPath("C:\\home\\alpha", false);
    test::PumpUntilSettled(f.app);
    f.Paint();
    KITE_EXPECT_EQ(f.pane()->tabs.size(), size_t{ 2 });

    const Theme& th = f.app.theme();
    const RectF pane = f.paneRect();
    const float y = pane.t + th.tabBarHeight * 0.5f;
    // Tabs are clamped to 190 px wide, so the second one starts past 190.
    f.Press(pane.l + 250.0f, y);
    f.Drag(f.renderer.size.w + 60.0f, y);
    f.Release(f.renderer.size.w + 60.0f, y);
    test::PumpUntilSettled(f.app);

    KITE_EXPECT_EQ(f.host.newWindows.size(), size_t{ 1 });
    KITE_EXPECT_EQ(f.host.newWindows[0], std::string("C:\\home\\alpha"));
    KITE_EXPECT_EQ(f.pane()->tabs.size(), size_t{ 1 });
}

// The bars are inside the window; a tab let go over one of them is not being
// pulled out, and it is not being dropped into a pane either.
KITE_TEST(appui, a_tab_dropped_on_the_session_bar_stays_where_it_was) {
    Fixture f;
    f.app.Execute(Cmd::NewTab);
    test::PumpUntilSettled(f.app);
    f.app.OpenPath("C:\\home\\alpha", false);
    test::PumpUntilSettled(f.app);
    f.Paint();

    const Theme& th = f.app.theme();
    const RectF pane = f.paneRect();
    f.Press(pane.l + 120.0f, pane.t + th.tabBarHeight * 0.5f);
    f.Drag(600.0f, th.sessionBarHeight * 0.5f);
    f.Release(600.0f, th.sessionBarHeight * 0.5f);
    test::PumpUntilSettled(f.app);

    KITE_EXPECT_EQ(f.host.newWindows.size(), size_t{ 0 });
    KITE_EXPECT_EQ(f.pane()->tabs.size(), size_t{ 2 });
    KITE_EXPECT_EQ(f.pane()->tabs[1]->path, std::string("C:\\home\\alpha"));
}

// --- IME の未確定文字列 ------------------------------------------------------
//
// 変換中の文字は入力欄の «中身» として描く。IME に描かせると、その窓は自前の
// フォントと行送りで文字を置くので同じ行の中で数ピクセルずれ、確定した瞬間に跳ぶ
// （利用者からの報告がこの形）。だから見るのは色ではなく置かれた場所で、ここでしか
// 検査できない。

KITE_TEST(appui, a_conversion_in_progress_is_drawn_inside_the_field) {
    Fixture f;
    f.app.Execute(Cmd::NewFolder);
    f.app.SetComposition("にほんご", 12, 0, 0);
    f.Paint();

    // 借りた行の入力欄の中。行の «上» でも窓の下端でもない。
    const test::FakeRenderer::Text* typed = f.TextNamed("にほんご");
    KITE_EXPECT(typed != nullptr);
    if (!typed) return;
    KITE_EXPECT_FALSE(FieldBoxUnder(f, typed->ink).empty());
    KITE_EXPECT(f.pane()->listArea.contains(typed->ink.l + 1.0f, typed->ink.center().y));

    // 下線 1 本が «まだ確定していない» の共通語彙。文字の下に、文字の幅だけ引く。
    const Theme& th = f.app.theme();
    KITE_EXPECT_EQ(f.renderer.CountFills(th.textDim), 1);
    for (const test::FakeRenderer::Fill& fill : f.renderer.fills) {
        if (!test::FakeRenderer::SameColor(fill.color, th.textDim)) continue;
        KITE_EXPECT_NEAR(fill.rect.l, typed->ink.l, 1.0f);
        KITE_EXPECT_NEAR(fill.rect.r, typed->ink.r, 1.0f);
        KITE_EXPECT(fill.rect.h() <= 2.0f);
    }
}

KITE_TEST(appui, the_caret_stands_after_the_text_being_converted) {
    Fixture f;
    f.app.Execute(Cmd::NewFolder);
    f.Paint();
    const RectF empty = f.ui.caretRect();

    // キャレットは変換中の文字列の «中» を指す（IME が数えている位置）。ここが
    // 動かないと、候補一覧は打ち始めた場所に取り残される。
    f.app.SetComposition("にほんご", 6, 0, 0);
    f.Paint();
    const RectF mid = f.ui.caretRect();
    KITE_EXPECT(mid.l > empty.l);

    f.app.SetComposition("にほんご", 12, 0, 0);
    f.Paint();
    KITE_EXPECT(f.ui.caretRect().l > mid.l);
    // 高さは行そのもの。候補一覧に «この矩形を避けろ» と言うために要る。
    KITE_EXPECT(f.ui.caretRect().b > f.ui.caretRect().t);
}

KITE_TEST(appui, the_clause_being_converted_is_marked_apart_from_the_rest) {
    Fixture f;
    f.app.Execute(Cmd::NewFolder);
    const Theme& th = f.app.theme();

    // 注目節なし ─ 下線だけで、下敷きは無い。
    f.app.SetComposition("にほんご", 12, 0, 0);
    f.Paint();
    KITE_EXPECT_EQ(f.renderer.CountFills(th.textSelection), 0);

    // 後半の節を変換中。節が 2 つ以上あるとき、どれを変換しているのかを言えるのは
    // IME の窓を消した以上ここだけ。
    f.app.SetComposition("にほんご", 12, 6, 12);
    f.Paint();
    KITE_EXPECT_EQ(f.renderer.CountFills(th.textSelection), 1);
    const test::FakeRenderer::Text* typed = f.TextNamed("にほんご");
    KITE_EXPECT(typed != nullptr);
    if (!typed) return;
    for (const test::FakeRenderer::Fill& fill : f.renderer.fills) {
        if (!test::FakeRenderer::SameColor(fill.color, th.textSelection)) continue;
        // 文字列の後ろ半分だけ。全体に敷いたのでは «どれを» の答えになっていない。
        KITE_EXPECT(fill.rect.l > typed->ink.l);
        KITE_EXPECT_NEAR(fill.rect.r, typed->ink.r, 1.0f);
    }
}

KITE_TEST(appui, converting_over_a_selection_shows_what_the_commit_will_leave) {
    Fixture f;
    const std::string name = f.tab()->CursorEntry()->name;
    f.app.Execute(Cmd::Rename);
    f.app.prompt().SelectAll();
    f.app.SetComposition("にほんご", 12, 0, 0);
    f.Paint();

    // 確定すれば選択は置き換えられる。Enter を押すまで元の名前が残っていると、
    // 何が起きるのかが押すまで分からない。
    KITE_EXPECT(f.TextNamed("にほんご") != nullptr);
    KITE_EXPECT(f.TextNamed(name) == nullptr);
}

KITE_TEST(appui, the_palette_shows_the_conversion_without_filtering_on_it) {
    Fixture f;
    f.app.Execute(Cmd::ShowCommandPalette);
    f.Paint();
    const int rowsBefore = static_cast<int>(f.app.commandPalette().rows().size());

    f.app.SetComposition("たぶ", 6, 0, 6);
    f.Paint();

    KITE_EXPECT(f.TextNamed(">たぶ") != nullptr);
    // 未確定の文字で一覧を削らない。確定する前に候補が全部消えたように見える。
    KITE_EXPECT(f.app.commandPalette().filter().empty());
    KITE_EXPECT_EQ(static_cast<int>(f.app.commandPalette().rows().size()), rowsBefore);
}

KITE_TEST(appui, a_conversion_over_the_list_is_read_out_in_the_status_bar) {
    Fixture f;
    // 一覧の上での変換 ─ 型入力ジャンプの名前は IME で打てる。行はどれも
    // 書き換わらないので、ここで言わなければ打った文字はどこにも出ない。
    f.app.SetComposition("にほんご", 12, 0, 0);
    f.Paint();

    const std::string said = f.app.strings().Format("ui.composing", { "にほんご" });
    const test::FakeRenderer::Text* status = f.TextNamed(said);
    KITE_EXPECT(status != nullptr);
    if (!status) return;
    KITE_EXPECT(status->ink.t >= f.renderer.size.h - f.app.theme().statusBarHeight - 0.01f);
}

KITE_TEST(appui, with_no_field_open_the_ime_is_pointed_at_the_cursor_row) {
    Fixture f;
    f.Paint();

    // 型入力ジャンプは名前を IME で打てるので、変換窓の行き先が要る。前に開いて
    // いた欄の跡地ではなく、今まさに探している行の脇。
    const test::FakeRenderer::Text* row = f.TextNamed(f.tab()->CursorEntry()->name);
    KITE_EXPECT(row != nullptr);
    if (!row) return;
    const RectF caret = f.ui.caretRect();
    KITE_EXPECT(caret.t <= row->ink.center().y);
    KITE_EXPECT(caret.b >= row->ink.center().y);
    KITE_EXPECT_NEAR(caret.l, row->ink.l, 1.0f);
}

KITE_TEST(appui, the_status_bar_says_how_much_of_the_volume_is_in_use) {
    Fixture f;
    f.Paint();

    // 容量は列挙が持ち帰る（fs::ListResult）─ 描くたびに OS へ訊ける値ではないので、
    // 一覧と同じ答えに相乗りしている。使用量は総容量からの引き算で、3 つ目の数を
    // 持ち回らない。
    const std::string said =
        f.app.strings().Format("ui.status_usage", { FormatSize(600), FormatSize(1000) });
    KITE_EXPECT(f.StatusTextWith(said) != nullptr);
}

KITE_TEST(appui, without_an_answer_the_status_bar_says_nothing_about_the_volume) {
    Fixture f;
    f.files.freeBytes = 0;
    f.files.totalBytes = 0;
    f.app.RefreshFocused();
    test::PumpUntilSettled(f.app);
    f.Paint();

    // 総容量 0 は「まだ訊いていない」─ 仮想フォルダも共有の一覧もこれ。「0 B / 0 B」と
    // 出せば、容量が尽きたと読める。
    KITE_EXPECT(f.StatusTextWith(FormatSize(0)) == nullptr);
    KITE_EXPECT(f.StatusTextWith(f.app.strings().Format(
                    "ui.status_items", { std::to_string(f.tab()->ItemCount()) })) != nullptr);
}

KITE_TEST(appui, the_status_bar_does_not_repeat_the_cursor_rows_name) {
    Fixture f;
    f.Paint();

    // 名前は行そのものに書いてあり、カーソルの枠がどの行かを言っている。同じことを
    // 帯の半分を使ってもう一度言う理由が無い。
    const fs::Entry* e = f.tab()->CursorEntry();
    KITE_EXPECT(e != nullptr);
    if (!e) return;
    KITE_EXPECT(f.StatusTextWith(e->name) == nullptr);
}

KITE_TEST(appui, a_long_message_drops_the_volume_line_instead_of_landing_on_it) {
    Fixture f;
    f.app.SetStatus(std::string(150, 'x'));
    f.Paint();

    // 矩形が重なっていれば文字も重なる ─ 入らないほうは重ねずに落とす。落とすのは
    // いつも同じ顔でいる背景のほうで、たった今起きたことの答えではない。
    const std::vector<test::FakeRenderer::Text> bar = f.StatusTexts();
    for (size_t i = 0; i < bar.size(); ++i) {
        for (size_t j = i + 1; j < bar.size(); ++j) {
            KITE_EXPECT_FALSE(test::FakeRenderer::Overlaps(bar[i].ink, bar[j].ink));
        }
    }
    KITE_EXPECT(f.StatusTextWith(std::string(150, 'x')) != nullptr);
    KITE_EXPECT(f.StatusTextWith(f.app.strings().Format(
                    "ui.status_usage", { FormatSize(600), FormatSize(1000) })) == nullptr);

    // 帯ごと埋める長さなら、落ちるのは左ぜんぶ ─ 残った幅が 0 なのだから、
    // 件数を重ねて描く先はもう無い。
    f.app.SetStatus(std::string(250, 'y'));
    f.Paint();
    const std::vector<test::FakeRenderer::Text> full = f.StatusTexts();
    for (size_t i = 0; i < full.size(); ++i) {
        for (size_t j = i + 1; j < full.size(); ++j) {
            KITE_EXPECT_FALSE(test::FakeRenderer::Overlaps(full[i].ink, full[j].ink));
        }
    }
}
