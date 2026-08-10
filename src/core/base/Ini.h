/// @file
/// @brief 依存ゼロの最小 INI リーダー／ライター。

#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace kite {

/// @brief INI 形式の設定を読み書きするコンテナ。
///
/// セクション内のエントリ順を保持するため、単純なキー・値設定と、順序に意味の
/// ある一覧（ブックマーク、セッション、キーバインド）の両方をこれ 1 つで扱える。
/// キーの重複は許容してそのまま保持する。
class Ini {
public:
    /// @brief キーと値の組。
    struct Entry {
        std::string key;    ///< キー名
        std::string value;  ///< 値。'=' 以降をそのまま保持する
    };

    /// @brief 名前付きのセクション。
    struct Section {
        std::string name;             ///< セクション名。角括弧は含まない
        std::vector<Entry> entries;   ///< 記述順のエントリ列
    };

    /// @brief テキストを解析して内容を置き換える。
    /// @param[in] text INI 形式のテキスト。UTF-8 BOM は読み飛ばす
    /// @note '#' と ';' で始まる行はコメント。'=' が無い行は無視する
    void Parse(std::string_view text);

    /// @brief 現在の内容を INI テキストに変換する。
    /// @return 直列化した文字列。Parse() で読み戻せる
    std::string Serialize() const;

    /// @brief セクションを名前で検索する。
    /// @param[in] name 探すセクション名。大文字小文字は区別しない
    /// @return 見つかったセクション。無ければ nullptr
    const Section* Find(std::string_view name) const;

    /// @brief セクションを取得し、無ければ作成する。
    /// @param[in] name セクション名
    /// @return 対象セクションへの参照
    Section& Ensure(std::string_view name);

    /// @brief 全セクションを返す。
    /// @return 記述順のセクション列
    const std::vector<Section>& sections() const { return sections_; }

    /// @brief 内容が空かを判定する。
    /// @return セクションが 1 つも無ければ true
    bool empty() const { return sections_.empty(); }

    /// @brief 文字列として値を取得する。
    /// @param[in] sec セクション名
    /// @param[in] key キー名
    /// @param[in] def 見つからなかった場合に返す既定値
    /// @return 最初に一致した値。無ければ `def`
    std::string GetStr(std::string_view sec, std::string_view key, std::string_view def = {}) const;

    /// @brief 整数として値を取得する。
    /// @param[in] sec セクション名
    /// @param[in] key キー名
    /// @param[in] def 見つからなかった場合に返す既定値
    /// @return 解釈した整数。無ければ `def`
    int GetInt(std::string_view sec, std::string_view key, int def = 0) const;

    /// @brief 浮動小数として値を取得する。
    /// @param[in] sec セクション名
    /// @param[in] key キー名
    /// @param[in] def 見つからなかった場合に返す既定値
    /// @return 解釈した値。無ければ `def`
    float GetFloat(std::string_view sec, std::string_view key, float def = 0.0f) const;

    /// @brief 真偽値として値を取得する。
    /// @param[in] sec セクション名
    /// @param[in] key キー名
    /// @param[in] def 見つからなかった場合に返す既定値
    /// @return "1" "true" "yes" "on" なら true。無ければ `def`
    bool GetBool(std::string_view sec, std::string_view key, bool def = false) const;

    /// @brief 値を設定する。同じキーがあれば置き換える。
    /// @param[in] sec セクション名
    /// @param[in] key キー名
    /// @param[in] value 設定する値
    void Set(std::string_view sec, std::string_view key, std::string_view value);

    /// @brief 整数値を設定する。
    /// @param[in] sec セクション名
    /// @param[in] key キー名
    /// @param[in] v 設定する値
    void SetInt(std::string_view sec, std::string_view key, int v);

    /// @brief 浮動小数値を設定する。
    /// @param[in] sec セクション名
    /// @param[in] key キー名
    /// @param[in] v 設定する値
    void SetFloat(std::string_view sec, std::string_view key, float v);

    /// @brief 真偽値を設定する。
    /// @param[in] sec セクション名
    /// @param[in] key キー名
    /// @param[in] v 設定する値
    void SetBool(std::string_view sec, std::string_view key, bool v);

    /// @brief 既存キーを置き換えず末尾に追加する。順序付き一覧を作るために使う。
    /// @param[in] sec セクション名
    /// @param[in] key キー名
    /// @param[in] value 追加する値
    void Append(std::string_view sec, std::string_view key, std::string_view value);

    /// @brief セクションの中身を空にする。セクション自体は残す。
    /// @param[in] sec セクション名
    void ClearSection(std::string_view sec);

private:
    std::vector<Section> sections_;
};

}  // namespace kite
