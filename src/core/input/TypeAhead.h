/// @file
/// @brief 一覧の型入力ジャンプ（type-ahead）。
///
/// 文字キーを押すと、その名前で始まる行へカーソルが飛ぶ。`Ctrl+F` の絞り込みとは
/// **別の機能**で、行は 1 つも消えない ─ 探すだけのために絞り込むと、見つけた後に
/// 絞り込みを解く手間が要るうえ、着いた先の前後に何があるかが見えない。
///
/// 打った文字は一定時間（kTimeoutMs）で消える。**時計そのものは持たない** ─
/// 現在時刻は呼ぶ側が渡す。OS にも描画にも触れないので、挙動は
/// tests/test_typeahead.cpp が端から端まで検証できる。
///
/// カーソルを動かすのはここではない。`Type()` は飛び先の行を返すだけで、実際に
/// 動かすのは App ─ ここが `Tab` を書き換え始めた時点で、«文字から行を決めるもの»
/// ではなくなる。

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace kite {

/// @brief 型入力ジャンプの状態。打ちかけの文字列と、それが打たれた時刻を持つ。
class TypeAhead {
public:
    /// @brief 打った文字列が生きている時間（ミリ秒）。
    ///
    /// 短すぎると名前を打ち終わる前に切れて途中から別の行へ飛び、長すぎると
    /// 「もう忘れているはず」の文字列が次のジャンプに混ざる。
    static constexpr uint64_t kTimeoutMs = 1200;

    /// @brief 探す対象の行を引く口。
    ///
    /// 一覧そのものを渡さないのは、この判断に要るのが行の名前だけだから。
    /// core/input が一覧の構造を知らずに済む。
    class IRows {
    public:
        virtual ~IRows() = default;

        /// @brief 行数を返す。
        /// @return 行数。0 なら飛び先は決められない
        virtual int Count() const = 0;

        /// @brief 行の名前を返す。
        /// @param[in] index 行番号。0 以上 Count() 未満
        /// @return 名前。飛び先にできない行（「..」）では空文字列
        virtual std::string_view NameAt(int index) const = 0;
    };

    /// @brief 1 打鍵の結果。
    struct Jump {
        bool taken = false;  ///< 型入力ジャンプがこの打鍵を受け取ったか
        int row = -1;        ///< 飛び先の行。どこにも当たらなければ -1
    };

    /// @brief 文字を 1 つ打ち足し、飛び先を決める。
    /// @param[in] codepoint 打たれた Unicode コードポイント
    /// @param[in] rows 探す対象の行
    /// @param[in] from 今カーソルが居る行。範囲外は 0 として扱う
    /// @param[in] nowMs 現在時刻（plat::NowMs() の値）
    /// @return 受け取ったかと飛び先。受け取らなかった打鍵では taken が false
    /// @note 前回の打鍵から kTimeoutMs を過ぎていれば、打ちかけの文字列を捨ててから
    ///       始める
    /// @note 制御文字は受け取らない。**空のバッファに来た空白も受け取らない** ─
    ///       `Space` は選択のトグルで、名前が空白で始まる項目のために 1 打鍵の
    ///       選択操作を明け渡すことはできない。打ちかけの文字列がある間は、
    ///       空白も名前の一部として受け取る
    /// @note 当たらなかった 1 文字はバッファに入れない。入れると、打ち間違いの
    ///       1 文字で以後の打鍵が全部空振りする文字列だけが残る
    Jump Type(uint32_t codepoint, const IRows& rows, int from, uint64_t nowMs);

    /// @brief 打ちかけの文字列の末尾 1 文字を消す。
    /// @param[in] nowMs 現在時刻（plat::NowMs() の値）
    /// @return 実際に消したら true。文字列が無い、または時間切れなら false
    /// @note カーソルは動かさない。短くした文字列にも今の行は必ず前方一致している
    ///       ─ 「ab」で着いた行は「a」でも当たる
    bool Erase(uint64_t nowMs);

    /// @brief 打ちかけの文字列を捨てる。
    /// @note コマンドを 1 つでも実行したら呼ぶこと。別の操作をした以上、打ちかけの
    ///       文字列はもう «今打っているもの» ではない
    void Clear();

    /// @brief 打ちかけの文字列が生きているかを返す。
    /// @param[in] nowMs 現在時刻（plat::NowMs() の値）
    /// @return 生きていれば true
    bool active(uint64_t nowMs) const;

    /// @brief 打ちかけの文字列を返す。
    /// @return 打たれた文字列。無ければ空
    /// @note 時間切れかどうかは見ない。表示に使う側は active() で確かめること
    const std::string& text() const { return text_; }

private:
    /// @brief needle で始まる行を from から順に探す（末尾で先頭へ回り込む）。
    int Find(std::string_view needle, const IRows& rows, int from, bool skipCurrent) const;

    std::string text_;     ///< 打たれた文字列
    uint64_t lastMs_ = 0;  ///< 最後に打たれた時刻
};

}  // namespace kite
