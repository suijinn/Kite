// Kite - the document model: tabs, panes, the split tree, sessions.
//
//   Workspace  owns N Sessions, one active
//   Session    owns a SplitNode tree; every leaf holds one Pane
//   Pane       owns N Tabs, one active
//   Tab        owns a path, its listing and its view state
//
// Switching sessions is just moving an index: every session stays resident, so
// the swap is instant. Listings for non-active sessions are dropped to keep
// memory flat and re-requested lazily when that session comes back.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/base/Types.h"
#include "core/fs/FileSystem.h"

namespace kite {

enum class SortKey : uint8_t { Name, Ext, Size, Date };

struct ViewState {
    SortKey sort = SortKey::Name;
    bool sortDesc = false;
    bool dirsFirst = true;
    bool showHidden = false;
};

class Tab {
public:
    std::string path;
    ViewState view;

    fs::ListResult listing;
    std::vector<int> visible;      // indices into listing.entries, post filter+sort
    std::vector<uint8_t> marked;   // parallel to listing.entries

    std::string filter;
    int cursor = 0;                // index into `visible`
    int anchor = 0;                // range-selection anchor, also into `visible`
    float scroll = 0.0f;           // pixels

    std::vector<std::string> back;
    std::vector<std::string> forward;

    uint64_t loadToken = 0;        // 0 = nothing in flight
    bool loaded = false;

    // Stable for the lifetime of the tab; identifies its filesystem watch.
    uint64_t watchId = 0;

    // Entry to put the cursor on once the next listing arrives. Set when
    // leaving a folder so going up lands on the folder you came out of.
    std::string pendingFocusName;

    std::string title() const;

    // Recomputes `visible` from listing + filter + sort, keeping the cursor on
    // the same entry by name where possible.
    void Rebuild();

    void DropListing();  // frees memory for a backgrounded session

    const fs::Entry* CursorEntry() const;
    std::string CursorPath() const;

    // Full paths of every marked entry; falls back to the cursor entry when
    // nothing is marked, which is what users expect from a filer.
    std::vector<std::string> SelectionPaths() const;
    int MarkedCount() const;
    uint64_t MarkedBytes() const;

    void ClearMarks();
    void MarkRange(int fromVisible, int toVisible, bool value);
};

class Pane {
public:
    std::vector<std::unique_ptr<Tab>> tabs;
    int active = 0;
    float tabScroll = 0.0f;

    // Written by the UI layer on every layout pass so the controller can page
    // and keep the cursor visible without knowing anything about pixels.
    float listHeight = 0.0f;
    float rowHeight = 22.0f;
    int rowsPerPage = 20;

    Tab* activeTab();
    const Tab* activeTab() const;
    Tab* AddTab(const std::string& path, int at = -1);
    bool CloseTab(int index, std::string* closedPath);
    void Activate(int index);

    // --- drag & drop reordering ---------------------------------------------
    // Within one pane.
    bool ReorderTab(int fromIndex, int toIndex);

    // Across panes. DetachTab may leave the pane empty; the caller is
    // responsible for closing a pane it emptied.
    std::unique_ptr<Tab> DetachTab(int index);
    void AttachTab(std::unique_ptr<Tab> tab, int at);
    bool empty() const { return tabs.empty(); }
};

// Monotonic; every Tab gets one so watches survive reordering.
uint64_t NextWatchId();

struct SplitNode {
    enum class Kind : uint8_t { Leaf, LeftRight, TopBottom };

    Kind kind = Kind::Leaf;
    float ratio = 0.5f;
    std::unique_ptr<Pane> pane;      // Kind::Leaf only
    std::unique_ptr<SplitNode> a;
    std::unique_ptr<SplitNode> b;
    SplitNode* parent = nullptr;

    RectF rect;         // last laid-out bounds, used for directional focus
    RectF splitterRect; // interactive divider, empty for leaves

    bool leaf() const { return kind == Kind::Leaf; }
};

class Session {
public:
    std::string name;
    std::unique_ptr<SplitNode> root;
    Pane* focus = nullptr;

    static std::unique_ptr<Session> Create(const std::string& name, const std::string& path);

    std::vector<Pane*> Panes() const;
    SplitNode* LeafOf(const Pane* p) const;

    // Splits the focused leaf; the new pane opens on the same folder.
    Pane* Split(Pane* target, SplitNode::Kind kind);
    bool ClosePane(Pane* target);
    void SwapWithSibling(Pane* target);

    // Neighbouring pane in a screen direction, based on the last layout.
    Pane* PaneInDirection(const Pane* from, int dx, int dy) const;

    std::string Serialize() const;
    static std::unique_ptr<Session> Deserialize(const std::string& name, const std::string& text);
};

struct Bookmark {
    std::string name;
    std::string path;
};

class Workspace {
public:
    std::vector<std::unique_ptr<Session>> sessions;
    int active = 0;
    std::vector<Bookmark> bookmarks;
    std::vector<std::string> closedTabs;  // reopen stack

    Session* activeSession();
    const Session* activeSession() const;
    Pane* focusedPane();
    Tab* focusedTab();

    Session* AddSession(const std::string& name, const std::string& path);
    void CloseSession(int index);
    void ActivateSession(int index);
};

}  // namespace kite
