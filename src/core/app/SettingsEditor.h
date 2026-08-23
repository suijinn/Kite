/// @file
/// @brief 設定画面の状態と操作。
///
/// 画面に出す行の組み立て、カーソル移動、値の増減までをここが持つ。UI 層は rows() を
/// 描いて入力を渡すだけで、判断は一切しない。OS にも描画にも触れないので、設定画面の
/// 挙動は tests/test_settings.cpp が端から端まで検証できる。KeyEditor と同じ関係。
///
/// **設定の値そのものは持たない。** 保持しているのは「選択肢の何番目か」だけで、実体は
/// App にある。直前に変わった項目は changed() が答えるので、App はその 1 項目だけを
/// 反映する ─ 全項目を書き戻す作りにすると、この画面が選択肢を持たない値
/// （`lang.fr.ini` を置いた人の `language = fr` など）まで先頭の選択肢で上書きされる。

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/i18n/Strings.h"
#include "core/input/Keys.h"

namespace kite {

/// @brief 新しいタブを作る位置。
enum class NewTabPosition : uint8_t {
    End,           ///< 一番右（末尾）に作る
    AfterCurrent,  ///< 今のタブのすぐ右に作る
};

/// @brief タブバーを置く場所。向きもこれで決まる。
enum class TabBarPosition : uint8_t {
    Top,   ///< 一覧の上。横に並べ、収まらなければ行を増やす
    Left,  ///< 一覧の左。縦に積み、収まらなければスクロールする
};

/// @brief 設定画面が扱う項目。
///
/// @note 並び順がそのまま画面の並び順になる。SettingGroupOf() が区画を返すので、
///       同じ区画の項目は続けて並べること
enum class SettingId : uint8_t {
    Theme,            ///< 明暗テーマ
    Language,         ///< 表示言語
    FontScale,        ///< 文字の大きさ
    Sidebar,          ///< サイドバーを表示するか
    ShellIcons,       ///< シェルの実アイコンを使うか
    TabBarPos,        ///< タブバーを置く場所（上＝横並び／左＝縦並び）
    NewTabPos,        ///< 新しいタブを作る位置
    OpenArchives,     ///< ZIP をフォルダとして開くか
    NewTabHidden,     ///< 新しいタブの既定：隠しファイルを表示するか
    NewTabDirsFirst,  ///< 新しいタブの既定：フォルダを先頭にまとめるか
    /// 既定のファイルマネージャーとして登録するか。
    ///
    /// **この 1 行だけは `settings.ini` に無い。** 実体は OS 側（レジストリ）に
    /// あるので、値は開くたびに OS へ訊き直す ─ Kite の外で変えられていても、
    /// 行が嘘をつかない。**変更には確認が要る**（`PromptKind::ConfirmDefaultManager`）。
    /// 効いたかどうかが Kite の中からは見えない唯一の行なので、矢印キーがかすった
    /// だけで `Win+E` の行き先が変わってはならない。
    DefaultManager,
    Count             ///< 列挙の終端。有効な項目ではない
};

/// @brief 設定画面の区画。見出し行の単位。
enum class SettingGroup : uint8_t {
    Appearance,  ///< 外観
    Tabs,        ///< タブ
    Files,       ///< ファイル
    NewTab,      ///< 新しいタブの既定
    System,      ///< OS との連携。ここだけ実体が settings.ini の外にある
    Count        ///< 列挙の終端。有効な区画ではない
};

/// @brief 設定画面が保持する値。すべて選択肢の添字で表す。
struct SettingsValues {
    /// 項目ごとの選択肢の添字。SettingId を添字に使う。
    int index[static_cast<size_t>(SettingId::Count)] = {};

    /// @brief 項目の値を返す。
    /// @param[in] id 対象の項目。SettingId::Count なら 0
    /// @return 選択肢の添字
    int Get(SettingId id) const;

