/// @file
/// @brief バックグラウンドでのファイル操作（削除・コピー・移動・複製）。
///
/// 列挙を UI スレッドから追い出したのと同じ話が、実行のほうにもそのまま当てはまる
/// ─ シェルの `SHFileOperation` は完了するまで戻らないので、UI スレッドで呼ぶと
/// **その間ウィンドウがメッセージを 1 つも処理できない。** 数 GB のコピーや、
/// 数万件のフォルダの削除で「しばらく使えなくなる」と報告された形がこれで、
/// 進捗ダイアログはシェルが出しているのに後ろの Kite だけが固まって見える。
///
/// そこで DirectoryLoader と同じ作りにしてある ─ ここに積み、ワーカーが
/// IFileSystem を呼び、完了分を IHost::Wake() の後に UI スレッドが回収する。
///
/// **同じ場所を奪い合わない依頼どうしは同時に走る。** USB への 20 分のコピーの裏で、
/// 別フォルダの 1 件の削除が 20 分待つのでは、非同期にした意味が半分になる ─
/// 「待たせて空を返す機能は、無い機能より悪い」のと同じ話。
///
/// **代わりに、衝突する依頼だけを直列化する**（`FileOpsConflict`）。守っているのは
/// 順序そのものではなく次の 3 つで、どれも «同じ場所に 2 つの操作を同時に入れない»
/// だけで満たせる:
///
/// - **取り消し履歴の `created` が、その操作だけが作ったものを指す。** 2 つの操作が
///   同じ行き先を狙うと「操作の前に無く、後に在る」の判定が隣の産物を拾い、
///   `Ctrl+Z` が他方の作ったファイルを消す。
/// - **同じ木に 2 つのシェル操作を重ねない。** 「コピー先のフォルダを別の操作が
///   削除している最中」には、シェルにも答えが無い。
/// - **依頼した順は、関係し合う依頼の間でだけ保たれる**（部分順序）。複製の名前を
///   決めてから依頼するまでの隙間も、同じ名前を狙う依頼が割り込めない以上は安全。
///
/// **奪い合いは «同じ名前» の単位で見る。** 行き先をフォルダごと名乗ると、USB へ
/// 2 つコピーするだけで両方が `E:\\` を待ち合う ─ そして «読むだけ» の相手（コピー元）
/// は誰とも奪い合わない。この 2 つが無いと、実際の使い方ではほとんど並列にならない。
///
/// 無関係な依頼が待たされないので、待ちが出るのは «同じ場所» か «上限に達した» ときだけ。
/// 走っている件数も待っている件数も画面が言う（`App::UpdateFileOpStatus`）。
///
/// **ここが答えるのは `IFileSystem` の 3 つだけ。** ごみ箱からの復元は
/// `IShellIntegration` ─ その先は `ShellHostClient` で、あちらは呼び出し側の
/// メッセージを回しながら待つ作りなので UI スレッドからしか呼べない（そして
/// 回している以上、そもそもウィンドウは固まらない）。

#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "core/fs/DirectoryLoader.h"
#include "core/fs/FileSystem.h"

