// Kite - textual path handling. No filesystem access, no OS calls, so it is
// unit-testable and portable. Accepts both '\' and '/' as separators.
#pragma once

#include <string>
#include <string_view>

namespace kite::path {

// The separator this build emits when joining. Becomes a platform trait when a
// non-Windows backend lands.
constexpr char kSep = '\\';

bool IsSep(char c);

std::string Join(std::string_view a, std::string_view b);

// Parent directory, or "" when `p` is already a root.
std::string Parent(std::string_view p);

// Last component. For "C:\" returns "C:\".
std::string FileName(std::string_view p);

// Lowercased extension without the dot; "" when there is none.
std::string Extension(std::string_view p);

// File name without the extension.
std::string Stem(std::string_view p);

bool IsRoot(std::string_view p);

// Collapses duplicate separators and resolves "." / ".." textually.
std::string Normalize(std::string_view p);

// A short label suited to a tab title: the last component, or the root itself.
std::string DisplayName(std::string_view p);

// Explorer-like ordering: case-insensitive, digit runs compared numerically.
int NaturalCompare(std::string_view a, std::string_view b);

// Percent-escapes the characters Kite's session serialization uses as
// delimiters, so arbitrary paths survive a round trip.
std::string EscapeToken(std::string_view s);
std::string UnescapeToken(std::string_view s);

}  // namespace kite::path
