/// @file
/// @brief UTF-8 と UTF-16 が出会う唯一の場所。
///
/// このファイルより上の層は UTF-8 のみを扱う。

#pragma once

#include <string>
#include <string_view>

namespace kite::win {

/// @brief UTF-8 を UTF-16 に変換する。
/// @param[in] utf8 変換元の文字列
/// @return 変換後のワイド文字列。変換に失敗した場合は空文字列
std::wstring ToWide(std::string_view utf8);

/// @brief UTF-16 を UTF-8 に変換する。
/// @param[in] wide 変換元のワイド文字列
/// @return 変換後の文字列。変換に失敗した場合は空文字列
std::string ToUtf8(std::wstring_view wide);

/// @brief NUL 終端のワイド文字列を UTF-8 に変換する。
/// @param[in] wide 変換元のワイド文字列。nullptr を渡してもよい
/// @return 変換後の文字列。nullptr の場合は空文字列
std::string ToUtf8(const wchar_t* wide);

/// @brief 必要なら "\\\\?\\" を付けた UTF-16 パスを返す。
/// @param[in] utf8 変換元のパス（UTF-8）
/// @return Win32 API に渡せるワイドパス。260 文字制限を超える深い階層や、
///         名前の長いクラウドフォルダでも列挙できるようにする
std::wstring ToExtendedPath(std::string_view utf8);

}  // namespace kite::win
