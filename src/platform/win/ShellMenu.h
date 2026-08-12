/// @file
/// @brief シェルのコンテキストメニューの構築・表示・実行。
///
/// このファイルは `kite_shellhost.exe` にのみリンクされる。サードパーティの
/// `IContextMenu` ハンドラ（Box、Google Drive、7-Zip、TortoiseGit …）が動くのは
/// ここだけであり、それらが落ちてもホストのプロセスが 1 つ消えるだけで済む。
/// **kite.exe に足さないこと。** 隔離の意味が無くなる。

#pragma once

#include <windows.h>

#include <string>
#include <vector>

#include "platform/win/ShellHostProtocol.h"

namespace kite::win {

/// @brief シェルのコンテキストメニューを表示し、選ばれた項目を実行する。
/// @param[in] menuOwner メニューを追跡するウィンドウ。**呼び出しスレッドが所有する
///            こと**（TrackPopupMenuEx の要求）。オーナードローの通知はここに届くので、
///            そのウィンドウプロシージャは ForwardContextMenuMessage() を呼ぶこと
/// @param[in] dialogOwner 実行したコマンドが開くダイアログの親にするウィンドウ。
///            別プロセスの Kite 本体を渡してよい。nullptr なら menuOwner を使う
/// @param[in] paths 対象のパス列。すべて同じフォルダに属している必要がある
/// @param[in] screenX 表示位置の X（スクリーン座標）。負ならカーソル位置
/// @param[in] screenY 表示位置の Y（スクリーン座標）。負ならカーソル位置
/// @param[in] extended true なら CMF_EXTENDEDVERBS 付きで構築する
/// @param[in] background true なら `paths` の先頭 1 件をフォルダとみなし、その「背景」
///            メニュー（`IShellFolder::CreateViewObject`）を出す。false なら項目の
///            メニュー（`IShellFolder::GetUIObjectOf`）
/// @return 選ばれた項目を実行したら Invoked、何も選ばれなければ None、
///         メニューを構築できなければ Failed
/// @pre COM が初期化済みであること
/// @note シェル拡張への呼び出しはすべて SEH で囲んである。拡張がフォールトしても
///       失われるのはそのメニューだけで、ホストは次の要求を処理できる
shellhost::Result ShowShellContextMenu(HWND menuOwner, HWND dialogOwner,
                                       const std::vector<std::string>& paths, int screenX,
                                       int screenY, bool extended, bool background);

/// @brief 以降に作るメニューをダーク／ライトのどちらで描かせるかを指定する。
/// @param[in] menuOwner メニューを追跡するウィンドウ
/// @param[in] dark true ならダーク、false ならライト
/// @return 指定を適用できたら true。未対応の Windows では何もせず false
/// @note 実体は uxtheme.dll の非公開エクスポート（序数 104 / 133 / 135 / 136）。
///       序数の意味が変わった 1903 より前のビルドでは、誤った関数を呼ばないよう
///       何もしない。呼ぶのは ShowShellContextMenu() より前で、同じ値なら
///       2 回目以降は素通りする
/// @todo オーナードローの項目を出す拡張（TortoiseGit など）は自前の配色を使うため、
///       この指定に従わない
bool SetShellMenuDarkMode(HWND menuOwner, bool dark);

/// @brief オーナードローのシェルメニューに必要なメッセージを中継する。
/// @param[in] message メッセージ ID
/// @param[in] wparam メッセージの WPARAM
/// @param[in] lparam メッセージの LPARAM
/// @param[out] result 処理した場合の戻り値が入る
/// @return 処理したら true。その場合 `result` をそのまま返すこと
/// @note メニューを追跡するウィンドウのプロシージャの先頭で呼ぶこと。
///       メニューが出ていない間は常に false を返す
bool ForwardContextMenuMessage(UINT message, WPARAM wparam, LPARAM lparam, LRESULT* result);

}  // namespace kite::win
