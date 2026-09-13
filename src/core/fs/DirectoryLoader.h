/// @file
/// @brief バックグラウンドでのディレクトリ列挙。
///
/// 遅いルート（ネットワーク共有、冷えたクラウドフォルダ、スリープ中の USB）は
/// 1 回の FindFirstFile で数秒ブロックしうる。そのため列挙は UI スレッドで動かさず、
/// ここでキューに積み、ワーカーが IFileSystem を呼び、完了分を IHost::Wake() 後に
/// UI スレッドが回収する。
///
/// リクエストには単調増加のトークンが付く。結果が届く前にタブが別の場所へ移動して
/// いた場合、トークンが一致しないので古い結果は単純に破棄される。

#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include "core/fs/FileSystem.h"
#include "core/fs/JobQueue.h"

namespace kite::fs {

/// @brief 完了した 1 件分の列挙結果。
struct LoadedListing {
    uint64_t token = 0;   ///< 対応するリクエストのトークン
    std::string path;     ///< 列挙したディレクトリのパス
    ListResult result;    ///< 列挙結果
};

/// @brief ディレクトリ列挙を非同期に行うキュー。
class DirectoryLoader {
public:
    /// @brief 列挙キューを作る。**ワーカーは最初の依頼まで作らない。**
    /// @param[in] fsys 列挙に使うファイルシステム。本オブジェクトより長生きすること
    /// @param[in] wake 完了通知先。本オブジェクトより長生きすること
    /// @param[in] workers ワーカースレッド数。1 未満を渡した場合は 1 に丸める
    DirectoryLoader(IFileSystem& fsys, IWakeSink& wake, int workers = 2);

    DirectoryLoader(const DirectoryLoader&) = delete;
    DirectoryLoader& operator=(const DirectoryLoader&) = delete;

    /// @brief 列挙を依頼する。
    /// @param[in] path 列挙するディレクトリのパス
    /// @return このリクエストを識別するトークン
    uint64_t Request(const std::string& path);

    /// @brief 完了済みの結果をすべて取り出す。
    /// @param[out] out 取り出した結果の追加先。既存の要素は保持される
    /// @note UI スレッドからのみ呼ぶこと
    void Drain(std::vector<LoadedListing>& out) { queue_.Drain(out); }

    /// @brief 未完了のリクエストがあるかを返す。
    /// @return 処理待ちまたは処理中のものがあれば true
    bool busy() const { return queue_.busy(); }

private:
    struct Job {
        uint64_t token = 0;
        std::string path;
    };

    IFileSystem& fs_;
    std::atomic<uint64_t> nextToken_{ 1 };
    JobQueue<Job, LoadedListing> queue_;
};

}  // namespace kite::fs
