#include "core/fs/FileOpQueue.h"

#include "core/base/PathUtil.h"
#include "core/base/Utf8.h"

namespace kite::fs {

namespace {

// Whether each source already has a namesake in the destination, in the order
// the sources were given. Taken before a transfer so the same question can be
// asked again afterwards: the shell resolves a collision either by renaming its
// copy or by overwriting, and neither outcome is visible from the return value.
std::vector<bool> DestinationsExist(IFileSystem& fsys, const std::vector<std::string>& sources,
                                    const std::string& destDir) {
    std::vector<bool> out;
    out.reserve(sources.size());
    for (const std::string& s : sources) {
        out.push_back(fsys.Exists(path::Join(destDir, path::FileName(s))));
    }
    return out;
}

// One place a request touches, normalized, if it is not already down. The scan
// is linear because the list is short by construction (kFileOpTouchLimit), and
// the common case - many items sharing one parent - matches on the first entry.
// A place already down as read-only becomes a write when it turns up again as
// one: the stronger claim is the true one.
void AddTouch(std::vector<FileOpTouch>& out, const std::string& p, bool write) {
    if (p.empty()) return;
    std::string normalized = path::Normalize(p);
    if (normalized.empty()) return;
    for (FileOpTouch& have : out) {
        if (!utf8::EqualsIgnoreCaseAscii(have.path, normalized)) continue;
        if (write) have.write = true;
        return;
    }
    out.push_back({ std::move(normalized), write });
}

// The items themselves, unless there are so many of them that comparing one by
// one stops being worth it - then the folders they sit in stand for them.
void AddItems(std::vector<FileOpTouch>& out, const std::vector<std::string>& items, bool write) {
    if (items.size() > kFileOpTouchLimit) {
        for (const std::string& p : items) AddTouch(out, path::Parent(p), write);
        return;
    }
    for (const std::string& p : items) AddTouch(out, p, write);
}

}  // namespace

std::vector<FileOpTouch> FileOpTouches(const FileOpRequest& request) {
    std::vector<FileOpTouch> out;
    // A delete empties them; a duplicate only reads them and writes elsewhere.
    AddItems(out, request.paths, request.kind != FileOpKind::Duplicate);
    // The names a duplicate picked. Those are what nothing else may reach for
    // while it is deciding they are free.
    AddItems(out, request.destPaths, true);

    for (const FileOpGroup& group : request.groups) {
        // A copy only reads its sources - one file copied to two places twice
        // over is not a tug of war. A move empties them, so it is a write.
        AddItems(out, group.sources, request.move);

        // The destination is named item by item rather than as a folder. Two
        // copies into one folder collide only if they land on the same name,
        // and claiming the folder itself would make every copy to a USB stick
        // wait for every other one - the shape this was reported in.
        if (group.sources.size() > kFileOpTouchLimit) {
            AddTouch(out, group.destDir, true);
            continue;
        }
        for (const std::string& src : group.sources) {
            AddTouch(out, path::Join(group.destDir, path::FileName(src)), true);
        }
    }
    return out;
}

bool FileOpsConflict(const std::vector<FileOpTouch>& a, const std::vector<FileOpTouch>& b) {
    for (const FileOpTouch& x : a) {
        for (const FileOpTouch& y : b) {
            // Two readers of the same file are not in each other's way.
            if (!x.write && !y.write) continue;
            if (utf8::EqualsIgnoreCaseAscii(x.path, y.path)) return true;
            if (path::IsInside(x.path, y.path) || path::IsInside(y.path, x.path)) return true;
        }
    }
    return false;
}

FileOpQueue::FileOpQueue(IFileSystem& fsys, IWakeSink& wake, int workers)
    : fs_(fsys), wake_(wake) {
    if (workers < 1) workers = 1;
    threads_.reserve(static_cast<size_t>(workers));
    for (int i = 0; i < workers; ++i) {
        threads_.emplace_back([this] { WorkerMain(); });
    }
}

FileOpQueue::~FileOpQueue() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
        // Only what has not been started. The ones in flight are the shell's
        // now, and there is no asking for them back.
        queue_.clear();
    }
    cv_.notify_all();
    for (std::thread& t : threads_) {
        if (t.joinable()) t.join();
    }
}

uint64_t FileOpQueue::Request(FileOpRequest request) {
    const uint64_t token = nextToken_.fetch_add(1, std::memory_order_relaxed);
    // Worked out here rather than on the worker: the answer decides whether the
    // job may start at all, so it has to exist before it goes in the queue.
    std::vector<FileOpTouch> touches = FileOpTouches(request);
    pending_.fetch_add(1, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push_back({ token, std::move(request), std::move(touches) });
    }
    cv_.notify_one();
    return token;
}

