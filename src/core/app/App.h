/// @file
/// @brief コントローラ。全状態を保持し、全コマンドを実行する。
///
/// ピクセルのことも Win32 のことも知らない。UI 層がここから読み取り、イベントを
/// 流し込む。プラットフォーム層がファイルシステム・シェル・ウィンドウを提供する。

#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "core/app/BookmarkPicker.h"
#include "core/app/Host.h"
#include "core/app/IconProvider.h"
#include "core/app/SettingsEditor.h"
#include "core/app/UndoStack.h"
#include "core/base/Ini.h"
#include "core/fs/DirectoryLoader.h"
#include "core/fs/DirectoryWatcher.h"
#include "core/fs/FileSystem.h"
#include "core/i18n/Strings.h"
#include "core/input/Commands.h"
#include "core/input/KeyEditor.h"
#include "core/input/KeyMap.h"
#include "core/input/PathComplete.h"
#include "core/input/TypeAhead.h"
#include "core/model/Workspace.h"
#include "core/theme/Theme.h"

namespace kite {

/// @brief 入力欄が何を尋ねているか。
enum class PromptKind : uint8_t {
    None,                    ///< 入力欄は出ていない
    Path,                    ///< 移動先パスの入力
    Filter,                  ///< 一覧の絞り込み
    Rename,                  ///< 名前の変更
    NewFolder,               ///< 新しいフォルダ名
    NewFile,                 ///< 新しいファイル名
    SessionName,             ///< セッション名の変更
    ConfirmDelete,           ///< ごみ箱への削除の確認
    ConfirmDeletePermanent,  ///< 完全削除の確認
};

/// @brief 入力欄の状態。
struct Prompt {
    PromptKind kind = PromptKind::None;      ///< 何を尋ねているか
    std::string labelKey;                    ///< 見出しの i18n キー
    std::string text;                        ///< 入力中の文字列
    size_t caret = 0;                        ///< キャレット位置。text へのバイト添字
    size_t anchor = 0;                       ///< 選択範囲のもう一端。caret と等しければ選択なし
    std::vector<std::string> pendingPaths;   ///< 確認待ちの操作対象

    /// @brief 入力欄が表示されているかを判定する。
    /// @return 表示中なら true
    bool active() const { return kind != PromptKind::None; }

    /// @brief 文字列が選択されているかを判定する。
    /// @return 選択されていれば true
    bool hasSelection() const { return caret != anchor; }

    /// @brief 選択範囲の先頭を返す。
    /// @return text へのバイト添字
    size_t selBegin() const { return caret < anchor ? caret : anchor; }

    /// @brief 選択範囲の終端を返す。
    /// @return text へのバイト添字。選択が無ければ selBegin() と等しい
    size_t selEnd() const { return caret < anchor ? anchor : caret; }

    /// @brief キャレットを動かし、選択を解除する。
    /// @param[in] pos 移動先。text へのバイト添字
    void SetCaret(size_t pos) {
        caret = pos;
        anchor = pos;
    }

    /// @brief 範囲を選択し、キャレットを終端側に置く。
    /// @param[in] begin 選択の先頭。text へのバイト添字
    /// @param[in] end 選択の終端。text へのバイト添字。どちらも末尾に丸める
    /// @note キャレットが終端側なのは、`→` を押せば選択の後ろへ畳めるという
    ///       入力欄の一般則に合わせるため
    void SelectRange(size_t begin, size_t end) {
        anchor = begin < text.size() ? begin : text.size();
        caret = end < text.size() ? end : text.size();
    }

    /// @brief 全体を選択する。
    void SelectAll() { SelectRange(0, text.size()); }

    /// @brief 選択されている範囲を削除する。
    /// @return 実際に削除したら true
    /// @note 文字入力・Backspace・Delete がまずこれを呼ぶ。選択したまま打った
    ///       文字が置き換えではなく挿入になると、全選択が何のためにあるのか
    ///       分からなくなる
    bool DeleteSelection() {
        if (!hasSelection()) return false;
        const size_t begin = selBegin();
        text.erase(begin, selEnd() - begin);
        SetCaret(begin);
        return true;
    }

