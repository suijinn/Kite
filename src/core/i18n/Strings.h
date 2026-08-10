/// @file
/// @brief 表示文字列のローカライズ。
///
/// 利用者から見える文字列はすべて Strings::Get() を通す。組み込みの表は英語と
/// 日本語。設定フォルダに置いた lang/&lt;code&gt;.ini が上書き・追加するので、
/// 言語を増やすのに再ビルドは要らない。

#pragma once

#include <initializer_list>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "core/base/Ini.h"

namespace kite {

/// @brief 言語ごとの文字列表。
class Strings {
public:
    /// @brief 言語表を読み込む。
    /// @param[in] code "en" や "ja" などの言語コード。未知のコードは英語にする
    /// @note 英語を先に読み込んでから対象言語を重ねるため、翻訳が不完全でも
    ///       未訳部分は英語で表示される
    void Load(std::string_view code);

    /// @brief 利用者定義の文字列で上書きする。
    /// @param[in] ini `[strings]` セクションを含む設定
    void ApplyOverrides(const Ini& ini);

    /// @brief 現在読み込まれている言語コードを返す。
    /// @return "en" または "ja" など
    const std::string& code() const { return code_; }

    /// @brief 文字列を引く。
    /// @param[in] key 検索キー
    /// @return 対応する文字列。見つからない場合はキー自身（欠落を目立たせるため）
    const std::string& Get(std::string_view key) const;

    /// @brief 番号付きキーの代替を含めて文字列を引く。
    /// @param[in] key 検索キー
    /// @return 対応する文字列。"foo.bar_3" は "foo.bar_n" の `{n}` を 3 に
    ///         置換した結果にもなる。どちらも無ければキー自身
    /// @note 番号違いだけの 24 個のコマンドラベルを表 3 行で賄うための仕組み
    std::string Label(std::string_view key) const;

    /// @brief 引数を差し込んだ文字列を返す。
    /// @param[in] key 検索キー
    /// @param[in] args `{0}` `{1}` … に順に差し込む文字列
    /// @return 置換後の文字列
    std::string Format(std::string_view key, std::initializer_list<std::string_view> args) const;

    /// @brief 組み込みで持っている言語コードを返す。
    /// @return 言語コードの一覧
    static std::vector<std::string> AvailableCodes();

private:
    std::string code_;
    std::unordered_map<std::string, std::string> map_;
};

}  // namespace kite
