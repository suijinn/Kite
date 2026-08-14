/// @file
/// @brief 設定ファイルを置くディレクトリの選択。
///
/// ファイルシステムには一切触れない。候補の並びと「実在するか」だけを受け取って
/// 1 つ選ぶので、置き場所の規則が OS 実装の中に埋もれず単体テストできる。
/// 実際に候補を組み立てるのは `IFileSystem::ConfigDir()` の実装側。

#pragma once

#include <string>
#include <vector>

namespace kite::config {

/// @brief 設定ディレクトリの候補 1 つ。
struct Candidate {
    std::string dir;      ///< 候補のディレクトリ。空文字列は候補として扱わない
    bool exists = false;  ///< 実在するか。呼び出し側が調べて渡す
};

/// @brief 候補の中から実際に使う設定ディレクトリを 1 つ選ぶ。
/// @param[in] candidates 優先度の高い順に並べた候補
/// @return 選ばれたディレクトリ。空でない候補が 1 つも無ければ空文字列
/// @note 「実在する最初の候補、1 つも無ければ空でない最後の候補」。実在を優先
///       するのは、置いたという事実がそのまま利用者の意思表示だから。無いときに
///       末尾を選ぶのは、そこが新規に作る場所になるため ─ 先頭を作ってしまうと、
///       ポータブル運用を頼んでいない人の exe の隣にも設定フォルダが生える
std::string Choose(const std::vector<Candidate>& candidates);

}  // namespace kite::config
