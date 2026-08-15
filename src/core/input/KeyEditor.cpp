#include "core/input/KeyEditor.h"

#include <algorithm>

#include "core/base/Utf8.h"

namespace kite {
namespace {

/// Matches the same way the file list filter does: lowercase, substring.
///
/// The haystack is the label, the keys.ini name and the current chords together,
/// so "ctrl+t", "tab.new" and "new tab" all find the same row - the user may be
/// coming from any of the three.
bool Matches(const std::string& needle, const std::string& label, const char* name,
             const std::string& chords) {
    if (needle.empty()) return true;
    if (utf8::ToLowerAscii(label).find(needle) != std::string::npos) return true;
    if (utf8::ToLowerAscii(name).find(needle) != std::string::npos) return true;
    return utf8::ToLowerAscii(chords).find(needle) != std::string::npos;
}

}  // namespace

void KeyEditor::Open(const Strings& strings, const KeyMap& keys) {
    visible_ = true;
    capturing_ = false;
    filter_.clear();
    message_.clear();
    cursor_ = -1;
    scroll_ = 0;
    Rebuild(strings, keys);
}

void KeyEditor::Close() {
    visible_ = false;
    capturing_ = false;
}

void KeyEditor::Rebuild(const Strings& strings, const KeyMap& keys) {
    const Cmd previous = selectedCommand();
    const std::string needle = utf8::ToLowerAscii(filter_);
    // Every row is about to be made again, so an index into one of them means
    // nothing. Whoever still has something to point at says so afterwards.
    chordCursor_ = -1;

    rows_.clear();
    CmdGroup lastGroup = CmdGroup::Count;
    for (const CommandInfo& info : AllCommands()) {
        const std::string label = strings.Label(info.labelKey);
        const std::string chords = keys.ChordText(info.id);
        if (!Matches(needle, label, info.name, chords)) continue;

        // Headers are emitted lazily so a filtered-out group leaves no heading
        // hanging over an empty stretch of list.
        if (info.group != lastGroup) {
            lastGroup = info.group;
            rows_.push_back({ Cmd::None, true, strings.Get(GroupLabelKey(info.group)), {}, {} });
        }
        // The chords are carried apart as well as joined: the joined line is what
        // gets drawn and searched, the pieces are what a single one can be picked
        // out of and removed by.
        std::vector<std::string> pieces;
        for (const Chord& c : keys.ChordsFor(info.id)) pieces.push_back(FormatChord(c));
        rows_.push_back({ info.id, false, label, chords, std::move(pieces) });
    }

    SelectCommand(previous);
    EnsureCursorVisible();
}

Cmd KeyEditor::selectedCommand() const {
    if (cursor_ < 0 || cursor_ >= static_cast<int>(rows_.size())) return Cmd::None;
    return rows_[cursor_].cmd;
}

int KeyEditor::commandCount() const {
    int count = 0;
    for (const Row& row : rows_) {
        if (!row.header) ++count;
    }
    return count;
}

void KeyEditor::SelectCommand(Cmd id) {
    cursor_ = -1;
    if (id != Cmd::None) {
        for (size_t i = 0; i < rows_.size(); ++i) {
            if (rows_[i].cmd == id) {
                cursor_ = static_cast<int>(i);
                return;
            }
        }
    }
    // The command it was on is filtered out (or there was none): fall back to the
    // first row that can actually be selected.
    for (size_t i = 0; i < rows_.size(); ++i) {
        if (!rows_[i].header) {
            cursor_ = static_cast<int>(i);
            return;
        }
    }
}

void KeyEditor::SetPageRows(int rows) {
    const int wanted = std::max(1, rows);
    if (wanted != pageRows_) {
        // The window changed size: fewer rows fit than before, and the selection
        // is the one thing that has to stay on screen.
        pageRows_ = wanted;
        EnsureCursorVisible();
        return;
    }
    // This arrives every frame, so it must not pull the view back to the cursor:
    // a wheel scroll away from the selection would be undone before it was ever
    // drawn. Only the range still needs holding - the row count moves with the
    // filter.
    scroll_ = std::clamp(scroll_, 0, std::max(0, static_cast<int>(rows_.size()) - pageRows_));
}

void KeyEditor::SelectRow(int index) {
    if (index < 0 || index >= static_cast<int>(rows_.size())) return;
    if (rows_[index].header) return;
    cursor_ = index;
    chordCursor_ = -1;
    capturing_ = false;
    EnsureCursorVisible();
}

const std::vector<std::string>& KeyEditor::SelectedChords() const {
    static const std::vector<std::string> kNone;
    if (cursor_ < 0 || cursor_ >= static_cast<int>(rows_.size())) return kNone;
    return rows_[cursor_].chordTexts;
}

void KeyEditor::SelectChord(int index, const Strings& strings) {
    const std::vector<std::string>& chords = SelectedChords();
    if (index < 0 || index >= static_cast<int>(chords.size())) {
        chordCursor_ = -1;
        return;
    }
    chordCursor_ = index;
    // Which key Delete is now aimed at, and that it is aimed at one of them
    // rather than the row: with several chords on a line, "Delete: clear" in the
    // footer is not an answer to "clear which".
    message_ = strings.Format("ui.key_settings_chord_picked", { chords[index] });
}

void KeyEditor::MoveChordCursor(int delta, const Strings& strings) {
    const int count = static_cast<int>(SelectedChords().size());
    if (count == 0) return;
    // Entering from nowhere lands on the end being walked towards; after that it
    // stops at the ends. This is a position inside one row, not a way through the
    // list, so wrapping would only lose the "that is all of them" the stop gives.
    const int next = (chordCursor_ < 0) ? (delta > 0 ? 0 : count - 1) : chordCursor_ + delta;
    SelectChord(std::clamp(next, 0, count - 1), strings);
}

void KeyEditor::RemoveChord(int index, KeyMap& keys, const Strings& strings) {
    const std::vector<std::string>& chords = SelectedChords();
    if (index < 0 || index >= static_cast<int>(chords.size())) return;

    const Chord chord = ParseChord(chords[index]);
    if (!chord.valid()) return;
    keys.Unbind(chord);
    dirty_ = true;
    message_ = strings.Format("ui.key_settings_chord_removed", { FormatChord(chord) });
    Rebuild(strings, keys);

    // Stay on the same spot in the row so a second key can be dropped with a
    // second Delete; only the last one taken away leaves nothing to point at.
    const int left = static_cast<int>(SelectedChords().size());
    chordCursor_ = (left == 0) ? -1 : std::min(index, left - 1);
}

void KeyEditor::Scroll(int deltaRows) {
    const int maxScroll = std::max(0, static_cast<int>(rows_.size()) - pageRows_);
    scroll_ = std::clamp(scroll_ + deltaRows, 0, maxScroll);
}

void KeyEditor::MoveCursor(int delta, bool absolute) {
    if (rows_.empty()) return;
    const int last = static_cast<int>(rows_.size()) - 1;
    int target = std::clamp(absolute ? delta : cursor_ + delta, 0, last);

    // Headers are labels, not rows the user can act on; keep going in the
    // direction of travel until a real one turns up.
    const int step = (absolute ? (target <= cursor_ ? 1 : -1) : (delta >= 0 ? 1 : -1));
    while (target >= 0 && target <= last && rows_[target].header) target += step;
    if (target < 0 || target > last) {
        // Ran off the end past a trailing header: search back the other way so
        // the selection never lands on nothing.
        target = std::clamp(absolute ? delta : cursor_ + delta, 0, last);
        while (target >= 0 && target <= last && rows_[target].header) target -= step;
    }
    if (target < 0 || target > last) return;

    cursor_ = target;
    // A chord is picked out of one row; moving off that row leaves nothing for
    // the pick to mean, and Delete goes back to answering for the whole command.
    chordCursor_ = -1;
    EnsureCursorVisible();
}

void KeyEditor::EnsureCursorVisible() {
    const int count = static_cast<int>(rows_.size());
    const int maxScroll = std::max(0, count - pageRows_);
    if (cursor_ >= 0) {
        // One line of context above the selection, so the group heading stays
        // readable while stepping down through a group.
        if (cursor_ - 1 < scroll_) scroll_ = cursor_ - 1;
        if (cursor_ >= scroll_ + pageRows_) scroll_ = cursor_ - pageRows_ + 1;
    }
    scroll_ = std::clamp(scroll_, 0, maxScroll);
}

void KeyEditor::BeginCapture(bool add) {
    if (selectedCommand() == Cmd::None) return;
    capturing_ = true;
    captureAdds_ = add;
    message_.clear();
}

void KeyEditor::CancelCapture() { capturing_ = false; }

bool KeyEditor::HandleKey(const Chord& chord, KeyMap& keys, const Strings& strings) {
    if (!visible_) return false;

    // The mark only ever covers the character message of the keystroke that set
    // it - which arrives before any further key can, since Windows queues the
    // WM_CHAR of a key down with it.
    swallowNextChar_ = false;

    if (capturing_) {
        // Escape is the way out of capture, so it cannot itself be assigned.
        // Every other chord is fair game - including F1 and Enter, which is the
        // point of having this screen at all.
        if (chord.key == Key::Escape && chord.mods == kModNone) {
            capturing_ = false;
            message_ = strings.Get("ui.key_settings_cancelled");
            return true;
        }
        const Cmd target = selectedCommand();
        if (target == Cmd::None) {
            capturing_ = false;
            return true;
        }

        const Cmd previous = keys.Lookup(chord);
        if (!captureAdds_) keys.UnbindCommand(target);
        keys.Bind(chord, target);
        capturing_ = false;
        dirty_ = true;
        // TranslateMessage has already turned this key down into a character
        // that is on its way here, and capture is over by the time it lands:
        // assigning "D" to a command would leave a "d" typed into the search
        // box. The chord was the answer to a question; its character is not a
        // second answer to a different one.
        swallowNextChar_ = true;

        if (previous != Cmd::None && previous != target) {
            // Bind() silently takes the chord from whoever had it; saying so is
            // the difference between "it works" and "something else broke".
            const CommandInfo* info = FindCommand(previous);
            message_ = strings.Format("ui.key_settings_taken",
                                      { FormatChord(chord),
                                        info ? strings.Label(info->labelKey) : std::string() });
        } else {
            message_ = strings.Format("ui.key_settings_assigned", { FormatChord(chord) });
        }
        Rebuild(strings, keys);
        return true;
    }

    const Cmd selected = selectedCommand();
    const CommandInfo* info = FindCommand(selected);
    const std::string label = info ? strings.Label(info->labelKey) : std::string();

    if (chord.mods == kModCtrl && chord.key == Key::R) {
        if (selected == Cmd::None) return true;
        keys.UnbindCommand(selected);
        for (const Chord& c : KeyMap::DefaultChordsFor(selected)) keys.Bind(c, selected);
        dirty_ = true;
        message_ = strings.Format("ui.key_settings_reset", { label });
        Rebuild(strings, keys);
        return true;
    }
    // Add rather than replace. It sits on Enter with Ctrl on it because Enter is
    // the replace, and the pair then reads the way Ctrl+Enter reads in the list -
    // the same action, but making one more of the thing instead of moving. Insert
    // was here on its own and is kept as a second way in, but it is off in the
    // corner of a keyboard that may not have it at all, so the footer names this
    // one.
    if (chord.mods == kModCtrl && chord.key == Key::Enter) {
        BeginCapture(true);
        return true;
    }
    if (chord.mods == (kModCtrl | kModShift) && chord.key == Key::R) {
        keys.LoadDefaults();
        dirty_ = true;
        message_ = strings.Get("ui.key_settings_reset_all");
        Rebuild(strings, keys);
        return true;
    }

    if (chord.mods == kModNone) {
        switch (chord.key) {
            case Key::Escape:
                // The filter is the more recent state, so it goes first; a second
                // Escape then closes the screen.
                if (!filter_.empty()) {
                    filter_.clear();
                    message_.clear();
                    Rebuild(strings, keys);
                } else {
                    Close();
                }
                return true;
            case Key::Up: MoveCursor(-1); return true;
            case Key::Down: MoveCursor(1); return true;
            case Key::PageUp: MoveCursor(-pageRows_); return true;
            case Key::PageDown: MoveCursor(pageRows_); return true;
            case Key::Home: MoveCursor(0, true); return true;
            case Key::End: MoveCursor(static_cast<int>(rows_.size()) - 1, true); return true;
            case Key::Enter: BeginCapture(false); return true;
            case Key::Insert: BeginCapture(true); return true;  // 上記 Ctrl+Enter の別名
            // Along the row rather than down the list: with two or three chords
            // on a line, these are how one of them is picked out.
            case Key::Left: MoveChordCursor(-1, strings); return true;
            case Key::Right: MoveChordCursor(1, strings); return true;
            case Key::Delete:
                if (selected == Cmd::None) return true;
                // Aimed at the one chord that was picked, or at the command as a
                // whole when none was. Clearing everything when the user had just
                // pointed at one of three would be the wrong half of the answer.
                if (chordCursor_ >= 0) {
                    RemoveChord(chordCursor_, keys, strings);
                    return true;
                }
                keys.UnbindCommand(selected);
                dirty_ = true;
                message_ = strings.Format("ui.key_settings_cleared", { label });
                Rebuild(strings, keys);
                return true;
            case Key::Backspace:
                if (!filter_.empty()) {
                    filter_.erase(utf8::PrevBoundary(filter_, filter_.size()));
                    Rebuild(strings, keys);
                }
                return true;
            default:
                break;
        }
    }

    // Anything else is swallowed: while this screen is up, a stray Ctrl+W must
    // not close a tab behind it.
    return true;
}

bool KeyEditor::HandleChar(uint32_t codepoint, const Strings& strings, const KeyMap& keys) {
    if (!visible_ || capturing_) return false;
    if (swallowNextChar_) {
        // The tail of the keystroke that was just assigned. Consumed rather than
        // passed on: nothing behind this screen may have it either.
        swallowNextChar_ = false;
        return true;
    }
    if (codepoint < 0x20 || codepoint == 0x7F) return false;

    utf8::Encode(codepoint, filter_);
    message_.clear();
    Rebuild(strings, keys);
    return true;
}

}  // namespace kite
