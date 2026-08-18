/// @file
/// @brief ドキュメントモデル。タブ、ペイン、分割ツリー、セッション。
///
/// ```
/// Workspace  N 個の Session を持ち、1 つがアクティブ
/// Session    SplitNode のツリーを持つ。葉が 1 つの Pane を持つ
/// Pane       N 個の Tab を持ち、1 つがアクティブ
/// Tab        パス、一覧、表示状態を持つ
/// ```
///
/// 全セッションが常駐しているのでセッション切り替えはインデックスの移動だけで済む。
/// 背面に回ったセッションの非アクティブタブは一覧を解放し、常駐メモリを画面に
/// 映っている量に比例させる。

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/base/Types.h"
#include "core/fs/FileSystem.h"

namespace kite {

/// @brief 一覧の並べ替え基準。
enum class SortKey : uint8_t {
    Name,  ///< 名前順
    Ext,   ///< 拡張子順
    Size,  ///< サイズ順
    Date,  ///< 更新日時順
};

/// @brief タブごとの表示設定。
struct ViewState {
    SortKey sort = SortKey::Name;  ///< 並べ替えの基準
    bool sortDesc = false;         ///< true なら降順
    bool dirsFirst = true;         ///< true ならフォルダを先頭にまとめる
    bool showHidden = false;       ///< true なら隠しファイルも表示する
};

/// @brief 1 つのタブ。表示中のフォルダとその一覧・選択状態・履歴を持つ。
class Tab {
public:
    /// @brief 「..」行を表す visible の値。listing.entries への添字ではない。
    ///
    /// 親フォルダは列挙結果に含まれない（`fs::ListResult` は "." と ".." を返さない）
    /// ので、実体を持たない行としてここに紛れ込ませる。こうすると行番号・カーソル・
    /// スクロールの計算が「表示行 = visible への添字」のままで済む。
    /// 添字として使う前に必ず IsParentRow() か EntryAt() を通すこと。
    static constexpr int kParentRow = -1;

    std::string path;  ///< 表示しているディレクトリのパス
    ViewState view;    ///< 並べ替えと表示の設定

    fs::ListResult listing;       ///< 列挙結果そのもの
    std::vector<int> visible;     ///< 表示行。listing.entries への添字か kParentRow
    std::vector<uint8_t> marked;  ///< listing.entries と同じ長さの選択フラグ

    std::string filter;   ///< 絞り込み文字列。空なら絞り込みなし
    int cursor = 0;       ///< カーソル位置。visible への添字
    int anchor = 0;       ///< 範囲選択の起点。visible への添字
    float scroll = 0.0f;  ///< スクロール量（ピクセル）

    std::vector<std::string> back;     ///< 「戻る」履歴
    std::vector<std::string> forward;  ///< 「進む」履歴

    uint64_t loadToken = 0;  ///< 実行中の列挙リクエストのトークン。0 なら実行中でない
    bool loaded = false;     ///< 一覧が取得済みか

    /// 次に一覧が届いたときカーソルを合わせたい項目名。
    /// 親フォルダへ移動する際に設定し、出てきたフォルダの上に着地させるために使う。
    std::string pendingFocusName;

    /// タブ生存中は変化しない。対応するファイルシステム監視を識別する。
    uint64_t watchId = 0;

    /// @brief タブ見出しに使う短い名前を返す。
    /// @return 末尾の構成要素。取れない場合はパスそのもの
    std::string title() const;

    /// @brief listing と filter、view から visible を作り直す。
    /// @note 可能な限りカーソルを同じ項目名の上に維持する。pendingFocusName が
    ///       設定されていればそちらを優先し、消費する
    /// @note path に親があり、かつ列挙が成功していれば先頭に「..」行を足す。
    ///       絞り込みにも並べ替えにも掛けない ─ 移動手段であって項目ではない
    void Rebuild();

    /// @brief 先頭に「..」行があるかを判定する。
    /// @return あれば true。path がルートなら false
    bool hasParentRow() const { return !visible.empty() && visible.front() == kParentRow; }

    /// @brief 表示行が「..」かを判定する。
    /// @param[in] visibleIndex 判定する行。visible への添字
    /// @return 「..」行なら true。範囲外なら false
    bool IsParentRow(int visibleIndex) const;

    /// @brief 表示行に対応する項目を返す。
    /// @param[in] visibleIndex 対象の行。visible への添字
    /// @return 対応する項目。「..」行または範囲外なら nullptr
    const fs::Entry* EntryAt(int visibleIndex) const;

