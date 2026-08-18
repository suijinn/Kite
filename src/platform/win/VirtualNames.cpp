#include "platform/win/VirtualNames.h"

#include "core/fs/VirtualPath.h"

namespace kite::win {
namespace {

// The CLSID spellings rather than the "shell:MyComputerFolder" ones. Both parse,
// but this is the form the shell itself hands back for these folders, so using
// it here keeps one place with one answer - the round trip through
// FromShellParsingName() lands back on the same id.
struct Known {
    const char* id;
    const char* parsing;
};

constexpr Known kKnown[] = {
    { vfs::kComputer, "::{20D04FE0-3AEA-1069-A2D8-08002B30309D}" },
    { vfs::kRecycleBin, "::{645FF040-5081-101B-9F08-00AA002F954E}" },
    { vfs::kNetwork, "::{F02C1A0D-BE21-4350-88B0-7367FC96EF3C}" },
};

// GUIDs are written either way round the alphabet depending on who printed them.
bool EqualsFold(const std::string& a, const char* b) {
    size_t i = 0;
    for (; i < a.size() && b[i] != '\0'; ++i) {
        char lhs = a[i];
        char rhs = b[i];
        if (lhs >= 'a' && lhs <= 'z') lhs = static_cast<char>(lhs - 32);
        if (rhs >= 'a' && rhs <= 'z') rhs = static_cast<char>(rhs - 32);
        if (lhs != rhs) return false;
    }
    return i == a.size() && b[i] == '\0';
}

}  // namespace

std::string ToShellParsingName(const std::string& path) {
    if (!vfs::IsVirtual(path)) return {};
    for (const Known& k : kKnown) {
        if (path == k.id) return k.parsing;
    }
    return path.substr(sizeof(vfs::kPrefix) - 1);
}

std::string ToShellPath(const std::string& path) {
    if (!vfs::IsVirtual(path)) return path;
    return ToShellParsingName(path);
}

std::string FromShellParsingName(const std::string& parsing, bool fileSystem) {
    if (parsing.empty()) return {};
    // A drive under "PC" parses to "C:\\" and is simply that folder from here
    // on - the fast path lists it, the watcher watches it, and nothing else in
    // Kite has to know it was reached through the shell namespace.
    if (fileSystem) return parsing;
    for (const Known& k : kKnown) {
        if (EqualsFold(parsing, k.parsing)) return k.id;
    }
    return std::string(vfs::kPrefix) + parsing;
}

}  // namespace kite::win
