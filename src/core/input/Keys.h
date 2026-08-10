/// @file
/// @brief プラットフォーム非依存のキー識別子と和音（コード）。

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace kite {

/// @brief 物理キーの識別子。
///
/// 各プラットフォームは自前のキーコードをこの値へ変換する。Windows 実装は
/// platform/win/WinWindow.cpp の TranslateVirtualKey()。
enum class Key : uint16_t {
    None = 0,  ///< 無効・未割り当て
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
    Menu,   ///< コンテキストメニューキー
    Count   ///< 列挙の終端。有効なキーではない
};

/// @brief 修飾キーのビットマスク。
enum Mod : uint8_t {
    kModNone = 0,       ///< 修飾なし
    kModCtrl = 1 << 0,  ///< Ctrl
    kModShift = 1 << 1, ///< Shift
    kModAlt = 1 << 2,   ///< Alt
};

/// @brief 修飾キーとキーの組み合わせ 1 打鍵分。
/// @todo 2 打鍵のキーシーケンスに拡張する（docs/ROADMAP.md の P3-2）
struct Chord {
    Key key = Key::None;      ///< 主となるキー
    uint8_t mods = kModNone;  ///< 押されている修飾キーの Mod ビット和

    /// @brief 有効な和音かを判定する。
    /// @return 主キーが設定されていれば true
    constexpr bool valid() const { return key != Key::None; }

    /// @brief ハッシュ表の鍵に使える整数へ詰める。
    /// @return 修飾とキーを 1 つにまとめた値
    constexpr uint32_t packed() const {
        return (static_cast<uint32_t>(mods) << 16) | static_cast<uint32_t>(key);
    }

    /// @brief 2 つの和音が等しいかを判定する。
    /// @param[in] o 比較する和音
    /// @return キーと修飾がともに一致すれば true
    constexpr bool operator==(const Chord& o) const {
        return key == o.key && mods == o.mods;
    }
};

/// @brief 文字列表現から和音を解釈する。
/// @param[in] text "Ctrl+Shift+T"、"Alt+Left"、"F5" のような表記
/// @return 解釈した和音。解釈できなければ valid() が false の和音
Chord ParseChord(std::string_view text);

/// @brief 和音を文字列表現に変換する。
/// @param[in] c 変換する和音
/// @return "Ctrl+Shift+T" 形式の文字列。無効な和音では空文字列
std::string FormatChord(const Chord& c);

/// @brief キーの正式な表記名を返す。
/// @param[in] k 対象のキー
/// @return 表記名。未知のキーでは空文字列
const char* KeyName(Key k);

}  // namespace kite
