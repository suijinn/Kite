#include "core/input/KeyEditor.h"

#include <algorithm>

#include "core/base/Utf8.h"

namespace kite {
namespace {

/// Joins every chord bound to one command, in the order they were bound.
std::string ChordList(const KeyMap& keys, Cmd id) {
    std::string out;
    for (const Chord& c : keys.ChordsFor(id)) {
        if (!out.empty()) out += ", ";
        out += FormatChord(c);
    }
    return out;
}

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

    rows_.clear();
    CmdGroup lastGroup = CmdGroup::Count;
    for (const CommandInfo& info : AllCommands()) {
        const std::string label = strings.Label(info.labelKey);
        const std::string chords = ChordList(keys, info.id);
        if (!Matches(needle, label, info.name, chords)) continue;

        // Headers are emitted lazily so a filtered-out group leaves no heading
        // hanging over an empty stretch of list.
        if (info.group != lastGroup) {
            lastGroup = info.group;
            rows_.push_back({ Cmd::None, true, strings.Get(GroupLabelKey(info.group)), {} });
        }
        rows_.push_back({ info.id, false, label, chords });
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
    pageRows_ = std::max(1, rows);
    EnsureCursorVisible();
}

void KeyEditor::SelectRow(int index) {
    if (index < 0 || index >= static_cast<int>(rows_.size())) return;
    if (rows_[index].header) return;
    cursor_ = index;
    capturing_ = false;
    EnsureCursorVisible();
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
            case Key::Insert: BeginCapture(true); return true;
            case Key::Delete:
                if (selected == Cmd::None) return true;
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
    if (codepoint < 0x20 || codepoint == 0x7F) return false;

    utf8::Encode(codepoint, filter_);
    message_.clear();
    Rebuild(strings, keys);
    return true;
}

}  // namespace kite
