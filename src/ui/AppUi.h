/// @file
/// @brief ウィンドウ全体のレイアウト・描画・ヒットテスト。
///
/// 描画のたびに「そこに何を描いたか」も記録するので、マウス処理はその一覧を逆順に
/// 引くだけで済む。保持型のウィジェットツリーも無効領域の管理も持たない。この規模
/// なら全面再描画で 1 ミリ秒を大きく下回る。
///
/// プラットフォームのドラッグ実装が必要とする情報（ポインタの下にあるフォルダ、
/// ドロップ先の強調表示位置）もここが提供する。OS 依存なのはドラッグの転送のみ。

#pragma once

#include <string>
#include <vector>

#include "core/app/App.h"
#include "ui/Renderer.h"

namespace kite::ui {

/// @brief 現在押されているマウスボタンのビットマスク。
enum MouseButtonMask : uint8_t {
    kButtonNone = 0,        ///< どのボタンも押されていない
    kButtonLeft = 1 << 0,   ///< 左ボタン
    kButtonRight = 1 << 1,  ///< 右ボタン
    kButtonMiddle = 1 << 2, ///< 中ボタン
};

/// @brief マウスイベント。座標はクライアント DIP。
struct MouseEvent {
    /// @brief イベントの種類。
    enum class Type : uint8_t {
        Move,   ///< 移動
        Down,   ///< ボタン押下
        Up,     ///< ボタン解放
        Wheel,  ///< ホイール回転
        Leave,  ///< ウィンドウ外へ出た
    };

    Type type = Type::Move;  ///< イベントの種類
    float x = 0.0f;          ///< クライアント座標の X（DIP）
    float y = 0.0f;          ///< クライアント座標の Y（DIP）
    int screenX = 0;         ///< スクリーン座標の X（ピクセル）
    int screenY = 0;         ///< スクリーン座標の Y（ピクセル）
    int button = 0;          ///< 0=左 1=右 2=中 3=戻る 4=進む
    uint8_t buttons = 0;     ///< 押下中ボタンの MouseButtonMask 和
    int clicks = 1;          ///< クリック数。2 ならダブルクリック
    uint8_t mods = 0;        ///< 修飾キーの Mod ビット和
    float wheel = 0.0f;      ///< ホイールの回転量。1.0 で 1 ノッチ
};

/// @brief 画面の描画とマウス操作を担当する。
class AppUi {
public:
    /// @brief コントローラを結び付けて構築する。
    /// @param[in] app 描画対象のアプリケーション。AppUi より長生きすること
    explicit AppUi(App& app);

    /// @brief 画面全体を描画する。
    /// @param[in,out] r 描画先
    /// @note 同時にヒットテスト用の領域一覧を作り直す
    void Paint(Renderer& r);

    /// @brief マウスイベントを処理する。
    /// @param[in] e 処理するイベント
    /// @return 消費したら true
    bool OnMouse(const MouseEvent& e);

    /// @brief IME の変換候補を出すべき位置を返す。
    /// @return クライアント座標（DIP）
    PointF caretPosition() const { return caret_; }

    /// @brief 望ましいマウスカーソル形状を返す。
    /// @return IHost::SetCursorShape() に渡す形状番号
    int desiredCursorShape() const { return cursorShape_; }

    /// @brief 指定位置にドロップした場合の転送先フォルダを返す。
    /// @param[in] x クライアント座標の X（DIP）
    /// @param[in] y クライアント座標の Y（DIP）
    /// @return 転送先フォルダのパス。妥当な場所でなければ空文字列
    std::string DropTargetAt(float x, float y) const;

    /// @brief ドロップ先の強調表示を設定する。
    /// @param[in] x クライアント座標の X（DIP）
    /// @param[in] y クライアント座標の Y（DIP）
    /// @note ドラッグが上を通過している間、プラットフォームのドロップターゲットが呼ぶ
    void SetDropFeedback(float x, float y);

    /// @brief ドロップ先の強調表示を消す。
    void ClearDropFeedback();

private:
    /// 描画時に記録する当たり判定の種別。
    enum class Hit : uint8_t {
        None,
        SessionChip,
        SessionAdd,
        SidebarSectionHeader,
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
        AddressBar,
        PromptField,  ///< 対象の上で開いている入力欄。外を押すと畳まれるので要る
        CompletionRow,
        KeyPanel,
        KeyRow,
        KeyChord,
        KeyAdd,
        SettingsPanel,
        SettingsRow,
        SettingsPrev,
        SettingsNext,
        BookmarkPanel,
        BookmarkRow,
    };

