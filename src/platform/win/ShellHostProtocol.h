/// @file
/// @brief kite.exe と kite_shellhost.exe が交換するメッセージの書式。
///
/// 種別は 2 つ。コンテキストメニューの表示要求と、シェルアイコンの取得要求。
/// どちらもサードパーティの DLL（メニューハンドラ、アイコンオーバーレイ
/// ハンドラ）を動かすため、同じ隔離の仕組みに相乗りしている。
///
/// このヘッダは Windows ヘッダに依存しない。tests/test_hostproto.cpp が
/// `kite_core` だけをリンクした状態でこれを直接 include して検証するので、
/// ここに `<windows.h>` を持ち込むとテストのビルドが壊れる。core / ui のレイヤ
/// 分離をテストが守っているのと同じ仕掛けで、この書式の独立性を守っている。
///
/// 復号側は**別プロセスから来たバイト列**を読む。壊れていることも、悪意ある
/// 内容であることも前提に、長さは必ず検査してから使うこと。

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace kite::shellhost {

/// @brief フレーム先頭に置く識別子。
///
/// 書式を変えたら必ずこの値も変えること。版の食い違う kite_shellhost.exe が
/// 残っていた場合に、誤って解釈するのではなく握手の時点で失敗させるためにある。
inline constexpr uint32_t kMagic = 0x34485348u;  // 'H','S','H','4'

/// @brief フレーム本体の上限バイト数。
///
/// 壊れた長さを信じて巨大な確保をしないための歯止め。260 文字のパスなら
/// 1 万件を超えても収まる。
inline constexpr uint32_t kMaxPayload = 4u * 1024u * 1024u;

/// @brief フレーム先頭にある固定長ヘッダ（識別子 4 バイト + 本体長 4 バイト）。
inline constexpr size_t kHeaderSize = 8;

/// @brief 一覧に載せられるパスの上限。
///
/// これを超える選択でメニューを出そうとしてもシェル側が実用にならないため、
/// 復号を拒否して打ち切る。
inline constexpr uint32_t kMaxPaths = 65536;

/// @brief 1 回の要求で頼めるアイコンの数。
///
/// 画面に見えている行の分しか要求しないので、これで十分足りる。バッチにする
/// 理由はパイプ往復の回数を減らすため（1 往復 0.2〜0.5 ms を 1 件ずつ払うと、
/// シェル呼び出しそのものより高く付く）。
inline constexpr uint32_t kMaxIconsPerRequest = 256;

/// @brief アイコン 1 辺のピクセル数の上限。
///
/// 壊れた寸法を信じて幅 × 高さ × 4 バイトを確保しないための歯止め。
inline constexpr uint32_t kMaxIconPixels = 256;

/// @brief フレームの種別。本体の先頭に置く。
enum class MessageKind : uint32_t {
    Menu = 1,   ///< コンテキストメニューの表示
    Icons = 2,  ///< シェルアイコンの取得
};

/// @brief メニュー表示の要求。kite.exe → kite_shellhost.exe。
struct Request {
    std::vector<std::string> paths;  ///< 対象のパス列。すべて同じフォルダに属すること
    int32_t screenX = -1;            ///< 表示位置の X。負ならホストがカーソル位置を使う
    int32_t screenY = -1;            ///< 表示位置の Y。負ならホストがカーソル位置を使う
    bool extended = false;           ///< 拡張メニュー（CMF_EXTENDEDVERBS）を出すか

    /// @brief フォルダ自身の「背景」メニューを出すか。
    ///
    /// false なら項目のメニュー（親フォルダから見た `paths` のメニュー）、true なら
    /// エクスプローラーで一覧の余白を右クリックしたときのメニュー。**同じフォルダを
    /// 指していても中身は別物**で、項目側には「そのフォルダを対象に何かする」動詞
    /// （TortoiseGit の「Git クローン」など）が並ぶ。true のとき対象は `paths` の
    /// 先頭 1 件だけで、残りは無視される。
    bool background = false;

    /// @brief ダークテーマでメニューを描くか。
    ///
    /// Kite 側のテーマをそのまま伝える。OS の設定ではなくこちらに従わせるのは、
    /// 明るいデスクトップで Kite だけを暗くしている利用者にとって、メニューだけ
    /// 白く光るのが一番目に付く不整合だから。
    bool dark = false;

    /// @brief 呼び出し側のウィンドウハンドル。0 なら指定なし。
    ///
    /// ホストは自前の隠しウィンドウでメニューを追跡するが、コマンド実行時に
    /// 開くダイアログ（削除の確認、プロパティ、進捗）の親にはこちらを使う。
    /// 隠しウィンドウを親にするとダイアログが Kite の背後に回りうる。
    uint64_t ownerWindow = 0;
};

/// @brief メニュー表示の結果。kite_shellhost.exe → kite.exe。
enum class Result : uint32_t {
    None = 0,     ///< メニューは出たが何も選ばれなかった
    Invoked = 1,  ///< 選ばれた項目をホストが実行した
    Failed = 2,   ///< メニューを構築できなかった
};

/// @brief メニュー表示の応答。
struct Response {
    Result result = Result::Failed;  ///< 結果の種別
};

/// @brief シェルアイコンの取得要求。kite.exe → kite_shellhost.exe。
struct IconRequest {
    std::vector<std::string> paths;  ///< 取得したいパス列。順序が応答と対応する
    uint32_t pixelSize = 16;         ///< 希望する 1 辺のピクセル数。実寸は応答が返す
};

/// @brief アイコン 1 枚分のビットマップ。
///
/// 形式は**乗算済みアルファの BGRA**、上から下へ。Direct2D がそのまま受け取れる
/// 並びで、変換は取得側（ホスト）で済ませている。
struct IconImage {
    uint32_t id = 0;            ///< このビットマップの識別子。接続内で一意
    uint32_t width = 0;         ///< 幅（ピクセル）
    uint32_t height = 0;        ///< 高さ（ピクセル）
    std::vector<uint8_t> bgra;  ///< 画素。width * height * 4 バイト
};

/// @brief シェルアイコンの取得結果。kite_shellhost.exe → kite.exe。
///
/// `ids` は要求したパスと同じ順に並ぶ。0 は「アイコンを取得できなかった」。
/// `images` に入るのは**この接続で初めて現れたビットマップだけ**。同じ見た目の
/// アイコンには同じ id が振られるので、拡張子が同じ 1 万件のフォルダでも実際に
/// 転送されるのは数枚で済む。
struct IconResponse {
    std::vector<uint32_t> ids;      ///< パスごとのビットマップ識別子
    std::vector<IconImage> images;  ///< 新しく現れたビットマップの実体
};

namespace detail {

/// @brief 32 ビット値をリトルエンディアンで追記する。
/// @param[in,out] out 追記先のバイト列
/// @param[in] value 追記する値
/// @note 双方が同じアーキテクチャなので memcpy でも通るが、書式を目で追える
///       ようにバイト順を明示している
inline void PutU32(std::vector<uint8_t>& out, uint32_t value) {
    out.push_back(static_cast<uint8_t>(value & 0xFFu));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFFu));
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xFFu));
    out.push_back(static_cast<uint8_t>((value >> 24) & 0xFFu));
}

