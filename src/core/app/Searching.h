/// @file
/// @brief 現フォルダ以下を歩いて名前で探す機能の判断。
///
/// **`Ctrl+F` の絞り込みとは別の機能。** あちらは今の一覧から行を減らすだけで、
/// こちらは **別の一覧を作る** ─ 並ぶ項目はどのサブフォルダのものでもよく、
/// `fs::Entry::address` が自分自身のパスを持つ（仮想フォルダと同じ手）。
///
/// 打ち込まれた問いは `Tab::filter` が持ち、`Tab::Rebuild()` がそのまま絞り込みに
/// 使う ─ 検索のためだけの絞り込みをもう 1 つ書かない。ワーカーに渡した «ふるい»
/// （`Tab::search.sieve`）とは普通ずれていて、そのずれこそが「前へ打ち足している
/// 間は歩き直さない」という約束の中身になる（`fs::SearchJob` の冒頭）。
///
/// **`App` を持たない。** 受け取るのはファイルシステムと表示文字列だけで、
/// «どのタブが今その答えを待っているか» は `Pump()` に渡される `Workspace` が答える。

#pragma once

#include <memory>
#include <string>

#include "core/fs/FileSystem.h"
#include "core/fs/SearchJob.h"
#include "core/i18n/Strings.h"
#include "core/model/Workspace.h"

namespace kite {

/// @brief 再帰検索の開始・打ち切り・取り込みをまとめたもの。
class Searching {
public:
    /// @brief 依存を結び付けて構築する。**ワーカーはまだ作らない。**
    /// @param[in] fsys 歩きに使うファイルシステム。本オブジェクトより長生きすること
    /// @param[in] strings ステータス行の文言を引く表。同上
    Searching(fs::IFileSystem& fsys, const Strings& strings);

    ~Searching();

    Searching(const Searching&) = delete;
    Searching& operator=(const Searching&) = delete;

    /// @brief ワーカーを用意する。
    /// @param[in] wake 途中経過の通知先。本オブジェクトより長生きすること
    void Start(fs::IWakeSink& wake);

    /// @brief ワーカーを畳む。
    void Shutdown();

    /// @brief 検索を始める。走っているものがあれば打ち切る。
    /// @param[in,out] tab 検索結果を出すタブ
    /// @param[in] query 探す文字列
    /// @note **開いた瞬間に一覧は空になる。** フォルダの中身を残したまま検索欄を
    ///       出すと、最初の 1 打鍵がファイルを消したように見える ─ 検索欄の下に
    ///       並ぶ行は検索結果でなければならない
    /// @note 走っている列挙のトークンを落とす。残すと、後から届いたフォルダの
    ///       一覧が集めたばかりの検索結果を黙って上書きする
    void Begin(Tab& tab, const std::string& query);

    /// @brief 検索をやめてフォルダの一覧に戻す準備をする。
    /// @param[in,out] tab 対象のタブ
    /// @note 問いも一緒に捨てる ─ 検索を抜けた先の一覧はこのフォルダの中身で、
    ///       そこに検索語が絞り込みとして残っているとフォルダが空に見える
    /// @note 一覧の取り直しは頼まない。呼ぶ側の «元に戻す» の一部
    void Cancel(Tab& tab);

    /// @brief 打ち込まれた問いを反映する。必要なら歩き直す。
    /// @param[in,out] tab 対象のタブ
    /// @param[in] query 今の問い
    /// @return 歩き直さずに済んだら true（呼ぶ側はカーソルを画面内へ引き戻す）
    /// @note ふるいが今の問いの部分文字列である限り、答えはすべてもう手元にある ─
    ///       前へ打ち足している間は歩き直さない。縮めたときだけ歩き直す
    bool SyncQuery(Tab& tab, const std::string& query);

    /// @brief 届いた途中経過を取り込む。
    /// @param[in,out] workspace 当たりの行き先を持つワークスペース
    /// @return 1 つでも取り込んだら true（呼ぶ側はカーソルを画面内へ引き戻す）
    /// @note 行き先の無くなった答え ─ タブが閉じた、背面に回って一覧を手放した ─
    ///       を見つけたら、そこで歩きも打ち切る
    bool Pump(Workspace& workspace);

    /// @brief 検索の進み具合を伝える文字列を返す。
    /// @param[in] tab フォーカスされているタブ
    /// @return ステータス行の右に出す文字列。言うことが無ければ空
    std::string status(const Tab& tab) const;

private:
    fs::IFileSystem& fs_;
    const Strings& strings_;
    std::unique_ptr<fs::SearchJob> job_;
};

}  // namespace kite
