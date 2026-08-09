// Kite - human-readable formatting of sizes and timestamps.
#pragma once

#include <cstdint>
#include <string>

namespace kite {

// "12.3 MB" style, matching what a file list needs at a glance.
std::string FormatSize(uint64_t bytes);

// "2026-08-09 17:11" in local time.
std::string FormatDateTime(int64_t unixSeconds);

}  // namespace kite
