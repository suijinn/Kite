#include "TestFramework.h"
#include "core/model/Workspace.h"

using namespace kite;

KITE_TEST(session, starts_as_a_single_leaf) {
    auto session = Session::Create("work", "C:\\home");
    KITE_EXPECT(session->root != nullptr);
    KITE_EXPECT(session->root->leaf());
    KITE_EXPECT_EQ(session->Panes().size(), size_t{ 1 });
    KITE_EXPECT_EQ(session->focus, session->Panes().front());
}

KITE_TEST(session, split_creates_a_sibling_on_the_same_folder) {
    auto session = Session::Create("work", "C:\\home");
    Pane* original = session->focus;

    Pane* created = session->Split(original, SplitNode::Kind::LeftRight);
    KITE_EXPECT(created != nullptr);
    KITE_EXPECT_NE(created, original);
    KITE_EXPECT_EQ(session->Panes().size(), size_t{ 2 });
    KITE_EXPECT_EQ(created->activeTab()->path, std::string("C:\\home"));
    KITE_EXPECT_FALSE(session->root->leaf());
    KITE_EXPECT_EQ(session->root->kind, SplitNode::Kind::LeftRight);
}

KITE_TEST(session, splits_can_be_nested) {
    auto session = Session::Create("work", "C:\\home");
    Pane* second = session->Split(session->focus, SplitNode::Kind::LeftRight);
    session->Split(second, SplitNode::Kind::TopBottom);
    KITE_EXPECT_EQ(session->Panes().size(), size_t{ 3 });
}

KITE_TEST(session, closing_a_pane_collapses_the_split_into_its_sibling) {
    auto session = Session::Create("work", "C:\\home");
    Pane* created = session->Split(session->focus, SplitNode::Kind::LeftRight);
    Pane* original = session->Panes().front();

    KITE_EXPECT(session->ClosePane(created));
    KITE_EXPECT_EQ(session->Panes().size(), size_t{ 1 });
    KITE_EXPECT(session->root->leaf());
    KITE_EXPECT_EQ(session->Panes().front(), original);
}

KITE_TEST(session, closing_the_focused_pane_moves_focus_somewhere_valid) {
    auto session = Session::Create("work", "C:\\home");
    Pane* created = session->Split(session->focus, SplitNode::Kind::LeftRight);
    session->focus = created;

    KITE_EXPECT(session->ClosePane(created));
    KITE_EXPECT(session->focus != nullptr);
    KITE_EXPECT_EQ(session->focus, session->Panes().front());
}

KITE_TEST(session, the_last_pane_cannot_be_closed) {
    auto session = Session::Create("work", "C:\\home");
    KITE_EXPECT_FALSE(session->ClosePane(session->focus));
    KITE_EXPECT_EQ(session->Panes().size(), size_t{ 1 });
}

KITE_TEST(session, closing_a_nested_pane_keeps_the_rest_of_the_tree) {
    auto session = Session::Create("work", "C:\\home");
    Pane* second = session->Split(session->focus, SplitNode::Kind::LeftRight);
    Pane* third = session->Split(second, SplitNode::Kind::TopBottom);

    KITE_EXPECT(session->ClosePane(third));
    KITE_EXPECT_EQ(session->Panes().size(), size_t{ 2 });
    KITE_EXPECT_EQ(session->root->kind, SplitNode::Kind::LeftRight);
}

KITE_TEST(session, swap_exchanges_a_pane_with_its_sibling) {
    auto session = Session::Create("work", "C:\\home");
    Pane* first = session->focus;
    Pane* second = session->Split(first, SplitNode::Kind::LeftRight);
    second->activeTab()->path = "C:\\other";

    KITE_EXPECT_EQ(session->Panes()[0], first);
    session->SwapWithSibling(first);
    KITE_EXPECT_EQ(session->Panes()[0], second);
}

