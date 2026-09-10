/// @file
/// @brief フォルダの合計サイズ。数えた値の表と、数えるワーカー。
///
/// 一覧のサイズ列はフォルダに対して `<DIR>` としか言えない ─ `FindFirstFile` は
/// フォルダの中身の合計を答えないので、知るには木を歩くしかない。歩きは
/// `fs::WalkTree` に任せ（検索とまったく同じ歩き方）、結果は `IHost::Wake()` の
/// 後に UI スレッドが回収する ─ 列挙を UI スレッドから追い出したのと同じ話が、
/// そのまま当てはまる。
///
/// **値を `fs::Entry` に持たせない。** `Entry` は列挙の答えで、監視の通知でも
/// `F5` でも作り直される ─ 数えた値の寿命はそれより長い。表のほうが正で、
/// `Entry::size` に書き戻すのは «並べ替えのための写し» でしかない（`App`）。
///
/// **表はウィンドウに 1 つ。** タブごとに持つと、同じフォルダを 2 枚のペインで
/// 見ているだけで 2 回歩くことになる（列がウィンドウに 1 つなのと同じ判断）。
///
/// 判断は OS にも描画にも触れないので、tests/test_foldersize.cpp が端から端まで
/// 検証できる。

#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "core/fs/DirectoryLoader.h"
#include "core/fs/FileSystem.h"

namespace kite::fs {

/// @brief 数えるワーカーの本数。
///
/// **検索が 1 本なのと事情が逆。** あちらは «2 本目の結果を誰も見ていない» から
/// 1 本だが、こちらは画面に出ているフォルダの行が全部同時に答えを待っている ─
/// 1 本にすると、先頭の 1 つが大きいだけで残りが全部その後ろに並ぶ。
constexpr int kFolderSizeWorkers = 2;

/// @brief 途中経過を吐き出す間隔（ミリ秒）。
///
/// 1 フォルダ数え終わるたびに起こすと、浅い木を歩いている間じゅう再描画が
/// 歩きの足を引っ張る。逆に貯めすぎると «増えていく» が見えなくなる。
constexpr uint64_t kFolderSizeFlushMs = 150;

/// @brief 監視の通知で数え直すまでに空ける間隔（ミリ秒）。
///
/// **通知 1 つごとに数え直さない。** 通知が言うのは «このフォルダの直下で何かが
/// 変わった» だけで、そこに書き込み続けるものが在れば（ダウンロード、ビルドの出力）
/// 数百ミリ秒ごとに届く ─ そのたびに子フォルダの木を歩き直すと、見ているだけで
/// ディスクを舐め続けることになる。数えたばかりの値はそのまま出し、次の通知に
/// 任せる。`F5` と自分で起こしたファイル操作はこの間隔を通らない（あちらは
/// «訊き直せ» そのもの）。
constexpr uint64_t kFolderSizeRecountMs = 3000;

/// @brief 1 つのフォルダについて数えた結果の状態。
enum class SizeState : uint8_t {
    Unknown,   ///< まだ数えていない。列は今までどおり `<DIR>`
    Counting,  ///< 歩いている最中。値は途中経過
    Done,      ///< 数え終わった
    Failed,    ///< そのフォルダ自体を読めなかった

    /// やめた（`Cmd::StopFolderSizes`）。列は `<DIR>` に戻るが «まだ» ではない。
    ///
    /// **`Unknown` に戻してはならない。** 自動で数える設定では、次に描いた
    /// 1 フレームがそのまま «数えてくれ» になる ─ やめた直後に数え直したのでは、
    /// やめる道が無いのと同じ。もう一度数えるのは、頼まれたとき
    /// （`Cmd::CountFolderSize`）か、その木が変わったとき（`F5`・ファイル操作）。
    Stopped,
};

/// @brief 1 つのフォルダについて数えた結果。
struct FolderSize {
    uint64_t bytes = 0;  ///< 含まれるファイルの論理サイズの合計
    uint64_t files = 0;  ///< 含まれるファイルの数
    uint64_t dirs = 0;   ///< 含まれるフォルダの数（自分自身は含まない）

    SizeState state = SizeState::Unknown;  ///< 数えた結果の状態

    /// 読めなかった枝があった。合計は «少なくともこれだけ» の意味になる。
    ///
    /// **列には出さない。** 数字の隣に印を足すと、数えるたびに列の綴りが 2 通りに
    /// なる ─ 言うのはステータス行のほうで、しかもそこはカーソル行 1 つについて
    /// 詳しく言える唯一の場所（`ui.folder_size_incomplete`）。
    bool incomplete = false;

