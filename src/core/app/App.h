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

#include "core/app/PlacePicker.h"
#include "core/app/CommandPalette.h"
#include "core/app/Host.h"
#include "core/app/IconProvider.h"
#include "core/app/SettingsEditor.h"
#include "core/app/UndoStack.h"
#include "core/base/Ini.h"
#include "core/fs/DirectoryLoader.h"
#include "core/fs/DirectoryWatcher.h"
#include "core/fs/FileOpQueue.h"
#include "core/fs/FileSystem.h"
#include "core/i18n/Strings.h"
#include "core/input/Commands.h"
#include "core/input/KeyEditor.h"
#include "core/input/KeyMap.h"
#include "core/input/PathComplete.h"
#include "core/input/TextField.h"
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
    ConfirmDefaultManager,   ///< 既定のファイルマネージャーの付け外しの確認
};

/// @brief 入力欄の状態。
///
/// 文字列・キャレット・選択と、その上の打鍵は `TextField` が持つ ─ チューザの
/// 絞り込み欄と同じ数え方でなければ、`Shift+←` の伸ばし方が画面ごとに違う入力欄に
/// なる。ここが足すのは «何を尋ねているか» だけ。
struct Prompt : TextField {
    PromptKind kind = PromptKind::None;      ///< 何を尋ねているか
    std::string labelKey;                    ///< 見出しの i18n キー
    std::vector<std::string> pendingPaths;   ///< 確認待ちの操作対象

    /// @brief 入力欄が表示されているかを判定する。
    /// @return 表示中なら true
    bool active() const { return kind != PromptKind::None; }

