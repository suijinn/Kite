/// @file
/// @brief コマンドパレット（`Ctrl+Shift+P`）の状態と操作。
///
/// 全コマンドを 1 枚のオーバーレイに並べ、絞り込んで実行する。**キー割り当てを覚えて
/// いなくても、あるいは和音が常駐ソフトに奪われていても、そこから全操作に届く。**
/// メニューバーもリボンも足さない方針（ROADMAP「あえてやらないこと」）のもとで、
/// 「どんな操作があるのか」を尋ねる唯一の道が `F1` の一覧だけだった ─ あちらは読む
/// ためのもので、実行する経路を持っていない。
///
/// 番号で指す 8 個（`Alt+Shift+1..8`）だけは、ラベルに行き先の名前を添える ─ 「ブック
/// マーク 1 へ」は**どのフォルダなのかを何も言っていない**ので、それ単体では読めない行に
/// なる。添えた名前は絞り込みにも当たるので、8 個までは名前で引ける。
///
/// 行が持つのは、表示名・`keys.ini` 上の名前・分類・割り当てられている和音の 4 つ。
/// 名前まで並べるのは、絞り込みがそれに当たるのに画面のどこにも出ていないと、当たること
/// 自体が伝わらないから（`F1` の「割り当ては全部並べる」と同じ）。
///
/// 行の組み立てと絞り込みまでをここが持つ。UI 層は rows() を描いて入力を渡すだけで、
/// 判断は一切しない。OS にも描画にも触れないので、挙動は tests/test_commandpalette.cpp が
/// 端から端まで検証できる。
///
/// 実行そのものはここでは行わない。`HandleKey()` が「何をすべきか」を Action で返し、
/// `App::Execute` を呼ぶのは App ─ ここがコマンドを実行し始めた時点で、この画面は
/// «選ばせるもの» ではなくなる（`PlacePicker` と同じ切り分け）。

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core/app/PickerList.h"
#include "core/i18n/Strings.h"
#include "core/input/Commands.h"
#include "core/input/KeyMap.h"
#include "core/input/Keys.h"
#include "core/model/Workspace.h"

namespace kite {

/// @brief コマンドパレットの画面。
///
/// ブックマーク一覧の兄弟で、同じパネル・同じ行の刻み・同じ作法（`↑↓` で移動、表示中は
/// 全打鍵を飲み込む、閉じるのは `Esc`）。違うのは行が持つものだけ ─ パスではなく、
/// コマンドのラベル・分類・割り当てられている和音。
class CommandPalette {
public:
    /// @brief コマンドモードの印。入力欄の先頭に常に在る。
    ///
    /// 行き先の一覧（`Ctrl+P`）の欄でこれを打てばこちらへ、消せばあちらへ戻る
    /// （VS Code と同じ読み方。切り替えは `App::SyncPickerMode`）。**印は入力欄の中に
    /// 文字として在る** ─ «今どちらのモードか» を言うのも、戻る道も、これ 1 つで足りる。
    static constexpr std::string_view kPrefix = ">";

    /// @brief 打鍵を処理した結果、呼び出し側がすべきこと。
    enum class Action : uint8_t {
        None,   ///< 何も起きない。打鍵は消費した
        Close,  ///< 画面を閉じる
        Run,    ///< 選択中のコマンドを実行する
    };

    /// @brief 一覧の 1 行。
    struct Row {
        Cmd cmd = Cmd::None;  ///< 実行するコマンド
        std::string label;    ///< 表示名（i18n 済み）
        std::string name;     ///< `keys.ini` 上の名前（`tab.new`）。表示言語で動かない綴り
        std::string group;    ///< 分類の見出し（i18n 済み）
        std::string chords;   ///< 割り当てられている和音。無ければ空
    };

