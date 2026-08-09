// Kite - the filesystem abstraction the whole core is written against.
// The Windows backend lives in platform/win/WinFileSystem.cpp.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace kite::fs {

enum class Attr : uint32_t {
    None = 0,
    Directory = 1u << 0,
    Hidden = 1u << 1,
    System = 1u << 2,
    ReadOnly = 1u << 3,
    Link = 1u << 4,       // symlink / junction / .lnk
    Compressed = 1u << 5,
    Encrypted = 1u << 6,
    // Cloud-backed and not present locally. Kite must never touch the contents
    // of these during enumeration or it would trigger a download.
    Placeholder = 1u << 7,
    Offline = 1u << 8,
};

inline Attr operator|(Attr a, Attr b) {
    return static_cast<Attr>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
inline Attr& operator|=(Attr& a, Attr b) {
    a = a | b;
    return a;
}
inline bool Has(Attr v, Attr flag) {
    return (static_cast<uint32_t>(v) & static_cast<uint32_t>(flag)) != 0;
}

struct Entry {
    std::string name;      // UTF-8, leaf name only
    uint64_t size = 0;     // logical size; 0 for directories
    int64_t mtime = 0;     // seconds since the Unix epoch
    Attr attrs = Attr::None;

    bool isDir() const { return Has(attrs, Attr::Directory); }
    bool isHidden() const { return Has(attrs, Attr::Hidden) || Has(attrs, Attr::System); }
};

enum class RootKind { Fixed, Removable, Network, Optical, Ram, Cloud, Special, Unknown };

struct Root {
    std::string path;   // "C:\", "\\\\server\\share\\", ...
    std::string label;  // display name
    RootKind kind = RootKind::Unknown;
    uint64_t totalBytes = 0;
    uint64_t freeBytes = 0;
};

enum class Status { Ok, NotFound, AccessDenied, Unavailable, Error };

struct ListResult {
    Status status = Status::Ok;
    std::string message;  // already-localized, or empty
    std::vector<Entry> entries;
};

class IFileSystem {
public:
    virtual ~IFileSystem() = default;

    // Blocking. Callers on the UI thread must go through DirectoryLoader.
    virtual ListResult List(const std::string& dir) = 0;

    virtual bool Exists(const std::string& path, bool* isDir = nullptr) = 0;

    // Drives, mapped network drives and mounted cloud folders.
    virtual std::vector<Root> Roots() = 0;

    virtual std::string HomeDir() = 0;
    virtual std::string ConfigDir() = 0;

    // Well-known user folders worth showing in the sidebar, in display order.
    virtual std::vector<Root> QuickAccess() = 0;

    // --- mutating operations -------------------------------------------------
    // Named Make* rather than Create*: <windows.h> defines CreateDirectory and
    // CreateFile as macros, which would rewrite these declarations.
    virtual bool MakeDirectory(const std::string& path, std::string* err) = 0;
    virtual bool MakeFile(const std::string& path, std::string* err) = 0;
    virtual bool Rename(const std::string& from, const std::string& to, std::string* err) = 0;

    // `toRecycleBin == false` deletes irreversibly.
    virtual bool Delete(const std::vector<std::string>& paths, bool toRecycleBin,
                        std::string* err) = 0;
    virtual bool CopyTo(const std::vector<std::string>& paths, const std::string& destDir,
                        bool move, std::string* err) = 0;
};

}  // namespace kite::fs