/// @brief 64 ビット値をリトルエンディアンで追記する。
/// @param[in,out] out 追記先のバイト列
/// @param[in] value 追記する値
inline void PutU64(std::vector<uint8_t>& out, uint64_t value) {
    PutU32(out, static_cast<uint32_t>(value & 0xFFFFFFFFu));
    PutU32(out, static_cast<uint32_t>(value >> 32));
}

/// @brief 長さ付きの文字列を追記する。
/// @param[in,out] out 追記先のバイト列
/// @param[in] text 追記する文字列（UTF-8）
inline void PutString(std::vector<uint8_t>& out, const std::string& text) {
    PutU32(out, static_cast<uint32_t>(text.size()));
    out.insert(out.end(), text.begin(), text.end());
}

/// @brief バイト列を前から順に読む。境界を越える読み出しは必ず失敗させる。
class Reader {
public:
    /// @brief 読み出し範囲を指定して構築する。
    /// @param[in] data 先頭ポインタ。size が 0 なら nullptr でもよい
    /// @param[in] size 読み出せるバイト数
    Reader(const uint8_t* data, size_t size) : data_(data), size_(size) {}

    /// @brief 32 ビット値を読む。
    /// @param[out] value 読み取った値。失敗時は書き換えない
    /// @return 読めたら true。残りが足りなければ false
    bool U32(uint32_t& value) {
        // Fail() が pos_ を末尾へ進めるので、一度失敗すればこの引き算は 0 になり、
        // 以降の読み出しはすべてここで落ちる。
        if (size_ - pos_ < 4) return Fail();
        value = static_cast<uint32_t>(data_[pos_]) | (static_cast<uint32_t>(data_[pos_ + 1]) << 8) |
                (static_cast<uint32_t>(data_[pos_ + 2]) << 16) |
                (static_cast<uint32_t>(data_[pos_ + 3]) << 24);
        pos_ += 4;
        return true;
    }

