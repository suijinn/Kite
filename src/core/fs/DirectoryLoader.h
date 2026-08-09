// Kite - background directory enumeration.
//
// A slow root (network share, cold cloud folder, sleeping USB disk) can block
// for seconds inside a single FindFirstFile call. Enumeration therefore never
// runs on the UI thread: requests are queued here, workers call IFileSystem,
// and finished listings are picked up by the UI thread after Host::Wake().
//
// Each request carries a monotonically increasing token. When a tab navigates
// away before its listing lands, the stale result is simply dropped because its
// token no longer matches the tab's current one.
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

class IWakeSink {
public:
    virtual ~IWakeSink() = default;
    // Called from a worker thread; must be safe to invoke cross-thread.
    virtual void Wake() = 0;
};

struct LoadedListing {
    uint64_t token = 0;
    std::string path;
    ListResult result;
};

class DirectoryLoader {
public:
    DirectoryLoader(IFileSystem& fsys, IWakeSink& wake, int workers = 2);
    ~DirectoryLoader();

    DirectoryLoader(const DirectoryLoader&) = delete;
    DirectoryLoader& operator=(const DirectoryLoader&) = delete;

    // Returns the token identifying this request.
    uint64_t Request(const std::string& path);

    // Moves every completed listing to the caller. UI thread only.
    void Drain(std::vector<LoadedListing>& out);

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
