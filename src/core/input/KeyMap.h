// Kite - chord -> command binding table.
//
// Defaults live in KeyMap::LoadDefaults(). keys.ini is applied on top, so a
// user only writes the lines they want to change. A value of "none" clears
// every binding for that command; any other value adds a chord. Binding a
// chord that is already taken silently steals it from the previous command.
#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "core/base/Ini.h"
#include "core/input/Commands.h"
#include "core/input/Keys.h"

namespace kite {

class KeyMap {
public:
    void LoadDefaults();
    // Returns human-readable warnings for unparsable lines (shown in the log).
    void ApplyIni(const Ini& ini, std::vector<std::string>* warnings = nullptr);

    Cmd Lookup(const Chord& c) const;
    std::vector<Chord> ChordsFor(Cmd id) const;
    std::string PrimaryChordText(Cmd id) const;

    // Serializes the full current mapping - written on first run so the user
    // has a complete, editable reference file.
    Ini ToIni() const;

    void Bind(const Chord& c, Cmd id);
    void UnbindCommand(Cmd id);

private:
    std::unordered_map<uint32_t, Cmd> byChord_;
    std::vector<std::pair<Chord, Cmd>> order_;  // insertion order, for display
};

}  // namespace kite
