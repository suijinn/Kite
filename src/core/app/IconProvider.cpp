#include "core/app/IconProvider.h"

#include <algorithm>

namespace kite {

uint32_t IconCache::Lookup(const std::string& path) {
    if (path.empty()) return 0;

    Entry& entry = entries_[path];
    entry.used = ++tick_;
    if (!entry.resolved && !entry.inFlight) {
        entry.inFlight = true;
        pending_.push_back(path);
        // Only an unresolved entry can have been inserted just now, so this is
        // the one place the table can have grown.
        EvictIfNeeded();
    }
    // An invalidated entry keeps handing back its previous id while the new
    // answer is on its way. One refresh out of date reads better than every row
    // flicking back to the fallback glyph and in again.
    return entry.iconId;
}

void IconCache::TakePending(std::vector<std::string>& out, size_t max) {
    const size_t count = std::min(max, pending_.size());
    if (count == 0) return;

    // Newest first: the rows that came into view most recently are the ones the
    // user is looking at, and a fast scroll leaves a long queue behind it.
    for (size_t i = 0; i < count; ++i) out.push_back(pending_[pending_.size() - 1 - i]);
    pending_.resize(pending_.size() - count);
}

void IconCache::Store(const std::string& path, uint32_t iconId) {
    auto it = entries_.find(path);
    // Gone already: the cache was cleared, or the entry was evicted while the
    // request was in flight. Adding it back would resurrect a row nobody is
    // looking at any more.
    if (it == entries_.end()) return;
    it->second.iconId = iconId;
    it->second.resolved = true;
    it->second.inFlight = false;
}

void IconCache::Reset(const std::vector<std::string>& paths) {
    for (const std::string& path : paths) {
        auto it = entries_.find(path);
        if (it == entries_.end() || it->second.resolved) continue;
        it->second.inFlight = false;
    }
}

void IconCache::Clear() {
    entries_.clear();
    pending_.clear();
}

void IconCache::Invalidate() {
    for (auto& [path, entry] : entries_) {
        // A request already with the host keeps its place in the queue. Asking
        // again would only put the same path in twice, and the answer it is
        // waiting for is at most a few hundred milliseconds old.
        if (!entry.inFlight) entry.resolved = false;
    }
}

void IconCache::Forget() {
    for (auto& [path, entry] : entries_) {
        if (entry.inFlight) continue;
        entry.resolved = false;
        entry.iconId = 0;
    }
}

void IconCache::SetCapacity(size_t capacity) {
    capacity_ = std::max<size_t>(1, capacity);
    EvictIfNeeded();
}

void IconCache::EvictIfNeeded() {
    if (entries_.size() <= capacity_) return;

    // Drop a quarter at a time rather than one per insertion: this walks the
    // whole table, and doing that on every added row in a huge folder would cost
    // more than the icons themselves.
    const size_t target = capacity_ - capacity_ / 4;
    std::vector<uint64_t> ages;
    ages.reserve(entries_.size());
    for (const auto& [path, entry] : entries_) ages.push_back(entry.used);

    const size_t drop = entries_.size() - target;
    std::nth_element(ages.begin(), ages.begin() + static_cast<ptrdiff_t>(drop), ages.end());
    const uint64_t cutoff = ages[drop];

    for (auto it = entries_.begin(); it != entries_.end();) {
        // An entry still waiting on a result keeps its slot: dropping it would
        // strand the reply, and the row is on screen right now anyway.
        it = (it->second.used < cutoff && !it->second.inFlight) ? entries_.erase(it)
                                                               : std::next(it);
    }
}

}  // namespace kite
