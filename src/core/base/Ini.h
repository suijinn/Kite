// Kite - minimal dependency-free INI reader/writer.
//
// Entry order inside a section is preserved, so the same container backs both
// plain key/value settings and ordered lists (bookmarks, sessions, keybinds).
// Duplicate keys are allowed and kept; GetStr() returns the first match.
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace kite {

class Ini {
public:
    struct Entry {
        std::string key;
        std::string value;
    };
    struct Section {
        std::string name;
        std::vector<Entry> entries;
    };

    void Parse(std::string_view text);
    std::string Serialize() const;

    const Section* Find(std::string_view name) const;
    Section& Ensure(std::string_view name);
    const std::vector<Section>& sections() const { return sections_; }
    bool empty() const { return sections_.empty(); }

    std::string GetStr(std::string_view sec, std::string_view key, std::string_view def = {}) const;
    int GetInt(std::string_view sec, std::string_view key, int def = 0) const;
    float GetFloat(std::string_view sec, std::string_view key, float def = 0.0f) const;
    bool GetBool(std::string_view sec, std::string_view key, bool def = false) const;

    // Replaces the first entry with this key, or appends when absent.
    void Set(std::string_view sec, std::string_view key, std::string_view value);
    void SetInt(std::string_view sec, std::string_view key, int v);
    void SetFloat(std::string_view sec, std::string_view key, float v);
    void SetBool(std::string_view sec, std::string_view key, bool v);

    // Always appends, keeping duplicates - for ordered lists.
    void Append(std::string_view sec, std::string_view key, std::string_view value);

    void ClearSection(std::string_view sec);

private:
    std::vector<Section> sections_;
};

}  // namespace kite
