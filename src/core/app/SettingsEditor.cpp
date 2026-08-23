#include "core/app/SettingsEditor.h"

#include <algorithm>
#include <cmath>

namespace kite {
namespace {

// 1 項目分の定義。並び順は SettingId の宣言順そのもので、画面の並びにもなる。
struct Info {
    SettingId id;
    SettingGroup group;
    const char* labelKey;
    int options;
    // 選択肢のラベル。nullptr なら SettingOptionText() が値から組み立てる
    // （文字サイズのように選択肢が数値そのものである項目）。
    const char* const* optionKeys;
};

const char* const kOnOff[] = { "settings.off", "settings.on" };
// 「オン／オフ」ではなく「いいえ／はい」。この行が訊いているのは機能の入り切りでは
// なく «Kite が既定のファイルマネージャーであるか» という事実。
const char* const kNoYes[] = { "settings.no", "settings.yes" };
const char* const kTheme[] = { "settings.theme.dark", "settings.theme.light" };
const char* const kLanguage[] = { "settings.language.auto", "settings.language.en",
                                  "settings.language.ja" };
const char* const kNewTabPos[] = { "settings.new_tab_pos.end", "settings.new_tab_pos.after" };
const char* const kTabBarPos[] = { "settings.tab_bar_pos.top", "settings.tab_bar_pos.left" };

// 文字サイズの刻み。Ctrl++ / Ctrl+- と同じ 0.1 きざみで、範囲も App の
// kFontScaleMin / kFontScaleMax に合わせてある ─ 片方だけ広げると、キーでは
// 出せる倍率が設定画面では選べない（またはその逆）ことになる。
constexpr float kFontScaleBase = 0.7f;
constexpr float kFontScaleStep = 0.1f;
constexpr int kFontScaleCount = 14;  // 0.7 .. 2.0

const Info kSettings[] = {
    { SettingId::Theme, SettingGroup::Appearance, "settings.theme", 2, kTheme },
    { SettingId::Language, SettingGroup::Appearance, "settings.language", 3, kLanguage },
    { SettingId::FontScale, SettingGroup::Appearance, "settings.font_scale", kFontScaleCount,
      nullptr },
    { SettingId::Sidebar, SettingGroup::Appearance, "settings.sidebar", 2, kOnOff },
    { SettingId::ShellIcons, SettingGroup::Appearance, "settings.shell_icons", 2, kOnOff },
    { SettingId::TabBarPos, SettingGroup::Tabs, "settings.tab_bar_pos", 2, kTabBarPos },
    { SettingId::NewTabPos, SettingGroup::Tabs, "settings.new_tab_pos", 2, kNewTabPos },
    { SettingId::OpenArchives, SettingGroup::Files, "settings.open_archives", 2, kOnOff },
    { SettingId::NewTabHidden, SettingGroup::NewTab, "settings.show_hidden", 2, kOnOff },
    { SettingId::NewTabDirsFirst, SettingGroup::NewTab, "settings.dirs_first", 2, kOnOff },
    { SettingId::DefaultManager, SettingGroup::System, "settings.default_manager", 2, kNoYes },
};

const Info* Find(SettingId id) {
    for (const Info& info : kSettings) {
        if (info.id == id) return &info;
    }
    return nullptr;
}

}  // namespace

int SettingsValues::Get(SettingId id) const {
    if (id == SettingId::Count) return 0;
    return index[static_cast<size_t>(id)];
}

void SettingsValues::Set(SettingId id, int value) {
    if (id == SettingId::Count) return;
    index[static_cast<size_t>(id)] = std::clamp(value, 0, std::max(0, SettingOptionCount(id) - 1));
}

SettingGroup SettingGroupOf(SettingId id) {
    const Info* info = Find(id);
    return info ? info->group : SettingGroup::Count;
}

const char* SettingGroupLabelKey(SettingGroup group) {
    switch (group) {
        case SettingGroup::Appearance: return "settings.group.appearance";
        case SettingGroup::Tabs: return "settings.group.tabs";
        case SettingGroup::Files: return "settings.group.files";
        case SettingGroup::NewTab: return "settings.group.new_tab";
        case SettingGroup::System: return "settings.group.system";
        default: return "settings.group.appearance";
    }
}

const char* SettingLabelKey(SettingId id) {
    const Info* info = Find(id);
    return info ? info->labelKey : "";
}

int SettingOptionCount(SettingId id) {
    const Info* info = Find(id);
    return info ? info->options : 0;
}

std::string SettingOptionText(const Strings& strings, SettingId id, int index) {
    const Info* info = Find(id);
    if (!info) return {};
    const int i = std::clamp(index, 0, info->options - 1);
    if (info->optionKeys) return strings.Get(info->optionKeys[i]);
    // 数値の項目は今のところ文字サイズだけ。ここに項目が増えるなら Info に単位を
    // 持たせること ─ 「数値ならパーセント」は今たまたま成り立っているだけ。
    const int percent = static_cast<int>(FontScaleValue(i) * 100.0f + 0.5f);
    return strings.Format("settings.percent", { std::to_string(percent) });
}

int FontScaleIndex(float scale) {
    const int i = static_cast<int>(std::lround((scale - kFontScaleBase) / kFontScaleStep));
    return std::clamp(i, 0, kFontScaleCount - 1);
}

float FontScaleValue(int index) {
    return kFontScaleBase +
           kFontScaleStep * static_cast<float>(std::clamp(index, 0, kFontScaleCount - 1));
}

int LanguageIndex(const std::string& code) {
    if (code == "en") return 1;
    if (code == "ja") return 2;
    return 0;
}

const char* LanguageCode(int index) {
    switch (std::clamp(index, 0, 2)) {
        case 1: return "en";
        case 2: return "ja";
        default: return "auto";
    }
}

// ---------------------------------------------------------------------------

void SettingsEditor::Open(const Strings& strings, const SettingsValues& values) {
    values_ = values;
    changed_ = SettingId::Count;
    cursor_ = -1;
    visible_ = true;
    Rebuild(strings);
}

void SettingsEditor::SetValues(const SettingsValues& values, const Strings& strings) {
    values_ = values;
    // Rebuild() は選択を項目で覚え直すので、カーソルはそのまま残る。
    Rebuild(strings);
}

void SettingsEditor::Close() {
    visible_ = false;
    changed_ = SettingId::Count;
}

void SettingsEditor::Rebuild(const Strings& strings) {
    // 選択は行番号ではなく項目で覚え直す。行数は固定だが、言語を変えると
    // ラベルの長さが変わるだけで並びは同じなので、これで十分。
    const SettingId keep = selected();

    rows_.clear();
    SettingGroup lastGroup = SettingGroup::Count;
    for (const Info& info : kSettings) {
        if (info.group != lastGroup) {
            lastGroup = info.group;
            Row header;
            header.header = true;
            header.label = strings.Get(SettingGroupLabelKey(info.group));
            rows_.push_back(std::move(header));
        }
        const int value = values_.Get(info.id);
        Row row;
        row.id = info.id;
        row.label = strings.Get(info.labelKey);
        row.value = SettingOptionText(strings, info.id, value);
        row.atFirst = (value <= 0);
        row.atLast = (value >= info.options - 1);
        rows_.push_back(std::move(row));
    }

    cursor_ = -1;
    for (size_t i = 0; i < rows_.size(); ++i) {
        if (!rows_[i].header && rows_[i].id == keep) {
            cursor_ = static_cast<int>(i);
            break;
        }
    }
    if (cursor_ < 0) EnsureCursorOnItem();
}

SettingId SettingsEditor::selected() const {
    if (cursor_ < 0 || cursor_ >= static_cast<int>(rows_.size())) return SettingId::Count;
    return rows_[static_cast<size_t>(cursor_)].id;
}

void SettingsEditor::SelectRow(int index) {
    if (index < 0 || index >= static_cast<int>(rows_.size())) return;
    if (rows_[static_cast<size_t>(index)].header) return;
    cursor_ = index;
}

void SettingsEditor::MoveCursor(int delta, bool absolute) {
    if (rows_.empty()) return;
    const int count = static_cast<int>(rows_.size());
    int target = absolute ? delta : cursor_ + delta;
    const int step = (delta >= 0 || absolute) ? 1 : -1;

    target = std::clamp(target, 0, count - 1);
    // 見出し行の上には止まらない。行き止まりまで来たら反対向きに探し直す ─
    // 先頭が見出し行なので、Home で「どこにも行けない」が起きうる。
    int i = target;
    while (i >= 0 && i < count && rows_[static_cast<size_t>(i)].header) i += step;
    if (i < 0 || i >= count) {
        i = target;
        while (i >= 0 && i < count && rows_[static_cast<size_t>(i)].header) i -= step;
    }
    if (i >= 0 && i < count) cursor_ = i;
}

bool SettingsEditor::Adjust(int delta, const Strings& strings, bool wrap) {
    const SettingId id = selected();
    if (id == SettingId::Count || delta == 0) return false;

    const int count = SettingOptionCount(id);
    if (count <= 1) return false;

    const int current = values_.Get(id);
    int next = current + delta;
    if (wrap) {
        next = ((next % count) + count) % count;
    } else {
        next = std::clamp(next, 0, count - 1);
    }
    if (next == current) return false;

    values_.Set(id, next);
    changed_ = id;
    Rebuild(strings);
    return true;
}

bool SettingsEditor::HandleKey(const Chord& chord, const Strings& strings) {
    if (!visible_) return false;

    switch (chord.key) {
        case Key::Escape:
            Close();
            return true;
        case Key::Up:
            MoveCursor(-1);
            return true;
        case Key::Down:
            MoveCursor(1);
            return true;
        case Key::Home:
        case Key::PageUp:
            MoveCursor(0, true);
            return true;
        case Key::End:
        case Key::PageDown:
            MoveCursor(static_cast<int>(rows_.size()) - 1, true);
            return true;
        case Key::Left:
            Adjust(-1, strings);
            return true;
        case Key::Right:
            Adjust(1, strings);
            return true;
        case Key::Enter:
        case Key::Space:
            Adjust(1, strings, true);
            return true;
        default:
            break;
    }
    // 表示中は残らず飲み込む。設定を変えている最中に、そのキーに割り当てられた
    // コマンドが暴発しては設定にならない（KeyEditor と同じ）。
    return true;
}

void SettingsEditor::EnsureCursorOnItem() {
    for (size_t i = 0; i < rows_.size(); ++i) {
        if (!rows_[i].header) {
            cursor_ = static_cast<int>(i);
            return;
        }
    }
    cursor_ = -1;
}

}  // namespace kite
