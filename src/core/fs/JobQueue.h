/// @file
/// @brief 「依頼を積み、ワーカーが 1 つずつ処理し、UI スレッドが回収する」骨格。
///
/// 列挙・検索・フォルダのサイズ・ファイル操作、そしてシェルアイコンの 5 つは、
/// どれも同じ形をしている ─ `mutex + condition_variable + 待ち行列 + 結果の箱 +
/// 停止フラグ + スレッド列`、そして `Request` / `Drain` / `busy`。違うのは
/// **依頼と結果の中身**と、**次にどれを走らせてよいか**の 2 つだけで、それ以外は
/// 5 回書かれていた。骨格を 1 つにしておけば、止め方も数え方も直すのは 1 回で済む。
///
/// **ワーカーは最初の依頼まで作らない。** 右クリックもアイコンも検索もしない
/// 起動でスレッドが 9 本立っているのは、その全部が «いつか使うかもしれない» から
/// でしかなかった。`WinIconProvider` がすでにそうしていたことを既定にしてある。
///
/// **本数と «次にどれを» の規則は各クラスのもの。** 検索が 1 本、サイズが 2 本、
/// ファイル操作が 4 本なのにはそれぞれ別の理由があり（各ヘッダに書いてある）、
/// 共有プールにはしない ─ `SHFileOperation` はスレッドを占有するので、他と
/// 混ぜられない。

#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace kite::fs {

/// @brief ワーカーから UI スレッドを起こすための通知先。
class IWakeSink {
public:
    virtual ~IWakeSink() = default;

    /// @brief UI スレッドに処理待ちがあることを伝える。
    /// @note ワーカースレッドから呼ばれる。スレッド安全に実装すること
    virtual void Wake() = 0;
};

/// @brief 非同期ワーカーの骨格。
///
/// @tparam Job 依頼 1 件。ムーブできること
/// @tparam Result 結果 1 件。ムーブできること
///
/// @note `Request()` と `Drain()` は UI スレッドから呼ぶ。`Run` はワーカー
///       スレッドで走るので、そこから触るものはスレッド安全でなければならない
template <class Job, class Result>
class JobQueue {
public:
    /// @brief ワーカーから UI スレッドへ結果を 1 件渡す口。
    ///
    /// **0 回でも何回でも呼んでよい。** 列挙とファイル操作は 1 回だけ呼ぶが、
    /// 検索とサイズは歩きながら途中経過を流す ─ 「1 依頼 = 1 結果」に固定すると、
    /// その 2 つがこの骨格に乗らない。呼ぶたびに `IWakeSink::Wake()` が飛ぶ。
    using Emit = std::function<void(Result)>;

    /// @brief 依頼を 1 つ処理する。ワーカースレッド上で呼ばれる。
    using Run = std::function<void(const Job&, const Emit&)>;

    /// @brief 次に走らせてよい依頼を選ぶ。
    ///
    /// 返すのは `queue` への添字で、走らせてよいものが無ければ `queue.size()`。
    /// 既定（空の `Pick`）は常に先頭。`FileOpQueue` だけがここに衝突判定を置く。
    /// `running` に並ぶのは、今ワーカーが握っている依頼への参照。
    using Pick = std::function<size_t(const std::deque<Job>& queue,
                                      const std::vector<const Job*>& running)>;

    /// @brief 骨格を作る。**スレッドはまだ作らない。**
    /// @param[in] wake 結果が届いたことの通知先。本オブジェクトより長生きすること
    /// @param[in] workers ワーカーの本数。1 未満は 1 に丸める
    /// @param[in] run 依頼 1 件を処理するもの
    /// @param[in] pick 次に走らせてよい依頼を選ぶもの。省略すると常に先頭
    /// @param[in] wakeOnStart 依頼を取った時点でも UI を起こすか。**ファイル操作
    ///            だけが要る** ─ «待機中» が «実行中» に変わるのは画面の答えが
    ///            変わることなので、誰も再描画を頼まなければ古い件数が残る
    JobQueue(IWakeSink& wake, int workers, Run run, Pick pick = {}, bool wakeOnStart = false)
        : wake_(wake), workers_(workers < 1 ? 1 : workers), run_(std::move(run)),
          pick_(std::move(pick)), wakeOnStart_(wakeOnStart) {}

    /// @brief ワーカーの停止を待って破棄する。
    /// @note 走っている依頼は**最後まで走る** ─ 途中で止められるかどうかは
    ///       `Run` の中身しだいで、骨格からは分からない。派生側が «自分から
    ///       抜ける» 合図（検索のトークン、サイズの代）を先に立ててから Stop()
    ///       を呼ぶこと
    ~JobQueue() { Stop(); }

    JobQueue(const JobQueue&) = delete;
    JobQueue& operator=(const JobQueue&) = delete;

