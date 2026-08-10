// The path -> icon-id table behind the shell icons.
//
// Everything the icon path decides without touching the OS lives here: what gets
// asked for, what gets asked for only once, and what is thrown away when the
// table grows past its limit. The fetching itself is platform code and is not
// covered by these.
#include "TestFramework.h"
#include "core/app/IconProvider.h"

using namespace kite;

namespace {

/// Pulls the queue the way the provider's Pump() does.
std::vector<std::string> Take(IconCache& cache, size_t max = 64) {
    std::vector<std::string> out;
    cache.TakePending(out, max);
    return out;
}

}  // namespace

KITE_TEST(iconcache, an_unknown_path_returns_nothing_and_queues_a_request) {
    IconCache cache;
    KITE_EXPECT_EQ(cache.Lookup("C:\\a.txt"), 0u);
    KITE_EXPECT_EQ(cache.pendingCount(), size_t{ 1 });

    const std::vector<std::string> wanted = Take(cache);
    KITE_EXPECT_EQ(wanted.size(), size_t{ 1 });
    KITE_EXPECT_EQ(wanted[0], std::string("C:\\a.txt"));
}

KITE_TEST(iconcache, a_path_is_only_asked_for_once) {
    // Lookup runs for every visible row on every frame - 60 times a second on a
    // full list. Asking again each time would flood the host.
    IconCache cache;
    for (int i = 0; i < 100; ++i) cache.Lookup("C:\\a.txt");
    KITE_EXPECT_EQ(cache.pendingCount(), size_t{ 1 });

    Take(cache);
    for (int i = 0; i < 100; ++i) cache.Lookup("C:\\a.txt");
    // Still in flight: no second request while the first is out.
    KITE_EXPECT_EQ(cache.pendingCount(), size_t{ 0 });

    cache.Store("C:\\a.txt", 7);
    for (int i = 0; i < 100; ++i) KITE_EXPECT_EQ(cache.Lookup("C:\\a.txt"), 7u);
    KITE_EXPECT_EQ(cache.pendingCount(), size_t{ 0 });
}

KITE_TEST(iconcache, a_path_with_no_icon_is_remembered_as_such) {
    // 0 is a real answer, not a missing one. Treating it as missing would ask
    // again every frame for the rest of the session.
    IconCache cache;
    cache.Lookup("C:\\weird");
    Take(cache);
    cache.Store("C:\\weird", 0);

    KITE_EXPECT_EQ(cache.Lookup("C:\\weird"), 0u);
    KITE_EXPECT_EQ(cache.pendingCount(), size_t{ 0 });
}

KITE_TEST(iconcache, the_newest_requests_are_served_first) {
    // A fast scroll leaves a queue of rows nobody is looking at any more. The
    // ones that just came into view are the ones worth fetching.
    IconCache cache;
    for (int i = 0; i < 10; ++i) cache.Lookup("C:\\f" + std::to_string(i));

    const std::vector<std::string> wanted = Take(cache, 3);
    KITE_EXPECT_EQ(wanted.size(), size_t{ 3 });
    KITE_EXPECT_EQ(wanted[0], std::string("C:\\f9"));
    KITE_EXPECT_EQ(wanted[2], std::string("C:\\f7"));
    KITE_EXPECT_EQ(cache.pendingCount(), size_t{ 7 });
}

KITE_TEST(iconcache, a_batch_that_never_came_back_can_be_asked_for_again) {
    // The host died mid-batch. Without this the rows would wait forever on a
    // reply that is not coming.
    IconCache cache;
    cache.Lookup("C:\\a");
    cache.Lookup("C:\\b");
    const std::vector<std::string> sent = Take(cache);
    KITE_EXPECT_EQ(cache.pendingCount(), size_t{ 0 });

    cache.Reset(sent);
    KITE_EXPECT_EQ(cache.Lookup("C:\\a"), 0u);
    KITE_EXPECT_EQ(cache.pendingCount(), size_t{ 1 });
}

KITE_TEST(iconcache, resetting_does_not_undo_an_answer_that_already_arrived) {
    IconCache cache;
    cache.Lookup("C:\\a");
    const std::vector<std::string> sent = Take(cache);
    cache.Store("C:\\a", 3);

    cache.Reset(sent);
    KITE_EXPECT_EQ(cache.Lookup("C:\\a"), 3u);
    KITE_EXPECT_EQ(cache.pendingCount(), size_t{ 0 });
}

KITE_TEST(iconcache, a_result_for_a_forgotten_path_is_dropped) {
    IconCache cache;
    cache.Lookup("C:\\a");
    Take(cache);
    cache.Clear();

    cache.Store("C:\\a", 5);
    KITE_EXPECT_EQ(cache.size(), size_t{ 0 });
    // And the path is treated as new the next time it is drawn.
    KITE_EXPECT_EQ(cache.Lookup("C:\\a"), 0u);
}

KITE_TEST(iconcache, the_table_stops_growing_and_keeps_what_is_in_use) {
    // Overlays make the icon depend on the path, so a long session walking a
    // large tree would otherwise accumulate an entry per file ever seen.
    IconCache cache;
    cache.SetCapacity(100);

    for (int i = 0; i < 500; ++i) {
        const std::string path = "C:\\f" + std::to_string(i);
        cache.Lookup(path);
        Take(cache);
        cache.Store(path, static_cast<uint32_t>(i + 1));
    }
    KITE_EXPECT(cache.size() <= size_t{ 100 });

    // The most recent entries survived; the oldest did not.
    KITE_EXPECT_EQ(cache.Lookup("C:\\f499"), 500u);
    KITE_EXPECT_EQ(cache.Lookup("C:\\f0"), 0u);
}

KITE_TEST(iconcache, eviction_never_strands_a_request_that_is_still_out) {
    // Dropping an in-flight entry would leave the arriving reply with nowhere to
    // go, and the row would stay glyph-only until it scrolled away and back.
    IconCache cache;
    cache.SetCapacity(10);

    cache.Lookup("C:\\pending");
    const std::vector<std::string> sent = Take(cache);
    for (int i = 0; i < 200; ++i) {
        cache.Lookup("C:\\f" + std::to_string(i));
    }

    cache.Store("C:\\pending", 42);
    KITE_EXPECT_EQ(cache.Lookup("C:\\pending"), 42u);
    KITE_EXPECT_EQ(sent.size(), size_t{ 1 });
}

KITE_TEST(iconcache, an_empty_path_is_never_asked_about) {
    IconCache cache;
    KITE_EXPECT_EQ(cache.Lookup(""), 0u);
    KITE_EXPECT_EQ(cache.pendingCount(), size_t{ 0 });
    KITE_EXPECT_EQ(cache.size(), size_t{ 0 });
}
