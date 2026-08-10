/// @file
/// @brief 別プロセスのホストにシェルアイコンを取ってこさせる側。
///
/// `kite.exe` 側でアイコンに触れる唯一の窓口。ここから先に `SHGetFileInfo` は
/// 現れない ─ オーバーレイハンドラ（TortoiseGit、OneDrive、Dropbox …）は
/// すべて `kite_shellhost.exe` の中で動く。

#pragma once

#include <windows.h>

#include <string>
#include <vector>

#include "platform/win/ShellHostProcess.h"
#include "platform/win/ShellHostProtocol.h"

namespace kite::win {

/// @brief アイコン取得専用のホスト接続。
///
/// メニュー用のホストとは別インスタンスを使う。メニューのホストは表示中ずっと
/// `TrackPopupMenu` の中にいるので、同じ接続を共有するとメニューが開いている間
/// アイコンが 1 枚も更新されなくなる。
///
/// @note ワーカースレッド専用。UI スレッドから呼んではならない
class IconHostClient {
public:
    /// @brief 何も起動していない状態で作る。
    IconHostClient() = default;

    IconHostClient(const IconHostClient&) = delete;
    IconHostClient& operator=(const IconHostClient&) = delete;

    /// @brief 一括でアイコンを取得する。
    /// @param[in] paths 取得したいパス列。応答は同じ順に並ぶ
    /// @param[in] pixelSize 希望する 1 辺のピクセル数
    /// @param[out] response 取得結果
    /// @return 取得できたら true。ホストを起動できない、応答が時間内に来ない、
    ///         表示中にホストが落ちた場合は false
    /// @note 応答待ちには時間制限がある。**これが別プロセスにしている最大の理由**で、
    ///       `SHGetFileInfo` 自体は中断もタイムアウトもできない。オーバーレイ
    ///       ハンドラが返ってこない場合、失うのはそのバッチとホスト 1 つで済む
    bool Fetch(const std::vector<std::string>& paths, uint32_t pixelSize,
               shellhost::IconResponse& response);

    /// @brief ホストを終了させる。
    /// @note 次の Fetch() で黙って起動し直す
    void Stop() { host_.Stop(); }

private:
    ShellHostProcess host_;
};

}  // namespace kite::win
