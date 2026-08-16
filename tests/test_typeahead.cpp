// The list's type-ahead jump.
//
// It owns neither the list nor the clock, so these tests hand it both: a fixed
// set of names and a time it is told to believe in.
#include "TestFramework.h"
#include "core/input/TypeAhead.h"

using namespace kite;

namespace {

// The rows of a folder, in the order they are drawn. The empty first row stands
// for "..", which is a way out rather than something to land on by name.
class Rows : public TypeAhead::IRows {
public:
    explicit Rows(std::vector<std::string> names) : names_(std::move(names)) {}

    int Count() const override { return static_cast<int>(names_.size()); }

    std::string_view NameAt(int index) const override { return names_[static_cast<size_t>(index)]; }

private:
    std::vector<std::string> names_;
};

Rows Home() {
    return Rows({ "", "alpha", "beta", "image2.png", "image10.png", "notes.txt" });
}

struct Harness {
    TypeAhead ta;
    Rows rows = Home();
    int cursor = 1;
    uint64_t now = 5000;

    // One keystroke, moving the cursor the way App does with the answer.
    TypeAhead::Jump Press(char c) {
        const TypeAhead::Jump jump = ta.Type(static_cast<uint32_t>(c), rows, cursor, now);
        if (jump.row >= 0) cursor = jump.row;
        return jump;
    }

    int Type(const std::string& text) {
        int row = -1;
        for (char c : text) row = Press(c).row;
        return row;
    }
};

}  // namespace

KITE_TEST(typeahead, jumps_to_the_first_row_starting_with_the_letter) {
    Harness h;
    KITE_EXPECT_EQ(h.Type("b"), 2);
    KITE_EXPECT_EQ(h.ta.text(), std::string("b"));
}

KITE_TEST(typeahead, folds_case_the_way_the_filesystem_does) {
    Harness h;
    KITE_EXPECT_EQ(h.Type("N"), 5);
}

KITE_TEST(typeahead, more_letters_narrow_the_answer) {
    Harness h;
    // "i" lands on image2.png; "im" keeps it, since adding to what is typed
    // searches from the cursor rather than past it.
    KITE_EXPECT_EQ(h.Press('i').row, 3);
    KITE_EXPECT_EQ(h.Press('m').row, 3);
    KITE_EXPECT_EQ(h.ta.text(), std::string("im"));
}

KITE_TEST(typeahead, the_same_letter_again_walks_to_the_next_match) {
    Harness h;
    KITE_EXPECT_EQ(h.Press('i').row, 3);
    KITE_EXPECT_EQ(h.Press('i').row, 4);
    // The buffer stays one letter long: the second press was a step, not a name.
    KITE_EXPECT_EQ(h.ta.text(), std::string("i"));
    // And it comes back round rather than stopping at the last one.
    KITE_EXPECT_EQ(h.Press('i').row, 3);
}

KITE_TEST(typeahead, a_real_double_letter_name_still_wins_over_stepping) {
    Harness h;
    h.rows = Rows({ "", "aardvark", "alpha", "apple" });
    h.cursor = 1;
    KITE_EXPECT_EQ(h.Press('a').row, 2);  // from the cursor, so alpha
    KITE_EXPECT_EQ(h.Press('a').row, 1);  // "aa" is a name here
    KITE_EXPECT_EQ(h.ta.text(), std::string("aa"));
}

KITE_TEST(typeahead, a_fresh_letter_steps_past_the_row_under_the_cursor) {
    Harness h;
    h.cursor = 3;  // image2.png
    // Otherwise pressing "i" would answer with the row already selected, and the
    // list would look frozen.
    KITE_EXPECT_EQ(h.Press('i').row, 4);
}

KITE_TEST(typeahead, wraps_around_the_end_of_the_list) {
    Harness h;
    h.cursor = 5;  // notes.txt, the last row
    KITE_EXPECT_EQ(h.Press('a').row, 1);
}

KITE_TEST(typeahead, never_lands_on_the_parent_row) {
    Harness h;
    h.rows = Rows({ "", "zulu" });
    h.cursor = 1;
    // The nameless row is passed over rather than answered with, so a folder
    // whose items match nothing leaves the cursor where it was.
    const TypeAhead::Jump jump = h.Press('q');
    KITE_EXPECT(jump.taken);
    KITE_EXPECT_EQ(jump.row, -1);
}

KITE_TEST(typeahead, a_letter_that_matches_nothing_is_not_kept) {
    Harness h;
    KITE_EXPECT_EQ(h.Press('n').row, 5);
    const TypeAhead::Jump miss = h.Press('q');
    KITE_EXPECT(miss.taken);
    KITE_EXPECT_EQ(miss.row, -1);
    // "nq" would make every following keystroke miss as well.
    KITE_EXPECT_EQ(h.ta.text(), std::string("n"));
}

