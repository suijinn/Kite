// The shortcut settings screen.
//
// Everything the screen decides lives here rather than in the painting code, so
// these tests drive it exactly the way the window does: chords in, rows out.
#include "TestFramework.h"
#include "core/i18n/Strings.h"
#include "core/input/KeyEditor.h"

using namespace kite;

namespace {

struct Harness {
    Strings strings;
    KeyMap keys;
    KeyEditor editor;

    Harness() {
        strings.Load("en");
        keys.LoadDefaults();
        editor.Open(strings, keys);
        // A fixed page height keeps PageUp / PageDown assertions meaningful; the
        // real one comes from the panel's height at paint time.
        editor.SetPageRows(20);
    }

    bool Key(const char* chord) { return editor.HandleKey(ParseChord(chord), keys, strings); }

    void Type(const char* text) {
        for (const char* p = text; *p; ++p) {
            editor.HandleChar(static_cast<uint32_t>(*p), strings, keys);
        }
    }

    // Moves the selection onto a named command, whatever the current filter is.
    bool Select(Cmd id) {
        for (size_t i = 0; i < editor.rows().size(); ++i) {
            if (editor.rows()[i].cmd == id) {
                editor.SelectRow(static_cast<int>(i));
                return true;
            }
        }
        return false;
    }

    const KeyEditor::Row* RowFor(Cmd id) const {
        for (const KeyEditor::Row& row : editor.rows()) {
            if (row.cmd == id) return &row;
        }
        return nullptr;
    }
};

}  // namespace

KITE_TEST(keyeditor, opens_with_every_command_and_a_selection) {
    Harness h;
    KITE_EXPECT(h.editor.visible());
    KITE_EXPECT_FALSE(h.editor.capturing());

    // The count on screen is commands only; group headings must not inflate it.
    KITE_EXPECT_EQ(static_cast<size_t>(h.editor.commandCount()), AllCommands().size());
    KITE_EXPECT(h.editor.rows().size() > AllCommands().size());

    // The selection starts on a real command, never on a group heading.
    KITE_EXPECT(h.editor.cursor() >= 0);
    KITE_EXPECT_FALSE(h.editor.rows()[h.editor.cursor()].header);
}

KITE_TEST(keyeditor, rows_show_every_chord_bound_to_a_command) {
    Harness h;
    const KeyEditor::Row* row = h.RowFor(Cmd::NextTab);
    KITE_EXPECT(row != nullptr);
    // Ctrl+Tab and Ctrl+PageDown both reach it; a sheet that showed only the
    // first would hide the one the user is looking for.
    KITE_EXPECT_NE(row->chords.find("Ctrl+Tab"), std::string::npos);
    KITE_EXPECT_NE(row->chords.find("Ctrl+PageDown"), std::string::npos);
}

KITE_TEST(keyeditor, arrow_keys_skip_over_group_headings) {
    Harness h;
    for (int i = 0; i < 200; ++i) {
        h.Key("Down");
        KITE_EXPECT_FALSE(h.editor.rows()[h.editor.cursor()].header);
    }
    for (int i = 0; i < 400; ++i) {
        h.Key("Up");
        KITE_EXPECT_FALSE(h.editor.rows()[h.editor.cursor()].header);
    }
    h.Key("End");
    KITE_EXPECT_FALSE(h.editor.rows()[h.editor.cursor()].header);
    h.Key("Home");
    KITE_EXPECT_FALSE(h.editor.rows()[h.editor.cursor()].header);
}

KITE_TEST(keyeditor, enter_captures_the_next_chord_and_replaces_the_binding) {
    Harness h;
    KITE_EXPECT(h.Select(Cmd::NewFolder));

    h.Key("Enter");
    KITE_EXPECT(h.editor.capturing());

    h.Key("Ctrl+Alt+Shift+F9");
    KITE_EXPECT_FALSE(h.editor.capturing());
    KITE_EXPECT_EQ(h.keys.Lookup(ParseChord("Ctrl+Alt+Shift+F9")), Cmd::NewFolder);
    // Replace, not add: the default is gone.
    KITE_EXPECT_EQ(h.keys.ChordsFor(Cmd::NewFolder).size(), size_t{ 1 });
    KITE_EXPECT_EQ(h.keys.Lookup(ParseChord("Ctrl+Shift+N")), Cmd::None);
    KITE_EXPECT(h.editor.dirty());
}

KITE_TEST(keyeditor, insert_adds_a_second_chord_instead_of_replacing) {
    Harness h;
    KITE_EXPECT(h.Select(Cmd::NewFolder));

    h.Key("Insert");
    h.Key("Ctrl+Alt+Shift+F9");
    KITE_EXPECT_EQ(h.keys.ChordsFor(Cmd::NewFolder).size(), size_t{ 2 });
    KITE_EXPECT_EQ(h.keys.Lookup(ParseChord("Ctrl+Shift+N")), Cmd::NewFolder);
    KITE_EXPECT_EQ(h.keys.Lookup(ParseChord("Ctrl+Alt+Shift+F9")), Cmd::NewFolder);
}

