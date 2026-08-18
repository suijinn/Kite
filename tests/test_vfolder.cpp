#include "TestFramework.h"
#include "core/fs/FileSystem.h"
#include "core/base/PathUtil.h"
#include "core/fs/VirtualPath.h"

using namespace kite;

KITE_TEST(vfolder, prefix_tells_virtual_from_filesystem_paths) {
    KITE_EXPECT(vfs::IsVirtual(vfs::kComputer));
    KITE_EXPECT(vfs::IsVirtual(vfs::kRecycleBin));
    KITE_EXPECT(vfs::IsVirtual(vfs::kNetwork));
    KITE_EXPECT(vfs::IsVirtual("virtual:::{20D04FE0-3AEA-1069-A2D8-08002B30309D}"));

    KITE_EXPECT_FALSE(vfs::IsVirtual("C:\\a"));
    KITE_EXPECT_FALSE(vfs::IsVirtual("\\\\server\\share"));
    KITE_EXPECT_FALSE(vfs::IsVirtual("::{20D04FE0-3AEA-1069-A2D8-08002B30309D}"));
    // The prefix on its own names nothing.
    KITE_EXPECT_FALSE(vfs::IsVirtual("virtual:"));
    KITE_EXPECT_FALSE(vfs::IsVirtual(""));
}

KITE_TEST(vfolder, only_the_three_known_places_have_labels) {
    KITE_EXPECT(vfs::IsWellKnown(vfs::kComputer));
    KITE_EXPECT(vfs::IsWellKnown(vfs::kRecycleBin));
    KITE_EXPECT(vfs::IsWellKnown(vfs::kNetwork));
    KITE_EXPECT_FALSE(vfs::IsWellKnown("virtual:something-else"));
    KITE_EXPECT_FALSE(vfs::IsWellKnown("C:\\"));
    KITE_EXPECT(vfs::LabelKey("virtual:something-else") == nullptr);
    KITE_EXPECT(vfs::LabelKey(vfs::kComputer) != nullptr);
}

KITE_TEST(vfolder, the_three_places_are_roots) {
    KITE_EXPECT_EQ(vfs::ParentOf(vfs::kComputer), std::string(""));
    KITE_EXPECT_EQ(vfs::ParentOf(vfs::kRecycleBin), std::string(""));
    KITE_EXPECT_EQ(vfs::ParentOf(vfs::kNetwork), std::string(""));
}

KITE_TEST(vfolder, parent_of_a_drive_root_is_the_computer) {
    // The rule Explorer follows, and the reason "PC" is reachable with Alt+Up
    // rather than only from the sidebar.
    KITE_EXPECT_EQ(vfs::ParentOf("C:\\"), std::string(vfs::kComputer));
    KITE_EXPECT_EQ(vfs::ParentOf("Z:\\"), std::string(vfs::kComputer));
    // Anything below a root still walks the filesystem.
    KITE_EXPECT_EQ(vfs::ParentOf("C:\\Users"), std::string("C:\\"));
    KITE_EXPECT_EQ(vfs::ParentOf("C:\\Users\\me"), std::string("C:\\Users"));
}

KITE_TEST(vfolder, parent_of_a_server_is_the_network) {
    // A share still answers with its server - that rule came first and stands.
    KITE_EXPECT_EQ(vfs::ParentOf("\\\\nas\\pub"), std::string("\\\\nas"));
    KITE_EXPECT_EQ(vfs::ParentOf("\\\\nas"), std::string(vfs::kNetwork));
    KITE_EXPECT_EQ(vfs::ParentOf("\\\\nas\\"), std::string(vfs::kNetwork));
}

KITE_TEST(vfolder, a_relative_path_gets_no_invented_parent) {
    // "docs" is not at the top of C:, it is a path Kite cannot place at all.
    KITE_EXPECT_EQ(vfs::ParentOf("docs"), std::string(""));
    KITE_EXPECT_EQ(vfs::ParentOf(""), std::string(""));
    KITE_EXPECT_EQ(vfs::ParentOf("docs\\a"), std::string("docs"));
}

KITE_TEST(vfolder, nested_namespace_folders_walk_back_one_component) {
    const std::string child = "virtual:::{AAA}\\::{BBB}";
    KITE_EXPECT_EQ(vfs::ParentOf(child), std::string("virtual:::{AAA}"));
    KITE_EXPECT_EQ(vfs::ParentOf("virtual:::{AAA}"), std::string(""));
    KITE_EXPECT_EQ(vfs::TrailingName(child), std::string("::{BBB}"));
    KITE_EXPECT_EQ(vfs::TrailingName(vfs::kComputer), std::string("computer"));
    KITE_EXPECT_EQ(vfs::TrailingName("C:\\a"), std::string(""));
}

KITE_TEST(vfolder, entry_path_prefers_the_address_it_was_given) {
    fs::Entry plain;
    plain.name = "b";
    KITE_EXPECT_EQ(fs::EntryPath("C:\\a", plain), std::string("C:\\a\\b"));

    // Inside "PC" the name is a label ("Windows (C:)") and the address is what
    // the row actually points at.
    fs::Entry drive;
    drive.name = "Windows (C:)";
    drive.address = "C:\\";
    KITE_EXPECT_EQ(fs::EntryPath(vfs::kComputer, drive), std::string("C:\\"));
}

KITE_TEST(vfolder, a_virtual_path_survives_the_trip_through_a_saved_session) {
    // A tab holding one of these is written to workspace.ini and read back on
    // the next launch. Normalize() runs on the way in and must leave it alone -
    // it has no root to fold, no "." or ".." to resolve, and a drive letter rule
    // that must not fire on the "virtual:" scheme.
    for (const char* p : { vfs::kComputer, vfs::kRecycleBin, vfs::kNetwork,
                           "virtual:::{AAA-BBB}\\::{CCC}" }) {
        KITE_EXPECT_EQ(path::Normalize(p), std::string(p));
        KITE_EXPECT_EQ(path::UnescapeToken(path::EscapeToken(p)), std::string(p));
        KITE_EXPECT(vfs::IsVirtual(path::Normalize(p)));
    }
}
