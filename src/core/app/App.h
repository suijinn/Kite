/// @file
/// @brief コントローラ。全状態を保持し、全コマンドを実行する。
///
/// ピクセルのことも Win32 のことも知らない。UI 層がここから読み取り、イベントを
/// 流し込む。プラットフォーム層がファイルシステム・シェル・ウィンドウを提供する。

#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/app/Host.h"
#include "core/app/IconProvider.h"
#include "core/base/Ini.h"
#include "core/fs/DirectoryLoader.h"
#include "core/fs/DirectoryWatcher.h"
#include "core/fs/FileSystem.h"
#include "core/i18n/Strings.h"
#include "core/input/Commands.h"
#include "core/input/KeyEditor.h"
#include "core/input/KeyMap.h"
#include "core/model/Workspace.h"
#include "core/theme/Theme.h"

namespace kite {

/// @brief 画面下部の入力欄が何を尋ねているか。
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
    std::vector<std::string> pendingPaths;   ///< 確認待ちの操作対象

    /// @brief 入力欄が表示されているかを判定する。
    /// @return 表示中なら true
    bool active() const { return kind != PromptKind::None; }

    /// @brief 文字入力ではなく Yes/No の確認かを判定する。
    /// @return 確認なら true
    bool isConfirm() const {
        return kind == PromptKind::ConfirmDelete || kind == PromptKind::ConfirmDeletePermanent;
    }
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

    /// @brief 設定とワークスペースを読み込み、最初の列挙を開始する。
    /// @param[in] startPaths コマンドラインで指定されたパス。追加タブとして開く
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

    /// @brief ショートカット一覧が表示中かを返す。
    /// @return 表示中なら true
    bool keyHelpVisible() const { return keyHelp_; }

    /// @brief ショートカットキー設定画面の状態を返す。
    /// @return 設定画面への参照
    const KeyEditor& keyEditor() const { return keyEditor_; }

    /// @brief ショートカットキー設定画面の状態を返す（変更可能）。
    /// @return 設定画面への参照
    /// @note UI 層がマウス操作（行の選択、取り込みの開始）と 1 画面の行数の通知に
    ///       使う。割り当てを変える操作は App::OnKey を通るので、keys.ini への
    ///       保存はそちらが行う
    KeyEditor& keyEditor() { return keyEditor_; }

    /// @brief サイドバーが表示中かを返す。
    /// @return 表示中なら true
    bool sidebarVisible() const { return sidebarVisible_; }

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

    /// @brief フォーカス中のタブを再列挙する。
    void RefreshFocused();

    /// @brief 選択中の項目に対してシェルのコンテキストメニューを表示する。
    /// @param[in] screenX 表示位置の X（スクリーン座標）。負ならカーソル位置
    /// @param[in] screenY 表示位置の Y（スクリーン座標）。負ならカーソル位置
    /// @param[in] extended true なら拡張メニューを最初から出す
    /// @note 選択が空なら、代わりに表示中のフォルダ自身を対象にする
    void ShowContextMenuAt(int screenX, int screenY, bool extended);

    /// @brief 表示中のフォルダに対してシェルのコンテキストメニューを表示する。
    /// @param[in] extended true なら拡張メニューを最初から出す
    /// @note 選択の有無にかかわらずフォルダ自身が対象。表示位置はカーソル行に
    ///       合わせる（CursorRowAnchor()）
    void ShowFolderContextMenu(bool extended);

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
    void SaveAll();

    /// @brief 設定フォルダ内のファイルパスを組み立てる。
    /// @param[in] file ファイル名
    /// @return 組み立てたフルパス
    std::string ConfigPath(const char* file) const;

private:
    void LoadConfig();
    void LoadWorkspace(const std::vector<std::string>& startPaths);
    void SaveWorkspaceFile();
    void SaveSettings();
    void WriteKeysFile();
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

    void ShowShellMenu(const std::vector<std::string>& paths, int screenX, int screenY,
                       bool extended);
    bool CursorRowAnchor(int& screenX, int& screenY);

    void GotoTab(int index);
    void GotoSession(int index);
    void GotoBookmark(int index);
    void DoDelete(bool permanent);
    void DoPaste();
    void RebuildFocused();

    fs::IFileSystem& fs_;
    IShellIntegration& shell_;
    IHost& host_;
    fs::IDirectoryWatcher* watcher_ = nullptr;
    IIconProvider* icons_ = nullptr;
    std::unordered_map<uint64_t, std::string> watched_;

    Workspace workspace_;
    KeyMap keymap_;
    KeyEditor keyEditor_;
    Strings strings_;
    Theme theme_;
    Ini settings_;
    std::unique_ptr<fs::DirectoryLoader> loader_;

    std::vector<fs::Root> roots_;
    std::vector<fs::Root> quickAccess_;

    Prompt prompt_;
    bool keyHelp_ = false;
    bool keysChanged_ = false;
    bool sidebarVisible_ = true;
    bool darkTheme_ = true;
    bool shellIcons_ = true;
    std::string language_ = "auto";
    ViewState defaultView_;

    std::string statusMessage_;
    uint64_t statusUntilMs_ = 0;
    std::string lastTitle_;

    WindowPlacement placement_;
    bool dirty_ = false;
};

}  // namespace kite
