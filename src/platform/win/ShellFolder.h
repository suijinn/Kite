/// @file
/// @brief シェル名前空間のフォルダを列挙する。
///
/// このファイルは `kite_shellhost.exe` にのみリンクされる。`IShellFolder` の
/// 列挙は**名前空間拡張（シェル名前空間フォルダ）の DLL を呼び出し元のプロセスに
/// 読み込ませる** ─ 「PC」を開くだけで iCloud や各種クラウドの拡張が起き上がる
/// のを実際に確認している。`ShellMenu.cpp` `ShellIcons.cpp` と同じ理由で
/// **kite.exe に足さないこと。**
///
/// 隔離のもう 1 つの効能は待ち時間のほう。名前空間拡張の列挙には中断の手段が
/// 無いので、返ってこない相手に当たったらプロセスごと捨てるしかない。

#pragma once

#include <windows.h>

#include <shtypes.h>

#include <string>
#include <vector>

#include "platform/win/ShellHostProtocol.h"

namespace kite::win {

/// @brief シェル名前空間のフォルダ 1 つ分を列挙する。
/// @param[in] parsingName 列挙する場所の解析名（`::{CLSID}` 形式など、UTF-8）
/// @return 列挙結果。失敗時は status に理由が入り entries は空
/// @pre COM が初期化済みであること
/// @note 列挙が途中で切れたら**集めた分ごと捨てる**。半分の一覧を全部として
///       出すと「残りは消えた」と読めてしまう（`WinFileSystem::List` が
///       `FindNextFileW` に対してしているのと同じ）
/// @note 呼び出しは SEH で囲んである。名前空間拡張がフォールトしても、失われる
///       のはその 1 回の列挙とホストプロセスだけ
shellhost::FolderResponse EnumerateShellFolder(const std::string& parsingName);

/// @brief フォルダの中から、解析名の一致する項目の絶対 PIDL を集める。
/// @param[in] container 探すフォルダの解析名（UTF-8）
/// @param[in] parsingNames 探す項目の解析名（UTF-8）
/// @return 見つかった項目の絶対 PIDL。呼び出し側が `CoTaskMemFree` すること。
///         `container` を開けなければ空
/// @pre COM が初期化済みであること
/// @note **`SHParseDisplayName` で代用できない場所があるためにある。** ごみ箱の
///       項目の解析名は隠された `$R` の写しのパスなので、解析するとごみ箱の項目
///       ではなくただのファイルが返る ─ そうして得た PIDL の親はごみ箱ではないので、
///       `IContextMenu` に「元に戻す」は現れない
/// @note 一致は解析名の**完全一致**（大文字小文字は畳む）。フォルダを 1 回列挙する
///       ので、実フォルダの項目にこれを使ってはならない
std::vector<PIDLIST_ABSOLUTE> ResolveItemsInFolder(const std::string& container,
                                                   const std::vector<std::string>& parsingNames);

/// @brief ごみ箱の中から、消される前のパスが一致する項目の絶対 PIDL を集める。
/// @param[in] container ごみ箱の解析名（UTF-8）
/// @param[in] originalPaths 消される前のフルパス（UTF-8）
/// @return 見つかった項目の絶対 PIDL。呼び出し側が `CoTaskMemFree` すること
/// @pre COM が初期化済みであること
/// @note `Ctrl+Z` で削除を戻すためにある。削除した時点で分かっているのは元のパスの
///       ほうで、ごみ箱に入った後の `$R` の名前は誰も見ていない
/// @note 元のパスは「元の場所」（「元の場所」（`PID_DISPLACED_FROM`））と項目名から組み立てる。
///       **項目名は拡張子を欠くことがある**（エクスプローラーの「拡張子を表示しない」
///       設定に従うため）ので、`元の場所 + 名前` に拡張子 1 つ分を足した形も
///       一致とみなす
/// @note **同じパスの項目が複数あれば、最後に消したものを採る**
///       （「削除日時」（`PID_DISPLACED_DATE`））。`Ctrl+Z` が指しているのは常に直前の削除
std::vector<PIDLIST_ABSOLUTE> ResolveTrashItemsByOrigin(
    const std::string& container, const std::vector<std::string>& originalPaths);

}  // namespace kite::win
