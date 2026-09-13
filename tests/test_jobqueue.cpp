// The skeleton the four background queues are built on: lazy start, one result
// or many per job, Pick deciding what may run next, Clear dropping what has not
// started. Each of those used to be written out once per queue.
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

#include "TestFramework.h"
#include "core/fs/JobQueue.h"

using namespace kite;

namespace {

struct CountingSink : fs::IWakeSink {
    std::atomic<int> wakes{ 0 };
    void Wake() override { wakes.fetch_add(1, std::memory_order_relaxed); }
};

// A latch a job can be parked on, so "two of these ran at once" is decided by
// the test rather than by how the scheduler happened to feel.
struct Gate {
    std::mutex mutex;
    std::condition_variable cv;
    bool open = false;
    int waiting = 0;

    void Wait() {
        std::unique_lock<std::mutex> lock(mutex);
        ++waiting;
        cv.notify_all();
        cv.wait(lock, [this] { return open; });
    }

    void Open() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            open = true;
        }
        cv.notify_all();
    }

    // Gives up rather than hanging the suite if the count is never reached.
    bool WaitForWaiting(int n) {
        std::unique_lock<std::mutex> lock(mutex);
        return cv.wait_for(lock, std::chrono::seconds(5), [this, n] { return waiting >= n; });
    }
};

