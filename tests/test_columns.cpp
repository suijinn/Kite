// 一覧の列 ─ 並び・幅・表示するかどうか。設定ファイルから読んだ列を Normalize() が
// どう直すかまで含めて、画面にも OS にも触れずに検証できる。
#include "TestFramework.h"
#include "core/model/Columns.h"

using namespace kite;

namespace {

// 列の並びを "name,ext,size,date,age" の形で書き出す。
std::string Order(const ColumnLayout& layout) {
    std::string out;
    for (const Column& column : layout.columns) {
        if (!out.empty()) out += ",";
        out += ColumnName(column.id);
    }
    return out;
}

}  // namespace

KITE_TEST(columns, default_holds_every_column_once) {
    const ColumnLayout layout = ColumnLayout::Default();
    KITE_EXPECT_EQ(static_cast<int>(layout.columns.size()), kColumnCount);
    KITE_EXPECT_EQ(Order(layout), std::string("name,ext,size,date,age"));
    for (const Column& column : layout.columns) KITE_EXPECT(column.visible);
}

KITE_TEST(columns, name_is_always_first_and_never_hidden) {
    ColumnLayout layout = ColumnLayout::Default();
    KITE_EXPECT_FALSE(layout.SetVisible(SortKey::Name, false));
    KITE_EXPECT_FALSE(layout.Move(0, 2));
    KITE_EXPECT_EQ(Order(layout), std::string("name,ext,size,date,age"));

    // 名前の手前へも入れられない。要求された位置は先頭の «次» に丸める。
    KITE_EXPECT(layout.Move(3, 0));
    KITE_EXPECT_EQ(Order(layout), std::string("name,date,ext,size,age"));
}

KITE_TEST(columns, move_reorders_the_rest) {
    ColumnLayout layout = ColumnLayout::Default();
    KITE_EXPECT(layout.Move(1, 3));
    KITE_EXPECT_EQ(Order(layout), std::string("name,size,date,ext,age"));
    KITE_EXPECT_FALSE(layout.Move(2, 2));
}

KITE_TEST(columns, width_is_clamped_and_name_has_none) {
    ColumnLayout layout = ColumnLayout::Default();
    const int date = layout.IndexOf(SortKey::Date);

    KITE_EXPECT(layout.SetWidth(date, 1000.0f));
    KITE_EXPECT_NEAR(layout.Find(SortKey::Date)->width, kColumnMaxWidth, 0.01f);
    KITE_EXPECT(layout.SetWidth(date, 1.0f));
    KITE_EXPECT_NEAR(layout.Find(SortKey::Date)->width, kColumnMinWidth, 0.01f);
    // 同じ幅にし直しても «変わった» とは言わない ─ 言うと、ドラッグの 1 フレームごとに
    // 設定ファイルを書き直すことになる。
    KITE_EXPECT_FALSE(layout.SetWidth(date, kColumnMinWidth));

    // 名前の列は残りを取るだけなので幅を持たない。
    KITE_EXPECT_FALSE(layout.SetWidth(0, 200.0f));
}

KITE_TEST(columns, normalize_appends_missing_columns) {
    ColumnLayout layout;
    layout.columns.push_back({ SortKey::Date, 90.0f, false });
    layout.Normalize();

    // 名前が先頭に生え、書かれていた列はその位置のまま、残りが組み込みの順で末尾へ。
    KITE_EXPECT_EQ(Order(layout), std::string("name,date,ext,size,age"));
    KITE_EXPECT_FALSE(layout.Find(SortKey::Date)->visible);
    KITE_EXPECT_NEAR(layout.Find(SortKey::Date)->width, 90.0f, 0.01f);
}

KITE_TEST(columns, normalize_drops_repeats_and_keeps_the_first) {
    ColumnLayout layout;
    layout.columns.push_back({ SortKey::Size, 70.0f, true });
    layout.columns.push_back({ SortKey::Ext, 50.0f, true });
    layout.columns.push_back({ SortKey::Size, 200.0f, false });
    layout.Normalize();

    KITE_EXPECT_EQ(Order(layout), std::string("name,size,ext,date,age"));
    KITE_EXPECT_NEAR(layout.Find(SortKey::Size)->width, 70.0f, 0.01f);
    KITE_EXPECT(layout.Find(SortKey::Size)->visible);
}

KITE_TEST(columns, names_round_trip) {
    for (SortKey id : kAllColumns) {
        SortKey back = SortKey::Date;
        KITE_EXPECT(ColumnFromName(ColumnName(id), back));
        KITE_EXPECT_EQ(static_cast<int>(back), static_cast<int>(id));
    }
    // 読めない綴りに «名前順» を答えない。打ち間違いが書かれていない列を足す形で
    // 効いては、直したい人が原因に辿り着けない。
    SortKey unused = SortKey::Size;
    KITE_EXPECT_FALSE(ColumnFromName("modified", unused));
    KITE_EXPECT_EQ(static_cast<int>(unused), static_cast<int>(SortKey::Size));
}

KITE_TEST(columns, resetting_a_width_touches_only_that_column) {
    ColumnLayout layout = ColumnLayout::Default();
    const float wasDate = layout.Find(SortKey::Date)->width;
    const int size = layout.IndexOf(SortKey::Size);
    const int date = layout.IndexOf(SortKey::Date);

    layout.SetWidth(size, 200.0f);
    layout.SetWidth(date, 200.0f);
    KITE_EXPECT(layout.ResetWidth(date));
    KITE_EXPECT_NEAR(layout.Find(SortKey::Date)->width, wasDate, 0.01f);
    KITE_EXPECT_NEAR(layout.Find(SortKey::Size)->width, 200.0f, 0.01f);

    // すでに既定なら «変わった» とは言わない。
    KITE_EXPECT_FALSE(layout.ResetWidth(date));
}

KITE_TEST(columns, resetting_every_width_leaves_order_and_visibility_alone) {
    ColumnLayout layout = ColumnLayout::Default();
    const ColumnLayout defaults = ColumnLayout::Default();

    layout.Move(layout.IndexOf(SortKey::Date), 1);
    layout.SetVisible(SortKey::Ext, false);
    for (size_t i = 1; i < layout.columns.size(); ++i) layout.SetWidth(static_cast<int>(i), 250.0f);

    KITE_EXPECT(layout.ResetWidth(-1));
    for (const Column& column : layout.columns) {
        KITE_EXPECT_NEAR(column.width, defaults.Find(column.id)->width, 0.01f);
    }
    // 幅だけ。「幅を戻す」と頼んで隠した列が戻ってきては困る。
    KITE_EXPECT_EQ(layout.IndexOf(SortKey::Date), 1);
    KITE_EXPECT_FALSE(layout.Find(SortKey::Ext)->visible);
}
