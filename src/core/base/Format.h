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

/// @brief 経過時間を «数 + 単位» に分解した結果。
///
/// **文字列にはしない。** 「5 分前」の綴りは言語ごとに違うので、ここが答えられるのは
/// «いくつの、どの単位か» までで、言葉にするのは画面の側（塊の見出しと同じ 2 本立て）。
struct AgeText {
    const char* labelKey = "";  ///< i18n キー。`{0}` に count が入る。空なら出さない
    int count = 0;              ///< 単位いくつ分か。`ui.age_now` では使わない
};

/// @brief 「どれだけ前か」を数と単位に分解する。
/// @param[in] unixSeconds 対象の時刻（Unix 秒）。0 以下なら空を返す
/// @param[in] nowSeconds 現在時刻（Unix 秒）
/// @return 単位の i18n キーと数
/// @note **時計は呼ぶ側が渡す。** ここが現在時刻を読むと、同じ入力に対する答えが
///       日によって変わり、テストが書けなくなる（TypeAhead の時間切れと同じ扱い）
/// @note 未来の時刻は «たった今» に畳む。時計のずれた共有の上では普通に起きるが、
///       「-3 分前」と出しても読む人には何の役にも立たない
AgeText FormatAge(int64_t unixSeconds, int64_t nowSeconds);

/// @brief Unix 時刻をローカル時刻の «2026-08-09 17:11» 形式に整形する。
/// @param[in] unixSeconds Unix エポックからの秒数
/// @return 整形した 16 文字の文字列。`unixSeconds` が 0 以下、または変換に
///         失敗した場合は空文字列
std::string FormatDateTime(int64_t unixSeconds);

}  // namespace kite
