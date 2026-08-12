/// @file
/// @brief プラットフォーム側のウィンドウが OS 非依存の App に提供するサービス。

#pragma once

#include <string>
#include <vector>

#include "core/fs/DirectoryLoader.h"

namespace kite {

/// @brief シェル連携のインターフェース。
///
/// 実装は platform/win/WinShell.cpp。
///
/// @note ShowContextMenu() はサードパーティのシェル拡張 DLL を動かすが、それは
///       別プロセス（kite_shellhost.exe）の中で起きる。この境界がパスと画面座標
///       しか渡さないのはそのためで、隔離の前後でインターフェースは変わっていない
class IShellIntegration {
public:
    virtual ~IShellIntegration() = default;

    /// @brief シェルのコンテキストメニューを表示し、選択された項目を実行する。
    /// @param[in] paths 対象のパス列。すべて同じフォルダに属している必要がある
    /// @param[in] screenX 表示位置の X 座標（スクリーン座標）。負ならカーソル位置
    /// @param[in] screenY 表示位置の Y 座標（スクリーン座標）。負ならカーソル位置
    /// @param[in] extended true なら Windows 11 の「その他のオプション」に相当する
    ///            拡張メニューを最初から表示する
    /// @param[in] background true なら `paths` の先頭をフォルダとみなし、その中の余白を
    ///            右クリックしたときのメニューを出す。false なら項目のメニュー。
    ///            **同じフォルダでも中身は別物**で、項目側にはそのフォルダを対象に
    ///            する動詞（「ここにクローン」など）が並ぶ
    /// @param[in] dark true ならダークテーマでメニューを描く。OS の設定ではなく
    ///            Kite 自身のテーマを渡すこと
    /// @return メニューを表示できたら true。何も選ばれずに閉じた場合も true。
    ///         メニューを出せなかった場合（別プロセスのホストを起動できない、
    ///         表示中にホストが落ちた、など）は false
    /// @note メニューが閉じるまで戻らない
    virtual bool ShowContextMenu(const std::vector<std::string>& paths, int screenX, int screenY,
                                 bool extended, bool background, bool dark) = 0;

    /// @brief 既定の関連付けでパスを開く。
    /// @param[in] path 開くファイルまたはフォルダのパス
    /// @return 成功したら true
    virtual bool Open(const std::string& path) = 0;

    /// @brief 「プログラムから開く」ダイアログを表示する。
    /// @param[in] path 対象のファイルパス
    /// @return 成功したら true
    virtual bool OpenWith(const std::string& path) = 0;

    /// @brief プロパティダイアログを表示する。
    /// @param[in] path 対象のパス
    /// @return 成功したら true
    virtual bool ShowProperties(const std::string& path) = 0;

    /// @brief エクスプローラーで対象を選択した状態で開く。
    /// @param[in] path 対象のパス
    /// @return 成功したら true
    virtual bool RevealInExplorer(const std::string& path) = 0;

    /// @brief 指定ディレクトリをカレントにしてターミナルを開く。
    /// @param[in] dir 対象のディレクトリ
    /// @return 成功したら true
    virtual bool OpenTerminal(const std::string& dir) = 0;

    /// @brief クリップボードにテキストを設定する。
    /// @param[in] utf8 設定する文字列
    /// @return 成功したら true
    virtual bool SetClipboardText(const std::string& utf8) = 0;

    /// @brief クリップボードにファイル一覧を設定する。
    /// @param[in] paths 対象のパス列
    /// @param[in] cut true なら切り取り、false ならコピーとして設定する
    /// @return 成功したら true
    virtual bool SetClipboardFiles(const std::vector<std::string>& paths, bool cut) = 0;

    /// @brief クリップボードからファイル一覧を取得する。
    /// @param[out] paths 取得したパス列の追加先
    /// @param[out] cut 切り取りなら true が入る。不要なら nullptr
    /// @return ファイルが取得できたら true。テキストしか無い場合などは false
    virtual bool GetClipboardFiles(std::vector<std::string>& paths, bool* cut) = 0;
};

/// @brief ウィンドウ自身が提供するサービス。
class IHost : public fs::IWakeSink {
public:
    ~IHost() override = default;

    /// @brief 再描画を要求する。
    virtual void Invalidate() = 0;

    /// @brief ウィンドウタイトルを設定する。
    /// @param[in] utf8 設定するタイトル文字列
    virtual void SetTitle(const std::string& utf8) = 0;

    /// @brief ウィンドウを閉じる。
    virtual void Close() = 0;

    /// @brief Kite をもう 1 つのウィンドウで開く。
    /// @param[in] dir 新しいウィンドウが最初に表示するフォルダ。空なら既定の場所
    /// @return 開けたら true。開けなければ false
    /// @note Windows 実装は別プロセスを起動する。1 プロセスに 2 つ目のウィンドウを
    ///       作るのではないのは、App も描画資源もワーカースレッドもウィンドウ 1 つ
    ///       ぶんの寿命に紐付いているため。新しいウィンドウは保存されたセッションを
    ///       読まず、終了時にも書かない（App::SetStandalone()）
    virtual bool OpenNewWindow(const std::string& dir) = 0;

    /// @brief IME の変換候補ウィンドウを出す位置を指定する。
    /// @param[in] x クライアント座標の X（DIP）
    /// @param[in] y クライアント座標の Y（DIP）
    /// @note 日本語入力が画面の隅に取り残されないようにするために必要
    virtual void SetImePosition(float x, float y) = 0;

    /// @brief マウスカーソルの形状を設定する。
    /// @param[in] shape 0=矢印、1=ハンド、2=左右リサイズ、3=上下リサイズ
    virtual void SetCursorShape(int shape) = 0;

    /// @brief クライアント座標をスクリーン座標に変換する。
    /// @param[in] x クライアント座標の X（DIP）
    /// @param[in] y クライアント座標の Y（DIP）
    /// @param[out] screenX スクリーン座標の X（ピクセル）。失敗時は書き換えない
    /// @param[out] screenY スクリーン座標の Y（ピクセル）。失敗時は書き換えない
    /// @return 変換できたら true。ウィンドウがまだ無ければ false
    /// @note キーボードから出すコンテキストメニューを、マウスカーソルではなく
    ///       カーソル行の位置に合わせるために要る。シェルに渡せるのはスクリーン
    ///       座標だけで、DIP からピクセルへの倍率を知っているのはウィンドウ側
    virtual bool ClientToScreen(float x, float y, int& screenX, int& screenY) = 0;

    /// @brief OS のドラッグ操作を開始する。
    /// @param[in] paths ドラッグするファイルのパス列
    /// @return どこかにドロップされたら true。取り消されたら false
    /// @note どのプラットフォームのドラッグ API も同様だが、ドラッグが終わるまで
    ///       ブロックする
    virtual bool BeginFileDrag(const std::vector<std::string>& paths) = 0;
};

}  // namespace kite
