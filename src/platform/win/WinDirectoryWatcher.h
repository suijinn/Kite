#pragma once

#include <windows.h>

#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "core/fs/DirectoryLoader.h"  // fs::IWakeSink
#include "core/fs/DirectoryWatcher.h"

namespace kite::win {

// ReadDirectoryChangesW driven by a single I/O completion port, so any number
// of watched folders costs one thread.
//
// All handle ownership lives on the worker thread. Watch/Unwatch from the UI
// thread only post commands, which removes the race between closing a
// directory handle and its in-flight completion - the classic way this API is
// got wrong.
class WinDirectoryWatcher final : public fs::IDirectoryWatcher {
public:
    explicit WinDirectoryWatcher(fs::IWakeSink& wake);
    ~WinDirectoryWatcher() override;

    WinDirectoryWatcher(const WinDirectoryWatcher&) = delete;
    WinDirectoryWatcher& operator=(const WinDirectoryWatcher&) = delete;

    void Watch(uint64_t watchId, const std::string& path) override;
    void Unwatch(uint64_t watchId) override;
    void Drain(std::vector<fs::ChangeEvent>& out) override;

private:
    struct Watch_ {
        uint64_t id = 0;
        std::string path;
        HANDLE dir = INVALID_HANDLE_VALUE;
        OVERLAPPED overlapped{};
        bool readPending = false;
        bool closing = false;
        // Declared as DWORDs rather than bytes because FILE_NOTIFY_INFORMATION
        // must start DWORD aligned; this gets that for free, without an
        // alignas that would pad the surrounding struct.
        DWORD buffer[(16 * 1024) / sizeof(DWORD)] = {};
    };

    struct Command {
        enum class Kind { Add, Remove } kind = Kind::Add;
        uint64_t id = 0;
        std::string path;
    };

    void WorkerMain();
    void ProcessCommands();
    void StartWatch(uint64_t id, const std::string& path);
    void StopWatch(uint64_t id);
    bool IssueRead(Watch_* watch);
    void MarkDirty(Watch_* watch);
    void FlushDebounced(bool force);
    void CloseAll();

    fs::IWakeSink& wake_;
    HANDLE port_ = nullptr;
    std::thread thread_;

    std::mutex mutex_;
    std::deque<Command> commands_;
    std::vector<fs::ChangeEvent> ready_;

    // Worker-thread only.
    std::unordered_map<uint64_t, std::unique_ptr<Watch_>> watches_;
    std::unordered_map<uint64_t, uint64_t> dirtyAtMs_;
};

}  // namespace kite::win