    /// @brief 文字入力ではなく Yes/No の確認かを判定する。
    /// @return 確認なら true
    bool isConfirm() const {
        return kind == PromptKind::ConfirmDelete || kind == PromptKind::ConfirmDeletePermanent;
    }

    /// @brief 対象そのものの上で編集する入力欄かを判定する。
    /// @return パンくずの行・一覧の行・セッションチップの上に出るものなら true
    /// @note 画面下部の帯に出るのは、これが false のもの ─ 一覧全体を相手にする
    ///       絞り込みと、文字を打つのではない削除の確認だけ
    bool isInline() const {
        return kind == PromptKind::Path || kind == PromptKind::Rename ||
               kind == PromptKind::NewFolder || kind == PromptKind::NewFile ||
               kind == PromptKind::SessionName;
    }
};

/// @brief サイドバーの区画。折り畳みはこの単位で行う。
enum class SidebarSection : uint8_t {
    QuickAccess,  ///< クイックアクセス
    Bookmarks,    ///< ブックマーク
    Drives,       ///< ドライブ
    Count         ///< 列挙の終端。有効な区画ではない
};

/// @brief ウィンドウの位置とサイズ。
struct WindowPlacement {
    int x = -1;             ///< 左端。負値なら OS 既定に任せる
    int y = -1;             ///< 上端。負値なら OS 既定に任せる
    int w = 1180;           ///< 幅
    int h = 720;            ///< 高さ
    bool maximized = false; ///< 最大化状態で復元するか
};

/// @brief アプリケーション本体。状態の保持とコマンド実行を担う。
class App {
public:
    /// @brief 依存オブジェクトを受け取って構築する。
    /// @param[in] filesystem ファイルシステム。App より長生きすること
    /// @param[in] shell シェル連携。App より長生きすること
    /// @param[in] host ウィンドウ。App より長生きすること
    /// @param[in] watcher 変更監視。nullptr なら自動更新を行わない
    App(fs::IFileSystem& filesystem, IShellIntegration& shell, IHost& host,
        fs::IDirectoryWatcher* watcher = nullptr);

    /// @brief ワーカースレッドを止めて破棄する。
    ~App();

    App(const App&) = delete;
    App& operator=(const App&) = delete;

    /// @brief シェルアイコンの取得口を差し込む。
    /// @param[in] icons 取得口。nullptr なら常にベクタ描画のアイコンを使う。
    ///            App より長生きすること
    /// @note Init() より前に呼ぶこと。設定で無効にされていればここで渡しても使わない
    void SetIconProvider(IIconProvider* icons) { icons_ = icons; }

    /// @brief パスに対応するシェルアイコンの識別子を返す。
    /// @param[in] path 対象のパス
    /// @return 描画に使える識別子。取得口が無い、設定で無効、まだ届いていない
    ///         場合は 0
    /// @note UI 層が描画のたびに呼ぶ
    uint32_t IconFor(const std::string& path);

    /// @brief 単独ウィンドウとして動かすかを指定する。
    /// @param[in] standalone true なら単独ウィンドウ
    /// @note Init() より前に呼ぶこと。単独ウィンドウは保存されたセッションを読まず、
    ///       設定もワークスペースも書き戻さない。`Cmd::NewWindow` で開いた 2 枚目
    ///       以降がこれにあたる。読めば新しいウィンドウに元の窓の全セッションが
    ///       複製され、書けば後から閉じたほうが本体の変更を古い内容で上書きする
    void SetStandalone(bool standalone) { standalone_ = standalone; }

    /// @brief 単独ウィンドウかを返す。
    /// @return 単独ウィンドウなら true
    bool standalone() const { return standalone_; }

    /// @brief 設定とワークスペースを読み込み、最初の列挙を開始する。
    /// @param[in] startPaths コマンドラインで指定されたパス。追加タブとして開く。
    ///            単独ウィンドウでは先頭がそのウィンドウ自身の開始位置になる
    /// @return 常に true
    bool Init(const std::vector<std::string>& startPaths);

