/// @file
/// @brief サイズと日時の人間向け整形。

#pragma once

#include <cstdint>
#include <string>

namespace kite {

/// @brief バイト数を «12.3 MB» 形式に整形する。
/// @param[in] bytes バイト数
/// @return 整形した文字列。1024 未満はバイト表記、以降は 1024 進で KB〜PB
/// @note 一覧の列に収めるため、値が 10 以上になると小数を落とす
std::string FormatSize(uint64_t bytes);

/// @brief Unix 時刻をローカル時刻の «2026-08-09 17:11» 形式に整形する。
/// @param[in] unixSeconds Unix エポックからの秒数
/// @return 整形した 16 文字の文字列。`unixSeconds` が 0 以下、または変換に
///         失敗した場合は空文字列
std::string FormatDateTime(int64_t unixSeconds);

}  // namespace kite
