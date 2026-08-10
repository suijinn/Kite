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
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/fs/FileSystem.h"

namespace kite::fs {

/// @brief ワーカーから UI スレッドを起こすための通知先。
class IWakeSink {
public:
    virtual ~IWakeSink() = default;

    /// @brief UI スレッドに処理待ちがあることを伝える。
    /// @note ワーカースレッドから呼ばれる。スレッド安全に実装すること
    virtual void Wake() = 0;
};

/// @brief 完了した 1 件分の列挙結果。
struct LoadedListing {
    uint64_t token = 0;   ///< 対応するリクエストのトークン
    std::string path;     ///< 列挙したディレクトリのパス
    ListResult result;    ///< 列挙結果
};

/// @brief ディレクトリ列挙を非同期に行うキュー。
class DirectoryLoader {
public:
    /// @brief ワーカースレッドを起動する。
    /// @param[in] fsys 列挙に使うファイルシステム。本オブジェクトより長生きすること
    /// @param[in] wake 完了通知先。本オブジェクトより長生きすること
    /// @param[in] workers ワーカースレッド数。1 未満を渡した場合は 1 に丸める
    DirectoryLoader(IFileSystem& fsys, IWakeSink& wake, int workers = 2);

    /// @brief ワーカースレッドの停止を待って破棄する。
    ~DirectoryLoader();

    DirectoryLoader(const DirectoryLoader&) = delete;
    DirectoryLoader& operator=(const DirectoryLoader&) = delete;

    /// @brief 列挙を依頼する。
    /// @param[in] path 列挙するディレクトリのパス
    /// @return このリクエストを識別するトークン
    uint64_t Request(const std::string& path);

    /// @brief 完了済みの結果をすべて取り出す。
    /// @param[out] out 取り出した結果の追加先。既存の要素は保持される
    /// @note UI スレッドからのみ呼ぶこと
    void Drain(std::vector<LoadedListing>& out);

    /// @brief 未完了のリクエストがあるかを返す。
    /// @return 処理待ちまたは処理中のものがあれば true
    bool busy() const { return pending_.load(std::memory_order_relaxed) > 0; }

private:
    struct Job {
        uint64_t token;
        std::string path;
    };

    void WorkerMain();

    IFileSystem& fs_;
    IWakeSink& wake_;

    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<Job> queue_;
    std::vector<LoadedListing> done_;
    bool stop_ = false;

    std::atomic<uint64_t> nextToken_{ 1 };
    std::atomic<int> pending_{ 0 };
    std::vector<std::thread> threads_;
};

}  // namespace kite::fs
