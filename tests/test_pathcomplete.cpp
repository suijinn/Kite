// The address bar's completion.
//
// It never enumerates anything itself, so these tests hand it listings the way
// App does once DirectoryLoader comes back: text in, candidates out.
#include "TestFramework.h"
#include "core/input/PathComplete.h"

using namespace kite;

namespace {

fs::Entry Dir(const std::string& name, fs::Attr extra = fs::Attr::None) {
    fs::Entry e;
    e.name = name;
    e.attrs = fs::Attr::Directory | extra;
    return e;
}

fs::Entry File(const std::string& name) {
    fs::Entry e;
    e.name = name;
    return e;
}

// "C:\home" with a couple of folders, a file, and one hidden folder. Deliberately
// out of order: the enumeration order is the OS's, and the offer's is Kite's.
std::vector<fs::Entry> Home() {
    return { Dir("beta"), File("notes.txt"), Dir("Alpha"), Dir(".git", fs::Attr::Hidden),
             Dir("archive") };
}

struct Harness {
    PathComplete pc;

    // Type a whole path, as the prompt does one character at a time.
    void Type(const std::string& text) {
        pc.SetInput(text);
        pc.Open();
    }

    void Listed(const std::string& dir = "C:\\home") { pc.SetListing(dir, Home()); }

    std::string Matches() const {
        std::string out;
        for (const std::string& m : pc.matches()) {
            if (!out.empty()) out += ",";
            out += m;
        }
        return out;
    }
};

}  // namespace

KITE_TEST(pathcomplete, splits_the_text_at_the_last_separator) {
    Harness h;
    h.Type("C:\\home\\al");
    KITE_EXPECT_EQ(h.pc.dir(), std::string("C:\\home"));
    KITE_EXPECT_EQ(h.pc.prefix(), std::string("al"));

    // Nothing is offered until the folder has actually been listed.
    KITE_EXPECT(h.pc.wantsListing());
    KITE_EXPECT_EQ(h.pc.matches().size(), size_t{ 0 });

    h.Listed();
    KITE_EXPECT_FALSE(h.pc.wantsListing());
    KITE_EXPECT_EQ(h.Matches(), std::string("Alpha"));
}

KITE_TEST(pathcomplete, a_trailing_separator_asks_for_everything_in_the_folder) {
    Harness h;
    h.Type("C:\\home\\");
    KITE_EXPECT_EQ(h.pc.dir(), std::string("C:\\home"));
    KITE_EXPECT_EQ(h.pc.prefix(), std::string(""));
    h.Listed();

    // Folders only, in natural order - never the file, and not .git while
    // nothing has been typed.
    KITE_EXPECT_EQ(h.Matches(), std::string("Alpha,archive,beta"));
}

KITE_TEST(pathcomplete, a_started_name_brings_hidden_folders_back) {
    Harness h;
    h.Type("C:\\home\\.");
    h.Listed();
    KITE_EXPECT_EQ(h.Matches(), std::string(".git"));
}

KITE_TEST(pathcomplete, matching_ignores_case) {
    Harness h;
    h.Type("C:\\home\\A");
    h.Listed();
    KITE_EXPECT_EQ(h.Matches(), std::string("Alpha,archive"));

    h.Type("C:\\home\\a");
    KITE_EXPECT_EQ(h.Matches(), std::string("Alpha,archive"));
}

KITE_TEST(pathcomplete, forward_slashes_and_dot_segments_still_find_the_folder) {
    Harness h;
    h.Type("C:/home/alpha/../be");
    KITE_EXPECT_EQ(h.pc.dir(), std::string("C:\\home"));
    h.Listed();
    KITE_EXPECT_EQ(h.Matches(), std::string("beta"));
    KITE_EXPECT(h.pc.Move(1));
    KITE_EXPECT_EQ(h.pc.text(), std::string("C:\\home\\beta"));
}

KITE_TEST(pathcomplete, cycling_ends_back_at_what_was_typed) {
    Harness h;
    h.Type("C:\\home\\a");
    h.Listed();
    KITE_EXPECT_EQ(h.pc.selected(), -1);
    KITE_EXPECT_EQ(h.pc.text(), std::string("C:\\home\\a"));

    KITE_EXPECT(h.pc.Move(1));
    KITE_EXPECT_EQ(h.pc.text(), std::string("C:\\home\\Alpha"));
    KITE_EXPECT(h.pc.Move(1));
    KITE_EXPECT_EQ(h.pc.text(), std::string("C:\\home\\archive"));

    // Past the last one is the typed text again, not a wrap straight back to
    // the first: overshooting has to have a way home.
    KITE_EXPECT(h.pc.Move(1));
    KITE_EXPECT_EQ(h.pc.selected(), -1);
    KITE_EXPECT_EQ(h.pc.text(), std::string("C:\\home\\a"));

    // And backwards from there is the last candidate.
    KITE_EXPECT(h.pc.Move(-1));
    KITE_EXPECT_EQ(h.pc.text(), std::string("C:\\home\\archive"));
}

