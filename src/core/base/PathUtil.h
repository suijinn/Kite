/// @file
/// @brief パス文字列の操作。
///
/// ファイルシステムには一切アクセスしないので単体テスト可能で、移植性もある。
/// 区切り文字は '\\' と '/' の両方を受け付ける。

#pragma once

#include <string>
#include <string_view>

namespace kite::path {

/// @brief 連結時に出力する区切り文字。
/// @note 非 Windows バックエンドを足す際にプラットフォーム特性へ移すこと。
constexpr char kSep = '\\';

/// @brief 文字がパス区切りかを判定する。
/// @param[in] c 判定する文字
/// @return '\\' または '/' なら true
bool IsSep(char c);

/// @brief 2 つのパスを区切り文字 1 個で連結する。
/// @param[in] a 前半のパス
/// @param[in] b 後半のパス
/// @return 連結結果。片方が空ならもう一方をそのまま返す
std::string Join(std::string_view a, std::string_view b);

/// @brief 親ディレクトリを返す。
/// @param[in] p 対象のパス
/// @return 親ディレクトリ。`p` がすでにルートなら空文字列
std::string Parent(std::string_view p);

/// @brief 最後の構成要素を返す。
/// @param[in] p 対象のパス
/// @return 末尾の名前。"C:\\" のようなルートに対してはルート自身
std::string FileName(std::string_view p);

/// @brief 拡張子を小文字で返す。
/// @param[in] p 対象のパス
/// @return ドットを含まない小文字の拡張子。無い場合や先頭ドットのみの
///         名前（".gitignore" など）では空文字列
std::string Extension(std::string_view p);

/// @brief 拡張子を除いたファイル名を返す。
/// @param[in] p 対象のパス
/// @return 拡張子を除いた名前
std::string Stem(std::string_view p);

/// @brief 1 つ前の構成要素の先頭位置を返す。
/// @param[in] p 対象のパス
/// @param[in] pos 現在位置。バイト添字。`p` の長さを超える値は末尾に丸める
/// @return 移動先のバイト添字。すでに先頭なら 0
/// @note 入力欄で `Ctrl+←` が動く単位。直前の区切りをまたいでから名前の先頭まで
///       戻るので、`C:\a\b` の末尾からは `b` の先頭、次に `a` の先頭に止まる。
///       区切りは ASCII なので、UTF-8 の途中で切れることはない
size_t PrevSegment(std::string_view p, size_t pos);

/// @brief 1 つ後ろの構成要素の末尾位置を返す。
/// @param[in] p 対象のパス
/// @param[in] pos 現在位置。バイト添字
/// @return 移動先のバイト添字。すでに末尾なら `p` の長さ
/// @note 入力欄で `Ctrl+→` が動く単位。PrevSegment() と対になる
size_t NextSegment(std::string_view p, size_t pos);

/// @brief パスが絶対パスかを判定する。
/// @param[in] p 対象のパス
/// @return "C:\\foo"、"\\\\server\\share"、"\\foo" のように根から始まっていれば true
bool IsAbsolute(std::string_view p);

/// @brief パスがルートそのものかを判定する。
/// @param[in] p 対象のパス
/// @return "C:\\" や "\\\\server\\share\\" のようなルートなら true
bool IsRoot(std::string_view p);

/// @brief 区切りの重複を畳み、"." と ".." を文字列上で解決する。
/// @param[in] p 対象のパス
/// @return 正規化したパス。ドライブ文字は大文字に揃える
/// @note ルートより上には遡らない。シンボリックリンクは解決しない
std::string Normalize(std::string_view p);

/// @brief タブ見出しに適した短い表示名を返す。
/// @param[in] p 対象のパス
/// @return 末尾の構成要素。ルートの場合は末尾の区切りを落とした形（"C:"）
std::string DisplayName(std::string_view p);

/// @brief エクスプローラー風の自然順比較を行う。
/// @param[in] a 比較する名前
/// @param[in] b 比較する名前
/// @return `a` が小さければ負、等しければ 0、大きければ正
/// @note 大文字小文字は ASCII 範囲で無視し、数字の並びは数値として比較する。
///       これにより "image2" が "image10" より前に来る
int NaturalCompare(std::string_view a, std::string_view b);

/// @brief セッション直列化の区切り文字をパーセントエスケープする。
/// @param[in] s エスケープ対象の文字列
/// @return エスケープ済み文字列。'%' '|' ',' '(' ')' '{' '}' '@' '=' と制御文字が対象
std::string EscapeToken(std::string_view s);

/// @brief EscapeToken() の逆変換を行う。
/// @param[in] s エスケープ済みの文字列
/// @return 復元した文字列。不正なエスケープ列はそのまま残す
std::string UnescapeToken(std::string_view s);

}  // namespace kite::path
