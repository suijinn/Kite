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
#include <variant>
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
        TabBarEdge,  ///< 縦置きタブバーの右の縁。掴むと幅が変わる
        Crumb,
        ColumnHeader,
        ColumnEdge,  ///< 列の左端の縁。掴むと幅が変わる
        GroupRow,    ///< 塊の見出し。押すとその塊が丸ごと選ばれる
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

    /// ペインの分割線を掴んでいる。
    struct SplitterDrag {
        SplitNode* node = nullptr;  ///< 掴んでいる分割
        float origin = 0.0f;        ///< 押した位置。分割の向きの軸だけ
        float ratio = 0.5f;         ///< 押した時点の比
    };

    /// タブを掴んでいる。押しただけの間は `started` が false。
    struct TabDrag {
        Pane* pane = nullptr;   ///< 掴んだタブのペイン
        int index = -1;         ///< 掴んだタブの位置
        bool started = false;   ///< 6 px 動いて本当のドラッグになったか
        Pane* dropPane = nullptr;  ///< 落とし先のペイン。無ければ nullptr
        int dropIndex = -1;        ///< 落とし先の位置
        RectF marker{};            ///< 挿入位置の印
        /// 窓の外まで運ばれた ─ 離せば新しいウィンドウ。**当たり判定ではなく
        /// 描画面の外側で測る**（バーの上も «当たりが無い» だが、外ではない）。
        bool outside = false;
    };

    /// 行を押した ─ 動かせば OS のファイルドラッグになる。
    ///
    /// 本番になった瞬間に OS へ渡して自分の状態は捨てるので、`started` に当たる
    /// ものは要らない（`BeginFileDrag` は終わるまで戻らない）。
    struct FileDrag {};

    /// 一覧の余白から選択の枠を引いている。
    ///
    /// 引き始めた点は画面座標ではなく **一覧の中の位置**（行 0 の上端からの画素数）
    /// で覚える ─ ホバーが行番号を覚えないのと同じ理由で、ガラスの上の一点が何を
    /// 指すかは一覧が動けば変わる。
    struct MarqueeDrag {
        Pane* pane = nullptr;
        Tab* tab = nullptr;
        float anchorX = 0.0f;  ///< 引き始めた x（画面座標）
        float anchorY = 0.0f;  ///< 引き始めた y（一覧の中の位置）
        float x = 0.0f;        ///< 今の x（画面座標）
        float y = 0.0f;        ///< 今の y（画面座標）
        std::vector<uint8_t> base;  ///< 引き始めたときの印。毎フレームここから引き直す
    };

    /// 並べ替えの相手。4 種とも «提案 → 印 → 確定» の 3 段が同じ形をしている。
    enum class ReorderKind : uint8_t {
        Sidebar,  ///< 1 つの区画の中の項目
        Section,  ///< サイドバーの区画そのもの
        Session,  ///< セッションのチップ
        Column,   ///< 一覧の列
    };

    /// 並べ替えのドラッグ。押しただけの間は `started` が false。
    struct ReorderDrag {
        ReorderKind kind = ReorderKind::Sidebar;
        SidebarSection section = SidebarSection::Count;  ///< Sidebar / Section のみ
        int index = -1;        ///< 掴んだものの位置
        bool started = false;  ///< 6 px 動いて本当のドラッグになったか
        int dropIndex = -1;    ///< 落とし先。提案が無ければ -1
        RectF marker{};        ///< 挿入位置の印

        /// 押しただけで離したときに開くフォルダ（Sidebar のみ）。
        std::string pendingPath;
        bool pendingNewTab = false;
    };

    /// 列の縁を掴んで幅を変えている。右端は動かないので、幅は «右端 - ポインタ»。
    struct ColumnWidthDrag {
        int index = -1;
        float right = 0.0f;
    };

    /// 縦置きタブバーの右の縁を掴んで幅を変えている。
    ///
    /// 上限を控えるのは、レイアウトが幅をペインの半分で止めるため ─ 控えずに
    /// 渡すと、画面のバーはもう伸びないのに覚えている幅だけが増える。
    struct TabBarWidthDrag {
        float left = 0.0f;
        float max = 0.0f;
    };

    /// 左ボタンが今おこなっている操作 ─ **1 つだけ**。
    ///
    /// 種別ごとのフィールドを平置きしていたころは、`CancelDrag` がそれを 1 つずつ
    /// 初期値へ戻す列で、フィールドを足すたびに抜けた。型にしてあれば `drag_ = {}`
    /// で全部が消える。
    using DragWhat = std::variant<std::monostate, SplitterDrag, TabDrag, FileDrag, MarqueeDrag,
                                  ReorderDrag, ColumnWidthDrag, TabBarWidthDrag>;

    /// 今のドラッグと、それが始まった点。
    struct DragState {
        /// 押した場所。«6 px 動いたら本番» を測る相手で、どの種別にも共通。
        PointF start{};
        DragWhat what{};
    };

    /// 当たり判定 1 つぶん。**文字列を持たない** ─ 毎フレーム全部を積み直すので、
    /// 行ごとにパスを写すとサイドバーやパンくずの行数ぶんの割り当てが乗る。
    /// パスの要る 2 種（サイドバーの行・パンくず）は添字から引き直す（PathIn）。
    struct Region {
        RectF rect;
        Hit kind = Hit::None;
        Pane* pane = nullptr;
        SplitNode* node = nullptr;
        int index = 0;
        SidebarSection section = SidebarSection::Count;  ///< サイドバーの行のみ
    };

    void Add(const RectF& r, Hit kind, int index = 0, Pane* pane = nullptr,
             SplitNode* node = nullptr);
    void AddSidebar(const RectF& r, Hit kind, SidebarSection section, int index);
    const Region* Pick(float x, float y) const;

    // The path a region points at, for the two kinds that point at one. Looked
    // up from the index rather than carried, so Region stays a POD. By value:
    // this is asked once per click, never per row per frame.
    std::string PathIn(const Region* region) const;

    // The breadcrumb trail of a tab, deepest last - the paths only. PaintPathBar
    // labels them; a click resolves the one it hit back through this.
    static std::vector<std::string> CrumbPaths(const Tab& tab);

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

    /// @brief ボタンを押したときの振り分け。
    /// @param[in] e 処理するイベント
    /// @return 消費したら true
    bool OnPress(const MouseEvent& e);

    /// @brief ポインタが動いたときの振り分け。
    /// @param[in] e 処理するイベント
    /// @param[in,out] redraw 入口の番人。何も変わらなかったときだけ降ろす
    /// @return 消費したら true
    /// @note 押しただけのドラッグが本番になるのはここ 1 か所
    bool OnDrag(const MouseEvent& e, Redraw& redraw);

    /// @brief ボタンを離したときの振り分け。
    /// @param[in] e 処理するイベント
    /// @return 消費したら true
    bool OnRelease(const MouseEvent& e);

    /// @brief ホイールの振り分け。
    /// @param[in] e 処理するイベント
    /// @return 消費したら true
    bool OnWheel(const MouseEvent& e);
    void BeginMarquee(Pane* pane, const MouseEvent& e);
    void UpdateMarquee(float x, float y);
    void ScrollPane(Pane* pane, float deltaPixels);

    bool ResolveTabDrop(float x, float y, Pane** outPane, int* outIndex) const;
    /// 1 ペインに置いた列 1 つ。見出しを描くときも行を描くときも同じ位置を使う。
    struct PlacedColumn {
        SortKey id = SortKey::Name;  ///< どの列か
        int index = 0;               ///< App::columns() への添字
        float l = 0.0f;              ///< 左端
        float r = 0.0f;              ///< 右端
    };

    /// @brief 列を並べ、入らない列を落とす。
    /// @param[in] area 一覧の矩形
    /// @param[out] outName 名前の列の右端
    /// @return 名前以外の列を左から順に並べたもの
    /// @note **落とすのは右端から。** どれを右へ置いたかは利用者が決めたことなので、
    ///       «名前から遠いほう» の答えはその並びがすでに言っている
    std::vector<PlacedColumn> LayoutColumns(const RectF& area, float* outName) const;

    /// @brief 見出しの矩形を前フレームの当たり判定から引く。
    /// @param[in] pane 対象のペイン
    /// @param[in] index App::columns() への添字
    /// @return 見出しの矩形。無ければ空
    RectF ColumnHeaderRect(const Pane* pane, int index) const;
    RectF TabBarRect(const Pane* pane) const;

    bool ResolveColumnDrop(float x, float y, int* outIndex, RectF* outMarker) const;

    /// @brief その列で並べ替える。
    /// @param[in] index App::columns() への添字。範囲外なら何もしない
    /// @note 列の識別子は並べ替えの基準そのものなので、表を 1 つ引くだけで済む
    void SortByColumn(int index);
    bool ResolveSessionDrop(float x, float y, int* outIndex, RectF* outMarker) const;
    bool ResolveSidebarDrop(float x, float y, int* outIndex, RectF* outMarker) const;
    RectF SectionBlock(SidebarSection section) const;
    bool ResolveSectionDrop(float x, float y, int* outIndex, RectF* outMarker) const;

    /// @brief 並べ替えのドラッグが今どの位置を求めているかを引き直す。
    /// @param[in] drag 対象のドラッグ。落とし先と印を書き換える
    /// @param[in] x ポインタの X
    /// @param[in] y ポインタの Y
    /// @note 4 種とも同じ 3 段 ─ 提案（Resolve*）→ 印 → 確定（FinishReorder）─ を
    ///       通るので、種別で表を引くだけで済む。自分の行から外れたところでは
    ///       何も提案しない（落とし先が -1 になる）
    void ProposeReorder(ReorderDrag& drag, float x, float y);

    /// @brief 並べ替えのドラッグを確定してドラッグを畳む。
    /// @param[in] drag 対象のドラッグ
    /// @note 落とし先が無ければ順序は変わらない ─ 提案しなかった場所で離すのは
    ///       «やめた» と同じ
    void FinishReorder(const ReorderDrag& drag);

    void FinishTabDrag(const TabDrag& drag);
    void CancelDrag();

    /// @brief ホバーの強調を止めるドラッグかを返す。
    /// @return 光らせてはいけない間なら true
    /// @note 分割線・タブ・枠・並べ替え（列を除く）・タブバーの幅 ─ どれも
    ///       «通りすがった行が光ると掴んでいるものの行き先が読めない» が理由
    bool DragHidesHover() const;

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

    DragState drag_{};

    // A press on a row that was already marked keeps the marks - the press may
    // be the start of a drag, and a drag carries the selection - so dropping
    // them waits for the release that turns out to be a plain click.
    //
    // Not part of DragState: it is not what is being dragged, it is a promise
    // about what letting go means.
    bool pendingUnmark_ = false;

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
