#include "platform/win/WinIconProvider.h"

#include <algorithm>

namespace kite::win {
namespace {

// One pipe round trip covers this many icons. Sized to cover a tall window in a
// single batch, so a fresh folder costs one round trip rather than one per row.
constexpr size_t kBatchSize = 64;

}  // namespace

WinIconProvider::WinIconProvider(fs::IWakeSink& wake) : wake_(wake) {}

WinIconProvider::~WinIconProvider() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
    }
    cv_.notify_all();
    if (worker_.joinable()) worker_.join();
}

uint32_t WinIconProvider::IconFor(const std::string& path) { return cache_.Lookup(path); }

void WinIconProvider::Invalidate() {
    // Only the path -> id table goes: the ids themselves are still valid for
    // this connection, so the bitmaps already uploaded get reused and the
    // re-fetch costs one round trip for the rows actually on screen.
    cache_.Invalidate();
}

void WinIconProvider::SetPixelSize(uint32_t pixels) {
    // Only the two sizes the shell actually keeps are worth asking for; anything
    // between them would come back as one of these anyway.
    const uint32_t wanted = pixels <= 16 ? 16u : 32u;
    if (wanted == pixelSize_) return;
    pixelSize_ = wanted;
    requestedSize_.store(wanted, std::memory_order_relaxed);

    // Every id held names a bitmap at the old size, so the table goes. The
    // bitmaps themselves stay: this is the same connection, so the host still
    // has them in its dedup table and will not send them again - and it will
    // hand out their ids again if the size ever comes back to this one.
    cache_.Clear();

    std::lock_guard<std::mutex> lock(mutex_);
    queue_.clear();
    done_.clear();
    images_.clear();
}

void WinIconProvider::Pump(D2DRenderer& renderer) {
    // 1. Take what finished and hand the pixels to the renderer.
    std::vector<std::pair<std::string, uint32_t>> done;
    std::vector<shellhost::IconImage> images;
    std::vector<std::string> abandoned;
    bool stale = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        done.swap(done_);
        images.swap(images_);
        stale = stale_;
        stale_ = false;
        if (failed_) {
            failed_ = false;
            abandoned.swap(inFlight_);
        }
    }

    // A host that exited (idle timeout, or a faulting overlay handler) is
    // replaced silently, and the replacement numbers its bitmaps from 1 again.
    // Everything remembered from the old connection now names a different
    // picture, so it goes before this batch is applied - otherwise rows that
    // were already resolved quietly swap to whichever icon inherited their id.
    //
    // The batch below is the new host's first, so its images are every bitmap it
    // has handed out: dropping ours here and taking those leaves the two sides
    // agreeing again. That agreement is why this cannot be done at any other
    // moment - the host sends a bitmap only the first time it appears, so
    // anything discarded out of step with it is never sent a second time.
    if (stale) {
        cache_.Forget();
        pixels_.clear();
        renderer.ClearIcons();
    }

    for (shellhost::IconImage& image : images) {
        renderer.UploadIcon(image.id, image.width, image.height, image.bgra.data());
        pixels_[image.id] = std::move(image);
    }
    for (const auto& [path, iconId] : done) cache_.Store(path, iconId);
    // A batch that never came back leaves its rows waiting forever otherwise.
    cache_.Reset(abandoned);

    // 2. Put the bitmaps back after a lost device.
    if (renderer.iconsLost()) {
        for (const auto& [id, image] : pixels_) {
            renderer.UploadIcon(id, image.width, image.height, image.bgra.data());
        }
        renderer.ClearIconsLost();
    }

    // 3. Send the next batch.
    std::vector<std::string> wanted;
    cache_.TakePending(wanted, kBatchSize);
    if (wanted.empty()) return;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.insert(queue_.end(), wanted.begin(), wanted.end());
        // Started late on purpose: a session that never shows a file list never
        // pays for the thread, and the host itself waits for the first batch.
        if (!worker_.joinable()) worker_ = std::thread([this] { WorkerMain(); });
    }
    cv_.notify_one();
}

void WinIconProvider::WorkerMain() {
    for (;;) {
        std::vector<std::string> batch;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return stop_ || !queue_.empty(); });
            if (stop_) return;

            const size_t count = std::min(kBatchSize, queue_.size());
            batch.assign(queue_.end() - static_cast<ptrdiff_t>(count), queue_.end());
            queue_.resize(queue_.size() - count);
            inFlight_ = batch;
        }

        shellhost::IconResponse response;
        unsigned generation = 0;
        const bool ok = client_.Fetch(batch, requestedSize_.load(std::memory_order_relaxed),
                                      response, generation);

        {
            std::lock_guard<std::mutex> lock(mutex_);
            // Noted even for a failed fetch: the host was replaced either way,
            // so the ids the UI thread is holding are already meaningless. The
            // first connection is not a replacement and has nothing to drop.
            if (generation != 0 && generation != generation_) {
                stale_ = stale_ || generation_ != 0;
                generation_ = generation;
                // Anything the UI thread has not collected yet was numbered by
                // the connection that just went away. Mixing it into the batch
                // below would put old ids back on new rows, which is the very
                // thing the flush exists to prevent.
                done_.clear();
                images_.clear();
            }
            // A reply of the wrong length is a broken host, not a partial answer.
            // Pairing those ids with these paths would put arbitrary icons on
            // arbitrary rows, so the whole batch is failed instead.
            if (ok && response.ids.size() == batch.size()) {
                for (size_t i = 0; i < batch.size(); ++i) {
                    done_.push_back({ std::move(batch[i]), response.ids[i] });
                }
                for (shellhost::IconImage& image : response.images) {
                    images_.push_back(std::move(image));
                }
                inFlight_.clear();
            } else {
                // Left in inFlight_ so the UI thread can move these paths back to
                // "not asked yet" - otherwise the rows wait on a reply that is
                // never coming.
                failed_ = true;
                inFlight_ = std::move(batch);
            }
        }
        wake_.Wake();
    }
}

}  // namespace kite::win
