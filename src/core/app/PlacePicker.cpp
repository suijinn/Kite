#include "core/app/PlacePicker.h"

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

// And the eight that reach a tab. Ctrl+<digit> aims at the focused pane, so these
// only belong on a row whose tab is in that pane - a chord printed next to a tab
// it does not reach is worse than no chord at all.
constexpr Cmd kNumberedTabs[] = {
    Cmd::Tab1, Cmd::Tab2, Cmd::Tab3, Cmd::Tab4,
    Cmd::Tab5, Cmd::Tab6, Cmd::Tab7, Cmd::Tab8,
};
constexpr int kNumberedTabCount =
    static_cast<int>(sizeof(kNumberedTabs) / sizeof(kNumberedTabs[0]));

}  // namespace

void PlacePicker::Open(const Strings& str, const KeyMap& keys,
                          const std::vector<Bookmark>& marks, const std::vector<OpenTab>& tabs,
                          const std::vector<fs::Root>& drives, const std::string& currentPath) {
    visible_ = true;

    all_.clear();

    // Bookmarks first. Tabs are an addition to this screen, not a replacement for
    // what it was - the hand that opens it to find a bookmark should still find
    // one at the top.
    const std::string bookmarkKind = str.Get("ui.goto_kind_bookmark");
    for (size_t i = 0; i < marks.size(); ++i) {
        const int index = static_cast<int>(i);
        Row row;
        row.kind = Kind::Bookmark;
        row.index = index;
        row.name = marks[i].name;
        row.path = marks[i].path;
        row.kindLabel = bookmarkKind;
        // What the numbered shortcut for this row is, on the rows that have one.
        // Showing it is the same call the F1 sheet makes: a screen that knows the
        // answer and does not say it leaves the shortcut undiscoverable.
        if (index < kNumberedCount) row.chords = keys.ChordText(kNumbered[index]);
        all_.push_back(std::move(row));
    }

    const std::string tabKind = str.Get("ui.goto_kind_tab");
    for (const OpenTab& open : tabs) {
        Row row;
        row.kind = Kind::Tab;
        row.pane = open.pane;
        row.tab = open.tab;
        row.name = open.name;
        row.path = open.path;
        row.kindLabel = tabKind;
        if (open.focused && open.tab < kNumberedTabCount) {
            row.chords = keys.ChordText(kNumberedTabs[open.tab]);
        }
        all_.push_back(std::move(row));
    }

    // Drives last. They are the one part of this list that is always there and
    // always the same, so they are the least likely thing anyone opened the
    // screen to find - and putting them above the tabs would push what is
    // actually open below the fold on a machine with a row of network drives.
    // Reaching them without the mouse used to mean the sidebar or typing the
    // letter into the address bar; neither is a keyboard route to "which disks
    // are there".
    const std::string driveKind = str.Get("ui.goto_kind_drive");
    for (const fs::Root& drive : drives) {
        Row row;
        row.kind = Kind::Drive;
        row.name = drive.label.empty() ? drive.path : drive.label;
        row.path = drive.path;
        row.kindLabel = driveKind;
        all_.push_back(std::move(row));
    }

    // Start on the folder already being looked at, when it is one of these. The
    // alternative - always the top - answers "which of these am I in" with
    // silence, and that is the one thing the screen knows for free. Rows that
    // point at a path are the candidates: the tab being looked at is not on the
    // list at all (the caller drops it - "go here" is not an answer to
    // "go where"), and another pane's tab showing the same folder is a different
    // place to go, not this one.
    int selected = -1;
    for (size_t i = 0; i < all_.size(); ++i) {
        if (all_[i].kind == Kind::Tab) continue;
        if (utf8::EqualsIgnoreCaseAscii(all_[i].path, currentPath)) {
            selected = static_cast<int>(i);
            break;
        }
    }

    // The id is the row's own position, not the bookmark index: the rows come from
    // two places now, and the only thing they have in common is where they sit in
    // this list. It only has to survive filtering, which does not reorder.
    //
    // Matched on the name, the path and the kind: bookmarks are often named for
    // the project and looked for by where they are (or the other way round), and
    // the kind lets "tab" bring up everything that is open.
    std::vector<PickerList::Entry> entries;
    entries.reserve(all_.size());
    for (size_t i = 0; i < all_.size(); ++i) {
        entries.push_back({ static_cast<int>(i),
                            { all_[i].name, all_[i].path, all_[i].kindLabel } });
    }
    list_.Reset(std::move(entries), selected);
    Sync();
}

void PlacePicker::Close() {
    visible_ = false;
    list_.Clear();
    all_.clear();
    rows_.clear();
}

// The visible rows, rebuilt from the ids the list kept. Only the filter moves
// them, so this runs per keystroke rather than per frame.
void PlacePicker::Sync() {
    rows_.clear();
    rows_.reserve(list_.shown().size());
    for (int id : list_.shown()) {
        if (id >= 0 && id < static_cast<int>(all_.size())) {
            rows_.push_back(all_[static_cast<size_t>(id)]);
        }
    }
}

int PlacePicker::total() const { return list_.total(); }

int PlacePicker::cursor() const { return list_.cursor(); }

int PlacePicker::scroll() const { return list_.scroll(); }

const std::string& PlacePicker::filter() const { return list_.filter(); }

void PlacePicker::FilterEdited() {
    list_.FilterEdited();
    Sync();
}

int PlacePicker::selectedIndex() const {
    const Row* row = selectedRow();
    return (row && row->kind == Kind::Bookmark) ? row->index : -1;
}

const PlacePicker::Row* PlacePicker::selectedRow() const {
    const int id = list_.selectedId();
    if (id < 0 || id >= static_cast<int>(all_.size())) return nullptr;
    return &all_[static_cast<size_t>(id)];
}

void PlacePicker::SetPageRows(int rows) { list_.SetPageRows(rows); }

void PlacePicker::SelectRow(int index) { list_.SelectRow(index); }

void PlacePicker::Scroll(int deltaRows) { list_.Scroll(deltaRows); }

void PlacePicker::MoveCursor(int delta, bool absolute) { list_.MoveCursor(delta, absolute); }

PlacePicker::Action PlacePicker::HandleKey(const Chord& chord) {
    if (!visible_) return Action::None;

    const PickerList::Action action = list_.HandleKey(chord);
    Sync();
    switch (action) {
        case PickerList::Action::Accept: return Action::Open;
        case PickerList::Action::AcceptAlt: {
            // Ctrl+Enter is a new tab instead of this one, read the same way the
            // listing's Ctrl+Enter is read - but a row that *is* an open tab has
            // no such reading, so the chord is swallowed rather than given a
            // meaning it does not have.
            const Row* row = selectedRow();
            return (row && row->kind != Kind::Tab) ? Action::OpenNewTab : Action::None;
        }
        case PickerList::Action::Close: return Action::Close;
        case PickerList::Action::None: break;
    }
    return Action::None;
}

bool PlacePicker::HandleChar(uint32_t codepoint) {
    if (!visible_) return false;
    if (!list_.HandleChar(codepoint)) return false;
    Sync();
    return true;
}

}  // namespace kite
