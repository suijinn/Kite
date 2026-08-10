// The wire format between kite.exe and kite_shellhost.exe.
//
// Two things are being checked. The obvious one is that a request survives the
// round trip. The important one is that the decoder refuses damaged input: it
// parses bytes written by another process, and that process is the one expected
// to crash. Reading a length it has not verified is how "the shell host died"
// would turn into "Kite died too".
//
// This suite also keeps the format honest about its dependencies. It links
// kite_core only, so the moment ShellMenuProtocol.h includes a Windows header
// this file stops compiling.
#include "TestFramework.h"
#include "platform/win/ShellMenuProtocol.h"

using namespace kite::shellhost;

namespace {

/// Strips the frame header, the way the pipe layer does before decoding.
const uint8_t* Body(const std::vector<uint8_t>& frame) {
    return frame.data() + kHeaderSize;
}
size_t BodySize(const std::vector<uint8_t>& frame) {
    return frame.size() - kHeaderSize;
}

}  // namespace

KITE_TEST(shellproto, a_request_survives_the_round_trip) {
    Request sent;
    sent.paths = { "C:\\home\\a.txt", "C:\\home\\写真.png", "\\\\server\\share\\b" };
    sent.screenX = 1234;
    sent.screenY = 56;
    sent.extended = true;
    sent.ownerWindow = 0x00000001DEADBEEFull;

    const std::vector<uint8_t> frame = EncodeRequest(sent);
    KITE_EXPECT(frame.size() > kHeaderSize);

    uint32_t declared = 0;
    KITE_EXPECT(ParseHeader(frame.data(), frame.size(), declared));
    KITE_EXPECT_EQ(size_t{ declared }, BodySize(frame));

    Request got;
    KITE_EXPECT(DecodeRequest(Body(frame), BodySize(frame), got));
    KITE_EXPECT_EQ(got.paths.size(), size_t{ 3 });
    KITE_EXPECT_EQ(got.paths[1], std::string("C:\\home\\写真.png"));
    KITE_EXPECT_EQ(got.paths[2], std::string("\\\\server\\share\\b"));
    KITE_EXPECT_EQ(got.screenX, 1234);
    KITE_EXPECT_EQ(got.screenY, 56);
    KITE_EXPECT(got.extended);
    KITE_EXPECT_EQ(got.ownerWindow, 0x00000001DEADBEEFull);
}

KITE_TEST(shellproto, negative_coordinates_mean_use_the_cursor_and_must_survive) {
    Request sent;
    sent.paths = { "C:\\home" };
    sent.screenX = -1;
    sent.screenY = -1;

    const std::vector<uint8_t> frame = EncodeRequest(sent);
    Request got;
    KITE_EXPECT(DecodeRequest(Body(frame), BodySize(frame), got));
    KITE_EXPECT_EQ(got.screenX, -1);
    KITE_EXPECT_EQ(got.screenY, -1);
    KITE_EXPECT_FALSE(got.extended);
}

KITE_TEST(shellproto, a_response_survives_the_round_trip) {
    for (const Result result : { Result::None, Result::Invoked, Result::Failed }) {
        Response sent;
        sent.result = result;
        const std::vector<uint8_t> frame = EncodeResponse(sent);

        Response got;
        KITE_EXPECT(DecodeResponse(Body(frame), BodySize(frame), got));
        KITE_EXPECT_EQ(static_cast<int>(got.result), static_cast<int>(result));
    }
}

KITE_TEST(shellproto, a_foreign_magic_is_refused) {
    std::vector<uint8_t> frame = EncodeRequest(Request{});
    frame[0] ^= 0xFF;  // a host from a different build

    uint32_t declared = 0;
    KITE_EXPECT_FALSE(ParseHeader(frame.data(), frame.size(), declared));
}

KITE_TEST(shellproto, a_short_header_is_refused) {
    const std::vector<uint8_t> frame = EncodeRequest(Request{});
    uint32_t declared = 0;
    for (size_t size = 0; size < kHeaderSize; ++size) {
        KITE_EXPECT_FALSE(ParseHeader(frame.data(), size, declared));
    }
}