    /// @brief 64 ビット値を読む。
    /// @param[out] value 読み取った値。失敗時は書き換えない
    /// @return 読めたら true。残りが足りなければ false
    bool U64(uint64_t& value) {
        uint32_t low = 0;
        uint32_t high = 0;
        if (!U32(low) || !U32(high)) return false;
        value = static_cast<uint64_t>(low) | (static_cast<uint64_t>(high) << 32);
        return true;
    }

    /// @brief 長さ付きの文字列を読む。
    /// @param[out] text 読み取った文字列。失敗時は書き換えない
    /// @return 読めたら true。長さが残りを超えていれば false
    bool String(std::string& text) {
        uint32_t length = 0;
        if (!U32(length)) return false;
        if (length > size_ - pos_) return Fail();
        text.assign(reinterpret_cast<const char*>(data_ + pos_), length);
        pos_ += length;
        return true;
    }

    /// @brief 長さを指定してバイト列を読む。
    /// @param[out] bytes 読み取った内容。失敗時は書き換えない
    /// @param[in] length 読み取るバイト数
    /// @return 読めたら true。残りが足りなければ false
    /// @note 長さが別に符号化されている画素データ用。長さの妥当性（画素数との
    ///       一致）は呼び出し側が先に検査すること
    bool Bytes(std::string& bytes, size_t length) {
        if (length > size_ - pos_) return Fail();
        bytes.assign(reinterpret_cast<const char*>(data_ + pos_), length);
        pos_ += length;
        return true;
    }

    /// @brief 読み残しがないか調べる。
    /// @return 末尾まで読み切っていて、かつ一度も失敗していなければ true
    bool AtEnd() const { return !failed_ && pos_ == size_; }

private:
    /// @brief 失敗を記録して以降の読み出しを全て失敗させる。
    /// @return 常に false
    bool Fail() {
        failed_ = true;
        pos_ = size_;
        return false;
    }

    const uint8_t* data_;
    size_t size_;
    size_t pos_ = 0;
    bool failed_ = false;
};

}  // namespace detail

