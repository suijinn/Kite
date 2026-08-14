/// @file
/// @brief 実行中の exe 自身の位置。
///
/// Kite は自分の隣にあるものを 2 つ当てにしている ─ `kite_shellhost.exe` と、
/// ポータブル運用時の `config` フォルダ。どちらも `GetModuleFileNameW` から引くので、
/// 数え方を 1 か所にまとめてある。

#pragma once

#include <string>

namespace kite::win {

/// @brief 実行中の exe のフルパスを返す。
/// @return exe のフルパス。取得に失敗したら空文字列
/// @note argv[0] ではなく実際に走っているモジュールを見る。2 枚目のウィンドウが
///       必ず 1 枚目と同じビルドになるのはこのため
std::wstring ModuleFilePath();

/// @brief 実行中の exe が置かれているディレクトリを返す。
/// @return ディレクトリのパス。末尾に区切り文字は付かない。取得に失敗したら空文字列
std::wstring ModuleDirectory();

}  // namespace kite::win
