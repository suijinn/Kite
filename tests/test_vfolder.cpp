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

KITE_TEST(vfolder, an_archive_is_named_by_its_extension_alone) {
    KITE_EXPECT(vfs::IsArchiveName("C:\\a\\pack.zip"));
    KITE_EXPECT(vfs::IsArchiveName("driver.CAB"));  // Extension() answers in lower case
    KITE_EXPECT_FALSE(vfs::IsArchiveName("C:\\a\\pack.7z"));
    KITE_EXPECT_FALSE(vfs::IsArchiveName("C:\\a\\notes.txt"));
    KITE_EXPECT_FALSE(vfs::IsArchiveName("C:\\a"));
    // Only what the shell opens as a folder on a stock Windows. A .7z reaches
    // the context menu through an extractor but never the namespace, so it
    // would name a place with nothing to enumerate it.
    KITE_EXPECT_FALSE(vfs::IsArchiveExtension("rar"));
}

KITE_TEST(vfolder, opening_an_archive_puts_it_in_the_shell_namespace) {
    const std::string zip = vfs::ArchivePath("C:\\a\\pack.zip");
    KITE_EXPECT_EQ(zip, std::string("virtual:C:\\a\\pack.zip"));
    // Which is what makes everything else fall into place: no writing, no
    // watcher, no completion, and the listing goes through the shell host.
    KITE_EXPECT(vfs::IsVirtual(zip));
    KITE_EXPECT_EQ(vfs::ArchiveFileOf(zip), std::string("C:\\a\\pack.zip"));
    KITE_EXPECT_EQ(vfs::ArchiveFileOf(zip + "\\docs\\notes.txt"), std::string("C:\\a\\pack.zip"));

    // A path that never entered an archive has no archive above it.
    KITE_EXPECT_EQ(vfs::ArchiveFileOf(vfs::kComputer), std::string(""));
    KITE_EXPECT_EQ(vfs::ArchiveFileOf("virtual:::{AAA}"), std::string(""));
    KITE_EXPECT_EQ(vfs::ArchiveFileOf("C:\\a\\pack.zip"), std::string(""));

    // The outer one wins: past the first archive the shell is answering, so a
    // .zip found inside is not a file the filesystem holds.
    KITE_EXPECT_EQ(vfs::ArchiveFileOf("virtual:C:\\a\\pack.zip\\inner.zip"),
                   std::string("C:\\a\\pack.zip"));
}

KITE_TEST(vfolder, the_archive_itself_is_where_the_walk_leaves_the_namespace) {
    // Inside, ".." steps back one component like any other virtual path.
    KITE_EXPECT_EQ(vfs::ParentOf("virtual:C:\\a\\pack.zip\\docs"),
                   std::string("virtual:C:\\a\\pack.zip"));
    // At the archive itself it lands in the real folder holding the file - the
    // seam. Stopping at "virtual:C:\\a" would name a place that is not a place,
    // and stopping altogether would strand the tab inside the zip.
    KITE_EXPECT_EQ(vfs::ParentOf("virtual:C:\\a\\pack.zip"), std::string("C:\\a"));
    KITE_EXPECT_EQ(vfs::ParentOf("virtual:C:\\pack.zip"), std::string("C:\\"));
    // A nested archive stops at the one containing it, which is still virtual:
    // nothing inside an archive is on disk.
    KITE_EXPECT_EQ(vfs::ParentOf("virtual:C:\\a\\pack.zip\\inner.zip"),
                   std::string("virtual:C:\\a\\pack.zip"));
}

KITE_TEST(vfolder, an_archive_path_keeps_its_shape_through_normalize) {
    // Written to workspace.ini and read back, like any other tab path.
    KITE_EXPECT_EQ(path::Normalize("virtual:C:\\a\\pack.zip\\docs"),
                   std::string("virtual:C:\\a\\pack.zip\\docs"));
    // The drive letter still folds, so two spellings of one archive cannot
    // become two tabs.
    KITE_EXPECT_EQ(path::Normalize("virtual:c:/a/pack.zip"),
                   std::string("virtual:C:\\a\\pack.zip"));
    // And a zip on a share keeps both of its leading backslashes. Folding the
    // scheme as an ordinary component ate one of them, and a UNC path left with
    // a single backslash names nothing at all.
    KITE_EXPECT_EQ(path::Normalize("virtual:\\\\nas\\pub\\pack.zip"),
                   std::string("virtual:\\\\nas\\pub\\pack.zip"));
    KITE_EXPECT_EQ(vfs::ParentOf("virtual:\\\\nas\\pub\\pack.zip"), std::string("\\\\nas\\pub\\"));
}

KITE_TEST(vfolder, a_virtual_path_survives_the_trip_through_a_saved_session) {
    // A tab holding one of these is written to workspace.ini and read back on
    // the next launch. Normalize() runs on the way in and must leave it alone -
    // it has no root to fold, no "." or ".." to resolve, and a drive letter rule
    // that must not fire on the "virtual:" scheme.
    for (const char* p : { vfs::kComputer, vfs::kRecycleBin, vfs::kNetwork,
                           "virtual:::{AAA-BBB}\\::{CCC}", "virtual:C:\\a\\pack.zip" }) {
        KITE_EXPECT_EQ(path::Normalize(p), std::string(p));
        KITE_EXPECT_EQ(path::UnescapeToken(path::EscapeToken(p)), std::string(p));
        KITE_EXPECT(vfs::IsVirtual(path::Normalize(p)));
    }
}
