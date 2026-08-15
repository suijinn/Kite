/// @file
/// @brief ショートカットキー設定画面の状態と操作。
///
/// 画面に出す行の組み立て、絞り込み、カーソル移動、キーの取り込みまでをここが持つ。
/// UI 層は rows() を描いて入力を渡すだけで、判断は一切しない。OS にも描画にも
/// 触れないので、設定画面の挙動は tests/test_keyeditor.cpp が端から端まで検証できる。
///
/// keys.ini への保存はここでは行わない。`dirty()` が立った時点で App が書き出す。

#pragma once

#include <string>
#include <vector>

#include "core/i18n/Strings.h"
#include "core/input/Commands.h"
#include "core/input/KeyMap.h"
#include "core/input/Keys.h"

namespace kite {

/// @brief ショートカットキー設定画面。
///
/// 「割り当てを変更する」以外の操作は受け付けない。表示中は生のキー入力を
/// すべて飲み込むので、設定中に別のコマンドが暴発することはない。
class KeyEditor {
public:
    /// @brief 一覧の 1 行。
    struct Row {
        Cmd cmd = Cmd::None;   ///< 対象コマンド。見出し行では Cmd::None
        bool header = false;   ///< 分類の見出し行か
        std::string label;     ///< 表示名。見出し行では分類名
        std::string chords;    ///< 割り当ての表示文字列。無ければ空
        std::vector<std::string> chordTexts;  ///< 同じ内容を和音 1 つずつに分けたもの
    };

    /// @brief 設定画面を開く。
    /// @param[in] strings 表示名の取得に使う文字列表
    /// @param[in] keys 現在の割り当て
    /// @note 絞り込みとメッセージは開くたびに初期化する
    void Open(const Strings& strings, const KeyMap& keys);

    /// @brief 設定画面を閉じる。取り込み待ちであれば中止する。
    void Close();

    /// @brief 一覧を作り直す。
    /// @param[in] strings 表示名の取得に使う文字列表
    /// @param[in] keys 現在の割り当て
    /// @note 選択はコマンド単位で覚え直すので、絞り込みで行が動いても選択は残る
    void Rebuild(const Strings& strings, const KeyMap& keys);

    /// @brief 表示中かを返す。
    /// @return 表示中なら true
    bool visible() const { return visible_; }

    /// @brief キーの取り込み待ちかを返す。
    /// @return 次の打鍵を割り当てに使う状態なら true
    bool capturing() const { return capturing_; }

    /// @brief 一覧の行を返す。
    /// @return 見出しを含む全行。絞り込み後の内容
    const std::vector<Row>& rows() const { return rows_; }

    /// @brief 選択中の行番号を返す。
    /// @return rows() への添字。行が無ければ -1
    int cursor() const { return cursor_; }

    /// @brief 行の中で選ばれている和音の番号を返す。
    /// @return 選択中の行の chordTexts への添字。選んでいなければ -1
    /// @note 行そのものの選択とは別。Delete はこれが立っていればその 1 つだけを、
    ///       立っていなければコマンドの割り当てを全部解除する
    int chordCursor() const { return chordCursor_; }

    /// @brief 一覧の先頭に表示する行番号を返す。
    /// @return rows() への添字
    int scroll() const { return scroll_; }

    /// @brief 絞り込み文字列を返す。
    /// @return 入力済みの文字列。無ければ空
    const std::string& filter() const { return filter_; }

    /// @brief 直前の操作の結果メッセージを返す。
    /// @return 表示する文字列。無ければ空
    const std::string& message() const { return message_; }

    /// @brief 選択中のコマンドを返す。
    /// @return 選択中のコマンド。選択が無ければ Cmd::None
    Cmd selectedCommand() const;

    /// @brief 一覧に出ているコマンドの数を返す。
    /// @return 見出しを除いた行数。絞り込み後の件数
    int commandCount() const;

    /// @brief 割り当てを変更したかを返す。
    /// @return 前回 ClearDirty() を呼んでから変更があれば true
    bool dirty() const { return dirty_; }

