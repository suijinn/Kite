// Drop-target validation. This is the one piece of drag & drop that can
// destroy data if it is wrong, so it lives in the OS-independent core and is
// tested directly.
#include "Fakes.h"
#include "TestFramework.h"

using namespace kite;

KITE_TEST(drop, accepts_a_normal_destination) {
    KITE_EXPECT(App::IsValidDropTarget({ "C:\\home\\a.txt" }, "C:\\other"));
    KITE_EXPECT(App::IsValidDropTarget({ "C:\\home\\alpha" }, "C:\\other"));
}

KITE_TEST(drop, rejects_empty_input) {
    KITE_EXPECT_FALSE(App::IsValidDropTarget({}, "C:\\other"));
    KITE_EXPECT_FALSE(App::IsValidDropTarget({ "C:\\a" }, ""));
}

KITE_TEST(drop, rejects_a_folder_dropped_onto_itself) {
    KITE_EXPECT_FALSE(App::IsValidDropTarget({ "C:\\home\\alpha" }, "C:\\home\\alpha"));
    // Case and separator style must not sneak past the check.
    KITE_EXPECT_FALSE(App::IsValidDropTarget({ "C:\\home\\alpha" }, "c:/home/alpha"));
}

KITE_TEST(drop, rejects_a_folder_dropped_into_its_own_subtree) {
    // The dangerous case: moving a folder inside itself.
    KITE_EXPECT_FALSE(App::IsValidDropTarget({ "C:\\home\\alpha" }, "C:\\home\\alpha\\nested"));
    KITE_EXPECT_FALSE(
        App::IsValidDropTarget({ "C:\\home\\alpha" }, "C:\\home\\alpha\\nested\\deeper"));
}

KITE_TEST(drop, a_sibling_with_a_shared_prefix_is_still_valid) {
    // "alpha2" starts with "alpha" but is not inside it; a naive prefix test
    // gets this wrong.
    KITE_EXPECT(App::IsValidDropTarget({ "C:\\home\\alpha" }, "C:\\home\\alpha2"));
}

KITE_TEST(drop, one_bad_source_invalidates_the_whole_drop) {
    const std::vector<std::string> paths = { "C:\\home\\ok.txt", "C:\\home\\alpha" };
    KITE_EXPECT_FALSE(App::IsValidDropTarget(paths, "C:\\home\\alpha\\nested"));
}

KITE_TEST(drop, dropping_into_the_parent_folder_is_allowed) {
    // Copying next to itself is legitimate; only a *move* there is a no-op,
    // and PerformDrop filters that separately.
    KITE_EXPECT(App::IsValidDropTarget({ "C:\\home\\alpha\\inner.md" }, "C:\\home\\alpha"));
}

KITE_TEST(drop, perform_drop_copies_through_the_filesystem) {
    test::ResetFakePlatform();
    test::FakeFileSystem files;
    test::PopulateStandardTree(files);
    test::FakeShell shell;
    test::FakeHost host;

    App app(files, shell, host);
    app.Init({});
    test::PumpUntilSettled(app);

    KITE_EXPECT(app.PerformDrop({ "C:\\home\\notes.txt" }, "C:\\home\\alpha", false));
    KITE_EXPECT_EQ(files.copyCalls.size(), size_t{ 1 });
    KITE_EXPECT_EQ(files.copyCalls[0].destDir, std::string("C:\\home\\alpha"));
    KITE_EXPECT_FALSE(files.copyCalls[0].move);
}

KITE_TEST(drop, perform_drop_refuses_an_invalid_target_without_touching_the_disk) {
    test::ResetFakePlatform();
    test::FakeFileSystem files;
    test::PopulateStandardTree(files);
    test::FakeShell shell;
    test::FakeHost host;

    App app(files, shell, host);
    app.Init({});
    test::PumpUntilSettled(app);

    KITE_EXPECT_FALSE(
        app.PerformDrop({ "C:\\home\\alpha" }, "C:\\home\\alpha\\nested", true));
    KITE_EXPECT_EQ(files.copyCalls.size(), size_t{ 0 });
}

KITE_TEST(drop, moving_into_the_current_parent_is_dropped_as_a_no_op) {
    test::ResetFakePlatform();
    test::FakeFileSystem files;
    test::PopulateStandardTree(files);
    test::FakeShell shell;
    test::FakeHost host;

    App app(files, shell, host);
    app.Init({});
    test::PumpUntilSettled(app);

    // notes.txt already lives in C:\home; moving it there changes nothing and
    // must not reach the filesystem.
    KITE_EXPECT_FALSE(app.PerformDrop({ "C:\\home\\notes.txt" }, "C:\\home", true));
    KITE_EXPECT_EQ(files.copyCalls.size(), size_t{ 0 });

    // The same drop as a copy is meaningful, so it goes through.
    KITE_EXPECT(app.PerformDrop({ "C:\\home\\notes.txt" }, "C:\\home", false));
    KITE_EXPECT_EQ(files.copyCalls.size(), size_t{ 1 });
}
