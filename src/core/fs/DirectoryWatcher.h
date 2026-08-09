// Kite - filesystem change notification.
//
// Kite does not try to apply individual change records to the listing. A
// notification only means "this folder is stale"; the tab then re-enumerates
// through the normal async path. That keeps the model simple and, more
// importantly, correct on the file systems where the notification stream is
// unreliable or coalesced (network shares, cloud sync folders).
//
// Debouncing happens inside the backend, because a file copy produces hundreds
// of records per second and re-listing on each one would be worse than not
// watching at all.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace kite::fs {

struct ChangeEvent {
    uint64_t watchId = 0;
    std::string path;
};

class IDirectoryWatcher {
public:
    virtual ~IDirectoryWatcher() = default;

    // Starts watching `path` under `watchId`, replacing any previous watch
    // registered with the same id. Failures are silent by design: a folder that
    // cannot be watched (offline share, denied access) simply does not
    // auto-refresh.
    virtual void Watch(uint64_t watchId, const std::string& path) = 0;
    virtual void Unwatch(uint64_t watchId) = 0;

    // Moves debounced events to the caller. UI thread only.
    virtual void Drain(std::vector<ChangeEvent>& out) = 0;
};

}  // namespace kite::fs