    /// @brief 文字入力ではなく Yes/No の確認かを判定する。
    /// @return 確認なら true
    bool isConfirm() const {
        return kind == PromptKind::ConfirmDelete || kind == PromptKind::ConfirmDeletePermanent ||
               kind == PromptKind::ConfirmDefaultManager;
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

/// @brief IME で変換中（未確定）の文字列。
///
/// 確定するまで Prompt::text にも絞り込み文字列にも入らない ─ 未確定の文字は
/// IME の都合で丸ごと消えうるので、入れてしまうと `Backspace` の意味も一覧の
/// 絞り込み結果も変換の途中経過で揺れる。
///
/// **描くのは Kite 自身で、IME には描かせない。** IME の変換窓は自前のフォントと
/// 行送りで文字を置くので、入力欄の中の文字と数ピクセルずれた位置に出る
/// （「確定していない状態だと文字の位置がずれる」と報告された形がこれ）。
struct Composition {
    std::string text;        ///< 変換中の文字列（UTF-8）。空なら変換中ではない
    size_t caret = 0;        ///< text 内のキャレット位置。text へのバイト添字
    size_t targetBegin = 0;  ///< 注目節の先頭。text へのバイト添字
    size_t targetEnd = 0;    ///< 注目節の終端。targetBegin と等しければ注目節なし

    /// @brief 変換中かを判定する。
    /// @return 変換中なら true
    bool active() const { return !text.empty(); }

    /// @brief 注目節（今まさに変換している節）があるかを判定する。
    /// @return あれば true
    bool hasTarget() const { return targetBegin < targetEnd && targetEnd <= text.size(); }
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

/// @brief 完了したファイル操作を何として履歴に積むか。
enum class UndoRecord : uint8_t {
    None,     ///< 積まない。取り消しの逆操作そのもの（Redo は無い）
    Fixed,    ///< 依頼したパスをそのまま積む。削除がこれ
    Created,  ///< **実際に生まれたもの**を積む。コピー・移動・複製がこれ
};

/// @brief 依頼したファイル操作について、完了時に何をするかの控え。
///
/// 操作はワーカースレッドへ渡ってしまうので、履歴も文言も一覧の取り直しも
/// 「帰ってきたときにやること」として残しておくほかない。ここが持つのは
/// **Kite 側の都合だけ**で、操作そのものの内容は `fs::FileOpRequest` の側にある。
struct PendingFileOp {
    uint64_t token = 0;                     ///< 対応する依頼のトークン
    const char* runningKey = nullptr;       ///< 実行中に出す文言キー
    const char* doneKey = nullptr;          ///< 成功時の文言キー。nullptr なら何も言わない
    const char* failKey = nullptr;          ///< 失敗時の文言キー
    bool countInDone = false;               ///< 成功時の文言の `{0}` に件数を差し込む
    UndoRecord record = UndoRecord::None;   ///< 履歴に積むか、何を積むか
    UndoKind undoKind = UndoKind::Delete;   ///< 積むときの種別
    std::vector<std::string> undoTargets;   ///< `UndoRecord::Fixed` のとき積むパス列
    bool clearCut = false;                  ///< 成功したら切り取りの印を捨てる
    std::vector<std::string> refresh;       ///< 完了時に取り直すフォルダ
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

    /// @brief IME で変換中の文字列を返す。
    /// @return 変換中の文字列。変換していなければ Composition::active() が false
    /// @note 描くのは UI 層。入力欄の文字列のキャレット位置に挿し込んだ形で見せる
    const Composition& composition() const { return composition_; }

    /// @brief IME で変換中の文字列を差し替える。
    /// @param[in] text 変換中の文字列（UTF-8）。空なら変換の終了と同じ
    /// @param[in] caret text 内のキャレット位置。バイト添字。末尾に丸める
    /// @param[in] targetBegin 注目節の先頭。バイト添字
    /// @param[in] targetEnd 注目節の終端。バイト添字。begin と等しければ注目節なし
    /// @note 入力欄が出ていないときも受け取る ─ 一覧の上（型入力ジャンプ）で変換して
    ///       いる場合で、そのときはステータス行が «変換中» として出す。IME に描かせる
    ///       道を残さない以上、どの画面にも描く先が要る
    void SetComposition(std::string text, size_t caret, size_t targetBegin, size_t targetEnd);

    /// @brief 変換中の文字列を捨てる。
    /// @note 確定した文字は OnChar() で 1 文字ずつ入る。ここは «無かったことにする» 側
    void EndComposition();

    /// @brief 文字入力を受け取る入力欄が画面に出ているかを返す。
    /// @return 出ていれば true
    /// @note プラットフォーム層が「入力欄ごと消えた変換を打ち切るか」を決めるのに使う。
    ///       削除の確認（Yes/No）と、和音を取り込んでいる最中のキー設定画面は false ─
    ///       どちらも打った «文字» ではなく打った «キー» が答えなので、入力欄ではない
    bool acceptsText() const;

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

    /// @brief 行き先の一覧（`Ctrl+P`）の状態を返す。
    /// @return 一覧への参照
    const PlacePicker& placePicker() const { return placePicker_; }

    /// @brief ブックマーク一覧の状態を返す（変更可能）。
    /// @return ブックマーク一覧への参照
    /// @note UI 層がマウス操作（行の選択、ホイール）と 1 画面の行数の通知に使う。
    ///       行を選んで実際に移動するのは App::ChoosePlace() ─ 移動は
    ///       ワークスペースを動かすので、画面の側に持たせない
    PlacePicker& placePicker() { return placePicker_; }

    /// @brief 行き先の一覧で選ばれている行へ行き、一覧を閉じる。
    /// @param[in] newTab true ならブックマークを新しいタブで開く
    /// @note UI 層が行のクリックで呼ぶ。キーボードは Enter / Ctrl+Enter で同じ道を通る
    /// @note ブックマークの行なら移動、**開いているタブの行ならそのタブへ移る**
    ///       （フォーカスもそのペインへ動く）─ 開いてあるものを選んだのだから、
    ///       同じフォルダをもう 1 枚開くのでは答えになっていない
    /// @note newTab はタブの行では無視する。すでに開いているタブに «新しいタブで» は無い
    void ChoosePlace(bool newTab);

    /// @brief コマンドパレットの状態を返す。
    /// @return パレットへの参照
    const CommandPalette& commandPalette() const { return commandPalette_; }

    /// @brief コマンドパレットの状態を返す（変更可能）。
    /// @return パレットへの参照
    /// @note UI 層がマウス操作（行の選択、ホイール）と 1 画面の行数の通知に使う。
    ///       行を選んで実際に実行するのは App::RunPaletteCommand() ─ 実行は
    ///       この画面の外の話なので、画面の側に持たせない
    CommandPalette& commandPalette() { return commandPalette_; }

    /// @brief パレットで選ばれているコマンドを実行し、パレットを閉じる。
    /// @note UI 層が行のクリックで呼ぶ。キーボードは Enter で同じ道を通る
    /// @note 閉じてから実行する ─ 実行したコマンドが入力欄を出したり別の
    ///       オーバーレイを開いたりするので、パレットが上に残っていてはならない
    void RunPaletteCommand();

    /// @brief 設定画面で変わった値を反映し、settings.ini へ書き出す。
    /// @note SettingsEditor::changed() が指す 1 項目だけを反映する。何も変わって
    ///       いなければ何もしない。UI 層がマウスで値を動かしたあとに呼ぶ
    void ApplyPendingSetting();

    /// @brief 新しいタブを作る位置を返す。
    /// @return 設定されている位置
    NewTabPosition newTabPosition() const { return newTabPosition_; }

    /// @brief 書庫（ZIP など）をフォルダとして開くかを返す。
    /// @return フォルダとして開くなら true。false なら関連付けられたアプリに渡す
    bool openArchives() const { return openArchives_; }

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

    /// @brief 行き先の一覧を開く。
    /// @return 開けたら true。行き先が 1 つも無ければ false（ステータス行で言う）
    /// @note 他のオーバーレイは閉じる。2 枚重なると、どちらが打鍵を受けるのか読めない
    bool OpenPlacePicker();

    /// @brief コマンドパレットを開く。
    /// @note 開くたびに作り直す ─ ラベルは言語で、和音は `Ctrl+Shift+,` で、番号で指す
    ///       8 個に添える行き先は `Ctrl+D` で変わる
    void OpenCommandPalette();

    /// @brief 絞り込み欄の先頭の `>` に合わせて、2 つのチューザを入れ替える。
    /// @note 絞り込み欄を書き換えた直後に必ず呼ぶ（文字入力・削除・貼り付け）。
    ///       `>` を打てばコマンドパレット、消せば行き先の一覧 ─ VS Code の
    ///       `Ctrl+P` → `>` と同じ読み方で、**入力欄は動かない**（2 つの画面は同じ
    ///       寸法で出る）
    /// @note 入力欄の中身はそのまま持ち越す。持ち越さないと、`>` を打った瞬間に
    ///       打ちかけの文字列が消える
    /// @note **混ぜるのではなくモードにする。** 1 つの一覧に行き先とコマンドを
    ///       並べると、`d` の 1 文字に `file.delete` と `Downloads` が並び、
    ///       毎日使う行き先が 124 行のコマンド表に薄められる（ROADMAP P3-12）
    void SyncPickerMode();

    /// @brief 行き先の一覧に並べる «開いているタブ» を集める。
    /// @return アクティブなセッションのタブ。今いるタブは含まない
    /// @note 今いるタブを外すのは、「ここへ行く」が行き先の答えにならないから。
    ///       他のペインのアクティブなタブは残す ─ 分割中はそれも立派な行き先
    /// @note 背面のセッションのタブは入れない。あちらへ移るのはセッションの切り替えで、
    ///       画面に出ていないペインごと入れ替わる別の操作（ROADMAP P3-12）
    std::vector<PlacePicker::OpenTab> CollectOpenTabs() const;

    /// @brief 行き先の一覧に並べる «履歴» を集める。
    /// @return フォーカス中のタブが見てきたフォルダ。**新しい順**で、重複と
    ///         今いるフォルダは落としてある
    /// @note **フォーカス中のタブの「戻る」履歴だけ**。全タブぶんを混ぜるには
    ///       «どちらが新しいか» を答えられなければならないが、履歴が持っているのは
    ///       タブごとの順序だけで時刻ではない ─ 2 本の列を並べる根拠がどこにも無い
    /// @note 「進む」履歴は入れない。あちらは «戻るで離れた先» で、`Alt+→` が
    ///       すでに指している ─ 最近見た順の列に混ぜると、その順序が嘘になる
    /// @note 件数には上限がある（App.cpp の `kHistoryRows`）。履歴は «最近» のための
    ///       行なので、古い側まで並べるとブックマークとドライブを画面外へ押し出す
    std::vector<PlacePicker::Visited> CollectHistory() const;

    /// @brief タブを引き抜いて新しいウィンドウで開く。
    /// @param[in] pane タブを持っているペイン。nullptr なら何もしない
    /// @param[in] index 引き抜くタブの添字
    /// @return 引き抜けたら true。開けなかった・引き抜けない最後の 1 枚なら false
    /// @note UI 層はタブをウィンドウの外へ落としたときにここを呼ぶ。新しい
    ///       ウィンドウは別プロセスなので渡るのはフォルダだけで、履歴も表示状態も
    ///       残らない（`Cmd::NewWindow` と同じ道を通る）
    /// @note その 1 枚しか無いウィンドウでは断る ─ 引き抜いた先が今の
    ///       ウィンドウと同じものになるだけで、元の窓は空にできない
    bool DetachTabToNewWindow(Pane* pane, int index);

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

    /// @brief その項目が「切り取り」でクリップボードに入っているかを返す。
    /// @param[in] path 対象のパス
    /// @return 切り取り済みなら true
    /// @note UI が行を薄く描くために毎フレーム呼ぶ。クリップボードは入っている
    ///       ことを何も言わないので、`Ctrl+X` を押したことが画面に残るのはここだけ
    bool IsCut(const std::string& path) const;

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
    /// @note フォルダを指すショートカット（.lnk）はフォルダとして扱い、Kite の中で
    ///       移動する ─ シェルに渡すとリンク先がエクスプローラーで開く
    void ActivateEntry(int visibleIndex, bool newTab);

    /// @brief カーソルが画面内に入るようスクロール量を調整する。
    void EnsureCursorVisible();

    /// @brief ステータスバーに一時的なメッセージを出す。
    /// @param[in] message 表示する文字列
    void SetStatus(const std::string& message);

    /// @brief タブ見出しに出す短い名前を返す。
    /// @param[in] tab 対象のタブ
    /// @return 表示名。仮想フォルダなら Kite の言語での名前
    /// @note `Tab::title()` を UI から直接呼ばないこと。「PC」「ごみ箱」
    ///       「ネットワーク」の名前は i18n の側にあり、タブは言語を知らない
    std::string DisplayName(const Tab& tab) const;

    /// @brief パスだけを持つ行き先の表示名を返す。
    /// @param[in] p 対象のパス
    /// @return 表示名。仮想フォルダなら Kite の言語での名前
    /// @note `DisplayName(const Tab&)` と別なのは、タブには列挙が持ち帰った表示名
    ///       （`fs::ListResult::title`）が在るから ─ 履歴の 1 行はパスしか持たない
    std::string DisplayNameOf(const std::string& p) const;

    /// @brief タイトルバーに出す、読める形のパスを返す。
    /// @param[in] tab 対象のタブ
    /// @return 実フォルダならパスそのもの。仮想フォルダなら表示名
    /// @note 仮想フォルダのパス（`virtual:` 付きのシェル解析名）は、読ませても
    ///       打たせても意味を持たない文字列
    /// @note 書庫の中だけは例外で、EditablePath() と同じ実パスの綴りを返す ─
    ///       「PC」と違い、書庫には「その中のどこか」が在る
    std::string DisplayPath(const Tab& tab) const;

    /// @brief アドレスバーに入れる、打ち返せる形のパスを返す。
    /// @param[in] tab 対象のタブ
    /// @return 書庫の中なら `virtual:` を外した実パスの綴り。それ以外はパスそのもの
    /// @note 出したものがそのまま打ち返される欄なので、**戻ってこられる綴りしか
    ///       出さない。** 書庫の中はシェルと同じ綴りが通る（`App::ArchiveTarget`
    ///       が読み替える）が、「PC」などは識別子のほうが唯一の道
    std::string EditablePath(const Tab& tab) const;

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

    /// @brief 実行中または処理待ちのファイル操作があるかを返す。
    /// @return 削除・コピー・移動・複製のいずれかが片付いていなければ true
    /// @note 操作そのものはワーカースレッドで走る。結果が届くのは PumpLoader()
    ///       の中なので、依頼した直後に一覧やディスクを見ても答えは出ていない
    bool fileOpsBusy() const;

    /// @brief 実行中のファイル操作について、ステータス行に出す文言を返す。
    /// @return 1 件なら「コピーしています…」等、2 件以上なら件数。待っているものが
    ///         あればそれも添える。何も走っていなければ空
    /// @note 期限で消えない ─ 数分かかる操作の間、その旨だけは出続けていなければ
    ///       「押したのに何も起きていない」と見分けが付かない
    /// @note **覚えずに毎回組み立てる。** 走り始めるのはワーカーの都合なので、
    ///       依頼した瞬間に作った文字列は «まだ待機中» のまま古くなる（画面を
    ///       描き直させるほうは `IWakeSink::Wake()` が受け持つ）
    std::string fileOpStatus() const;

private:
    /// @brief 設定フォルダへ 1 ファイル書き、失敗をステータス行に出す。
    /// @param[in] file 設定フォルダ内のファイル名
    /// @param[in] data 書き込む内容
    /// @return 成功したら true
    /// @note 書き込み失敗を握り潰さないための唯一の入口。設定フォルダへ書くものは
    ///       すべてここを通すこと ─ 書けない場所（Program Files、読み取り専用の
    ///       メディア）に置かれたとき、黙って保存されないのが一番たちが悪い
    bool WriteConfigFile(const char* file, std::string_view data);

    /// @brief 切り取りの印を捨てる。
    /// @note 印はコピー・切り取りのやり直し、貼り付け、`Escape`、そしてクリップ
    ///       ボードが別のものに変わったときに消える
    void ClearCutMarks();

    /// @brief 切り取りの印がまだクリップボードと一致しているかを確かめる。
    /// @note ウィンドウがフォーカスを取り戻したときに呼ぶ。その間に別のアプリが
    ///       コピーしていれば、薄いままの行は嘘になる。読めなかったときは触らない
    ///       ─ 一時的にクリップボードを掴まれているだけのことがある
    void SyncCutMarks();

    /// @brief フォルダを指すショートカット（.lnk）かを判定し、リンク先を返す。
    /// @param[in] path 対象のパス
    /// @param[out] target リンク先のフォルダ。false を返すときは書き換えない
    /// @return リンク先が実在するフォルダなら true
    /// @note 拡張子で先に振り分ける ─ 解決には COM とファイル読み取りと存在確認が
    ///       要るので、ただのファイルを開くたびに払う代金ではない
    /// @note ファイルを指すショートカットは false。プログラムを名指していることが
    ///       あり、それを起動するのはまさにシェルが .lnk に対して行う仕事
    bool ShortcutFolder(const std::string& path, std::string& target);

    /// @brief 書庫をフォルダとして開くパスに読み替える。
    /// @param[in] path 開こうとしているパス
    /// @return 書庫なら `virtual:` を付けたパス、そうでなければ `path` のまま
    /// @note 移動の入口すべて（一覧の Enter・アドレスバー・OpenPath）が通る。
    ///       `[ui] open_archives` が false なら何もしない
    /// @note 拡張子が合ったときだけ実在を確かめる ─ `.lnk` と同じ順序で、
    ///       ふつうのフォルダへ移動するのに問い合わせを 1 つも増やさない
    std::string ArchiveTarget(const std::string& path);

    /// @brief コマンドラインで渡された 1 つを、開けるパスに読み替える。
    /// @param[in] arg 受け取った引数そのもの
    /// @return 開くべきパス
    /// @note **起動時の引数も、2 つ目の起動から転送されてきた引数もここを通る。**
    ///       解析名を仮想パスに直し（`vfs::FromCommandLine`）、綴りを畳み、書庫なら
    ///       中を開く形にする ─ 3 段の順序を 1 か所に置くためにある
    /// @note ここを通さなかったせいで、`kite.exe pack.zip` が書庫の中ではなく
    ///       「ディレクトリ名が無効です」になっていた。**一覧の Enter やアドレス
    ///       バーと同じ答えを返すこと**が守るべき形で、入口ごとに違う答えを持つ
    ///       のなら、それは同じアプリではない
    std::string CommandLinePath(const std::string& arg);

    /// @brief 表示中の場所が Kite 自身の書き込みを拒むかを判定する。
    /// @return 仮想フォルダ（書庫の中を含む）なら true。そのときステータス行に
    ///         理由を出す
    /// @note 作成・名前の変更・削除・切り取り・貼り付けの入口すべてが先にこれを
    ///       呼ぶ。仮想フォルダの項目が持つのは操作の相手になるパスではない
    ///       （消したファイルなら隠された $R の写し）ので、動くように見えて
    ///       別のものを壊す
    bool ReadOnlyHere();

    /// @brief 選択中のごみ箱の項目を元の場所へ戻す。
    /// @note ごみ箱を表示していないタブでは何もせずその旨を出す。取り消し履歴には
    ///       積まない ─ 「元に戻す」を元に戻すのは「消す」であって、`Ctrl+Z` で
    ///       救い出したファイルがごみ箱へ帰るのは誰も望んでいない
    void DoRestore();

    /// @brief 既定のファイルマネージャーの付け外しに確認を求める。
    /// @param[in] on true で登録、false で解除
    /// @note **入口は設定画面の行 1 つだけ。** 行は «今どうなっているか» を値として
    ///       出せるので、コマンド 2 つ（ラベルが半分の確率で嘘になる）よりも、
    ///       トグルのコマンド 1 つ（状態を言えない）よりも正直になる
    /// @note **設定画面で唯一、確認を挟む行。** 他の行は変えたことがその場で見える
    ///       が、この行の効果は Kite の外（`Win+E` とフォルダのダブルクリック）に
    ///       出るので、矢印キーがかすっただけで変わってはならない
    /// @note 登録の口が無い環境では確認を出さずに理由を出す
    void ConfirmDefaultManager(bool on);

    /// @brief 確認を経て、既定のファイルマネージャーとしての登録を付け外しする。
    /// @param[in] on true で登録、false で解除
    /// @note すでにその状態なら何も書かずに理由を出す。画面に出ていない状態なので、
    ///       黙って何もしないと «キーが効かなかった» と見分けが付かない
    /// @note `DefaultManager::Other`（別の場所の Kite が登録済み）は「すでに登録
    ///       されている」ではない ─ そこへ付け替えるのがこの行の仕事
    void SetDefaultManager(bool on);

    /// @brief 設定画面の行の値を、OS の実際の状態から引き直す。
    /// @note 確認を取り消した後・OS が断った後に呼ぶ。行が動かした値と実際とが
    ///       食い違ったままだと、画面が嘘をつく
    void SyncDefaultManagerRow();

    /// @brief シェルメニューに渡す「項目が属するフォルダ」を返す。
    /// @return 仮想フォルダを表示中ならそのパス。実フォルダなら空文字列
    /// @note 実フォルダで空を返すのは性能の話ではなく前提の話 ─ 渡すとシェルは
    ///       そのフォルダを列挙して項目を突き合わせる（`IShellIntegration::
    ///       ShowContextMenu`）
    std::string ShellMenuContainer();

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

    /// @brief 表示中のオーバーレイをすべて閉じる。
    /// @note オーバーレイは同時に 1 枚しか出さないので、開く側は必ずこれを先に
    ///       呼ぶ。1 枚ずつ閉じて回ると、5 枚目を足した日に閉じ忘れが生まれる
    void CloseAllOverlays();

    /// @brief タブの表示先を差し替え、読み込みを要求する。
    /// @param[in,out] tab 対象のタブ
    /// @param[in] path 新しいパス
    /// @note 履歴は触らない ─ 「戻る」も「他のペインで開く」も同じ差し替えを
    ///       するが、履歴に積むかどうかは呼ぶ側の話
    void RetargetTab(Tab& tab, const std::string& path);

    /// @brief ペインにタブを 1 枚足し、表示状態を与えて読み込みを要求する。
    /// @param[in,out] pane 足す先のペイン
    /// @param[in] path 開くパス
    /// @param[in] at 挿す位置。-1 で末尾
    /// @param[in] view 与える表示状態（新しいタブなら `defaultView_`、複製なら元の値）
    /// @return 足したタブ。`Pane::AddTab` が返すものをそのまま返す
    Tab* OpenTabIn(Pane& pane, const std::string& path, int at, const ViewState& view);

    /// @brief 表示状態の真偽値を 1 つ反転し、以後の新しいタブにも引き継ぐ。
    /// @param[in,out] tab 対象のタブ。nullptr なら何もしない
    /// @param[in] flag 反転する `ViewState` のメンバ
    /// @note 隠しファイル・並び順・フォルダ優先の 3 つが同じ手順を踏むので 1 つに
    ///       してある ─ 片方だけ `defaultView_` を更新し忘れる形の不具合を防ぐ
    void ToggleViewFlag(Tab* tab, bool ViewState::* flag);

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

    /// @brief 同じフォルダの中に複製を作る。
    /// @param[in] sources 複製する項目のパス列。すべて自分の親フォルダに残る
    /// @param[in] clearCut 成功したら切り取りの印を捨てるか
    /// @return 依頼できたら true。空いている名前を見つけられなければ false
    /// @note 名前を選ぶところまでが同期で、コピーそのものは FileOpQueue に渡る
    ///       ─ 件数と文言（`ui.duplicated`）が決まるのは完了したとき
    /// @note 同一フォルダへの貼り付け（およびドロップ）の答え。同じ名前は
    ///       置けないので `a.txt` → `a_copy.txt` と名前を付け直す
    ///       （`path::DuplicateName` / `path::kCopySuffix`）
    /// @note 名前は Kite が決めてから `IFileSystem::CopyAs()` に渡す。シェルに
    ///       解決させると出来上がった名前が誰にも分からず、取り消し履歴が
    ///       「この操作が作ったもの」を指せなくなる
    bool DuplicateInPlace(const std::vector<std::string>& sources, bool clearCut);
    void DoUndo();
    void RebuildFocused();

    /// @brief ファイル操作を依頼し、完了時に何をするかを控える。
    /// @param[in] request ワーカーに渡す依頼
    /// @param[in,out] plan 完了時の振る舞い。トークンはここで埋める
    /// @note 依頼した時点では何も起きていない ─ 履歴も文言も一覧の取り直しも、
    ///       すべて FinishFileOp() の仕事
    void QueueFileOp(fs::FileOpRequest request, PendingFileOp plan);

    /// @brief 完了したファイル操作を回収し、控えてあった後始末を行う。
    /// @note PumpLoader() から呼ばれる
    void PumpFileOps();

    /// @brief 完了した 1 件について、履歴・文言・一覧の取り直しを行う。
    /// @param[in] done ワーカーが返した結果
    /// @param[in] plan 依頼時に控えた振る舞い
    void FinishFileOp(const fs::FileOpDone& done, const PendingFileOp& plan);

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
    PlacePicker placePicker_;
    CommandPalette commandPalette_;
    Strings strings_;
    Theme theme_;
    Ini settings_;
    std::unique_ptr<fs::DirectoryLoader> loader_;
    // 削除・コピー・移動はここを通る。UI スレッドで `SHFileOperation` を呼ぶと
    // 終わるまでウィンドウがメッセージを 1 つも処理できない ─ 大きなコピーの間
    // 「使えなくなる」と報告された形がこれ。
    std::unique_ptr<fs::FileOpQueue> fileOps_;
    std::vector<PendingFileOp> pendingOps_;

    std::vector<fs::Root> roots_;
    std::vector<fs::Root> quickAccess_;
    // 並べ替えた結果をパスで覚えたもの。ドライブも既知フォルダも列挙のたびに
    // OS の順で返ってくるので、覚えていないと次の RefreshRoots() で元に戻る。
    std::vector<std::string> quickAccessOrder_;
    std::vector<std::string> driveOrder_;

    UndoStack undo_;

    Prompt prompt_;
    Composition composition_;
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
    // 「切り取り」でクリップボードに入れた項目。エクスプローラーと同じく、その行を
    // 薄く描くためだけに覚えている ─ クリップボードは自分が何を持っているかを
    // 知らせてこないので、覚えていなければ画面は何も言えない
    std::vector<std::string> cutPaths_;
    bool shellIcons_ = true;
    // ZIP を「開く」と言われたときの答え。true なら中を一覧に出し、false なら
    // 関連付けられたアプリ（展開ソフト）に渡す
    bool openArchives_ = true;
    bool standalone_ = false;
    // 確認待ちの «既定のファイルマネージャーにするか»。行が動かした値をそのまま
    // 読み直せない ─ 確認の間に行のカーソルは動きうるし、取り消せば値は戻る
    bool pendingDefaultManager_ = false;
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
