/// @file
/// @brief 実ファイルシステムに無い場所（「PC」「ごみ箱」「ネットワーク」）の表記。
///
/// この層は OS のシェル名前空間を知らない。知っているのは「`virtual:` で始まる
/// パスはファイルシステムのパスではない」という**文字列の規則**だけで、その先を
/// 何と読み替えるかはプラットフォーム層の仕事（Windows なら `platform/win/
/// VirtualNames.h` がシェルの解析名へ訳す）。
///
/// 規則をここに置くのは `config::Choose()` や `path::ToExtended()` と同じ理由 ─
/// Windows 実装の中に埋めるとテストできなくなる。

#pragma once

#include <string>
#include <string_view>

namespace kite::vfs {

/// @brief 仮想パスであることを示す前置。
///
/// ファイルシステムのパスとぶつからない綴りであることが唯一の要件。ドライブ文字
/// （`C:`）とも UNC（`\\srv`）とも、シェルの解析名（`::{CLSID}`）とも重ならない。
inline constexpr char kPrefix[] = "virtual:";

/// @brief 「PC」。ドライブと既知フォルダが並ぶ。
inline constexpr char kComputer[] = "virtual:computer";

/// @brief 「ごみ箱」。
inline constexpr char kRecycleBin[] = "virtual:recycle-bin";

/// @brief 「ネットワーク」。
inline constexpr char kNetwork[] = "virtual:network";

/// @brief 仮想パスかを判定する。
/// @param[in] p 対象のパス
/// @return `virtual:` で始まっていれば true
/// @note ここが true のパスは `IFileSystem` の実 FS 経路では読めない。列挙も
///       アイコンもプラットフォーム層がシェルへ回す
bool IsVirtual(std::string_view p);

/// @brief Kite が名前を知っている 3 つの根のどれかかを判定する。
/// @param[in] p 対象のパス
/// @return `kComputer` `kRecycleBin` `kNetwork` のいずれかなら true
bool IsWellKnown(std::string_view p);

/// @brief 表示名の i18n キーを返す。
/// @param[in] p 対象のパス
/// @return "ui.vfolder_computer" などのキー。名前を知らない場所なら nullptr
/// @note 3 つの根だけは**シェルの表示名ではなく Kite の言語設定に従わせる**。
///       Windows が英語で Kite が日本語、という組み合わせは普通にある
const char* LabelKey(std::string_view p);

/// @brief 「..」で上がる先を返す。
/// @param[in] p 対象のパス
/// @return 親のパス。これ以上遡れないなら空文字列
/// @note 実 FS の根から仮想フォルダへ抜ける規則もここが持つ ─ `C:\\` の上は
///       「PC」、`\\server` の上は「ネットワーク」。共有の親をサーバーにしたのと
///       同じ判断で、その上に実在の置き場所がある以上、そこで止めると `..` も
///       `Alt+↑` もパンくずも辿り着けない場所になる
std::string ParentOf(std::string_view p);

/// @brief 名前を知らない仮想パスの、見出しに使う末尾要素を返す。
/// @param[in] p 対象のパス
/// @return 末尾の構成要素。前置しか無ければ空文字列
/// @note 3 つの根には LabelKey() を先に当てること。ここが答えるのは、シェルが
///       返した解析名（`::{CLSID}\\...`）で表される入れ子の仮想フォルダだけ
std::string TrailingName(std::string_view p);

}  // namespace kite::vfs