    /// @brief 設定を保存し、ワーカースレッドを停止する。
    void Shutdown();

    /// @brief キー入力を処理する。
    /// @param[in] chord 押された和音
    /// @return 消費したら true。false ならプラットフォーム側が既定処理を行ってよい
    bool OnKey(const Chord& chord);

    /// @brief 文字入力を処理する。
    /// @param[in] codepoint 入力された Unicode コードポイント
    /// @return 消費したら true。入力欄が出ていなければ false
    bool OnChar(uint32_t codepoint);

    /// @brief コマンドを実行する。ここがコマンド処理の唯一の入口。
    /// @param[in] cmd 実行するコマンド
    void Execute(Cmd cmd);

    /// @brief 完了した列挙結果と変更通知を取り込む。
    /// @note IHost::Wake() を受けた UI スレッドから呼ぶ
    void PumpLoader();

    /// @brief ワークスペースを返す。
    /// @return ワークスペースへの参照
    Workspace& workspace() { return workspace_; }

    /// @brief ワークスペースを返す（const 版）。
    /// @return ワークスペースへの参照
    const Workspace& workspace() const { return workspace_; }

    /// @brief 現在のテーマを返す。
    /// @return テーマへの参照
    const Theme& theme() const { return theme_; }

    /// @brief 文字列表を返す。
    /// @return 文字列表への参照
    const Strings& strings() const { return strings_; }

    /// @brief キー割り当て表を返す。
    /// @return キーマップへの参照
    const KeyMap& keys() const { return keymap_; }

    /// @brief ファイルシステムを返す。
    /// @return ファイルシステムへの参照
    fs::IFileSystem& filesystem() { return fs_; }

    /// @brief シェル連携を返す。
    /// @return シェル連携への参照
    IShellIntegration& shell() { return shell_; }

    /// @brief ウィンドウを返す。
    /// @return ウィンドウへの参照
    IHost& host() { return host_; }

    /// @brief 入力欄の状態を返す。
    /// @return 入力欄への参照
    const Prompt& prompt() const { return prompt_; }

    /// @brief 入力欄の状態を返す（変更可能）。
    /// @return 入力欄への参照
    Prompt& prompt() { return prompt_; }

    /// @brief パス入力の補完状態を返す。
    /// @return 補完状態への参照
    /// @note 出ているのは PromptKind::Path の入力欄のときだけ。他の入力欄では
    ///       常に閉じている
    const PathComplete& pathComplete() const { return complete_; }

    /// @brief 対象の上で開いている入力欄を畳む。
    /// @note 打った文字列は捨てる。UI 層が「入力欄の外が押された」ときに呼ぶ ─
    ///       押した先が答えなのだから、書きかけの名前を抱えたまま居座らせない。
    ///       効くのは Prompt::isInline() が真の入力欄だけで、画面下部の帯に出る
    ///       絞り込みと削除の確認は残る
    void CancelInlineEdit();

    /// @brief 補完候補を選び、そのフォルダへ移動する。
    /// @param[in] index PathComplete::matches() への添字
    /// @note UI 層が候補行のクリックで呼ぶ。キーボードは Tab で選んで Enter で
    ///       決めるが、マウスで候補を押した人はもう決めている
    void ChooseCompletion(int index);

    /// @brief ショートカット一覧が表示中かを返す。
    /// @return 表示中なら true
    bool keyHelpVisible() const { return keyHelp_; }

    /// @brief ショートカットキー設定画面の状態を返す。
    /// @return 設定画面への参照
    const KeyEditor& keyEditor() const { return keyEditor_; }

    /// @brief ショートカットキー設定画面の状態を返す（変更可能）。
    /// @return 設定画面への参照
    /// @note UI 層がマウス操作（行の選択、和音の選択、取り込みの開始）と 1 画面の
    ///       行数の通知に使う。割り当て表そのものを変える操作はここからは行えない
    ///       ─ 書き出しを伴うので App::RemoveKeyBinding() を通す
    KeyEditor& keyEditor() { return keyEditor_; }

