/// @file
/// @brief `kite_shellhost.exe` を 1 つ起動して、名前付きパイプで繋いだ状態を保つ。
///
/// メニュー用とアイコン用で 2 つ起動するため、起動・接続・後始末をここにまとめて
/// ある。メニューのホストは `TrackPopupMenu` の中でメニューが閉じるまで戻らない
/// ので、アイコンがその後ろに並ばされないよう別インスタンスにしている。

#pragma once

#include <windows.h>

#include <string>

#include "platform/win/ShellPipe.h"

namespace kite::win {

/// @brief ホストプロセス 1 つ分の起動と寿命管理。
///
/// ホストは最初の要求まで起動しない。起動パスに COM もシェルも置かないという
/// 方針をここでも守るため、および右クリックもアイコン表示も無い起動でホストの
/// メモリを払わないため。
///
/// ホストは要求が途切れて一定時間経つと自分で終了する。落ちた場合も同じ扱いで、
/// 次の要求で黙って起動し直す。
///
/// @note 1 つのインスタンスを複数スレッドから同時に使ってはならない
class ShellHostProcess {
public:
    /// @brief 何も起動していない状態で作る。
    ShellHostProcess() = default;

    /// @brief ホストを終了させて後片付けする。
    ~ShellHostProcess();

    ShellHostProcess(const ShellHostProcess&) = delete;
    ShellHostProcess& operator=(const ShellHostProcess&) = delete;

    /// @brief ホストが動いていることを保証する。必要なら起動して接続を待つ。
    /// @param[in] pump 接続待ちの間に呼ぶコールバック。nullptr でもよい
    /// @param[in] context `pump` に渡す値
    /// @return 使えるホストがあれば true
    /// @note 実行ファイルが見つからないと分かった時点で以後の試行を諦める。
    ///       置かれていないファイルは 2 回の要求の間に現れないため
    bool Ensure(PipePump pump, void* context);

    /// @brief パイプを閉じ、ホストの終了を待つ（必要なら強制終了する）。
    void Stop();

    /// @brief 接続済みのパイプを返す。
    /// @return パイプのハンドル。未起動なら INVALID_HANDLE_VALUE
    HANDLE pipe() const { return pipe_; }

    /// @brief ホストのプロセス ID を返す。
    /// @return プロセス ID。未起動なら 0
    DWORD processId() const { return processId_; }

    /// @brief 現在の接続の世代番号を返す。
    /// @return 起動のたびに 1 ずつ増える番号。一度も起動していなければ 0
    /// @note ホストが持つ状態（アイコンの識別子表など）は接続と一緒に消えるので、
    ///       呼び出し側はこの値が変わったら自分が覚えている識別子を捨てること
    unsigned generation() const { return generation_; }

private:
    /// @brief ホストの実行ファイルのパスを組み立てる。
    /// @return `kite.exe` と同じフォルダの `kite_shellhost.exe` のパス。
    ///         自分のパスが取れなければ空文字列
    static std::wstring ExecutablePath();

    HANDLE pipe_ = INVALID_HANDLE_VALUE;  ///< 名前付きパイプのサーバー側
    HANDLE process_ = nullptr;            ///< ホストのプロセスハンドル
    HANDLE job_ = nullptr;                ///< ホストを道連れにするためのジョブ
    DWORD processId_ = 0;                 ///< ホストのプロセス ID
    unsigned generation_ = 0;             ///< パイプ名を毎回変えるための連番
    bool unavailable_ = false;            ///< 実行ファイルが無いと分かった状態
};

}  // namespace kite::win
