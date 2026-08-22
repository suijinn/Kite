#include "core/input/TextField.h"

#include "core/base/PathUtil.h"
#include "core/base/Utf8.h"

namespace kite {

bool TextField::Insert(std::string_view s) {
    if (s.empty()) return false;
    DeleteSelection();
    text.insert(caret, s);
    SetCaret(caret + s.size());
    return true;
}

TextField::Edit TextField::HandleKey(const Chord& chord) {
    switch (chord.key) {
        case Key::Left:
        case Key::Right: {
            const bool back = (chord.key == Key::Left);
            const bool extend = (chord.mods & kModShift) != 0;
            size_t pos;
            if ((chord.mods & kModCtrl) != 0) {
                // Ctrl moves by path component. In a field that holds a path,
                // that is what a "word" is - stopping inside "Users" is never
                // what anyone reaches for Ctrl+arrow to do.
                pos = back ? path::PrevSegment(text, caret) : path::NextSegment(text, caret);
            } else if (!extend && hasSelection()) {
                // Without Shift, an arrow collapses the selection to its edge
                // rather than also eating a character.
                pos = back ? selBegin() : selEnd();
            } else {
                pos = back ? utf8::PrevBoundary(text, caret) : utf8::NextBoundary(text, caret);
            }
            // Shift keeps the anchor where it was, which is what makes a run of
            // Shift+arrow grow one selection instead of a series of them.
            if (extend) {
                caret = pos;
            } else {
                SetCaret(pos);
            }
            return Edit::Moved;
        }
        case Key::Home:
        case Key::End: {
            const size_t pos = (chord.key == Key::Home) ? 0 : text.size();
            if ((chord.mods & kModShift) != 0) {
                caret = pos;
            } else {
                SetCaret(pos);
            }
            return Edit::Moved;
        }
        case Key::Backspace: {
            if (DeleteSelection()) return Edit::Changed;
            if (caret == 0) return Edit::Moved;
            const size_t start = utf8::PrevBoundary(text, caret);
            text.erase(start, caret - start);
            SetCaret(start);
            return Edit::Changed;
        }
        case Key::Delete: {
            // Shift+Delete is the other spelling of cut, the way Shift+Insert is
            // the other spelling of paste - and this type never touches the
            // clipboard, so it leaves that chord to whoever can.
            if ((chord.mods & kModShift) != 0) return Edit::None;
            if (DeleteSelection()) return Edit::Changed;
            if (caret >= text.size()) return Edit::Moved;
            const size_t end = utf8::NextBoundary(text, caret);
            text.erase(caret, end - caret);
            return Edit::Changed;
        }
        case Key::A:
            // Select all. The chord is Cmd::SelectAll everywhere else, and it
            // means the same thing here - the field is what has focus.
            if (chord.mods != kModCtrl) return Edit::None;
            SelectAll();
            return Edit::Moved;
        default:
            return Edit::None;
    }
}

}  // namespace kite