namespace kite::fs {

/// @brief 同時に走らせるファイル操作の上限。
///
/// 上限が要る理由は 2 つ ─ **シェルの進捗ダイアログが積み上がる**のと、**同じ物理
/// ディスクでは並列コピーがかえって遅い**こと。手で始める操作が 5 つ以上重なる場面は
/// 無いので、実質「当たらない上限」になる値にしてある。**設定にはしない** ─
/// 切り替えたことをその場で目で確かめられる類の値ではない。
constexpr int kFileOpWorkers = 4;

/// @brief 1 つの依頼が個別のパスを名乗る上限。これを超えると親フォルダを名乗る。
///
/// 衝突判定は総当たりなので、1 万件の削除を 1 万個のパスとして持つと比較が
/// 1 億回になる。**それだけの項目を持つ依頼は、実質そのフォルダを丸ごと相手に
/// している**ので、まとめて親フォルダ 1 つを名乗らせる ─ 大きな依頼だけが
/// 「同じフォルダの操作を待たせる」側に倒れる。
constexpr size_t kFileOpTouchLimit = 64;

/// @brief 依頼するファイル操作の種類。
enum class FileOpKind {
    Delete,     ///< ごみ箱へ、または完全に削除する
    Transfer,   ///< 行き先のフォルダを指してのコピーまたは移動
    Duplicate,  ///< 行き先の名前を 1 件ずつ指してのコピー（同一フォルダの複製）
};

/// @brief 転送 1 まとまり。行き先 1 つと、そこへ運ぶ元のパス列。
///
/// 束ねてあるのは、取り消しの「移動を戻す」が元のフォルダごとに分かれるから
/// （1 件ずつ呼ぶと進捗ダイアログが件数ぶん開いて閉じる）。貼り付けやドロップは
/// 行き先が 1 つしかないので、この 1 要素の場合にあたる。
struct FileOpGroup {
    std::string destDir;               ///< 行き先ディレクトリ
    std::vector<std::string> sources;  ///< そこへ運ぶ元のパス列
};

/// @brief 1 件分の依頼。
struct FileOpRequest {
    FileOpKind kind = FileOpKind::Delete;  ///< 何をするか
    std::vector<std::string> paths;        ///< Delete と Duplicate の対象
    std::vector<std::string> destPaths;    ///< Duplicate の行き先。`paths` と同じ長さ・同じ順
    std::vector<FileOpGroup> groups;       ///< Transfer の中身
    bool recycle = true;                   ///< Delete のとき、ごみ箱へ入れるなら true
    bool move = false;                     ///< Transfer のとき、移動なら true
};

/// @brief 完了した 1 件分の結果。
struct FileOpDone {
    uint64_t token = 0;                    ///< 対応する依頼のトークン
    FileOpKind kind = FileOpKind::Delete;  ///< 依頼された操作
    bool ok = false;                       ///< 成功したか。利用者の中断も true
    std::string error;                     ///< 失敗理由。成功時は空

    /// @brief **この操作が実際に作ったもの**だけを並べたパス列。
    ///
    /// 競合したときシェルは別名を付けるか上書きするかで、どちらの場合も転送先の
    /// 名前は元の名前とは限らない。**操作の前に無く、後に在る**ものだけが確実に
    /// この操作の産物で、それ以外に触れれば元から在ったファイルを消すことになる。
    /// 取り消し履歴が触ってよいのはここに並んだものだけ。
    std::vector<std::string> created;

    /// @brief `created` と同じ長さ・同じ順で、その元のパス。移動でのみ埋まる。
    ///
    /// 移動を戻すには「どこから来たか」が要る。コピーには戻る先が無いので空。
    std::vector<std::string> origins;
};

/// @brief 依頼が触る 1 か所。
struct FileOpTouch {
    std::string path;   ///< 正規化済みのパス
    bool write = true;  ///< 書き換える相手。false は**読むだけ**（コピー元）
};

/// @brief 依頼が触る場所を列挙する。
/// @param[in] request 対象の依頼
/// @return 正規化・重複排除済みの、触る場所の一覧
/// @note **行き先はフォルダではなく «そこに置く名前» を名乗る。** フォルダごと
///       名乗ると、USB へ 2 つコピーするだけで両方が `E:\\` を奪い合う ─ 実際に
///       「並列にならない」と報告された形がこれ。名前まで一致して初めて、
///       「操作の前に無く、後に在る」の判定が隣の産物を拾いうる
/// @note **対象そのものの親は入れない。** 入れると「C:\\home の 1 件を削除」と
///       「C:\\home の 1 件を USB へコピー」が衝突扱いになり、並列にした意味が
///       ちょうど失われる。フォルダを丸ごと相手にする依頼との衝突は、
///       「下にあるか」の判定が拾う
/// @note **コピー元は読むだけ**（`write = false`）。同じファイルを 2 か所へ
///       コピーするのは奪い合いではない。移動元と削除の対象、そして行き先は書く側
/// @note 項目が kFileOpTouchLimit を超える依頼は、まとめて親フォルダを名乗る
std::vector<FileOpTouch> FileOpTouches(const FileOpRequest& request);

/// @brief 2 つの依頼が同じ場所を奪い合うかを判定する。
/// @param[in] a 一方の依頼が触る場所（FileOpTouches() の結果）
/// @param[in] b もう一方の依頼が触る場所
/// @return 重なる場所があり、しかも**どちらかが書く側**なら true
/// @note 「下にある」も重なり。フォルダを削除する依頼と、その中へ入れる依頼は、
///       文字列としては一致しないが同じものを奪い合っている
/// @note 読む者どうしは奪い合わない ─ 1 つのファイルを 2 か所へコピーする 2 件は、
///       同時に走ってよい
bool FileOpsConflict(const std::vector<FileOpTouch>& a, const std::vector<FileOpTouch>& b);

/// @brief ファイル操作を非同期に行うキュー。
///
/// @note Request() と Drain() は UI スレッドからのみ呼ぶこと。IFileSystem の
///       操作系はワーカースレッドから呼ばれるので、スレッド安全でなければならない
class FileOpQueue {
public:
    /// @brief ワーカースレッドを起動する。
    /// @param[in] fsys 操作に使うファイルシステム。本オブジェクトより長生きすること
    /// @param[in] wake 完了通知先。本オブジェクトより長生きすること
    /// @param[in] workers 同時に走らせる上限。1 未満を渡した場合は 1 に丸める
    FileOpQueue(IFileSystem& fsys, IWakeSink& wake, int workers = kFileOpWorkers);

