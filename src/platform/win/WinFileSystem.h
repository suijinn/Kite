/// @file
/// @brief Win32 API によるファイルシステム実装。

#pragma once

#include <string>
#include <vector>

#include "core/fs/FileSystem.h"

namespace kite::win {

/// @brief Win32 API を直接使うファイルシステム。
///
/// IShellFolder を意図的に使わない。列挙が速いままで、サードパーティのシェル拡張を
/// 読み込むこともなく、ファイル内容に一切触れないのでクラウドのプレースホルダを
/// ダウンロードさせずに一覧できる。
///
/// @note List() は DirectoryLoader のワーカースレッドから呼ばれるためスレッド安全
///       でなければならない。可変な状態を持たないことで満たしている
class WinFileSystem final : public fs::IFileSystem {
public:
    /// @brief 既知フォルダと設定ディレクトリを解決し、メディア未挿入時のダイアログを
    ///        抑止する。
    /// @note 設定ディレクトリは exe の隣の `config` を優先し、無ければ
    ///       `%APPDATA%\\Kite`。選ぶ規則そのものは config::Choose() が持つ
    WinFileSystem();

    /// @copydoc fs::IFileSystem::List
    fs::ListResult List(const std::string& dir) override;

    /// @copydoc fs::IFileSystem::Exists
    bool Exists(const std::string& path, bool* isDir) override;

    /// @copydoc fs::IFileSystem::Roots
    /// @note 切断されたネットワークドライブで数秒固まらないよう、容量の取得は
    ///       固定・リムーバブルドライブに限っている
    std::vector<fs::Root> Roots() override;

    /// @copydoc fs::IFileSystem::HomeDir
    std::string HomeDir() override;

    /// @copydoc fs::IFileSystem::ConfigDir
    /// @note 置き場所はコンストラクタで一度だけ決める。実行中に決め直さないのは、
    ///       設定を読んだ場所と書く場所が食い違わないようにするため
    std::string ConfigDir() override;

    /// @copydoc fs::IFileSystem::QuickAccess
    std::vector<fs::Root> QuickAccess() override;

    /// @copydoc fs::IFileSystem::MakeDirectory
    bool MakeDirectory(const std::string& path, std::string* err) override;

    /// @copydoc fs::IFileSystem::MakeFile
    bool MakeFile(const std::string& path, std::string* err) override;

    /// @copydoc fs::IFileSystem::Rename
    bool Rename(const std::string& from, const std::string& to, std::string* err) override;

    /// @copydoc fs::IFileSystem::Delete
    bool Delete(const std::vector<std::string>& paths, bool toRecycleBin, std::string* err) override;

    /// @copydoc fs::IFileSystem::CopyTo
    /// @note 進捗と競合解決のダイアログはシェルに任せている。自前で書き直すのは
    ///       ファイラーがデータを失う最も一般的な原因
    bool CopyTo(const std::vector<std::string>& paths, const std::string& destDir, bool move,
                std::string* err) override;

private:
    std::string home_;
    std::string configDir_;
};

}  // namespace kite::win