KITE_TEST(session, directional_focus_uses_the_laid_out_rectangles) {
    auto session = Session::Create("work", "C:\\home");
    Pane* left = session->focus;
    Pane* right = session->Split(left, SplitNode::Kind::LeftRight);

    // Painting normally fills these in; do it by hand for the test.
    session->LeafOf(left)->rect = { 0, 0, 100, 100 };
    session->LeafOf(right)->rect = { 100, 0, 200, 100 };

    KITE_EXPECT_EQ(session->PaneInDirection(left, 1, 0), right);
    KITE_EXPECT_EQ(session->PaneInDirection(right, -1, 0), left);
    KITE_EXPECT(session->PaneInDirection(left, 0, 1) == nullptr);
}

KITE_TEST(session, serialize_round_trips_a_single_leaf) {
    auto session = Session::Create("work", "C:\\home");
    const std::string text = session->Serialize();

    auto restored = Session::Deserialize("work", text);
    KITE_EXPECT(restored != nullptr);
    KITE_EXPECT_EQ(restored->Panes().size(), size_t{ 1 });
    KITE_EXPECT_EQ(restored->Panes().front()->activeTab()->path, std::string("C:\\home"));
}

KITE_TEST(session, serialize_round_trips_a_nested_layout_with_tabs) {
    auto session = Session::Create("work", "C:\\home");
    Pane* second = session->Split(session->focus, SplitNode::Kind::LeftRight);
    Pane* third = session->Split(second, SplitNode::Kind::TopBottom);
    third->AddTab("C:\\extra");
    third->Activate(1);
    session->root->ratio = 0.35f;

    auto restored = Session::Deserialize("work", session->Serialize());
    KITE_EXPECT(restored != nullptr);
    KITE_EXPECT_EQ(restored->Panes().size(), size_t{ 3 });
    KITE_EXPECT_EQ(restored->root->kind, SplitNode::Kind::LeftRight);
    KITE_EXPECT_NEAR(restored->root->ratio, 0.35f, 0.01);

    Pane* restoredThird = restored->Panes()[2];
    KITE_EXPECT_EQ(restoredThird->tabs.size(), size_t{ 2 });
    KITE_EXPECT_EQ(restoredThird->active, 1);
    KITE_EXPECT_EQ(restoredThird->tabs[1]->path, std::string("C:\\extra"));
}

KITE_TEST(session, serialize_survives_paths_containing_delimiters) {
    // Commas, braces and pipes are all legal in Windows paths and all used by
    // the layout format.
    auto session = Session::Create("work", "C:\\odd,name{x}\\y(z)");
    auto restored = Session::Deserialize("work", session->Serialize());
    KITE_EXPECT(restored != nullptr);
    KITE_EXPECT_EQ(restored->Panes().front()->activeTab()->path,
                   std::string("C:\\odd,name{x}\\y(z)"));
}

KITE_TEST(session, deserialize_rejects_garbage_instead_of_half_building) {
    KITE_EXPECT(Session::Deserialize("x", "") == nullptr);
    KITE_EXPECT(Session::Deserialize("x", "nonsense") == nullptr);
    KITE_EXPECT(Session::Deserialize("x", "H(0.5,L{C:\\a}@0") == nullptr);  // truncated
    KITE_EXPECT(Session::Deserialize("x", "L{}@0") == nullptr);             // no tabs
}

KITE_TEST(session, deserialize_clamps_a_silly_ratio) {
    auto restored = Session::Deserialize("x", "H(9.9,L{C:\\a}@0,L{C:\\b}@0)");
    KITE_EXPECT(restored != nullptr);
    KITE_EXPECT_NEAR(restored->root->ratio, 0.5f, 0.001);
}

KITE_TEST(session, deserialize_clamps_an_out_of_range_active_tab) {
    auto restored = Session::Deserialize("x", "L{C:\\a|C:\\b}@9");
    KITE_EXPECT(restored != nullptr);
    KITE_EXPECT_EQ(restored->Panes().front()->active, 1);
}