// Spins until the queue has nothing left, the way the UI thread's pump does.
template <class Q>
void Settle(Q& queue) {
    for (int i = 0; i < 2000 && queue.busy(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

}  // namespace

KITE_TEST(jobqueue, no_thread_is_started_until_the_first_request) {
    CountingSink sink;
    fs::JobQueue<int, int> queue(sink, 2, [](const int& job, const auto& emit) { emit(job * 2); });

    // The whole point of the lazy start: a session that never searches, never
    // right-clicks and never counts a folder pays for none of those threads.
    KITE_EXPECT_FALSE(queue.started());
    KITE_EXPECT_FALSE(queue.busy());

    queue.Request(21);
    KITE_EXPECT(queue.started());

    Settle(queue);
    std::vector<int> out;
    queue.Drain(out);
    KITE_EXPECT_EQ(out.size(), size_t{ 1 });
    KITE_EXPECT_EQ(out[0], 42);
    KITE_EXPECT(sink.wakes.load() >= 1);
}

KITE_TEST(jobqueue, a_job_may_emit_any_number_of_results) {
    CountingSink sink;
    // The walkers do exactly this: batches as they go, then a final one. Fixing
    // it at one result per job is what would keep them off this skeleton.
    fs::JobQueue<int, int> queue(sink, 1, [](const int& job, const auto& emit) {
        for (int i = 0; i < job; ++i) emit(i);
    });

    queue.Request(3);
    queue.Request(0);  // and none at all is allowed too
    Settle(queue);

    std::vector<int> out;
    queue.Drain(out);
    KITE_EXPECT_EQ(out.size(), size_t{ 3 });
    KITE_EXPECT_EQ(queue.pending(), 0);
}

KITE_TEST(jobqueue, pick_decides_what_runs_next_and_can_hold_a_job_back) {
    CountingSink sink;
    std::mutex order;
    std::vector<int> ran;

    // Only even jobs may start, and never more than the one at the front - which
    // is the shape of FileOpQueue's conflict test, without the paths.
    fs::JobQueue<int, int> queue(
        sink, 2,
        [&](const int& job, const auto& emit) {
            {
                std::lock_guard<std::mutex> lock(order);
                ran.push_back(job);
            }
            emit(job);
        },
        [](const std::deque<int>& q, const std::vector<const int*>&) {
            for (size_t i = 0; i < q.size(); ++i) {
                if (q[i] % 2 == 0) return i;
            }
            return q.size();
        });

    queue.Request(1);  // odd: never runnable
    queue.Request(2);
    queue.Request(4);

    // The two even ones finish; the odd one is still pending and always will be.
    for (int i = 0; i < 2000 && queue.pending() > 1; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    KITE_EXPECT_EQ(queue.pending(), 1);

    std::lock_guard<std::mutex> lock(order);
    KITE_EXPECT_EQ(ran.size(), size_t{ 2 });
    for (int job : ran) KITE_EXPECT_EQ(job % 2, 0);
}

KITE_TEST(jobqueue, pick_sees_what_is_already_running) {
    CountingSink sink;
    Gate gate;
    std::atomic<int> started{ 0 };

    // One at a time, decided by what Pick is told is running - the guarantee
    // FileOpQueue leans on to keep two operations out of the same folder.
    fs::JobQueue<int, int> queue(
        sink, 4,
        [&](const int& job, const auto& emit) {
            started.fetch_add(1, std::memory_order_relaxed);
            gate.Wait();
            emit(job);
        },
        [](const std::deque<int>& q, const std::vector<const int*>& running) {
            return running.empty() ? size_t{ 0 } : q.size();
        });

    queue.Request(1);
    queue.Request(2);
    queue.Request(3);

    KITE_EXPECT(gate.WaitForWaiting(1));
    // Long enough that a second worker would have taken one by now if Pick were
    // not being consulted with the running list.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    KITE_EXPECT_EQ(started.load(), 1);
    KITE_EXPECT_EQ(queue.running(), 1);
    KITE_EXPECT_EQ(queue.pending(), 3);

    // Opening it lets all three through, one after another.
    gate.Open();
    Settle(queue);
    KITE_EXPECT_EQ(started.load(), 3);
    KITE_EXPECT_EQ(queue.pending(), 0);
}

KITE_TEST(jobqueue, clear_drops_what_has_not_started_and_leaves_what_has) {
    CountingSink sink;
    Gate gate;
    std::atomic<int> started{ 0 };

    fs::JobQueue<int, int> queue(sink, 1, [&](const int& job, const auto& emit) {
        started.fetch_add(1, std::memory_order_relaxed);
        gate.Wait();
        emit(job);
    });

    queue.Request(1);
    KITE_EXPECT(gate.WaitForWaiting(1));
    queue.Request(2);
    queue.Request(3);
    KITE_EXPECT_EQ(queue.pending(), 3);

    // Two were only queued; the one in flight is not something Clear can take
    // back - stopping that is the caller's own signal (the search token, the
    // folder-size epoch), checked at a point the job chooses.
    KITE_EXPECT_EQ(queue.Clear(), 2);
    KITE_EXPECT_EQ(queue.pending(), 1);

    gate.Open();
    Settle(queue);
    KITE_EXPECT_EQ(started.load(), 1);

    std::vector<int> out;
    queue.Drain(out);
    KITE_EXPECT_EQ(out.size(), size_t{ 1 });
    KITE_EXPECT_EQ(out[0], 1);
}

KITE_TEST(jobqueue, workers_run_side_by_side_when_nothing_holds_them_back) {
    CountingSink sink;
    Gate gate;

    fs::JobQueue<int, int> queue(sink, 3, [&](const int& job, const auto& emit) {
        gate.Wait();
        emit(job);
    });

    queue.Request(1);
    queue.Request(2);
    queue.Request(3);

    // Counted at the gate, not by which finished first: timing-based answers
    // fail on a busy machine rather than on a real regression.
    KITE_EXPECT(gate.WaitForWaiting(3));
    KITE_EXPECT_EQ(queue.running(), 3);

    gate.Open();
    Settle(queue);
    KITE_EXPECT_EQ(queue.pending(), 0);
}

KITE_TEST(jobqueue, destruction_waits_for_what_is_running) {
    CountingSink sink;
    Gate gate;
    std::atomic<bool> finished{ false };

    // Opened from another thread, because the destructor below will not return
    // until the job does - which is the thing being checked.
    std::thread opener([&] {
        gate.WaitForWaiting(1);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        gate.Open();
    });

    {
        fs::JobQueue<int, int> queue(sink, 1, [&](const int&, const auto&) {
            gate.Wait();
            finished.store(true);
        });
        queue.Request(1);
        KITE_EXPECT(gate.WaitForWaiting(1));
    }
    // Past the closing brace, so the destructor has joined.
    KITE_EXPECT(finished.load());
    opener.join();
}
