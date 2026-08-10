/// @file
/// @brief kite.exe と kite_shellhost.exe を結ぶパイプの、フレーム単位の送受信。
///
/// 呼び出し側から見れば同期的だが、待っている間も `pump` を通じてメッセージを
/// 処理できる。これが必要なのは両側とも事情がある:
///
/// - kite.exe 側はメニューが閉じるまで待つ。ユーザーがメニューを開いたまま放置
///   すれば数分になりうるので、その間ウィンドウが凍ると「応答なし」になる。
/// - kite_shellhost.exe 側は次の要求まで待つ。最上位ウィンドウを持つプロセスが
///   メッセージを回さないと、`WM_SETTINGCHANGE` のような**システム全体への
///   ブロードキャストを送った側**がタイムアウトまで待たされる。

#pragma once

#include <windows.h>

#include <cstdint>
#include <vector>

namespace kite::win {

/// @brief 転送の結果。
enum class PipeStatus {
    Ok,       ///< フレームを 1 つ送受信できた
    Timeout,  ///< 制限時間内に届かなかった
    Closed,   ///< 相手が終了した、または書式が壊れていた
    Aborted,  ///< `pump` が false を返して打ち切った
};

/// @brief 待機中に呼ばれるコールバック。
/// @param[in] context 呼び出し側が渡した値がそのまま来る
/// @return 待機を続けるなら true。打ち切るなら false
/// @note メッセージが到着したときと、一定間隔ごとに呼ばれる。ここで送受信中の
///       パイプに触ってはならない
using PipePump = bool (*)(void* context);

/// @brief フレームを 1 つ送る。全バイト書き切るまで待つ。
/// @param[in] pipe 送信先のパイプ。FILE_FLAG_OVERLAPPED 付きで開いていること
/// @param[in] frame 送るバイト列。kite::shellhost::Frame() が作ったもの
/// @param[in] pump 待機中に呼ぶコールバック。nullptr でもよい
/// @param[in] context `pump` に渡す値
/// @return 書き切れたら Ok
PipeStatus WritePipeFrame(HANDLE pipe, const std::vector<uint8_t>& frame, PipePump pump,
                          void* context);

/// @brief フレームを 1 つ受け取る。
/// @param[in] pipe 受信元のパイプ。FILE_FLAG_OVERLAPPED 付きで開いていること
/// @param[out] payload フレーム本体（ヘッダを除いた部分）が入る
/// @param[in] timeoutMs 制限時間。待ち続けるなら INFINITE
/// @param[in] pump 待機中に呼ぶコールバック。nullptr でもよい
/// @param[in] context `pump` に渡す値
/// @return 受け取れたら Ok。相手が落ちた場合は Closed
/// @note 相手プロセスが死ぬとパイプが壊れ、待機中の読み取りが Closed で返る。
///       そのため相手のプロセスハンドルを別に監視する必要はない
PipeStatus ReadPipeFrame(HANDLE pipe, std::vector<uint8_t>& payload, DWORD timeoutMs, PipePump pump,
                         void* context);

/// @brief 名前付きパイプに相手が接続するのを待つ。
/// @param[in] pipe CreateNamedPipeW が返したサーバー側ハンドル
/// @param[in] timeoutMs 制限時間
/// @param[in] pump 待機中に呼ぶコールバック。nullptr でもよい
/// @param[in] context `pump` に渡す値
/// @return 接続されたら Ok
PipeStatus WaitForPipeClient(HANDLE pipe, DWORD timeoutMs, PipePump pump, void* context);

}  // namespace kite::win