    /// 左ボタンが今おこなっている操作。
    enum class Drag : uint8_t {
        None,
        Splitter,
        PendingTab,   // pressed on a tab, not yet moved far enough
        Tab,          // reordering / relocating a tab
        PendingFile,  // pressed on a row, may become an OS file drag
        Marquee,      // pressed on empty list space, sweeping a selection band
        PendingSidebar,  // pressed on a sidebar item, not yet moved far enough
        Sidebar,         // reordering within one sidebar section
        PendingSection,  // pressed on a sidebar heading, not yet moved far enough
        Section,         // reordering the sidebar sections themselves
        PendingSession,  // pressed on a session chip, not yet moved far enough
        Session,         // reordering the session chips
    };

    struct Region {
        RectF rect;
        Hit kind = Hit::None;
        Pane* pane = nullptr;
        SplitNode* node = nullptr;
        int index = 0;
        SidebarSection section = SidebarSection::Count;  ///< サイドバーの行のみ
        std::string path;
    };

    void Add(const RectF& r, Hit kind, int index = 0, Pane* pane = nullptr,
             SplitNode* node = nullptr, std::string path = {});
    void AddSidebar(const RectF& r, Hit kind, SidebarSection section, int index,
                    std::string path = {});
    const Region* Pick(float x, float y) const;

    bool PointerOver(const RectF& box) const;
    bool Hovered(const RectF& box) const;
    static bool IsTabBarHit(Hit kind);

    /// セッションバーに並べた 1 個ぶん。折り返した結果の行番号を持つ。
    struct Chip {
        RectF box;      ///< 画面上の位置。折り返しとスクロールを済ませた後の値
        int index = 0;  ///< セッションの添字
    };

    /// タブバーを折り返した結果。横置き・縦置きのどちらも同じ形で表す。
    ///
    /// 「行」は流れの折り返し単位で、横置きなら画面上の 1 行、縦置きなら 1 枚ぶんの
    /// 段。縦置きは列を増やさない（増やせば一覧の幅が消える）ので perRow は常に 1 で、
    /// 1 枚が 1 行になる。
    struct TabLayout {
        bool vertical = false;   ///< 縦置き（ペインの左）か
        float perTab = 0.0f;     ///< 流れ方向の 1 枚ぶん。横なら幅、縦なら高さ
        float thickness = 0.0f;  ///< バーの厚み。横ならバー全体の高さ、縦なら幅
        int perRow = 1;          ///< 1 行に並ぶ枚数。縦置きでは常に 1
        int rows = 1;            ///< 折り返して必要になった行数
        int firstRow = 0;        ///< 画面に出る先頭の行
        int shownRows = 1;       ///< 実際に描く行数。rows を超えない
    };

    bool SessionChipEditing(int index) const;
    float LayoutSessionBar(Renderer& r, const RectF& area);
    void PaintSessionBar(Renderer& r, const RectF& area);
    TabLayout LayoutTabBar(Pane& pane, const RectF& area) const;
    void PaintSidebar(Renderer& r, const RectF& area);
    void PaintStatusBar(Renderer& r, const RectF& area);
    void PaintPromptField(Renderer& r, const RectF& field, FontRole role = FontRole::Ui);
    void PaintInlineField(Renderer& r, const RectF& box, FontRole role = FontRole::Ui,
                          float indent = 0.0f);
    void PaintPrompt(Renderer& r, const RectF& area);
    void LayoutCompletion(Renderer& r, const RectF& promptArea);
    void PaintCompletion(Renderer& r);
    void PaintKeyHelp(Renderer& r, const RectF& area);
    void PaintKeySettings(Renderer& r, const RectF& area);
    bool HandleKeySettingsClick(const MouseEvent& e);
    void PaintSettings(Renderer& r, const RectF& area);
    bool HandleSettingsClick(const MouseEvent& e);
    void PaintBookmarks(Renderer& r, const RectF& area);
    bool HandleBookmarkClick(const MouseEvent& e);
    void PaintNode(Renderer& r, SplitNode* node, const RectF& area);
    void PaintPane(Renderer& r, Pane* pane, const RectF& area);
    void PaintTabBar(Renderer& r, Pane* pane, const RectF& area, bool focused,
                     const TabLayout& layout);
    Color FocusColor(bool focused) const;
    void PaintPathBar(Renderer& r, Pane* pane, Tab* tab, const RectF& area, bool focused);
    void PaintList(Renderer& r, Pane* pane, Tab* tab, const RectF& area, bool focused);
    void PaintDragOverlay(Renderer& r);

    bool HandleListClick(const Region& region, const MouseEvent& e);
    void BeginMarquee(Pane* pane, const MouseEvent& e);
    void UpdateMarquee(float x, float y);
    void ScrollPane(Pane* pane, float deltaPixels);

