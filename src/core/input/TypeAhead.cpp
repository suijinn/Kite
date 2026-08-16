#include "core/input/TypeAhead.h"

#include "core/base/Utf8.h"

namespace kite {
namespace {

// Whether the whole string is the same character over and over. "ss" typed in a
// folder with no "ss..." item is the user asking for the next "s", not for a
// name nobody has.
bool AllSameChar(std::string_view s) {
    if (s.empty()) return false;
    size_t i = 0;
    const uint32_t first = utf8::Decode(s, i);
    while (i < s.size()) {
        if (utf8::Decode(s, i) != first) return false;
    }
    return true;
}

std::string FirstChar(std::string_view s) {
    size_t i = 0;
    utf8::Decode(s, i);
    return std::string(s.substr(0, i));
}

}  // namespace

bool TypeAhead::active(uint64_t nowMs) const {
    return !text_.empty() && nowMs - lastMs_ <= kTimeoutMs;
}

void TypeAhead::Clear() {
    text_.clear();
    lastMs_ = 0;
}

int TypeAhead::Find(std::string_view needle, const IRows& rows, int from, bool skipCurrent) const {
    const int count = rows.Count();
    if (count <= 0) return -1;
    if (from < 0 || from >= count) from = 0;

    // Wrapping is what makes a single letter walk the list: pressed often enough
    // it visits every item starting with it and comes back round.
    const int start = skipCurrent ? from + 1 : from;
    for (int step = 0; step < count; ++step) {
        const int row = ((start + step) % count + count) % count;
        const std::string_view name = rows.NameAt(row);
        if (name.empty()) continue;
        if (utf8::StartsWithIgnoreCaseAscii(name, needle)) return row;
    }
    return -1;
}

TypeAhead::Jump TypeAhead::Type(uint32_t codepoint, const IRows& rows, int from, uint64_t nowMs) {
    if (codepoint < 0x20 || codepoint == 0x7F) return {};
    if (!active(nowMs)) text_.clear();

    std::string next = text_;
    utf8::Encode(codepoint, next);
    // A space on its own belongs to the selection toggle; inside a name being
    // typed it is just another letter.
    if (next == " ") return {};

    // A fresh letter steps past the current row, so pressing it again moves on
    // instead of answering with the row already under the cursor. Adding to what
    // is already typed searches from the cursor, because the row it is sitting
    // on is usually the one being narrowed down to.
    const bool fresh = text_.empty();
    int row = Find(next, rows, from, fresh);
    if (row >= 0) {
        text_ = next;
        lastMs_ = nowMs;
        return { true, row };
    }

    // "aa" with no "aa..." item in sight: read it as the second press of "a".
    // Trying the pair first is what keeps both readings available - a folder
    // that really does hold "aardvark" is still reachable by typing it.
    if (!fresh && AllSameChar(next)) {
        const std::string single = FirstChar(next);
        row = Find(single, rows, from, true);
        if (row >= 0) {
            text_ = single;
            lastMs_ = nowMs;
            return { true, row };
        }
    }

    // The keystroke is still swallowed and the clock still restarts: the user is
    // in the middle of typing a name, and letting a mistyped letter expire the
    // buffer early would drop the letters that came before it too.
    lastMs_ = nowMs;
    return { true, -1 };
}

bool TypeAhead::Erase(uint64_t nowMs) {
    if (!active(nowMs)) return false;
    text_.erase(utf8::PrevBoundary(text_, text_.size()));
    lastMs_ = nowMs;
    return true;
}

}  // namespace kite
