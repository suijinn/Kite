#include "platform/win/FolderHostClient.h"

#include "platform/win/ShellPipe.h"

namespace kite::win {
namespace {

// Generous, because "Network" is in this set: discovering what is on the LAN
// depends on services that may be off, and the shell takes its own time to
// decide there is nothing there - 15 seconds is a measured, successful answer
// on a quiet home network. What the limit rules out is the case nothing else
// can recover from: a namespace extension that never returns. A hung one is
// just as hung at 20 seconds as at 30, and the extra 10 are what keep a slow
// but working answer from being thrown away.
constexpr DWORD kListTimeoutMs = 30000;

}  // namespace

bool FolderHostClient::List(const std::string& parsingName, shellhost::FolderResponse& response) {
    if (parsingName.empty()) return false;
    std::lock_guard<std::mutex> lock(mutex_);

    // No pump: this runs on a loader worker, which owns no windows and so has
    // nothing to keep painting while it waits.
    if (!host_.Ensure(nullptr, nullptr)) return false;

    shellhost::FolderRequest request;
    request.path = parsingName;
    const std::vector<uint8_t> frame = shellhost::EncodeFolderRequest(request);
    if (frame.empty()) return false;

    if (WritePipeFrame(host_.pipe(), frame, nullptr, nullptr) != PipeStatus::Ok) {
        host_.Stop();
        return false;
    }

    std::vector<uint8_t> payload;
    if (ReadPipeFrame(host_.pipe(), payload, kListTimeoutMs, nullptr, nullptr) != PipeStatus::Ok) {
        // Timed out, or the host died with a faulting extension inside it. The
        // connection is out of step either way: drop it, and let the next
        // listing start a fresh host.
        host_.Stop();
        return false;
    }
    if (!shellhost::DecodeFolderResponse(payload.data(), payload.size(), response)) {
        host_.Stop();
        return false;
    }
    return true;
}

void FolderHostClient::Stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    host_.Stop();
}

}  // namespace kite::win