KITE_TEST(pathcomplete, typing_on_drops_the_selection) {
    Harness h;
    h.Type("C:\\home\\a");
    h.Listed();
    h.pc.Move(1);
    KITE_EXPECT_EQ(h.pc.selected(), 0);

    h.Type("C:\\home\\ar");
    KITE_EXPECT_EQ(h.pc.selected(), -1);
    KITE_EXPECT_EQ(h.pc.text(), std::string("C:\\home\\ar"));
    KITE_EXPECT_EQ(h.Matches(), std::string("archive"));
}

KITE_TEST(pathcomplete, selecting_by_index_replaces_the_text) {
    Harness h;
    h.Type("C:\\home\\");
    h.Listed();
    KITE_EXPECT_FALSE(h.pc.Select(9));
    KITE_EXPECT(h.pc.Select(2));
    KITE_EXPECT_EQ(h.pc.text(), std::string("C:\\home\\beta"));
}

KITE_TEST(pathcomplete, every_candidate_reads_as_a_whole_path) {
    Harness h;
    // Typed with slashes and a ".." in it: the leaf name alone would not say
    // where taking the candidate would actually land.
    h.Type("C:/home/beta/../a");
    h.Listed();
    KITE_EXPECT_EQ(h.pc.TextAt(0), std::string("C:\\home\\Alpha"));
    KITE_EXPECT_EQ(h.pc.TextAt(1), std::string("C:\\home\\archive"));
    KITE_EXPECT_EQ(h.pc.TextAt(-1), std::string(""));
    KITE_EXPECT_EQ(h.pc.TextAt(2), std::string(""));

    // What is drawn is what taking it puts in the field.
    h.pc.Move(1);
    KITE_EXPECT_EQ(h.pc.text(), h.pc.TextAt(h.pc.selected()));
}

KITE_TEST(pathcomplete, a_root_completes_from_the_drive) {
    Harness h;
    h.Type("C:\\ho");
    KITE_EXPECT_EQ(h.pc.dir(), std::string("C:\\"));
    h.pc.SetListing("C:\\", { Dir("home"), Dir("Windows") });
    KITE_EXPECT_EQ(h.Matches(), std::string("home"));
    h.pc.Move(1);
    // Join must not double the separator that the root already ends with.
    KITE_EXPECT_EQ(h.pc.text(), std::string("C:\\home"));
}

KITE_TEST(pathcomplete, relative_input_asks_for_nothing) {
    Harness h;
    // The prompt hands its text to NavigateFocused, which resolves nothing
    // against a working directory - so there is no folder to look in.
    h.Type("home\\al");
    KITE_EXPECT_EQ(h.pc.dir(), std::string(""));
    KITE_EXPECT_FALSE(h.pc.wantsListing());

    h.Type("C:\\home");  // no separator yet after the drive's own
    KITE_EXPECT_EQ(h.pc.dir(), std::string("C:\\"));
}

KITE_TEST(pathcomplete, a_listing_for_a_folder_left_behind_offers_nothing) {
    Harness h;
    h.Type("C:\\home\\alpha\\ne");
    // The answer for the folder above arrives late, because the user kept
    // typing while it was in flight.
    h.Listed("C:\\home");
    KITE_EXPECT_EQ(h.pc.matches().size(), size_t{ 0 });
    // ... and the folder that is actually being typed is still wanted.
    KITE_EXPECT(h.pc.wantsListing());
    KITE_EXPECT_EQ(h.pc.dir(), std::string("C:\\home\\alpha"));
}

KITE_TEST(pathcomplete, a_folder_that_cannot_be_listed_is_asked_for_once) {
    Harness h;
    h.Type("C:\\nowhere\\x");
    KITE_EXPECT(h.pc.wantsListing());
    h.pc.SetListing("C:\\nowhere", {});
    KITE_EXPECT_FALSE(h.pc.wantsListing());
    KITE_EXPECT_EQ(h.pc.matches().size(), size_t{ 0 });
}

KITE_TEST(pathcomplete, folding_the_list_keeps_the_chosen_text) {
    Harness h;
    h.Type("C:\\home\\a");
    h.Listed();
    h.pc.Move(1);
    h.pc.Close();
    KITE_EXPECT_FALSE(h.pc.open());
    KITE_EXPECT_FALSE(h.pc.wantsListing());  // folded means no more enumeration
    KITE_EXPECT_EQ(h.pc.text(), std::string("C:\\home\\Alpha"));

    // Reopening does not have to list the folder again.
    h.pc.Open();
    KITE_EXPECT_FALSE(h.pc.wantsListing());
}
