#include "platform/win/ShellPipe.h"

#include "platform/win/ShellHostProtocol.h"

namespace kite::win {
namespace {

// How often the wait wakes up on its own so `pump` runs even when no message
// arrives. Long enough to be free, short enough that a caller polling for
// "should I give up?" reacts without a visible delay.
constexpr DWORD kPumpIntervalMs = 100;

/// Owns an OVERLAPPED and its event, and makes sure a cancelled operation has
/// really finished before the structure dies.
///
/// This is the same trap as ReadDirectoryChangesW: letting an OVERLAPPED go out
/// of scope with the I/O still in flight hands the kernel a dangling pointer to
/// write into later.
class Operation {
public:
    Operation() { overlapped_.hEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr); }

    ~Operation() {
        if (pending_) {
            ::CancelIoEx(pipe_, &overlapped_);
            DWORD transferred = 0;
            ::GetOverlappedResult(pipe_, &overlapped_, &transferred, TRUE);
        }
        if (overlapped_.hEvent) ::CloseHandle(overlapped_.hEvent);
    }

    Operation(const Operation&) = delete;
    Operation& operator=(const Operation&) = delete;

    bool valid() const { return overlapped_.hEvent != nullptr; }
    HANDLE event() const { return overlapped_.hEvent; }
    OVERLAPPED* get() { return &overlapped_; }

    void Begin(HANDLE pipe) {
        pipe_ = pipe;
        pending_ = true;
        ::ResetEvent(overlapped_.hEvent);
    }

    void Finish() { pending_ = false; }

private:
    OVERLAPPED overlapped_{};
    HANDLE pipe_ = nullptr;
    bool pending_ = false;
};

/// Waits for one pending operation, running `pump` while it waits.
PipeStatus Await(HANDLE pipe, Operation& op, DWORD timeoutMs, PipePump pump, void* context,
                 DWORD* transferred) {
    const ULONGLONG start = ::GetTickCount64();
    for (;;) {
        DWORD slice = kPumpIntervalMs;
        if (timeoutMs != INFINITE) {
            const ULONGLONG elapsed = ::GetTickCount64() - start;
            if (elapsed >= timeoutMs) return PipeStatus::Timeout;
            const DWORD remaining = static_cast<DWORD>(timeoutMs - elapsed);
            if (remaining < slice) slice = remaining;
        }

        // Deliberately without MWMO_INPUTAVAILABLE. A caller's pump may leave
        // messages in the queue on purpose (kite.exe keeps its own async
        // notifications for after the menu closes), and with that flag an
        // unremoved message would make every wait return at once - a busy loop.
        // Only genuinely new input wakes us; the slice covers the rest.
        const HANDLE handles[1] = { op.event() };
        const DWORD wait = ::MsgWaitForMultipleObjects(1, handles, FALSE, slice, QS_ALLINPUT);
        if (wait == WAIT_OBJECT_0) break;
        if (wait == WAIT_FAILED) return PipeStatus::Closed;
        // Either a message arrived or the slice expired; both are pump points.
        if (pump && !pump(context)) return PipeStatus::Aborted;
    }

    op.Finish();
    if (!::GetOverlappedResult(pipe, op.get(), transferred, FALSE)) return PipeStatus::Closed;
    return PipeStatus::Ok;
}

/// Reads exactly `size` bytes, however many reads that takes.
PipeStatus ReadExactly(HANDLE pipe, uint8_t* buffer, size_t size, DWORD timeoutMs, PipePump pump,
                       void* context) {
    size_t done = 0;
    while (done < size) {
        Operation op;
        if (!op.valid()) return PipeStatus::Closed;
        op.Begin(pipe);

        DWORD transferred = 0;
        const DWORD want = static_cast<DWORD>(size - done);
        if (!::ReadFile(pipe, buffer + done, want, &transferred, op.get())) {
            if (::GetLastError() != ERROR_IO_PENDING) return PipeStatus::Closed;
            const PipeStatus status = Await(pipe, op, timeoutMs, pump, context, &transferred);
            if (status != PipeStatus::Ok) return status;
        } else {
            op.Finish();
        }
        if (transferred == 0) return PipeStatus::Closed;
        done += transferred;
    }
    return PipeStatus::Ok;
}

}  // namespace

PipeStatus WritePipeFrame(HANDLE pipe, const std::vector<uint8_t>& frame, PipePump pump,
                          void* context) {
    if (pipe == INVALID_HANDLE_VALUE || frame.empty()) return PipeStatus::Closed;

    size_t done = 0;
    while (done < frame.size()) {
        Operation op;
        if (!op.valid()) return PipeStatus::Closed;
        op.Begin(pipe);

        DWORD transferred = 0;
        const DWORD want = static_cast<DWORD>(frame.size() - done);
        if (!::WriteFile(pipe, frame.data() + done, want, &transferred, op.get())) {
            if (::GetLastError() != ERROR_IO_PENDING) return PipeStatus::Closed;
            // A write that the peer is not reading must not wedge us forever.
            const PipeStatus status = Await(pipe, op, 30000, pump, context, &transferred);
            if (status != PipeStatus::Ok) return status;
        } else {
            op.Finish();
        }
        if (transferred == 0) return PipeStatus::Closed;
        done += transferred;
    }
    return PipeStatus::Ok;
}

PipeStatus ReadPipeFrame(HANDLE pipe, std::vector<uint8_t>& payload, DWORD timeoutMs, PipePump pump,
                         void* context) {
    if (pipe == INVALID_HANDLE_VALUE) return PipeStatus::Closed;

    uint8_t header[shellhost::kHeaderSize] = {};
    PipeStatus status = ReadExactly(pipe, header, sizeof(header), timeoutMs, pump, context);
    if (status != PipeStatus::Ok) return status;

    uint32_t size = 0;
    // A bad magic means the stream is out of sync or the peer is a different
    // build. There is no way to resynchronize, so drop the connection.
    if (!shellhost::ParseHeader(header, sizeof(header), size)) return PipeStatus::Closed;

    payload.assign(size, 0);
    if (size == 0) return PipeStatus::Ok;

    // The body follows immediately, so this leg gets its own bounded wait rather
    // than the caller's (possibly infinite) one.
    status = ReadExactly(pipe, payload.data(), payload.size(), 30000, pump, context);
    return status;
}

PipeStatus WaitForPipeClient(HANDLE pipe, DWORD timeoutMs, PipePump pump, void* context) {
    if (pipe == INVALID_HANDLE_VALUE) return PipeStatus::Closed;

    Operation op;
    if (!op.valid()) return PipeStatus::Closed;
    op.Begin(pipe);

    if (::ConnectNamedPipe(pipe, op.get())) {
        op.Finish();
        return PipeStatus::Ok;
    }
    const DWORD error = ::GetLastError();
    if (error == ERROR_PIPE_CONNECTED) {
        // The client got there first; the connection is already established.
        op.Finish();
        return PipeStatus::Ok;
    }
    if (error != ERROR_IO_PENDING) return PipeStatus::Closed;

    DWORD transferred = 0;
    return Await(pipe, op, timeoutMs, pump, context, &transferred);
}

}  // namespace kite::win
