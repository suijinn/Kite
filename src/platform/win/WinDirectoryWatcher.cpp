#include "platform/win/WinDirectoryWatcher.h"

#include <memory>

#include "platform/win/WinUtf.h"

namespace kite::win {
namespace {

// Completion keys below this value are control signals rather than watches.
constexpr ULONG_PTR kQuitKey = 1;
constexpr ULONG_PTR kCommandKey = 2;
constexpr ULONG_PTR kFirstWatchKey = 16;

// How long a folder must stay quiet before we call it settled. A file copy
// emits changes continuously; re-listing on each one would be pathological.
constexpr uint64_t kDebounceMs = 250;
constexpr DWORD kPollMs = 100;

constexpr DWORD kNotifyFilter = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
                                FILE_NOTIFY_CHANGE_ATTRIBUTES | FILE_NOTIFY_CHANGE_SIZE |
                                FILE_NOTIFY_CHANGE_LAST_WRITE;

}  // namespace

WinDirectoryWatcher::WinDirectoryWatcher(fs::IWakeSink& wake) : wake_(wake) {
    port_ = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 1);
    if (port_) thread_ = std::thread([this] { WorkerMain(); });
}

WinDirectoryWatcher::~WinDirectoryWatcher() {
    if (port_) {
        ::PostQueuedCompletionStatus(port_, 0, kQuitKey, nullptr);
    }
    if (thread_.joinable()) thread_.join();
    if (port_) ::CloseHandle(port_);
}

void WinDirectoryWatcher::Watch(uint64_t watchId, const std::string& path) {
    if (!port_) return;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        commands_.push_back({ Command::Kind::Add, watchId, path });
    }
    ::PostQueuedCompletionStatus(port_, 0, kCommandKey, nullptr);
}

void WinDirectoryWatcher::Unwatch(uint64_t watchId) {
    if (!port_) return;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        commands_.push_back({ Command::Kind::Remove, watchId, {} });
    }
    ::PostQueuedCompletionStatus(port_, 0, kCommandKey, nullptr);
}

void WinDirectoryWatcher::Drain(std::vector<fs::ChangeEvent>& out) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (ready_.empty()) return;
    for (fs::ChangeEvent& e : ready_) out.push_back(std::move(e));
    ready_.clear();
}

// ---------------------------------------------------------------------------
// Worker thread
// ---------------------------------------------------------------------------

void WinDirectoryWatcher::WorkerMain() {
    for (;;) {
        DWORD bytes = 0;
        ULONG_PTR key = 0;
        OVERLAPPED* overlapped = nullptr;
        const BOOL ok = ::GetQueuedCompletionStatus(port_, &bytes, &key, &overlapped, kPollMs);

        if (!ok && overlapped == nullptr) {
            // Timed out: this is also the debounce tick.
            FlushDebounced(false);
            continue;
        }
        if (key == kQuitKey) break;
        if (key == kCommandKey) {
            ProcessCommands();
            continue;
        }
        if (key < kFirstWatchKey) continue;

        const uint64_t id = static_cast<uint64_t>(key) - kFirstWatchKey;
        auto it = watches_.find(id);
        if (it == watches_.end()) continue;

        Watch_* watch = it->second.get();
        watch->readPending = false;

        if (watch->closing) {
            // Its handle is already closed; this was the cancellation notice.
            watches_.erase(it);
            continue;
        }
        if (!ok) {
            // The directory went away (unmounted, deleted, network dropped).
            // Report it once so the tab shows the error, then drop the watch.
            MarkDirty(watch);
            StopWatch(id);
            continue;
        }

        // bytes == 0 means the kernel buffer overflowed and records were lost.
        // Since a notification only marks the folder stale, that is harmless.
        MarkDirty(watch);
        if (!IssueRead(watch)) StopWatch(id);
        FlushDebounced(false);
    }

    FlushDebounced(true);
    CloseAll();
}

void WinDirectoryWatcher::ProcessCommands() {
    std::deque<Command> pending;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pending.swap(commands_);
    }
    for (const Command& c : pending) {
        if (c.kind == Command::Kind::Add) {
            StartWatch(c.id, c.path);
        } else {
            StopWatch(c.id);
        }
    }
}