KITE_TEST(shellproto, an_oversized_length_is_refused_before_anything_is_allocated) {
    std::vector<uint8_t> frame = EncodeRequest(Request{});
    // Claim 4 GB of body. Believing this is how a corrupt stream turns into an
    // out-of-memory abort.
    frame[4] = 0xFF;
    frame[5] = 0xFF;
    frame[6] = 0xFF;
    frame[7] = 0xFF;

    uint32_t declared = 0;
    KITE_EXPECT_FALSE(ParseHeader(frame.data(), frame.size(), declared));
}

KITE_TEST(shellproto, a_truncated_body_is_refused_at_every_length) {
    Request sent;
    sent.paths = { "C:\\home\\a.txt", "C:\\home\\b.txt" };
    const std::vector<uint8_t> frame = EncodeRequest(sent);

    for (size_t size = 0; size < BodySize(frame); ++size) {
        Request got;
        KITE_EXPECT_FALSE(DecodeRequest(Body(frame), size, got));
    }
    Request got;
    KITE_EXPECT(DecodeRequest(Body(frame), BodySize(frame), got));
}

KITE_TEST(shellproto, trailing_bytes_are_refused) {
    const std::vector<uint8_t> frame = EncodeRequest(Request{});
    std::vector<uint8_t> body(Body(frame), Body(frame) + BodySize(frame));
    body.push_back(0);

    Request got;
    KITE_EXPECT_FALSE(DecodeRequest(body.data(), body.size(), got));

    std::vector<uint8_t> response = EncodeResponse(Response{});
    response.push_back(0);
    Response gotResponse;
    KITE_EXPECT_FALSE(
        DecodeResponse(response.data() + kHeaderSize, response.size() - kHeaderSize, gotResponse));
}

KITE_TEST(shellproto, an_absurd_path_count_is_refused_without_reserving_for_it) {
    // The count is used for reserve(), so it has to be bounded before it is
    // trusted - otherwise four bytes from a broken pipe ask for gigabytes.
    std::vector<uint8_t> body;
    detail::PutU32(body, 0);            // screenX
    detail::PutU32(body, 0);            // screenY
    detail::PutU32(body, 0);            // extended
    detail::PutU64(body, 0);            // ownerWindow
    detail::PutU32(body, 0xFFFFFFFFu);  // pathCount

    Request got;
    KITE_EXPECT_FALSE(DecodeRequest(body.data(), body.size(), got));
}

KITE_TEST(shellproto, a_path_length_running_past_the_end_is_refused) {
    std::vector<uint8_t> body;
    detail::PutU32(body, 0);
    detail::PutU32(body, 0);
    detail::PutU32(body, 0);
    detail::PutU64(body, 0);
    detail::PutU32(body, 1);     // one path follows
    detail::PutU32(body, 4096);  // ... claiming 4096 bytes
    body.push_back('C');         // ... but only one is here

    Request got;
    KITE_EXPECT_FALSE(DecodeRequest(body.data(), body.size(), got));
}

KITE_TEST(shellproto, an_unknown_result_code_is_refused) {
    std::vector<uint8_t> body;
    detail::PutU32(body, 99);

    Response got;
    KITE_EXPECT_FALSE(DecodeResponse(body.data(), body.size(), got));
}

KITE_TEST(shellproto, an_empty_selection_encodes_and_decodes) {
    // kite.exe never sends this, but the decoder must not treat "no paths" as a
    // reason to walk off the end.
    const std::vector<uint8_t> frame = EncodeRequest(Request{});
    Request got;
    KITE_EXPECT(DecodeRequest(Body(frame), BodySize(frame), got));
    KITE_EXPECT(got.paths.empty());
}

KITE_TEST(shellproto, a_path_holding_arbitrary_bytes_survives) {
    // Paths are length-prefixed, not NUL-terminated, so nothing in the middle of
    // one may end it early.
    Request sent;
    sent.paths = { std::string("C:\\a\0b", 6), std::string(1, '\xFF') };

    const std::vector<uint8_t> frame = EncodeRequest(sent);
    Request got;
    KITE_EXPECT(DecodeRequest(Body(frame), BodySize(frame), got));
    KITE_EXPECT_EQ(got.paths.size(), size_t{ 2 });
    KITE_EXPECT_EQ(got.paths[0].size(), size_t{ 6 });
    KITE_EXPECT_EQ(got.paths[0], std::string("C:\\a\0b", 6));
    KITE_EXPECT_EQ(got.paths[1], std::string(1, '\xFF'));
}
