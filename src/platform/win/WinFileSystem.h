#pragma once

#include <string>
#include <vector>

#include "core/fs/FileSystem.h"

namespace kite::win {

// Win32-API-based filesystem. Deliberately avoids IShellFolder: enumeration
// stays fast, never loads third-party shell extensions, and never touches file
// contents - so cloud placeholders are listed without being downloaded.
//
// List() is called from DirectoryLoader worker threads and must stay
// thread-safe; it holds no mutable state.
class WinFileSystem final : public fs::IFileSystem {
public:
    WinFileSystem();

    fs::ListResult List(const std::string& dir) override;
    bool Exists(const std::string& path, bool* isDir) override;
    std::vector<fs::Root> Roots() override;
    std::string HomeDir() override;
    std::string ConfigDir() override;
    std::vector<fs::Root> QuickAccess() override;

    bool MakeDirectory(const std::string& path, std::string* err) override;
    bool MakeFile(const std::string& path, std::string* err) override;
    bool Rename(const std::string& from, const std::string& to, std::string* err) override;
    bool Delete(const std::vector<std::string>& paths, bool toRecycleBin, std::string* err) override;
    bool CopyTo(const std::vector<std::string>& paths, const std::string& destDir, bool move,
                std::string* err) override;

private:
    std::string home_;
    std::string configDir_;
};

}  // namespace kite::win