    /// @brief 「..」行を除いた表示件数を返す。
    /// @return 実際の項目数
    int ItemCount() const;

    /// @brief 一覧データを解放する。背面に回ったタブのメモリを減らすために使う。
    /// @note path と履歴は残るので、再表示時に再列挙すれば元に戻る
    void DropListing();

    /// @brief カーソル位置の項目を返す。
    /// @return 対応する項目。一覧が空、またはカーソルが範囲外なら nullptr
    const fs::Entry* CursorEntry() const;

    /// @brief カーソル位置の項目のフルパスを返す。
    /// @return フルパス。項目が無ければ空文字列
    std::string CursorPath() const;

    /// @brief 操作対象のパス一覧を返す。
    /// @return 選択済み項目のフルパス列。1 つも選択されていなければカーソル位置の
    ///         項目 1 件。それも無ければ空
    std::vector<std::string> SelectionPaths() const;

    /// @brief 表示中かつ選択済みの項目数を返す。
    /// @return 選択件数
    int MarkedCount() const;

    /// @brief 表示中かつ選択済みの項目の合計サイズを返す。
    /// @return 合計バイト数
    uint64_t MarkedBytes() const;

    /// @brief 選択をすべて解除する。
    /// @note ExtendTo() の途中なら、その土台も捨てて一区切りにする
    void ClearMarks();

    /// @brief 表示上の範囲をまとめて選択・解除する。
    /// @param[in] fromVisible 範囲の一端。visible への添字
    /// @param[in] toVisible 範囲のもう一端。visible への添字
    /// @param[in] value true で選択、false で解除
    /// @note 添字の大小は問わない。範囲外はクランプされる。「..」行は飛ばす
    void MarkRange(int fromVisible, int toVisible, bool value);

    /// @brief 範囲選択の起点を今のカーソル位置に置き直す。
    /// @note 選択そのものには触れない。カーソルを動かしても印が消えないのは
    ///       このため ─ 離れた項目を Space で拾っていける
    void ResetAnchor();

    /// @brief 起点から指定行までを範囲選択し、カーソルをそこへ移す。
    /// @param[in] toVisible 範囲の終端。visible への添字。範囲外はクランプされる
    /// @note 連続して呼ばれる間は「伸縮を始める前の選択」を土台に引き直す。
    ///       範囲を縮めたときに印が残らず、Space で付けた離れた印も巻き添えに
    ///       しない。ResetAnchor() が土台を捨てて一区切りにする
    void ExtendTo(int toVisible);

private:
    std::vector<uint8_t> markBase;  // 伸縮を始める直前の marked。extending 中のみ有効
    bool extending = false;         // 直前の操作が ExtendTo だったか
};

/// @brief 1 つのペイン。複数のタブを持ち、そのうち 1 つを表示する。
class Pane {
public:
    std::vector<std::unique_ptr<Tab>> tabs;  ///< 保持しているタブ
    int active = 0;                          ///< アクティブなタブの添字

    float listHeight = 0.0f;  ///< 一覧領域の高さ。UI 層がレイアウトごとに書き込む
    float rowHeight = 22.0f;  ///< 1 行の高さ。UI 層がレイアウトごとに書き込む
    int rowsPerPage = 20;     ///< 1 画面分の行数。UI 層がレイアウトごとに書き込む

    /// @brief タブバーの先頭に出す行。ホイールで動かした位置を覚える。
    ///
    /// 「行」は折り返しの単位で、横置きなら画面上の 1 行、縦置きならタブ 1 枚。
    /// 覚えているのは**ホイールで動かしたときだけ**その位置を保つためで、タブを
    /// 切り替えれば tabScrollFor の側がこれを引き戻す（AppUi::LayoutTabBar）。
    int tabScroll = 0;

    /// @brief tabScroll をアクティブなタブに合わせたときの active の値。
    ///
    /// これが active と食い違っていたら、選択が動いたのにまだ引き戻していない
    /// ということ。**両者が同じ間は tabScroll に触らない** ─ 触ると、ホイールで
    /// 別のタブを見に行った次のフレームに必ず元へ戻る。
    int tabScrollFor = -1;

    int tabRows = 1;         ///< タブバーの総行数。UI 層がレイアウトごとに書き込む
    int tabRowsPerPage = 1;  ///< タブバーに出ている行数。UI 層がレイアウトごとに書き込む

