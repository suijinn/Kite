/// @file
/// @brief 各プラットフォームが必ず実装する自由関数群。
///
/// core 層が OS に触れてよいのはここだけ。これ以外の OS 境界は
/// core/fs/FileSystem.h、core/app/Host.h、ui/Renderer.h のインターフェース経由。

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace kite::plat {

/// @brief テキストファイルを丸ごと読み込む。
/// @param[in] utf8Path 読み込むファイルのパス（UTF-8）
/// @param[out] out 読み込んだ内容。失敗時は空にされる
/// @return 成功したら true。存在しない・開けない・巨大すぎる場合は false
bool ReadTextFile(const std::string& utf8Path, std::string& out);

/// @brief テキストファイルを新規作成または上書きする。
/// @param[in] utf8Path 書き込むファイルのパス（UTF-8）
/// @param[in] data 書き込む内容
/// @return 成功したら true
bool WriteTextFile(const std::string& utf8Path, std::string_view data);

/// @brief ディレクトリを（必要なら親ごと）作成する。
/// @param[in] utf8Path 作成するディレクトリのパス（UTF-8）
/// @return 成功、またはすでに存在していれば true
bool EnsureDirectory(const std::string& utf8Path);

/// @brief 単調増加する経過時間を返す。
/// @return 任意の固定原点からのミリ秒。壁時計ではないので日時計算には使えない
uint64_t NowMs();

/// @brief 現在時刻を Unix 秒で返す。
/// @return 1970-01-01 UTC からの秒数。取れなければ 0
/// @note NowMs() とは別物。あちらは原点の無い単調増加で、日時の計算には使えない ─
///       «どれだけ前に更新されたか» のように、ファイルの持つ時刻と引き算できる値が
///       要る場所はこちらを呼ぶ
int64_t NowUnixSeconds();

/// @brief ユーザーの UI 言語を返す。
/// @return "ja" や "en" のような言語コード。判定できない場合は "en"
std::string PreferredLanguage();

}  // namespace kite::plat
