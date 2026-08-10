/// @file
/// @brief ReadDirectoryChangesW によるディレクトリ監視。

#pragma once

#include <windows.h>

#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "core/fs/DirectoryLoader.h"  // fs::IWakeSink
#include "core/fs/DirectoryWatcher.h"

namespace kite::win {

/// @brief 単一の I/O 完了ポートで駆動するディレクトリ監視。
///
/// 監視対象がいくつあってもスレッドは 1 本で済む。
///
/// @note ハンドルの所有権はすべてワーカースレッドにある。UI スレッドからの
///       Watch()/Unwatch() はコマンドを投函するだけなので、「ディレクトリ
///       ハンドルを閉じる」と「その読み取り完了通知が届く」の競合が起きない。
///       この API を誤用する典型パターンを構造的に避けている
class WinDirectoryWatcher final : public fs::IDirectoryWatcher {
public:
    /// @brief 完了ポートとワーカースレッドを用意する。
    /// @param[in] wake 変更を検出したときに起こす相手。本オブジェクトより長生きすること
    explicit WinDirectoryWatcher(fs::IWakeSink& wake);

    /// @brief ワーカースレッドを停止し、全ハンドルを閉じてから破棄する。
    ~WinDirectoryWatcher() override;

    WinDirectoryWatcher(const WinDirectoryWatcher&) = delete;
    WinDirectoryWatcher& operator=(const WinDirectoryWatcher&) = delete;

    /// @copydoc fs::IDirectoryWatcher::Watch
    void Watch(uint64_t watchId, const std::string& path) override;

    /// @copydoc fs::IDirectoryWatcher::Unwatch
    void Unwatch(uint64_t watchId) override;

    /// @copydoc fs::IDirectoryWatcher::Drain
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
