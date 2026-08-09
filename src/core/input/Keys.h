// Kite - platform-independent key identifiers and chords.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace kite {

enum class Key : uint16_t {
    None = 0,
    A, B, C, D, E, F, G, H, I, J, K, L, M,
    N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    Left, Right, Up, Down,
    Home, End, PageUp, PageDown,
    Enter, Escape, Tab, Space, Backspace, Delete, Insert,
    Minus, Equal, LBracket, RBracket, Backslash, Semicolon, Quote,
    Comma, Period, Slash, Grave,
    NumpadAdd, NumpadSub, NumpadMul, NumpadDiv, NumpadEnter,
    Menu,  // the context-menu key
    Count
};

enum Mod : uint8_t {
    kModNone = 0,
    kModCtrl = 1 << 0,
    kModShift = 1 << 1,
    kModAlt = 1 << 2,
};

struct Chord {
    Key key = Key::None;
    uint8_t mods = kModNone;

    constexpr bool valid() const { return key != Key::None; }
    constexpr uint32_t packed() const {
        return (static_cast<uint32_t>(mods) << 16) | static_cast<uint32_t>(key);
    }
    constexpr bool operator==(const Chord& o) const {
        return key == o.key && mods == o.mods;
    }
};

// "Ctrl+Shift+T", "Alt+Left", "F5". Returns an invalid chord on failure.
Chord ParseChord(std::string_view text);
std::string FormatChord(const Chord& c);

const char* KeyName(Key k);

}  // namespace kite
