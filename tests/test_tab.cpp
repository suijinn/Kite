#include "TestFramework.h"
#include "core/fs/VirtualPath.h"
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

// The n-th real item, skipping the ".." row every non-root folder now carries.
std::string NameAt(const Tab& tab, int itemIndex) {
    const fs::Entry* e = tab.EntryAt(itemIndex + (tab.hasParentRow() ? 1 : 0));
    return e ? e->name : std::string();
}

// The visible index of the n-th real item.
int RowOf(const Tab& tab, int itemIndex) {
    return itemIndex + (tab.hasParentRow() ? 1 : 0);
}

std::string CursorName(const Tab& tab) {
    const fs::Entry* e = tab.CursorEntry();
    return e ? e->name : std::string();
}

}  // namespace

KITE_TEST(tab, hides_hidden_entries_by_default) {
    Tab tab = MakeTab();
    KITE_EXPECT_EQ(tab.ItemCount(), 5);

    tab.view.showHidden = true;
    tab.Rebuild();
    KITE_EXPECT_EQ(tab.ItemCount(), 6);
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
    KITE_EXPECT_EQ(tab.ItemCount(), 0);

    tab.filter = "image";
    tab.Rebuild();
    KITE_EXPECT_EQ(tab.ItemCount(), 2);

    tab.filter = "PNG";
    tab.Rebuild();
    KITE_EXPECT_EQ(tab.ItemCount(), 2);
}

KITE_TEST(tab, rebuild_keeps_the_cursor_on_the_same_entry) {
    Tab tab = MakeTab();
    tab.cursor = RowOf(tab, 4);  // notes.txt
    KITE_EXPECT_EQ(CursorName(tab), std::string("notes.txt"));

    tab.view.sortDesc = true;
    tab.Rebuild();
    KITE_EXPECT_EQ(CursorName(tab), std::string("notes.txt"));
}

KITE_TEST(tab, pending_focus_wins_over_cursor_preservation) {
    // This is what makes "go up" land on the folder you just left.
    Tab tab = MakeTab();
    tab.cursor = 0;
    tab.pendingFocusName = "notes.txt";
    tab.Rebuild();
    KITE_EXPECT_EQ(CursorName(tab), std::string("notes.txt"));
    // It is consumed, not sticky.
    KITE_EXPECT_EQ(tab.pendingFocusName, std::string(""));
}

KITE_TEST(tab, cursor_is_clamped_when_the_listing_shrinks) {
    Tab tab = MakeTab();
    tab.cursor = RowOf(tab, 4);
    tab.filter = "alpha";
    tab.Rebuild();
    KITE_EXPECT_EQ(tab.ItemCount(), 1);
    KITE_EXPECT_EQ(CursorName(tab), std::string("alpha"));
}

KITE_TEST(tab, selection_falls_back_to_the_cursor_entry) {
    Tab tab = MakeTab();
    tab.cursor = RowOf(tab, 2);  // image2.png
    const std::vector<std::string> paths = tab.SelectionPaths();
    KITE_EXPECT_EQ(paths.size(), size_t{ 1 });
    KITE_EXPECT_EQ(paths[0], std::string("C:\\home\\image2.png"));
}

KITE_TEST(tab, marked_entries_take_priority_over_the_cursor) {
    Tab tab = MakeTab();
    tab.cursor = 0;
    tab.MarkRange(RowOf(tab, 2), RowOf(tab, 3), true);

    KITE_EXPECT_EQ(tab.MarkedCount(), 2);
    KITE_EXPECT_EQ(tab.MarkedBytes(), uint64_t{ 4096 + 2048 });

    const std::vector<std::string> paths = tab.SelectionPaths();
    KITE_EXPECT_EQ(paths.size(), size_t{ 2 });

    tab.ClearMarks();
    KITE_EXPECT_EQ(tab.MarkedCount(), 0);
}