    /// @brief 一覧の描画領域（クライアント座標・DIP）。UI 層がレイアウトごとに書き込む。
    ///
    /// キーボードから出すコンテキストメニューをカーソル行の位置に合わせるために
    /// 要る。App は行の座標を自分では持てない ─ どこに何を描いたかを知っているのは
    /// UI 層だけなので、listHeight などと同じくここへ書き戻してもらう。
    /// 一度もレイアウトされていなければ empty() が true。
    RectF listArea;

    /// @brief アクティブなタブを返す。
    /// @return アクティブなタブ。タブが 1 つも無ければ nullptr
    Tab* activeTab();

    /// @brief アクティブなタブを返す（const 版）。
    /// @return アクティブなタブ。タブが 1 つも無ければ nullptr
    const Tab* activeTab() const;

    /// @brief タブを追加してアクティブにする。
    /// @param[in] path 新しいタブが表示するパス
    /// @param[in] at 挿入位置。負値または範囲外なら末尾に追加する
    /// @return 追加したタブ。所有権は Pane が持つ
    Tab* AddTab(const std::string& path, int at = -1);

    /// @brief タブを閉じる。
    /// @param[in] index 閉じるタブの添字
    /// @param[out] closedPath 閉じたタブのパスが入る。不要なら nullptr
    /// @return 閉じたら true。添字が不正、または最後の 1 枚なら false
    /// @note ペインは常にタブを 1 枚以上保つ
    bool CloseTab(int index, std::string* closedPath);

    /// @brief アクティブなタブを切り替える。
    /// @param[in] index アクティブにするタブの添字。範囲外はクランプする
    void Activate(int index);

    /// @brief 同じペイン内でタブを並べ替える。
    /// @param[in] fromIndex 移動するタブの添字
    /// @param[in] toIndex 移動先の添字。範囲外はクランプする
    /// @return 並べ替えたら true。添字が不正、または移動先が同じなら false
    /// @note アクティブなタブが変わらないよう active も追随させる
    bool ReorderTab(int fromIndex, int toIndex);

    /// @brief タブを取り外して所有権を返す。
    /// @param[in] index 取り外すタブの添字
    /// @return 取り外したタブ。添字が不正なら nullptr
    /// @note ペインが空になりうる。空にした呼び出し側がペインを閉じる責任を持つ
    std::unique_ptr<Tab> DetachTab(int index);

    /// @brief タブを受け取ってアクティブにする。
    /// @param[in] tab 受け取るタブ。nullptr なら何もしない
    /// @param[in] at 挿入位置。範囲外なら末尾に追加する
    void AttachTab(std::unique_ptr<Tab> tab, int at);

    /// @brief タブが 1 枚も無いかを判定する。
    /// @return 空なら true
    bool empty() const { return tabs.empty(); }
};

/// @brief 監視識別子を採番する。
/// @return 単調増加する識別子。タブごとに 1 つ割り当て、並べ替えでも変わらない
uint64_t NextWatchId();

/// @brief ペイン分割ツリーのノード。葉がペイン、内部ノードが分割を表す。
struct SplitNode {
    /// @brief ノードの種別。
    enum class Kind : uint8_t {
        Leaf,       ///< 葉。ペインを 1 つ持つ
        LeftRight,  ///< 左右に分割（縦の仕切り）
        TopBottom,  ///< 上下に分割（横の仕切り）
    };

    Kind kind = Kind::Leaf;         ///< ノードの種別
    float ratio = 0.5f;             ///< 分割比。a 側が占める割合
    std::unique_ptr<Pane> pane;     ///< Kind::Leaf のときのみ有効
    std::unique_ptr<SplitNode> a;   ///< 前側の子。左または上
    std::unique_ptr<SplitNode> b;   ///< 後側の子。右または下
    SplitNode* parent = nullptr;    ///< 親ノード。ルートでは nullptr

    RectF rect;          ///< 最後にレイアウトされた領域。方向フォーカス移動に使う
    RectF splitterRect;  ///< 操作可能な分割線の領域。葉では空

    /// @brief 葉かどうかを判定する。
    /// @return 葉なら true
    bool leaf() const { return kind == Kind::Leaf; }
};

/// @brief 1 つのセッション。ペイン分割のレイアウトとフォーカスを持つ。
class Session {
public:
    std::string name;               ///< 利用者が付けた名前
    std::unique_ptr<SplitNode> root;  ///< 分割ツリーの根
    Pane* focus = nullptr;          ///< フォーカスされているペイン

    /// @brief 単一ペインのセッションを作る。
    /// @param[in] name セッション名
    /// @param[in] path 最初のタブが表示するパス
    /// @return 生成したセッション
    static std::unique_ptr<Session> Create(const std::string& name, const std::string& path);