KITE_TEST(typeahead, control_characters_are_left_alone) {
    Harness h;
    const TypeAhead::Jump esc = h.ta.Type(0x1B, h.rows, h.cursor, h.now);
    KITE_EXPECT_FALSE(esc.taken);
    KITE_EXPECT(h.ta.text().empty());
}

KITE_TEST(typeahead, a_space_on_its_own_is_left_to_the_selection_toggle) {
    Harness h;
    const TypeAhead::Jump space = h.ta.Type(' ', h.rows, h.cursor, h.now);
    KITE_EXPECT_FALSE(space.taken);
    KITE_EXPECT(h.ta.text().empty());
}

KITE_TEST(typeahead, a_space_inside_a_name_is_part_of_it) {
    Harness h;
    h.rows = Rows({ "", "my notes", "my pictures" });
    h.cursor = 1;
    KITE_EXPECT_EQ(h.Press('m').row, 2);
    KITE_EXPECT_EQ(h.Press('y').row, 2);
    KITE_EXPECT_EQ(h.Press(' ').row, 2);
    KITE_EXPECT_EQ(h.Press('n').row, 1);
    KITE_EXPECT_EQ(h.ta.text(), std::string("my n"));
}

KITE_TEST(typeahead, the_typed_name_expires) {
    Harness h;
    KITE_EXPECT_EQ(h.Press('i').row, 3);
    h.now += TypeAhead::kTimeoutMs + 1;
    KITE_EXPECT_FALSE(h.ta.active(h.now));
    // Long enough afterwards the same key starts over instead of stepping, so it
    // answers with the first "i" below the cursor again.
    KITE_EXPECT_EQ(h.Press('i').row, 4);
    KITE_EXPECT_EQ(h.ta.text(), std::string("i"));
}

KITE_TEST(typeahead, stays_alive_while_the_keys_keep_coming) {
    Harness h;
    h.Press('i');
    h.now += TypeAhead::kTimeoutMs;
    h.Press('m');
    h.now += TypeAhead::kTimeoutMs;
    KITE_EXPECT(h.ta.active(h.now));
    KITE_EXPECT_EQ(h.ta.text(), std::string("im"));
}

KITE_TEST(typeahead, backspace_shortens_what_was_typed) {
    Harness h;
    h.Type("ima");
    KITE_EXPECT(h.ta.Erase(h.now));
    KITE_EXPECT_EQ(h.ta.text(), std::string("im"));
    // Nothing left to erase, and nothing to erase after it expired either.
    KITE_EXPECT(h.ta.Erase(h.now));
    KITE_EXPECT(h.ta.Erase(h.now));
    KITE_EXPECT_FALSE(h.ta.Erase(h.now));
}

KITE_TEST(typeahead, backspace_does_nothing_once_the_name_has_expired) {
    Harness h;
    h.Press('i');
    h.now += TypeAhead::kTimeoutMs + 1;
    KITE_EXPECT_FALSE(h.ta.Erase(h.now));
}

KITE_TEST(typeahead, erasing_a_multibyte_character_takes_the_whole_character) {
    Harness h;
    h.rows = Rows({ "", "資料", "写真" });
    h.cursor = 1;
    // U+8CC7 U+6599 - three bytes each.
    h.ta.Type(0x8CC7, h.rows, h.cursor, h.now);
    h.ta.Type(0x6599, h.rows, h.cursor, h.now);
    KITE_EXPECT_EQ(h.ta.text().size(), size_t{ 6 });
    KITE_EXPECT(h.ta.Erase(h.now));
    KITE_EXPECT_EQ(h.ta.text().size(), size_t{ 3 });
}

KITE_TEST(typeahead, matches_names_outside_ascii) {
    Harness h;
    h.rows = Rows({ "", "写真", "資料" });
    h.cursor = 1;
    const TypeAhead::Jump jump = h.ta.Type(0x8CC7, h.rows, h.cursor, h.now);
    KITE_EXPECT_EQ(jump.row, 2);
}

KITE_TEST(typeahead, an_empty_list_has_nowhere_to_jump) {
    Harness h;
    h.rows = Rows({});
    const TypeAhead::Jump jump = h.ta.Type('a', h.rows, 0, h.now);
    KITE_EXPECT(jump.taken);
    KITE_EXPECT_EQ(jump.row, -1);
}

KITE_TEST(typeahead, clearing_starts_the_next_name_from_scratch) {
    Harness h;
    h.Type("im");
    h.ta.Clear();
    KITE_EXPECT_FALSE(h.ta.active(h.now));
    KITE_EXPECT_EQ(h.Press('n').row, 5);
}