KITE_TEST(keyeditor, capturing_a_chord_someone_else_owns_says_who_lost_it) {
    Harness h;
    KITE_EXPECT(h.Select(Cmd::NewFolder));
    h.Key("Enter");
    h.Key("Ctrl+T");  // NewTab's

    KITE_EXPECT_EQ(h.keys.Lookup(ParseChord("Ctrl+T")), Cmd::NewFolder);
    for (const Chord& c : h.keys.ChordsFor(Cmd::NewTab)) {
        KITE_EXPECT_NE(c, ParseChord("Ctrl+T"));
    }
    // Silently stealing it would leave the user hunting for a broken shortcut.
    KITE_EXPECT_NE(h.editor.message().find("New tab"), std::string::npos);
}

KITE_TEST(keyeditor, escape_leaves_capture_without_changing_anything) {
    Harness h;
    KITE_EXPECT(h.Select(Cmd::NewFolder));
    h.Key("Enter");
    h.Key("Escape");

    KITE_EXPECT_FALSE(h.editor.capturing());
    KITE_EXPECT(h.editor.visible());
    KITE_EXPECT_FALSE(h.editor.dirty());
    KITE_EXPECT_EQ(h.keys.Lookup(ParseChord("Ctrl+Shift+N")), Cmd::NewFolder);
}

KITE_TEST(keyeditor, chords_the_screen_itself_uses_can_still_be_assigned) {
    // Enter, Delete and F1 all mean something to this screen, which is exactly
    // why capture has to hand them through untouched.
    Harness h;
    for (const char* chord : { "Enter", "Delete", "F1", "Down" }) {
        KITE_EXPECT(h.Select(Cmd::OpenTerminal));
        h.Key("Enter");
        h.Key(chord);
        KITE_EXPECT_EQ(h.keys.Lookup(ParseChord(chord)), Cmd::OpenTerminal);
    }
}

KITE_TEST(keyeditor, delete_clears_every_binding_for_the_selected_command) {
    Harness h;
    KITE_EXPECT(h.Select(Cmd::NextTab));
    h.Key("Delete");

    KITE_EXPECT_EQ(h.keys.ChordsFor(Cmd::NextTab).size(), size_t{ 0 });
    KITE_EXPECT_EQ(h.keys.Lookup(ParseChord("Ctrl+Tab")), Cmd::None);
    KITE_EXPECT(h.RowFor(Cmd::NextTab)->chords.empty());
}

KITE_TEST(keyeditor, ctrl_r_puts_one_command_back_to_its_default) {
    Harness h;
    KITE_EXPECT(h.Select(Cmd::NewTab));
    h.Key("Enter");
    h.Key("Ctrl+Alt+Shift+F9");
    KITE_EXPECT_EQ(h.keys.Lookup(ParseChord("Ctrl+T")), Cmd::None);

    h.Key("Ctrl+R");
    KITE_EXPECT_EQ(h.keys.Lookup(ParseChord("Ctrl+T")), Cmd::NewTab);
    KITE_EXPECT_EQ(h.keys.ChordsFor(Cmd::NewTab).size(), size_t{ 1 });
}

KITE_TEST(keyeditor, ctrl_shift_r_puts_everything_back) {
    Harness h;
    KITE_EXPECT(h.Select(Cmd::NewTab));
    h.Key("Delete");
    KITE_EXPECT(h.Select(Cmd::CloseTab));
    h.Key("Delete");

    h.Key("Ctrl+Shift+R");
    KITE_EXPECT_EQ(h.keys.Lookup(ParseChord("Ctrl+T")), Cmd::NewTab);
    KITE_EXPECT_EQ(h.keys.Lookup(ParseChord("Ctrl+W")), Cmd::CloseTab);
}

KITE_TEST(keyeditor, typing_filters_by_label_name_and_chord) {
    Harness h;
    h.Type("terminal");
    KITE_EXPECT(h.RowFor(Cmd::OpenTerminal) != nullptr);
    KITE_EXPECT(h.RowFor(Cmd::NewTab) == nullptr);

    // The keys.ini name works too, for anyone arriving from the file.
    h.Key("Escape");
    h.Type("shell.reveal");
    KITE_EXPECT(h.RowFor(Cmd::RevealInExplorer) != nullptr);

    // And so does the chord, which is how you find out what a key already does.
    h.Key("Escape");
    h.Type("alt+shift+3");
    KITE_EXPECT(h.RowFor(Cmd::Bookmark3) != nullptr);
    KITE_EXPECT(h.RowFor(Cmd::Bookmark4) == nullptr);
}

KITE_TEST(keyeditor, a_filter_leaves_no_heading_without_rows_under_it) {
    Harness h;
    h.Type("terminal");
    const std::vector<KeyEditor::Row>& rows = h.editor.rows();
    for (size_t i = 0; i < rows.size(); ++i) {
        if (!rows[i].header) continue;
        KITE_EXPECT(i + 1 < rows.size());
        KITE_EXPECT_FALSE(rows[i + 1].header);
    }
}

