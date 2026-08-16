/// @file
/// @brief 単一インスタンス化。既に動いている Kite にパスを渡す。
///
/// エクスプローラーの代わりに使う以上、フォルダを開くたびにウィンドウが増えては困る。
/// 2 つ目の起動は、既存のウィンドウへパスを送って自分は黙って終わる。
///
/// **`--new-window` が付いた起動だけはここを通らない**（`WinWindow::kNewWindowFlag`）。
/// 単一インスタンス化が入った今、あれが「本当に新しい窓が要る」と言える唯一の手段。
///
/// 目印はすべて **exe のフルパスから導く**。固定名にすると、別のフォルダへ展開した
/// 2 つのコピーが同じインスタンスとして振る舞い、一方の `config` だけが使われる
/// （CLAUDE.md「設定の置き場所」）。

#pragma once

#include <windows.h>

#include <string>
#include <vector>

namespace kite::win {

/// @brief 渡されたパスを運ぶ `WM_COPYDATA` の識別子。
///
/// 他所から飛んできた `WM_COPYDATA` を取り違えないための印。中身は改行区切りの
/// UTF-8 のパス列で、Windows ヘッダを持たない側からも読めるよう文字列のまま送る。
constexpr ULONG_PTR kForwardPathsId = 0x4B697465;  // 'Kite'

/// @brief ウィンドウクラス名を返す。
/// @param[in] primary 単一インスタンスの受け口になるウィンドウなら true。
///            `--new-window` で開いた単独ウィンドウなら false
/// @return クラス名。exe のフルパスから導いた値が末尾に付く
/// @note ウィンドウの登録と、既存インスタンスの探索の両方がこれを使う。名前を
///       exe ごとに変えてあるので、探索は `FindWindowW` 1 回で済み、他所に展開した
///       別のコピーを掴むこともない。
///
///       **単独ウィンドウには別の名前を使う。** 同じ名前だと探索がそちらを先に
///       掴みうるが、単独ウィンドウはワークスペースを保存しない ─ 頼んだフォルダが
///       «閉じれば消えるタブ» として開くことになる
const wchar_t* InstanceClassName(bool primary);

/// @brief 単一インスタンス用のミューテックスを確保する。
/// @param[out] alreadyRunning 同じ exe のインスタンスが既に居れば true が入る
/// @return ミューテックスのハンドル。失敗したら nullptr。プロセス終了まで保持すること
/// @note 名前空間は `Local\` ─ ユーザーのセッションごとに別のインスタンスになる。
///       ハンドルを閉じるとインスタンスの印も消えるので、プロセスの最後まで持つこと
HANDLE AcquireInstanceMutex(bool& alreadyRunning);

/// @brief 既存インスタンスのメインウィンドウを探す。
/// @param[in] timeoutMs 見つかるまで待つ上限（ミリ秒）
/// @return ウィンドウハンドル。見つからなければ nullptr
/// @note ミューテックスが在るのにウィンドウがまだ無い、という隙間がある ─ 先の
///       プロセスが起動の途中なら、ミューテックスのほうが先にできている。だから
///       1 回引いて諦めるのではなく、少しのあいだ待つ
HWND FindExistingWindow(unsigned timeoutMs);

/// @brief 既存インスタンスにパスを渡し、そのウィンドウを前面に出す。
/// @param[in] target 既存インスタンスのウィンドウ
/// @param[in] paths 開かせるパス列。空なら前面に出すだけ
/// @return 渡せたら true。相手が応答しなければ false
/// @note 応答しない相手を待ち続けないよう `SendMessageTimeoutW` を使う。false が
///       返ったときは、呼び出し側が自分でウィンドウを開くこと ─ ダブルクリックに
///       何も起きないのが一番悪い
bool ForwardPaths(HWND target, const std::vector<std::string>& paths);

/// @brief `WM_COPYDATA` で届いた本体をパス列に戻す。
/// @param[in] payload 受け取ったバイト列。改行区切りの UTF-8
/// @param[in] bytes payload の長さ
/// @return パス列。空の行は落とす
std::vector<std::string> ParseForwardedPaths(const void* payload, size_t bytes);

}  // namespace kite::win
