#include "core/base/Utf8.h"

namespace kite::utf8 {

uint32_t Decode(std::string_view s, size_t& i) {
    if (i >= s.size()) return 0;
    const unsigned char c0 = static_cast<unsigned char>(s[i]);
    if (c0 < 0x80) {
        ++i;
        return c0;
    }
    auto cont = [&](size_t k) -> bool {
        return i + k < s.size() && (static_cast<unsigned char>(s[i + k]) & 0xC0) == 0x80;
    };
    auto tail = [&](size_t k) -> uint32_t {
        return static_cast<unsigned char>(s[i + k]) & 0x3F;
    };

    if ((c0 & 0xE0) == 0xC0 && cont(1)) {
        uint32_t cp = ((c0 & 0x1Fu) << 6) | tail(1);
        i += 2;
        return cp;
    }
    if ((c0 & 0xF0) == 0xE0 && cont(1) && cont(2)) {
        uint32_t cp = ((c0 & 0x0Fu) << 12) | (tail(1) << 6) | tail(2);
        i += 3;
        return cp;
    }
    if ((c0 & 0xF8) == 0xF0 && cont(1) && cont(2) && cont(3)) {
        uint32_t cp = ((c0 & 0x07u) << 18) | (tail(1) << 12) | (tail(2) << 6) | tail(3);
        i += 4;
        return cp;
    }
    ++i;
    return kReplacement;
}

void Encode(uint32_t cp, std::string& out) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

std::string Encode(uint32_t cp) {
    std::string s;
    Encode(cp, s);
    return s;
}

size_t PrevBoundary(std::string_view s, size_t i) {
    if (i == 0) return 0;
    --i;
    while (i > 0 && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) --i;
    return i;
}

size_t NextBoundary(std::string_view s, size_t i) {
    if (i >= s.size()) return s.size();
    ++i;
    while (i < s.size() && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) ++i;
    return i;
}

size_t CharCount(std::string_view s) {
    size_t n = 0;
    for (size_t i = 0; i < s.size(); i = NextBoundary(s, i)) ++n;
    return n;
}

std::string ToLowerAscii(std::string_view s) {
    std::string out(s);
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
    }
    return out;
}

bool EqualsIgnoreCaseAscii(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        char ca = a[i], cb = b[i];
        if (ca >= 'A' && ca <= 'Z') ca = static_cast<char>(ca + 32);
        if (cb >= 'A' && cb <= 'Z') cb = static_cast<char>(cb + 32);
        if (ca != cb) return false;
    }
    return true;
}

bool IsWide(uint32_t cp) {
    return (cp >= 0x1100 && cp <= 0x115F) ||   // Hangul Jamo
           (cp >= 0x2E80 && cp <= 0xA4CF) ||   // CJK radicals .. Yi
           (cp >= 0xAC00 && cp <= 0xD7A3) ||   // Hangul syllables
           (cp >= 0xF900 && cp <= 0xFAFF) ||   // CJK compatibility ideographs
           (cp >= 0xFE30 && cp <= 0xFE6F) ||   // CJK compatibility forms
           (cp >= 0xFF00 && cp <= 0xFF60) ||   // Fullwidth forms
           (cp >= 0xFFE0 && cp <= 0xFFE6) ||
           (cp >= 0x20000 && cp <= 0x3FFFD);   // CJK extension B+
}

}  // namespace kite::utf8
