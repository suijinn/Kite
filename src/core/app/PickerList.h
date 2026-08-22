/// @file
/// @brief 「絞り込みと ↑↓ と Enter の 1 枚のオーバーレイ」の、行の中身に依らない部分。
///
/// 行き先の一覧（`Ctrl+P`）とコマンドパレット（`Ctrl+Shift+P`）は、行が何を持つかだけ
/// が違う同じ画面である。絞り込み・カーソル・スクロール・打鍵の読み方をここに 1 つだけ
/// 置き、行の組み立てと «選ばれたものをどうするか» だけを呼び出し側に残す。
///
/// **2 つに分かれた時点でどちらかが必ず古くなる。** タブバーの縦横で折り返しとスクロールの
/// 計算を共通にしてあるのと同じ判断で、片方だけに «選択を画面内に引き戻す» のような
/// 規則を書き足さないこと。
///
/// 行の実体は持たない。持つのは «呼び出し側が行を指すための値»（id）と絞り込みの対象に
/// する文字列だけなので、OS にも描画にも触れない。

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/input/Keys.h"
#include "core/input/TextField.h"

namespace kite {

/// @brief 絞り込み付き一覧の状態。
///
/// 表示中は打鍵をすべて飲み込む前提で書かれている ─ 呼び出し側が「見えている間は
/// キーマップを通さない」を守る限り、選んでいる最中に別のコマンドが暴発することはない。
class PickerList {
public:
    /// @brief 打鍵を処理した結果、呼び出し側がすべきこと。
    enum class Action : uint8_t {
        None,       ///< 何も起きない。打鍵は消費した
        Close,      ///< 画面を閉じる
        Accept,     ///< 選択中の行を採る（`Enter`）
        AcceptAlt,  ///< 修飾付きで採る（`Ctrl+Enter`）。意味は呼び出し側が決める
    };

    /// @brief 絞り込みの対象になる 1 行。
    struct Entry {
        int id = 0;                       ///< 呼び出し側が行を指すための値。一覧の中で重複しないこと
        std::vector<std::string> fields;  ///< 絞り込みに掛ける文字列。**どれか 1 つ**に当たれば一致
    };

    /// @brief 絞り込みに掛けない «モードの印» を決める。
    /// @param[in] prefix 入力欄の先頭に置く印。印を持たない画面では空
    /// @note 印は**入力欄の中に文字として在る**（VS Code と同じで、消せば戻れる）。
    ///       絞り込みが掛かるのはその後ろだけで、`Escape` は印まで戻す ─ 印まで
    ///       消してしまうと、消した瞬間に画面が入れ替わる
    /// @note Reset() より先に呼ぶこと。Reset() が入力欄を印で初期化する
    void SetPrefix(std::string prefix);

    /// @brief モードの印を返す。
    /// @return 入力欄の先頭に置かれる印。無ければ空
    const std::string& prefix() const { return prefix_; }

    /// @brief 一覧の中身を差し替える。絞り込みは捨てる。
    /// @param[in] entries 絞り込み前の全行。並びがそのまま行の並びになる
    /// @param[in] selectedId 最初に選んでおく行の id。無ければ -1
    /// @note 絞り込みは開くたびに初期化する。前回打った文字が残っていると、開いた
    ///       瞬間に一覧が虫食いになって「行が消えた」と読めてしまう
    void Reset(std::vector<Entry> entries, int selectedId);

    /// @brief 一覧を捨てる。
    void Clear();

    /// @brief 絞り込み後の行の id を順に返す。
    /// @return 表示する行の id 列。絞り込みが何にも当たらなければ空
    const std::vector<int>& shown() const { return shown_; }

    /// @brief 絞り込み前の件数を返す。
    /// @return 全行の数
    int total() const { return static_cast<int>(all_.size()); }

    /// @brief 選択中の行番号を返す。
    /// @return shown() への添字。行が無ければ -1
    int cursor() const { return cursor_; }

    /// @brief 一覧の先頭に表示する行番号を返す。
    /// @return shown() への添字
    int scroll() const { return scroll_; }

    /// @brief 1 画面に収まる行数を返す。
    /// @return 直前に SetPageRows() で渡された行数
    int pageRows() const { return pageRows_; }

