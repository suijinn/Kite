#include "core/fs/FolderSize.h"

#include <algorithm>
#include <utility>

#include "core/base/PathUtil.h"
#include "core/base/Platform.h"
#include "core/fs/TreeWalk.h"

namespace kite::fs {

// ---------------------------------------------------------------------------
// FolderSizeCache
// ---------------------------------------------------------------------------

FolderSize FolderSizeCache::Get(const std::string& path) const {
    auto it = entries_.find(path);
    if (it == entries_.end()) return {};
    return it->second.value;
}

bool FolderSizeCache::Request(const std::string& path, bool force) {
    if (path.empty()) return false;

    Entry_& entry = entries_[path];
    entry.used = ++tick_;
    // 歩いている最中のものは、頼まれても二重には歩かない。
    if (entry.value.state == SizeState::Counting) return false;
    // 答えが出ているもの・やめたものは、自分からは数え直さない。ここが毎フレームの
    // 描画から呼ばれるので、この 1 行が «見るたびに歩き直す» を止めている。
    if (!force && entry.value.state != SizeState::Unknown) return false;

    entry.value = FolderSize{};
    entry.value.state = SizeState::Counting;
    ++counting_;
    EvictIfNeeded();
    return true;
}

bool FolderSizeCache::Apply(const FolderSizeUpdate& update) {
    auto it = entries_.find(update.path);
    if (it == entries_.end()) return false;
    // 忘れられた後に届いた答え、または誰も頼んでいない答え。書き戻すと、捨てた
    // はずの値が «数え終わった» 顔で戻ってくる。
    if (it->second.value.state != SizeState::Counting) return false;

    FolderSize& value = it->second.value;
    value.bytes = update.bytes;
    value.files = update.files;
    value.dirs = update.dirs;
    value.incomplete = update.incomplete;
    if (update.done) {
        value.state = update.failed ? SizeState::Failed : SizeState::Done;
        value.countedAtMs = update.atMs;
        --counting_;
    }
    return true;
}

void FolderSizeCache::ResetInFlight() {
    for (auto& [path, entry] : entries_) {
        if (entry.value.state != SizeState::Counting) continue;
        // 途中まで数えた値は捨てるが、印は «やめた» で残す ─ «まだ» に戻すと、
        // 自動で数える設定では次の 1 フレームが即座に頼み直す。
        entry.value = FolderSize{};
        entry.value.state = SizeState::Stopped;
    }
    counting_ = 0;
}

void FolderSizeCache::ForgetRelated(const std::string& path) {
    Forget(path, 0, 0);
}

void FolderSizeCache::ForgetChanged(const std::string& path, uint64_t nowMs,
                                    uint64_t keepNewerThanMs) {
    Forget(path, nowMs, keepNewerThanMs);
}

void FolderSizeCache::Forget(const std::string& path, uint64_t nowMs, uint64_t keepNewerThanMs) {
    if (path.empty()) return;
    for (auto it = entries_.begin(); it != entries_.end();) {
        const std::string& known = it->first;
        const bool related = known == path || kite::path::IsInside(known, path) ||
                             kite::path::IsInside(path, known);
        if (!related) {
            ++it;
            continue;
        }
        // 数えたばかりの値は残す ─ 書き込み続けるフォルダを開いているだけで、
        // その下の木を何度も歩き直すことになる（`kFolderSizeRecountMs`）。
        const FolderSize& value = it->second.value;
        if (keepNewerThanMs > 0 && value.state == SizeState::Done && value.countedAtMs > 0 &&
            nowMs >= value.countedAtMs && nowMs - value.countedAtMs < keepNewerThanMs) {
            ++it;
            continue;
        }
        if (value.state == SizeState::Counting) --counting_;
        it = entries_.erase(it);
    }
}

void FolderSizeCache::Clear() {
    entries_.clear();
    counting_ = 0;
}

void FolderSizeCache::SetCapacity(size_t capacity) {
    capacity_ = std::max<size_t>(1, capacity);
    EvictIfNeeded();
}

void FolderSizeCache::EvictIfNeeded() {
    while (entries_.size() > capacity_) {
        auto oldest = entries_.end();
        for (auto it = entries_.begin(); it != entries_.end(); ++it) {
            // 数え中のものは捨てない ─ 捨てた瞬間にその歩きの行き先が消え、
            // 届いた答えは誰にも取り込まれないまま消える。
            if (it->second.value.state == SizeState::Counting) continue;
            if (oldest == entries_.end() || it->second.used < oldest->second.used) oldest = it;
        }
        if (oldest == entries_.end()) return;  // 全部が数え中。上限より歩きを優先する
        entries_.erase(oldest);
    }
}

// ---------------------------------------------------------------------------
// FolderSizeJob
// ---------------------------------------------------------------------------

FolderSizeJob::FolderSizeJob(IFileSystem& fsys, IWakeSink& wake, int workers)
    : fs_(fsys), wake_(wake) {
    const int count = std::max(1, workers);
    threads_.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) threads_.emplace_back([this] { WorkerMain(); });
}

