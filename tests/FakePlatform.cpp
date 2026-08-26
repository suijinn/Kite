// Kite tests - an in-memory implementation of core/base/Platform.h.
//
// This is the whole reason the platform seam is only five free functions:
// linking the core against this file is enough to exercise App end to end
// without touching a real disk or a real clock.
#include <map>
#include <string>

#include "Fakes.h"
#include "core/base/Platform.h"

namespace kite::test {

std::map<std::string, std::string>& FakeFiles() {
    static std::map<std::string, std::string> files;
    return files;
}

uint64_t& FakeClockMs() {
    static uint64_t now = 1000;
    return now;
}

int64_t& FakeUnixTime() {
    // 2026-01-01 00:00:00 UTC。値そのものに意味は無いが、固定してあることに意味が
    // ある ─ 経過時間の検査が «テストを走らせた日» で答えを変えてはならない。
    static int64_t now = 1767225600;
    return now;
}

std::string& FakeReadOnlyPrefix() {
    static std::string prefix;
    return prefix;
}

void ResetFakePlatform() {
    FakeFiles().clear();
    FakeClockMs() = 1000;
    FakeUnixTime() = 1767225600;
    FakeReadOnlyPrefix().clear();
}

namespace {

// The prefix stands for a location that refuses to be written to - a zip
// extracted into Program Files, a folder on read-only media.
bool IsReadOnly(const std::string& path) {
    const std::string& prefix = FakeReadOnlyPrefix();
    return !prefix.empty() && path.compare(0, prefix.size(), prefix) == 0;
}

// The read-only folder itself is taken to exist already, which is what makes
// SHCreateDirectoryExW answer ERROR_ALREADY_EXISTS on the real thing. Only
// something new underneath it cannot be created.
bool IsBelowReadOnly(const std::string& path) {
    return IsReadOnly(path) && path.size() > FakeReadOnlyPrefix().size();
}

}  // namespace

}  // namespace kite::test

namespace kite::plat {

bool ReadTextFile(const std::string& utf8Path, std::string& out) {
    auto& files = test::FakeFiles();
    auto it = files.find(utf8Path);
    if (it == files.end()) return false;
    out = it->second;
    return true;
}

bool WriteTextFile(const std::string& utf8Path, std::string_view data) {
    // A refused write leaves nothing behind, the way CreateFile refusing to open
    // does: the file that was there before is still there, unchanged.
    if (test::IsReadOnly(utf8Path)) return false;
    test::FakeFiles()[utf8Path] = std::string(data);
    return true;
}

bool EnsureDirectory(const std::string& utf8Path) { return !test::IsBelowReadOnly(utf8Path); }

uint64_t NowMs() { return test::FakeClockMs(); }

int64_t NowUnixSeconds() { return test::FakeUnixTime(); }

std::string PreferredLanguage() { return "en"; }

}  // namespace kite::plat
