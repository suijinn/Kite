/// @file
/// @brief 実行中の exe 自身の位置と、Win32 に渡すパスの形。
///
/// Kite は自分の隣にあるものを 2 つ当てにしている ─ `kite_shellhost.exe` と、
/// ポータブル運用時の `config` フォルダ。どちらも `GetModuleFileNameW` から引くので、
/// 数え方を 1 か所にまとめてある。
///
/// @note 長いパスの変換がここに居るのは、`WinUtf.cpp` が core を参照できないため。
///       あちらは `kite_shellhost.exe` にもリンクされていて、そちらは
///       `kite_core` を持たない（CMakeLists.txt の該当箇所を参照）

#pragma once

#include <string>
#include <string_view>

namespace kite::win {

/// @brief 実行中の exe のフルパスを返す。
/// @return exe のフルパス。取得に失敗したら空文字列
/// @note argv[0] ではなく実際に走っているモジュールを見る。2 枚目のウィンドウが
///       必ず 1 枚目と同じビルドになるのはこのため
std::wstring ModuleFilePath();

/// @brief 実行中の exe が置かれているディレクトリを返す。
/// @return ディレクトリのパス。末尾に区切り文字は付かない。取得に失敗したら空文字列
std::wstring ModuleDirectory();

/// @brief 必要なら "\\\\?\\" を付けた UTF-16 パスを返す。
/// @param[in] utf8 変換元のパス（UTF-8）
/// @return Win32 API に渡せるワイドパス。260 文字制限を超える深い階層や、
///         名前の長いクラウドフォルダでも列挙できるようにする
/// @note 付けるかどうかの規則は `kite::path::ToExtended()`（core 側）が持つ。
///       ここは UTF-16 へ変換するだけ
std::wstring ToExtendedPath(std::string_view utf8);

/// @brief コマンドライン引数 1 つを、`CommandLineToArgvW` が元どおりに分解できる形で括る。
/// @param[in] arg 括る引数（UTF-16）
/// @return 前後を `"` で囲んだ文字列。閉じ引用符の直前のバックスラッシュだけ倍にする
/// @note フォルダのパスは `C:\\` のように区切りで終わることがあり、そのまま `"` で
///       閉じると引用符自身がエスケープされて残り全部が 1 つの引数になる
std::wstring QuoteArgument(std::wstring_view arg);

}  // namespace kite::win