    /// @brief 設定画面で選ばれている行の、和音 1 つぶんの割り当てを解除する。
    /// @param[in] index 選択中の行の chordTexts への添字
    /// @note UI 層が和音のクリックで呼ぶ。keys.ini への書き出しまで行う
    void RemoveKeyBinding(int index);

    /// @brief 設定画面の状態を返す。
    /// @return 設定画面への参照
    const SettingsEditor& settingsEditor() const { return settingsEditor_; }

    /// @brief 設定画面の状態を返す（変更可能）。
    /// @return 設定画面への参照
    /// @note UI 層がマウス操作（行の選択、値の増減）に使う。値を変えたあとの反映と
    ///       settings.ini への保存は App::ApplyPendingSetting() が行うので、値を
    ///       動かしたら必ずそれを呼ぶこと
    SettingsEditor& settingsEditor() { return settingsEditor_; }

    /// @brief ブックマーク一覧の状態を返す。
    /// @return ブックマーク一覧への参照
    const BookmarkPicker& bookmarkPicker() const { return bookmarkPicker_; }

    /// @brief ブックマーク一覧の状態を返す（変更可能）。
    /// @return ブックマーク一覧への参照
    /// @note UI 層がマウス操作（行の選択、ホイール）と 1 画面の行数の通知に使う。
    ///       行を選んで実際に移動するのは App::ChooseBookmark() ─ 移動は
    ///       ワークスペースを動かすので、画面の側に持たせない
    BookmarkPicker& bookmarkPicker() { return bookmarkPicker_; }

    /// @brief ブックマーク一覧で選ばれている行へ移動し、一覧を閉じる。
    /// @param[in] newTab true なら新しいタブで開く
    /// @note UI 層が行のクリックで呼ぶ。キーボードは Enter / Ctrl+Enter で同じ道を通る
    void ChooseBookmark(bool newTab);

    /// @brief 設定画面で変わった値を反映し、settings.ini へ書き出す。
    /// @note SettingsEditor::changed() が指す 1 項目だけを反映する。何も変わって
    ///       いなければ何もしない。UI 層がマウスで値を動かしたあとに呼ぶ
    void ApplyPendingSetting();

    /// @brief 新しいタブを作る位置を返す。
    /// @return 設定されている位置
    NewTabPosition newTabPosition() const { return newTabPosition_; }

    /// @brief タブバーを置く場所を返す。
    /// @return 設定されている場所
    /// @note UI 層はこれ 1 つでタブバーの向きを決める。Left なら縦置き
    TabBarPosition tabBarPosition() const { return tabBarPosition_; }

    /// @brief サイドバーが表示中かを返す。
    /// @return 表示中なら true
    bool sidebarVisible() const { return sidebarVisible_; }

    /// @brief サイドバーの区画を上から順に返す。
    /// @return 区画の並び。必ず全区画がちょうど 1 回ずつ含まれる
    const std::vector<SidebarSection>& sidebarSections() const { return sidebarSections_; }

    /// @brief サイドバーの区画そのものを並べ替える。
    /// @param[in] from 動かす区画の位置。sidebarSections() への添字
    /// @param[in] to 動かした先の位置。**抜き取ったあとの並びでの添字**
    ///            （MoveSidebarItem と同じ約束）
    /// @return 並びが実際に変わったら true
    bool MoveSidebarSection(int from, int to);

    /// @brief サイドバーの区画が折り畳まれているかを返す。
    /// @param[in] section 対象の区画。SidebarSection::Count は常に false
    /// @return 折り畳まれていれば true
    bool sidebarCollapsed(SidebarSection section) const;

    /// @brief サイドバーの区画の折り畳みを切り替える。
    /// @param[in] section 対象の区画。SidebarSection::Count なら何もしない
    /// @note 状態は settings.ini に残る。ドライブが 20 台ある環境で、毎回開くたびに
    ///       ブックマークまでスクロールし直すことになるため
    void ToggleSidebarSection(SidebarSection section);

    /// @brief 文字サイズの倍率を返す。
    /// @return 倍率。1.0 が設定ファイルどおりの大きさ
    float fontScale() const { return fontScale_; }

