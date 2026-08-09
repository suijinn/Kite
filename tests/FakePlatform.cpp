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

void ResetFakePlatform() {
    FakeFiles().clear();
    FakeClockMs() = 1000;
}

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
    test::FakeFiles()[utf8Path] = std::string(data);
    return true;
}

bool EnsureDirectory(const std::string&) { return true; }

uint64_t NowMs() { return test::FakeClockMs(); }

std::string PreferredLanguage() { return "en"; }

}  // namespace kite::plat