    /// 数え終わった時刻（`plat::NowMs()`）。0 なら未確定。
    ///
    /// 覚えているのは «数え直すまでに間隔を空ける» ため（`kFolderSizeRecountMs`）。
    /// 画面には出さない ─ 列に «いつの値か» を書く場所は無いし、書けば数字が
    /// 2 つ並ぶ。
    uint64_t countedAtMs = 0;

    /// @brief 画面に出せる値を持っているかを判定する。
    /// @return 数え中または数え終わっていれば true
    bool known() const { return state == SizeState::Counting || state == SizeState::Done; }

    /// @brief 並べ替えに使ってよい値かを判定する。
    /// @return 数え終わっていれば true
    /// @note **途中経過は並べ替えに載せない。** 載せると、数えている間ずっと行が
    ///       動いて目で追えなくなる
    bool settled() const { return state == SizeState::Done; }
};

/// @brief ワーカーが返す 1 回分の途中経過（または確定値）。
struct FolderSizeUpdate {
    uint64_t epoch = 0;       ///< どの «代» の歩きか。やめると増える
    std::string path;         ///< 数えているフォルダ
    uint64_t bytes = 0;       ///< ここまでに数えたバイト数
    uint64_t files = 0;       ///< ここまでに数えたファイル数
    uint64_t dirs = 0;        ///< ここまでに数えたフォルダ数
    uint64_t atMs = 0;        ///< 数え終わった時刻。`done` のときだけ意味を持つ
    bool done = false;        ///< これで数え終わり
    bool incomplete = false;  ///< 読めなかった枝があった
    bool failed = false;      ///< そのフォルダ自体を読めなかった
};

/// @brief パスから «数えた結果» への表。
///
/// `IconCache` と同じ役回り ─ UI は描画のたびに `Get()` を訊くだけで、無ければ
/// 「まだ」が返る。違うのは、要求するかどうかを決めるのが表ではなく `App` だと
/// いうこと（数えるのは自動・手動・切の 3 通りで、その判断は設定の側にある）。
///
/// @note OS に触れないのでここだけ単体テストできる。実際に歩くのは FolderSizeJob
class FolderSizeCache {
public:
    /// @brief 覚えている結果を返す。
    /// @param[in] path 対象のフォルダ
    /// @return 対応する結果。覚えていなければ状態が `SizeState::Unknown` のもの
    FolderSize Get(const std::string& path) const;

    /// @brief 数えることにして «数え中» の印を立てる。
    /// @param[in] path 対象のフォルダ
    /// @param[in] force 答えが出ているもの・やめたものも数え直す
    /// @return 新たに印を立てたら true。既に数え中、または（force でなく）答えが
    ///         出ていれば false
    /// @note true を返したときだけワーカーに頼むこと。毎フレーム描画から呼ばれる
    ///       ので、ここが二重の依頼を止めている
    /// @note 歩いている最中のものは `force` でも二重に頼まない
    bool Request(const std::string& path, bool force = false);

    /// @brief ワーカーから届いた途中経過（または確定値）を取り込む。
    /// @param[in] update 届いた結果
    /// @return 取り込んだら true。行き先が無ければ false
    /// @note **忘れられたパスの結果は捨てる。** 歩いている最中にファイル操作や
    ///       `F5` がその木を無効にすることがあり、そこへ古い答えを書き戻すと、
    ///       捨てたはずの値が «数え終わった» 顔で戻ってくる
    bool Apply(const FolderSizeUpdate& update);

    /// @brief 数え中のものを «やめた» 印に変える。
    /// @note やめたとき（`Cmd::StopFolderSizes`）に呼ぶ。途中経過は捨てる ─
    ///       止めた値が «数え終わった» 値と同じ顔で残ると、どちらなのかを画面が
    ///       言えない
    /// @note **«まだ» には戻さない**（`SizeState::Stopped`）。自動で数える設定では、
    ///       次の 1 フレームがそのまま «数えてくれ» になってしまう
    void ResetInFlight();

    /// @brief その木に関わる結果をすべて捨てる。
    /// @param[in] path 変わったフォルダまたは項目のパス
    /// @note 捨てるのは «その下» と «その上» の両方 ─ 下のフォルダが変われば
    ///       上の合計も変わる。判定は `path::IsInside` 1 か所に任せる
    ///       （文字列の前方一致で書くと `alpha` と `alpha2` が親子になる）
    void ForgetRelated(const std::string& path);

