#include "platform/win/IconHostClient.h"

#include "platform/win/ShellPipe.h"

namespace kite::win {
namespace {

// A batch of icons is a few dozen shell calls. Cold, on a network share or a
// repository the overlay cache has not seen yet, that can genuinely take a
// second or two - so the limit is generous. What it rules out is the case that
// cannot be recovered from any other way: a handler that never returns.
constexpr DWORD kFetchTimeoutMs = 8000;

}  // namespace

bool IconHostClient::Fetch(const std::vector<std::string>& paths, uint32_t pixelSize,
                           shellhost::IconResponse& response, unsigned& generation) {
    if (paths.empty()) return false;
    // No pump: this runs on a worker thread that owns no windows, so there is
    // nothing to keep painting while it waits.
    if (!host_.Ensure(nullptr, nullptr)) return false;
    // Read after Ensure: a host that had exited on its idle timer is replaced
    // there, and the replacement hands out ids starting from 1 again.
    generation = host_.generation();

    shellhost::IconRequest request;
    request.paths = paths;
    request.pixelSize = pixelSize;

    const std::vector<uint8_t> frame = shellhost::EncodeIconRequest(request);
    if (frame.empty()) return false;

    if (WritePipeFrame(host_.pipe(), frame, nullptr, nullptr) != PipeStatus::Ok) {
        host_.Stop();
        return false;
    }

    std::vector<uint8_t> payload;
    if (ReadPipeFrame(host_.pipe(), payload, kFetchTimeoutMs, nullptr, nullptr) != PipeStatus::Ok) {
        // Timed out, or the host died with a faulting overlay handler inside it.
        // Either way the connection is out of step now: drop it and let the next
        // batch start a fresh host.
        host_.Stop();
        return false;
    }
    if (!shellhost::DecodeIconResponse(payload.data(), payload.size(), response)) {
        host_.Stop();
        return false;
    }
    return true;
}

}  // namespace kite::win
