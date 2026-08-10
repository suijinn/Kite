/// @file
/// @brief 別プロセスのシェルメニューホストを起動して使う側。
///
/// `kite.exe` 側の唯一の窓口。ここから先に `IContextMenu` は現れない ─ サード
/// パーティのシェル拡張はすべて `kite_shellhost.exe` の中で動く。

#pragma once

#include <windows.h>

#include <string>
#include <vector>

namespace kite::win {

/// @brief `kite_shellhost.exe` を起動し、パイプ越しにメニューを依頼する。
///
/// ホストは最初のメニュー要求で初めて起動する。起動パスに COM もシェルも置かない
/// という方針をここでも守るため、および右クリックを一度もしない起動でホストの
/// メモリを払わないため。
///
/// ホストは要求が途切れて一定時間経つと自分で終了する。落ちた場合も同じ扱いで、
/// 次の要求で黙って起動し直す。
///
/// @note すべてのメソッドを UI スレッドから呼ぶこと
class ShellHostClient {
public:
    /// @brief 何も起動していない状態で作る。
    ShellHostClient() = default;

    /// @brief ホストを終了させて後片付けする。
    ~ShellHostClient();

    ShellHostClient(const ShellHostClient&) = delete;
    ShellHostClient& operator=(const ShellHostClient&) = delete;

    /// @brief ホストにコンテキストメニューを出させ、閉じるまで待つ。
    /// @param[in] owner 呼び出し側のウィンドウ。ホストが開くダイアログの親になる
    /// @param[in] paths 対象のパス列。すべて同じフォルダに属している必要がある
    /// @param[in] screenX 表示位置の X（スクリーン座標）。負ならカーソル位置
    /// @param[in] screenY 表示位置の Y（スクリーン座標）。負ならカーソル位置
    /// @param[in] extended true なら拡張メニューを最初から出す
    /// @return メニューを出せたら true。ホストを起動できなかった場合、メニューを
    ///         構築できなかった場合、メニュー表示中にホストが落ちた場合は false
    /// @note メニューが閉じるまで戻らないが、待っている間も `owner` の再描画と
    ///       タイマーは動き続ける。クライアント領域への入力だけは捨てる ─
    ///       同一プロセスで出していた頃のモーダルな挙動に合わせるため
    bool ShowContextMenu(HWND owner, const std::vector<std::string>& paths, int screenX,
                         int screenY, bool extended);

private:
    /// @brief ホストが動いていることを保証する。必要なら起動する。
    /// @param[in] owner 接続を待つ間も再描画を続けるウィンドウ。nullptr でもよい
    /// @return 使えるホストがあれば true
    bool EnsureHost(HWND owner);

    /// @brief パイプを閉じ、ホストの終了を待つ（必要なら強制終了する）。
    void CloseHost();

    /// @brief ホストの実行ファイルのパスを組み立てる。
    /// @return `kite.exe` と同じフォルダの `kite_shellhost.exe` のパス。
    ///         自分のパスが取れなければ空文字列
    static std::wstring HostExecutablePath();

    HANDLE pipe_ = INVALID_HANDLE_VALUE;  ///< 名前付きパイプのサーバー側
    HANDLE process_ = nullptr;            ///< ホストのプロセスハンドル
    HANDLE job_ = nullptr;                ///< ホストを道連れにするためのジョブ
    DWORD processId_ = 0;                 ///< ホストのプロセス ID
    unsigned generation_ = 0;             ///< パイプ名を毎回変えるための連番
    bool unavailable_ = false;            ///< 実行ファイルが無いと分かった状態
};

}  // namespace kite::win
