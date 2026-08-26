#include "core/model/Workspace.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>

#include "core/base/Format.h"
#include "core/base/PathUtil.h"
#include "core/base/Utf8.h"
#include "core/fs/VirtualPath.h"

namespace kite {
namespace {

// Move one element of an owning list, keeping the same element active across the
// move. `to` is the index the element should end up at once it has been lifted
// out, which is the convention every reorder in Kite is written in.
//
// Tabs and sessions do exactly this; the two used to say it twice, and an index
// fix applied to one of them would have left the other quietly wrong.
template <typename T>
bool ReorderKeepingActive(std::vector<std::unique_ptr<T>>& items, int& active, int fromIndex,
                          int toIndex) {
    const int count = static_cast<int>(items.size());
    if (fromIndex < 0 || fromIndex >= count) return false;
    toIndex = std::clamp(toIndex, 0, count - 1);
    if (fromIndex == toIndex) return false;

    std::unique_ptr<T> moved = std::move(items[fromIndex]);
    items.erase(items.begin() + fromIndex);
    items.insert(items.begin() + toIndex, std::move(moved));

    if (active == fromIndex) {
        active = toIndex;
    } else if (fromIndex < active && toIndex >= active) {
        --active;
    } else if (fromIndex > active && toIndex <= active) {
        ++active;
    }
    return true;
}

// 塊の見出し 1 つ分。text は言語に依らない綴り、labelKey は言葉が要るときだけ。
struct GroupKey {
    std::string text;
    std::string labelKey;
};

// 名前の頭文字。ASCII の英字は大文字に畳み、数字はまとめて 1 つの塊にする ─
// 「0-9」だけは畳まないと、数字で始まる名前が並ぶフォルダで塊が 10 個できる。
// それ以外（記号・かな・漢字）は最初の 1 文字がそのまま塊の名前になる。
std::string InitialOf(const std::string& name) {
    if (name.empty()) return {};
    size_t i = 0;
    const uint32_t cp = utf8::Decode(name, i);
    if (cp >= '0' && cp <= '9') return "0-9";
    if (cp >= 'a' && cp <= 'z') return std::string(1, static_cast<char>(cp - 'a' + 'A'));
    return utf8::Encode(cp);
}

// サイズの塊。単位の綴りは列と同じ（FormatSize と揃えてある）。境目に «だいたい
// このくらい» で答えられる粒度を選んである ─ 1 バイト刻みで塊を作っても、探して
// いる «大きいファイル» は見つからない。
std::string SizeBucketOf(uint64_t bytes) {
    constexpr uint64_t kMB = 1024ull * 1024ull;
    if (bytes == 0) return "0 B";
    if (bytes < kMB) return "< 1 MB";
    if (bytes < 10 * kMB) return "1 - 10 MB";
    if (bytes < 100 * kMB) return "10 - 100 MB";
    if (bytes < 1024 * kMB) return "100 MB - 1 GB";
    return "> 1 GB";
}

// その項目が属する塊。並べ替えの基準がそのまま塊の基準になるので、ここで見るのは
// view.sort ひとつ。
GroupKey GroupKeyOf(const fs::Entry& e, const ViewState& view) {
    // フォルダを先頭にまとめてあるなら、その «先頭のまとまり» がそのまま 1 つの塊。
    // 拡張子とサイズでは、まとめていなくてもフォルダは自分の塊にする ─ 列が
    // 「<DIR>」としか言えない値で塊を作っても、名前が付かない。
    if (e.isDir() && (view.dirsFirst || view.sort == SortKey::Ext ||
                      view.sort == SortKey::Size)) {
        return { {}, "ui.group_folders" };
    }
    switch (view.sort) {
        case SortKey::Ext: {
            const std::string ext = path::Extension(e.name);
            if (ext.empty()) return { {}, "ui.group_no_ext" };
            return { ext, {} };
        }
        case SortKey::Size:
            return { SizeBucketOf(e.size), {} };
        case SortKey::Date:
        case SortKey::Age: {
            // 年月まで。日付そのものは列に出ているし、1 日ごとの塊は «先月ぶん» を
            // 探している目には細かすぎる。切り出す先を FormatDateTime にしてある
            // のは、ローカル時刻への直し方を 2 通り持たないため。
            //
            // **経過時間で並べていても見出しは年月。** 「3 日前」でまとめるには
            // Rebuild() が時計を持つことになり、しかも同じ一覧が翌日には別の塊に
            // 割れる ─ 値は同じ更新時刻なので、動かない綴りのほうで言う。
            const std::string stamp = FormatDateTime(e.mtime);
            if (stamp.size() < 7) return { {}, "ui.group_unknown" };
            return { stamp.substr(0, 7), {} };
        }
        default: {
            const std::string initial = InitialOf(e.name);
            if (initial.empty()) return { {}, "ui.group_unknown" };
            return { initial, {} };
        }
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// Tab
// ---------------------------------------------------------------------------

std::string Tab::title() const {
    // 仮想フォルダの名前はパスに書かれていない。列挙が答えを持って帰ってくる
    // ので、あればそちらを使う。
    if (!listing.title.empty()) return listing.title;
    if (vfs::IsVirtual(path)) {
        std::string trailing = vfs::TrailingName(path);
        if (!trailing.empty()) return trailing;
    }
    std::string t = path::DisplayName(path);
    return t.empty() ? path : t;
}

void Tab::Rebuild() {
    const std::vector<fs::Entry>& entries = listing.entries;
    marked.resize(entries.size(), 0);

    // 「..」には名前で覚えられる実体が無いので、「先頭行に載っていた」ことを
    // そのまま覚える。利用者が自分で置いたカーソルを再列挙で奪わないため。
    const bool onParentRow = IsParentRow(cursor);

    std::string keepName = pendingFocusName;
    pendingFocusName.clear();
    if (keepName.empty()) {
        if (const fs::Entry* e = EntryAt(cursor)) keepName = e->name;
    }

    const std::string needle = utf8::ToLowerAscii(filter);

    visible.clear();
    visible.reserve(entries.size());
    for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
        const fs::Entry& e = entries[i];
        if (!view.showHidden && e.isHidden()) continue;
        if (!needle.empty() && utf8::ToLowerAscii(e.name).find(needle) == std::string::npos) {
            continue;
        }
        visible.push_back(i);
    }

    const ViewState v = view;
    std::stable_sort(visible.begin(), visible.end(), [&](int lhs, int rhs) {
        const fs::Entry& a = entries[lhs];
        const fs::Entry& b = entries[rhs];
        if (v.dirsFirst && a.isDir() != b.isDir()) return a.isDir();

        int cmp = 0;
        switch (v.sort) {
            case SortKey::Name:
                cmp = path::NaturalCompare(a.name, b.name);
                break;
            case SortKey::Ext: {
                cmp = path::NaturalCompare(path::Extension(a.name), path::Extension(b.name));
                if (cmp == 0) cmp = path::NaturalCompare(a.name, b.name);
                break;
            }
            case SortKey::Size:
                cmp = (a.size == b.size) ? path::NaturalCompare(a.name, b.name)
                                         : (a.size < b.size ? -1 : 1);
                break;
            case SortKey::Date:
                cmp = (a.mtime == b.mtime) ? path::NaturalCompare(a.name, b.name)
                                           : (a.mtime < b.mtime ? -1 : 1);
                break;
            case SortKey::Age:
                // 見ている値は更新日時と同じだが、向きが逆。この列が答えているのは
                // «どれだけ前か» なので、昇順は «経過が短い» ＝ 新しいものが先。
                cmp = (a.mtime == b.mtime) ? path::NaturalCompare(a.name, b.name)
                                           : (a.mtime > b.mtime ? -1 : 1);
                break;
        }
        return v.sortDesc ? cmp > 0 : cmp < 0;
    });

    // 塊の見出しは並べ替えの «後» に挿す。塊とは同じ値が続いている範囲のことなので、
    // 並び終わるまではどこで切れるのかが決まらない。
    groups.clear();
    if (view.grouped) {
        std::vector<int> rows;
        rows.reserve(visible.size() + 8);
        GroupKey last;
        for (int index : visible) {
            GroupKey key = GroupKeyOf(entries[index], view);
            if (groups.empty() || key.text != last.text || key.labelKey != last.labelKey) {
                Group group;
                group.text = key.text;
                group.labelKey = key.labelKey;
                group.firstRow = static_cast<int>(rows.size());
                groups.push_back(std::move(group));
                rows.push_back(kGroupRowBase - static_cast<int>(groups.size() - 1));
                last = std::move(key);
            }
            ++groups.back().count;
            rows.push_back(index);
        }
        visible.swap(rows);
    }

    // 並べ替えの後に挿す。「..」は名前でも日付でも動かない。読めなかったフォルダに
    // は出さない ─ 画面はエラーだけを出すので、触れない行が残るだけになる。
    if (listing.status == fs::Status::Ok && !vfs::ParentOf(path).empty()) {
        visible.insert(visible.begin(), kParentRow);
        // 見出しの行番号は 1 つずつ後ろへ。ここを忘れると、塊の見出しがその塊の
        // 最後の項目を指す。
        for (Group& group : groups) ++group.firstRow;
    }

    if (!keepName.empty()) {
        for (int i = 0; i < static_cast<int>(visible.size()); ++i) {
            if (visible[i] < 0) continue;
            if (entries[visible[i]].name == keepName) {
                cursor = i;
                break;
            }
        }
    }
    if (visible.empty()) {
        cursor = 0;
    } else {
        cursor = std::clamp(cursor, 0, static_cast<int>(visible.size()) - 1);
    }
    // 添字がずれて「..」に載っただけなら最初の項目へ送る。新しいフォルダを開いた
    // 直後（cursor = 0）に、まず目に入るのが移動手段では困る。
    if (!onParentRow && IsParentRow(cursor) && ItemCount() > 0) cursor = 1;
    // 見出しの上には止まらない。並べ替えを変えただけで塊の切れ目が動くので、
    // 添字がそのまま残っていると、まさにその見出しにカーソルが乗る。
    cursor = SkipGroupRows(cursor, 1);
    ResetAnchor();
}

void Tab::DropListing() {
    listing.entries.clear();
    listing.entries.shrink_to_fit();
    listing.status = fs::Status::Ok;
    listing.message.clear();
    visible.clear();
    visible.shrink_to_fit();
    marked.clear();
    marked.shrink_to_fit();
    groups.clear();
    groups.shrink_to_fit();
    loaded = false;
    ResetAnchor();
}

bool Tab::IsParentRow(int visibleIndex) const {
    if (visibleIndex < 0 || visibleIndex >= static_cast<int>(visible.size())) return false;
    return visible[visibleIndex] == kParentRow;
}

bool Tab::IsGroupRow(int visibleIndex) const {
    if (visibleIndex < 0 || visibleIndex >= static_cast<int>(visible.size())) return false;
    return visible[visibleIndex] <= kGroupRowBase;
}

const Tab::Group* Tab::GroupAt(int visibleIndex) const {
    if (!IsGroupRow(visibleIndex)) return nullptr;
    const int index = kGroupRowBase - visible[visibleIndex];
    if (index < 0 || index >= static_cast<int>(groups.size())) return nullptr;
    return &groups[static_cast<size_t>(index)];
}

int Tab::SkipGroupRows(int visibleIndex, int direction) const {
    const int count = static_cast<int>(visible.size());
    if (count == 0) return visibleIndex;
    const int step = direction < 0 ? -1 : 1;
    for (int i = visibleIndex; i >= 0 && i < count; i += step) {
        if (!IsGroupRow(i)) return i;
    }
    // 進みたい向きが行き止まりだった。見出しの下には必ず項目があるので、逆向きなら
    // 必ず見つかる ─ 一覧の末尾が見出しになるのは、塊が空のときだけで、それは無い。
    for (int i = visibleIndex; i >= 0 && i < count; i -= step) {
        if (!IsGroupRow(i)) return i;
    }
    return visibleIndex;
}

const fs::Entry* Tab::EntryAt(int visibleIndex) const {
    if (visibleIndex < 0 || visibleIndex >= static_cast<int>(visible.size())) return nullptr;
    const int index = visible[visibleIndex];
    if (index < 0 || index >= static_cast<int>(listing.entries.size())) return nullptr;
    return &listing.entries[index];
}

int Tab::ItemCount() const {
    // 実体を持たない行を引く。塊の見出しはちょうど groups の数だけ挿してあるので、
    // 数え直さなくても分かる。
    return static_cast<int>(visible.size()) - (hasParentRow() ? 1 : 0) -
           static_cast<int>(groups.size());
}

const fs::Entry* Tab::CursorEntry() const { return EntryAt(cursor); }

std::string Tab::CursorPath() const {
    const fs::Entry* e = CursorEntry();
    return e ? fs::EntryPath(path, *e) : std::string();
}

std::vector<std::string> Tab::SelectionPaths() const {
    std::vector<std::string> out;
    for (int index : visible) {
        if (index >= 0 && index < static_cast<int>(marked.size()) && marked[index]) {
            out.push_back(fs::EntryPath(path, listing.entries[index]));
        }
    }
    if (out.empty()) {
        const std::string c = CursorPath();
        if (!c.empty()) out.push_back(c);
    }
    return out;
}

int Tab::MarkedCount() const {
    int n = 0;
    for (int index : visible) {
        if (index >= 0 && index < static_cast<int>(marked.size()) && marked[index]) ++n;
    }
    return n;
}

uint64_t Tab::MarkedBytes() const {
    uint64_t total = 0;
    for (int index : visible) {
        if (index >= 0 && index < static_cast<int>(marked.size()) && marked[index]) {
            total += listing.entries[index].size;
        }
    }
    return total;
}

void Tab::ClearMarks() {
    std::fill(marked.begin(), marked.end(), static_cast<uint8_t>(0));
    // 選択を捨てた以上、伸縮の土台も捨てる。残しておくと次の ExtendTo が
    // 消したはずの印を呼び戻す。
    extending = false;
    markBase.clear();
    markBase.shrink_to_fit();
}

void Tab::MarkRange(int fromVisible, int toVisible, bool value) {
    if (visible.empty()) return;
    int lo = std::clamp(std::min(fromVisible, toVisible), 0, static_cast<int>(visible.size()) - 1);
    int hi = std::clamp(std::max(fromVisible, toVisible), 0, static_cast<int>(visible.size()) - 1);
    for (int i = lo; i <= hi; ++i) {
        if (visible[i] < 0) continue;  // 「..」と塊の見出し ─ 選べる実体が無い
        marked[visible[i]] = value ? 1 : 0;
    }
}

void Tab::ResetAnchor() {
    anchor = cursor;
    extending = false;
    markBase.clear();
    markBase.shrink_to_fit();
}

void Tab::ExtendTo(int toVisible) {
    if (visible.empty()) return;
    if (!extending) {
        markBase = marked;
        extending = true;
    }
    marked = markBase;
    const int want = std::clamp(toVisible, 0, static_cast<int>(visible.size()) - 1);
    // 見出しへ伸ばそうとしたら、進んでいる向きへ 1 つ越える。手前で止めると
    // Shift+↓ が塊の切れ目で効かなくなる ─ 次に押しても同じ見出しを指すので。
    cursor = SkipGroupRows(want, want >= anchor ? 1 : -1);
    MarkRange(anchor, cursor, true);
}

// ---------------------------------------------------------------------------
// Pane
// ---------------------------------------------------------------------------

Tab* Pane::activeTab() {
    if (tabs.empty()) return nullptr;
    active = std::clamp(active, 0, static_cast<int>(tabs.size()) - 1);
    return tabs[active].get();
}

const Tab* Pane::activeTab() const {
    if (tabs.empty()) return nullptr;
    const int index = std::clamp(active, 0, static_cast<int>(tabs.size()) - 1);
    return tabs[index].get();
}

uint64_t NextWatchId() {
    static std::atomic<uint64_t> counter{ 1 };
    return counter.fetch_add(1, std::memory_order_relaxed);
}

Tab* Pane::AddTab(const std::string& p, int at) {
    auto tab = std::make_unique<Tab>();
    tab->path = p;
    tab->watchId = NextWatchId();
    Tab* raw = tab.get();
    if (at < 0 || at > static_cast<int>(tabs.size())) {
        tabs.push_back(std::move(tab));
        active = static_cast<int>(tabs.size()) - 1;
    } else {
        tabs.insert(tabs.begin() + at, std::move(tab));
        active = at;
    }
    return raw;
}

bool Pane::CloseTab(int index, std::string* closedPath) {
    if (index < 0 || index >= static_cast<int>(tabs.size())) return false;
    if (tabs.size() == 1) return false;  // a pane always keeps one tab
    if (closedPath) *closedPath = tabs[index]->path;
    tabs.erase(tabs.begin() + index);
    if (active >= static_cast<int>(tabs.size())) active = static_cast<int>(tabs.size()) - 1;
    return true;
}

void Pane::Activate(int index) {
    if (tabs.empty()) return;
    active = std::clamp(index, 0, static_cast<int>(tabs.size()) - 1);
}

bool Pane::ReorderTab(int fromIndex, int toIndex) {
    return ReorderKeepingActive(tabs, active, fromIndex, toIndex);
}

std::unique_ptr<Tab> Pane::DetachTab(int index) {
    if (index < 0 || index >= static_cast<int>(tabs.size())) return nullptr;
    std::unique_ptr<Tab> moved = std::move(tabs[index]);
    tabs.erase(tabs.begin() + index);
    if (active >= static_cast<int>(tabs.size())) active = static_cast<int>(tabs.size()) - 1;
    if (active < 0) active = 0;
    return moved;
}

void Pane::AttachTab(std::unique_ptr<Tab> tab, int at) {
    if (!tab) return;
    const int count = static_cast<int>(tabs.size());
    at = (at < 0 || at > count) ? count : at;
    tabs.insert(tabs.begin() + at, std::move(tab));
    active = at;
}

// ---------------------------------------------------------------------------
// Session
// ---------------------------------------------------------------------------

namespace {

void CollectPanes(SplitNode* node, std::vector<Pane*>& out) {
    if (!node) return;
    if (node->leaf()) {
        if (node->pane) out.push_back(node->pane.get());
        return;
    }
    CollectPanes(node->a.get(), out);
    CollectPanes(node->b.get(), out);
}

SplitNode* FindLeafFor(SplitNode* node, const Pane* p) {
    if (!node) return nullptr;
    if (node->leaf()) return node->pane.get() == p ? node : nullptr;
    if (SplitNode* found = FindLeafFor(node->a.get(), p)) return found;
    return FindLeafFor(node->b.get(), p);
}

std::unique_ptr<SplitNode> MakeLeaf(const std::string& path) {
    auto node = std::make_unique<SplitNode>();
    node->kind = SplitNode::Kind::Leaf;
    node->pane = std::make_unique<Pane>();
    node->pane->AddTab(path);
    return node;
}

}  // namespace

std::unique_ptr<Session> Session::Create(const std::string& name, const std::string& path) {
    auto s = std::make_unique<Session>();
    s->name = name;
    s->root = MakeLeaf(path);
    s->focus = s->root->pane.get();
    return s;
}

std::vector<Pane*> Session::Panes() const {
    std::vector<Pane*> out;
    CollectPanes(root.get(), out);
    return out;
}

SplitNode* Session::LeafOf(const Pane* p) const { return FindLeafFor(root.get(), p); }

Pane* Session::Split(Pane* target, SplitNode::Kind kind) {
    if (!target || kind == SplitNode::Kind::Leaf) return nullptr;
    SplitNode* leaf = LeafOf(target);
    if (!leaf) return nullptr;

    const Tab* src = target->activeTab();
    const std::string path = src ? src->path : std::string();

    // Re-root the existing pane into the first child and add a sibling.
    auto moved = std::make_unique<SplitNode>();
    moved->kind = SplitNode::Kind::Leaf;
    moved->pane = std::move(leaf->pane);
    moved->parent = leaf;

    auto fresh = MakeLeaf(path);
    fresh->parent = leaf;
    Pane* created = fresh->pane.get();

    leaf->kind = kind;
    leaf->ratio = 0.5f;
    leaf->a = std::move(moved);
    leaf->b = std::move(fresh);
    return created;
}

bool Session::ClosePane(Pane* target) {
    SplitNode* leaf = LeafOf(target);
    if (!leaf || !leaf->parent) return false;  // the only pane

    SplitNode* parent = leaf->parent;
    // Decide this before the reset below destroys `target`; comparing a
    // dangling pointer afterwards would be undefined.
    const bool wasFocused = (focus == target);

    std::unique_ptr<SplitNode> sibling =
        (parent->a.get() == leaf) ? std::move(parent->b) : std::move(parent->a);
    parent->a.reset();
    parent->b.reset();

    // Collapse the parent into the surviving sibling, in place.
    SplitNode* grandparent = parent->parent;
    parent->kind = sibling->kind;
    parent->ratio = sibling->ratio;
    parent->pane = std::move(sibling->pane);
    parent->a = std::move(sibling->a);
    parent->b = std::move(sibling->b);
    parent->parent = grandparent;
    if (parent->a) parent->a->parent = parent;
    if (parent->b) parent->b->parent = parent;

    if (wasFocused) {
        std::vector<Pane*> panes = Panes();
        focus = panes.empty() ? nullptr : panes.front();
    }
    return true;
}

void Session::SwapWithSibling(Pane* target) {
    SplitNode* leaf = LeafOf(target);
    if (!leaf || !leaf->parent) return;
    SplitNode* parent = leaf->parent;
    std::swap(parent->a, parent->b);
}

Pane* Session::PaneInDirection(const Pane* from, int dx, int dy) const {
    SplitNode* leaf = FindLeafFor(root.get(), from);
    if (!leaf) return nullptr;
    const PointF origin = leaf->rect.center();

    Pane* best = nullptr;
    float bestScore = 0.0f;
    for (Pane* p : Panes()) {
        if (p == from) continue;
        SplitNode* other = LeafOf(p);
        if (!other) continue;
        const PointF c = other->rect.center();
        const float ddx = c.x - origin.x;
        const float ddy = c.y - origin.y;
        // Must lie in the requested direction, dominantly so.
        if (dx != 0 && (ddx * dx <= 0 || std::abs(ddy) > std::abs(ddx))) continue;
        if (dy != 0 && (ddy * dy <= 0 || std::abs(ddx) > std::abs(ddy))) continue;
        const float score = std::abs(ddx) + std::abs(ddy);
        if (!best || score < bestScore) {
            best = p;
            bestScore = score;
        }
    }
    return best;
}

namespace {

void SerializeNode(const SplitNode* node, std::string& out) {
    if (!node) return;
    if (node->leaf()) {
        out += "L{";
        const Pane* p = node->pane.get();
        for (size_t i = 0; i < p->tabs.size(); ++i) {
            if (i) out += '|';
            out += path::EscapeToken(p->tabs[i]->path);
        }
        out += "}@";
        out += std::to_string(p->active);
        return;
    }
    out += (node->kind == SplitNode::Kind::LeftRight) ? 'H' : 'V';
    char buf[24];
    std::snprintf(buf, sizeof(buf), "(%.3f,", static_cast<double>(node->ratio));
    out += buf;
    SerializeNode(node->a.get(), out);
    out += ',';
    SerializeNode(node->b.get(), out);
    out += ')';
}

// Recursive-descent parser matching SerializeNode.
std::unique_ptr<SplitNode> ParseNode(const std::string& s, size_t& i) {
    if (i >= s.size()) return nullptr;

    if (s[i] == 'L') {
        ++i;
        if (i >= s.size() || s[i] != '{') return nullptr;
        ++i;
        auto node = std::make_unique<SplitNode>();
        node->kind = SplitNode::Kind::Leaf;
        node->pane = std::make_unique<Pane>();

        std::string token;
        while (i < s.size() && s[i] != '}') {
            if (s[i] == '|') {
                node->pane->AddTab(path::UnescapeToken(token));
                token.clear();
            } else {
                token.push_back(s[i]);
            }
            ++i;
        }
        if (!token.empty()) node->pane->AddTab(path::UnescapeToken(token));
        if (i < s.size()) ++i;  // '}'

        int activeIndex = 0;
        if (i < s.size() && s[i] == '@') {
            ++i;
            size_t start = i;
            while (i < s.size() && s[i] >= '0' && s[i] <= '9') ++i;
            activeIndex = std::atoi(s.substr(start, i - start).c_str());
        }
        if (node->pane->tabs.empty()) return nullptr;
        node->pane->Activate(activeIndex);
        return node;
    }

    if (s[i] == 'H' || s[i] == 'V') {
        auto node = std::make_unique<SplitNode>();
        node->kind = (s[i] == 'H') ? SplitNode::Kind::LeftRight : SplitNode::Kind::TopBottom;
        ++i;
        if (i >= s.size() || s[i] != '(') return nullptr;
        ++i;
        size_t start = i;
        while (i < s.size() && s[i] != ',') ++i;
        node->ratio = static_cast<float>(std::atof(s.substr(start, i - start).c_str()));
        if (node->ratio < 0.05f || node->ratio > 0.95f) node->ratio = 0.5f;
        if (i < s.size()) ++i;  // ','

        node->a = ParseNode(s, i);
        if (i < s.size() && s[i] == ',') ++i;
        node->b = ParseNode(s, i);
        if (i < s.size() && s[i] == ')') ++i;

        if (!node->a || !node->b) return nullptr;
        node->a->parent = node.get();
        node->b->parent = node.get();
        return node;
    }
    return nullptr;
}

}  // namespace

std::string Session::Serialize() const {
    std::string out;
    SerializeNode(root.get(), out);
    return out;
}

std::unique_ptr<Session> Session::Deserialize(const std::string& name, const std::string& text) {
    size_t i = 0;
    std::unique_ptr<SplitNode> root = ParseNode(text, i);
    if (!root) return nullptr;
    auto s = std::make_unique<Session>();
    s->name = name;
    s->root = std::move(root);
    std::vector<Pane*> panes = s->Panes();
    if (panes.empty()) return nullptr;
    s->focus = panes.front();
    return s;
}

// ---------------------------------------------------------------------------
// Workspace
// ---------------------------------------------------------------------------

Session* Workspace::activeSession() {
    if (sessions.empty()) return nullptr;
    active = std::clamp(active, 0, static_cast<int>(sessions.size()) - 1);
    return sessions[active].get();
}

const Session* Workspace::activeSession() const {
    if (sessions.empty()) return nullptr;
    const int index = std::clamp(active, 0, static_cast<int>(sessions.size()) - 1);
    return sessions[index].get();
}

Pane* Workspace::focusedPane() {
    Session* s = activeSession();
    if (!s) return nullptr;
    if (!s->focus) {
        std::vector<Pane*> panes = s->Panes();
        s->focus = panes.empty() ? nullptr : panes.front();
    }
    return s->focus;
}

Tab* Workspace::focusedTab() {
    Pane* p = focusedPane();
    return p ? p->activeTab() : nullptr;
}

Session* Workspace::AddSession(const std::string& name, const std::string& path) {
    sessions.push_back(Session::Create(name, path));
    active = static_cast<int>(sessions.size()) - 1;
    return sessions.back().get();
}

void Workspace::CloseSession(int index) {
    if (sessions.size() <= 1) return;
    if (index < 0 || index >= static_cast<int>(sessions.size())) return;
    sessions.erase(sessions.begin() + index);
    if (active >= static_cast<int>(sessions.size())) active = static_cast<int>(sessions.size()) - 1;
}

void Workspace::ActivateSession(int index) {
    if (sessions.empty()) return;
    const int next = std::clamp(index, 0, static_cast<int>(sessions.size()) - 1);
    if (next == active) return;

    // Release listings held by the session we are leaving; they are cheap to
    // rebuild and this keeps resident memory proportional to what is on screen.
    if (Session* prev = activeSession()) {
        for (Pane* p : prev->Panes()) {
            for (std::unique_ptr<Tab>& t : p->tabs) {
                if (t.get() != p->activeTab()) t->DropListing();
            }
        }
    }
    active = next;
}

// Reordering is not a way of switching, and deliberately not ActivateSession():
// nothing is being left, so the listings the other sessions hold have no reason
// to be dropped.
bool Workspace::ReorderSession(int fromIndex, int toIndex) {
    return ReorderKeepingActive(sessions, active, fromIndex, toIndex);
}

}  // namespace kite