    /// @brief サイドバーの項目を同じ区画の中で並べ替える。
    /// @param[in] section 対象の区画。SidebarSection::Count なら何もしない
    /// @param[in] from 動かす項目の位置
    /// @param[in] to 動かした先の位置。**抜き取ったあとの並びでの添字**
    ///            （Pane::ReorderTab と同じ約束。後ろへ動かすときは呼ぶ側が 1 引く）
    /// @return 並びが実際に変わったら true。範囲外や移動なしなら false
    /// @note クイックアクセスとドライブは OS が毎回作り直す一覧なので、並びは
    ///       パスの列として settings.ini に覚え、`RefreshRoots()` で掛け直す。
    ///       ブックマークは Kite 自身の並びなので bookmarks.ini がそのまま順序になる
    bool MoveSidebarItem(SidebarSection section, int from, int to);

    /// @brief サイドバーの区画に並ぶ項目数を返す。
    /// @param[in] section 対象の区画
    /// @return 項目数。SidebarSection::Count では 0
    int SidebarItemCount(SidebarSection section) const;

    /// @brief ステータスバーに出すメッセージを返す。
    /// @return メッセージ。無ければ空文字列
    const std::string& statusMessage() const { return statusMessage_; }

    /// @brief ステータスメッセージの表示期限が切れたかを返す。
    /// @return 期限切れなら true
    bool statusExpired() const;

    /// @brief ドライブ一覧を返す。
    /// @return ルート項目の一覧
    const std::vector<fs::Root>& roots() const { return roots_; }

    /// @brief クイックアクセス項目を返す。
    /// @return 既知フォルダの一覧
    const std::vector<fs::Root>& quickAccess() const { return quickAccess_; }

    /// @brief 保存されたウィンドウ位置を返す。
    /// @return ウィンドウ位置
    const WindowPlacement& placement() const { return placement_; }

    /// @brief ウィンドウ位置を記録する。
    /// @param[in] p 記録する位置とサイズ
    void SetPlacement(const WindowPlacement& p) { placement_ = p; }

    /// @brief ペインにフォーカスを移す。
    /// @param[in] pane フォーカスするペイン。nullptr なら何もしない
    void FocusPane(Pane* pane);

    /// @brief フォーカス中のペインのタブを切り替える。
    /// @param[in] index アクティブにするタブの添字。負値なら最後のタブ
    /// @note 一覧を解放したタブ（背面に回っていたセッションのもの）を選んだときに
    ///       再列挙を要求するので、UI 層はタブを直接 Pane::Activate せずここを通すこと
    void GotoTab(int index);

    /// @brief セッションを切り替える。
    /// @param[in] index アクティブにするセッションの添字。範囲外なら何もしない
    /// @note UI 層はセッションチップのクリックでここを呼ぶ。`Cmd::Session1..8` は
    ///       8 個しか無いので、9 個目以降のチップをコマンドに直すことはできない
    void GotoSession(int index);

    /// @brief セッションの並び順を変える。
    /// @param[in] from 動かすセッションの添字
    /// @param[in] to 移動先の添字。範囲外はクランプする
    /// @return 並べ替えたら true
    /// @note UI 層はチップのドラッグでここを呼ぶ。挿入位置は要素を抜く前の数え方で
    ///       渡す（Workspace::ReorderSession と同じ約束。後ろへ動かすときは呼ぶ側が
    ///       1 引く）
    bool MoveSession(int from, int to);

    /// @brief ウィンドウが OS のフォーカスを持っているかを記録する。
    /// @param[in] active 持っていれば true
    /// @note UI がフォーカス表示の色を切り替えるためだけに使う。状態が変わったときだけ
    ///       再描画を要求する。プラットフォーム層がウィンドウのアクティブ化通知で呼ぶ
    void SetWindowActive(bool active);

    /// @brief ウィンドウが OS のフォーカスを持っているかを返す。
    /// @return 持っていれば true。既定は true
    bool windowActive() const { return windowActive_; }

