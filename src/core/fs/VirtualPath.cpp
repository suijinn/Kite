#include "core/fs/VirtualPath.h"

#include "core/base/PathUtil.h"

namespace kite::vfs {
namespace {

// The prefix minus its terminating NUL.
constexpr size_t kPrefixLength = sizeof(kPrefix) - 1;

}  // namespace

bool IsVirtual(std::string_view p) {
    return p.size() > kPrefixLength && p.compare(0, kPrefixLength, kPrefix) == 0;
}

bool IsWellKnown(std::string_view p) { return LabelKey(p) != nullptr; }

const char* LabelKey(std::string_view p) {
    if (p == kComputer) return "ui.vfolder_computer";
    if (p == kRecycleBin) return "ui.vfolder_recycle_bin";
    if (p == kNetwork) return "ui.vfolder_network";
    return nullptr;
}

std::string ParentOf(std::string_view p) {
    if (p.empty()) return {};
    // A virtual path needs no special walk: path::Parent() has no filesystem
    // root to protect here, so it strips the last component of "virtual:a\\b"
    // and answers with nothing at all for "virtual:a".
    if (IsVirtual(p)) return path::Parent(p);

    std::string parent = path::Parent(p);
    if (!parent.empty()) return parent;

    // Nothing above it in the filesystem - but there is somewhere above it on
    // screen. Only roots get this: a relative path that ran out of components
    // is not "at the top of C:", it is a path Kite cannot place at all.
    if (!path::IsRoot(p) && !path::IsUncServer(p)) return {};
    if (path::UncServerLength(p) > 0) return kNetwork;
    return kComputer;
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
