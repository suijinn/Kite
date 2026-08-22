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

    /// @brief この項目自身を指すパス。空なら「親のパス + 名前」で指せる。
    ///
    /// 実フォルダの項目では常に空 ─ 埋めると 1 件あたり 1 本ずつ std::string が
    /// 増え、10 万件のフォルダでその代金を払うことになる。値が入るのは仮想
    /// フォルダ（「PC」「ごみ箱」「ネットワーク」）の中身のように、**名前を
    /// 連結しても指せない**項目だけ。「PC」の下のドライブなら "C:\\"、名前空間
    /// 拡張なら vfs::kPrefix 付きのシェル解析名が入る。
    ///
    /// @note 添字と同じで、`path::Join()` を直接書かず EntryPath() を通すこと
    std::string address;

    /// @brief ディレクトリかを判定する。
    /// @return ディレクトリなら true
    bool isDir() const { return Has(attrs, Attr::Directory); }

    /// @brief 既定で一覧から隠す対象かを判定する。
    /// @return 隠し属性またはシステム属性が立っていれば true
    bool isHidden() const { return Has(attrs, Attr::Hidden) || Has(attrs, Attr::System); }
};

/// @brief 項目を指すパスを組み立てる。
/// @param[in] dir 項目が属するディレクトリのパス
/// @param[in] entry 対象の項目
/// @return 項目を指すパス
/// @note `path::Join(dir, entry.name)` を直接書かないこと。仮想フォルダの
///       項目は名前を連結しても指せず、Entry::address のほうが答えになる
std::string EntryPath(const std::string& dir, const Entry& entry);

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

    /// @brief 列挙した場所そのものの表示名。空なら呼ぶ側がパスから作る。
    ///
    /// 実フォルダでは常に空（末尾の要素がそのまま名前になる）。値が入るのは、
    /// パスを見ても名前が読み取れない仮想フォルダだけ。
    std::string title;

    /// @brief 列挙した場所が載っているボリュームの空き容量。分からなければ 0。
    ///
    /// 一覧に相乗りしているのは、これを訊く先が OS で、しかも冷えた共有では
    /// 数秒返ってこないから ─ 列挙がすでにワーカーで同じボリュームに触れている
    /// ので、そこで 1 回訊けば UI スレッドは 1 度もブロックしない。タブが一覧を
    /// 取り直すたびに（監視の通知でも F5 でも）新しい値になる。
    /// 仮想フォルダとサーバーの共有一覧では 0 のまま ─ ボリュームではない。
    uint64_t freeBytes = 0;

    /// @brief 同じボリュームの総容量。分からなければ 0。
    ///
    /// freeBytes と必ず同じ問い合わせから来る（片方だけが入ることはない）。
    /// 使用量は「総容量 - 空き」で出す ─ 引き算の答えを持ち回ると、2 つの数と
    /// 食い違う日が来る。
    uint64_t totalBytes = 0;
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

    /// @brief 転送先の名前を 1 件ずつ指定してコピーする。
    /// @param[in] paths 対象のパス列
    /// @param[in] destPaths 転送先のフルパス列。`paths` と同じ長さ・同じ順
    /// @param[out] err 失敗理由。不要なら nullptr
    /// @return 成功したら true。利用者が中断した場合も true を返す
    /// @note 同一フォルダ内の複製（`a.txt` → `a_copy.txt`）のためにある。
    ///       CopyTo() は行き先のフォルダしか言えないので、元と同じ名前で置くこと
    ///       しかできず、同じフォルダへ向けると必ず名前がぶつかる
    /// @note 名前を決めるのは呼び出し側。ここで衝突を解決させると、出来上がった
    ///       名前が誰にも分からなくなり、取り消し履歴が「この操作が作ったもの」を
    ///       指せなくなる
    virtual bool CopyAs(const std::vector<std::string>& paths,
                        const std::vector<std::string>& destPaths, std::string* err) = 0;
};

}  // namespace kite::fs