    /// @brief パスを開く。
    /// @param[in] path 開くパス
    /// @param[in] newTab true なら新しいタブで開く
    void OpenPath(const std::string& path, bool newTab);

    /// @brief フォーカス中のタブを別のフォルダへ移動させる。
    /// @param[in] path 移動先のパス
    /// @note 履歴に積み、絞り込みとカーソルを初期化して再列挙を要求する
    void NavigateFocused(const std::string& path);

    /// @brief 一覧の項目を開く。フォルダなら移動、ファイルならシェルに委ねる。
    /// @param[in] visibleIndex 開く項目。Tab::visible への添字
    /// @param[in] newTab true なら新しいタブで開く（フォルダのみ）
    /// @note 「..」行なら親フォルダへ移動する
    void ActivateEntry(int visibleIndex, bool newTab);

    /// @brief カーソルが画面内に入るようスクロール量を調整する。
    void EnsureCursorVisible();

    /// @brief ステータスバーに一時的なメッセージを出す。
    /// @param[in] message 表示する文字列
    void SetStatus(const std::string& message);

    /// @brief 失敗をステータスバーに出す。何が失敗したかは必ず言う。
    /// @param[in] key 何の操作が失敗したかを言う文字列キー（"ui.rename_failed" など）
    /// @param[in] detail OS が返した補足。空でもよい
    /// @note `SetStatus(err)` を直接呼ばないこと ─ `ErrorText()` は文言を持たない
    ///       コードに空文字列を返すので、そのまま渡すと**何も出ないまま操作だけが
    ///       失敗する**。書式は一覧のエラー行（`AppUi` の `PaintList`）と同じ
    void ReportFailure(const char* key, const std::string& detail);

    /// @brief フォーカス中のタブを再列挙する。
    void RefreshFocused();

    /// @brief 選択中の項目に対してシェルのコンテキストメニューを表示する。
    /// @param[in] screenX 表示位置の X（スクリーン座標）。負ならカーソル位置
    /// @param[in] screenY 表示位置の Y（スクリーン座標）。負ならカーソル位置
    /// @param[in] extended true なら拡張メニューを最初から出す
    /// @note 選択が空なら、代わりに表示中のフォルダの「背景」メニュー（一覧の余白を
    ///       右クリックしたときのもの）を出す
    void ShowContextMenuAt(int screenX, int screenY, bool extended);

    /// @brief 表示中のフォルダに対してシェルのコンテキストメニューを表示する。
    /// @param[in] extended true なら拡張メニューを最初から出す
    /// @note 選択の有無にかかわらずフォルダ自身が対象。出すのは「項目」メニュー
    ///       ─ 親フォルダの一覧でそのフォルダを右クリックしたときと同じ献立で、
    ///       送る・コピー・削除・7-Zip などが並ぶ。表示位置はカーソル行に合わせる
    ///       （CursorRowAnchor()）
    void ShowFolderContextMenu(bool extended);

    /// @brief 一覧の余白に対してシェルの「背景」メニューを表示する。
    /// @param[in] screenX 表示位置の X（スクリーン座標）。負ならカーソル位置
    /// @param[in] screenY 表示位置の Y（スクリーン座標）。負ならカーソル位置
    /// @param[in] extended true なら拡張メニューを最初から出す
    /// @note カーソル行や選択は見ない。余白はどの項目でもないので、答えるのは
    ///       表示中のフォルダの背景（新規作成・貼り付け）だけ
    void ShowBackgroundContextMenu(int screenX, int screenY, bool extended);

    /// @brief ブックマークの登録と解除を切り替える。
    /// @param[in] path 対象のパス
    void ToggleBookmark(const std::string& path);

    /// @brief パスがブックマーク済みかを判定する。
    /// @param[in] path 対象のパス
    /// @return 登録済みなら true
    bool HasBookmark(const std::string& path) const;

    /// @brief ドロップされたパスを転送先へコピーまたは移動する。
    /// @param[in] paths 転送するパス列
    /// @param[in] destDir 転送先ディレクトリ
    /// @param[in] move true なら移動、false ならコピー
    /// @return 実際に転送を行ったら true。不正な転送先、または移動が無意味な
    ///         場合（すでに転送先にある）は何もせず false
    bool PerformDrop(const std::vector<std::string>& paths, const std::string& destDir, bool move);