    /// @brief すべてのペインを表示順に返す。
    /// @return ペインへのポインタ列。所有権は移らない
    std::vector<Pane*> Panes() const;

    /// @brief ペインを保持している葉ノードを探す。
    /// @param[in] p 探すペイン
    /// @return 対応する葉ノード。見つからなければ nullptr
    SplitNode* LeafOf(const Pane* p) const;

    /// @brief ペインを分割して新しいペインを作る。
    /// @param[in] target 分割するペイン
    /// @param[in] kind 分割方向。Kind::Leaf を渡した場合は何もしない
    /// @return 生成されたペイン。失敗したら nullptr
    /// @note 新しいペインは元のペインと同じフォルダを開く
    Pane* Split(Pane* target, SplitNode::Kind kind);

    /// @brief ペインを閉じ、分割を兄弟側へ畳む。
    /// @param[in] target 閉じるペイン
    /// @return 閉じたら true。最後の 1 つなら false
    bool ClosePane(Pane* target);

    /// @brief ペインを兄弟と入れ替える。
    /// @param[in] target 入れ替えるペイン。分割の中に無い場合は何もしない
    void SwapWithSibling(Pane* target);

    /// @brief 画面上の方向で隣接するペインを探す。
    /// @param[in] from 起点のペイン
    /// @param[in] dx 水平方向。-1 で左、1 で右、0 で指定なし
    /// @param[in] dy 垂直方向。-1 で上、1 で下、0 で指定なし
    /// @return 見つかったペイン。無ければ nullptr
    /// @pre 直前に一度レイアウトされ SplitNode::rect が埋まっていること
    Pane* PaneInDirection(const Pane* from, int dx, int dy) const;

    /// @brief レイアウトを文字列に直列化する。
    /// @return sessions.ini に保存できる文字列
    std::string Serialize() const;

    /// @brief Serialize() の出力からセッションを復元する。
    /// @param[in] name 復元するセッションに付ける名前
    /// @param[in] text Serialize() が出力した文字列
    /// @return 復元したセッション。書式が壊れている場合は nullptr
    static std::unique_ptr<Session> Deserialize(const std::string& name, const std::string& text);
};

/// @brief 登録されたブックマーク 1 件。
struct Bookmark {
    std::string name;  ///< 表示名
    std::string path;  ///< 移動先のパス
};

/// @brief 全セッションとブックマークを保持する最上位のモデル。
class Workspace {
public:
    std::vector<std::unique_ptr<Session>> sessions;  ///< 保持しているセッション
    int active = 0;                                  ///< アクティブなセッションの添字
    std::vector<Bookmark> bookmarks;                 ///< ブックマーク
    std::vector<std::string> closedTabs;             ///< 復元用に積んだ閉じたタブのパス

    /// @brief アクティブなセッションを返す。
    /// @return アクティブなセッション。1 つも無ければ nullptr
    Session* activeSession();

    /// @brief アクティブなセッションを返す（const 版）。
    /// @return アクティブなセッション。1 つも無ければ nullptr
    const Session* activeSession() const;

    /// @brief フォーカスされているペインを返す。
    /// @return フォーカス中のペイン。無ければ nullptr
    Pane* focusedPane();

    /// @brief フォーカスされているペインのアクティブタブを返す。
    /// @return 対象のタブ。無ければ nullptr
    Tab* focusedTab();

    /// @brief セッションを追加してアクティブにする。
    /// @param[in] name セッション名
    /// @param[in] path 最初のタブが表示するパス
    /// @return 追加したセッション。所有権は Workspace が持つ
    Session* AddSession(const std::string& name, const std::string& path);

    /// @brief セッションを閉じる。
    /// @param[in] index 閉じるセッションの添字
    /// @note 最後の 1 つは閉じられない。添字が不正な場合も何もしない
    void CloseSession(int index);

    /// @brief アクティブなセッションを切り替える。
    /// @param[in] index アクティブにするセッションの添字。範囲外はクランプする
    /// @note 切り替え時、離れるセッションの非アクティブタブは一覧を解放する
    void ActivateSession(int index);

    /// @brief セッションの並び順を変える。
    /// @param[in] fromIndex 動かすセッションの添字
    /// @param[in] toIndex 移動先の添字。範囲外はクランプする
    /// @return 並べ替えたら true。添字が不正、または移動先が同じなら false
    /// @note アクティブなセッションが変わらないよう active も追随させる
    ///       （Pane::ReorderTab と同じ約束）
    bool ReorderSession(int fromIndex, int toIndex);
};

}  // namespace kite
