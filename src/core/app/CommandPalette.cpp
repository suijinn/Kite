#include "core/app/CommandPalette.h"

namespace kite {
namespace {

// The eight numbered bookmark commands, spelled out rather than derived from
// Bookmark1 plus an offset - arithmetic on Cmd walks off the end of the table.
// The same table PlacePicker keeps for the same reason.
constexpr Cmd kNumberedBookmarks[] = {
    Cmd::Bookmark1, Cmd::Bookmark2, Cmd::Bookmark3, Cmd::Bookmark4,
    Cmd::Bookmark5, Cmd::Bookmark6, Cmd::Bookmark7, Cmd::Bookmark8,
};

// Which bookmark a command goes to, or -1 for every other command.
int BookmarkSlot(Cmd cmd) {
    for (size_t i = 0; i < sizeof(kNumberedBookmarks) / sizeof(kNumberedBookmarks[0]); ++i) {
        if (kNumberedBookmarks[i] == cmd) return static_cast<int>(i);
    }
    return -1;
}

}  // namespace

void CommandPalette::Open(const Strings& str, const KeyMap& keys,
                          const std::vector<Bookmark>& marks) {
    visible_ = true;

    all_.clear();
    std::vector<PickerList::Entry> entries;
    for (const CommandInfo& info : AllCommands()) {
        // Not its own row. Running it would close the palette and open it again
        // with the filter cleared, which is Escape spelled the long way - a row
        // that looks like an answer and is a dead end.
        if (info.id == Cmd::ShowCommandPalette) continue;

        Row row;
        row.cmd = info.id;
        // Label(), not Get(): the 24 numbered commands share three table rows
        // between them (`cmd.goto_session_n`), and Get() would put that key on
        // the row verbatim.
        row.label = str.Label(info.labelKey);
        // "Go to bookmark 3" does not say which folder that is, which makes those
        // eight rows unreadable on their own - they name a position in a list the
        // screen is not showing. The destination goes into the label rather than a
        // column of its own, so it is also what the filter matches: up to the
        // eighth, a bookmark can be found here by name. Past that it is the
        // bookmark list's job, and this screen does not grow rows for data.
        const int slot = BookmarkSlot(info.id);
        if (slot >= 0 && slot < static_cast<int>(marks.size())) {
            const Bookmark& mark = marks[static_cast<size_t>(slot)];
            // Bookmarks are named when they are made, but a hand-edited
            // bookmarks.ini can leave that blank - then the path is the only
            // thing there is to say.
            const std::string& where = mark.name.empty() ? mark.path : mark.name;
            row.label = str.Format("ui.command_palette_target", { row.label, where });
        }
        // The keys.ini spelling. The filter matches it, so the screen has to show
        // it - a row that answers to something it never displays teaches nobody
        // that the name can be typed here at all.
        row.name = info.name;
        row.group = str.Get(GroupLabelKey(info.group));
        // Every chord the command has, the way the F1 sheet lists them: a screen
        // that knows the binding and stays quiet about it teaches nothing, and
        // this screen exists for the people who do not know the bindings yet.
        row.chords = keys.ChordText(info.id);
        all_.push_back(row);

        // The ini name too (`tab.new`). Someone who has edited keys.ini looks for
        // what they read there, and it is the one spelling that does not move with
        // the display language.
        entries.push_back({ static_cast<int>(info.id), { row.label, row.group, row.name } });
    }

    // Nothing is preselected: the palette is opened to type, and the top row is
    // where the first Enter should land. The field starts holding the marker, so
    // that "the palette is up" and "the text starts with >" are the same fact -
    // which is what lets Backspace walk back to the places list.
    list_.SetPrefix(std::string(kPrefix));
    list_.Reset(std::move(entries), -1);
    Sync();
}

void CommandPalette::Close() {
    visible_ = false;
    list_.Clear();
    all_.clear();
    rows_.clear();
}

// The visible rows, rebuilt from the ids the list kept. PickerList hands them back
// in the order they went in, so walking both at once finds each row without a
// lookup table. Only the filter moves them, so this runs per keystroke rather than
// per frame.
void CommandPalette::Sync() {
    rows_.clear();
    rows_.reserve(list_.shown().size());
    size_t at = 0;
    for (int id : list_.shown()) {
        while (at < all_.size() && static_cast<int>(all_[at].cmd) != id) ++at;
        if (at >= all_.size()) break;
        rows_.push_back(all_[at++]);
    }
}

int CommandPalette::total() const { return list_.total(); }

int CommandPalette::cursor() const { return list_.cursor(); }

int CommandPalette::scroll() const { return list_.scroll(); }

const std::string& CommandPalette::filter() const { return list_.filter(); }

void CommandPalette::FilterEdited() {
    list_.FilterEdited();
    Sync();
}

Cmd CommandPalette::selectedCommand() const {
    const int id = list_.selectedId();
    return (id > 0 && id < static_cast<int>(Cmd::Count)) ? static_cast<Cmd>(id) : Cmd::None;
}

void CommandPalette::SetPageRows(int rows) { list_.SetPageRows(rows); }

void CommandPalette::SelectRow(int index) { list_.SelectRow(index); }

void CommandPalette::Scroll(int deltaRows) { list_.Scroll(deltaRows); }

void CommandPalette::MoveCursor(int delta, bool absolute) { list_.MoveCursor(delta, absolute); }

CommandPalette::Action CommandPalette::HandleKey(const Chord& chord) {
    if (!visible_) return Action::None;

    const PickerList::Action action = list_.HandleKey(chord);
    Sync();
    switch (action) {
        case PickerList::Action::Accept: return Action::Run;
        case PickerList::Action::Close: return Action::Close;
        // Ctrl+Enter is swallowed rather than run: there is no second way to run a
        // command, and quietly treating it as Enter would make one up.
        case PickerList::Action::AcceptAlt:
        case PickerList::Action::None: break;
    }
    return Action::None;
}

bool CommandPalette::HandleChar(uint32_t codepoint) {
    if (!visible_) return false;
    if (!list_.HandleChar(codepoint)) return false;
    Sync();
    return true;
}

}  // namespace kite
