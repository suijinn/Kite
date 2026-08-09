// Kite - UTF-8 code point helpers. All Kite strings are UTF-8 internally;
// conversion to UTF-16 happens only at the Win32 boundary.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace kite::utf8 {

constexpr uint32_t kReplacement = 0xFFFD;

// Decodes the code point starting at `i` and advances `i` past it.
uint32_t Decode(std::string_view s, size_t& i);

void Encode(uint32_t cp, std::string& out);
std::string Encode(uint32_t cp);

// Byte offset of the code point boundary before / after `i`.
size_t PrevBoundary(std::string_view s, size_t i);
size_t NextBoundary(std::string_view s, size_t i);

size_t CharCount(std::string_view s);

// ASCII-only case folding; enough for extension and command-name comparison.
std::string ToLowerAscii(std::string_view s);
bool EqualsIgnoreCaseAscii(std::string_view a, std::string_view b);

// True when the code point is a wide (East Asian) glyph. Used only for
// fallback text measuring when no real text engine is available.
bool IsWide(uint32_t cp);

}  // namespace kite::utf8
