/// @file
/// @brief 一覧の列。並び順・幅・表示するかどうか。
///
/// **列の識別子は並べ替えの基準（`SortKey`）そのもの。** 別の列挙を立てて 1 対 1 の
/// 変換表を持つより、«列とは並べ替えられる 1 つの見方» と言い切ったほうが取り違えが
/// 起きない ─ 見出しをクリックするとその列で並ぶ、という画面の作法もそのまま
/// 「列 = 基準」と言っている。
///
/// 幅は倍率を掛ける前の DIP。掛けるのは App で、`ui/` が受け取るのは掛け算済みの
/// 値だけ（`Theme` と同じ扱い ─ ui 側に「今 1.4 倍だから」と判断する場所を作らない）。

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace kite {

/// @brief 一覧の並べ替え基準。そのまま列の識別子でもある。
enum class SortKey : uint8_t {
    Name,  ///< 名前順
    Ext,   ///< 拡張子順
    Size,  ///< サイズ順
    Date,  ///< 更新日時順

    /// 更新からの経過時間順。値は `Date` と同じ更新時刻だが、**並ぶ向きが逆**
    /// ─ 経過時間の昇順は «新しいものが先» で、更新日時の昇順は «古いものが先»。
    /// 列が答えている問いがそのまま並び順になる。
    Age,
};

/// 全部の列を宣言順に並べたもの。組み込みの既定の順でもある。
///
/// **`SortKey` に終端の列挙子を置いていない。** 置くと `switch` を書くたびに
/// «有効でない基準» の枝が要るようになる（一覧の並べ替えがまさにそれ）ので、
/// «全部» が要る場所にはこの表を配る。
constexpr SortKey kAllColumns[] = { SortKey::Name, SortKey::Ext, SortKey::Size, SortKey::Date,
                                    SortKey::Age };

/// 列の総数。
constexpr int kColumnCount = 5;

/// 列幅の下限（DIP）。これより狭いと見出しの文字が 1 つも読めない。
constexpr float kColumnMinWidth = 40.0f;

/// 列幅の上限（DIP）。名前の列を押し潰して一覧を読めなくしないための線。
constexpr float kColumnMaxWidth = 400.0f;

/// 名前の列に必ず残す幅（DIP）。これを切るところで右端の列を落とす。
constexpr float kColumnNameMinWidth = 120.0f;

/// @brief 一覧の列 1 つ。
struct Column {
    SortKey id = SortKey::Name;  ///< どの列か。並べ替えの基準でもある
    float width = 0.0f;          ///< 幅（DIP、倍率を掛ける前）。名前の列では使わない
    bool visible = true;         ///< 画面に出すか。名前の列は常に true
};

/// @brief 列の並び。先頭は必ず名前の列。
///
/// **名前の列は動かせないし消せない。** アイコンもカーソルの枠も、名前を変えるときの
/// 入力欄もその列の上に乗っているので、動かせば «行の左端が名前» という一覧そのものの
/// 読み方が崩れる。動かせるのは残りの列で、名前の列は残った幅を全部取る。
struct ColumnLayout {
    std::vector<Column> columns;  ///< 表示順。先頭は名前の列

    /// @brief 組み込みの既定を返す。
    /// @return 名前・拡張子・サイズ・更新日時・経過時間の 5 列を既定の幅で並べたもの
    static ColumnLayout Default();

    /// @brief 不足・重複・順序の乱れを直す。
    /// @note 設定ファイルを読んだ直後に必ず通すこと。書かれていない列は組み込みの
    ///       順で末尾に足す ─ 手で編集したファイルや古い版が書いたファイルに全部が
    ///       並んでいるとは限らず、落とすと画面から消えたまま戻す手段が無くなる
    ///       （サイドバーの区画と同じ規則）
    void Normalize();

    /// @brief 列の位置を返す。
    /// @param[in] id 探す列
    /// @return `columns` への添字。無ければ -1
    int IndexOf(SortKey id) const;

    /// @brief 列を返す。
    /// @param[in] id 探す列
    /// @return 対応する列。無ければ nullptr
    const Column* Find(SortKey id) const;

    /// @brief 列の幅を変える。
    /// @param[in] index 対象の列。`columns` への添字
    /// @param[in] width 新しい幅（DIP）。上下限にクランプする
    /// @return 実際に変わったら true
    /// @note 名前の列は残りを取るだけなので幅を持たない。指定しても false を返す
    bool SetWidth(int index, float width);

    /// @brief 列の幅を組み込みの既定に戻す。
    /// @param[in] index 対象の列。`columns` への添字。負なら全部の列
    /// @return 実際に変わったら true
    /// @note 戻すのは幅だけ。並びも表示も、利用者が別の操作で決めたことなので
    ///       巻き添えにしない ─ 「幅を戻す」と頼まれて隠した列が戻ってきては困る
    bool ResetWidth(int index);

    /// @brief 列を表示するかどうかを変える。
    /// @param[in] id 対象の列
    /// @param[in] visible 表示するなら true
    /// @return 実際に変わったら true
    /// @note 名前の列は常に表示。指定しても false を返す
    bool SetVisible(SortKey id, bool visible);

    /// @brief 列を並べ替える。
    /// @param[in] from 動かす列の現在位置
    /// @param[in] to 抜き取った後で入れたい位置
    /// @return 実際に動いたら true
    /// @note 先頭（名前の列）は動かせないし、その手前へも入れられない
    bool Move(int from, int to);
};

/// @brief 列（＝並べ替えの基準）の設定ファイル上の綴りを返す。
/// @param[in] id 対象の列
/// @return `"name"` などの綴り
/// @note `settings.ini` の `[view] sort` と `[columns]` が同じ綴りを使う ─
///       出どころを 2 つにすると、同じものが 2 通りの名前で書かれる
const char* ColumnName(SortKey id);

/// @brief 設定ファイル上の綴りから列を引く。
/// @param[in] name `"name"` などの綴り
/// @param[out] out 対応する列。読めなかったときは書き換えない
/// @return 読めたら true
/// @note 読めない綴りに «名前順» を答えない ─ 設定ファイルの打ち間違いが、書かれて
///       いない列を勝手に足す形で効いては、直したい人が原因に辿り着けない
bool ColumnFromName(const std::string& name, SortKey& out);

/// @brief 列の見出しに使う i18n キーを返す。
/// @param[in] id 対象の列
/// @return i18n の検索キー
const char* ColumnLabelKey(SortKey id);

}  // namespace kite
