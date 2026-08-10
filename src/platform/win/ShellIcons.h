/// @file
/// @brief シェルアイコンの取得。オーバーレイ込み。
///
/// このファイルは `kite_shellhost.exe` にのみリンクされる。`SHGFI_ADDOVERLAYS` は
/// アイコンオーバーレイハンドラ（TortoiseGit の `TortoiseOverlays.dll`、OneDrive、
/// Dropbox …）をシェルに読み込ませる ─ つまりサードパーティの DLL が呼び出し元の
/// プロセスに常駐する。`ShellMenu.cpp` と同じ理由で **kite.exe に足さないこと。**
///
/// 隔離のもう 1 つの効能はこちら側にある。`SHGetFileInfo` は中断もタイムアウトも
/// できないので、ハンドラが返ってこないとそのスレッドは永久に止まる。別プロセスで
/// あれば、呼び出し側はパイプ読み取りに時間制限を掛けて、ホストごと捨てられる。

#pragma once

#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

namespace kite::win {

/// @brief 取り出したアイコンの画素。
///
/// 並びは**乗算済みアルファの BGRA**、上から下へ。Direct2D がそのまま受け取れる
/// 形式に、ここで揃えている。
struct IconBitmap {
    uint32_t width = 0;         ///< 幅（ピクセル）
    uint32_t height = 0;        ///< 高さ（ピクセル）
    std::vector<uint8_t> bgra;  ///< 画素。width * height * 4 バイト
};

/// @brief パスに対応するシェルアイコンを、オーバーレイを合成して取り出す。
/// @param[in] path 対象のパス（UTF-8）
/// @param[in] pixelSize 希望する 1 辺のピクセル数。実際の寸法は `out` が持つ
/// @param[out] out 取り出した画素
/// @return 取り出せたら true
/// @pre COM が初期化済みであること
/// @note 呼び出しは SEH で囲んである。オーバーレイハンドラがフォールトしても
///       失われるのはそのアイコン 1 枚だけで、ホストは次の要求を処理できる
/// @note ファイルの内容は読まない。クラウドのプレースホルダに対しても
///       ダウンロードは走らない（属性とオーバーレイの判定しか行われない）
bool LoadShellIcon(const std::string& path, uint32_t pixelSize, IconBitmap& out);

}  // namespace kite::win