FolderSizeJob::~FolderSizeJob() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
        queue_.clear();
    }
    // 歩いている最中なら、次のフォルダの切れ目で自分から抜ける ─ 数えるだけで
    // 何も書き換えないので、待つ理由が無い（検索と同じ）。
    epoch_.fetch_add(1, std::memory_order_relaxed);
    cv_.notify_all();
    for (std::thread& t : threads_) {
        if (t.joinable()) t.join();
    }
}

void FolderSizeJob::Request(const std::string& path) {
    if (path.empty()) return;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (std::find(claimed_.begin(), claimed_.end(), path) != claimed_.end()) return;
        claimed_.push_back(path);
        queue_.push_back(Job{ epoch_.load(std::memory_order_relaxed), path });
    }
    pending_.fetch_add(1, std::memory_order_relaxed);
    cv_.notify_one();
}

void FolderSizeJob::CancelAll() {
    int dropped = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        dropped = static_cast<int>(queue_.size());
        queue_.clear();
        // 歩いている最中のものは自分で抜ける。その «場所» の予約だけ先に外して
        // おくと、やめた直後の描画がもう一度頼めるようになる。
        claimed_.clear();
    }
    // 先に増やす ─ 走っているワーカーはフォルダの切れ目でこれを見て抜ける。
    epoch_.fetch_add(1, std::memory_order_relaxed);
    if (dropped > 0) pending_.fetch_sub(dropped, std::memory_order_relaxed);
}

void FolderSizeJob::Drain(std::vector<FolderSizeUpdate>& out) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (done_.empty()) return;
    for (FolderSizeUpdate& u : done_) out.push_back(std::move(u));
    done_.clear();
}

void FolderSizeJob::Publish(FolderSizeUpdate update) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        done_.push_back(std::move(update));
    }
    wake_.Wake();
}

void FolderSizeJob::Count(const Job& job) {
    FolderSizeUpdate acc;
    acc.epoch = job.epoch;
    acc.path = job.path;

    bool rootUnreadable = false;
    bool first = true;
    uint64_t lastFlush = plat::NowMs();

    const auto alive = [&] { return epoch_.load(std::memory_order_relaxed) == job.epoch; };

    WalkTree(fs_, job.path, alive, [&](const std::string&, const ListResult& result) {
        if (result.status != Status::Ok) {
            // 読めなかった枝は飛ばす（検索と同じ）が、**飛ばしたことは覚える** ─
            // 検索の答えは «見つかったもの» なので黙って飛ばしてよいが、合計は
            // 飛ばした瞬間に間違う。それが «少なくともこれだけ» であることは
            // ステータス行が言う。
            if (first) rootUnreadable = true;
            acc.incomplete = true;
            first = false;
            return true;
        }
        first = false;

        for (const Entry& e : result.entries) {
            if (e.isDir()) {
                ++acc.dirs;
                // リンクの先へは降りないので、その中身は数えない（`WalkTree`）。
                continue;
            }
            ++acc.files;
            acc.bytes += e.size;
        }

        const uint64_t now = plat::NowMs();
        if (now - lastFlush >= kFolderSizeFlushMs) {
            lastFlush = now;
            Publish(acc);
        }
        return true;
    });

    // 途中で畳まれた歩きは «確定» を出さない ─ 出せば、止めたはずの値が
    // 数え終わった顔で表に残る（`epoch` で弾かれるが、そもそも送らない）。
    if (!alive()) return;

    acc.done = true;
    acc.atMs = plat::NowMs();
    acc.failed = rootUnreadable;
    Publish(std::move(acc));
}

void FolderSizeJob::WorkerMain() {
    for (;;) {
        Job job;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return stop_ || !queue_.empty(); });
            if (stop_) return;
            job = std::move(queue_.front());
            queue_.pop_front();
        }

        Count(job);

        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = std::find(claimed_.begin(), claimed_.end(), job.path);
            if (it != claimed_.end()) claimed_.erase(it);
        }
        pending_.fetch_sub(1, std::memory_order_relaxed);
    }
}

}  // namespace kite::fs