KITE_TEST(tab, mark_range_is_order_independent_and_clamped) {
    Tab tab = MakeTab();
    tab.MarkRange(RowOf(tab, 3), RowOf(tab, 1), true);
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

KITE_TEST(tab, a_parent_row_leads_the_list_unless_the_folder_is_a_root) {
    Tab tab = MakeTab();
    KITE_EXPECT(tab.hasParentRow());
    KITE_EXPECT(tab.IsParentRow(0));
    KITE_EXPECT(tab.EntryAt(0) == nullptr);
    KITE_EXPECT_EQ(tab.visible.size(), size_t{ 6 });  // 5 items plus ".."
    KITE_EXPECT_EQ(tab.ItemCount(), 5);

    // A drive root is no longer the top: above it is "PC" (vfs::ParentOf), so
    // the row stays and Alt+Up leads out of the filesystem rather than nowhere.
    tab.path = "C:\\";
    tab.Rebuild();
    KITE_EXPECT(tab.hasParentRow());
    KITE_EXPECT_EQ(tab.visible.size(), size_t{ 6 });

    // A virtual root really is the top.
    tab.path = vfs::kComputer;
    tab.Rebuild();
    KITE_EXPECT_FALSE(tab.hasParentRow());
    KITE_EXPECT_EQ(tab.visible.size(), size_t{ 5 });
}

KITE_TEST(tab, the_parent_row_ignores_sorting_and_filtering) {
    Tab tab = MakeTab();
    tab.view.sortDesc = true;
    tab.view.sort = SortKey::Date;
    tab.filter = "image";
    tab.Rebuild();
    // Two matches, and ".." still on top: it is a move, not an item.
    KITE_EXPECT(tab.IsParentRow(0));
    KITE_EXPECT_EQ(tab.ItemCount(), 2);
}

KITE_TEST(tab, the_parent_row_can_never_be_selected) {
    Tab tab = MakeTab();
    tab.MarkRange(0, static_cast<int>(tab.visible.size()) - 1, true);
    KITE_EXPECT_EQ(tab.MarkedCount(), 5);  // not 6

    // A cursor parked on ".." offers nothing to operate on, so commands that
    // fall back to the cursor row do nothing rather than acting on the folder.
    tab.ClearMarks();
    tab.cursor = 0;
    KITE_EXPECT(tab.CursorEntry() == nullptr);
    KITE_EXPECT_EQ(tab.CursorPath(), std::string(""));
    KITE_EXPECT_EQ(tab.SelectionPaths().size(), size_t{ 0 });
}

KITE_TEST(tab, a_rebuild_keeps_the_cursor_on_the_parent_row_but_never_parks_it_there) {
    Tab tab = MakeTab();
    tab.cursor = 0;
    tab.Rebuild();
    KITE_EXPECT_EQ(tab.cursor, 0);  // the user put it there; leave it

    // A fresh listing starts on the first real item instead.
    Tab fresh = MakeTab();
    KITE_EXPECT_EQ(CursorName(fresh), std::string("alpha"));
}

KITE_TEST(tab, an_unreadable_folder_gets_no_parent_row) {
    // The pane draws the error instead of the list, so a row nobody can reach
    // would only be a cursor trap.
    Tab tab;
    tab.path = "C:\\home\\gone";
    tab.listing.status = fs::Status::NotFound;
    tab.Rebuild();
    KITE_EXPECT_EQ(tab.visible.size(), size_t{ 0 });
}

KITE_TEST(tab, extending_rebuilds_the_range_instead_of_piling_marks_up) {
    Tab tab = MakeTab();
    tab.cursor = RowOf(tab, 0);
    tab.ResetAnchor();

    tab.ExtendTo(RowOf(tab, 3));
    KITE_EXPECT_EQ(tab.MarkedCount(), 4);

    // Shrinking the range has to release what it passes back over.
    tab.ExtendTo(RowOf(tab, 1));
    KITE_EXPECT_EQ(tab.MarkedCount(), 2);
}

KITE_TEST(tab, extending_keeps_marks_made_outside_the_range) {
    // Space-marking scattered rows and then shift-extending elsewhere must not
    // throw the scattered marks away.
    Tab tab = MakeTab();
    const int last = RowOf(tab, 4);
    tab.marked[tab.visible[last]] = 1;

    tab.cursor = RowOf(tab, 0);
    tab.ResetAnchor();
    tab.ExtendTo(RowOf(tab, 1));
    KITE_EXPECT_EQ(tab.MarkedCount(), 3);  // two extended plus the stray one

    tab.ExtendTo(RowOf(tab, 0));
    KITE_EXPECT_EQ(tab.MarkedCount(), 2);
    // ResetAnchor ends the run, so the next extension starts from what is there.
    tab.ResetAnchor();
    tab.ExtendTo(RowOf(tab, 2));
    KITE_EXPECT_EQ(tab.MarkedCount(), 4);
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

// --- grouping ---------------------------------------------------------------

KITE_TEST(tab, grouping_inserts_headings_without_changing_the_item_count) {
    Tab tab = MakeTab();
    const int before = tab.ItemCount();

    tab.view.grouped = true;
    tab.Rebuild();

    // Folders first, so they are their own block; the files group by initial.
    KITE_EXPECT_EQ(static_cast<int>(tab.groups.size()), 3);
    KITE_EXPECT_EQ(tab.ItemCount(), before);
    KITE_EXPECT_EQ(tab.visible.size(), static_cast<size_t>(before + 3 + 1));  // +".."

    KITE_EXPECT_EQ(tab.groups[0].labelKey, std::string("ui.group_folders"));
    KITE_EXPECT_EQ(tab.groups[0].count, 2);
    KITE_EXPECT_EQ(tab.groups[1].text, std::string("I"));
    KITE_EXPECT_EQ(tab.groups[1].count, 2);
    KITE_EXPECT_EQ(tab.groups[2].text, std::string("N"));
    KITE_EXPECT_EQ(tab.groups[2].count, 1);
}

KITE_TEST(tab, a_heading_points_at_its_own_row) {
    Tab tab = MakeTab();
    tab.view.grouped = true;
    tab.Rebuild();

    for (const Tab::Group& group : tab.groups) {
        // ".." shifted every heading down by one; forgetting that fix would make
        // a heading name the last item of its block.
        KITE_EXPECT(tab.IsGroupRow(group.firstRow));
        KITE_EXPECT_EQ(tab.GroupAt(group.firstRow)->count, group.count);
        KITE_EXPECT(tab.EntryAt(group.firstRow) == nullptr);
        KITE_EXPECT(tab.EntryAt(group.firstRow + 1) != nullptr);
    }
}

KITE_TEST(tab, the_cursor_never_lands_on_a_heading) {
    Tab tab = MakeTab();
    tab.view.grouped = true;
    tab.Rebuild();
    KITE_EXPECT_FALSE(tab.IsGroupRow(tab.cursor));

    // Whichever way it is asked, the answer is a row that holds something.
    for (int row = 0; row < static_cast<int>(tab.visible.size()); ++row) {
        KITE_EXPECT_FALSE(tab.IsGroupRow(tab.SkipGroupRows(row, 1)));
        KITE_EXPECT_FALSE(tab.IsGroupRow(tab.SkipGroupRows(row, -1)));
    }
    // Past the last heading there is always an item, so going forward from the
    // last row of all still answers with a row.
    const int last = static_cast<int>(tab.visible.size()) - 1;
    KITE_EXPECT_FALSE(tab.IsGroupRow(tab.SkipGroupRows(last, 1)));
}

KITE_TEST(tab, marking_across_a_heading_marks_only_items) {
    Tab tab = MakeTab();
    tab.view.grouped = true;
    tab.Rebuild();

    tab.MarkRange(0, static_cast<int>(tab.visible.size()) - 1, true);
    KITE_EXPECT_EQ(tab.MarkedCount(), tab.ItemCount());

    // One block on its own: the rows after its heading and nothing else.
    tab.ClearMarks();
    const Tab::Group& folders = tab.groups[0];
    tab.MarkRange(folders.firstRow, folders.firstRow + folders.count, true);
    KITE_EXPECT_EQ(tab.MarkedCount(), folders.count);
}

KITE_TEST(tab, extending_onto_a_heading_steps_over_it) {
    Tab tab = MakeTab();
    tab.view.grouped = true;
    tab.Rebuild();

    // Stand on the last folder; the next row down is the files' heading.
    const Tab::Group& folders = tab.groups[0];
    tab.cursor = folders.firstRow + folders.count;
    tab.ResetAnchor();
    KITE_EXPECT(tab.IsGroupRow(tab.cursor + 1));

    tab.ExtendTo(tab.cursor + 1);
    KITE_EXPECT_FALSE(tab.IsGroupRow(tab.cursor));
    KITE_EXPECT_EQ(tab.cursor, folders.firstRow + folders.count + 2);
    KITE_EXPECT_EQ(tab.MarkedCount(), 2);  // the folder and the first file
}

KITE_TEST(tab, size_grouping_buckets_files_and_keeps_folders_apart) {
    Tab tab = MakeTab();
    tab.view.grouped = true;
    tab.view.sort = SortKey::Size;
    tab.Rebuild();

    KITE_EXPECT_EQ(tab.groups[0].labelKey, std::string("ui.group_folders"));
    // 120 B, 2 KB and 4 KB all sit under a megabyte, so the files are one block.
    KITE_EXPECT_EQ(static_cast<int>(tab.groups.size()), 2);
    KITE_EXPECT_EQ(tab.groups[1].text, std::string("< 1 MB"));
    KITE_EXPECT_EQ(tab.groups[1].count, 3);
}

KITE_TEST(tab, turning_grouping_off_removes_every_heading) {
    Tab tab = MakeTab();
    tab.view.grouped = true;
    tab.Rebuild();
    const int rows = static_cast<int>(tab.visible.size());

    tab.view.grouped = false;
    tab.Rebuild();
    KITE_EXPECT_EQ(static_cast<int>(tab.groups.size()), 0);
    KITE_EXPECT_EQ(static_cast<int>(tab.visible.size()), rows - 3);
    KITE_EXPECT_FALSE(tab.IsGroupRow(tab.cursor));
}
