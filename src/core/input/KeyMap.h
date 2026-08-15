/// @file
/// @brief 和音からコマンドへの割り当て表。

#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "core/base/Ini.h"
#include "core/input/Commands.h"
#include "core/input/Keys.h"

namespace kite {

/// @brief キーバインドの保持と解決を行う。
///
/// 既定値は LoadDefaults() が持つ。keys.ini はその上から適用され、**書かれている
/// コマンドについてはファイルの内容がそのまま答えになる**（既定は残らない）。
/// 触れていないコマンドは既定のまま。値 "none" は割り当て無しを意味する。
/// すでに使われている和音を割り当てると、以前のコマンドから黙って奪う。
class KeyMap {
public:
    /// @brief 組み込みの既定割り当てを読み込む。既存の内容は破棄される。
    void LoadDefaults();

    /// @brief keys.ini の内容を現在の割り当てに適用する。
    /// @param[in] ini `[keys]` セクションを含む設定
    /// @param[out] warnings 解釈できなかった行の説明の追加先。不要なら nullptr
    /// @note 名前の挙がったコマンドの割り当ては、まとめて先に解除してから
    ///       ファイルの順で入れ直す。1 つのコマンドを複数行書けばその全部が付く
    /// @note 既定を足すのではなく置き換えるのは、生成した keys.ini の行を書き換えた
    ///       ときに既定が残ると、一覧に出るのが先に割り当てられた既定のほうになり、
    ///       編集がどこにも反映されないように見えるため
    void ApplyIni(const Ini& ini, std::vector<std::string>* warnings = nullptr);

    /// @brief 和音に対応するコマンドを引く。
    /// @param[in] c 押された和音
    /// @return 対応するコマンド。割り当てが無ければ Cmd::None
    Cmd Lookup(const Chord& c) const;

    /// @brief コマンドに割り当てられた和音をすべて返す。
    /// @param[in] id 対象のコマンド
    /// @return 割り当て順の和音列。無ければ空
    std::vector<Chord> ChordsFor(Cmd id) const;

    /// @brief コマンドに割り当てられた和音を表示用の 1 行にまとめて返す。
    /// @param[in] id 対象のコマンド
    /// @return 割り当て順に ", " で連ねた文字列。無ければ空文字列
    /// @note 代表の 1 つだけを返していた頃は、2 つ目以降を割り当てても画面に
    ///       出るのは常に最初の 1 つで、増やしたことが確かめられなかった
    std::string ChordText(Cmd id) const;

    /// @brief 現在の割り当て全体を INI に書き出す。
    /// @return `[keys]` セクションを持つ設定
    /// @note 初回起動時にこれを出力し、利用者向けのリファレンス兼編集対象にしている
    Ini ToIni() const;

    /// @brief 和音にコマンドを割り当てる。
    /// @param[in] c 割り当てる和音。無効な和音は無視される
    /// @param[in] id 割り当てるコマンド
    /// @note 既に他のコマンドが使っている和音なら、そちらの割り当ては解除される
    void Bind(const Chord& c, Cmd id);

    /// @brief コマンドへの割り当てをすべて解除する。
    /// @param[in] id 対象のコマンド
    void UnbindCommand(Cmd id);

    /// @brief 和音 1 つぶんの割り当てを解除する。
    /// @param[in] c 解除する和音。割り当てが無ければ何もしない
    /// @note 同じコマンドに複数の和音がある場合に、その 1 つだけを外すために使う
    void Unbind(const Chord& c);

    /// @brief 組み込みの既定割り当てを返す。現在の割り当ては見ない。
    /// @param[in] id 対象のコマンド
    /// @return 既定の和音列。既定を持たないコマンドでは空
    /// @note 設定画面の「既定に戻す」が、1 コマンドだけを戻すために使う
    static std::vector<Chord> DefaultChordsFor(Cmd id);

private:
    std::unordered_map<uint32_t, Cmd> byChord_;
    std::vector<std::pair<Chord, Cmd>> order_;  // insertion order, for display
};

}  // namespace kite
