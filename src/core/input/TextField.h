/// @file
/// @brief 1 行のテキスト入力欄 ─ 文字列・キャレット・選択範囲と、その上の打鍵。
///
/// Kite には文字を打ち込む場所が 2 種類ある ─ パスや名前を尋ねる入力欄（`Prompt`）と、
/// チューザの絞り込み欄（`PickerList`）。**キャレットの数え方をここに 1 つだけ置く。**
/// 2 つに分かれた時点でどちらかが必ず古くなる、というのはタブバーの縦横やチューザの
/// 器と同じ話で、`Shift+←` の伸ばし方が画面ごとに違うのでは入力欄とは言えない。
///
/// OS にも描画にも触れない。クリップボードは含まない ─ 読み書きできるのは
/// `IShellIntegration` を持つ側だけなので、そちらが `Selection()` と `Insert()` を
/// 使って組み立てる。

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "core/input/Keys.h"

namespace kite {

/// @brief 1 行のテキスト入力欄の中身。
///
/// `caret` と `anchor` が等しければ選択なし。**キャレットを動かす場所では
/// SetCaret() を通すこと** ─ `caret` だけを書くと `anchor` が前の位置に残り、
/// 意図しない範囲が選択されたままになる（`Shift` の側はこれをわざとやっている）。
struct TextField {
    /// @brief 打鍵を処理した結果。
    enum class Edit : uint8_t {
        None,     ///< この欄のキーではない。呼び出し側が読む
        Moved,    ///< キャレットか選択だけが動いた。文字列は変わっていない
        Changed,  ///< 文字列が変わった
    };

    std::string text;  ///< 入力中の文字列（UTF-8）
    size_t caret = 0;  ///< キャレット位置。text へのバイト添字
    size_t anchor = 0; ///< 選択範囲のもう一端。caret と等しければ選択なし

    /// @brief 文字列が選択されているかを判定する。
    /// @return 選択されていれば true
    bool hasSelection() const { return caret != anchor; }

    /// @brief 選択範囲の先頭を返す。
    /// @return text へのバイト添字
    size_t selBegin() const { return caret < anchor ? caret : anchor; }

    /// @brief 選択範囲の終端を返す。
    /// @return text へのバイト添字。選択が無ければ selBegin() と等しい
    size_t selEnd() const { return caret < anchor ? anchor : caret; }

    /// @brief 選択されている文字列を返す。
    /// @return 選択範囲の写し。選択が無ければ空
    std::string Selection() const { return text.substr(selBegin(), selEnd() - selBegin()); }

    /// @brief キャレットを動かし、選択を解除する。
    /// @param[in] pos 移動先。text へのバイト添字
    void SetCaret(size_t pos) {
        caret = pos;
        anchor = pos;
    }

    /// @brief 範囲を選択し、キャレットを終端側に置く。
    /// @param[in] begin 選択の先頭。text へのバイト添字
    /// @param[in] end 選択の終端。text へのバイト添字。どちらも末尾に丸める
    /// @note キャレットが終端側なのは、`→` を押せば選択の後ろへ畳めるという
    ///       入力欄の一般則に合わせるため
    void SelectRange(size_t begin, size_t end) {
        anchor = begin < text.size() ? begin : text.size();
        caret = end < text.size() ? end : text.size();
    }

    /// @brief 全体を選択する。
    void SelectAll() { SelectRange(0, text.size()); }

    /// @brief 文字列を捨て、キャレットを先頭に戻す。
    void Clear() {
        text.clear();
        SetCaret(0);
    }

    /// @brief 選択されている範囲を削除する。
    /// @return 実際に削除したら true
    /// @note 文字入力・Backspace・Delete がまずこれを呼ぶ。選択したまま打った
    ///       文字が置き換えではなく挿入になると、全選択が何のためにあるのか
    ///       分からなくなる
    bool DeleteSelection() {
        if (!hasSelection()) return false;
        const size_t begin = selBegin();
        text.erase(begin, selEnd() - begin);
        SetCaret(begin);
        return true;
    }

    /// @brief 選択を置き換えて文字列を挿し込む。
    /// @param[in] s 挿し込む文字列。空なら何もしない
    /// @return 実際に挿し込んだら true
    bool Insert(std::string_view s);

    /// @brief キー入力を処理する。
    /// @param[in] chord 押された和音
    /// @return 処理の結果。この欄のキーでなければ Edit::None
    /// @note 受け持つのは `←→`（`Shift` / `Ctrl` 付きを含む）・`Home` / `End`・
    ///       `Backspace`・`Delete`・`Ctrl+A` だけ。`Enter` も `Escape` も画面ごとに
    ///       意味が違うので呼び出し側のもの
    /// @note **`Shift+Delete` は受け取らない** ─ あれは切り取りの別の綴りで、
    ///       クリップボードを持つ側の仕事（`Ctrl+C` / `Ctrl+X` / `Ctrl+V` も同じ）
    /// @note `Ctrl+←→` が動く単位は «単語» ではなく «パスの構成要素»。この欄に入る
    ///       のはパスであることが多く、`Users` の途中で止まっても誰も嬉しくない
    Edit HandleKey(const Chord& chord);
};

}  // namespace kite