void FileOpQueue::Drain(std::vector<FileOpDone>& out) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (done_.empty()) return;
    for (FileOpDone& d : done_) out.push_back(std::move(d));
    done_.clear();
}

size_t FileOpQueue::FindRunnable() const {
    for (size_t i = 0; i < queue_.size(); ++i) {
        bool blocked = false;
        for (const auto& active : active_) {
            if (FileOpsConflict(queue_[i].touches, active.second)) {
                blocked = true;
                break;
            }
        }
        // And by anything asked for earlier that wants the same place. Without
        // this a later request could overtake the one it depends on, and the
        // pair would apply in the wrong order.
        for (size_t j = 0; !blocked && j < i; ++j) {
            if (FileOpsConflict(queue_[i].touches, queue_[j].touches)) blocked = true;
        }
        if (!blocked) return i;
    }
    return queue_.size();
}

FileOpDone FileOpQueue::Run(const Job& job) {
    const FileOpRequest& req = job.request;
    FileOpDone out;
    out.token = job.token;
    out.kind = req.kind;
    out.ok = true;

    switch (req.kind) {
        case FileOpKind::Delete:
            out.ok = fs_.Delete(req.paths, req.recycle, &out.error);
            break;

        case FileOpKind::Transfer:
            for (const FileOpGroup& group : req.groups) {
                if (group.sources.empty() || group.destDir.empty()) continue;
                const std::vector<bool> existedBefore =
                    DestinationsExist(fs_, group.sources, group.destDir);
                if (!fs_.CopyTo(group.sources, group.destDir, req.move, &out.error)) {
                    // Stopped at the first failure rather than pressing on: a
                    // half-applied undo that reports success leaves the user
                    // believing the folders are back as they were.
                    out.ok = false;
                    break;
                }
                for (size_t i = 0; i < group.sources.size(); ++i) {
                    if (i < existedBefore.size() && existedBefore[i]) continue;
                    const std::string dest =
                        path::Join(group.destDir, path::FileName(group.sources[i]));
                    if (!fs_.Exists(dest)) continue;
                    out.created.push_back(dest);
                    out.origins.push_back(group.sources[i]);
                }
            }
            break;

        case FileOpKind::Duplicate:
            out.ok = fs_.CopyAs(req.paths, req.destPaths, &out.error);
            if (out.ok) {
                // Every name here was picked because nothing held it, so all
                // that is left to ask is whether the copy arrived.
                for (const std::string& dest : req.destPaths) {
                    if (fs_.Exists(dest)) out.created.push_back(dest);
                }
            }
            break;
    }

    // A copy has nowhere to go back to, and saying otherwise would let an undo
    // reach for a source that was never moved.
    if (!req.move) out.origins.clear();
    return out;
}

void FileOpQueue::WorkerMain() {
    for (;;) {
        Job job;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            size_t index = 0;
            cv_.wait(lock, [this, &index] {
                if (stop_) return true;
                index = FindRunnable();
                return index < queue_.size();
            });
            if (stop_) return;
            job = std::move(queue_[index]);
            queue_.erase(queue_.begin() + static_cast<ptrdiff_t>(index));
            active_.push_back({ job.token, job.touches });
            running_.fetch_add(1, std::memory_order_relaxed);
        }

        // 走り「始めた」ことも画面の答えを変える ─ 待機中だったものが実行中に
        // 変わっても、誰も再描画を頼まなければ古い件数が出たままになる。
        wake_.Wake();

        FileOpDone result = Run(job);

        {
            std::lock_guard<std::mutex> lock(mutex_);
            done_.push_back(std::move(result));
            for (size_t i = 0; i < active_.size(); ++i) {
                if (active_[i].first != job.token) continue;
                active_.erase(active_.begin() + static_cast<ptrdiff_t>(i));
                break;
            }
            // Counted down while the place is being given up, so that "how many
            // are running" and "which places are taken" cannot disagree.
            running_.fetch_sub(1, std::memory_order_relaxed);
            pending_.fetch_sub(1, std::memory_order_relaxed);
        }
        // Everyone, not one: the place this request was holding can be what
        // several of the waiting ones were blocked on.
        cv_.notify_all();
        wake_.Wake();
    }
}

}  // namespace kite::fs
