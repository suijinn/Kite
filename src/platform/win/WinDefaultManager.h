/// @file
/// @brief 既定のファイルマネージャーとしての登録（`Win+E` とフォルダを開く操作）。
///
/// 「エクスプローラーを置き換える」という目標のうち、Kite 自身では動かせなかった最後の
/// 一片。ここが書き換えるのは `HKCU\Software\Classes` の 3 つのキーだけで、**昇格は
/// 一切求めない** ─ ファイラーを 1 つ入れるために管理者権限を要求するのは、そのファイラーが
/// 何をするかまだ知らない人にとって最悪の第一印象になる（ROADMAP P2-11）。
///
/// **付けたものは全部外せる。** 書き換える前の状態（キーがそもそも在ったか、在ったなら
/// 既定値と `DelegateExecute` は何だったか）を `HKCU\Software\Kite\DefaultManagerBackup`
/// に控えてから書く。解除はその控えを読んで元の形に戻す ─ 戻せなければ、Kite を消した
/// あとフォルダが 1 つも開かない Windows が残る。
///
/// @note core からは `IShellIntegration::DefaultManagerState()` /
///       `SetDefaultManager()` として見える。レジストリの綴りを知っているのはこの
///       ファイルだけで、`core` は「登録されているか」しか受け取らない

#pragma once

#include "core/app/Host.h"

namespace kite::win {

/// @brief 既定のファイルマネージャーとしての登録状態を調べる。
/// @return 登録状態。読み取りに失敗したときは DefaultManager::No
/// @note 判定に使うのは代表の 1 キー（フォルダを開く動詞）だけ。3 つは必ず一緒に
///       書かれ一緒に消されるので、1 つ読めば残りも同じことを言う
/// @note **登録されているのが Kite 以外なら DefaultManager::No。** それは事故では
///       なく利用者の選択で、起動のたびに「Kite は既定ではありません」と言うのは
///       ただの押し売りになる。`Other` を返すのは、**同じ名前の exe が別の場所から**
///       登録されているとき ─ ポータブル運用で zip を展開し直した後の形がこれで、
///       登録は死んでいるのに画面はそれを言う機会を持たない
DefaultManager DefaultManagerState();

/// @brief 既定のファイルマネージャーとしての登録を付け外しする。
/// @param[in] on true で登録、false で解除
/// @return 書き換えられたら true
/// @note 途中で失敗したら、そこまでに書いたぶんを控えから戻してから false を返す ─
///       半分だけ書き換わったレジストリは、どちらの状態でもない
/// @note 控え（`DefaultManagerBackup`）が既に在れば取り直さない。**控えが指すのは
///       «Kite が触る前» の状態**で、登録済みの状態から取り直すと Kite 自身が書いた
///       値を「元の値」として覚えることになる
/// @note 書き換えたら `SHChangeNotify(SHCNE_ASSOCCHANGED)` を投げる。投げないと、
///       次のログオンまで古い関連付けのまま動き続ける
bool SetDefaultManager(bool on);

}  // namespace kite::win
