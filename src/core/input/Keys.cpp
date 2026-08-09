#include "core/input/Keys.h"

#include "core/base/Utf8.h"

namespace kite {
namespace {

struct KeyNameEntry {
    Key key;
    const char* name;
};

// Order does not matter for lookup; the first match wins when formatting.
const KeyNameEntry kKeyNames[] = {
    { Key::A, "A" }, { Key::B, "B" }, { Key::C, "C" }, { Key::D, "D" }, { Key::E, "E" },
    { Key::F, "F" }, { Key::G, "G" }, { Key::H, "H" }, { Key::I, "I" }, { Key::J, "J" },
    { Key::K, "K" }, { Key::L, "L" }, { Key::M, "M" }, { Key::N, "N" }, { Key::O, "O" },
    { Key::P, "P" }, { Key::Q, "Q" }, { Key::R, "R" }, { Key::S, "S" }, { Key::T, "T" },
    { Key::U, "U" }, { Key::V, "V" }, { Key::W, "W" }, { Key::X, "X" }, { Key::Y, "Y" },
    { Key::Z, "Z" },
    { Key::Num0, "0" }, { Key::Num1, "1" }, { Key::Num2, "2" }, { Key::Num3, "3" },
    { Key::Num4, "4" }, { Key::Num5, "5" }, { Key::Num6, "6" }, { Key::Num7, "7" },
    { Key::Num8, "8" }, { Key::Num9, "9" },
    { Key::F1, "F1" }, { Key::F2, "F2" }, { Key::F3, "F3" }, { Key::F4, "F4" },
    { Key::F5, "F5" }, { Key::F6, "F6" }, { Key::F7, "F7" }, { Key::F8, "F8" },
    { Key::F9, "F9" }, { Key::F10, "F10" }, { Key::F11, "F11" }, { Key::F12, "F12" },
    { Key::Left, "Left" }, { Key::Right, "Right" }, { Key::Up, "Up" }, { Key::Down, "Down" },
    { Key::Home, "Home" }, { Key::End, "End" },
    { Key::PageUp, "PageUp" }, { Key::PageDown, "PageDown" },
    { Key::Enter, "Enter" }, { Key::Escape, "Escape" }, { Key::Tab, "Tab" },
    { Key::Space, "Space" }, { Key::Backspace, "Backspace" }, { Key::Delete, "Delete" },
    { Key::Insert, "Insert" },
    { Key::Minus, "-" }, { Key::Equal, "=" }, { Key::LBracket, "[" }, { Key::RBracket, "]" },
    { Key::Backslash, "\\" }, { Key::Semicolon, ";" }, { Key::Quote, "'" },
    { Key::Comma, "," }, { Key::Period, "." }, { Key::Slash, "/" }, { Key::Grave, "`" },
    { Key::NumpadAdd, "Num+" }, { Key::NumpadSub, "Num-" }, { Key::NumpadMul, "Num*" },
    { Key::NumpadDiv, "Num/" }, { Key::NumpadEnter, "NumEnter" },
    { Key::Menu, "Menu" },
};

// Accepted spellings that are not the canonical name.
const KeyNameEntry kKeyAliases[] = {
    { Key::Escape, "Esc" },
    { Key::Enter, "Return" },
    { Key::PageUp, "PgUp" },
    { Key::PageDown, "PgDn" },
    { Key::Delete, "Del" },
    { Key::Insert, "Ins" },
    { Key::Backspace, "BS" },
    { Key::Minus, "Minus" },
    { Key::Equal, "Plus" },
    { Key::Comma, "Comma" },
    { Key::Period, "Period" },
    { Key::Slash, "Slash" },
    { Key::Grave, "Grave" },
    { Key::Backslash, "Backslash" },
};

Key KeyFromName(std::string_view name) {
    for (const KeyNameEntry& e : kKeyNames) {
        if (utf8::EqualsIgnoreCaseAscii(name, e.name)) return e.key;
    }
    for (const KeyNameEntry& e : kKeyAliases) {
        if (utf8::EqualsIgnoreCaseAscii(name, e.name)) return e.key;
    }
    return Key::None;
}

}  // namespace

const char* KeyName(Key k) {
    for (const KeyNameEntry& e : kKeyNames) {
        if (e.key == k) return e.name;
    }
    return "";
}

Chord ParseChord(std::string_view text) {
    Chord c;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t plus = text.find('+', pos);
        // A trailing "+" is the key itself, e.g. "Ctrl++".
        if (plus == pos && pos + 1 >= text.size()) plus = std::string_view::npos;

        std::string_view part = (plus == std::string_view::npos) ? text.substr(pos)
                                                                 : text.substr(pos, plus - pos);
        // Trim.
        while (!part.empty() && (part.front() == ' ' || part.front() == '\t')) part.remove_prefix(1);
        while (!part.empty() && (part.back() == ' ' || part.back() == '\t')) part.remove_suffix(1);

        if (!part.empty()) {
            if (utf8::EqualsIgnoreCaseAscii(part, "Ctrl") ||
                utf8::EqualsIgnoreCaseAscii(part, "Control")) {
                c.mods |= kModCtrl;
            } else if (utf8::EqualsIgnoreCaseAscii(part, "Shift")) {
                c.mods |= kModShift;
            } else if (utf8::EqualsIgnoreCaseAscii(part, "Alt")) {
                c.mods |= kModAlt;
            } else {
                const Key k = KeyFromName(part);
                if (k == Key::None) return {};
                c.key = k;
            }
        }
        if (plus == std::string_view::npos) break;
        pos = plus + 1;
    }
    return c.valid() ? c : Chord{};
}

std::string FormatChord(const Chord& c) {
    if (!c.valid()) return {};
    std::string out;
    if (c.mods & kModCtrl) out += "Ctrl+";
    if (c.mods & kModAlt) out += "Alt+";
    if (c.mods & kModShift) out += "Shift+";
    out += KeyName(c.key);
    return out;
}

}  // namespace kite
