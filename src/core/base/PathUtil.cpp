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

// The length of a leading scheme like "virtual:", or 0 if there is none.
//
// Two lower-case letters at least, so a drive letter ("c:") never matches: one
// letter followed by a colon is a drive on Windows and nothing else.
size_t SchemeLength(std::string_view p) {
    size_t i = 0;
    while (i < p.size() && p[i] >= 'a' && p[i] <= 'z') ++i;
    if (i < 2 || i >= p.size() || p[i] != ':') return 0;
    return i + 1;
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
    if (p.size() <= root) {
        // A share is a root as far as the filesystem is concerned, but the
        // server above it holds the list of shares, so it is somewhere to go.
        const size_t server = UncServerLength(p);
        if (server > 0 && p.find_first_not_of("\\/", server) != std::string_view::npos) {
            return std::string(p.substr(0, server));
        }
        return {};  // already a root
    }

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

std::string DuplicateName(std::string_view name, int attempt, bool wholeName) {
    const std::string leaf = FileName(name);
    const std::string stem = wholeName ? leaf : Stem(leaf);
    // Not Extension(): that answers in lower case, which would rewrite ".TXT"
    // on its way past. What is wanted here is the bytes the name already has.
    const std::string ext = leaf.substr(stem.size());

    std::string out = stem;
    out += kCopySuffix;
    // The first copy carries no number, so the second is the one that reads "2" -
    // the count the user sees, not the attempt.
    if (attempt > 0) out += std::to_string(attempt + 1);
    out += ext;
    return out;
}

size_t PrevSegment(std::string_view p, size_t pos) {
    size_t i = pos < p.size() ? pos : p.size();
    while (i > 0 && IsSep(p[i - 1])) --i;
    while (i > 0 && !IsSep(p[i - 1])) --i;
    return i;
}

size_t NextSegment(std::string_view p, size_t pos) {
    size_t i = pos < p.size() ? pos : p.size();
    while (i < p.size() && IsSep(p[i])) ++i;
    while (i < p.size() && !IsSep(p[i])) ++i;
    return i;
}

bool IsAbsolute(std::string_view p) { return RootLength(p) > 0; }

bool IsRoot(std::string_view p) {
    const size_t root = RootLength(p);
    return root > 0 && p.size() <= root;
}

size_t UncServerLength(std::string_view p) {
    if (p.size() < 3 || !IsSep(p[0]) || !IsSep(p[1])) return 0;
    size_t i = 2;
    while (i < p.size() && !IsSep(p[i])) ++i;
    return i > 2 ? i : 0;
}

bool IsUncServer(std::string_view p) {
    const size_t server = UncServerLength(p);
    if (server == 0) return false;
    return p.find_first_not_of("\\/", server) == std::string_view::npos;
}

std::string UncRoot(std::string_view p) {
    const size_t server = UncServerLength(p);
    if (server == 0) return {};
    const size_t shareBegin = p.find_first_not_of("\\/", server);
    if (shareBegin == std::string_view::npos) return std::string(p.substr(0, server));
    size_t shareEnd = shareBegin;
    while (shareEnd < p.size() && !IsSep(p[shareEnd])) ++shareEnd;
    std::string out(p.substr(0, server));
    out.push_back(kSep);
    out.append(p.substr(shareBegin, shareEnd - shareBegin));
    return out;
}

bool IsInside(std::string_view child, std::string_view parent) {
    // ルート末尾の区切りは「この後に来るものがある」という印で、境目そのもの
    // ではない。両側から落としてから比べ、境目は child 側の 1 文字で見る ─
    // 落とすのを片側だけにすると、ルートが自分自身の中にあることになる。
    std::string c = Normalize(child);
    std::string up = Normalize(parent);
    while (c.size() > 1 && IsSep(c.back())) c.pop_back();
    while (up.size() > 1 && IsSep(up.back())) up.pop_back();
    if (c.empty() || up.empty()) return false;
    if (c.size() <= up.size()) return false;
    if (!utf8::EqualsIgnoreCaseAscii(std::string_view(c).substr(0, up.size()), up)) return false;
    // ここが無いと、名前の頭が同じだけの兄弟が「下」になる。
    return IsSep(c[up.size()]);
}

std::string Normalize(std::string_view p) {
    // A scheme is carried through untouched and only what follows it is folded.
    // Treating the whole string as one path eats the two leading backslashes of
    // a UNC body - "virtual:\\nas\pub" would come back as "virtual:\nas\pub",
    // which names nothing. A shell parsing name ("::{CLSID}") is left entirely
    // alone: it is not a path and has no components to fold.
    if (const size_t scheme = SchemeLength(p)) {
        const std::string_view body = p.substr(scheme);
        if (body.empty() || (body.size() >= 2 && body[0] == ':' && body[1] == ':')) {
            return std::string(p);
        }
        return std::string(p.substr(0, scheme)) + Normalize(body);
    }

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
    // "\\srv\" and "\\srv" name the same place, and a drive's trailing separator
    // has no counterpart here: there is no share to end. Settling on one spelling
    // keeps a tab from looking like it moved when it did not.
    const size_t server = UncServerLength(out);
    if (server > 0 && out.find_first_not_of("\\/", server) == std::string::npos) {
        out.resize(server);
    }
    return out;
}

std::string ToExtended(std::string_view p) {
    // MAX_PATH is 260 including the NUL, and a directory handed to a listing
    // grows by the "\*" pattern on top of that. Switching over a little early
    // costs nothing; being 2 characters short of the limit costs the folder.
    constexpr size_t kThreshold = 240;

    if (p.size() < kThreshold) return std::string(p);  // bytes >= UTF-16 units
    if (p.size() >= 4 && IsSep(p[0]) && IsSep(p[1]) && (p[2] == '?' || p[2] == '.') &&
        IsSep(p[3])) {
        return std::string(p);  // already extended, or a device path
    }
    if (utf8::Utf16Length(p) < kThreshold) return std::string(p);

    // The extended form bypasses the normalization Win32 would otherwise do,
    // so whatever this hands back has to already be in its final spelling.
    const std::string full = Normalize(p);
    const size_t server = UncServerLength(full);
    if (server > 0) return "\\\\?\\UNC\\" + full.substr(2);
    if (full.size() >= 3 && full[1] == ':' && IsSep(full[2])) return "\\\\?\\" + full;
    // A relative path, or "\foo" with no drive: the extended form needs a full
    // path, and this layer does not know the current directory to make one.
    return full;
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
