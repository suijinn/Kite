// Kite - layout, painting and hit-testing for the whole window.
//
// Painting also records the interactive regions it drew, so mouse handling is
// a reverse lookup over that list. No retained widget tree, no invalidation
// bookkeeping: at these sizes a full repaint is well under a millisecond.
//
// Drag handling lives here too, including the parts a platform drag backend
// needs (which folder is under the pointer, where to draw the drop feedback).
// Only the OS drag transport itself is platform code.
#pragma once

#include <string>
#include <vector>

#include "core/app/App.h"
#include "ui/Renderer.h"

namespace kite::ui {

enum MouseButtonMask : uint8_t {
    kButtonNone = 0,
    kButtonLeft = 1 << 0,
    kButtonRight = 1 << 1,
    kButtonMiddle = 1 << 2,
};

struct MouseEvent {
    enum class Type : uint8_t { Move, Down, Up, Wheel, Leave };

    Type type = Type::Move;
    float x = 0.0f;
    float y = 0.0f;
    int screenX = 0;
    int screenY = 0;
    int button = 0;            // 0 left, 1 right, 2 middle, 3 back, 4 forward
    uint8_t buttons = 0;       // which buttons are currently held
    int clicks = 1;
    uint8_t mods = 0;
    float wheel = 0.0f;
};

class AppUi {
public:
    explicit AppUi(App& app);

    void Paint(Renderer& r);
    bool OnMouse(const MouseEvent& e);

    // Where the IME candidate window should sit, in client DIPs.
    PointF caretPosition() const { return caret_; }
    int desiredCursorShape() const { return cursorShape_; }

    // --- external drag & drop -------------------------------------------------
    // Folder that a drop at this client point would go into; empty when the
    // point is not over a valid destination.
    std::string DropTargetAt(float x, float y) const;

    // Highlights that destination. Called by the platform drop target while a
    // drag hovers, then cleared on leave or drop.
    void SetDropFeedback(float x, float y);
    void ClearDropFeedback();

private:
    enum class Hit : uint8_t {
        None,
        SessionChip,
        SessionAdd,
        SidebarItem,
        TabBar,
        TabItem,
        TabClose,
        TabAdd,
        Crumb,
        ColumnHeader,
        ListRow,
        ListBackground,
        Splitter,
    };

    // What the left button is currently doing.
    enum class Drag : uint8_t {
        None,
        Splitter,
        PendingTab,   // pressed on a tab, not yet moved far enough
        Tab,          // reordering / relocating a tab
        PendingFile,  // pressed on a row, may become an OS file drag
    };

    struct Region {
        RectF rect;
        Hit kind = Hit::None;
        Pane* pane = nullptr;
        SplitNode* node = nullptr;
        int index = 0;
        std::string path;
    };

    void Add(const RectF& r, Hit kind, int index = 0, Pane* pane = nullptr,
             SplitNode* node = nullptr, std::string path = {});
    const Region* Pick(float x, float y) const;

    void PaintSessionBar(Renderer& r, const RectF& area);
    void PaintSidebar(Renderer& r, const RectF& area);
    void PaintStatusBar(Renderer& r, const RectF& area);
    void PaintPrompt(Renderer& r, const RectF& area);
    void PaintKeyHelp(Renderer& r, const RectF& area);
    void PaintNode(Renderer& r, SplitNode* node, const RectF& area);
    void PaintPane(Renderer& r, Pane* pane, const RectF& area);
    void PaintTabBar(Renderer& r, Pane* pane, const RectF& area);
    void PaintPathBar(Renderer& r, Pane* pane, Tab* tab, const RectF& area);
    void PaintList(Renderer& r, Pane* pane, Tab* tab, const RectF& area, bool focused);
    void PaintDragOverlay(Renderer& r);

    bool HandleListClick(const Region& region, const MouseEvent& e);
    void ScrollPane(Pane* pane, float deltaPixels);

    // Tab drag helpers.
    bool ResolveTabDrop(float x, float y, Pane** outPane, int* outIndex) const;
    void FinishTabDrag();
    void CancelDrag();

    App& app_;

    std::vector<Region> regions_;
    PointF caret_{ 0.0f, 0.0f };
    int cursorShape_ = 0;

    Drag drag_ = Drag::None;
    float dragStartX_ = 0.0f;
    float dragStartY_ = 0.0f;

    // Splitter drag.
    SplitNode* dragSplitter_ = nullptr;
    float dragOrigin_ = 0.0f;
    float dragRatio_ = 0.5f;

    // Tab drag.
    Pane* dragTabPane_ = nullptr;
    int dragTabIndex_ = -1;
    Pane* dropTabPane_ = nullptr;
    int dropTabIndex_ = -1;
    RectF dropTabMarker_{};

    // External file drop feedback.
    bool dropActive_ = false;
    RectF dropHighlight_{};
    std::string dropPath_;

    float sidebarScroll_ = 0.0f;
    RectF sidebarRect_{};
};

}  // namespace kite::ui
