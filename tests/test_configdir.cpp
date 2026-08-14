#include "TestFramework.h"
#include "core/app/ConfigDir.h"

using namespace kite;

namespace {

// The two candidates Kite actually hands over, in priority order.
const std::string kPortable = "C:\\Apps\\Kite\\config";
const std::string kRoaming = "C:\\Users\\me\\AppData\\Roaming\\Kite";

}  // namespace

KITE_TEST(config, portable_folder_wins_when_it_exists) {
    const std::string chosen =
        config::Choose({ { kPortable, true }, { kRoaming, true } });
    KITE_EXPECT_EQ(chosen, kPortable);
}

KITE_TEST(config, falls_back_to_roaming_when_the_portable_folder_is_absent) {
    const std::string chosen =
        config::Choose({ { kPortable, false }, { kRoaming, true } });
    KITE_EXPECT_EQ(chosen, kRoaming);
}

// First run on a fresh machine: nothing exists yet, and the answer is where the
// files will be created. It must be the last candidate - creating the first one
// would grow a config folder next to the exe for people who never asked for one.
KITE_TEST(config, creates_in_the_last_candidate_when_none_exist) {
    const std::string chosen =
        config::Choose({ { kPortable, false }, { kRoaming, false } });
    KITE_EXPECT_EQ(chosen, kRoaming);
}

// GetModuleFileNameW and SHGetKnownFolderPath both fail by returning nothing.
// An empty candidate is not a place, so it can neither be chosen nor become the
// fallback that later writes go to.
KITE_TEST(config, empty_candidates_are_skipped) {
    KITE_EXPECT_EQ(config::Choose({ { "", true }, { kRoaming, false } }), kRoaming);
    KITE_EXPECT_EQ(config::Choose({ { kPortable, false }, { "", false } }), kPortable);
    KITE_EXPECT_EQ(config::Choose({ { "", true }, { "", false } }), std::string());
}

KITE_TEST(config, no_candidates_yields_nothing) {
    KITE_EXPECT_EQ(config::Choose({}), std::string());
}

// Priority is the order of the list, not how many exist.
KITE_TEST(config, first_existing_candidate_wins_among_several) {
    const std::string chosen = config::Choose(
        { { "C:\\a", false }, { "C:\\b", true }, { "C:\\c", true } });
    KITE_EXPECT_EQ(chosen, std::string("C:\\b"));
}