    /// @brief 依頼を積む。最初の 1 件でワーカーを起動する。
    /// @param[in] job 積む依頼
    /// @note **トークンは配らない。** 依頼を識別する値が要るクラスは自分で採番し、
    ///       `Job` に入れて渡す ─ 検索はワーカーが走り出す «前» にその値を
    ///       `active_` へ立てておく必要があり、積んでから受け取るのでは間に合わない
    void Request(Job job) {
        pending_.fetch_add(1, std::memory_order_relaxed);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.push_back(std::move(job));
            EnsureWorkers();
        }
        cv_.notify_one();
    }

    /// @brief 積んであるだけの依頼を捨てる。走っているものには触れない。
    /// @return 捨てた件数
    /// @note 走っている依頼を止める合図は骨格の外 ─ 検索は `active_`、サイズは
    ///       `epoch_` で、どちらもフォルダの切れ目で自分から抜ける
    int Clear() {
        int dropped = 0;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            dropped = static_cast<int>(queue_.size());
            queue_.clear();
        }
        if (dropped > 0) pending_.fetch_sub(dropped, std::memory_order_relaxed);
        return dropped;
    }

    /// @brief 届いている結果をすべて取り出す。
    /// @param[out] out 取り出した結果の追加先。既存の要素は保持される
    /// @note UI スレッドからのみ呼ぶこと
    void Drain(std::vector<Result>& out) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (done_.empty()) return;
        for (Result& r : done_) out.push_back(std::move(r));
        done_.clear();
    }

    /// @brief 未完了の依頼があるかを返す。
    /// @return 実行中または処理待ちのものがあれば true
    bool busy() const { return pending() > 0; }

    /// @brief 未完了の依頼の数を返す。
    /// @return 実行中のものを含む、まだ終わっていない依頼の数
    int pending() const { return pending_.load(std::memory_order_relaxed); }

    /// @brief いま実際に走っている依頼の数を返す。
    /// @return ワーカーが握っている依頼の数。待っているだけのものは含まない
    int running() const { return runningCount_.load(std::memory_order_relaxed); }

    /// @brief ワーカーを起動済みかを返す。
    /// @return 1 本でも立っていれば true
    /// @note 遅延起動が効いていることをテストが確かめるためにある
    bool started() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return !threads_.empty();
    }

protected:
    /// @brief ワーカーを止めて join する。派生クラスのデストラクタから呼ぶ。
    /// @note 2 度呼んでも安全。派生側が «自分から抜ける» 合図を立ててから
    ///       呼べるように、デストラクタとは別に取り出してある
    void Stop() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_ = true;
            // 積んであるだけのものは捨てる。走っているものは取り返せない。
            queue_.clear();
        }
        cv_.notify_all();
        for (std::thread& t : threads_) {
            if (t.joinable()) t.join();
        }
        threads_.clear();
    }

private:
    // `mutex_` を持った状態で呼ぶこと。
    void EnsureWorkers() {
        if (!threads_.empty() || stop_) return;
        threads_.reserve(static_cast<size_t>(workers_));
        for (int i = 0; i < workers_; ++i) {
            threads_.emplace_back([this] { WorkerMain(); });
        }
    }

    void Publish(Result r) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            done_.push_back(std::move(r));
        }
        wake_.Wake();
    }

    void WorkerMain() {
        const Emit emit = [this](Result r) { Publish(std::move(r)); };
        for (;;) {
            // 依頼はワーカーのスタックに置く ─ `running_` に並ぶのはここへの
            // ポインタなので、走っている間じゅう有効で、外すときの «自分» も
            // 取り違えようが無い。
            Job job;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                size_t index = 0;
                cv_.wait(lock, [this, &index] {
                    if (stop_) return true;
                    if (queue_.empty()) return false;
                    index = pick_ ? pick_(queue_, running_) : 0;
                    return index < queue_.size();
                });
                if (stop_) return;
                job = std::move(queue_[index]);
                queue_.erase(queue_.begin() + static_cast<ptrdiff_t>(index));
                running_.push_back(&job);
                runningCount_.fetch_add(1, std::memory_order_relaxed);
            }

            if (wakeOnStart_) wake_.Wake();

            if (run_) run_(job, emit);

            {
                std::lock_guard<std::mutex> lock(mutex_);
                for (size_t i = 0; i < running_.size(); ++i) {
                    if (running_[i] != &job) continue;
                    running_.erase(running_.begin() + static_cast<ptrdiff_t>(i));
                    break;
                }
                // 場所を手放すのと同じロックの中で数える ─ «何本走っているか» と
                // «どこが埋まっているか» が食い違うと、Pick が嘘の上で選ぶ。
                runningCount_.fetch_sub(1, std::memory_order_relaxed);
                pending_.fetch_sub(1, std::memory_order_relaxed);
            }
            // 全員に。手放した場所は、待っている複数の依頼が揃って待っていた
            // ものでありうる。
            cv_.notify_all();
        }
    }

    IWakeSink& wake_;
    const int workers_;
    Run run_;
    Pick pick_;
    const bool wakeOnStart_;

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<Job> queue_;
    std::vector<const Job*> running_;  ///< 今ワーカーが握っている依頼。Pick が見る
    std::vector<Result> done_;
    bool stop_ = false;

    std::atomic<int> pending_{ 0 };
    std::atomic<int> runningCount_{ 0 };
    std::vector<std::thread> threads_;
};

}  // namespace kite::fs
