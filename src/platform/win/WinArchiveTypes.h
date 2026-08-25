/// @file
/// @brief この Windows が「フォルダとして開ける」書庫の拡張子を、レジストリに訊く。
///
/// core が持っているのは規則だけで（`core/fs/VirtualPath.h`）、どの拡張子が実際に
/// 開けるのかを知っているのはここだけ。**決め打ちにできない** ─ 同じ Kite が動く
/// 先で答えが違うからで、tar を gzip した書庫は Windows 11 なら開けて Windows 10 では
/// 開けず、7z は 7-Zip を入れて関連付けた時点でシェル名前空間から外れる。
///
/// 訊くのはレジストリの綴りだけで、**シェル拡張の DLL は 1 つも読み込まない** ─
/// 名前空間の実装を起こす経路は今までどおり `kite_shellhost.exe` の中だけにある。

#pragma once

#include <string>
#include <vector>

namespace kite::win {

/// @brief シェルがフォルダとして開ける拡張子を列挙する。
/// @return 先頭のドットを含まない拡張子（小文字）。1 つも確かめられなければ空
/// @note そのまま `vfs::SetArchiveExtensions()` に渡す。**空が返ったら渡さないこと**
///       ─ 「開けるものが 1 つも無い」ではなく「訊けなかった」なので、core の既定
///       （zip・cab）をそのまま生かすほうが正しい
/// @note 判定は「関連付けられた ProgID がシェル名前空間の CLSID を名乗るか」。
///       iso（マウントする）や msi（インストーラ）は ProgID を持っていても名前空間
///       には現れないので、この 1 段で自然に落ちる
/// @note **二重拡張子は末尾の `gz` として通る。** 拡張子は末尾 1 つしか見ないが、
///       それを開けるシェルは中の tar もそのまま展開して見せる（実測）
std::vector<std::string> ShellFolderExtensions();

}  // namespace kite::win