    /// @brief 項目の値を設定する。
    /// @param[in] id 対象の項目。SettingId::Count なら何もしない
    /// @param[in] value 選択肢の添字。範囲外はクランプする
    void Set(SettingId id, int value);
};

/// @brief 項目が属する区画を返す。
/// @param[in] id 対象の項目
/// @return 対応する区画。SettingId::Count では SettingGroup::Count
SettingGroup SettingGroupOf(SettingId id);

/// @brief 区画の見出しに使う i18n キーを返す。
/// @param[in] group 対象の区画
/// @return i18n の検索キー
const char* SettingGroupLabelKey(SettingGroup group);

/// @brief 項目のラベルに使う i18n キーを返す。
/// @param[in] id 対象の項目
/// @return i18n の検索キー。SettingId::Count では空文字列
const char* SettingLabelKey(SettingId id);

/// @brief 項目が取りうる選択肢の数を返す。
/// @param[in] id 対象の項目
/// @return 選択肢の数。SettingId::Count では 0
int SettingOptionCount(SettingId id);

/// @brief 選択肢の表示文字列を組み立てる。
/// @param[in] strings 表示名の取得に使う文字列表
/// @param[in] id 対象の項目
/// @param[in] index 選択肢の添字。範囲外はクランプする
/// @return 表示文字列
std::string SettingOptionText(const Strings& strings, SettingId id, int index);

/// @brief 文字サイズの倍率を選択肢の添字に直す。
/// @param[in] scale 倍率
/// @return 最も近い選択肢の添字
/// @note `Ctrl++` の刻みと同じ 0.1 きざみなので、キーで変えた値もそのまま乗る
int FontScaleIndex(float scale);

/// @brief 選択肢の添字を文字サイズの倍率に直す。
/// @param[in] index 選択肢の添字。範囲外はクランプする
/// @return 倍率
float FontScaleValue(int index);

/// @brief 言語コードを選択肢の添字に直す。
/// @param[in] code 言語コード。"auto" / "en" / "ja"
/// @return 選択肢の添字。どれでもなければ 0（自動）
int LanguageIndex(const std::string& code);

/// @brief 選択肢の添字を言語コードに直す。
/// @param[in] index 選択肢の添字。範囲外はクランプする
/// @return 言語コード
const char* LanguageCode(int index);

/// @brief 設定画面。
///
/// 表示中は生のキー入力をすべて飲み込む。KeyEditor と同じ理由 ─ 設定を変えている
/// 最中に、そのキーに割り当てられたコマンドが暴発しては設定にならない。
class SettingsEditor {
public:
    /// @brief 一覧の 1 行。
    struct Row {
        SettingId id = SettingId::Count;  ///< 対象の項目。見出し行では Count
        bool header = false;              ///< 区画の見出し行か
        std::string label;                ///< 表示名。見出し行では区画名
        std::string value;                ///< 現在の選択肢の表示文字列
        bool atFirst = false;             ///< 選択肢の先頭にいる（左へは動かせない）
        bool atLast = false;              ///< 選択肢の末尾にいる（右へは動かせない）
    };

    /// @brief 設定画面を開く。
    /// @param[in] strings 表示名の取得に使う文字列表
    /// @param[in] values 現在の設定値
    void Open(const Strings& strings, const SettingsValues& values);

    /// @brief 設定画面を閉じる。
    void Close();

    /// @brief 一覧を作り直す。
    /// @param[in] strings 表示名の取得に使う文字列表
    /// @note 言語を変えると全行のラベルが変わるので、値を変えたあとは必ず呼ぶ
    void Rebuild(const Strings& strings);

    /// @brief 保持している設定値を差し替える。
    /// @param[in] values 新しい設定値
    /// @param[in] strings 表示名の取得に使う文字列表
    /// @note **カーソルは動かさない。** 使うのは «行が動かした値が実際には変わって
    ///       いなかった» ときで（確認を取り消した、OS が断った）、指していた行から
    ///       手を離させる理由が無い
    void SetValues(const SettingsValues& values, const Strings& strings);

    /// @brief 表示中かを返す。
    /// @return 表示中なら true
    bool visible() const { return visible_; }

    /// @brief 一覧の行を返す。
    /// @return 見出しを含む全行
    const std::vector<Row>& rows() const { return rows_; }

    /// @brief 選択中の行番号を返す。
    /// @return rows() への添字。行が無ければ -1
    int cursor() const { return cursor_; }

    /// @brief 保持している設定値を返す。
    /// @return 選択肢の添字の集まり
    const SettingsValues& values() const { return values_; }

    /// @brief 直前の操作で値が変わった項目を返す。
    /// @return 変わった項目。無ければ SettingId::Count
    /// @note App はこの 1 項目だけを反映する。反映したら ClearChanged() を呼ぶこと
    SettingId changed() const { return changed_; }

    /// @brief 変更ありの印を落とす。
    void ClearChanged() { changed_ = SettingId::Count; }

    /// @brief 選択中の項目を返す。
    /// @return 選択中の項目。見出し行または選択なしなら SettingId::Count
    SettingId selected() const;

    /// @brief 行を選択する。見出し行と範囲外は無視する。
    /// @param[in] index rows() への添字
    void SelectRow(int index);

    /// @brief 選択を移動する。見出し行は読み飛ばす。
    /// @param[in] delta 移動量。absolute が true なら移動先の行番号
    /// @param[in] absolute true なら delta を移動先の行番号として扱う
    void MoveCursor(int delta, bool absolute = false);

    /// @brief 選択中の項目の値を動かす。
    /// @param[in] delta 動かす量。正で次の選択肢へ
    /// @param[in] strings 表示名の取得に使う文字列表
    /// @param[in] wrap true なら端で反対側へ回り込む
    /// @return 値が実際に変わったら true
    /// @note `←` `→` は回り込まない（wrap = false）。文字サイズのように選択肢が
    ///       多い項目で、端から端へ飛ぶのは事故にしかならない。Enter と Space だけが
    ///       回り込む ─ 押すたびに次へ進む「切り替え」であって、方向の指示ではない
    bool Adjust(int delta, const Strings& strings, bool wrap = false);

    /// @brief キー入力を処理する。
    /// @param[in] chord 押された和音
    /// @param[in] strings 表示名の取得に使う文字列表
    /// @return 消費したら true。表示していなければ false
    /// @note 表示中は Escape で閉じる以外のすべてを消費する
    bool HandleKey(const Chord& chord, const Strings& strings);

private:
    void EnsureCursorOnItem();

    std::vector<Row> rows_;
    SettingsValues values_;
    SettingId changed_ = SettingId::Count;
    int cursor_ = -1;
    bool visible_ = false;
};

}  // namespace kite
