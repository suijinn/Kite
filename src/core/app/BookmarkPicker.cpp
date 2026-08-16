#include "core/app/BookmarkPicker.h"

#include <algorithm>

#include "core/base/Utf8.h"
#include "core/input/Commands.h"

namespace kite {
namespace {

// The eight numbered commands, spelled out rather than derived from Bookmark1
// plus an offset. Arithmetic on Cmd walks off the end of the table the moment a
// ninth bookmark exists - which is the very case this screen was added for.
constexpr Cmd kNumbered[] = {
    Cmd::Bookmark1, Cmd::Bookmark2, Cmd::Bookmark3, Cmd::Bookmark4,
    Cmd::Bookmark5, Cmd::Bookmark6, Cmd::Bookmark7, Cmd::Bookmark8,
};
constexpr int kNumberedCount = static_cast<int>(sizeof(kNumbered) / sizeof(kNumbered[0]));

bool Matches(const std::string& needle, const BookmarkPicker::Row& row) {
    if (needle.empty()) return true;
    if (utf8::ToLowerAscii(row.name).find(needle) != std::string::npos) return true;
    // The path too: bookmarks are often named for the project and looked for by
    // where they are, or the other way round.
    return utf8::ToLowerAscii(row.path).find(needle) != std::string::npos;
}

}  // namespace

void BookmarkPicker::Open(const std::vector<Bookmark>& marks, const KeyMap& keys,
                          const std::string& currentPath) {
    visible_ = true;
    filter_.clear();
    scroll_ = 0;

    all_.clear();
    for (size_t i = 0; i < marks.size(); ++i) {
        const int index = static_cast<int>(i);
        // What the numbered shortcut for this row is, on the rows that have one.
        // Showing it is the same call the F1 sheet makes: a screen that knows the
        // answer and does not say it leaves the shortcut undiscoverable.
        const std::string chords =
            (index < kNumberedCount) ? keys.ChordText(kNumbered[index]) : std::string();
        all_.push_back({ index, marks[i].name, marks[i].path, chords });
    }

    // Start on the folder already being looked at, when it is one of these. The
    // alternative - always the top - answers "which of these am I in" with
    // silence, and that is the one thing the screen knows for free.
    selected_ = -1;
    for (const Row& row : all_) {
        if (utf8::EqualsIgnoreCaseAscii(row.path, currentPath)) {
            selected_ = row.index;
            break;
        }
    }

    Rebuild();
}

void BookmarkPicker::Close() {
    visible_ = false;
    filter_.clear();
    all_.clear();
    rows_.clear();
    selected_ = -1;
    cursor_ = -1;
    scroll_ = 0;
}

void BookmarkPicker::Rebuild() {
    const std::string needle = utf8::ToLowerAscii(filter_);

    rows_.clear();
    for (const Row& row : all_) {
        if (Matches(needle, row)) rows_.push_back(row);
    }

    // The selection is held as a bookmark, not as a row number: typing one more
    // letter renumbers every row, and a cursor that stayed on row 3 would drift
    // onto a different bookmark each keystroke.
    cursor_ = -1;
    if (selected_ >= 0) {
        for (size_t i = 0; i < rows_.size(); ++i) {
            if (rows_[i].index == selected_) {
                cursor_ = static_cast<int>(i);
                break;
            }
        }
    }
    if (cursor_ < 0 && !rows_.empty()) {
        // Filtered away (or nothing was selected): fall to the top, which is what
        // the next Enter should take. Leaving no cursor would mean Enter does
        // nothing on a list that plainly has rows in it.
        cursor_ = 0;
        selected_ = rows_[0].index;
    }
    if (rows_.empty()) selected_ = -1;

    EnsureCursorVisible();
}

int BookmarkPicker::selectedIndex() const {
    if (cursor_ < 0 || cursor_ >= static_cast<int>(rows_.size())) return -1;
    return rows_[cursor_].index;
}

void BookmarkPicker::EnsureCursorVisible() {
    const int maxScroll = std::max(0, static_cast<int>(rows_.size()) - pageRows_);
    if (cursor_ >= 0) {
        if (cursor_ < scroll_) scroll_ = cursor_;
        if (cursor_ >= scroll_ + pageRows_) scroll_ = cursor_ - pageRows_ + 1;
    }
    scroll_ = std::clamp(scroll_, 0, maxScroll);
}

void BookmarkPicker::SetPageRows(int rows) {
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

void BookmarkPicker::SelectRow(int index) {
    if (index < 0 || index >= static_cast<int>(rows_.size())) return;
    cursor_ = index;
    selected_ = rows_[index].index;
    EnsureCursorVisible();
}

void BookmarkPicker::Scroll(int deltaRows) {
    const int maxScroll = std::max(0, static_cast<int>(rows_.size()) - pageRows_);
    scroll_ = std::clamp(scroll_ + deltaRows, 0, maxScroll);
}

void BookmarkPicker::MoveCursor(int delta, bool absolute) {
    if (rows_.empty()) return;
    const int last = static_cast<int>(rows_.size()) - 1;
    SelectRow(std::clamp(absolute ? delta : cursor_ + delta, 0, last));
}

BookmarkPicker::Action BookmarkPicker::HandleKey(const Chord& chord) {
    if (!visible_) return Action::None;

    // A new tab instead of this one, read the same way the listing's Ctrl+Enter
    // is read. Nothing else on this screen takes a modifier, so anything else
    // carrying one is simply swallowed below.
    if (chord.mods == kModCtrl && chord.key == Key::Enter) {
        return (cursor_ >= 0) ? Action::OpenNewTab : Action::None;
    }

    if (chord.mods == kModNone) {
        switch (chord.key) {
            case Key::Escape:
                // The filter is the more recent state, so it goes first: a screen
                // full of a mistyped filter can be cleared without losing the
                // screen, and a second Escape then leaves.
                if (!filter_.empty()) {
                    filter_.clear();
                    Rebuild();
                    return Action::None;
                }
                return Action::Close;
            case Key::Up: MoveCursor(-1); return Action::None;
            case Key::Down: MoveCursor(1); return Action::None;
            case Key::PageUp: MoveCursor(-pageRows_); return Action::None;
            case Key::PageDown: MoveCursor(pageRows_); return Action::None;
            case Key::Home: MoveCursor(0, true); return Action::None;
            case Key::End: MoveCursor(static_cast<int>(rows_.size()) - 1, true); return Action::None;
            case Key::Enter: return (cursor_ >= 0) ? Action::Open : Action::None;
            case Key::Backspace:
                if (!filter_.empty()) {
                    filter_.erase(utf8::PrevBoundary(filter_, filter_.size()));
                    Rebuild();
                }
                return Action::None;
            default:
                break;
        }
    }

    // Everything else is swallowed. A stray shortcut firing behind an open
    // chooser would act on the folder the user is in the middle of leaving.
    return Action::None;
}

bool BookmarkPicker::HandleChar(uint32_t codepoint) {
    if (!visible_) return false;
    if (codepoint < 0x20 || codepoint == 0x7F) return false;

    utf8::Encode(codepoint, filter_);
    Rebuild();
    return true;
}

}  // namespace kite
