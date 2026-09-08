/// @file
/// @brief 現フォルダ以下を歩いて名前で探す、バックグラウンドの再帰検索。
///
/// 列挙を UI スレッドから追い出したのと同じ話が、そのまま当てはまる ─ 深い木を
/// 1 本歩けば `List()` の待ち時間が何百回ぶんも積み上がる。ここに積み、ワーカーが
/// `IFileSystem` を呼び、見つかったぶんを `IHost::Wake()` の後に UI スレッドが
/// 回収する（`DirectoryLoader` と同じ作り）。
///
/// **Windows Search のインデックスは使わない。** インデックスが張られていない
/// フォルダ ─ 外付けディスク、ネットワーク共有、除外されたフォルダ ─ で黙って
/// 0 件を返す検索は、無い機能より悪い。自前で歩けばどこでも同じ答えが出る。
///
/// **答えは «ふるい» で刈ってから返す。** ワーカーは `Start()` に渡された問いに
/// 当たった項目だけを持ち帰るので、常駐するのは «見つかったもの» だけで済む
/// （全項目を持ち帰って UI 側で絞ると、`C:\` の検索がメモリを食い尽くす）。
/// 打ち足したぶんの絞り込みは、すでに `Tab::filter` が行っている ─ **問いが
/// 伸びる間はふるいを張り替えなくてよい**（`q` に当たるものは `qu` にも必ず
/// 当たる）ので、前へ打っている限り歩き直しは起きない。縮めたときだけ歩き直す。
///
/// **一度に 1 本しか歩かない。** 2 本走らせても同じディスクを取り合うだけで、
/// しかも 2 本目の結果は誰も見ていない一覧へ届く。`Start()` は前の歩きを
/// 打ち切ってから始める。

#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/fs/DirectoryLoader.h"
#include "core/fs/FileSystem.h"

namespace kite::fs {

/// @brief 1 回の検索で持ち帰る項目数の上限。
///
/// 上限が無いと `C:\` から "e" を探した瞬間に常駐メモリが青天井になる。読める
/// 量をはるかに超えているので、ここに当たったら問いのほうが広すぎる ─ 打ち切った
/// ことは画面が言う（`ui.search_truncated`）。
constexpr size_t kSearchMaxResults = 5000;

/// @brief 1 回の通知でまとめて返す項目数。
///
/// 1 件ごとに起こすと、当たりの多いフォルダで再描画がそのまま歩きの足を引っ張る。
/// 逆に貯めすぎると «増えていく» が見えなくなるので、時間切れ
/// （`kSearchFlushMs`）と早いほうで吐き出す。
constexpr size_t kSearchBatch = 64;

/// @brief 当たりが溜まらなくても吐き出すまでの時間（ミリ秒）。
///
/// 当たりの少ない木では `kSearchBatch` に一生届かない ─ 見つかった 1 件が
/// 歩き終わるまで画面に出ないのでは、探している側からは止まって見える。
constexpr uint64_t kSearchFlushMs = 120;

/// @brief 検索の途中経過 1 回分。
struct SearchBatch {
    uint64_t token = 0;          ///< 対応する検索のトークン
    std::vector<Entry> entries;  ///< 見つかった項目。address にフルパスが入る
    bool done = false;           ///< これで歩き終わり
    bool truncated = false;      ///< kSearchMaxResults に達して打ち切った
};

/// @brief 再帰検索を非同期に行うワーカー。
class SearchJob {
public:
    /// @brief ワーカースレッドを起動する。
    /// @param[in] fsys 列挙に使うファイルシステム。本オブジェクトより長生きすること
    /// @param[in] wake 完了通知先。本オブジェクトより長生きすること
    SearchJob(IFileSystem& fsys, IWakeSink& wake);

    /// @brief ワーカースレッドの停止を待って破棄する。
    ~SearchJob();

    SearchJob(const SearchJob&) = delete;
    SearchJob& operator=(const SearchJob&) = delete;

    /// @brief 検索を始める。走っているものがあれば打ち切る。
    /// @param[in] root 歩き始めるフォルダ
    /// @param[in] query 探す文字列。名前に部分一致するものを当たりとする
    /// @return この検索を識別するトークン。`query` が空なら 0（何も始めない）
    /// @note 大文字小文字は問わない（ASCII の範囲で畳む。`Tab::filter` と同じ）
    uint64_t Start(const std::string& root, const std::string& query);

    /// @brief 走っている検索を打ち切る。
    /// @param[in] token 打ち切る検索のトークン。今走っているものと違えば何もしない
    /// @note 打ち切りが効くのはフォルダの切れ目 ─ `List()` 1 回ぶんは戻ってくる
    ///       まで止められない
    void Cancel(uint64_t token);

    /// @brief 届いている途中経過をすべて取り出す。
    /// @param[out] out 取り出した結果の追加先。既存の要素は保持される
    /// @note UI スレッドからのみ呼ぶこと
    void Drain(std::vector<SearchBatch>& out);

    /// @brief まだ歩いている最中かを返す。
    /// @return 歩いていれば true
    bool busy() const { return running_.load(std::memory_order_relaxed); }

private:
    struct Job {
        uint64_t token = 0;
        std::string root;
        std::string needle;  // 小文字に畳んだ問い
    };

    void WorkerMain();
    void Walk(const Job& job);
    void Publish(uint64_t token, std::vector<Entry>& batch, bool done, bool truncated);

    IFileSystem& fs_;
    IWakeSink& wake_;

    std::mutex mutex_;
    std::condition_variable cv_;
    Job pending_;
    bool hasPending_ = false;
    bool stop_ = false;
    std::vector<SearchBatch> done_;

    // 「今どれを歩いてよいか」。ワーカーはフォルダの切れ目ごとにこれと自分の
    // トークンを比べる ─ 食い違っていれば、その歩きはもう誰も見ていない。
    std::atomic<uint64_t> active_{ 0 };
    std::atomic<uint64_t> nextToken_{ 1 };
    std::atomic<bool> running_{ false };
    std::thread thread_;
};

}  // namespace kite::fs
