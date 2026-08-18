/// @file
/// @brief 別プロセスのシェルメニューホストを起動して使う側。
///
/// `kite.exe` 側の唯一の窓口。ここから先に `IContextMenu` は現れない ─ サード
/// パーティのシェル拡張はすべて `kite_shellhost.exe` の中で動く。

#pragma once

#include <windows.h>

#include <string>
#include <vector>

#include "platform/win/ShellHostProcess.h"

namespace kite::win {

/// @brief `kite_shellhost.exe` を起動し、パイプ越しにメニューを依頼する。
///
/// ホストは最初のメニュー要求で初めて起動する。起動パスに COM もシェルも置かない
/// という方針をここでも守るため、および右クリックを一度もしない起動でホストの
/// メモリを払わないため。
///
/// ホストは要求が途切れて一定時間経つと自分で終了する。落ちた場合も同じ扱いで、
/// 次の要求で黙って起動し直す。
///
/// @note すべてのメソッドを UI スレッドから呼ぶこと
class ShellHostClient {
public:
    /// @brief 何も起動していない状態で作る。
    ShellHostClient() = default;

    ShellHostClient(const ShellHostClient&) = delete;
    ShellHostClient& operator=(const ShellHostClient&) = delete;

    /// @brief ホストにコンテキストメニューを出させ、閉じるまで待つ。
    /// @param[in] owner 呼び出し側のウィンドウ。ホストが開くダイアログの親になる
    /// @param[in] container 対象が属するフォルダの解析名。空なら `paths` を直接解析する。
    ///            **実フォルダでは必ず空にすること**（詳細は
    ///            `shellhost::Request::container`）
    /// @param[in] paths 対象のパス列。すべて同じフォルダに属している必要がある
    /// @param[in] screenX 表示位置の X（スクリーン座標）。負ならカーソル位置
    /// @param[in] screenY 表示位置の Y（スクリーン座標）。負ならカーソル位置
    /// @param[in] extended true なら拡張メニューを最初から出す
    /// @param[in] background true なら `paths` の先頭をフォルダとみなし、一覧の余白を
    ///            右クリックしたときのメニューを出す
    /// @param[in] dark true ならホストにダークテーマでメニューを描かせる
    /// @return メニューを出せたら true。ホストを起動できなかった場合、メニューを
    ///         構築できなかった場合、メニュー表示中にホストが落ちた場合は false
    /// @note メニューが閉じるまで戻らないが、待っている間も `owner` の再描画と
    ///       タイマーは動き続ける。クライアント領域への入力だけは捨てる ─
    ///       同一プロセスで出していた頃のモーダルな挙動に合わせるため
    bool ShowContextMenu(HWND owner, const std::string& container,
                         const std::vector<std::string>& paths, int screenX, int screenY,
                         bool extended, bool background, bool dark);

    /// @brief メニューを出さずに、名前で指定した動詞をホストに実行させる。
    /// @param[in] owner 呼び出し側のウィンドウ。動詞が開くダイアログの親になる
    /// @param[in] container 対象が属するフォルダの解析名
    /// @param[in] paths 対象の解析名。すべて同じフォルダに属していること
    /// @param[in] verb 動詞の名前（ごみ箱からの復元なら "undelete"）
    /// @param[in] byOriginalPath true なら `paths` を消される前のフルパスとして読む
    /// @return 実行できたら true
    /// @note メニュー用のホストを使う ─ 実体は同じ `IContextMenu` で、しかもシェルの
    ///       確認ダイアログを出しうる。列挙用のホストに混ぜると、その間フォルダが
    ///       開けなくなる
    /// @note 応答までの待ちに制限は無い。シェルが「本当に戻しますか」を出したまま
    ///       利用者が席を立てば、それだけ待つのが正しい
    bool InvokeVerb(HWND owner, const std::string& container,
                    const std::vector<std::string>& paths, const std::string& verb,
                    bool byOriginalPath);

private:
    ShellHostProcess host_;  ///< メニュー専用のホスト。アイコン用とは別インスタンス
};

}  // namespace kite::win
