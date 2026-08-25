#include "core/fs/VirtualPath.h"

#include <utility>

#include "core/base/PathUtil.h"

namespace kite::vfs {
namespace {

// The prefix minus its terminating NUL.
constexpr size_t kPrefixLength = sizeof(kPrefix) - 1;

// Which extensions the shell walks into is not a rule - it is a fact about the
// machine Kite happens to be running on. Windows 11 browses tar, gz, 7z and rar
// through the same namespace that has always taken zip; Windows 10 takes
// neither, and installing an extractor moves .7z back out of the namespace on
// either. Naming one here that the shell cannot open would name a place with
// nobody to enumerate it: the tab opens empty, which is worse than handing the
// file to the shell in the first place.
//
// So the list arrives from outside (SetArchiveExtensions), and what is written
// here is only the answer for when nobody said: the two the shell has opened as
// folders since Windows XP, which is also what the tests want to see.
//
// Written once at start-up, before any loader worker exists; read from the UI
// thread and those workers alike afterwards.
std::vector<std::string>& ArchiveExtensions() {
    static std::vector<std::string> extensions = { "zip", "cab" };
    return extensions;
}

// The path without the prefix, and without a trailing separator - the spelling
// the shell itself uses for the same place.
std::string_view Body(std::string_view p) {
    std::string_view body = p.substr(kPrefixLength);
    while (body.size() > 1 && path::IsSep(body.back())) body.remove_suffix(1);
    return body;
}

}  // namespace

bool IsVirtual(std::string_view p) {
    return p.size() > kPrefixLength && p.compare(0, kPrefixLength, kPrefix) == 0;
}

bool IsWellKnown(std::string_view p) { return LabelKey(p) != nullptr; }

bool IsArchiveExtension(std::string_view ext) {
    if (ext.empty()) return false;
    for (const std::string& known : ArchiveExtensions()) {
        if (ext == known) return true;
    }
    return false;
}

void SetArchiveExtensions(std::vector<std::string> extensions) {
    ArchiveExtensions() = std::move(extensions);
}

bool IsArchiveName(std::string_view p) { return IsArchiveExtension(path::Extension(p)); }

std::string ArchivePath(std::string_view file) {
    if (file.empty()) return {};
    return std::string(kPrefix) + std::string(file);
}

std::string ArchiveFileOf(std::string_view p) {
    if (!IsVirtual(p)) return {};
    const std::string_view body = Body(p);
    // "::{CLSID}" names a namespace extension, not a file: no component of it
    // is a path, so no component of it can be an archive on disk.
    if (body.size() >= 2 && body[0] == ':' && body[1] == ':') return {};

    // The first archive component wins. Everything past it belongs to the
    // shell, so a .zip found *inside* one is not a file the filesystem holds.
    for (size_t i = 0; i <= body.size();) {
        size_t end = body.find_first_of("\\/", i);
        if (end == std::string_view::npos) end = body.size();
        if (IsArchiveName(body.substr(0, end))) return std::string(body.substr(0, end));
        if (end == body.size()) break;
        i = end + 1;
    }
    return {};
}

const char* LabelKey(std::string_view p) {
    if (p == kComputer) return "ui.vfolder_computer";
    if (p == kRecycleBin) return "ui.vfolder_recycle_bin";
    if (p == kNetwork) return "ui.vfolder_network";
    return nullptr;
}

std::string ParentOf(std::string_view p) {
    if (p.empty()) return {};
    // path::Parent() needs no help with the ordinary case: there is no
    // filesystem root to protect, so it strips the last component of "virtual:a\\b"
    // and answers with nothing at all for "virtual:a".
    if (IsVirtual(p)) {
        // The archive itself is the seam: above it is the real folder it sits
        // in, and the walk leaves the shell namespace for good. Items inside it
        // step back one component like any other virtual path.
        const std::string archive = ArchiveFileOf(p);
        if (!archive.empty() && archive.size() == Body(p).size()) return path::Parent(archive);
        return path::Parent(p);
    }

    std::string parent = path::Parent(p);
    if (!parent.empty()) return parent;

    // Nothing above it in the filesystem - but there is somewhere above it on
    // screen. Only roots get this: a relative path that ran out of components
    // is not "at the top of C:", it is a path Kite cannot place at all.
    if (!path::IsRoot(p) && !path::IsUncServer(p)) return {};
    if (path::UncServerLength(p) > 0) return kNetwork;
    return kComputer;
}

// A shell parsing name is already the body of a virtual path, so the whole job is
// putting the prefix in front of it. Recognising it by the leading "::{" is enough:
// no filesystem path starts that way, and the shell writes nothing else into a
// command line for a place that has no path.
std::string FromCommandLine(std::string_view arg) {
    constexpr std::string_view kParsingName = "::{";
    if (IsVirtual(arg)) return std::string(arg);
    if (arg.compare(0, kParsingName.size(), kParsingName) != 0) return std::string(arg);
    return std::string(kPrefix) + std::string(arg);
}

std::string TrailingName(std::string_view p) {
    if (!IsVirtual(p)) return {};
    std::string_view body = p.substr(kPrefixLength);
    while (!body.empty() && path::IsSep(body.back())) body.remove_suffix(1);
    const size_t sep = body.find_last_of("\\/");
    if (sep != std::string_view::npos) body = body.substr(sep + 1);
    return std::string(body);
}

}  // namespace kite::vfs
