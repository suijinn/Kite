#include "core/app/PickerList.h"

#include <algorithm>

#include "core/base/Utf8.h"

namespace kite {
namespace {

// Whole tail, part of the tail, matched elsewhere. The ranks themselves never
// leave Rebuild() - what the header promises is the resulting order, not this.
constexpr int kRankCount = 3;

bool Matches(const std::string& needle, const PickerList::Entry& entry) {
    if (needle.empty()) return true;
    for (const std::string& field : entry.fields) {
        if (utf8::ContainsLowerAscii(field, needle)) return true;
    }
    return false;
}

// How near the row's own end the hit landed. Small is high.
//
// A substring test over the whole path answers "does this word appear anywhere
// under here", and for a filter that is too generous to sort by: typing "kite"
// aims at the folder called Kite, not at the dozen rows that merely live below
// it and spell it in the middle of their path. The tail is what the row is
// called, so a hit there is a hit on the thing itself.
//
// Rows without a tail (the command palette has none) all land in the last rank,
// which leaves that screen's order exactly as it was handed over.
int TailRank(const std::string& needle, const PickerList::Entry& entry) {
    if (needle.empty() || entry.tail.empty()) return kRankCount - 1;
    // Whole tail first: with Kite and KiteOld both on screen, the one that *is*
    // what was typed has to win, or the ranking has not answered anything.
    if (utf8::EqualsIgnoreCaseAscii(entry.tail, needle)) return 0;
    if (utf8::ContainsLowerAscii(entry.tail, needle)) return 1;
    return kRankCount - 1;
}

}  // namespace

void PickerList::SetPrefix(std::string prefix) { prefix_ = std::move(prefix); }

void PickerList::Reset(std::vector<Entry> entries, int selectedId) {
    all_ = std::move(entries);
    // The marker is part of the text from the start, so the field says which mode
    // it is in without a label of its own - and deleting it is what leaves.
    filter_.Clear();
    filter_.Insert(prefix_);
    scroll_ = 0;
    selected_ = selectedId;
    Rebuild();
}

void PickerList::Clear() {
    all_.clear();
    shown_.clear();
    filter_.Clear();
    query_.clear();
    selected_ = -1;
    cursor_ = -1;
    scroll_ = 0;
}

void PickerList::Rebuild() {
    // Only what follows the mode marker is matched. Mid-edit the marker can be
    // gone for one frame - the caller swaps screens on the next call - so this
    // has to read the text as it is rather than assume the marker is there.
    query_ = filter_.text;
    if (!prefix_.empty() && query_.compare(0, prefix_.size(), prefix_) == 0) {
        query_.erase(0, prefix_.size());
    }
    const std::string needle = utf8::ToLowerAscii(query_);

    // Collected rank by rank rather than sorted: the order inside a rank has to
    // stay the order the caller handed over - bookmarks, then tabs, then quick
    // access, then drives, then history - and a comparison that has to keep
    // saying so is one more place for it to stop being true.
    std::vector<int> ranked[kRankCount];
    for (const Entry& entry : all_) {
        if (!Matches(needle, entry)) continue;
        ranked[TailRank(needle, entry)].push_back(entry.id);
    }
    shown_.clear();
    for (const std::vector<int>& rank : ranked) {
        shown_.insert(shown_.end(), rank.begin(), rank.end());
    }

    // The selection is held as an id, not as a row number: typing one more letter
    // renumbers every row, and a cursor that stayed on row 3 would drift onto a
    // different thing each keystroke.
    cursor_ = -1;
    if (selected_ >= 0) {
        for (size_t i = 0; i < shown_.size(); ++i) {
            if (shown_[i] == selected_) {
                cursor_ = static_cast<int>(i);
                break;
            }
        }
    }
    if (cursor_ < 0 && !shown_.empty()) {
        // Filtered away (or nothing was selected): fall to the top, which is what
        // the next Enter should take. Leaving no cursor would mean Enter does
        // nothing on a list that plainly has rows in it.
        cursor_ = 0;
        selected_ = shown_[0];
    }
    if (shown_.empty()) selected_ = -1;

    EnsureCursorVisible();
}

int PickerList::selectedId() const {
    if (cursor_ < 0 || cursor_ >= static_cast<int>(shown_.size())) return -1;
    return shown_[cursor_];
}

void PickerList::EnsureCursorVisible() {
    const int maxScroll = std::max(0, static_cast<int>(shown_.size()) - pageRows_);
    if (cursor_ >= 0) {
        if (cursor_ < scroll_) scroll_ = cursor_;
        if (cursor_ >= scroll_ + pageRows_) scroll_ = cursor_ - pageRows_ + 1;
    }
    scroll_ = std::clamp(scroll_, 0, maxScroll);
}

void PickerList::SetPageRows(int rows) {
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
    scroll_ = std::clamp(scroll_, 0, std::max(0, static_cast<int>(shown_.size()) - pageRows_));
}

void PickerList::SelectRow(int index) {
    if (index < 0 || index >= static_cast<int>(shown_.size())) return;
    cursor_ = index;
    selected_ = shown_[index];
    EnsureCursorVisible();
}

void PickerList::Scroll(int deltaRows) {
    const int maxScroll = std::max(0, static_cast<int>(shown_.size()) - pageRows_);
    scroll_ = std::clamp(scroll_ + deltaRows, 0, maxScroll);
}

void PickerList::MoveCursor(int delta, bool absolute) {
    if (shown_.empty()) return;
    const int last = static_cast<int>(shown_.size()) - 1;
    SelectRow(std::clamp(absolute ? delta : cursor_ + delta, 0, last));
}

PickerList::Action PickerList::HandleKey(const Chord& chord) {
    // What the modifier means is the caller's business; all that is decided here
    // is that it is a second way of taking the row. Nothing else on these screens
    // takes a modifier, so anything else carrying one is swallowed below.
    if (chord.mods == kModCtrl && chord.key == Key::Enter) {
        return (cursor_ >= 0) ? Action::AcceptAlt : Action::None;
    }

    if (chord.mods == kModNone) {
        switch (chord.key) {
            case Key::Escape:
                // The filter is the more recent state, so it goes first: a screen
                // full of a mistyped filter can be cleared without losing the
                // screen, and a second Escape then leaves. It clears back to the
                // mode marker, not past it - taking the marker too would swap the
                // screen out from under an Escape that meant "clear what I typed".
                if (filter_.text.size() > prefix_.size()) {
                    filter_.Clear();
                    filter_.Insert(prefix_);
                    Rebuild();
                    return Action::None;
                }
                return Action::Close;
            case Key::Up: MoveCursor(-1); return Action::None;
            case Key::Down: MoveCursor(1); return Action::None;
            case Key::PageUp: MoveCursor(-pageRows_); return Action::None;
            case Key::PageDown: MoveCursor(pageRows_); return Action::None;
            case Key::Home:
            case Key::End:
                // Only while there is nothing typed. An empty field has nowhere
                // to put the caret, so the pair reads as "first row" / "last row"
                // the way it does on the listing behind; once a filter is being
                // typed the field is what they belong to, which is what every
                // other text field on the desktop does.
                if (filter_.text.size() <= prefix_.size()) {
                    MoveCursor(chord.key == Key::Home ? 0 : static_cast<int>(shown_.size()) - 1,
                               true);
                    return Action::None;
                }
                break;
            case Key::Enter: return (cursor_ >= 0) ? Action::Accept : Action::None;
            default:
                break;
        }
    }

    // Everything left is the filter field's: the caret, the selection, and the
    // one-character deletes. The counting lives in TextField so that this field
    // and the prompt cannot drift apart.
    if (filter_.HandleKey(chord) == TextField::Edit::Changed) Rebuild();

    // Anything the field did not want is swallowed too. A stray shortcut firing
    // behind an open chooser would act on whatever the user is in the middle of
    // leaving.
    return Action::None;
}

bool PickerList::HandleChar(uint32_t codepoint) {
    if (codepoint < 0x20 || codepoint == 0x7F) return false;

    filter_.Insert(utf8::Encode(codepoint));
    Rebuild();
    return true;
}

}  // namespace kite
