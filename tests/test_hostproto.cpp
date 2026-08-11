// The wire format between kite.exe and kite_shellhost.exe.
//
// Two things are being checked. The obvious one is that a message survives the
// round trip. The important one is that the decoder refuses damaged input: it
// parses bytes written by another process, and that process is the one expected
// to crash. Reading a length it has not verified is how "the shell host died"
// would turn into "Kite died too". The icon frames raise the stakes - they carry
// raw pixels, so a pixel count that does not match the byte count has to be
// caught here rather than in the renderer.
//
// This suite also keeps the format honest about its dependencies. It links
// kite_core only, so the moment ShellHostProtocol.h includes a Windows header
// this file stops compiling.
#include "TestFramework.h"
#include "platform/win/ShellHostProtocol.h"

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

KITE_TEST(hostproto, a_request_survives_the_round_trip) {
    Request sent;
    sent.paths = { "C:\\home\\a.txt", "C:\\home\\写真.png", "\\\\server\\share\\b" };
    sent.screenX = 1234;
    sent.screenY = 56;
    sent.extended = true;
    sent.background = true;
    sent.dark = true;
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
    KITE_EXPECT(got.background);
    KITE_EXPECT(got.dark);
    KITE_EXPECT_EQ(got.ownerWindow, 0x00000001DEADBEEFull);
}

KITE_TEST(hostproto, the_background_flag_travels_on_its_own) {
    // Three single bits share this frame. A decoder that reads them in the wrong
    // order still round-trips whichever one is tested alone, and getting this one
    // wrong means the host builds a menu from the other shell object entirely.
    Request sent;
    sent.paths = { "C:\\home" };
    sent.extended = false;
    sent.background = true;
    sent.dark = false;

    const std::vector<uint8_t> frame = EncodeRequest(sent);
    Request got;
    KITE_EXPECT(DecodeRequest(Body(frame), BodySize(frame), got));
    KITE_EXPECT_FALSE(got.extended);
    KITE_EXPECT(got.background);
    KITE_EXPECT_FALSE(got.dark);
}

KITE_TEST(hostproto, the_theme_flag_travels_independently_of_the_extended_flag) {
    // Both are single bits in the same frame; a decoder that mixes them up would
    // still round-trip whichever one happened to be tested alone.
    Request sent;
    sent.paths = { "C:\\home" };
    sent.extended = true;
    sent.dark = false;

    const std::vector<uint8_t> frame = EncodeRequest(sent);
    Request got;
    KITE_EXPECT(DecodeRequest(Body(frame), BodySize(frame), got));
    KITE_EXPECT(got.extended);
    KITE_EXPECT_FALSE(got.dark);
}

