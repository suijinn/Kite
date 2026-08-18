#include "core/fs/FileSystem.h"

#include "core/base/PathUtil.h"

namespace kite::fs {

std::string EntryPath(const std::string& dir, const Entry& entry) {
    // The address wins when there is one: inside a virtual folder the name is
    // only a label, and joining it to the folder's own path would build a
    // string that points at nothing ("virtual:computer\\Windows (C:)").
    if (!entry.address.empty()) return entry.address;
    return path::Join(dir, entry.name);
}

}  // namespace kite::fs