/// @brief 完成した 1 フレームを組み立てる。
/// @param[in] payload フレーム本体
/// @return 識別子と長さを前置したバイト列。本体が上限を超える場合は空
inline std::vector<uint8_t> Frame(const std::vector<uint8_t>& payload) {
    if (payload.size() > kMaxPayload) return {};
    std::vector<uint8_t> out;
    out.reserve(kHeaderSize + payload.size());
    detail::PutU32(out, kMagic);
    detail::PutU32(out, static_cast<uint32_t>(payload.size()));
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

/// @brief フレームヘッダを検査し、本体の長さを取り出す。
/// @param[in] header 少なくとも kHeaderSize バイトある先頭部分
/// @param[in] size `header` から読めるバイト数
/// @param[out] payloadSize 本体のバイト数が入る
/// @return 識別子が一致し、長さが上限以内なら true
inline bool ParseHeader(const uint8_t* header, size_t size, uint32_t& payloadSize) {
    if (size < kHeaderSize) return false;
    detail::Reader reader(header, kHeaderSize);
    uint32_t magic = 0;
    uint32_t length = 0;
    if (!reader.U32(magic) || !reader.U32(length)) return false;
    if (magic != kMagic || length > kMaxPayload) return false;
    payloadSize = length;
    return true;
}

/// @brief フレーム本体の先頭にある種別を読む。
/// @param[in] payload フレーム本体の先頭（ヘッダを除いた部分）
/// @param[in] size 本体のバイト数
/// @param[out] kind 読み取った種別
/// @return 既知の種別が読めたら true
/// @note 本体を消費しない。復号の前にどの Decode を呼ぶか決めるために使う
inline bool DecodeKind(const uint8_t* payload, size_t size, MessageKind& kind) {
    detail::Reader reader(payload, size);
    uint32_t value = 0;
    if (!reader.U32(value)) return false;
    if (value != static_cast<uint32_t>(MessageKind::Menu) &&
        value != static_cast<uint32_t>(MessageKind::Icons)) {
        return false;
    }
    kind = static_cast<MessageKind>(value);
    return true;
}

namespace detail {

/// @brief 種別を検査して読み飛ばす。
/// @param[in,out] reader 読み取り位置
/// @param[in] expected 期待する種別
/// @return 一致すれば true
inline bool ExpectKind(Reader& reader, MessageKind expected) {
    uint32_t value = 0;
    if (!reader.U32(value)) return false;
    return value == static_cast<uint32_t>(expected);
}

}  // namespace detail

/// @brief 要求を 1 フレームに符号化する。
/// @param[in] request 送る要求
/// @return 送信できるバイト列。上限を超える場合は空
inline std::vector<uint8_t> EncodeRequest(const Request& request) {
    std::vector<uint8_t> payload;
    detail::PutU32(payload, static_cast<uint32_t>(MessageKind::Menu));
    detail::PutU32(payload, static_cast<uint32_t>(request.screenX));
    detail::PutU32(payload, static_cast<uint32_t>(request.screenY));
    detail::PutU32(payload, request.extended ? 1u : 0u);
    detail::PutU32(payload, request.background ? 1u : 0u);
    detail::PutU32(payload, request.dark ? 1u : 0u);
    detail::PutU64(payload, request.ownerWindow);
    detail::PutU32(payload, static_cast<uint32_t>(request.paths.size()));
    for (const std::string& path : request.paths) detail::PutString(payload, path);
    return Frame(payload);
}

/// @brief 要求の本体を復号する。
/// @param[in] payload フレーム本体の先頭（ヘッダを除いた部分）
/// @param[in] size 本体のバイト数
/// @param[out] request 復号結果。失敗時の内容は不定
/// @return 完全に復号でき、余分なバイトも残っていなければ true
inline bool DecodeRequest(const uint8_t* payload, size_t size, Request& request) {
    detail::Reader reader(payload, size);
    if (!detail::ExpectKind(reader, MessageKind::Menu)) return false;

    uint32_t x = 0;
    uint32_t y = 0;
    uint32_t extended = 0;
    uint32_t background = 0;
    uint32_t dark = 0;
    uint32_t count = 0;
    if (!reader.U32(x) || !reader.U32(y) || !reader.U32(extended)) return false;
    if (!reader.U32(background) || !reader.U32(dark)) return false;
    if (!reader.U64(request.ownerWindow) || !reader.U32(count)) return false;
    if (count > kMaxPaths) return false;

    request.screenX = static_cast<int32_t>(x);
    request.screenY = static_cast<int32_t>(y);
    request.extended = extended != 0;
    request.background = background != 0;
    request.dark = dark != 0;
    request.paths.clear();
    // reserve は count を信じた確保になる。kMaxPaths で上限を掛けた後に行うこと。
    request.paths.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        std::string path;
        if (!reader.String(path)) return false;
        request.paths.push_back(std::move(path));
    }
    return reader.AtEnd();
}

/// @brief 応答を 1 フレームに符号化する。
/// @param[in] response 送る応答
/// @return 送信できるバイト列
inline std::vector<uint8_t> EncodeResponse(const Response& response) {
    std::vector<uint8_t> payload;
    detail::PutU32(payload, static_cast<uint32_t>(MessageKind::Menu));
    detail::PutU32(payload, static_cast<uint32_t>(response.result));
    return Frame(payload);
}

/// @brief 応答の本体を復号する。
/// @param[in] payload フレーム本体の先頭（ヘッダを除いた部分）
/// @param[in] size 本体のバイト数
/// @param[out] response 復号結果。失敗時の内容は不定
/// @return 完全に復号でき、既知の結果種別であれば true
inline bool DecodeResponse(const uint8_t* payload, size_t size, Response& response) {
    detail::Reader reader(payload, size);
    if (!detail::ExpectKind(reader, MessageKind::Menu)) return false;

    uint32_t result = 0;
    if (!reader.U32(result) || !reader.AtEnd()) return false;
    if (result > static_cast<uint32_t>(Result::Failed)) return false;
    response.result = static_cast<Result>(result);
    return true;
}

/// @brief アイコン取得の要求を 1 フレームに符号化する。
/// @param[in] request 送る要求
/// @return 送信できるバイト列。件数が上限を超える場合は空
inline std::vector<uint8_t> EncodeIconRequest(const IconRequest& request) {
    if (request.paths.size() > kMaxIconsPerRequest) return {};
    std::vector<uint8_t> payload;
    detail::PutU32(payload, static_cast<uint32_t>(MessageKind::Icons));
    detail::PutU32(payload, request.pixelSize);
    detail::PutU32(payload, static_cast<uint32_t>(request.paths.size()));
    for (const std::string& path : request.paths) detail::PutString(payload, path);
    return Frame(payload);
}

