/// @file
/// @brief コア全体が前提とするファイルシステム抽象。
///
/// Windows 実装は platform/win/WinFileSystem.cpp。

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace kite::fs {

/// @brief ファイル・ディレクトリの属性ビット。
enum class Attr : uint32_t {
    None = 0,                ///< 属性なし
    Directory = 1u << 0,     ///< ディレクトリ
    Hidden = 1u << 1,        ///< 隠し属性
    System = 1u << 2,        ///< システム属性
    ReadOnly = 1u << 3,      ///< 読み取り専用
    Link = 1u << 4,          ///< シンボリックリンク・ジャンクション・ショートカット
    Compressed = 1u << 5,    ///< 圧縮
    Encrypted = 1u << 6,     ///< 暗号化
    Placeholder = 1u << 7,   ///< クラウド上にのみ実体がある。列挙中に内容へ触れないこと
    Offline = 1u << 8,       ///< オフライン。アクセスに時間がかかりうる
};

/// @brief 属性ビットの論理和を取る。
/// @param[in] a 左辺の属性
/// @param[in] b 右辺の属性
/// @return 両方のビットを立てた属性
inline Attr operator|(Attr a, Attr b) {
    return static_cast<Attr>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

/// @brief 属性ビットを合成して代入する。
/// @param[in,out] a 更新される属性
/// @param[in] b 追加する属性
/// @return 更新後の `a` への参照
inline Attr& operator|=(Attr& a, Attr b) {
    a = a | b;
    return a;
}

/// @brief 特定の属性ビットが立っているかを判定する。
/// @param[in] v 検査する属性の集合
/// @param[in] flag 探すビット
/// @return 立っていれば true
inline bool Has(Attr v, Attr flag) {
    return (static_cast<uint32_t>(v) & static_cast<uint32_t>(flag)) != 0;
}

/// @brief ディレクトリ内の 1 項目。
struct Entry {
    std::string name;        ///< UTF-8 の名前。パスではなく末尾の要素のみ
    uint64_t size = 0;       ///< 論理サイズ。ディレクトリでは 0
    int64_t mtime = 0;       ///< 最終更新時刻。Unix エポックからの秒数
    Attr attrs = Attr::None; ///< 属性ビット

    /// @brief ディレクトリかを判定する。
    /// @return ディレクトリなら true
    bool isDir() const { return Has(attrs, Attr::Directory); }

    /// @brief 既定で一覧から隠す対象かを判定する。
    /// @return 隠し属性またはシステム属性が立っていれば true
    bool isHidden() const { return Has(attrs, Attr::Hidden) || Has(attrs, Attr::System); }
};

/// @brief ルート（ドライブや特別なフォルダ）の種別。
enum class RootKind {
    Fixed,      ///< 固定ディスク
    Removable,  ///< リムーバブルディスク
    Network,    ///< ネットワークドライブ
    Optical,    ///< 光学ドライブ
    Ram,        ///< RAM ディスク
    Cloud,      ///< クラウドストレージがマウントしたもの
    Special,    ///< 既知フォルダ（デスクトップ、ドキュメント等）
    Unknown,    ///< 判別できないもの
};

/// @brief サイドバーに並べるルート項目。
struct Root {
    std::string path;                    ///< "C:\\" や "\\\\server\\share\\" 等
    std::string label;                   ///< 表示名
    RootKind kind = RootKind::Unknown;   ///< 種別
    uint64_t totalBytes = 0;             ///< 総容量。取得しなかった場合は 0
    uint64_t freeBytes = 0;              ///< 空き容量。取得しなかった場合は 0
};

/// @brief 列挙結果の状態。
enum class Status {
    Ok,           ///< 成功
    NotFound,     ///< パスが存在しない
    AccessDenied, ///< アクセス権が無い
    Unavailable,  ///< ドライブ未接続・ネットワーク到達不能など
    Error,        ///< その他の失敗
};

/// @brief ディレクトリ 1 つ分の列挙結果。
struct ListResult {
    Status status = Status::Ok;   ///< 結果の状態
    std::string message;          ///< OS が返した補足メッセージ。無ければ空
    std::vector<Entry> entries;   ///< 列挙された項目。"." と ".." は含まない
};

/// @brief ファイルシステム操作のインターフェース。
class IFileSystem {
public:
    virtual ~IFileSystem() = default;

    /// @brief ディレクトリの内容を列挙する。
    /// @param[in] dir 列挙するディレクトリのパス
    /// @return 列挙結果。失敗時は status に理由が入り entries は空
    /// @note ブロッキング。UI スレッドから直接呼ばず DirectoryLoader を通すこと
    virtual ListResult List(const std::string& dir) = 0;

    /// @brief パスの存在を確認する。
    /// @param[in] path 確認するパス
    /// @param[out] isDir ディレクトリなら true が入る。不要なら nullptr
    /// @return 存在すれば true
    virtual bool Exists(const std::string& path, bool* isDir = nullptr) = 0;

    /// @brief ドライブとマウント済みの場所を列挙する。
    /// @return ルート項目の一覧
    virtual std::vector<Root> Roots() = 0;

    /// @brief ユーザーのホームディレクトリを返す。
    /// @return ホームディレクトリのパス
    virtual std::string HomeDir() = 0;

    /// @brief 設定ファイルを置くディレクトリを返す。
    /// @return 設定ディレクトリのパス
    virtual std::string ConfigDir() = 0;

    /// @brief サイドバーに出す既知フォルダを表示順で返す。
    /// @return クイックアクセス項目の一覧
    virtual std::vector<Root> QuickAccess() = 0;

    /// @brief ディレクトリを作成する。
    /// @param[in] path 作成するディレクトリのパス
    /// @param[out] err 失敗理由。不要なら nullptr
    /// @return 成功したら true
    /// @note `CreateDirectory` は \<windows.h\> のマクロなので Make* という名前にしてある
    virtual bool MakeDirectory(const std::string& path, std::string* err) = 0;

    /// @brief 空のファイルを新規作成する。
    /// @param[in] path 作成するファイルのパス
    /// @param[out] err 失敗理由。不要なら nullptr
    /// @return 成功したら true。既に存在する場合は false
    virtual bool MakeFile(const std::string& path, std::string* err) = 0;

    /// @brief ファイルまたはディレクトリの名前を変更する。
    /// @param[in] from 変更元のパス
    /// @param[in] to 変更後のパス
    /// @param[out] err 失敗理由。不要なら nullptr
    /// @return 成功したら true
    virtual bool Rename(const std::string& from, const std::string& to, std::string* err) = 0;

    /// @brief ファイルまたはディレクトリを削除する。
    /// @param[in] paths 削除対象のパス列
    /// @param[in] toRecycleBin true ならごみ箱へ、false なら完全に削除する
    /// @param[out] err 失敗理由。不要なら nullptr
    /// @return 成功したら true。利用者が中断した場合も true を返す
    virtual bool Delete(const std::vector<std::string>& paths, bool toRecycleBin,
                        std::string* err) = 0;

    /// @brief ファイルまたはディレクトリをコピーまたは移動する。
    /// @param[in] paths 対象のパス列
    /// @param[in] destDir 転送先ディレクトリ
    /// @param[in] move true なら移動、false ならコピー
    /// @param[out] err 失敗理由。不要なら nullptr
    /// @return 成功したら true。利用者が中断した場合も true を返す
    virtual bool CopyTo(const std::vector<std::string>& paths, const std::string& destDir,
                        bool move, std::string* err) = 0;
};

}  // namespace kite::fs
