#include "core/input/KeyMap.h"

#include <algorithm>

#include "core/base/Utf8.h"

namespace kite {
namespace {

struct DefaultBinding {
    Cmd cmd;
    const char* chord;
};

// Design notes for the defaults:
//  * Ctrl+<digit> selects a tab, Alt+<digit> selects a session, Alt+Shift+<digit>
//    jumps to a bookmark - three parallel, easy-to-remember rows.
//  * Everything created lives on N, in rough order of how big the new thing is:
//    Ctrl+N a window, Ctrl+Alt+N a session, Ctrl+Shift+N a folder,
//    Ctrl+Alt+Shift+N a file.
//  * Tab / Shift+Tab move between panes (the Total Commander muscle memory).
//  * Alt+V / Alt+H split; both are reachable on a JIS keyboard, unlike Ctrl+\.
//  * The Menu key opens the *extended* shell menu directly - that one-step
//    access is a core reason this app exists. Ctrl+Menu aims the same menu at
//    the folder being viewed, which a selection would otherwise hide.
const DefaultBinding kDefaults[] = {
    // Application
    { Cmd::NewWindow,           "Ctrl+N" },
    { Cmd::Quit,                "Ctrl+Q" },
    { Cmd::ReloadConfig,        "Ctrl+Alt+C" },
    { Cmd::OpenConfigFolder,    "Ctrl+Alt+Comma" },
    { Cmd::ToggleTheme,         "Ctrl+Shift+M" },
    { Cmd::ShowKeyHelp,         "F1" },
    { Cmd::ShowKeySettings,     "Ctrl+F1" },
    // 設定画面と、その中身が書かれるフォルダ。同じキーに Alt を足すと後者になる。
    { Cmd::ShowSettings,        "Ctrl+Comma" },
    { Cmd::CancelOverlay,       "Escape" },

    // Navigation
    { Cmd::GoUp,                "Backspace" },
    { Cmd::GoUp,                "Alt+Up" },
    { Cmd::GoBack,              "Alt+Left" },
    { Cmd::GoForward,           "Alt+Right" },
    { Cmd::GoHome,              "Alt+Home" },
    { Cmd::Refresh,             "F5" },
    { Cmd::Refresh,             "Ctrl+R" },
    { Cmd::OpenSelected,        "Enter" },
    { Cmd::OpenInNewTab,        "Ctrl+Enter" },
    { Cmd::EditPath,            "Ctrl+L" },
    { Cmd::FocusFilter,         "Ctrl+F" },

    // Cursor and selection
    { Cmd::CursorUp,            "Up" },
    { Cmd::CursorDown,          "Down" },
    { Cmd::CursorPageUp,        "PageUp" },
    { Cmd::CursorPageDown,      "PageDown" },
    { Cmd::CursorTop,           "Home" },
    { Cmd::CursorBottom,        "End" },
    { Cmd::ExtendUp,            "Shift+Up" },
    { Cmd::ExtendDown,          "Shift+Down" },
    { Cmd::ExtendPageUp,        "Shift+PageUp" },
    { Cmd::ExtendPageDown,      "Shift+PageDown" },
    { Cmd::ExtendTop,           "Shift+Home" },
    { Cmd::ExtendBottom,        "Shift+End" },
    { Cmd::ToggleSelection,     "Space" },
    { Cmd::ToggleSelection,     "Insert" },
    { Cmd::SelectAll,           "Ctrl+A" },
    { Cmd::SelectNone,          "Ctrl+Shift+A" },
    { Cmd::InvertSelection,     "Ctrl+I" },

    // Tabs
    { Cmd::NewTab,              "Ctrl+T" },
    { Cmd::CloseTab,            "Ctrl+W" },
    { Cmd::DuplicateTab,        "Ctrl+K" },
    { Cmd::ReopenTab,           "Ctrl+Shift+T" },
    { Cmd::NextTab,             "Ctrl+Tab" },
    { Cmd::NextTab,             "Ctrl+PageDown" },
    { Cmd::PrevTab,             "Ctrl+Shift+Tab" },
    { Cmd::PrevTab,             "Ctrl+PageUp" },
    { Cmd::MoveTabLeft,         "Ctrl+Shift+PageUp" },
    { Cmd::MoveTabRight,        "Ctrl+Shift+PageDown" },
    { Cmd::Tab1,                "Ctrl+1" },
    { Cmd::Tab2,                "Ctrl+2" },
    { Cmd::Tab3,                "Ctrl+3" },
    { Cmd::Tab4,                "Ctrl+4" },
    { Cmd::Tab5,                "Ctrl+5" },
    { Cmd::Tab6,                "Ctrl+6" },
    { Cmd::Tab7,                "Ctrl+7" },
    { Cmd::Tab8,                "Ctrl+8" },
    { Cmd::TabLast,             "Ctrl+9" },

    // Panes
    { Cmd::SplitLeftRight,      "Alt+V" },
    { Cmd::SplitTopBottom,      "Alt+H" },
    { Cmd::ClosePane,           "Alt+W" },
    { Cmd::FocusNextPane,       "Tab" },
    { Cmd::FocusPrevPane,       "Shift+Tab" },
    { Cmd::FocusPaneLeft,       "Ctrl+Alt+Left" },
    { Cmd::FocusPaneRight,      "Ctrl+Alt+Right" },
    { Cmd::FocusPaneUp,         "Ctrl+Alt+Up" },
    { Cmd::FocusPaneDown,       "Ctrl+Alt+Down" },
    { Cmd::OpenInOtherPane,     "Ctrl+Shift+Enter" },
    { Cmd::SyncOtherPane,       "Alt+S" },
    { Cmd::SwapPanes,           "Alt+X" },

    // Sessions
    { Cmd::NewSession,          "Ctrl+Alt+N" },
    { Cmd::CloseSession,        "Ctrl+Alt+W" },
    { Cmd::RenameSession,       "Ctrl+Alt+R" },
    { Cmd::NextSession,         "Ctrl+Alt+PageDown" },
    { Cmd::PrevSession,         "Ctrl+Alt+PageUp" },
    { Cmd::MoveSessionLeft,     "Ctrl+Alt+Shift+PageUp" },
    { Cmd::MoveSessionRight,    "Ctrl+Alt+Shift+PageDown" },
    { Cmd::SaveWorkspace,       "Ctrl+S" },
    { Cmd::Session1,            "Alt+1" },
    { Cmd::Session2,            "Alt+2" },
    { Cmd::Session3,            "Alt+3" },
    { Cmd::Session4,            "Alt+4" },
    { Cmd::Session5,            "Alt+5" },
    { Cmd::Session6,            "Alt+6" },
    { Cmd::Session7,            "Alt+7" },
    { Cmd::Session8,            "Alt+8" },

    // View
    { Cmd::ToggleHidden,        "Ctrl+H" },
    { Cmd::ToggleSidebar,       "Ctrl+B" },
    { Cmd::SortByName,          "Ctrl+Shift+1" },
    { Cmd::SortByExt,           "Ctrl+Shift+2" },
    { Cmd::SortBySize,          "Ctrl+Shift+3" },
    { Cmd::SortByDate,          "Ctrl+Shift+4" },
    { Cmd::ToggleSortOrder,     "Ctrl+Shift+R" },
    { Cmd::ToggleDirsFirst,     "Ctrl+Shift+F" },
    // The browser row, and for the same reason: this is the one setting people
    // reach for without wanting to know where the settings live. Ctrl+Plus is
    // VK_OEM_PLUS, which is "=" on a US layout and ";" on a JIS one - both are
    // the key browsers zoom in on there. Shift+ is bound too, because "Ctrl and
    // the plus sign" is what the hand actually does on a US keyboard.
    { Cmd::FontLarger,          "Ctrl+Plus" },
    { Cmd::FontLarger,          "Ctrl+Shift+Plus" },
    { Cmd::FontSmaller,         "Ctrl+Minus" },
    { Cmd::FontReset,           "Ctrl+0" },

    // Bookmarks
    { Cmd::AddBookmark,         "Ctrl+D" },
    { Cmd::RemoveBookmark,      "Ctrl+Shift+D" },
    // 番号が尽きた先。9 個目以降のブックマークにキーボードで届く道はこれだけなので、
    // 数字の列（Alt+Shift+<桁>）に揃えるより打ちやすさを取って修飾を 1 つにしてある。
    // Ctrl+B のほうがブラウザ流だが、あちらは既にサイドバーの表示切り替え ─ 既定を
    // 移すと、その行を持つ既存の keys.ini と 1 つの和音を取り合う。
    // 文字キー単独と Shift+<文字> は空けたまま残す（ROADMAP P3-4 の型入力ジャンプ）。
    { Cmd::ShowBookmarks,       "Alt+B" },
    { Cmd::Bookmark1,           "Alt+Shift+1" },
    { Cmd::Bookmark2,           "Alt+Shift+2" },
    { Cmd::Bookmark3,           "Alt+Shift+3" },
    { Cmd::Bookmark4,           "Alt+Shift+4" },
    { Cmd::Bookmark5,           "Alt+Shift+5" },
    { Cmd::Bookmark6,           "Alt+Shift+6" },
    { Cmd::Bookmark7,           "Alt+Shift+7" },
    { Cmd::Bookmark8,           "Alt+Shift+8" },

    // File operations
    { Cmd::NewFolder,           "Ctrl+Shift+N" },
    // N is the "new" letter, and Ctrl+N is spoken for by the new window - the
    // one binding here that other apps also have. New file takes the last free
    // seat in the row rather than moving off N and being looked for there.
    { Cmd::NewFile,             "Ctrl+Alt+Shift+N" },
    { Cmd::Rename,              "F2" },
    { Cmd::DeleteToRecycle,     "Delete" },
    { Cmd::Restore,            "Alt+R" },
    { Cmd::DeletePermanent,     "Shift+Delete" },
    { Cmd::Copy,                "Ctrl+C" },
    { Cmd::Cut,                 "Ctrl+X" },
    { Cmd::Paste,               "Ctrl+V" },
    { Cmd::CopyPath,            "Ctrl+Shift+C" },
    { Cmd::CopyName,            "Ctrl+Alt+Shift+C" },
    { Cmd::Undo,                "Ctrl+Z" },

    // Shell
    // Shift adds the extended verbs, the way it does in Explorer. Kite used to
    // put them on the plain menu instead - "everything one action away" - but
    // those verbs are exactly the ones the shell hides on purpose, and handlers
    // stock them accordingly: TortoiseGit files "Git Clone..." there, which then
    // showed up while standing inside a working copy.
    { Cmd::ContextMenu,               "Menu" },
    { Cmd::ExtendedContextMenu,       "Shift+F10" },
    { Cmd::FolderContextMenu,         "Ctrl+Menu" },
    { Cmd::ExtendedFolderContextMenu, "Ctrl+Shift+F10" },
    { Cmd::Properties,          "Alt+Enter" },
    { Cmd::OpenWith,            "Ctrl+Alt+Enter" },
    { Cmd::RevealInExplorer,    "Ctrl+Shift+E" },
    { Cmd::OpenTerminal,        "Ctrl+Grave" },
    { Cmd::OpenTerminal,        "Alt+T" },
    // Next to the address bar's own key: typing \\192.168.1.5 there is what
    // leads here, and signing in is the step that makes the same typing work.
    { Cmd::ConnectNetwork,      "Ctrl+Shift+L" },
};

}  // namespace

void KeyMap::LoadDefaults() {
    byChord_.clear();
    order_.clear();
    for (const DefaultBinding& d : kDefaults) {
        const Chord c = ParseChord(d.chord);
        if (c.valid()) Bind(c, d.cmd);
    }
}

std::vector<Chord> KeyMap::DefaultChordsFor(Cmd id) {
    std::vector<Chord> out;
    for (const DefaultBinding& d : kDefaults) {
        if (d.cmd != id) continue;
        const Chord c = ParseChord(d.chord);
        if (c.valid()) out.push_back(c);
    }
    return out;
}

void KeyMap::Bind(const Chord& c, Cmd id) {
    if (!c.valid()) return;
    auto it = byChord_.find(c.packed());
    if (it != byChord_.end()) {
        // The chord is being reassigned: drop the stale display entry.
        order_.erase(std::remove_if(order_.begin(), order_.end(),
                                    [&](const std::pair<Chord, Cmd>& p) { return p.first == c; }),
                     order_.end());
    }
    byChord_[c.packed()] = id;
    order_.push_back({ c, id });
}

void KeyMap::Unbind(const Chord& c) {
    byChord_.erase(c.packed());
    order_.erase(std::remove_if(order_.begin(), order_.end(),
                                [&](const std::pair<Chord, Cmd>& p) { return p.first == c; }),
                 order_.end());
}

void KeyMap::UnbindCommand(Cmd id) {
    for (auto it = byChord_.begin(); it != byChord_.end();) {
        it = (it->second == id) ? byChord_.erase(it) : std::next(it);
    }
    order_.erase(std::remove_if(order_.begin(), order_.end(),
                                [&](const std::pair<Chord, Cmd>& p) { return p.second == id; }),
                 order_.end());
}

void KeyMap::ApplyIni(const Ini& ini, std::vector<std::string>* warnings) {
    const Ini::Section* sec = ini.Find("keys");
    if (!sec) return;

    // A command the file names has exactly the chords the file gives it: every
    // existing binding goes first, in one pass, so line order cannot matter.
    //
    // Adding on top of the defaults instead was worse than it sounds. The file
    // Kite writes lists every command, so editing one of its lines is the normal
    // way to change a key - and the default stayed bound underneath, still first
    // in insertion order, which is the one the F1 sheet shows. The edit worked
    // nowhere the user could see it. "none" falls out of the same pass.
    for (const Ini::Entry& e : sec->entries) {
        const Cmd id = CommandFromName(e.key);
        if (id != Cmd::None) UnbindCommand(id);
    }

    for (const Ini::Entry& e : sec->entries) {
        if (utf8::EqualsIgnoreCaseAscii(e.value, "none")) continue;
        const Cmd id = CommandFromName(e.key);
        if (id == Cmd::None) {
            if (warnings) warnings->push_back("keys.ini: unknown command '" + e.key + "'");
            continue;
        }
        const Chord c = ParseChord(e.value);
        if (!c.valid()) {
            if (warnings) warnings->push_back("keys.ini: unparsable chord '" + e.value + "'");
            continue;
        }
        Bind(c, id);
    }
}

Cmd KeyMap::Lookup(const Chord& c) const {
    auto it = byChord_.find(c.packed());
    return it == byChord_.end() ? Cmd::None : it->second;
}

std::vector<Chord> KeyMap::ChordsFor(Cmd id) const {
    std::vector<Chord> out;
    for (const std::pair<Chord, Cmd>& p : order_) {
        if (p.second == id) out.push_back(p.first);
    }
    return out;
}

std::string KeyMap::ChordText(Cmd id) const {
    std::string out;
    for (const Chord& c : ChordsFor(id)) {
        if (!out.empty()) out += ", ";
        out += FormatChord(c);
    }
    return out;
}

Ini KeyMap::ToIni() const {
    Ini ini;
    Ini::Section& sec = ini.Ensure("keys");
    // Emit grouped by command so the file reads like documentation.
    for (const CommandInfo& info : AllCommands()) {
        const std::vector<Chord> chords = ChordsFor(info.id);
        if (chords.empty()) {
            // A real line, not a comment: read back, a commented-out command is
            // a command the file never mentions, and the default returns. An
            // unbound shortcut would come back from the dead on the next start.
            sec.entries.push_back({ info.name, "none" });
            continue;
        }
        for (const Chord& c : chords) {
            sec.entries.push_back({ info.name, FormatChord(c) });
        }
    }
    return ini;
}

}  // namespace kite