void WinDirectoryWatcher::StartWatch(uint64_t id, const std::string& path) {
    auto existing = watches_.find(id);
    if (existing != watches_.end()) {
        if (existing->second->path == path && !existing->second->closing) return;
        StopWatch(id);
    }

    HANDLE dir = ::CreateFileW(ToExtendedPath(path).c_str(), FILE_LIST_DIRECTORY,
                               FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                               OPEN_EXISTING,
                               FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
    if (dir == INVALID_HANDLE_VALUE) return;  // silently unwatched

    auto watch = std::make_unique<Watch_>();
    watch->id = id;
    watch->path = path;
    watch->dir = dir;

    if (!::CreateIoCompletionPort(dir, port_, kFirstWatchKey + id, 0)) {
        ::CloseHandle(dir);
        return;
    }

    Watch_* raw = watch.get();
    watches_[id] = std::move(watch);
    if (!IssueRead(raw)) StopWatch(id);
}

void WinDirectoryWatcher::StopWatch(uint64_t id) {
    auto it = watches_.find(id);
    if (it == watches_.end()) return;

    Watch_* watch = it->second.get();
    if (watch->closing) return;
    watch->closing = true;

    if (watch->dir != INVALID_HANDLE_VALUE) {
        ::CancelIoEx(watch->dir, &watch->overlapped);
        ::CloseHandle(watch->dir);
        watch->dir = INVALID_HANDLE_VALUE;
    }
    dirtyAtMs_.erase(id);

    // With no read outstanding no completion is coming, so it is safe to free
    // now. Otherwise the entry lives until its cancellation arrives.
    if (!watch->readPending) watches_.erase(it);
}

bool WinDirectoryWatcher::IssueRead(Watch_* watch) {
    if (!watch || watch->dir == INVALID_HANDLE_VALUE) return false;
    ::ZeroMemory(&watch->overlapped, sizeof(watch->overlapped));

    const BOOL ok = ::ReadDirectoryChangesW(watch->dir, watch->buffer, sizeof(watch->buffer),
                                            FALSE,  // this folder only, not the whole subtree
                                            kNotifyFilter, nullptr, &watch->overlapped, nullptr);
    watch->readPending = (ok != FALSE);
    return watch->readPending;
}

void WinDirectoryWatcher::MarkDirty(Watch_* watch) {
    dirtyAtMs_[watch->id] = ::GetTickCount64();
}

void WinDirectoryWatcher::FlushDebounced(bool force) {
    if (dirtyAtMs_.empty()) return;
    const uint64_t now = ::GetTickCount64();

    std::vector<fs::ChangeEvent> settled;
    for (auto it = dirtyAtMs_.begin(); it != dirtyAtMs_.end();) {
        if (!force && now - it->second < kDebounceMs) {
            ++it;
            continue;
        }
        auto watch = watches_.find(it->first);
        if (watch != watches_.end()) {
            settled.push_back({ it->first, watch->second->path });
        }
        it = dirtyAtMs_.erase(it);
    }
    if (settled.empty()) return;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (fs::ChangeEvent& e : settled) ready_.push_back(std::move(e));
    }
    wake_.Wake();
}

void WinDirectoryWatcher::CloseAll() {
    for (auto& entry : watches_) {
        Watch_* watch = entry.second.get();
        if (watch->dir != INVALID_HANDLE_VALUE) {
            ::CancelIoEx(watch->dir, &watch->overlapped);
            ::CloseHandle(watch->dir);
            watch->dir = INVALID_HANDLE_VALUE;
        }
    }
    // Drain the cancellation completions before the buffers go away; the
    // kernel may still be holding pointers into them.
    for (;;) {
        bool anyPending = false;
        for (auto& entry : watches_) {
            if (entry.second->readPending) anyPending = true;
        }
        if (!anyPending) break;

        DWORD bytes = 0;
        ULONG_PTR key = 0;
        OVERLAPPED* overlapped = nullptr;
        if (!::GetQueuedCompletionStatus(port_, &bytes, &key, &overlapped, 100) &&
            overlapped == nullptr) {
            break;  // nothing left to arrive
        }
        if (key >= kFirstWatchKey) {
            auto it = watches_.find(static_cast<uint64_t>(key) - kFirstWatchKey);
            if (it != watches_.end()) it->second->readPending = false;
        }
    }
    watches_.clear();
}

}  // namespace kite::win
