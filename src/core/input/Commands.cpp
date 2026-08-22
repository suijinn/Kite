#include "core/input/Commands.h"

#include <array>

#include "core/base/Utf8.h"

namespace kite {
namespace {

std::vector<CommandInfo> BuildTable() {
    std::vector<CommandInfo> table;
    table.reserve(static_cast<size_t>(Cmd::Count));
#define KITE_X(id, name, label, group) table.push_back({ Cmd::id, name, label, CmdGroup::group });
    KITE_COMMAND_LIST(KITE_X)
#undef KITE_X
    return table;
}

// keys.ini に書かれうる旧名。
//
// **読むときだけ効く。** 書き出す（KeyMap::ToIni）のは常に現在の名前で、そうしないと
// 一度書き戻した時点でファイルの中に 2 つの綴りが並ぶ。名前を変えたコマンドを持つ
// 既存の設定ファイルは、その行がそのまま生きる ─ ここを外すと、利用者が割り当てた和音が
// 「不明なコマンド」の警告 1 行と引き換えに静かに既定へ戻る。
//
// **表を増やさないこと。** 名前は keys.ini の中で唯一「表示言語で動かない綴り」なので、
// 変えるのは画面の実態とずれたときだけでよい。
struct CommandAlias {
    const char* name;  ///< 旧名
    Cmd id;            ///< 今その名前が指すコマンド
};

constexpr CommandAlias kAliases[] = {
    // 0.1.x までの名前。画面はブックマークだけを並べていたが、開いているタブと
    // ドライブが増えて «行き先» になった（Cmd::ShowPlaces / nav.places）。
    { "bookmark.list", Cmd::ShowPlaces },
};

}  // namespace

const std::vector<CommandInfo>& AllCommands() {
    static const std::vector<CommandInfo> table = BuildTable();
    return table;
}

const CommandInfo* FindCommand(Cmd id) {
    if (id == Cmd::None || id >= Cmd::Count) return nullptr;
    const std::vector<CommandInfo>& table = AllCommands();
    const size_t index = static_cast<size_t>(id) - 1;  // Cmd::None occupies 0
    if (index >= table.size()) return nullptr;
    return &table[index];
}

Cmd CommandFromName(std::string_view name) {
    for (const CommandInfo& c : AllCommands()) {
        if (utf8::EqualsIgnoreCaseAscii(name, c.name)) return c.id;
    }
    // Current names first: an alias must never win over a name in the table, or
    // renaming a command could quietly steal a binding from a different one.
    for (const CommandAlias& a : kAliases) {
        if (utf8::EqualsIgnoreCaseAscii(name, a.name)) return a.id;
    }
    return Cmd::None;
}

const char* CommandName(Cmd id) {
    const CommandInfo* info = FindCommand(id);
    return info ? info->name : "";
}

const char* GroupLabelKey(CmdGroup g) {
    switch (g) {
        case CmdGroup::App:      return "group.app";
        case CmdGroup::Navigate: return "group.navigate";
        case CmdGroup::Select:   return "group.select";
        case CmdGroup::Tab:      return "group.tab";
        case CmdGroup::Pane:     return "group.pane";
        case CmdGroup::Session:  return "group.session";
        case CmdGroup::View:     return "group.view";
        case CmdGroup::Bookmark: return "group.bookmark";
        case CmdGroup::File:     return "group.file";
        case CmdGroup::Shell:    return "group.shell";
        default:                 return "group.other";
    }
}

}  // namespace kite
