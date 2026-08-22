/// @file
/// @brief 行き先の一覧（`Ctrl+P`）の状態と操作。
///
/// ブックマークと**開いているタブ**と**ドライブ**を 1 枚のオーバーレイに並べ、絞り込みと
/// カーソル移動で
/// «次に見る場所» を選ばせる。番号ショートカット（`Alt+Shift+1..8` / `Ctrl+1..8`）は
/// 8 個で打ち止めなので、**その先へキーボードだけで届く道はここしかない。** 数が増える
/// ほど、番号を覚えるより絞り込むほうが速くなる。
///
/// **ここが «行き先» の窓口で、コマンドパレットは «コマンド» の窓口**（ROADMAP P3-12）。
/// 移動はファイラーの主動詞なので、頻度の高いこちらに行き先の種類を足していく ─ 逆に
/// パレットへ行き先を混ぜると、稀な 124 行が頻繁な検索を薄める。
///
/// @note `keys.ini` 上の名前は `nav.places`。旧名 `bookmark.list` は
///       `CommandFromName()` が別名として読み続けるので、既存の設定ファイルの行は
///       そのまま生きる ─ 名前・分類・列挙子・クラス名のどれもが «行き先» を指す
///
/// 画面に出す行の組み立て、絞り込み、カーソル移動までをここが持つ。UI 層は rows() を
/// 描いて入力を渡すだけで、判断は一切しない。OS にも描画にも触れないので、挙動は
/// tests/test_placepicker.cpp が端から端まで検証できる。
///
/// 移動そのものはここでは行わない。`HandleKey()` が「何をすべきか」を Action で返し、
/// 実際に navigate するのは App ─ ここが `Workspace` を書き換え始めた時点で、
/// この画面は «選ばせるもの» ではなくなる。
///
/// 絞り込み・カーソル・スクロールそのものは `PickerList` が持つ。コマンドパレットと
/// 共有しているので、**選択を画面内に引き戻すような規則をここに書き足さないこと。**

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/app/PickerList.h"
#include "core/fs/FileSystem.h"
#include "core/i18n/Strings.h"
#include "core/input/KeyMap.h"
#include "core/input/Keys.h"
#include "core/model/Workspace.h"

namespace kite {

/// @brief 行き先の一覧の画面。
///
/// 「選んで移動する」以外の操作は受け付けない。追加・削除・並べ替えはサイドバーの
/// 仕事で、同じ操作の入口を 2 か所に置かない。表示中は生のキー入力をすべて飲み込むので、
/// 選んでいる最中に別のコマンドが暴発することはない。
class PlacePicker {
public:
    /// @brief 打鍵を処理した結果、呼び出し側がすべきこと。
    enum class Action : uint8_t {
        None,        ///< 何も起きない。打鍵は消費した
        Close,       ///< 画面を閉じる
        Open,        ///< 選択中の行き先へ行く（タブの行ならそのタブに移る）
        OpenNewTab,  ///< 選択中のブックマークを新しいタブで開く
    };

    /// @brief 行が指しているものの種別。
    enum class Kind : uint8_t {
        Bookmark,  ///< ブックマーク。移動先はパス
        Tab,       ///< 今開いているタブ。移動先はそのタブ自身
        Drive,     ///< ドライブ。移動先はパス
    };

    /// @brief 開いているタブ 1 枚分。App が組み立てて渡す。
    ///
    /// 表示名を作るのも、どのペインを数えるのも App の仕事 ─ この画面は `Workspace` を
    /// 見に行かない。
    struct OpenTab {
        int pane = 0;   ///< Session::Panes() への添字
        int tab = 0;    ///< Pane::tabs への添字
        /// そのペインがフォーカス中か。`Ctrl+<数字>` が届くのはフォーカス中のペインだけ
        /// なので、行に和音を出してよいかの判断に使う。
        bool focused = false;
        std::string name;  ///< 表示名（App::DisplayName の結果）
        std::string path;  ///< そのタブが見ているフォルダ
    };

