#include "core/base/PathUtil.h"

#include <cctype>
#include <vector>

#include "core/base/Utf8.h"

namespace kite::path {
namespace {

// "C:\" or "\\server\share\" style roots.
size_t RootLength(std::string_view p) {
    if (p.size() >= 2 && p[1] == ':') {
        return (p.size() >= 3 && IsSep(p[2])) ? 3 : 2;
    }
    if (p.size() >= 2 && IsSep(p[0]) && IsSep(p[1])) {
        size_t i = 2;
        while (i < p.size() && !IsSep(p[i])) ++i;  // server
        if (i < p.size()) ++i;
        while (i < p.size() && !IsSep(p[i])) ++i;  // share
        if (i < p.size() && IsSep(p[i])) ++i;
        return i;
    }
    if (!p.empty() && IsSep(p[0])) return 1;
    return 0;
}

uint32_t FoldCp(uint32_t cp) {
    if (cp >= 'A' && cp <= 'Z') return cp + 32;
    return cp;
}

}  // namespace

bool IsSep(char c) { return c == '\\' || c == '/'; }

std::string Join(std::string_view a, std::string_view b) {
    if (a.empty()) return std::string(b);
    if (b.empty()) return std::string(a);
    std::string out(a);
    if (!IsSep(out.back())) out.push_back(kSep);
    size_t i = 0;
    while (i < b.size() && IsSep(b[i])) ++i;
    out.append(b.substr(i));
    return out;
}

std::string Parent(std::string_view p) {
    while (p.size() > RootLength(p) && IsSep(p.back())) p.remove_suffix(1);
    const size_t root = RootLength(p);
    if (p.size() <= root) return {};  // already a root

    size_t i = p.size();
    while (i > root && !IsSep(p[i - 1])) --i;
    while (i > root && IsSep(p[i - 1])) --i;
    if (i <= root) return std::string(p.substr(0, root));
    return std::string(p.substr(0, i));
}

std::string FileName(std::string_view p) {
    const size_t root = RootLength(p);
    while (p.size() > root && IsSep(p.back())) p.remove_suffix(1);
    if (p.size() <= root) return std::string(p);
    size_t i = p.size();
    while (i > root && !IsSep(p[i - 1])) --i;
    return std::string(p.substr(i));
}

std::string Extension(std::string_view p) {
    const std::string name = FileName(p);
    const size_t dot = name.rfind('.');
    if (dot == std::string::npos || dot == 0) return {};
    return utf8::ToLowerAscii(std::string_view(name).substr(dot + 1));
}

std::string Stem(std::string_view p) {
    std::string name = FileName(p);
    const size_t dot = name.rfind('.');
    if (dot == std::string::npos || dot == 0) return name;
    return name.substr(0, dot);
}

bool IsRoot(std::string_view p) {
    const size_t root = RootLength(p);
    return root > 0 && p.size() <= root;
}

std::string Normalize(std::string_view p) {
    const size_t root = RootLength(p);
    std::string head(p.substr(0, root));
    for (char& c : head) {
        if (c == '/') c = kSep;
    }
    if (!head.empty() && head.size() >= 2 && head[1] == ':' && head[0] >= 'a' && head[0] <= 'z') {
        head[0] = static_cast<char>(head[0] - 32);
    }

    std::vector<std::string_view> parts;
    size_t i = root;
    while (i < p.size()) {
        size_t j = i;
        while (j < p.size() && !IsSep(p[j])) ++j;
        std::string_view part = p.substr(i, j - i);
        if (part == "..") {
            if (!parts.empty()) parts.pop_back();
        } else if (!part.empty() && part != ".") {
            parts.push_back(part);
        }
        i = j;
        while (i < p.size() && IsSep(p[i])) ++i;
    }

    std::string out = head;
    for (size_t k = 0; k < parts.size(); ++k) {
        if (!out.empty() && !IsSep(out.back())) out.push_back(kSep);
        out.append(parts[k]);
    }
    if (out.empty()) out = head;
    return out;
}

std::string DisplayName(std::string_view p) {
    if (p.empty()) return {};
    std::string name = FileName(p);
    if (name.empty()) return std::string(p);
    // Trim the trailing separator on roots so tabs read "C:" not "C:\".
    while (name.size() > 1 && IsSep(name.back())) name.pop_back();
    return name;
}

int NaturalCompare(std::string_view a, std::string_view b) {
    size_t ia = 0, ib = 0;
    while (ia < a.size() && ib < b.size()) {
        const unsigned char ca = static_cast<unsigned char>(a[ia]);
        const unsigned char cb = static_cast<unsigned char>(b[ib]);

        if (std::isdigit(ca) && std::isdigit(cb)) {
            // Compare the whole digit run numerically, ignoring leading zeros.
            size_t ja = ia, jb = ib;
            while (ja < a.size() && std::isdigit(static_cast<unsigned char>(a[ja]))) ++ja;
            while (jb < b.size() && std::isdigit(static_cast<unsigned char>(b[jb]))) ++jb;
            std::string_view da = a.substr(ia, ja - ia);
            std::string_view db = b.substr(ib, jb - ib);
            size_t za = da.find_first_not_of('0');
            size_t zb = db.find_first_not_of('0');
            da = (za == std::string_view::npos) ? da.substr(da.size() - 1) : da.substr(za);
            db = (zb == std::string_view::npos) ? db.substr(db.size() - 1) : db.substr(zb);
            if (da.size() != db.size()) return da.size() < db.size() ? -1 : 1;
            const int c = da.compare(db);
            if (c != 0) return c < 0 ? -1 : 1;
            ia = ja;
            ib = jb;
            continue;
        }

        const uint32_t pa = FoldCp(utf8::Decode(a, ia));
        const uint32_t pb = FoldCp(utf8::Decode(b, ib));
        if (pa != pb) return pa < pb ? -1 : 1;
    }
    if (ia < a.size()) return 1;
    if (ib < b.size()) return -1;
    return 0;
}

std::string EscapeToken(std::string_view s) {
    static const char* kHex = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u < 0x20 || c == '%' || c == '|' || c == ',' || c == '(' || c == ')' || c == '{' ||
            c == '}' || c == '@' || c == '=' || c == '\n' || c == '\r') {
            out.push_back('%');
            out.push_back(kHex[u >> 4]);
            out.push_back(kHex[u & 0x0F]);
        } else {
            out.push_back(c);
        }
    }
    return out;
}

std::string UnescapeToken(std::string_view s) {
    auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            const int h = hex(s[i + 1]);
            const int l = hex(s[i + 2]);
            if (h >= 0 && l >= 0) {
                out.push_back(static_cast<char>((h << 4) | l));
                i += 2;
                continue;
            }
        }
        out.push_back(s[i]);
    }
    return out;
}

}  // namespace kite::path
