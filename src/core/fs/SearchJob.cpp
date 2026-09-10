#include "core/fs/SearchJob.h"

#include <utility>

#include "core/base/Platform.h"
#include "core/base/Utf8.h"
#include "core/fs/TreeWalk.h"

namespace kite::fs {

SearchJob::SearchJob(IFileSystem& fsys, IWakeSink& wake) : fs_(fsys), wake_(wake) {
    thread_ = std::thread([this] { WorkerMain(); });
}

SearchJob::~SearchJob() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
    }
    // 歩いている最中なら、次のフォルダの切れ目で自分から抜ける。
    active_.store(0, std::memory_order_relaxed);
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
}

uint64_t SearchJob::Start(const std::string& root, const std::string& query) {
    if (query.empty() || root.empty()) {
        // 空の問いに答えは無い。走っているものは畳む ─ 打った文字を全部消した
        // 人の前に、消す前の答えが残り続けるのはただの嘘。
        active_.store(0, std::memory_order_relaxed);
        return 0;
    }
    const uint64_t token = nextToken_.fetch_add(1, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_ = Job{ token, root, utf8::ToLowerAscii(query) };
        hasPending_ = true;
    }
    // 先に立てる ─ ワーカーが走り出す前にこれを見て、前の歩きが自分から抜ける。
    active_.store(token, std::memory_order_relaxed);
    cv_.notify_one();
    return token;
}

void SearchJob::Cancel(uint64_t token) {
    if (token == 0) return;
    uint64_t expected = token;
    active_.compare_exchange_strong(expected, 0, std::memory_order_relaxed);
}

void SearchJob::Drain(std::vector<SearchBatch>& out) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (done_.empty()) return;
    for (SearchBatch& b : done_) out.push_back(std::move(b));
    done_.clear();
}

void SearchJob::Publish(uint64_t token, std::vector<Entry>& batch, bool done, bool truncated) {
    if (batch.empty() && !done) return;
    SearchBatch out;
    out.token = token;
    out.entries.swap(batch);
    out.done = done;
    out.truncated = truncated;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        done_.push_back(std::move(out));
    }
    wake_.Wake();
}

void SearchJob::Walk(const Job& job) {
    // 歩き方そのものは `fs::WalkTree` が持つ ─ 幅優先、リンクの先へは降りない、
    // 読めないフォルダは飛ばす、打ち切りはフォルダの切れ目。ここが足すのは
    // «名前に当たったら集める» という訪問子だけで、フォルダのサイズを数える側
    // （`fs::FolderSizeJob`）も同じ歩きの上に乗っている。
    std::vector<Entry> batch;
    size_t found = 0;
    uint64_t lastFlush = plat::NowMs();
    bool truncated = false;

    const auto alive = [&] { return active_.load(std::memory_order_relaxed) == job.token; };

    WalkTree(fs_, job.root, alive, [&](const std::string& dir, const ListResult& result) {
        // 読めなかったフォルダは黙って飛ばす。1 つのアクセス拒否で検索そのものを
        // 失敗にすると、`C:\` からの検索は `System Volume Information` に当たった
        // 時点で必ず終わる ─ 探している人が頼んだのは «読めるところを全部» である。
        if (result.status != Status::Ok) return true;

        for (const Entry& e : result.entries) {
            if (utf8::ToLowerAscii(e.name).find(job.needle) == std::string::npos) continue;
            Entry hit = e;
            // 名前を親のパスに繋いでも指せない ─ 当たった項目は今いるフォルダの
            // 直下とは限らないので、`address` に «その項目自身のパス» を入れる
            // （仮想フォルダの項目と同じ手で、`fs::EntryPath()` がそのまま答える）。
            hit.address = EntryPath(dir, e);
            batch.push_back(std::move(hit));
            if (++found >= kSearchMaxResults) {
                truncated = true;
                return false;
            }
        }

        // 隠し属性で刈らない ─ 検索が答えているのは «在るか無いか» で、隠すか
        // どうかは一覧の読み方（`Tab::Rebuild` が `view.showHidden` で決める）。
        // ここで落とすと、`Ctrl+H` を押しても歩き直すまで出てこない。

        const uint64_t now = plat::NowMs();
        if (batch.size() >= kSearchBatch || now - lastFlush >= kSearchFlushMs) {
            lastFlush = now;
            Publish(job.token, batch, false, false);
        }
        return true;
    });

    if (truncated) {
        Publish(job.token, batch, true, true);
        return;
    }
    if (!alive()) return;
    Publish(job.token, batch, true, false);
}

void SearchJob::WorkerMain() {
    for (;;) {
        Job job;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return stop_ || hasPending_; });
            if (stop_) return;
            job = std::move(pending_);
            hasPending_ = false;
        }
        running_.store(true, std::memory_order_relaxed);
        Walk(job);
        running_.store(false, std::memory_order_relaxed);
    }
}

}  // namespace kite::fs
