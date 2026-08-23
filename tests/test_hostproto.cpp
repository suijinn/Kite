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

KITE_TEST(hostproto, a_folder_listing_round_trips) {
    FolderResponse sent;
    sent.status = FolderStatus::Ok;
    sent.title = "PC";

    FolderEntry drive;
    drive.name = "Windows (C:)";
    drive.parsing = "C:\\";
    drive.attrs = static_cast<uint32_t>(FolderAttr::Directory) |
                  static_cast<uint32_t>(FolderAttr::FileSystem);
    sent.entries.push_back(drive);

    FolderEntry ext;
    ext.name = "iCloud";
    ext.parsing = "::{AAA}\\::{BBB}";
    ext.size = 1234;
    ext.mtime = -5;  // before 1970, and the sign has to survive
    ext.attrs = static_cast<uint32_t>(FolderAttr::Directory);
    sent.entries.push_back(ext);

    const std::vector<uint8_t> frame = EncodeFolderResponse(sent);
    KITE_EXPECT_FALSE(frame.empty());

    FolderResponse got;
    KITE_EXPECT(DecodeFolderResponse(Body(frame), BodySize(frame), got));
    KITE_EXPECT(got.status == FolderStatus::Ok);
    KITE_EXPECT_EQ(got.title, std::string("PC"));
    KITE_EXPECT_EQ(got.entries.size(), size_t{ 2 });
    KITE_EXPECT_EQ(got.entries[0].parsing, std::string("C:\\"));
    KITE_EXPECT_EQ(got.entries[1].mtime, int64_t{ -5 });
    KITE_EXPECT_EQ(got.entries[1].size, uint64_t{ 1234 });
    KITE_EXPECT_EQ(got.entries[1].attrs, static_cast<uint32_t>(FolderAttr::Directory));
}

KITE_TEST(hostproto, a_folder_request_round_trips_and_is_told_apart) {
    FolderRequest sent;
    sent.path = "shell:RecycleBinFolder";
    const std::vector<uint8_t> frame = EncodeFolderRequest(sent);

    MessageKind kind = MessageKind::Menu;
    KITE_EXPECT(DecodeKind(Body(frame), BodySize(frame), kind));
    KITE_EXPECT(kind == MessageKind::Folder);

    FolderRequest got;
    KITE_EXPECT(DecodeFolderRequest(Body(frame), BodySize(frame), got));
    KITE_EXPECT_EQ(got.path, std::string("shell:RecycleBinFolder"));

    // The other decoders must not accept it: a frame read as the wrong kind is
    // how two versions of the format quietly disagree.
    Request menu;
    KITE_EXPECT_FALSE(DecodeRequest(Body(frame), BodySize(frame), menu));
    IconRequest icons;
    KITE_EXPECT_FALSE(DecodeIconRequest(Body(frame), BodySize(frame), icons));
}

KITE_TEST(hostproto, a_folder_response_refuses_an_impossible_count) {
    // The count comes from another process, and reserve() would believe it.
    std::vector<uint8_t> body;
    detail::PutU32(body, static_cast<uint32_t>(MessageKind::Folder));
    detail::PutU32(body, static_cast<uint32_t>(FolderStatus::Ok));
    detail::PutString(body, "");
    detail::PutString(body, "");
    detail::PutU32(body, kMaxFolderEntries + 1);

    FolderResponse got;
    KITE_EXPECT_FALSE(DecodeFolderResponse(body.data(), body.size(), got));

    // A status nobody defined is refused too.
    std::vector<uint8_t> bad;
    detail::PutU32(bad, static_cast<uint32_t>(MessageKind::Folder));
    detail::PutU32(bad, 99);
    KITE_EXPECT_FALSE(DecodeFolderResponse(bad.data(), bad.size(), got));
}

KITE_TEST(hostproto, a_menu_request_carries_the_folder_its_items_came_from) {
    Request sent;
    sent.container = "::{645FF040-5081-101B-9F08-00AA002F954E}";
    sent.paths = { "C:\\$Recycle.Bin\\S-1-5-21\\$R0L38Q5.ini" };
    sent.dark = true;

    const std::vector<uint8_t> frame = EncodeRequest(sent);
    Request got;
    KITE_EXPECT(DecodeRequest(Body(frame), BodySize(frame), got));
    KITE_EXPECT_EQ(got.container, sent.container);
    KITE_EXPECT_EQ(got.paths.size(), size_t{ 1 });
    KITE_EXPECT_EQ(got.paths[0], sent.paths[0]);
    KITE_EXPECT(got.dark);

    // An empty container is the ordinary case and has to survive as empty: it
    // is what tells the host to parse the paths instead of enumerating.
    Request plain;
    plain.paths = { "C:\\a.txt" };
    const std::vector<uint8_t> plainFrame = EncodeRequest(plain);
    Request plainGot;
    KITE_EXPECT(DecodeRequest(Body(plainFrame), BodySize(plainFrame), plainGot));
    KITE_EXPECT(plainGot.container.empty());
}

