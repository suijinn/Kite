#include "TestFramework.h"

#include <cstdio>
#include <cstring>
#include <exception>

namespace kite::test {
namespace {

struct TestFailure {
    std::string text;
};

std::vector<TestCase>& Registry() {
    // Function-local so registration order across translation units is safe.
    static std::vector<TestCase> registry;
    return registry;
}

}  // namespace

int Register(const char* suite, const char* name, void (*body)()) {
    Registry().push_back({ suite, name, body });
    return 0;
}

void Fail(const char* file, int line, const std::string& message) {
    const char* shortFile = file;
    for (const char* p = file; *p; ++p) {
        if (*p == '\\' || *p == '/') shortFile = p + 1;
    }
    throw TestFailure{ std::string(shortFile) + ":" + std::to_string(line) + "\n        " +
                       message };
}

}  // namespace kite::test

int main(int argc, char** argv) {
    using kite::test::Registry;

    std::string filter;
    bool listOnly = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--filter") == 0 && i + 1 < argc) {
            filter = argv[++i];
        } else if (std::strcmp(argv[i], "--list") == 0) {
            listOnly = true;
        }
    }

    int passed = 0;
    int failed = 0;
    int skipped = 0;

    for (const kite::test::TestCase& test : Registry()) {
        const std::string full = test.suite + "." + test.name;
        if (!filter.empty() && full.find(filter) == std::string::npos) {
            ++skipped;
            continue;
        }
        if (listOnly) {
            std::printf("%s\n", full.c_str());
            continue;
        }

        try {
            test.body();
            ++passed;
        } catch (const kite::test::TestFailure& failure) {
            std::printf("FAILED  %s\n        %s\n", full.c_str(), failure.text.c_str());
            ++failed;
        } catch (const std::exception& e) {
            std::printf("FAILED  %s\n        unexpected exception: %s\n", full.c_str(), e.what());
            ++failed;
        } catch (...) {
            std::printf("FAILED  %s\n        unexpected non-standard exception\n", full.c_str());
            ++failed;
        }
    }

    if (listOnly) return 0;

    std::printf("%d passed, %d failed", passed, failed);
    if (skipped > 0) std::printf(", %d not selected", skipped);
    std::printf("\n");

    if (passed == 0 && failed == 0) {
        std::printf("no tests matched the filter\n");
        return 1;
    }
    return failed == 0 ? 0 : 1;
}