    bool ResolveTabDrop(float x, float y, Pane** outPane, int* outIndex) const;
    void FinishTabDrag();
    bool ResolveSessionDrop(float x, float y, int* outIndex, RectF* outMarker) const;
    void FinishSessionDrag();
    bool ResolveSidebarDrop(float x, float y, int* outIndex, RectF* outMarker) const;
    void FinishSidebarDrag();
    RectF SectionBlock(SidebarSection section) const;
    bool ResolveSectionDrop(float x, float y, int* outIndex, RectF* outMarker) const;
    void FinishSectionDrag();
    void CancelDrag();

    App& app_;

    std::vector<Region> regions_;
    PointF caret_{ 0.0f, 0.0f };
    int cursorShape_ = 0;

    // Where the pointer is, as of the last event. Painting asks this directly
    // instead of remembering which row was hit: the row under a fixed pointer
    // changes when the list scrolls, and no mouse event says so.
    float mouseX_ = 0.0f;
    float mouseY_ = 0.0f;
    bool mouseInside_ = false;

    // Only for deciding when a move is worth a repaint.
    Hit hoverKind_ = Hit::None;
    RectF hoverRect_{};

    Drag drag_ = Drag::None;
    float dragStartX_ = 0.0f;
    float dragStartY_ = 0.0f;

    SplitNode* dragSplitter_ = nullptr;
    float dragOrigin_ = 0.0f;
    float dragRatio_ = 0.5f;

    // Where the band started, as a place in the listing (pixels from the top of
    // row 0) rather than a place on the screen. Same reason the hover keeps no
    // row index: what a point on the glass means changes when the list moves.
    Pane* marqueePane_ = nullptr;
    Tab* marqueeTab_ = nullptr;
    float marqueeAnchorX_ = 0.0f;
    float marqueeAnchorY_ = 0.0f;
    float marqueeX_ = 0.0f;
    float marqueeY_ = 0.0f;
    std::vector<uint8_t> marqueeBase_;  // marks as they were when the sweep began

    // The sidebar row under the button, and where letting go would put it.
    // Opening it waits for the release: navigating on the press would mean
    // every reorder also left the folder the user was reordering from.
    SidebarSection dragSidebarSection_ = SidebarSection::Count;
    int dragSidebarIndex_ = -1;
    int dropSidebarIndex_ = -1;
    RectF dropSidebarMarker_{};
    std::string pendingSidebarPath_;
    bool pendingSidebarNewTab_ = false;

    // A heading being carried moves its whole block - the heading and every row
    // under it. Folding it waits for the release, for the same reason opening a
    // folder does: the press cannot know yet which of the two it is.
    SidebarSection dragSection_ = SidebarSection::Count;
    int dragSectionIndex_ = -1;
    int dropSectionIndex_ = -1;
    RectF dropSectionMarker_{};

    // The chip being carried along the session bar, and the slot letting go
    // would put it in. The press activates that session (as a plain click always
    // has), so the bar keeps scrolling to the chip under the pointer.
    int dragSessionIndex_ = -1;
    int dropSessionIndex_ = -1;
    RectF dropSessionMarker_{};

    Pane* dragTabPane_ = nullptr;
    int dragTabIndex_ = -1;
    Pane* dropTabPane_ = nullptr;
    int dropTabIndex_ = -1;
    RectF dropTabMarker_{};

    bool dropActive_ = false;
    RectF dropHighlight_{};
    std::string dropPath_;

    // The completion popup, measured when the bar it drops out of is painted
    // and drawn after every pane - it hangs over the list, and the layout
    // underneath must not shift as candidates come and go. Nothing behind it
    // lights up, so the rectangle has to be known before those rows are drawn.
    RectF completionRect_{};
    int completionTop_ = 0;   // first candidate on screen
    int completionRows_ = 0;  // how many fit

    // The session chips, laid out before the bar is painted: the bar's height is
    // however many rows they wrapped into, and everything below it has to be
    // placed after that is known. Positions are absolute, so the layout pass and
    // the paint pass cannot disagree about where a chip is.
    std::vector<Chip> sessionChips_;
    RectF sessionAdd_{};
    RectF sessionBrand_{};

    /// ショートカット一覧（F1）で送った行数。窓が小さくて全部が入らないときだけ
    /// 意味を持つ。閉じている間は 0 に戻すので、開き直せば必ず先頭から。
    int keyHelpScroll_ = 0;

    float sidebarScroll_ = 0.0f;
    RectF sidebarRect_{};
    // Height of everything the last frame laid out, so folding a section away
    // cannot leave the sidebar scrolled past what is left of it.
    float sidebarContent_ = 0.0f;
};

}  // namespace kite::ui
