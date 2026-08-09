#include "TestFramework.h"
#include "core/model/Workspace.h"

using namespace kite;

namespace {

// A tab holding a small mixed listing: two folders, four files, one hidden.
Tab MakeTab() {
    Tab tab;
    tab.path = "C:\\home";

    auto add = [&](const char* name, bool dir, uint64_t size, int64_t mtime,
                   fs::Attr extra = fs::Attr::None) {
        fs::Entry entry;
        entry.name = name;
        entry.size = size;
        entry.mtime = mtime;
        entry.attrs = extra;
        if (dir) entry.attrs |= fs::Attr::Directory;
        tab.listing.entries.push_back(entry);
    };

    add("zeta", true, 0, 100);
    add("alpha", true, 0, 900);
    add("image10.png", false, 2048, 300);
    add("image2.png", false, 4096, 200);
    add("notes.txt", false, 120, 800);
    add(".hidden", false, 10, 50, fs::Attr::Hidden);

    tab.Rebuild();
    return tab;
}

std::string NameAt(const Tab& tab, int visibleIndex) {
    return tab.listing.entries[tab.visible[visibleIndex]].name;
}

}  // namespace

KITE_TEST(tab, hides_hidden_entries_by_default) {
    Tab tab = MakeTab();
    KITE_EXPECT_EQ(tab.visible.size(), size_t{ 5 });

    tab.view.showHidden = true;
    tab.Rebuild();
    KITE_EXPECT_EQ(tab.visible.size(), size_t{ 6 });
}

KITE_TEST(tab, folders_come_first_then_natural_name_order) {
    Tab tab = MakeTab();
    KITE_EXPECT_EQ(NameAt(tab, 0), std::string("alpha"));
    KITE_EXPECT_EQ(NameAt(tab, 1), std::string("zeta"));
    // image2 before image10 - the reason NaturalCompare exists.
    KITE_EXPECT_EQ(NameAt(tab, 2), std::string("image2.png"));
    KITE_EXPECT_EQ(NameAt(tab, 3), std::string("image10.png"));
    KITE_EXPECT_EQ(NameAt(tab, 4), std::string("notes.txt"));
}

KITE_TEST(tab, dirs_first_can_be_turned_off) {
    Tab tab = MakeTab();
    tab.view.dirsFirst = false;
    tab.Rebuild();
    KITE_EXPECT_EQ(NameAt(tab, 0), std::string("alpha"));
    KITE_EXPECT_EQ(NameAt(tab, 1), std::string("image2.png"));
}

KITE_TEST(tab, sorts_by_size_and_date) {
    Tab tab = MakeTab();
    tab.view.dirsFirst = false;

    tab.view.sort = SortKey::Size;
    tab.Rebuild();
    KITE_EXPECT_EQ(NameAt(tab, 0), std::string("alpha"));  // folders report size 0
    KITE_EXPECT_EQ(NameAt(tab, 4), std::string("image2.png"));  // largest

    tab.view.sort = SortKey::Date;
    tab.Rebuild();
    KITE_EXPECT_EQ(NameAt(tab, 0), std::string("zeta"));   // mtime 100, oldest
    KITE_EXPECT_EQ(NameAt(tab, 1), std::string("image2.png"));  // mtime 200
    KITE_EXPECT_EQ(NameAt(tab, 4), std::string("alpha"));  // mtime 900, newest
}

KITE_TEST(tab, descending_reverses_the_order) {
    Tab tab = MakeTab();
    tab.view.sortDesc = true;
    tab.Rebuild();
    // Folders stay grouped first; only the order within a group flips.
    KITE_EXPECT_EQ(NameAt(tab, 0), std::string("zeta"));
    KITE_EXPECT_EQ(NameAt(tab, 1), std::string("alpha"));
}

KITE_TEST(tab, sorts_by_extension_then_name) {
    Tab tab = MakeTab();
    tab.view.sort = SortKey::Ext;
    tab.Rebuild();
    // png entries share an extension, so name order decides between them.
    KITE_EXPECT_EQ(NameAt(tab, 2), std::string("image2.png"));
    KITE_EXPECT_EQ(NameAt(tab, 3), std::string("image10.png"));
    KITE_EXPECT_EQ(NameAt(tab, 4), std::string("notes.txt"));
}

KITE_TEST(tab, filter_is_a_case_insensitive_substring_match) {
    Tab tab = MakeTab();
    tab.filter = "IMG";
    tab.Rebuild();
    KITE_EXPECT_EQ(tab.visible.size(), size_t{ 0 });

    tab.filter = "image";
    tab.Rebuild();
    KITE_EXPECT_EQ(tab.visible.size(), size_t{ 2 });

    tab.filter = "PNG";
    tab.Rebuild();
    KITE_EXPECT_EQ(tab.visible.size(), size_t{ 2 });
}