KITE_TEST(hostproto, a_verb_request_round_trips_and_is_told_apart) {
    VerbRequest sent;
    sent.container = "::{645FF040-5081-101B-9F08-00AA002F954E}";
    sent.paths = { "C:\\$Recycle.Bin\\$R1.ini", "C:\\$Recycle.Bin\\$R2.ini" };
    sent.verb = "undelete";
    sent.ownerWindow = 0x123456789ABCDEFull;

    const std::vector<uint8_t> frame = EncodeVerbRequest(sent);
    MessageKind kind = MessageKind::Menu;
    KITE_EXPECT(DecodeKind(Body(frame), BodySize(frame), kind));
    KITE_EXPECT(kind == MessageKind::Verb);

    VerbRequest got;
    KITE_EXPECT(DecodeVerbRequest(Body(frame), BodySize(frame), got));
    KITE_EXPECT_EQ(got.verb, std::string("undelete"));
    KITE_EXPECT_EQ(got.container, sent.container);
    KITE_EXPECT_EQ(got.paths.size(), size_t{ 2 });
    KITE_EXPECT_EQ(got.ownerWindow, sent.ownerWindow);

    // Reading a frame as the wrong kind is how two versions quietly disagree.
    Request menu;
    KITE_EXPECT_FALSE(DecodeRequest(Body(frame), BodySize(frame), menu));
    FolderRequest folder;
    KITE_EXPECT_FALSE(DecodeFolderRequest(Body(frame), BodySize(frame), folder));
}

KITE_TEST(hostproto, a_verb_response_round_trips) {
    VerbResponse sent;
    sent.ok = true;
    sent.applied = 3;
    const std::vector<uint8_t> frame = EncodeVerbResponse(sent);

    VerbResponse got;
    KITE_EXPECT(DecodeVerbResponse(Body(frame), BodySize(frame), got));
    KITE_EXPECT(got.ok);
    KITE_EXPECT_EQ(got.applied, uint32_t{ 3 });

    VerbResponse failed;
    const std::vector<uint8_t> failedFrame = EncodeVerbResponse(VerbResponse{});
    KITE_EXPECT(DecodeVerbResponse(Body(failedFrame), BodySize(failedFrame), failed));
    KITE_EXPECT_FALSE(failed.ok);
}


KITE_TEST(hostproto, an_extract_request_round_trips) {
    ExtractRequest sent;
    sent.container = "C:\\Users\\me\\写真.zip\\中身";
    sent.path = "C:\\Users\\me\\写真.zip\\中身\\メモ.txt";
    const std::vector<uint8_t> frame = EncodeExtractRequest(sent);

    ExtractRequest got;
    KITE_EXPECT(DecodeExtractRequest(Body(frame), BodySize(frame), got));
    KITE_EXPECT_EQ(got.container, sent.container);
    KITE_EXPECT_EQ(got.path, sent.path);

    // Reading a frame as the wrong kind is how two versions quietly disagree.
    VerbRequest verb;
    KITE_EXPECT_FALSE(DecodeVerbRequest(Body(frame), BodySize(frame), verb));
    FolderRequest folder;
    KITE_EXPECT_FALSE(DecodeFolderRequest(Body(frame), BodySize(frame), folder));
}

KITE_TEST(hostproto, an_extract_response_round_trips) {
    ExtractResponse sent;
    sent.ok = true;
    sent.path = "C:\\Users\\me\\AppData\\Local\\Temp\\Kite\\A1B2C3D4\\メモ.txt";
    const std::vector<uint8_t> frame = EncodeExtractResponse(sent);

    ExtractResponse got;
    KITE_EXPECT(DecodeExtractResponse(Body(frame), BodySize(frame), got));
    KITE_EXPECT(got.ok);
    KITE_EXPECT_EQ(got.path, sent.path);

    // A failure carries no path: there is nothing for the caller to open, and a
    // leftover one would be opened anyway.
    const std::vector<uint8_t> failedFrame = EncodeExtractResponse(ExtractResponse{});
    ExtractResponse failed;
    KITE_EXPECT(DecodeExtractResponse(Body(failedFrame), BodySize(failedFrame), failed));
    KITE_EXPECT_FALSE(failed.ok);
    KITE_EXPECT(failed.path.empty());
}

KITE_TEST(hostproto, every_kind_the_encoders_write_is_a_kind_the_host_accepts) {
    // The host reads DecodeKind first and hangs up on anything it does not
    // know. A kind added to the enum and to the encoders but not to that list is
    // refused before it reaches its own decoder - which looks from Kite like the
    // operation quietly failing, and did: opening a file inside an archive
    // answered "could not open this item" with nothing else wrong.
    const std::vector<std::vector<uint8_t>> frames = {
        EncodeRequest(Request{}),
        EncodeIconRequest(IconRequest{}),
        EncodeFolderRequest(FolderRequest{}),
        EncodeVerbRequest(VerbRequest{}),
        EncodeExtractRequest(ExtractRequest{}),
    };
    const MessageKind expected[] = { MessageKind::Menu, MessageKind::Icons,
                                     MessageKind::Folder, MessageKind::Verb,
                                     MessageKind::Extract };
    for (size_t i = 0; i < frames.size(); ++i) {
        KITE_EXPECT(!frames[i].empty());
        MessageKind kind = MessageKind::Menu;
        KITE_EXPECT(DecodeKind(Body(frames[i]), BodySize(frames[i]), kind));
        KITE_EXPECT_EQ(static_cast<uint32_t>(kind), static_cast<uint32_t>(expected[i]));
    }

    // And nothing beyond them: an unknown kind is how two versions disagree.
    std::vector<uint8_t> unknown = frames.back();
    unknown[kHeaderSize] = 99;
    MessageKind kind = MessageKind::Menu;
    KITE_EXPECT_FALSE(DecodeKind(Body(unknown), BodySize(unknown), kind));
}
