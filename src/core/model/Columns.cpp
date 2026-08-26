#include "core/model/Columns.h"

#include <algorithm>

namespace kite {
namespace {

// 既定の幅（DIP）。列に入る文字がそのまま決めている ─ 拡張子は 3〜4 文字、サイズは
// 「123.4 MB」、更新日時は「2026-08-09 17:11」の 16 文字。
float DefaultWidth(SortKey id) {
    switch (id) {
        case SortKey::Ext: return 58.0f;
        case SortKey::Size: return 84.0f;
        case SortKey::Date: return 124.0f;
        case SortKey::Age: return 84.0f;
        default: return 0.0f;  // 名前の列は残りを取る
    }
}

}  // namespace

ColumnLayout ColumnLayout::Default() {
    ColumnLayout layout;
    for (SortKey id : kAllColumns) {
        layout.columns.push_back({ id, DefaultWidth(id), true });
    }
    return layout;
}

void ColumnLayout::Normalize() {
    std::vector<Column> out;
    out.reserve(kColumnCount);

    // 名前の列が先頭。書かれていなければ既定で作る ─ 幅も表示も持たない列なので、
    // 読めなかったことによる違いはどこにも出ない。
    Column name = { SortKey::Name, 0.0f, true };
    for (const Column& c : columns) {
        if (c.id == SortKey::Name) name = c;
    }
    name.id = SortKey::Name;
    name.width = 0.0f;
    name.visible = true;
    out.push_back(name);

    auto has = [&](SortKey id) {
        return std::any_of(out.begin(), out.end(), [&](const Column& c) { return c.id == id; });
    };
    for (const Column& c : columns) {
        if (has(c.id)) continue;  // 二度目の言及は捨てる。最初の位置が答え
        Column copy = c;
        copy.width = std::clamp(copy.width, kColumnMinWidth, kColumnMaxWidth);
        out.push_back(copy);
    }
    // 書かれていなかった列は組み込みの順で末尾に足す。落とすと画面から消えたまま、
    // 設定ファイルを直す以外に戻す手段が無くなる。
    for (SortKey id : kAllColumns) {
        if (!has(id)) out.push_back({ id, DefaultWidth(id), true });
    }
    columns = std::move(out);
}

int ColumnLayout::IndexOf(SortKey id) const {
    for (size_t i = 0; i < columns.size(); ++i) {
        if (columns[i].id == id) return static_cast<int>(i);
    }
    return -1;
}

const Column* ColumnLayout::Find(SortKey id) const {
    const int index = IndexOf(id);
    return index < 0 ? nullptr : &columns[static_cast<size_t>(index)];
}

bool ColumnLayout::SetWidth(int index, float width) {
    if (index <= 0 || index >= static_cast<int>(columns.size())) return false;
    const float value = std::clamp(width, kColumnMinWidth, kColumnMaxWidth);
    Column& column = columns[static_cast<size_t>(index)];
    if (column.width == value) return false;
    column.width = value;
    return true;
}

bool ColumnLayout::ResetWidth(int index) {
    const int count = static_cast<int>(columns.size());
    bool changed = false;
    for (int i = 1; i < count; ++i) {  // 名前の列は幅を持たない
        if (index >= 0 && i != index) continue;
        Column& column = columns[static_cast<size_t>(i)];
        const float width = DefaultWidth(column.id);
        if (column.width == width) continue;
        column.width = width;
        changed = true;
    }
    return changed;
}

bool ColumnLayout::SetVisible(SortKey id, bool visible) {
    const int index = IndexOf(id);
    if (index <= 0) return false;  // 名前の列は消せない
    Column& column = columns[static_cast<size_t>(index)];
    if (column.visible == visible) return false;
    column.visible = visible;
    return true;
}

bool ColumnLayout::Move(int from, int to) {
    const int count = static_cast<int>(columns.size());
    if (from <= 0 || from >= count) return false;
    to = std::clamp(to, 1, count - 1);  // 名前の列の手前には入れられない
    if (from == to) return false;

    const Column moved = columns[static_cast<size_t>(from)];
    columns.erase(columns.begin() + from);
    columns.insert(columns.begin() + to, moved);
    return true;
}

const char* ColumnName(SortKey id) {
    switch (id) {
        case SortKey::Ext: return "ext";
        case SortKey::Size: return "size";
        case SortKey::Date: return "date";
        case SortKey::Age: return "age";
        default: return "name";
    }
}

bool ColumnFromName(const std::string& name, SortKey& out) {
    for (SortKey id : kAllColumns) {
        if (name == ColumnName(id)) {
            out = id;
            return true;
        }
    }
    return false;
}

const char* ColumnLabelKey(SortKey id) {
    switch (id) {
        case SortKey::Ext: return "ui.ext";
        case SortKey::Size: return "ui.size";
        case SortKey::Date: return "ui.modified";
        case SortKey::Age: return "ui.age";
        default: return "ui.name";
    }
}

}  // namespace kite
