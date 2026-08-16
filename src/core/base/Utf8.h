/// @file
/// @brief UTF-8 のコードポイント操作。
///
/// Kite の文字列は内部すべて UTF-8。UTF-16 への変換は platform/win/ の境界でのみ
/// 起きるため、コアはこのヘッダだけで文字単位の処理を完結させる。

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace kite::utf8 {

/// @brief 不正なバイト列に遭遇したときに返すコードポイント（U+FFFD）。
constexpr uint32_t kReplacement = 0xFFFD;

/// @brief `i` の位置から 1 文字を復号し、`i` をその次の文字の先頭へ進める。
/// @param[in] s 走査対象の UTF-8 文字列
/// @param[in,out] i 開始バイト位置。呼び出し後は次の文字の先頭を指す
/// @return 復号したコードポイント。壊れた列や途中で切れた列では kReplacement
/// @note 不正な列でも `i` は必ず 1 バイト以上進む。呼び出し側の無限ループを防ぐため
uint32_t Decode(std::string_view s, size_t& i);

/// @brief コードポイントを UTF-8 に符号化して末尾に追加する。
/// @param[in] cp 符号化するコードポイント
/// @param[out] out 追加先の文字列。既存の内容は保持される
void Encode(uint32_t cp, std::string& out);

/// @brief コードポイントを UTF-8 に符号化した文字列を返す。
/// @param[in] cp 符号化するコードポイント
/// @return 符号化結果の文字列
std::string Encode(uint32_t cp);

/// @brief `i` の直前にある文字の先頭バイト位置を返す。
/// @param[in] s 走査対象の UTF-8 文字列
/// @param[in] i 基準となるバイト位置
/// @return 直前の文字の先頭位置。`i` が 0 なら 0
size_t PrevBoundary(std::string_view s, size_t i);

/// @brief `i` の次にある文字の先頭バイト位置を返す。
/// @param[in] s 走査対象の UTF-8 文字列
/// @param[in] i 基準となるバイト位置
/// @return 次の文字の先頭位置。末尾に達していれば文字列長
size_t NextBoundary(std::string_view s, size_t i);

/// @brief 文字数（コードポイント数）を数える。
/// @param[in] s 対象の UTF-8 文字列
/// @return コードポイントの個数。バイト数ではない
size_t CharCount(std::string_view s);

/// @brief UTF-16 に変換したときの符号単位数を数える。
/// @param[in] s 対象の UTF-8 文字列
/// @return UTF-16 の符号単位数。BMP 外の文字はサロゲート対で 2 と数える
/// @note Windows の MAX_PATH はこの単位で決まる。バイト数で測ると、日本語の
///       フォルダ名で 3 倍に見積もって不要な長パス変換を掛けることになる
size_t Utf16Length(std::string_view s);

/// @brief ASCII 範囲だけを小文字化する。
/// @param[in] s 変換元の文字列
/// @return 変換後の文字列。マルチバイト文字はそのまま
std::string ToLowerAscii(std::string_view s);

/// @brief ASCII 範囲のみ大文字小文字を無視して比較する。
/// @param[in] a 比較する文字列
/// @param[in] b 比較する文字列
/// @return 等しければ true
bool EqualsIgnoreCaseAscii(std::string_view a, std::string_view b);

/// @brief ASCII 範囲のみ大文字小文字を無視して前方一致を判定する。
/// @param[in] s 走査対象の文字列
/// @param[in] prefix 前方一致させる文字列。空なら常に true
/// @return `s` が `prefix` で始まっていれば true
/// @note ASCII の外はバイト列のまま比較する。同じ文字を打った UTF-8 どうしなら
///       それで一致するし、ASCII の外の大文字小文字を畳むには本プロジェクトが
///       持たない変換表が要る
bool StartsWithIgnoreCaseAscii(std::string_view s, std::string_view prefix);

/// @brief 東アジアの全角文字かどうかを判定する。
/// @param[in] cp 判定するコードポイント
/// @return 全角なら true
/// @note 実テキストエンジンが無い場合の概算幅計算にのみ使う。DirectWrite が
///       使える経路では実測値を優先すること
bool IsWide(uint32_t cp);

}  // namespace kite::utf8