    /// @brief 変更ありの印を落とす。
    /// @note keys.ini に書き出した側が呼ぶ
    void ClearDirty() { dirty_ = false; }

    /// @brief 1 画面に収まる行数を教える。
    /// @param[in] rows 行数。1 未満は 1 として扱う
    /// @note PageUp / PageDown の移動量とスクロール位置の計算に使うので、
    ///       描画のたびに実際の高さから渡すこと
    /// @note 行数が変わったときだけ選択を画面内に引き戻す。毎回引き戻すと
    ///       ホイールで選択から離れたスクロールが描く前に取り消される
    void SetPageRows(int rows);

    /// @brief 行を選択する。見出し行と範囲外は無視する。
    /// @param[in] index rows() への添字
    void SelectRow(int index);

    /// @brief 一覧を上下にスクロールする。選択は動かさない。
    /// @param[in] deltaRows 動かす行数。負で上へ
    void Scroll(int deltaRows);

    /// @brief 選択を移動する。見出し行は読み飛ばす。
    /// @param[in] delta 移動量。absolute が true なら移動先の行番号
    /// @param[in] absolute true なら delta を移動先の行番号として扱う
    void MoveCursor(int delta, bool absolute = false);

    /// @brief 行の中の和音を 1 つ選ぶ。
    /// @param[in] index 選択中の行の chordTexts への添字。範囲外は選択解除
    /// @param[in] strings メッセージの組み立てに使う文字列表
    void SelectChord(int index, const Strings& strings);

    /// @brief 行の中の和音 1 つぶんの割り当てを解除する。
    /// @param[in] index 選択中の行の chordTexts への添字
    /// @param[in,out] keys 変更対象の割り当て表
    /// @param[in] strings メッセージの組み立てに使う文字列表
    /// @note 解除後は同じ位置の和音に選択が残る。最後の 1 つを消したときだけ外れる
    void RemoveChord(int index, KeyMap& keys, const Strings& strings);

    /// @brief キーの取り込みを開始する。
    /// @param[in] add true なら既存の割り当てを残して追加、false なら置き換え
    /// @note 選択が無い場合は何もしない
    void BeginCapture(bool add);

    /// @brief キーの取り込みを中止する。
    void CancelCapture();

    /// @brief キー入力を処理する。
    /// @param[in] chord 押された和音
    /// @param[in,out] keys 変更対象の割り当て表
    /// @param[in] strings メッセージの組み立てに使う文字列表
    /// @return 消費したら true。表示していなければ false
    /// @note 表示中は Escape で閉じる以外のすべてを消費する。取り込み待ちの間は
    ///       Escape を除くあらゆる和音が割り当ての対象になる
    /// @note Enter が置き換え、Ctrl+Enter が追加（Insert も同じ）。Delete で解除、
    ///       Ctrl+R で 1 つを既定に戻し、Ctrl+Shift+R で全部を戻す
    bool HandleKey(const Chord& chord, KeyMap& keys, const Strings& strings);

    /// @brief 文字入力を絞り込みに反映する。
    /// @param[in] codepoint 入力された Unicode コードポイント
    /// @param[in] strings 表示名の取得に使う文字列表
    /// @param[in] keys 現在の割り当て
    /// @return 消費したら true。表示していない、または取り込み待ちなら false
    bool HandleChar(uint32_t codepoint, const Strings& strings, const KeyMap& keys);

private:
    void EnsureCursorVisible();
    void SelectCommand(Cmd id);
    void MoveChordCursor(int delta, const Strings& strings);
    const std::vector<std::string>& SelectedChords() const;

    std::vector<Row> rows_;
    std::string filter_;
    std::string message_;
    int cursor_ = -1;
    int chordCursor_ = -1;
    int scroll_ = 0;
    int pageRows_ = 20;
    bool visible_ = false;
    bool capturing_ = false;
    bool captureAdds_ = false;
    bool dirty_ = false;
    /// 取り込みを終えた打鍵に続く文字入力を捨てるための印。
    bool swallowNextChar_ = false;
};

}  // namespace kite