KITE_TEST(hostproto, negative_coordinates_mean_use_the_cursor_and_must_survive) {
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

KITE_TEST(hostproto, a_response_survives_the_round_trip) {
    for (const Result result : { Result::None, Result::Invoked, Result::Failed }) {
        Response sent;
        sent.result = result;
        const std::vector<uint8_t> frame = EncodeResponse(sent);

        Response got;
        KITE_EXPECT(DecodeResponse(Body(frame), BodySize(frame), got));
        KITE_EXPECT_EQ(static_cast<int>(got.result), static_cast<int>(result));
    }
}

KITE_TEST(hostproto, a_foreign_magic_is_refused) {
    std::vector<uint8_t> frame = EncodeRequest(Request{});
    frame[0] ^= 0xFF;  // a host from a different build

    uint32_t declared = 0;
    KITE_EXPECT_FALSE(ParseHeader(frame.data(), frame.size(), declared));
}

KITE_TEST(hostproto, a_short_header_is_refused) {
    const std::vector<uint8_t> frame = EncodeRequest(Request{});
    uint32_t declared = 0;
    for (size_t size = 0; size < kHeaderSize; ++size) {
        KITE_EXPECT_FALSE(ParseHeader(frame.data(), size, declared));
    }
}

KITE_TEST(hostproto, an_oversized_length_is_refused_before_anything_is_allocated) {
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

KITE_TEST(hostproto, a_truncated_body_is_refused_at_every_length) {
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

KITE_TEST(hostproto, trailing_bytes_are_refused) {
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

KITE_TEST(hostproto, an_absurd_path_count_is_refused_without_reserving_for_it) {
    // The count is used for reserve(), so it has to be bounded before it is
    // trusted - otherwise four bytes from a broken pipe ask for gigabytes.
    std::vector<uint8_t> body;
    detail::PutU32(body, static_cast<uint32_t>(MessageKind::Menu));
    detail::PutU32(body, 0);            // screenX
    detail::PutU32(body, 0);            // screenY
    detail::PutU32(body, 0);            // extended
    detail::PutU32(body, 0);            // background
    detail::PutU32(body, 0);            // dark
    detail::PutU64(body, 0);            // ownerWindow
    detail::PutU32(body, 0xFFFFFFFFu);  // pathCount

    Request got;
    KITE_EXPECT_FALSE(DecodeRequest(body.data(), body.size(), got));
}

KITE_TEST(hostproto, a_path_length_running_past_the_end_is_refused) {
    std::vector<uint8_t> body;
    detail::PutU32(body, static_cast<uint32_t>(MessageKind::Menu));
    detail::PutU32(body, 0);
    detail::PutU32(body, 0);
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

KITE_TEST(hostproto, an_unknown_result_code_is_refused) {
    std::vector<uint8_t> body;
    detail::PutU32(body, static_cast<uint32_t>(MessageKind::Menu));
    detail::PutU32(body, 99);

    Response got;
    KITE_EXPECT_FALSE(DecodeResponse(body.data(), body.size(), got));
}

KITE_TEST(hostproto, an_empty_selection_encodes_and_decodes) {
    // kite.exe never sends this, but the decoder must not treat "no paths" as a
    // reason to walk off the end.
    const std::vector<uint8_t> frame = EncodeRequest(Request{});
    Request got;
    KITE_EXPECT(DecodeRequest(Body(frame), BodySize(frame), got));
    KITE_EXPECT(got.paths.empty());
}

// --- icons ------------------------------------------------------------------

namespace {

/// A plausible icon: every byte distinct enough to catch a stride mistake.
IconImage MakeImage(uint32_t id, uint32_t w, uint32_t h) {
    IconImage image;
    image.id = id;
    image.width = w;
    image.height = h;
    image.bgra.resize(static_cast<size_t>(w) * h * 4);
    for (size_t i = 0; i < image.bgra.size(); ++i) {
        image.bgra[i] = static_cast<uint8_t>((i * 7 + id) & 0xFF);
    }
    return image;
}

}  // namespace

KITE_TEST(hostproto, an_icon_request_survives_the_round_trip) {
    IconRequest sent;
    sent.paths = { "C:\\home\\a.txt", "C:\\repo\\写真.png" };
    sent.pixelSize = 32;

    const std::vector<uint8_t> frame = EncodeIconRequest(sent);
    IconRequest got;
    KITE_EXPECT(DecodeIconRequest(Body(frame), BodySize(frame), got));
    KITE_EXPECT_EQ(got.pixelSize, 32u);
    KITE_EXPECT_EQ(got.paths.size(), size_t{ 2 });
    KITE_EXPECT_EQ(got.paths[1], std::string("C:\\repo\\写真.png"));
}

KITE_TEST(hostproto, an_icon_response_survives_the_round_trip) {
    IconResponse sent;
    // Three paths, two distinct bitmaps: the third path reuses the first icon,
    // which is the whole point of sending ids separately from pixels.
    sent.ids = { 1, 2, 1 };
    sent.images = { MakeImage(1, 16, 16), MakeImage(2, 32, 32) };

    const std::vector<uint8_t> frame = EncodeIconResponse(sent);
    IconResponse got;
    KITE_EXPECT(DecodeIconResponse(Body(frame), BodySize(frame), got));
    KITE_EXPECT_EQ(got.ids.size(), size_t{ 3 });
    KITE_EXPECT_EQ(got.ids[2], 1u);
    KITE_EXPECT_EQ(got.images.size(), size_t{ 2 });
    KITE_EXPECT_EQ(got.images[1].width, 32u);
    KITE_EXPECT_EQ(got.images[1].bgra.size(), size_t{ 32 * 32 * 4 });
    KITE_EXPECT_EQ(got.images[0].bgra, sent.images[0].bgra);
}

KITE_TEST(hostproto, an_icon_that_could_not_be_read_is_carried_as_id_zero) {
    IconResponse sent;
    sent.ids = { 0, 0 };

    const std::vector<uint8_t> frame = EncodeIconResponse(sent);
    IconResponse got;
    KITE_EXPECT(DecodeIconResponse(Body(frame), BodySize(frame), got));
    KITE_EXPECT_EQ(got.ids.size(), size_t{ 2 });
    KITE_EXPECT_EQ(got.ids[0], 0u);
    KITE_EXPECT(got.images.empty());
}

KITE_TEST(hostproto, a_pixel_count_that_disagrees_with_the_byte_count_is_refused) {
    // The dangerous one. If this got through, the renderer would upload a 32x32
    // bitmap out of a buffer holding four pixels.
    std::vector<uint8_t> body;
    detail::PutU32(body, static_cast<uint32_t>(MessageKind::Icons));
    detail::PutU32(body, 1);  // one id
    detail::PutU32(body, 1);
    detail::PutU32(body, 1);      // one image
    detail::PutU32(body, 1);      // id
    detail::PutU32(body, 32);     // width
    detail::PutU32(body, 32);     // height
    detail::PutU32(body, 16);     // ... but only 16 bytes of pixels
    body.resize(body.size() + 16);

    IconResponse got;
    KITE_EXPECT_FALSE(DecodeIconResponse(body.data(), body.size(), got));
}

KITE_TEST(hostproto, an_oversized_icon_is_refused_before_anything_is_allocated) {
    std::vector<uint8_t> body;
    detail::PutU32(body, static_cast<uint32_t>(MessageKind::Icons));
    detail::PutU32(body, 0);           // no ids
    detail::PutU32(body, 1);           // one image
    detail::PutU32(body, 1);           // id
    detail::PutU32(body, 0xFFFFu);     // width
    detail::PutU32(body, 0xFFFFu);     // height
    detail::PutU32(body, 0xFFFFFFFFu); // byte count

    IconResponse got;
    KITE_EXPECT_FALSE(DecodeIconResponse(body.data(), body.size(), got));
}

KITE_TEST(hostproto, an_absurd_icon_count_is_refused) {
    std::vector<uint8_t> body;
    detail::PutU32(body, static_cast<uint32_t>(MessageKind::Icons));
    detail::PutU32(body, 0xFFFFFFFFu);  // id count

    IconResponse got;
    KITE_EXPECT_FALSE(DecodeIconResponse(body.data(), body.size(), got));

    IconRequest request;
    request.paths.assign(kMaxIconsPerRequest + 1, "C:\\a");
    KITE_EXPECT(EncodeIconRequest(request).empty());
}

KITE_TEST(hostproto, a_truncated_icon_response_is_refused_at_every_length) {
    IconResponse sent;
    sent.ids = { 1, 1 };
    sent.images = { MakeImage(1, 8, 8) };
    const std::vector<uint8_t> frame = EncodeIconResponse(sent);

    for (size_t size = 0; size < BodySize(frame); ++size) {
        IconResponse got;
        KITE_EXPECT_FALSE(DecodeIconResponse(Body(frame), size, got));
    }
    IconResponse got;
    KITE_EXPECT(DecodeIconResponse(Body(frame), BodySize(frame), got));
}

KITE_TEST(hostproto, each_frame_says_what_it_is_and_the_wrong_reader_refuses_it) {
    // Both kinds travel the same pipe. A menu response decoded as an icon
    // response would otherwise read a result code as an id count.
    const std::vector<uint8_t> menu = EncodeRequest(Request{});
    const std::vector<uint8_t> icons = EncodeIconRequest(IconRequest{});

    MessageKind kind = MessageKind::Icons;
    KITE_EXPECT(DecodeKind(Body(menu), BodySize(menu), kind));
    KITE_EXPECT_EQ(static_cast<int>(kind), static_cast<int>(MessageKind::Menu));
    KITE_EXPECT(DecodeKind(Body(icons), BodySize(icons), kind));
    KITE_EXPECT_EQ(static_cast<int>(kind), static_cast<int>(MessageKind::Icons));

    IconRequest asIcons;
    KITE_EXPECT_FALSE(DecodeIconRequest(Body(menu), BodySize(menu), asIcons));
    Request asMenu;
    KITE_EXPECT_FALSE(DecodeRequest(Body(icons), BodySize(icons), asMenu));

    // An unknown kind from a future build is refused rather than guessed at.
    std::vector<uint8_t> body;
    detail::PutU32(body, 99);
    KITE_EXPECT_FALSE(DecodeKind(body.data(), body.size(), kind));
}

KITE_TEST(hostproto, a_path_holding_arbitrary_bytes_survives) {
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
