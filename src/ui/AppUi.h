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
        KeyPanel,
        KeyRow,
    };

    /// 左ボタンが今おこなっている操作。
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
    void PaintKeySettings(Renderer& r, const RectF& area);
    bool HandleKeySettingsClick(const MouseEvent& e);
    void PaintNode(Renderer& r, SplitNode* node, const RectF& area);
    void PaintPane(Renderer& r, Pane* pane, const RectF& area);
    void PaintTabBar(Renderer& r, Pane* pane, const RectF& area);
    void PaintPathBar(Renderer& r, Pane* pane, Tab* tab, const RectF& area);
    void PaintList(Renderer& r, Pane* pane, Tab* tab, const RectF& area, bool focused);
    void PaintDragOverlay(Renderer& r);

    bool HandleListClick(const Region& region, const MouseEvent& e);
    void ScrollPane(Pane* pane, float deltaPixels);

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

    SplitNode* dragSplitter_ = nullptr;
    float dragOrigin_ = 0.0f;
    float dragRatio_ = 0.5f;

    Pane* dragTabPane_ = nullptr;
    int dragTabIndex_ = -1;
    Pane* dropTabPane_ = nullptr;
    int dropTabIndex_ = -1;
    RectF dropTabMarker_{};

    bool dropActive_ = false;
    RectF dropHighlight_{};
    std::string dropPath_;

    float sidebarScroll_ = 0.0f;
    RectF sidebarRect_{};
};

}  // namespace kite::ui
