#include "core/base/Ini.h"

#include <cstdio>
#include <cstdlib>

#include "core/base/Utf8.h"

namespace kite {
namespace {

std::string_view Trim(std::string_view s) {
    size_t b = 0, e = s.size();
    auto ws = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    while (b < e && ws(s[b])) ++b;
    while (e > b && ws(s[e - 1])) --e;
    return s.substr(b, e - b);
}

}  // namespace

void Ini::Parse(std::string_view text) {
    sections_.clear();
    // Skip a UTF-8 BOM if present.
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF) {
        text.remove_prefix(3);
    }

    Section* cur = nullptr;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t nl = text.find('\n', pos);
        std::string_view line = Trim(text.substr(pos, nl == std::string_view::npos ? nl : nl - pos));
        pos = (nl == std::string_view::npos) ? text.size() + 1 : nl + 1;

        if (line.empty() || line[0] == '#' || line[0] == ';') continue;

        if (line.front() == '[' && line.back() == ']') {
            cur = &Ensure(Trim(line.substr(1, line.size() - 2)));
            continue;
        }
        size_t eq = line.find('=');
        if (eq == std::string_view::npos) continue;
        if (!cur) cur = &Ensure("");
        cur->entries.push_back({ std::string(Trim(line.substr(0, eq))),
                                 std::string(Trim(line.substr(eq + 1))) });
    }
}

std::string Ini::Serialize() const {
    std::string out;
    for (const Section& s : sections_) {
        if (!s.name.empty()) {
            out += '[';
            out += s.name;
            out += "]\n";
        }
        for (const Entry& e : s.entries) {
            out += e.key;
            out += '=';
            out += e.value;
            out += '\n';
        }
        out += '\n';
    }
    return out;
}

const Ini::Section* Ini::Find(std::string_view name) const {
    for (const Section& s : sections_) {
        if (utf8::EqualsIgnoreCaseAscii(s.name, name)) return &s;
    }
    return nullptr;
}

Ini::Section& Ini::Ensure(std::string_view name) {
    for (Section& s : sections_) {
        if (utf8::EqualsIgnoreCaseAscii(s.name, name)) return s;
    }
    sections_.push_back({ std::string(name), {} });
    return sections_.back();
}

std::string Ini::GetStr(std::string_view sec, std::string_view key, std::string_view def) const {
    if (const Section* s = Find(sec)) {
        for (const Entry& e : s->entries) {
            if (utf8::EqualsIgnoreCaseAscii(e.key, key)) return e.value;
        }
    }
    return std::string(def);
}

int Ini::GetInt(std::string_view sec, std::string_view key, int def) const {
    std::string v = GetStr(sec, key);
    if (v.empty()) return def;
    return std::atoi(v.c_str());
}

float Ini::GetFloat(std::string_view sec, std::string_view key, float def) const {
    std::string v = GetStr(sec, key);
    if (v.empty()) return def;
    return static_cast<float>(std::atof(v.c_str()));
}

bool Ini::GetBool(std::string_view sec, std::string_view key, bool def) const {
    std::string v = utf8::ToLowerAscii(GetStr(sec, key));
    if (v.empty()) return def;
    return v == "1" || v == "true" || v == "yes" || v == "on";
}

void Ini::Set(std::string_view sec, std::string_view key, std::string_view value) {
    Section& s = Ensure(sec);
    for (Entry& e : s.entries) {
        if (utf8::EqualsIgnoreCaseAscii(e.key, key)) {
            e.value = std::string(value);
            return;
        }
    }
    s.entries.push_back({ std::string(key), std::string(value) });
}

void Ini::SetInt(std::string_view sec, std::string_view key, int v) {
    Set(sec, key, std::to_string(v));
}

void Ini::SetFloat(std::string_view sec, std::string_view key, float v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.4g", static_cast<double>(v));
    Set(sec, key, buf);
}

void Ini::SetBool(std::string_view sec, std::string_view key, bool v) {
    Set(sec, key, v ? "true" : "false");
}

void Ini::Append(std::string_view sec, std::string_view key, std::string_view value) {
    Ensure(sec).entries.push_back({ std::string(key), std::string(value) });
}

void Ini::ClearSection(std::string_view sec) {
    Ensure(sec).entries.clear();
}

}  // namespace kite
