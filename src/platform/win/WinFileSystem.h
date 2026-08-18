/// @file
/// @brief Win32 API によるファイルシステム実装。

#pragma once

#include <string>
#include <vector>

#include "core/fs/FileSystem.h"
#include "platform/win/FolderHostClient.h"

namespace kite::win {

/// @brief Win32 API を直接使うファイルシステム。
///
/// 実フォルダに対しては IShellFolder を意図的に使わない。列挙が速いままで、
/// サードパーティのシェル拡張を読み込むこともなく、ファイル内容に一切触れないので
/// クラウドのプレースホルダをダウンロードさせずに一覧できる。
///
/// 仮想フォルダ（`virtual:` 付きのパス）だけがシェル名前空間を必要とするが、
/// **その列挙もこのプロセスでは行わない** ─ `kite_shellhost.exe` に投げる
/// （`FolderHostClient`）。実 FS の高速経路には手を触れていない。
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
    /// @note 先頭に「PC」「ごみ箱」「ネットワーク」を置く。ここに置くことが
    ///       「この環境はこの 3 つを開ける」という表明そのもので、名前は付けない
    ///       （Kite の言語で呼ぶのは App::RefreshRoots の仕事）
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
    /// @brief 仮想フォルダ 1 つ分をホストに列挙させる。
    /// @param[in] dir `virtual:` 付きのパス
    /// @return 列挙結果
    fs::ListResult ListVirtual(const std::string& dir);

    std::string home_;
    std::string configDir_;
    FolderHostClient folders_;  ///< 仮想フォルダの列挙口。内部で直列化している
};

}  // namespace kite::win