    /// @brief ワーカースレッドの停止を待って破棄する。
    /// @note 実行中の操作は**中断できない** ─ シェルに始めさせたコピーを途中で
    ///       止める手段が無いので、終わるまで待つ。積んであるだけのものは捨てる
    ~FileOpQueue();

    FileOpQueue(const FileOpQueue&) = delete;
    FileOpQueue& operator=(const FileOpQueue&) = delete;

    /// @brief 操作を依頼する。
    /// @param[in] request 依頼の内容
    /// @return この依頼を識別するトークン
    /// @note すぐ走るとは限らない。先に居る依頼と同じ場所を触るなら、そちらが
    ///       終わるまで待つ（`FileOpsConflict`）。無関係なら空きワーカーの数だけ
    ///       同時に走る
    uint64_t Request(FileOpRequest request);

    /// @brief 完了済みの結果をすべて取り出す。
    /// @param[out] out 取り出した結果の追加先。既存の要素は保持される
    /// @note UI スレッドからのみ呼ぶこと
    void Drain(std::vector<FileOpDone>& out);

    /// @brief 未完了の依頼があるかを返す。
    /// @return 実行中または処理待ちのものがあれば true
    bool busy() const { return pending() > 0; }

    /// @brief 未完了の依頼の数を返す。
    /// @return 実行中のものを含む、まだ結果を返していない依頼の数
    int pending() const { return pending_.load(std::memory_order_relaxed); }

    /// @brief いま実際に走っている依頼の数を返す。
    /// @return ワーカーが握っている依頼の数。待っているだけのものは含まない
    /// @note ステータス行が «実行中 n 件» と «他 m 件待機中» を言い分けるためにある
    int running() const { return running_.load(std::memory_order_relaxed); }

private:
    struct Job {
        uint64_t token;
        FileOpRequest request;
        std::vector<FileOpTouch> touches;  ///< 衝突判定に使う、この依頼が触る場所
    };

    void WorkerMain();

    /// @brief 依頼を 1 つ実行する。ワーカースレッド上で呼ばれる。
    FileOpDone Run(const Job& job);

    /// @brief いま走らせてよい依頼を探す。`mutex_` を持った状態で呼ぶこと。
    /// @return `queue_` への添字。走らせてよいものが無ければ `queue_.size()`
    /// @note 実行中のどれとも衝突せず、**自分より前に並んでいるどれとも衝突しない**
    ///       ものだけ。後者が無いと、先に頼まれた依頼を後の依頼が追い越しうる ─
    ///       関係し合う依頼の順序は依頼した順でなければならない
    size_t FindRunnable() const;

    IFileSystem& fs_;
    IWakeSink& wake_;

    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<Job> queue_;
    std::vector<FileOpDone> done_;
    // 実行中の依頼が触っている場所。トークンで引くのは、終わった順に抜けるため。
    std::vector<std::pair<uint64_t, std::vector<FileOpTouch>>> active_;
    bool stop_ = false;

    std::atomic<uint64_t> nextToken_{ 1 };
    std::atomic<int> pending_{ 0 };
    std::atomic<int> running_{ 0 };
    std::vector<std::thread> threads_;
};

}  // namespace kite::fs
