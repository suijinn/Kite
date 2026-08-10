/// @file
/// @brief Windows のシェル連携。

#pragma once

#include <windows.h>

// WIN32_LEAN_AND_MEAN が ole2.h を windows.h から外すため、受け渡しする COM
// インターフェースは自前で取り込む必要がある。
#include <objidl.h>

#include <string>
#include <vector>

#include "core/app/Host.h"

namespace kite::win {

/// @brief Windows 版のシェル連携。
///
/// Kite が外部のコードを読み込む唯一の場所。メニュー表示中は Box、Google Drive、
/// 7-Zip などの IContextMenu ハンドラが自プロセス内で動く。呼び出しはすべて SEH で
/// 囲ってあるので、拡張がフォールトしても失われるのはメニューだけで済む。
///
/// @todo メニュー処理全体を別プロセス（kite_shellhost.exe）へ移す。
///       docs/ROADMAP.md の P1-1
class WinShell final : public IShellIntegration {
public:
    /// @brief 所有ウィンドウを設定する。
    /// @param[in] hwnd メニューやダイアログの親にするウィンドウ
    void SetWindow(HWND hwnd) { hwnd_ = hwnd; }

    /// @copydoc IShellIntegration::ShowContextMenu
    void ShowContextMenu(const std::vector<std::string>& paths, int screenX, int screenY,
                         bool extended) override;

    /// @copydoc IShellIntegration::Open
    bool Open(const std::string& path) override;

    /// @copydoc IShellIntegration::OpenWith
    bool OpenWith(const std::string& path) override;

    /// @copydoc IShellIntegration::ShowProperties
    bool ShowProperties(const std::string& path) override;

    /// @copydoc IShellIntegration::RevealInExplorer
    bool RevealInExplorer(const std::string& path) override;

    /// @copydoc IShellIntegration::OpenTerminal
    /// @note Windows Terminal があればそれを、無ければ cmd.exe を起動する
    bool OpenTerminal(const std::string& dir) override;

    /// @copydoc IShellIntegration::SetClipboardText
    bool SetClipboardText(const std::string& utf8) override;

    /// @copydoc IShellIntegration::SetClipboardFiles
    bool SetClipboardFiles(const std::vector<std::string>& paths, bool cut) override;

    /// @copydoc IShellIntegration::GetClipboardFiles
    bool GetClipboardFiles(std::vector<std::string>& paths, bool* cut) override;

private:
    HWND hwnd_ = nullptr;
};

/// @brief オーナードローのシェルメニューに必要なメッセージを中継する。
/// @param[in] hwnd メッセージを受け取ったウィンドウ
/// @param[in] message メッセージ ID
/// @param[in] wparam メッセージの WPARAM
/// @param[in] lparam メッセージの LPARAM
/// @param[out] result 処理した場合の戻り値が入る
/// @return 処理したら true。その場合 `result` をそのまま返すこと
/// @note ウィンドウプロシージャの先頭で呼ぶこと
bool ForwardContextMenuMessage(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam,
                               LRESULT* result);

/// @brief OLE を初期化する。
/// @return 初期化済みなら true
/// @note 意図的に遅延させている。メニューもドラッグも使わない起動ではこのコストを
///       払わない
bool EnsureOle();

/// @brief パス列からシェルの IDataObject を作る。
/// @param[in] paths 対象のパス列。すべて同じフォルダに属している必要がある
/// @return 生成した IDataObject。呼び出し側が Release() する。失敗時は nullptr
/// @note 自前で CF_HDROP を組まずシェルに作らせることで、エクスプローラーや
///       アーカイバへのドロップがエクスプローラー発のドラッグと同じ挙動になる
IDataObject* CreateShellDataObject(const std::vector<std::string>& paths);

/// @brief ドロップされたデータオブジェクトからファイル一覧を取り出す。
/// @param[in] data 取り出し元のデータオブジェクト。nullptr でも安全
/// @return 取り出したパス列。CF_HDROP を持たない場合は空
std::vector<std::string> ExtractDroppedPaths(IDataObject* data);

}  // namespace kite::win