/// @brief アイコン取得の要求を復号する。
/// @param[in] payload フレーム本体の先頭（ヘッダを除いた部分）
/// @param[in] size 本体のバイト数
/// @param[out] request 復号結果。失敗時の内容は不定
/// @return 完全に復号でき、余分なバイトも残っていなければ true
inline bool DecodeIconRequest(const uint8_t* payload, size_t size, IconRequest& request) {
    detail::Reader reader(payload, size);
    if (!detail::ExpectKind(reader, MessageKind::Icons)) return false;

    uint32_t count = 0;
    if (!reader.U32(request.pixelSize) || !reader.U32(count)) return false;
    if (count > kMaxIconsPerRequest) return false;
    if (request.pixelSize == 0 || request.pixelSize > kMaxIconPixels) return false;

    request.paths.clear();
    request.paths.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        std::string path;
        if (!reader.String(path)) return false;
        request.paths.push_back(std::move(path));
    }
    return reader.AtEnd();
}

/// @brief アイコン取得の結果を 1 フレームに符号化する。
/// @param[in] response 送る結果
/// @return 送信できるバイト列。上限を超える場合は空
inline std::vector<uint8_t> EncodeIconResponse(const IconResponse& response) {
    if (response.ids.size() > kMaxIconsPerRequest) return {};
    if (response.images.size() > kMaxIconsPerRequest) return {};

    std::vector<uint8_t> payload;
    detail::PutU32(payload, static_cast<uint32_t>(MessageKind::Icons));
    detail::PutU32(payload, static_cast<uint32_t>(response.ids.size()));
    for (uint32_t id : response.ids) detail::PutU32(payload, id);

    detail::PutU32(payload, static_cast<uint32_t>(response.images.size()));
    for (const IconImage& image : response.images) {
        if (image.width > kMaxIconPixels || image.height > kMaxIconPixels) return {};
        if (image.bgra.size() != static_cast<size_t>(image.width) * image.height * 4) return {};
        detail::PutU32(payload, image.id);
        detail::PutU32(payload, image.width);
        detail::PutU32(payload, image.height);
        detail::PutU32(payload, static_cast<uint32_t>(image.bgra.size()));
        payload.insert(payload.end(), image.bgra.begin(), image.bgra.end());
    }
    return Frame(payload);
}

/// @brief アイコン取得の結果を復号する。
/// @param[in] payload フレーム本体の先頭（ヘッダを除いた部分）
/// @param[in] size 本体のバイト数
/// @param[out] response 復号結果。失敗時の内容は不定
/// @return 完全に復号でき、余分なバイトも残っていなければ true
/// @note 画素数と実バイト数の食い違いは必ず弾く。ここを信じると、壊れたフレーム 1 つで
///       描画側が確保していないメモリを読みに行くことになる
inline bool DecodeIconResponse(const uint8_t* payload, size_t size, IconResponse& response) {
    detail::Reader reader(payload, size);
    if (!detail::ExpectKind(reader, MessageKind::Icons)) return false;

    uint32_t idCount = 0;
    if (!reader.U32(idCount) || idCount > kMaxIconsPerRequest) return false;
    response.ids.clear();
    response.ids.reserve(idCount);
    for (uint32_t i = 0; i < idCount; ++i) {
        uint32_t id = 0;
        if (!reader.U32(id)) return false;
        response.ids.push_back(id);
    }

    uint32_t imageCount = 0;
    if (!reader.U32(imageCount) || imageCount > kMaxIconsPerRequest) return false;
    response.images.clear();
    response.images.reserve(imageCount);
    for (uint32_t i = 0; i < imageCount; ++i) {
        IconImage image;
        uint32_t bytes = 0;
        if (!reader.U32(image.id) || !reader.U32(image.width) || !reader.U32(image.height)) {
            return false;
        }
        if (!reader.U32(bytes)) return false;
        if (image.width == 0 || image.height == 0) return false;
        if (image.width > kMaxIconPixels || image.height > kMaxIconPixels) return false;
        if (bytes != static_cast<uint32_t>(image.width) * image.height * 4) return false;

        std::string pixels;
        if (!reader.Bytes(pixels, bytes)) return false;
        image.bgra.assign(pixels.begin(), pixels.end());
        response.images.push_back(std::move(image));
    }
    return reader.AtEnd();
}

}  // namespace kite::shellhost
