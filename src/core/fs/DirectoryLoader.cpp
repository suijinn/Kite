#include "core/fs/DirectoryLoader.h"

namespace kite::fs {

DirectoryLoader::DirectoryLoader(IFileSystem& fsys, IWakeSink& wake, int workers)
    : fs_(fsys), queue_(wake, workers, [this](const Job& job, const JobQueue<Job, LoadedListing>::Emit& emit) {
          LoadedListing listing;
          listing.token = job.token;
          listing.path = job.path;
          listing.result = fs_.List(job.path);
          emit(std::move(listing));
      }) {}

uint64_t DirectoryLoader::Request(const std::string& path) {
    // 採番はここ。JobQueue はトークンを配らない ─ 何で依頼を識別するかは
    // クラスごとに違う（サイズの «代»、検索の «今どれを歩いてよいか»）。
    const uint64_t token = nextToken_.fetch_add(1, std::memory_order_relaxed);
    queue_.Request(Job{ token, path });
    return token;
}

}  // namespace kite::fs
