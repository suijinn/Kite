// Kite - localization.
//
// Every user-visible string goes through Strings::Get(). Built-in tables cover
// English and Japanese; a lang/<code>.ini in the config folder overrides or
// adds entries, so a new language never requires a rebuild.
#pragma once

#include <initializer_list>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "core/base/Ini.h"

namespace kite {

class Strings {
public:
    // `code` is "en", "ja", ... Unknown codes fall back to English.
    void Load(std::string_view code);
    void ApplyOverrides(const Ini& ini);

    const std::string& code() const { return code_; }

    // Returns the key itself when missing, which makes gaps obvious.
    const std::string& Get(std::string_view key) const;

    // Like Get(), but "foo.bar_3" also matches a "foo.bar_n" entry whose text
    // contains {n}. Keeps 24 numbered command labels down to 3 table rows.
    std::string Label(std::string_view key) const;

    // Replaces {0}, {1}, ... in the looked-up string.
    std::string Format(std::string_view key, std::initializer_list<std::string_view> args) const;

    static std::vector<std::string> AvailableCodes();

private:
    std::string code_;
    std::unordered_map<std::string, std::string> map_;
};

}  // namespace kite