KITE_TEST(tab, rebuild_keeps_the_cursor_on_the_same_entry) {
    Tab tab = MakeTab();
    tab.cursor = 4;  // notes.txt
    KITE_EXPECT_EQ(NameAt(tab, tab.cursor), std::string("notes.txt"));

    tab.view.sortDesc = true;
    tab.Rebuild();
    KITE_EXPECT_EQ(NameAt(tab, tab.cursor), std::string("notes.txt"));
}

KITE_TEST(tab, pending_focus_wins_over_cursor_preservation) {
    // This is what makes "go up" land on the folder you just left.
    Tab tab = MakeTab();
    tab.cursor = 0;
    tab.pendingFocusName = "notes.txt";
    tab.Rebuild();
    KITE_EXPECT_EQ(NameAt(tab, tab.cursor), std::string("notes.txt"));
    // It is consumed, not sticky.
    KITE_EXPECT_EQ(tab.pendingFocusName, std::string(""));
}

KITE_TEST(tab, cursor_is_clamped_when_the_listing_shrinks) {
    Tab tab = MakeTab();
    tab.cursor = 4;
    tab.filter = "alpha";
    tab.Rebuild();
    KITE_EXPECT_EQ(tab.visible.size(), size_t{ 1 });
    KITE_EXPECT_EQ(tab.cursor, 0);
}

KITE_TEST(tab, selection_falls_back_to_the_cursor_entry) {
    Tab tab = MakeTab();
    tab.cursor = 2;  // image2.png
    const std::vector<std::string> paths = tab.SelectionPaths();
    KITE_EXPECT_EQ(paths.size(), size_t{ 1 });
    KITE_EXPECT_EQ(paths[0], std::string("C:\\home\\image2.png"));
}

KITE_TEST(tab, marked_entries_take_priority_over_the_cursor) {
    Tab tab = MakeTab();
    tab.cursor = 0;
    tab.MarkRange(2, 3, true);

    KITE_EXPECT_EQ(tab.MarkedCount(), 2);
    KITE_EXPECT_EQ(tab.MarkedBytes(), uint64_t{ 4096 + 2048 });

    const std::vector<std::string> paths = tab.SelectionPaths();
    KITE_EXPECT_EQ(paths.size(), size_t{ 2 });

    tab.ClearMarks();
    KITE_EXPECT_EQ(tab.MarkedCount(), 0);
}

KITE_TEST(tab, mark_range_is_order_independent_and_clamped) {
    Tab tab = MakeTab();
    tab.MarkRange(3, 1, true);
    KITE_EXPECT_EQ(tab.MarkedCount(), 3);

    tab.ClearMarks();
    tab.MarkRange(-5, 999, true);
    KITE_EXPECT_EQ(tab.MarkedCount(), 5);
}

KITE_TEST(tab, marks_never_survive_into_a_stale_index) {
    // Rebuild resizes `marked` alongside the listing; a filter must not make
    // SelectionPaths return entries the user cannot see.
    Tab tab = MakeTab();
    tab.MarkRange(0, 4, true);
    tab.filter = "notes";
    tab.Rebuild();

    const std::vector<std::string> paths = tab.SelectionPaths();
    KITE_EXPECT_EQ(paths.size(), size_t{ 1 });
    KITE_EXPECT_EQ(paths[0], std::string("C:\\home\\notes.txt"));
}

KITE_TEST(tab, drop_listing_frees_state_but_keeps_the_path) {
    Tab tab = MakeTab();
    tab.DropListing();
    KITE_EXPECT_EQ(tab.listing.entries.size(), size_t{ 0 });
    KITE_EXPECT_EQ(tab.visible.size(), size_t{ 0 });
    KITE_EXPECT_FALSE(tab.loaded);
    KITE_EXPECT_EQ(tab.path, std::string("C:\\home"));
}

KITE_TEST(tab, cursor_entry_is_null_on_an_empty_listing) {
    Tab tab;
    tab.path = "C:\\empty";
    tab.Rebuild();
    KITE_EXPECT(tab.CursorEntry() == nullptr);
    KITE_EXPECT_EQ(tab.CursorPath(), std::string(""));
    KITE_EXPECT_EQ(tab.SelectionPaths().size(), size_t{ 0 });
}

KITE_TEST(tab, title_is_the_last_component) {
    Tab tab;
    tab.path = "C:\\home\\alpha";
    KITE_EXPECT_EQ(tab.title(), std::string("alpha"));
    tab.path = "C:\\";
    KITE_EXPECT_EQ(tab.title(), std::string("C:"));
}
