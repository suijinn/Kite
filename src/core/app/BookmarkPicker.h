/// @file
/// @brief ブックマーク一覧（`Alt+B`）の状態と操作。
///
/// 番号ショートカット（`Alt+Shift+1..8`）は 8 個で打ち止めなので、**9 件目以降の
/// ブックマークにキーボードだけで届く道はここしかない。** 全件を 1 枚のオーバーレイに
/// 並べ、絞り込みとカーソル移動で選ばせる。件数が増えるほど、番号を覚えるより絞り込む
/// ほうが速くなる。
///
/// 画面に出す行の組み立て、絞り込み、カーソル移動までをここが持つ。UI 層は rows() を
/// 描いて入力を渡すだけで、判断は一切しない。OS にも描画にも触れないので、挙動は
/// tests/test_bookmarkpicker.cpp が端から端まで検証できる。
///
/// 移動そのものはここでは行わない。`HandleKey()` が「何をすべきか」を Action で返し、
/// 実際に navigate するのは App ─ ここが `Workspace` を書き換え始めた時点で、
/// この画面は «選ばせるもの» ではなくなる。

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/input/KeyMap.h"
#include "core/input/Keys.h"
#include "core/model/Workspace.h"

namespace kite {

/// @brief ブックマーク一覧の画面。
///
/// 「選んで移動する」以外の操作は受け付けない。追加・削除・並べ替えはサイドバーの
/// 仕事で、同じ操作の入口を 2 か所に置かない。表示中は生のキー入力をすべて飲み込むので、
/// 選んでいる最中に別のコマンドが暴発することはない。
class BookmarkPicker {
public:
    /// @brief 打鍵を処理した結果、呼び出し側がすべきこと。
    enum class Action : uint8_t {
        None,        ///< 何も起きない。打鍵は消費した
        Close,       ///< 画面を閉じる
        Open,        ///< 選択中のブックマークへ移動する
        OpenNewTab,  ///< 選択中のブックマークを新しいタブで開く
    };

    /// @brief 一覧の 1 行。
    struct Row {
        int index = 0;       ///< Workspace::bookmarks への添字。絞り込んでも元の位置を指す
        std::string name;    ///< 表示名
        std::string path;    ///< 移動先のパス
        std::string chords;  ///< 番号ショートカットの表示文字列。割り当てが無ければ空
    };

    /// @brief 一覧を開く。
    /// @param[in] marks 現在のブックマーク。並びがそのまま行の並びになる
    /// @param[in] keys 現在の割り当て。番号ショートカットの表示に使う
    /// @param[in] currentPath 表示中のフォルダ。一致する行があればそこにカーソルを置く
    /// @note 絞り込みは開くたびに初期化する。前回打った文字が残っていると、開いた
    ///       瞬間に一覧が虫食いになって「ブックマークが消えた」と読めてしまう
    void Open(const std::vector<Bookmark>& marks, const KeyMap& keys,
              const std::string& currentPath);

    /// @brief 一覧を閉じる。
    void Close();

    /// @brief 表示中かを返す。
    /// @return 表示中なら true
    bool visible() const { return visible_; }

    /// @brief 一覧の行を返す。
    /// @return 絞り込み後の行。絞り込みが何にも当たらなければ空
    const std::vector<Row>& rows() const { return rows_; }

    /// @brief ブックマークの総数を返す。
    /// @return 絞り込み前の件数
    int total() const { return static_cast<int>(all_.size()); }

    /// @brief 選択中の行番号を返す。
    /// @return rows() への添字。行が無ければ -1
    int cursor() const { return cursor_; }

    /// @brief 一覧の先頭に表示する行番号を返す。
    /// @return rows() への添字
    int scroll() const { return scroll_; }

    /// @brief 絞り込み文字列を返す。
    /// @return 入力済みの文字列。無ければ空
    const std::string& filter() const { return filter_; }

    /// @brief 選択中のブックマークの位置を返す。
    /// @return Workspace::bookmarks への添字。選択が無ければ -1
    /// @note 移動する側はこれを使う。rows() への添字は絞り込みで動くので、
    ///       画面の外へ持ち出してはならない
    int selectedIndex() const;

    /// @brief 1 画面に収まる行数を教える。
    /// @param[in] rows 行数。1 未満は 1 として扱う
    /// @note PageUp / PageDown の移動量とスクロール位置の計算に使うので、
    ///       描画のたびに実際の高さから渡すこと
    /// @note 行数が変わったときだけ選択を画面内に引き戻す。毎回引き戻すと
    ///       ホイールで選択から離れたスクロールが描く前に取り消される
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
    /// @note 表示中はここに来たすべての和音を飲み込む。移動先を選んでいる最中に
    ///       `Ctrl+T` でタブが増えては選ばせたことにならない
    /// @note `Enter` が現ペインで開き、`Ctrl+Enter` が新しいタブで開く（一覧の
    ///       `Ctrl+Enter` と同じ読み方）。`Escape` は先に絞り込みを捨て、もう一度で閉じる
    Action HandleKey(const Chord& chord);

    /// @brief 文字入力を絞り込みに反映する。
    /// @param[in] codepoint 入力された Unicode コードポイント
    /// @return 消費したら true。表示していなければ false
    /// @note 名前とパスの**両方**に部分一致で当てる。前方一致にすると、深いパスの
    ///       末尾のフォルダ名で探せない
    bool HandleChar(uint32_t codepoint);

private:
    void Rebuild();
    void EnsureCursorVisible();

    std::vector<Row> all_;   ///< 絞り込み前の全行
    std::vector<Row> rows_;  ///< 絞り込み後の行
    std::string filter_;
    /// 絞り込みで行が動いても選択が残るよう、Workspace::bookmarks への添字で覚える。
    int selected_ = -1;
    int cursor_ = -1;
    int scroll_ = 0;
    int pageRows_ = 12;
    bool visible_ = false;
};

}  // namespace kite
