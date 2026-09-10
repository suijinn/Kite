/// @file
/// @brief フォルダの木を歩く共通の仕掛け。
///
/// **再帰の列挙を 2 つ書かない。** 検索（`fs::SearchJob`）とフォルダのサイズ
/// （`fs::FolderSizeJob`）は集めるものが違うだけで、歩き方の約束はまったく同じ ─
/// リンクの先へは降りない、読めないフォルダは飛ばす、打ち切りが効くのはフォルダの
/// 切れ目、そして幅優先。2 本に分かれた時点で、輪への耐性や打ち切りの規則が
/// どちらかで古くなる。
///
/// ここが持つのは «歩くこと» だけで、集めるのは訪問子の仕事。1 フォルダぶんの
/// 列挙結果をそのまま渡すので、読めなかったフォルダの扱い（黙って飛ばす／数え漏れの
/// 印を立てる）も呼ぶ側が決められる。
///
/// @note ワーカースレッドから呼ばれる。`IFileSystem::List()` がブロックするので、
///       UI スレッドから呼んではならない

#pragma once

#include <functional>
#include <string>

#include "core/fs/FileSystem.h"

namespace kite::fs {

/// @brief 歩き続けてよいかを答える述語。
///
/// フォルダの切れ目ごとに訊かれる。false を返せばそこで歩きは終わる ─ `List()`
/// 1 回ぶんは戻ってくるまで止められないので、打ち切りが効く粒度はこれが上限。
using TreeWalkAlive = std::function<bool()>;

/// @brief 訪れた 1 フォルダぶんを受け取る訪問子。
///
/// @note `result.status` が `Status::Ok` でないこともある（アクセス拒否、消えた
///       フォルダ）。**歩きはそこへ降りないが、訪れたことは必ず伝える** ─ 黙って
///       飛ばすか、数え漏れとして印を立てるかは集める側の問題で、検索とサイズで
///       答えが違う
using TreeWalkVisit = std::function<bool(const std::string& dir, const ListResult& result)>;

/// @brief 木を幅優先で歩く。
/// @param[in,out] fsys 列挙に使うファイルシステム
/// @param[in] root 歩き始めるフォルダ
/// @param[in] alive 歩き続けてよいかを答える述語。フォルダの切れ目ごとに訊く
/// @param[in] visit 訪れたフォルダを受け取る訪問子。false を返せば歩きを終える
/// @note **幅優先。** 深さ優先だと、最初に踏み込んだ枝が深いだけで «今いる
///       フォルダのすぐ下» が最後まで出てこない ─ 検索なら «近くの当たり» が、
///       サイズなら «直下の大物» が後回しになる
/// @note **リンクの先へは降りない。** ジャンクションを辿ると
///       `C:\Users\All Users` のような輪に入り、歩きが終わらなくなる。
///       リンクそのものは訪問子に渡るので、«降りない» だけで «見えない» ではない
/// @note **読めなかったフォルダの先へは降りない。** 訪問子には渡す（上記）
void WalkTree(IFileSystem& fsys, const std::string& root, const TreeWalkAlive& alive,
              const TreeWalkVisit& visit);

}  // namespace kite::fs