    /// @brief 選択中の行の id を返す。
    /// @return 行の id。選択が無ければ -1
    /// @note 画面の外へ持ち出すのはこちら。shown() への添字は絞り込みで動く
    int selectedId() const;

    /// @brief 絞り込み文字列を返す。
    /// @return 実際に一致に掛けている文字列（モードの印は含まない）。無ければ空
    /// @note 入力欄に見えている文字列そのものは field() のほう
    const std::string& filter() const { return query_; }

    /// @brief 絞り込みの入力欄を返す。
    /// @return キャレットと選択を持つ入力欄
    /// @note 描く側はこれを見る。キャレットが末尾にあるとは限らない
    const TextField& field() const { return filter_; }

    /// @brief 絞り込みの入力欄を書き換えられる形で返す。
    /// @return キャレットと選択を持つ入力欄
    /// @note **書き換えたら FilterEdited() を呼ぶこと。** クリップボードを読み書き
    ///       できるのは `IShellIntegration` を持つ側だけなので、`Ctrl+C` / `Ctrl+X` /
    ///       `Ctrl+V` の 3 つだけはここを通って外から編集される
    TextField& filterField() { return filter_; }

    /// @brief 外から絞り込み欄を書き換えた後に呼ぶ。
    /// @note 一致した行を数え直す。呼ばないと、打ち込んだ文字が画面には出ているのに
    ///       一覧だけが古いままになる
    void FilterEdited() { Rebuild(); }

    /// @brief 1 画面に収まる行数を教える。
    /// @param[in] rows 行数。1 未満は 1 として扱う
    /// @note PageUp / PageDown の移動量とスクロール位置の計算に使うので、
    ///       描画のたびに実際の高さから渡すこと
    /// @note 行数が変わったときだけ選択を画面内に引き戻す。毎回引き戻すと
    ///       ホイールで選択から離れたスクロールが描く前に取り消される
    void SetPageRows(int rows);

    /// @brief 行を選択する。範囲外は無視する。
    /// @param[in] index shown() への添字
    void SelectRow(int index);

    /// @brief 一覧を上下にスクロールする。選択は動かさない。
    /// @param[in] deltaRows 動かす行数。負で上へ
    void Scroll(int deltaRows);

    /// @brief 選択を移動する。
    /// @param[in] delta 移動量。absolute が true なら移動先の行番号
    /// @param[in] absolute true なら delta を移動先の行番号として扱う
    void MoveCursor(int delta, bool absolute = false);

    /// @brief キー入力を処理する。
    /// @param[in] chord 押された和音
    /// @return 呼び出し側がすべきこと
    /// @note ここに来た和音はすべて飲み込む。行を選んでいる最中に `Ctrl+T` で
    ///       タブが増えては選ばせたことにならない
    /// @note `Escape` は先に絞り込みを捨て、もう一度で Action::Close を返す
    /// @note 一覧のキー（`↑↓`・`PageUp/Down`・`Enter`）を先に見て、残りを絞り込み欄へ
    ///       渡す。**`Home` / `End` だけは絞り込みが空のときだけ一覧のもの** ─ 空の欄で
    ///       キャレットを動かしても何も起きないので、そのときは «先頭の行へ» と読む
    Action HandleKey(const Chord& chord);

    /// @brief 文字入力を絞り込みに反映する。
    /// @param[in] codepoint 入力された Unicode コードポイント
    /// @return 消費したら true。制御文字なら false
    /// @note 一致は Entry::fields のどれかへの部分一致（ASCII の大文字小文字は畳む）。
    ///       前方一致にすると、深いパスの末尾のフォルダ名や語尾で探せない
    bool HandleChar(uint32_t codepoint);

private:
    void Rebuild();
    void EnsureCursorVisible();

    std::vector<Entry> all_;
    std::vector<int> shown_;  ///< 絞り込み後の id 列
    TextField filter_;
    std::string prefix_;  ///< 入力欄の先頭に置く «モードの印»。絞り込みには掛けない
    std::string query_;   ///< filter_.text から印を除いたもの。一致に掛けるのはこちら
    /// 絞り込みで行が動いても選択が残るよう、行番号ではなく id で覚える。
    int selected_ = -1;
    int cursor_ = -1;
    int scroll_ = 0;
    int pageRows_ = 12;
};

}  // namespace kite
