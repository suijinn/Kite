/// @file
/// @brief Win32 ウィンドウ。メッセージポンプ、入力変換、IHost の実装。

#pragma once

#include <windows.h>

#include <memory>
#include <string>
#include <vector>

#include "core/app/App.h"
#include "core/app/Host.h"
#include "platform/win/D2DRenderer.h"
#include "platform/win/WinDropTarget.h"
#include "platform/win/WinIconProvider.h"
#include "ui/AppUi.h"

namespace kite::win {

/// @brief 2 枚目以降のウィンドウであることを表すコマンドライン引数。
///
/// これが付いた起動だけが「単独ウィンドウ」（App::SetStandalone()）になり、同時に
/// **単一インスタンスの検出を通らない唯一の起動**でもある（WinSingleInstance.h）。
/// パスだけを渡す起動は既存ウィンドウの新しいタブになるので、「本当に新しい窓が
/// 要る」と言える手段はこれだけ。
constexpr char kNewWindowFlag[] = "--new-window";

/// @brief メインウィンドウ。
///
/// タブやブックマークが何であるかは一切知らない。OS のイベントを UI 層の型に
/// 変換し、コントローラが必要とするウィンドウ機能を提供するだけ。
class WinWindow final : public IHost {
public:
    /// @brief 空のウィンドウオブジェクトを作る。
    WinWindow();

    /// @brief 破棄する。
    ~WinWindow() override;

    /// @brief コントローラと UI を結び付ける。
    /// @param[in] app コントローラ。本オブジェクトより長生きすること
    /// @param[in] appUi UI。本オブジェクトより長生きすること
    /// @note Create() より前に呼ぶこと。ウィンドウ作成時にテーマを参照する
    void Attach(App* app, ui::AppUi* appUi);

    /// @brief ウィンドウを作成して表示する。
    /// @param[in] placement 復元する位置とサイズ
    /// @return 成功したら true。ウィンドウまたは描画資源の作成に失敗したら false
    bool Create(const WindowPlacement& placement);

    /// @brief メッセージループを回す。
    /// @return WM_QUIT の終了コード
    int Run();

    /// @brief ウィンドウハンドルを返す。
    /// @return ハンドル。未作成なら nullptr
    HWND handle() const { return hwnd_; }

    /// @copydoc IHost::Invalidate
    void Invalidate() override;

    /// @copydoc IHost::SetTitle
    void SetTitle(const std::string& utf8) override;

    /// @copydoc IHost::Close
    void Close() override;

    /// @copydoc IHost::OpenNewWindow
    /// @note 自分と同じ exe を `--new-window` 付きで起動する。ジョブには入れない
    ///       ─ 開いたウィンドウは開いた側より長生きしてよい
    bool OpenNewWindow(const std::string& dir) override;

    /// @copydoc IHost::SetCursorShape
    void SetCursorShape(int shape) override;

    /// @copydoc IHost::ClientToScreen
    bool ClientToScreen(float x, float y, int& screenX, int& screenY) override;

    /// @copydoc IHost::BeginFileDrag
    bool BeginFileDrag(const std::vector<std::string>& paths) override;

    /// @brief シェルアイコンの取得口を結び付ける。
    /// @param[in] icons 取得口。nullptr なら何もしない。ウィンドウより長生きすること
    /// @note 描画の直前に取り込みと要求を回す。行の高さと DPI から寸法も決める
    void SetIconProvider(WinIconProvider* icons) { icons_ = icons; }

    /// @copydoc fs::IWakeSink::Wake
    /// @note ローダーと監視のワーカースレッドから呼ばれる
    void Wake() override;

private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
    LRESULT Handle(UINT message, WPARAM wparam, LPARAM lparam);

    void Paint();
    void DispatchMouse(ui::MouseEvent::Type type, int button, int clicks, WPARAM wparam,
                       LPARAM lparam, bool screenCoords = false);
    void ApplyDarkTitleBar();
    void UpdateImeWindow();
    void ReadComposition();
    void CommitComposition();
    void CancelComposition();
    void SavePlacement();
    void EnableDragAndDrop();
    void RunPendingDrop();

    HWND hwnd_ = nullptr;
    App* app_ = nullptr;
    ui::AppUi* ui_ = nullptr;
    WinIconProvider* icons_ = nullptr;
    D2DRenderer renderer_;

    float dpiScale_ = 1.0f;
    bool rendererReady_ = false;
    bool closing_ = false;
    int titleBarDark_ = -1;  ///< タイトルバーに適用済みのテーマ。-1 は未適用
    std::wstring title_;     ///< 最後に指定されたタイトル。ウィンドウ作成前の指定も保持する
    uint32_t highSurrogate_ = 0;
    /// キャレットが立っている場所。IME の窓（変換窓・候補一覧）はここを基準に置く。
    RectF imeCaret_{};
    /// 変換中か。未確定の文字列を描くのは常に Kite 自身なので、IME の窓は出ていない
    bool composing_ = false;
    /// その変換が入力欄の中で始まったか。欄が畳まれたら道連れにするかの判断に使う
    bool compositionInField_ = false;
    int cursorShape_ = 0;

    /// WM_MOUSELEAVE を予約済みか。TrackMouseEvent は 1 回で 1 通しか送らない
    bool mouseTracked_ = false;

    // Drag & drop. OLE is initialized after the first frame so it never sits on
    // the startup path.
    WinDropTarget* dropTarget_ = nullptr;
    bool dragDropScheduled_ = false;
    bool dragInProgress_ = false;

    struct PendingDrop {
        std::vector<std::string> paths;
        std::string destDir;
        bool move = false;
        bool valid = false;
    } pendingDrop_;
};

}  // namespace kite::win
