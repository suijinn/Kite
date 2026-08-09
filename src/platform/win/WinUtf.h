// Kite - the single place UTF-8 meets UTF-16. Everything above this line in
// the codebase is UTF-8 only.
#pragma once

#include <string>
#include <string_view>

namespace kite::win {

std::wstring ToWide(std::string_view utf8);
std::string ToUtf8(std::wstring_view wide);
std::string ToUtf8(const wchar_t* wide);

// Prefixes "\\?\" when a path is long enough to need it, so deep trees and
// cloud folders with long names still enumerate.
std::wstring ToExtendedPath(std::string_view utf8);

}  // namespace kite::win
