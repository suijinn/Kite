/// @file
/// @brief 配色と寸法。すべてデータ駆動なので、テーマは ini ファイル 1 つで表せる。

#pragma once

#include <string>

#include "core/base/Ini.h"
#include "core/base/Types.h"

namespace kite {

/// @brief 画面全体の配色と寸法。
struct Theme {
    Color windowBg;   ///< ウィンドウ背景
    Color panelBg;    ///< サイドバーや各種バーの背景
    Color listBg;     ///< 一覧の背景
    Color listBgAlt;  ///< 一覧の縞模様（奇数行）の背景
    Color border;     ///< 区切り線

    Color text;        ///< 標準の文字色
    Color textDim;     ///< 補助的な文字色
    Color textFolder;  ///< フォルダ名の文字色
    Color textError;   ///< エラー表示の文字色

    Color accent;           ///< アクセント色
    Color accentText;       ///< アクセント色の上に載せる文字色
    Color rowHover;         ///< マウスが乗っている行の背景
    Color rowSelected;      ///< 選択された行の背景
    Color rowSelectedText;  ///< 選択された行の文字色
    Color cursorBorder;     ///< カーソル行の枠線
    Color paneFocusBorder;  ///< フォーカスされたペインの枠線

    Color tabActiveBg;      ///< アクティブなタブの背景
    Color tabInactiveBg;    ///< 非アクティブなタブの背景
    Color tabActiveText;    ///< アクティブなタブの文字色
    Color tabInactiveText;  ///< 非アクティブなタブの文字色
    Color sessionActiveBg;  ///< アクティブなセッションチップの背景
    Color scrollThumb;      ///< スクロールバーのつまみ
    Color scrollTrack;      ///< スクロールバーの溝
    Color overlayScrim;     ///< オーバーレイ背後を暗くする膜
    Color overlayBg;        ///< オーバーレイ本体の背景

    float rowHeight = 22.0f;         ///< 一覧 1 行の高さ（DIP）
    float headerHeight = 24.0f;      ///< 列見出しの高さ（DIP）
    float tabBarHeight = 28.0f;      ///< タブバーの高さ（DIP）
    float pathBarHeight = 26.0f;     ///< パスバーの高さ（DIP）
    float sessionBarHeight = 26.0f;  ///< セッションバーの高さ（DIP）
    float statusBarHeight = 22.0f;   ///< ステータスバーの高さ（DIP）
    float sidebarWidth = 190.0f;     ///< サイドバーの幅（DIP）
    float splitterWidth = 4.0f;      ///< ペイン分割線の幅（DIP）
    float fontSize = 13.0f;          ///< 基準フォントサイズ（DIP）
    float uiScale = 1.0f;            ///< フォントサイズ全体の倍率

    std::string fontFamily = "Yu Gothic UI";  ///< UI フォントのファミリ名
    std::string monoFamily = "Consolas";      ///< 等幅フォントのファミリ名

    bool dark = true;  ///< ダークテーマかどうか。タイトルバーの色にも使う

    /// @brief ダークテーマの既定値を返す。
    /// @return 既定のダークテーマ
    static Theme Dark();

    /// @brief ライトテーマの既定値を返す。
    /// @return 既定のライトテーマ
    static Theme Light();

    /// @brief 設定ファイルの内容を上書き適用する。
    /// @param[in] ini `[theme.dark]` / `[theme.light]` と `[ui]` を含む設定
    /// @note 読むのは dark メンバに対応する側のセクションのみ。解釈できない色は
    ///       既定値のまま残す
    void ApplyIni(const Ini& ini);
};

}  // namespace kite