    /// @brief 転送先として妥当かを判定する。
    /// @param[in] paths 転送するパス列
    /// @param[in] destDir 転送先ディレクトリ
    /// @return 妥当なら true。自分自身、または自分のサブツリーへの転送は false
    /// @note ここだけがデータを壊しうる判定なので、OS 非依存の core に置いて
    ///       直接テストしている
    static bool IsValidDropTarget(const std::vector<std::string>& paths,
                                  const std::string& destDir);

    /// @brief 画面に出ているタブに合わせて監視を張り直す。
    /// @note 新しく監視対象になったフォルダは、見ていない間の変更が不明なので
    ///       強制的に再列挙する
    void SyncWatches();

    /// @brief 設定とワークスペースをすべて保存する。
    /// @return すべて書けたら true
    /// @note 単独ウィンドウでは何も書かずに true を返す。書けなかったファイルは
    ///       ステータス行に出るが、終了時の呼び出しでは誰にも見えない
    bool SaveAll();

    /// @brief 設定フォルダ内のファイルパスを組み立てる。
    /// @param[in] file ファイル名
    /// @return 組み立てたフルパス
    std::string ConfigPath(const char* file) const;

    /// @brief 別プロセスから渡されたパスを、このウィンドウの新しいタブで開く。
    /// @param[in] paths 開くパス列。空なら何もしない
    /// @note 単一インスタンス化（ROADMAP P1-3）の受け口。渡されるのは 2 つ目の
    ///       起動のコマンドライン引数そのものなので、`[ui] new_tab_position` には
    ///       従わず**渡された順に末尾へ並べる** ─ 起動時の引数と同じ扱い
    void OpenForwardedPaths(const std::vector<std::string>& paths);

    /// @brief 元に戻せる操作の履歴を返す。
    /// @return 履歴への参照
    const UndoStack& undoStack() const { return undo_; }

private:
    /// @brief 設定フォルダへ 1 ファイル書き、失敗をステータス行に出す。
    /// @param[in] file 設定フォルダ内のファイル名
    /// @param[in] data 書き込む内容
    /// @return 成功したら true
    /// @note 書き込み失敗を握り潰さないための唯一の入口。設定フォルダへ書くものは
    ///       すべてここを通すこと ─ 書けない場所（Program Files、読み取り専用の
    ///       メディア）に置かれたとき、黙って保存されないのが一番たちが悪い
    bool WriteConfigFile(const char* file, std::string_view data);

    void LoadConfig();
    void LoadSidebarSections();
    void LoadLanguage();
    void ApplyTheme();
    SettingsValues CollectSettings() const;
    void ApplySetting(SettingId id, const SettingsValues& values);
    int NewTabAt(const Pane& pane) const;
    void SetFontScale(float scale);
    void LoadWorkspace(const std::vector<std::string>& startPaths);
    bool SaveWorkspaceFile();
    bool SaveSettings();
    bool WriteKeysFile();
    void SaveKeysIfChanged();
    void CloseKeyEditor();
    void RefreshRoots();

    void RequestLoad(Tab& tab, bool force = false);
    void EnsureVisibleTabsLoaded();
    void RefreshTabsShowing(const std::string& dir);

    void NavigateToParent(bool newTab);
    void MoveCursor(int delta, bool extend, bool absolute = false);
    void ApplyPrompt();
    void CancelPrompt();
    void BeginPrompt(PromptKind kind, const char* labelKey, const std::string& initial);
    bool HandlePromptKey(const Chord& chord);

    /// @brief 打鍵を型入力ジャンプに渡し、当たった行へカーソルを移す。
    /// @param[in] codepoint 入力された Unicode コードポイント
    /// @return 型入力ジャンプが受け取ったら true
    bool TypeAheadChar(uint32_t codepoint);

    void SyncCompletion(bool open);
    void RequestCompletion();
    bool MoveCompletion(int delta);

