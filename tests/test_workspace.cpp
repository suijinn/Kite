#include "TestFramework.h"
#include "core/model/Workspace.h"

using namespace kite;

KITE_TEST(workspace, add_tab_activates_the_new_tab_and_assigns_a_watch_id) {
    Pane pane;
    Tab* first = pane.AddTab("C:\\a");
    Tab* second = pane.AddTab("C:\\b");

    KITE_EXPECT_EQ(pane.tabs.size(), size_t{ 2 });
    KITE_EXPECT_EQ(pane.active, 1);
    KITE_EXPECT_EQ(pane.activeTab(), second);
    KITE_EXPECT_NE(first->watchId, uint64_t{ 0 });
    KITE_EXPECT_NE(first->watchId, second->watchId);
}

KITE_TEST(workspace, add_tab_at_an_index_inserts_there) {
    Pane pane;
    pane.AddTab("C:\\a");
    pane.AddTab("C:\\c");
    pane.AddTab("C:\\b", 1);

    KITE_EXPECT_EQ(pane.tabs[1]->path, std::string("C:\\b"));
    KITE_EXPECT_EQ(pane.active, 1);
}

KITE_TEST(workspace, a_pane_always_keeps_at_least_one_tab) {
    Pane pane;
    pane.AddTab("C:\\a");
    std::string closed;
    KITE_EXPECT_FALSE(pane.CloseTab(0, &closed));
    KITE_EXPECT_EQ(pane.tabs.size(), size_t{ 1 });
}

KITE_TEST(workspace, close_tab_reports_the_path_and_moves_the_selection) {
    Pane pane;
    pane.AddTab("C:\\a");
    pane.AddTab("C:\\b");
    pane.AddTab("C:\\c");
    pane.Activate(2);

    std::string closed;
    KITE_EXPECT(pane.CloseTab(2, &closed));
    KITE_EXPECT_EQ(closed, std::string("C:\\c"));
    KITE_EXPECT_EQ(pane.active, 1);
}

KITE_TEST(workspace, reorder_moves_a_tab_and_follows_it) {
    Pane pane;
    pane.AddTab("C:\\a");
    pane.AddTab("C:\\b");
    pane.AddTab("C:\\c");
    pane.Activate(0);  // "a" is active

    KITE_EXPECT(pane.ReorderTab(0, 2));
    KITE_EXPECT_EQ(pane.tabs[0]->path, std::string("C:\\b"));
    KITE_EXPECT_EQ(pane.tabs[1]->path, std::string("C:\\c"));
    KITE_EXPECT_EQ(pane.tabs[2]->path, std::string("C:\\a"));
    // The active tab is still "a", now at the end.
    KITE_EXPECT_EQ(pane.active, 2);
}

KITE_TEST(workspace, reorder_shifts_the_active_index_when_another_tab_moves_past_it) {
    Pane pane;
    pane.AddTab("C:\\a");
    pane.AddTab("C:\\b");
    pane.AddTab("C:\\c");
    pane.Activate(1);  // "b"

    pane.ReorderTab(0, 2);  // move "a" to the end, past "b"
    KITE_EXPECT_EQ(pane.tabs[pane.active]->path, std::string("C:\\b"));
}

KITE_TEST(workspace, reorder_rejects_no_ops_and_bad_indices) {
    Pane pane;
    pane.AddTab("C:\\a");
    pane.AddTab("C:\\b");
    KITE_EXPECT_FALSE(pane.ReorderTab(0, 0));
    KITE_EXPECT_FALSE(pane.ReorderTab(-1, 1));
    KITE_EXPECT_FALSE(pane.ReorderTab(5, 0));
}

KITE_TEST(workspace, detach_and_attach_move_a_tab_between_panes) {
    Pane left;
    left.AddTab("C:\\a");
    left.AddTab("C:\\b");

    Pane right;
    right.AddTab("C:\\z");

    std::unique_ptr<Tab> moved = left.DetachTab(1);
    KITE_EXPECT(moved != nullptr);
    KITE_EXPECT_EQ(moved->path, std::string("C:\\b"));
    KITE_EXPECT_EQ(left.tabs.size(), size_t{ 1 });

    const uint64_t watchId = moved->watchId;
    right.AttachTab(std::move(moved), 0);
    KITE_EXPECT_EQ(right.tabs.size(), size_t{ 2 });
    KITE_EXPECT_EQ(right.tabs[0]->path, std::string("C:\\b"));
    KITE_EXPECT_EQ(right.active, 0);
    // The watch identity survives the move, so the watcher does not churn.
    KITE_EXPECT_EQ(right.tabs[0]->watchId, watchId);
}

KITE_TEST(workspace, detaching_the_last_tab_leaves_the_pane_empty) {
    Pane pane;
    pane.AddTab("C:\\a");
    std::unique_ptr<Tab> moved = pane.DetachTab(0);
    KITE_EXPECT(moved != nullptr);
    KITE_EXPECT(pane.empty());
    KITE_EXPECT(pane.activeTab() == nullptr);
}

KITE_TEST(workspace, attach_clamps_an_out_of_range_index) {
    Pane pane;
    pane.AddTab("C:\\a");
    auto extra = std::make_unique<Tab>();
    extra->path = "C:\\b";
    pane.AttachTab(std::move(extra), 99);
    KITE_EXPECT_EQ(pane.tabs.size(), size_t{ 2 });
    KITE_EXPECT_EQ(pane.tabs[1]->path, std::string("C:\\b"));
}

KITE_TEST(workspace, activate_clamps_rather_than_crashing) {
    Pane pane;
    pane.AddTab("C:\\a");
    pane.AddTab("C:\\b");
    pane.Activate(99);
    KITE_EXPECT_EQ(pane.active, 1);
    pane.Activate(-4);
    KITE_EXPECT_EQ(pane.active, 0);
}

KITE_TEST(workspace, sessions_can_be_added_and_closed_but_never_all_of_them) {
    Workspace workspace;
    workspace.AddSession("one", "C:\\a");
    workspace.AddSession("two", "C:\\b");
    KITE_EXPECT_EQ(workspace.sessions.size(), size_t{ 2 });
    KITE_EXPECT_EQ(workspace.active, 1);

    workspace.CloseSession(1);
    KITE_EXPECT_EQ(workspace.sessions.size(), size_t{ 1 });
    KITE_EXPECT_EQ(workspace.active, 0);

    workspace.CloseSession(0);
    KITE_EXPECT_EQ(workspace.sessions.size(), size_t{ 1 });
}

KITE_TEST(workspace, switching_sessions_frees_background_listings) {
    Workspace workspace;
    Session* first = workspace.AddSession("one", "C:\\a");
    Pane* pane = first->Panes().front();
    Tab* background = pane->AddTab("C:\\background");
    background->listing.entries.resize(64);
    background->loaded = true;
    pane->Activate(0);  // make the other tab the active one

    workspace.AddSession("two", "C:\\b");
    workspace.ActivateSession(0);
    workspace.ActivateSession(1);

    KITE_EXPECT_EQ(background->listing.entries.size(), size_t{ 0 });
    KITE_EXPECT_FALSE(background->loaded);
}

KITE_TEST(workspace, focused_tab_follows_the_focused_pane) {
    Workspace workspace;
    Session* session = workspace.AddSession("one", "C:\\a");
    Pane* created = session->Split(session->focus, SplitNode::Kind::LeftRight);
    KITE_EXPECT(created != nullptr);

    session->focus = created;
    KITE_EXPECT_EQ(workspace.focusedPane(), created);
    KITE_EXPECT_EQ(workspace.focusedTab(), created->activeTab());
}
