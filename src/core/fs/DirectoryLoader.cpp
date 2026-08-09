#include "core/fs/DirectoryLoader.h"

namespace kite::fs {

DirectoryLoader::DirectoryLoader(IFileSystem& fsys, IWakeSink& wake, int workers)
    : fs_(fsys), wake_(wake) {
    if (workers < 1) workers = 1;
    threads_.reserve(static_cast<size_t>(workers));
    for (int i = 0; i < workers; ++i) {
        threads_.emplace_back([this] { WorkerMain(); });
    }
}

DirectoryLoader::~DirectoryLoader() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
    }
    cv_.notify_all();
    for (std::thread& t : threads_) {
        if (t.joinable()) t.join();
    }
}

uint64_t DirectoryLoader::Request(const std::string& path) {
    const uint64_t token = nextToken_.fetch_add(1, std::memory_order_relaxed);
    pending_.fetch_add(1, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push_back({ token, path });
    }
    cv_.notify_one();
    return token;
}

void DirectoryLoader::Drain(std::vector<LoadedListing>& out) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (done_.empty()) return;
    for (LoadedListing& l : done_) out.push_back(std::move(l));
    done_.clear();
}

void DirectoryLoader::WorkerMain() {
    for (;;) {
        Job job;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return stop_ || !queue_.empty(); });
            if (stop_) return;
            job = std::move(queue_.front());
            queue_.pop_front();
        }

        LoadedListing listing;
        listing.token = job.token;
        listing.path = job.path;
        listing.result = fs_.List(job.path);

        {
            std::lock_guard<std::mutex> lock(mutex_);
            done_.push_back(std::move(listing));
        }
        pending_.fetch_sub(1, std::memory_order_relaxed);
        wake_.Wake();
    }
}

}  // namespace kite::fs