    void ShowShellMenu(const std::vector<std::string>& paths, int screenX, int screenY,
                       bool extended, bool background);
    bool CursorRowAnchor(int& screenX, int& screenY);

    void UpdateTitle();

    void GotoBookmark(int index);
    void DoDelete(bool permanent);
    void DoPaste();
    void DoUndo();
    void RebuildFocused();

    /// @brief 転送先に「実際にこの操作が作ったもの」だけを拾って履歴に積む。
    /// @param[in] sources 転送元のパス列
    /// @param[in] destDir 転送先ディレクトリ
    /// @param[in] existedBefore sources と同じ長さ。転送前に転送先が在ったか
    /// @param[in] move true なら移動として積む。false ならコピー
    /// @note 競合したときシェルは別名を付けるか上書きするかで、どちらの場合も
    ///       転送先の名前は「元の名前」とは限らない。**操作の前に無く、後に在る**
    ///       ものだけが確実にこの操作の産物で、それ以外に触れれば元から在った
    ///       ファイルを消すことになる
    void RecordTransfer(const std::vector<std::string>& sources, const std::string& destDir,
                        const std::vector<bool>& existedBefore, bool move);

    fs::IFileSystem& fs_;
    IShellIntegration& shell_;
    IHost& host_;
    fs::IDirectoryWatcher* watcher_ = nullptr;
    IIconProvider* icons_ = nullptr;
    std::unordered_map<uint64_t, std::string> watched_;

    Workspace workspace_;
    KeyMap keymap_;
    KeyEditor keyEditor_;
    SettingsEditor settingsEditor_;
    BookmarkPicker bookmarkPicker_;
    Strings strings_;
    Theme theme_;
    Ini settings_;
    std::unique_ptr<fs::DirectoryLoader> loader_;

    std::vector<fs::Root> roots_;
    std::vector<fs::Root> quickAccess_;
    // 並べ替えた結果をパスで覚えたもの。ドライブも既知フォルダも列挙のたびに
    // OS の順で返ってくるので、覚えていないと次の RefreshRoots() で元に戻る。
    std::vector<std::string> quickAccessOrder_;
    std::vector<std::string> driveOrder_;

    UndoStack undo_;

    Prompt prompt_;
    PathComplete complete_;
    TypeAhead typeAhead_;
    // 直前の打鍵をコマンドが実行した、という印。TranslateMessage は
    // DispatchMessage の前に走るので、WM_KEYDOWN を消費しても WM_CHAR は止まらない
    // ─ 印が無いと、キーを割り当てた 1 文字がコマンドの実行後に型入力ジャンプへ
    // 落ちて一覧が飛ぶ。次の OnKey で必ず落とすうえ、**文字を伴う和音でしか
    // 立てない** ─ 矢印キーで立てると、印が次の打鍵まで残る。
    bool swallowChar_ = false;
    // The listing the completion is waiting on. Kept apart from the tabs' own
    // tokens so that typing past a slow folder just drops its answer.
    uint64_t completeToken_ = 0;
    std::string completeRequested_;
    bool keyHelp_ = false;
    bool keysChanged_ = false;
    bool sidebarVisible_ = true;
    std::vector<SidebarSection> sidebarSections_ = { SidebarSection::QuickAccess,
                                                     SidebarSection::Bookmarks,
                                                     SidebarSection::Drives };
    bool sidebarCollapsed_[static_cast<size_t>(SidebarSection::Count)] = {};
    float fontScale_ = 1.0f;
    bool windowActive_ = true;
    bool darkTheme_ = true;
    bool shellIcons_ = true;
    bool standalone_ = false;
    NewTabPosition newTabPosition_ = NewTabPosition::End;
    TabBarPosition tabBarPosition_ = TabBarPosition::Top;
    std::string language_ = "auto";
    ViewState defaultView_;

    std::string statusMessage_;
    uint64_t statusUntilMs_ = 0;
    std::string lastTitle_;

    WindowPlacement placement_;
    bool dirty_ = false;
};

}  // namespace kite
