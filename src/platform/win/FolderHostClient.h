/// @file
/// @brief 別プロセスのホストに仮想フォルダを列挙させる側。
///
/// `kite.exe` 側でシェル名前空間に触れる唯一の窓口。ここから先に `IShellFolder`
/// は現れない ─ 名前空間拡張（iCloud、各種クラウド、ZIP …）はすべて
/// `kite_shellhost.exe` の中で動く。

#pragma once

#include <mutex>
#include <string>

#include "platform/win/ShellHostProcess.h"
#include "platform/win/ShellHostProtocol.h"

namespace kite::win {

/// @brief 列挙専用のホスト接続。
///
/// メニュー用ともアイコン用とも別インスタンスを使う。メニューのホストは表示中
/// ずっと `TrackPopupMenu` の中にいるので、共有すると**メニューを開いている間
/// フォルダが 1 つも開けなくなる**。アイコン用と分けているのは
/// `SHGetFileInfo` が最大 8 秒待たされうるためで、一覧の到着がその後ろに並ぶ
/// 理由が無い。
///
/// @note `DirectoryLoader` のワーカーは複数あるので、内部で直列化している。
///       仮想フォルダの列挙は数えるほどしか起きないうえ、シェル名前空間の
///       列挙は速い ─ 待たされるのは実 FS の一覧ではなく、もう 1 枚の仮想
///       フォルダを同時に開いたときだけ
class FolderHostClient {
public:
    /// @brief 何も起動していない状態で作る。
    FolderHostClient() = default;

    FolderHostClient(const FolderHostClient&) = delete;
    FolderHostClient& operator=(const FolderHostClient&) = delete;

    /// @brief 仮想フォルダ 1 つ分を列挙する。
    /// @param[in] parsingName 列挙する場所のシェル解析名（`virtual:` は外した形）
    /// @param[out] response 列挙結果
    /// @return 取得できたら true。ホストを起動できない、応答が時間内に来ない、
    ///         列挙中にホストが落ちた場合は false
    /// @note 応答待ちには時間制限がある。名前空間拡張の列挙は中断できないので、
    ///       返ってこない相手に対してできるのはホストごと捨てることだけ
    /// @note ワーカースレッド専用。UI スレッドから呼んではならない
    bool List(const std::string& parsingName, shellhost::FolderResponse& response);

    /// @brief ホストを終了させる。
    /// @note 次の List() で黙って起動し直す
    void Stop();

private:
    std::mutex mutex_;  ///< 複数のローダーワーカーが 1 本の接続を取り合わないように
    ShellHostProcess host_;
};

}  // namespace kite::win
