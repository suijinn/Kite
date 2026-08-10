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
/// 既定値は LoadDefaults() が持つ。keys.ini はその上から適用されるので、利用者は
/// 変更したい行だけ書けばよい。値 "none" はそのコマンドの割り当てをすべて解除する。
/// すでに使われている和音を割り当てると、以前のコマンドから黙って奪う。
class KeyMap {
public:
    /// @brief 組み込みの既定割り当てを読み込む。既存の内容は破棄される。
    void LoadDefaults();

    /// @brief keys.ini の内容を現在の割り当てに適用する。
    /// @param[in] ini `[keys]` セクションを含む設定
    /// @param[out] warnings 解釈できなかった行の説明の追加先。不要なら nullptr
    /// @note "none" は行の順序に関係なく、追加より先にまとめて処理される
    void ApplyIni(const Ini& ini, std::vector<std::string>* warnings = nullptr);

    /// @brief 和音に対応するコマンドを引く。
    /// @param[in] c 押された和音
    /// @return 対応するコマンド。割り当てが無ければ Cmd::None
    Cmd Lookup(const Chord& c) const;

    /// @brief コマンドに割り当てられた和音をすべて返す。
    /// @param[in] id 対象のコマンド
    /// @return 割り当て順の和音列。無ければ空
    std::vector<Chord> ChordsFor(Cmd id) const;

    /// @brief コマンドの代表的な和音を表示用文字列で返す。
    /// @param[in] id 対象のコマンド
    /// @return 最初に割り当てられた和音の文字列。無ければ空文字列
    std::string PrimaryChordText(Cmd id) const;

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

private:
    std::unordered_map<uint32_t, Cmd> byChord_;
    std::vector<std::pair<Chord, Cmd>> order_;  // insertion order, for display
};

}  // namespace kite