    /// @brief 一覧の 1 行。
    struct Row {
        Kind kind = Kind::Bookmark;  ///< 行が指しているもの
        /// Workspace::bookmarks への添字。絞り込んでも元の位置を指す。タブの行では -1
        int index = -1;
        int pane = -1;  ///< Kind::Tab のとき Session::Panes() への添字。それ以外は -1
        int tab = -1;   ///< Kind::Tab のとき Pane::tabs への添字。それ以外は -1
        std::string name;       ///< 表示名
        std::string path;       ///< 行き先のパス
        /// 「ブックマーク」「開いているタブ」「ドライブ」。絞り込みにも当たる
        std::string kindLabel;
        std::string chords;     ///< 番号ショートカットの表示文字列。無ければ空
    };

    /// @brief 一覧を開く。
    /// @param[in] str 表示文字列。行の種別の名前を引く
    /// @param[in] keys 現在の割り当て。番号ショートカットの表示に使う
    /// @param[in] marks 現在のブックマーク。並びがそのまま行の並びになる
    /// @param[in] tabs 今開いているタブ。**今いるタブは呼び出し側が除いて渡す**
    /// @param[in] drives ドライブ。サイドバーに出ているものと同じ並び
    /// @param[in] currentPath 表示中のフォルダ。一致する行があればそこにカーソルを置く
    /// @note 並びは**ブックマークが先、開いているタブ、ドライブの順**。ブックマークを
    ///       探すためにこの画面を開く人の手が変わらないようにするため（後の 2 つは
    ///       追加であって、置き換えではない）。ドライブが最後なのは、いつも同じ顔ぶれで
    ///       並んでいる «背景» だから ─ 探しに来る頻度がいちばん低い
    /// @note 絞り込みは開くたびに初期化する。前回打った文字が残っていると、開いた
    ///       瞬間に一覧が虫食いになって「ブックマークが消えた」と読めてしまう
    void Open(const Strings& str, const KeyMap& keys, const std::vector<Bookmark>& marks,
              const std::vector<OpenTab>& tabs, const std::vector<fs::Root>& drives,
              const std::string& currentPath);

    /// @brief 一覧を閉じる。
    void Close();

    /// @brief 表示中かを返す。
    /// @return 表示中なら true
    bool visible() const { return visible_; }

    /// @brief 一覧の行を返す。
    /// @return 絞り込み後の行。絞り込みが何にも当たらなければ空
    const std::vector<Row>& rows() const { return rows_; }

    /// @brief 行き先の総数を返す。
    /// @return 絞り込み前の件数（ブックマーク＋開いているタブ＋ドライブ）
    int total() const;

    /// @brief 選択中の行番号を返す。
    /// @return rows() への添字。行が無ければ -1
    int cursor() const;

    /// @brief 一覧の先頭に表示する行番号を返す。
    /// @return rows() への添字
    int scroll() const;

    /// @brief 絞り込み文字列を返す。
    /// @return 入力済みの文字列。無ければ空
    /// @note この画面はモードの印を持たないので、入力欄の中身そのものと同じ
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

    /// @brief 選択中のブックマークの位置を返す。
    /// @return Workspace::bookmarks への添字。選択が無い、またはタブの行なら -1
    /// @note rows() への添字は絞り込みで動くので、画面の外へ持ち出してはならない
    int selectedIndex() const;

    /// @brief 選択中の行を返す。
    /// @return 選択中の行。選択が無ければ nullptr
    /// @note 移動する側はこれを使う。**閉じる前に写しを取ること** ─ Close() が
    ///       行を捨てるので、閉じたあとのポインタは何も指さない
    const Row* selectedRow() const;

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
    /// @note **タブの行では `Ctrl+Enter` を飲み込む** ─ すでに開いているタブを
    ///       「新しいタブで開く」は、その行に無い意味。パスを指す行（ブックマークと
    ///       ドライブ）にはある
    Action HandleKey(const Chord& chord);

    /// @brief 文字入力を絞り込みに反映する。
    /// @param[in] codepoint 入力された Unicode コードポイント
    /// @return 消費したら true。表示していなければ false
    /// @note 名前・パス・種別の名前に部分一致で当てる（「タブ」と打てば開いている
    ///       タブだけが並ぶ）。前方一致にすると、深いパスの末尾のフォルダ名で探せない
    bool HandleChar(uint32_t codepoint);

private:
    void Sync();

    PickerList list_;        ///< 絞り込みとカーソル。Row::index を id として渡す
    std::vector<Row> all_;   ///< 絞り込み前の全行
    std::vector<Row> rows_;  ///< 絞り込み後の行。list_.shown() を引き当てたもの
    bool visible_ = false;
};

}  // namespace kite
