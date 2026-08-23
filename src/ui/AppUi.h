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
#include <string_view>
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
    /// @return キャレットの矩形（クライアント座標・DIP）。幅は持たず、上端と下端は
    ///         キャレットが立っている行の高さ
    /// @note 高さまで返すのは、候補ウィンドウに «この矩形を避けろ» と言うため ─
    ///       変換している当の文字の上に候補一覧が乗ると、選んでいる相手が見えない。
    ///       入力欄が出ていないときは、フォーカスされた一覧のカーソル行を指す
    ///       （型入力ジャンプの変換もどこかに出るので、隅に取り残さない）
    RectF caretRect() const { return caret_; }

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
    // 描画・当たり判定・マウス処理の 3 ファイルが共有する寸法。ファイル内の
    // 定数にすると分割した先ごとに 1 つずつ生まれ、片方だけが直る日が来る。
    static constexpr float kPad = 8.0f;
    static constexpr float kScrollbarWidth = 10.0f;

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
        PlacePanel,
        PlaceRow,
        PalettePanel,
        PaletteRow,
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
    bool OutsideWindow(float x, float y) const;
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
    void PaintTextField(Renderer& r, const RectF& box, const TextField& f, FontRole role,
                        float caretInset, std::string_view placeholder = {},
                        size_t placeholderUntil = 0);
    void PaintPromptField(Renderer& r, const RectF& field, FontRole role = FontRole::Ui);
    /// 入力欄の中で、変換中の文字列が占めている横位置。
    ///
    /// 測るのは «画面に出ている 1 本の文字列» の接頭辞で、断片を別々に測って足さない
    /// （詰めが入った瞬間に全体の幅と合わなくなる）。中身が空なら active() が false。
    struct CompositionRun {
        float from = 0.0f;        ///< 変換中の文字列の左端
        float to = 0.0f;          ///< 同じく右端
        float targetFrom = 0.0f;  ///< 注目節の左端
        float targetTo = 0.0f;    ///< 同じく右端

        /// @brief 変換中かを判定する。
        /// @return 幅を持っていれば true
        bool active() const { return to > from; }

        /// @brief 注目節があるかを判定する。
        /// @return 幅を持っていれば true
        bool hasTarget() const { return targetTo > targetFrom; }
    };

    void PaintCompositionBack(Renderer& r, const RectF& field, const CompositionRun& run,
                              FontRole role);
    void PaintCompositionMarks(Renderer& r, const RectF& field, const CompositionRun& run,
                               FontRole role);
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
    /// @brief 絞り込み付きチューザ 1 枚分の «器» の中身。
    ///
    /// 行き先の一覧（`Ctrl+P`）とコマンドパレット（`Ctrl+Shift+P`）は行が違うだけの
    /// 同じ画面なので、パネル・表題・件数・入力欄はここに 1 組だけ置いて両方が使う。
    struct PickerChrome {
        std::string title;        ///< 表題（パネル左上）
        std::string count;        ///< 件数（表題の右。入る幅が無ければ出さない）
        /// 絞り込みの入力欄。キャレットと選択もここから引く（末尾にあるとは限らない）
        const TextField* field = nullptr;
        /// 入力欄の先頭にある «モードの印»（コマンドパレットの `>`）の長さ。
        /// これだけしか入っていない状態は «まだ何も打っていない» なので、案内を出す
        size_t prefixLen = 0;
        std::string placeholder;  ///< 絞り込みが空のときに入力欄へ出す案内
        std::string hint;         ///< パネル下端の案内
    };

    /// @brief 器を描いた結果、行を描く側が要る寸法。
    struct PickerFrame {
        RectF panel;       ///< パネル全体
        RectF body;        ///< 行を描く領域。上端 1 px は区切り線が塗られている
        int pageRows = 1;  ///< body に収まる行数。PageUp / PageDown の移動量になる
    };

    /// @brief 絞り込み付きチューザの器を描く。
    /// @param[in,out] r 描画先
    /// @param[in] area ウィンドウ全体の矩形
    /// @param[in] chrome 表題・件数・絞り込みなど、画面ごとに違う中身
    /// @param[in] panelHit パネルに登録する当たり判定の種別
    /// @return 行を描く領域と 1 画面の行数
    /// @note **寸法は area だけで決まる。** 件数では決まらないので、どのチューザも
    ///       同じ大きさ・同じ位置に出る ─ パレットからブックマーク一覧を選んでも、
    ///       打ち込んでいた入力欄が動かない
    /// @note 1 画面の行数は呼び出し側が自分の PickerList へ渡すこと。渡さないと
    ///       PageDown の移動量と選択の引き戻しが窓の高さに追随しない
    PickerFrame PaintPickerFrame(Renderer& r, const RectF& area, const PickerChrome& chrome,
                                Hit panelHit);

    /// @brief 行の脇に細いつまみを描く。全行が収まっていれば何もしない。
    /// @param[in,out] r 描画先
    /// @param[in] track つまみを走らせる帯
    /// @param[in] rows 行数
    /// @param[in] pageRows 1 画面に収まる行数
    /// @param[in] first 先頭に出ている行番号
    /// @note 縦置きのタブバー・`F1` の一覧・チューザ・キー設定の 4 か所が共有する。
    ///       一覧の脇の太いものとは別物で、こちらはタブや行と帯を分け合う
    /// @todo 掴めない（一覧のスクロールバーと同じ扱い。ROADMAP P3-11）
    void PaintThinScrollbar(Renderer& r, const RectF& track, int rows, int pageRows, int first);

    /// @brief チューザの行の脇に細いつまみを描く。
    /// @param[in,out] r 描画先
    /// @param[in] body 行を描いている領域
    /// @param[in] rows 絞り込み後の行数
    /// @param[in] pageRows 1 画面に収まる行数
    /// @param[in] first 先頭に出ている行番号
    /// @note 帯の位置を決めるだけで、描くのは PaintThinScrollbar
    void PaintPickerScrollbar(Renderer& r, const RectF& body, int rows, int pageRows, int first);

    void PaintPlaces(Renderer& r, const RectF& area);
    bool HandlePlaceClick(const MouseEvent& e);
    void PaintCommandPalette(Renderer& r, const RectF& area);
    bool HandlePaletteClick(const MouseEvent& e);
    void PaintNode(Renderer& r, SplitNode* node, const RectF& area);
    void PaintPane(Renderer& r, Pane* pane, const RectF& area);
    void PaintTabBar(Renderer& r, Pane* pane, const RectF& area, bool focused,
                     const TabLayout& layout);
    Color FocusColor(bool focused) const;
    void PaintPathBar(Renderer& r, Pane* pane, Tab* tab, const RectF& area, bool focused);
    void PaintList(Renderer& r, Pane* pane, Tab* tab, const RectF& area, bool focused);
    void PaintDragOverlay(Renderer& r);

    /// @brief 当たり判定 1 つに対するドロップ先を答える。
    /// @param[in] region 対象。nullptr なら行き先なし
    /// @return 落とせるフォルダのパス。落とせないなら空文字列
    /// @note `DropTargetAt` の中身。座標ではなく領域を受けるのは、ドラッグ中の
    ///       フィードバックが同じ 1 点について領域とパスの両方を要るため ─
    ///       `Pick` は行数に比例するので 1 回で済ませる
    std::string DropTargetIn(const Region* region) const;

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
    // Where the IME should put its windows. Set while painting, because that is
    // when the caret's own position is worked out; a field wins over the list's
    // cursor row, and the row is only the answer when no field is on screen.
    RectF caret_{};
    bool caretInField_ = false;
    RectF listCaret_{};
    bool listCaretValid_ = false;
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

    // How big the surface was last frame. Only a drag that left the window needs
    // it, and a drag cannot start before a frame has been painted.
    SizeF surface_{};

    Drag drag_ = Drag::None;
    float dragStartX_ = 0.0f;
    float dragStartY_ = 0.0f;

    // A press on a row that was already marked keeps the marks - the press may
    // be the start of a drag, and a drag carries the selection - so dropping
    // them waits for the release that turns out to be a plain click.
    bool pendingUnmark_ = false;

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
    // Carried past the edge of the window: letting go out there asks for a
    // window of its own. Measured against the surface, not the hit list - "no
    // region here" also happens over the bars, and those are not outside.
    bool dropTabOutside_ = false;

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
