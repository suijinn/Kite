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