    /// @brief パレットを開く。
    /// @param[in] str 表示文字列。ラベルと分類の名前を引く
    /// @param[in] keys 現在の割り当て。行に出す和音に使う
    /// @param[in] marks 現在のブックマーク。`Cmd::Bookmark1..8` の行に行き先の名前を
    ///            添えるためだけに使う。ここに**ブックマークの行を足すのではない** ─
    ///            並べるのはコマンド表であって、実行中に増減するデータではない
    ///            （9 件目以降は `Ctrl+P` の一覧の仕事）
    /// @note 行の並びはコマンド表の定義順（`F1` の一覧と同じ並び）。使用頻度で
    ///       並べ替えないのは、探している行が打鍵ごとに動かないほうが速いから
    /// @note 絞り込みは開くたびに初期化する。前回打った文字が残っていると、開いた
    ///       瞬間に一覧が虫食いになって「コマンドが消えた」と読めてしまう
    void Open(const Strings& str, const KeyMap& keys, const std::vector<Bookmark>& marks);

    /// @brief パレットを閉じる。
    void Close();

    /// @brief 表示中かを返す。
    /// @return 表示中なら true
    bool visible() const { return visible_; }

    /// @brief 一覧の行を返す。
    /// @return 絞り込み後の行。絞り込みが何にも当たらなければ空
    const std::vector<Row>& rows() const { return rows_; }

    /// @brief コマンドの総数を返す。
    /// @return 絞り込み前の件数
    int total() const;

    /// @brief 選択中の行番号を返す。
    /// @return rows() への添字。行が無ければ -1
    int cursor() const;

    /// @brief 一覧の先頭に表示する行番号を返す。
    /// @return rows() への添字
    int scroll() const;

    /// @brief 絞り込み文字列を返す。
    /// @return 一致に掛けている文字列（先頭の `>` は含まない）。無ければ空
    const std::string& filter() const;

    /// @brief 絞り込みの入力欄を返す。
    /// @return キャレットと選択を持つ入力欄
    /// @note 描く側はこれを見る。キャレットが末尾にあるとは限らない
    const TextField& field() const { return list_.field(); }

    /// @brief 絞り込みの入力欄を書き換えられる形で返す。
    /// @return キャレットと選択を持つ入力欄
    /// @note **書き換えたら FilterEdited() を呼ぶこと。** クリップボードを読み書き
    ///       できるのは `IShellIntegration` を持つ側だけなので、`Ctrl+C` / `Ctrl+X` /
    ///       `Ctrl+V` の 3 つだけはここを通って App から編集される
    TextField& filterField() { return list_.filterField(); }

    /// @brief 外から絞り込み欄を書き換えた後に呼ぶ。
    /// @note 一致した行を数え直して rows() に反映する
    void FilterEdited();

    /// @brief 選択中のコマンドを返す。
    /// @return 実行するコマンド。選択が無ければ Cmd::None
    /// @note 実行する側はこれを使う。rows() への添字は絞り込みで動くので、
    ///       画面の外へ持ち出してはならない
    Cmd selectedCommand() const;

    /// @brief 1 画面に収まる行数を教える。
    /// @param[in] rows 行数。1 未満は 1 として扱う
    /// @note PageUp / PageDown の移動量とスクロール位置の計算に使うので、
    ///       描画のたびに実際の高さから渡すこと
    void SetPageRows(int rows);

    /// @brief 行を選択する。範囲外は無視する。
    /// @param[in] index rows() への添字
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
    /// @return 呼び出し側がすべきこと。表示していなければ Action::None
    /// @note 表示中はここに来たすべての和音を飲み込む。コマンドを選んでいる最中に
    ///       別のコマンドが暴発しては選ばせたことにならない
    /// @note `Enter` だけが実行で、修飾を足した `Ctrl+Enter` には意味を持たせない ─
    ///       行が持つのはコマンド 1 つで、«別の実行のしかた» が無い
    Action HandleKey(const Chord& chord);

    /// @brief 文字入力を絞り込みに反映する。
    /// @param[in] codepoint 入力された Unicode コードポイント
    /// @return 消費したら true。表示していなければ false
    /// @note ラベル・分類・`keys.ini` 上の名前のいずれかへの部分一致で当てる。
    ///       名前まで見るのは、設定ファイルで見た `tab.new` を打つ人がいるから
    bool HandleChar(uint32_t codepoint);

private:
    void Sync();

    PickerList list_;        ///< 絞り込みとカーソル。Cmd の値を id として渡す
    std::vector<Row> all_;   ///< 絞り込み前の全行。コマンド表の定義順
    std::vector<Row> rows_;  ///< 絞り込み後の行。list_.shown() を引き当てたもの
    bool visible_ = false;
};

}  // namespace kite
