#include "core/input/PathComplete.h"

#include <algorithm>

#include "core/base/PathUtil.h"

namespace kite {
namespace {

char Fold(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c;
}

// Prefix match, case-insensitive over ASCII only. Multi-byte names compare byte
// for byte, which is what UTF-8 gives for free as long as both sides were typed
// with the same characters - and case folding outside ASCII would need a table
// this project does not carry.
bool StartsWithFold(const std::string& name, const std::string& prefix) {
    if (name.size() < prefix.size()) return false;
    for (size_t i = 0; i < prefix.size(); ++i) {
        if (Fold(name[i]) != Fold(prefix[i])) return false;
    }
    return true;
}

}  // namespace

void PathComplete::Reset() {
    *this = PathComplete{};
}

void PathComplete::SetInput(const std::string& text) {
    typed_ = text;
    text_ = text;
    sel_ = -1;

    size_t sep = std::string::npos;
    for (size_t i = text.size(); i > 0; --i) {
        if (path::IsSep(text[i - 1])) {
            sep = i - 1;
            break;
        }
    }

    // Relative input has no directory to look in: the prompt hands its text to
    // NavigateFocused, which resolves nothing against a working directory.
    if (sep == std::string::npos || !path::IsAbsolute(text)) {
        dir_.clear();
        prefix_.clear();
        matches_.clear();
        return;
    }

    dir_ = path::Normalize(text.substr(0, sep + 1));
    prefix_ = text.substr(sep + 1);
    Rebuild();
}

void PathComplete::SetListing(const std::string& dir, const std::vector<fs::Entry>& entries) {
    listedDir_ = dir;
    names_.clear();
    for (const fs::Entry& e : entries) {
        if (!e.isDir()) continue;
        names_.push_back({ e.name, e.isHidden() });
    }
    std::sort(names_.begin(), names_.end(), [](const Candidate& a, const Candidate& b) {
        return path::NaturalCompare(a.name, b.name) < 0;
    });
    Rebuild();
}

void PathComplete::Rebuild() {
    matches_.clear();
    sel_ = -1;
    text_ = typed_;
    if (dir_.empty() || dir_ != listedDir_) return;

    for (const Candidate& c : names_) {
        // Nothing typed yet means "show me what is in here", and the answer to
        // that should not open with $Recycle.Bin. Once a name is started the
        // hidden ones come back: the user is asking for something by name, and
        // refusing to finish a name they can see the start of is just wrong.
        if (prefix_.empty() && c.hidden) continue;
        if (!StartsWithFold(c.name, prefix_)) continue;
        matches_.push_back(c.name);
    }
}

std::string PathComplete::TextAt(int index) const {
    if (index < 0 || index >= static_cast<int>(matches_.size())) return {};
    return path::Join(dir_, matches_[static_cast<size_t>(index)]);
}

bool PathComplete::Move(int delta) {
    if (matches_.empty() || delta == 0) return false;
    const int count = static_cast<int>(matches_.size());
    int next = sel_ + (delta > 0 ? 1 : -1);
    if (next >= count) next = -1;
    if (next < -1) next = count - 1;
    sel_ = next;
    text_ = (sel_ < 0) ? typed_ : TextAt(sel_);
    return true;
}

bool PathComplete::Select(int index) {
    if (index < 0 || index >= static_cast<int>(matches_.size())) return false;
    sel_ = index;
    text_ = TextAt(index);
    return true;
}

}  // namespace kite