KITE_TEST(keyeditor, backspace_walks_the_filter_back) {
    Harness h;
    h.Type("terminal");
    const size_t narrowed = h.editor.rows().size();

    h.Key("Backspace");
    KITE_EXPECT_EQ(h.editor.filter(), std::string("termina"));
    KITE_EXPECT(h.editor.rows().size() >= narrowed);
}

KITE_TEST(keyeditor, a_filter_that_matches_nothing_leaves_no_selection_to_edit) {
    Harness h;
    h.Type("zzzznothing");
    KITE_EXPECT_EQ(h.editor.rows().size(), size_t{ 0 });
    KITE_EXPECT_EQ(h.editor.selectedCommand(), Cmd::None);

    // Every editing key must be a no-op rather than a crash or a stray binding.
    h.Key("Enter");
    KITE_EXPECT_FALSE(h.editor.capturing());
    h.Key("Delete");
    h.Key("Ctrl+R");
    h.Key("Down");
    KITE_EXPECT_FALSE(h.editor.dirty());
}

KITE_TEST(keyeditor, the_selection_follows_its_command_through_a_filter_change) {
    Harness h;
    h.Type("terminal");
    KITE_EXPECT_EQ(h.editor.selectedCommand(), Cmd::OpenTerminal);

    h.Key("Backspace");
    KITE_EXPECT_EQ(h.editor.selectedCommand(), Cmd::OpenTerminal);
}

KITE_TEST(keyeditor, escape_clears_the_filter_first_and_closes_second) {
    Harness h;
    h.Type("tab");
    KITE_EXPECT(h.Key("Escape"));
    KITE_EXPECT(h.editor.filter().empty());
    KITE_EXPECT(h.editor.visible());

    KITE_EXPECT(h.Key("Escape"));
    KITE_EXPECT_FALSE(h.editor.visible());
}

KITE_TEST(keyeditor, every_key_is_swallowed_while_the_screen_is_up) {
    // Otherwise a shortcut typed into the settings screen fires the command it
    // is bound to, behind the panel.
    Harness h;
    KITE_EXPECT(h.Key("Ctrl+W"));
    KITE_EXPECT(h.Key("Ctrl+Q"));
    KITE_EXPECT(h.Key("Alt+F4"));

    h.editor.Close();
    KITE_EXPECT_FALSE(h.Key("Ctrl+W"));
}

KITE_TEST(keyeditor, the_selection_stays_on_screen_while_it_moves) {
    Harness h;
    h.editor.SetPageRows(10);
    h.Key("End");
    KITE_EXPECT(h.editor.cursor() >= h.editor.scroll());
    KITE_EXPECT(h.editor.cursor() < h.editor.scroll() + 10);

    h.Key("Home");
    KITE_EXPECT(h.editor.cursor() >= h.editor.scroll());
    KITE_EXPECT(h.editor.cursor() < h.editor.scroll() + 10);

    for (int i = 0; i < 30; ++i) {
        h.Key("Down");
        KITE_EXPECT(h.editor.cursor() >= h.editor.scroll());
        KITE_EXPECT(h.editor.cursor() < h.editor.scroll() + 10);
    }
    h.Key("PageDown");
    KITE_EXPECT(h.editor.cursor() >= h.editor.scroll());
    KITE_EXPECT(h.editor.cursor() < h.editor.scroll() + 10);
}

KITE_TEST(keyeditor, scrolling_never_runs_past_the_ends_of_the_list) {
    Harness h;
    h.editor.SetPageRows(10);
    h.editor.Scroll(-50);
    KITE_EXPECT_EQ(h.editor.scroll(), 0);

    h.editor.Scroll(100000);
    KITE_EXPECT_EQ(h.editor.scroll(), static_cast<int>(h.editor.rows().size()) - 10);
}

KITE_TEST(keyeditor, reopening_starts_from_a_clean_slate) {
    Harness h;
    h.Type("terminal");
    h.Key("Enter");
    h.editor.Close();

    h.editor.Open(h.strings, h.keys);
    KITE_EXPECT(h.editor.filter().empty());
    KITE_EXPECT_FALSE(h.editor.capturing());
    // One heading per group, plus every command.
    KITE_EXPECT_EQ(h.editor.rows().size(),
                   AllCommands().size() + static_cast<size_t>(CmdGroup::Count));
}

KITE_TEST(keyeditor, clicking_a_heading_does_not_move_the_selection) {
    Harness h;
    const Cmd before = h.editor.selectedCommand();
    for (size_t i = 0; i < h.editor.rows().size(); ++i) {
        if (h.editor.rows()[i].header) {
            h.editor.SelectRow(static_cast<int>(i));
            break;
        }
    }
    KITE_EXPECT_EQ(h.editor.selectedCommand(), before);
}
