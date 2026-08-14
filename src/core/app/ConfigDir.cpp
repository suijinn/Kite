#include "core/app/ConfigDir.h"

namespace kite::config {

// Empty candidates are skipped rather than selected-and-ignored: the paths come
// from calls that can fail (the module path, a known folder), and a failure that
// turns into an empty string here would send every ini write to a relative path
// next to whatever the working directory happens to be.
std::string Choose(const std::vector<Candidate>& candidates) {
    const std::string* fallback = nullptr;
    for (const Candidate& c : candidates) {
        if (c.dir.empty()) continue;
        if (c.exists) return c.dir;
        fallback = &c.dir;
    }
    return fallback ? *fallback : std::string();
}

}  // namespace kite::config
