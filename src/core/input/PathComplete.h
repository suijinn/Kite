/// @file
/// @brief アドレスバー（`Ctrl+L`）の入力補完。
///
/// 打たれた文字列を「ディレクトリ」と「打ちかけの名前」に割り、そのディレクトリの
/// 下にあるフォルダのうち前方一致するものを候補にする。**列挙そのものはここでは
/// 行わない** ─ 冷えたネットワーク共有は 1 回の `FindFirstFile` で数秒ブロックする
/// ので、呼ぶ側が `DirectoryLoader` を通して取り、`SetListing()` で渡す。
///
/// OS にも描画にも触れないので、補完の挙動は tests/test_pathcomplete.cpp と
/// tests/test_app.cpp が端から端まで検証できる。

#pragma once

#include <string>
#include <vector>

#include "core/fs/FileSystem.h"

namespace kite {

/// @brief パス入力の補完状態。
///
/// 候補はフォルダだけを出す。入力欄の Enter が行うのは移動なので、ファイルを
/// 勧めても行き先にできない。
class PathComplete {
public:
    /// @brief 打たれた文字列を反映し、候補を作り直す。
    /// @param[in] text 入力欄の現在の文字列
    /// @note 選択は毎回 -1（＝打った文字列そのもの）に戻る。打ち足した瞬間に
    ///       前の候補が居座ると、1 文字打つたびに知らないパスへ化ける
    void SetInput(const std::string& text);

    /// @brief 候補一覧を開く。
    void Open() { open_ = true; }

    /// @brief 候補一覧を畳む。列挙の結果は捨てない。
    /// @note 選択していた候補は文字列に残す。畳んだ拍子に入力が巻き戻ると、
    ///       選んだつもりのパスが黙って消える
    void Close() { open_ = false; }

    /// @brief 何も無かった状態に戻す。
    /// @note 入力欄を開くとき・閉じるときに呼ぶ。列挙の結果も捨てる
    void Reset();

    /// @brief 候補一覧が開いているかを返す。
    /// @return 開いていれば true
    bool open() const { return open_; }

    /// @brief 候補の元になるディレクトリを返す。
    /// @return ディレクトリのパス。絶対パスを打っていなければ空文字列
    const std::string& dir() const { return dir_; }

    /// @brief 打ちかけの名前を返す。
    /// @return 最後の区切り文字より後ろの文字列
    const std::string& prefix() const { return prefix_; }

    /// @brief dir() の一覧をまだ受け取っていないかを返す。
    /// @return 列挙を依頼すべきなら true
    /// @note 畳んでいる間は依頼しない。入力欄を出しただけで列挙が走ると、
    ///       補完を使わない利用者にもネットワーク共有の待ちが乗る
    bool wantsListing() const { return open_ && !dir_.empty() && dir_ != listedDir_; }

    /// @brief 列挙の結果を渡す。
    /// @param[in] dir 列挙したディレクトリ。dir() と違えば候補は空になる
    /// @param[in] entries 列挙された項目。フォルダ以外は捨てる
    /// @note 失敗した列挙も空の `entries` で渡すこと。渡さないと wantsListing()
    ///       が立ったままになり、同じディレクトリを延々と列挙し続ける
    void SetListing(const std::string& dir, const std::vector<fs::Entry>& entries);

    /// @brief 候補の一覧を返す。
    /// @return 前方一致したフォルダ名。自然順に並ぶ
    const std::vector<std::string>& matches() const { return matches_; }

    /// @brief 選択中の候補の位置を返す。
    /// @return matches() への添字。何も選んでいなければ -1
    int selected() const { return sel_; }

    /// @brief 候補を採ったときに入力欄へ入る文字列を返す。
    /// @param[in] index matches() への添字
    /// @return ディレクトリと候補を繋いだフルパス。範囲外なら空文字列
    /// @note 画面に出すのもこれ。名前だけを並べると、`..` を含む入力や
    ///       スラッシュ書きのときに「どこの下の話か」が候補から読み取れない
    std::string TextAt(int index) const;

    /// @brief 選択を 1 つ動かす。
    /// @param[in] delta 正で次の候補、負で前の候補
    /// @return 文字列が変わったら true。候補が無ければ false
    /// @note 端では「打った文字列そのもの」に戻る。行きすぎたときに戻る先が
    ///       無ければ、打ち直す以外に元の入力へ帰れない
    bool Move(int delta);

    /// @brief 候補を位置で選ぶ。
    /// @param[in] index matches() への添字
    /// @return 文字列が変わったら true。範囲外なら false
    bool Select(int index);

    /// @brief 入力欄に出すべき文字列を返す。
    /// @return 選択中なら候補で置き換えた文字列、そうでなければ打った文字列
    const std::string& text() const { return text_; }

private:
    void Rebuild();

    struct Candidate {
        std::string name;
        bool hidden = false;
    };

    std::string typed_;   ///< 利用者が打った文字列
    std::string text_;    ///< 入力欄に出す文字列。選択中は候補で置き換わる
    std::string dir_;     ///< 補完対象のディレクトリ
    std::string prefix_;  ///< 打ちかけの名前

    std::string listedDir_;            ///< names_ がどのディレクトリのものか
    std::vector<Candidate> names_;     ///< listedDir_ の直下のフォルダ。自然順
    std::vector<std::string> matches_; ///< prefix_ に前方一致したもの

    int sel_ = -1;
    bool open_ = false;
};

}  // namespace kite