    /// @brief 監視の通知に応じて捨てる。数えたばかりの値は残す。
    /// @param[in] path 変化が届いたフォルダ
    /// @param[in] nowMs 今の時刻（`plat::NowMs()`）
    /// @param[in] keepNewerThanMs これより新しく数えた値は残す
    /// @note 残した値は «1 回ぶん古い» ことがある ─ そのフォルダへの書き込みが
    ///       止まれば、次の通知は必ず間隔より後に届くので数え直される。
    ///       `F5` はこの間隔を通らない（`ForgetRelated`）
    void ForgetChanged(const std::string& path, uint64_t nowMs, uint64_t keepNewerThanMs);

    /// @brief 表を空にする。
    void Clear();

    /// @brief 表の上限を設定する。
    /// @param[in] capacity 覚えておくフォルダの最大数。1 未満は 1 に丸める
    void SetCapacity(size_t capacity);

    /// @brief 覚えているフォルダの数を返す。
    /// @return 登録件数
    size_t size() const { return entries_.size(); }

    /// @brief 数え中のフォルダの数を返す。
    /// @return 件数
    size_t countingCount() const { return counting_; }

private:
    struct Entry_ {
        FolderSize value;
        uint64_t used = 0;
    };

    void EvictIfNeeded();
    void Forget(const std::string& path, uint64_t nowMs, uint64_t keepNewerThanMs);

    std::unordered_map<std::string, Entry_> entries_;
    uint64_t tick_ = 0;
    size_t counting_ = 0;
    size_t capacity_ = 4096;
};

/// @brief フォルダのサイズをバックグラウンドで数えるワーカー。
///
/// 依頼はフォルダ 1 つずつ。**1 回の歩きで一覧の全フォルダを数える形にしていない**
/// のは、そうすると全部の子が最後まで «途中» のままになり、確定が全部同時に来る
/// から ─ 子ごとに歩けば、1 つずつ «確定» が増えていく。
class FolderSizeJob {
public:
    /// @brief ワーカースレッドを起動する。
    /// @param[in] fsys 列挙に使うファイルシステム。本オブジェクトより長生きすること
    /// @param[in] wake 完了通知先。本オブジェクトより長生きすること
    /// @param[in] workers ワーカースレッド数。1 未満を渡した場合は 1 に丸める
    FolderSizeJob(IFileSystem& fsys, IWakeSink& wake, int workers = kFolderSizeWorkers);

    /// @brief ワーカースレッドの停止を待って破棄する。
    ~FolderSizeJob();

    FolderSizeJob(const FolderSizeJob&) = delete;
    FolderSizeJob& operator=(const FolderSizeJob&) = delete;

    /// @brief フォルダを 1 つ数えるよう依頼する。
    /// @param[in] path 数えるフォルダ
    /// @note 同じパスが既に待ち行列にいる（または歩いている）なら何もしない ─
    ///       表が忘れた直後に描画が頼み直すことがあり、二重に歩けば同じ木を
    ///       2 度読むだけになる
    void Request(const std::string& path);

    /// @brief 走っているものと待っているものをすべて畳む。
    /// @note 歩いている最中のものが止まるのはフォルダの切れ目。以後届く結果は
    ///       古い «代» のものなので、`epoch()` と食い違う答えは捨てること
    void CancelAll();

    /// @brief 届いている結果をすべて取り出す。
    /// @param[out] out 取り出した結果の追加先。既存の要素は保持される
    /// @note UI スレッドからのみ呼ぶこと
    void Drain(std::vector<FolderSizeUpdate>& out);

    /// @brief 今の «代» を返す。
    /// @return 代の番号。`CancelAll()` のたびに増える
    uint64_t epoch() const { return epoch_.load(std::memory_order_relaxed); }

    /// @brief 数えているもの・待っているものがあるかを返す。
    /// @return あれば true
    bool busy() const { return pending_.load(std::memory_order_relaxed) > 0; }

private:
    struct Job {
        uint64_t epoch = 0;
        std::string path;
    };

    void WorkerMain();
    void Count(const Job& job);
    void Publish(FolderSizeUpdate update);

    IFileSystem& fs_;
    IWakeSink& wake_;

    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<Job> queue_;
    // 待ち行列と «今歩いているもの» の両方。同じパスを 2 度歩かないためだけに持つ。
    std::vector<std::string> claimed_;
    std::vector<FolderSizeUpdate> done_;
    bool stop_ = false;

    std::atomic<uint64_t> epoch_{ 1 };
    std::atomic<int> pending_{ 0 };
    std::vector<std::thread> threads_;
};

}  // namespace kite::fs
