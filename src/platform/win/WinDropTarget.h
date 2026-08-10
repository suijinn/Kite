/// @file
/// @brief メインウィンドウの IDropTarget 実装。

#pragma once

#include <windows.h>

#include <objidl.h>  // IDataObject; not pulled in under WIN32_LEAN_AND_MEAN
#include <oleidl.h>
#include <shlobj.h>

#include <functional>
#include <string>
#include <vector>

#include "ui/AppUi.h"

namespace kite::win {

/// @brief ドラッグを受け取り、ドロップ内容をウィンドウへ引き渡す。
///
/// 転送そのものはここで行わない。Drop() は `onDrop` に処理を渡して即座に戻る。
/// Drop() の中でファイル操作を走らせると、ドラッグ元のアプリ（エクスプローラーを
/// 含む）がコピー完了までフリーズしてしまうため。
class WinDropTarget final : public IDropTarget {
public:
    /// @brief ドロップ確定時に呼ばれるハンドラの型。
    using DropHandler =
        std::function<void(std::vector<std::string> paths, std::string destDir, bool move)>;

    /// @brief ドロップターゲットを構築する。
    /// @param[in] hwnd 対象のウィンドウ
    /// @param[in] appUi 転送先の判定と強調表示に使う UI。本オブジェクトより長生きすること
    /// @param[in] dpiScale DPI 倍率への参照。実行中に変わるので参照で保持する
    /// @param[in] onDrop ドロップ確定時に呼ぶハンドラ
    WinDropTarget(HWND hwnd, ui::AppUi& appUi, const float& dpiScale, DropHandler onDrop);

    /// @brief インターフェースを問い合わせる。
    /// @param[in] riid 要求するインターフェース ID
    /// @param[out] ppv 取得したインターフェースが入る
    /// @return 成功したら S_OK。未対応なら E_NOINTERFACE
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override;

    /// @brief 参照カウントを増やす。
    /// @return 増加後の参照カウント
    ULONG STDMETHODCALLTYPE AddRef() override;

    /// @brief 参照カウントを減らし、0 になれば自身を破棄する。
    /// @return 減少後の参照カウント
    ULONG STDMETHODCALLTYPE Release() override;

    /// @brief ドラッグがウィンドウに入ったときに呼ばれる。
    /// @param[in] data ドラッグされているデータ
    /// @param[in] keyState 修飾キーとマウスボタンの状態
    /// @param[in] point カーソル位置（スクリーン座標）
    /// @param[in,out] effect 実行予定の効果。受け入れ可否を書き戻す
    /// @return 常に S_OK
    HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* data, DWORD keyState, POINTL point,
                                        DWORD* effect) override;

    /// @brief ドラッグ中にカーソルが動くたび呼ばれる。
    /// @param[in] keyState 修飾キーとマウスボタンの状態
    /// @param[in] point カーソル位置（スクリーン座標）
    /// @param[in,out] effect 実行予定の効果。受け入れ可否を書き戻す
    /// @return 常に S_OK
    HRESULT STDMETHODCALLTYPE DragOver(DWORD keyState, POINTL point, DWORD* effect) override;

    /// @brief ドラッグがウィンドウから出たときに呼ばれる。
    /// @return 常に S_OK
    HRESULT STDMETHODCALLTYPE DragLeave() override;

    /// @brief ドロップされたときに呼ばれる。
    /// @param[in] data ドロップされたデータ
    /// @param[in] keyState 修飾キーとマウスボタンの状態
    /// @param[in] point ドロップ位置（スクリーン座標）
    /// @param[in,out] effect 実際に行った効果を書き戻す
    /// @return 常に S_OK
    HRESULT STDMETHODCALLTYPE Drop(IDataObject* data, DWORD keyState, POINTL point,
                                   DWORD* effect) override;

private:
    PointF ToClient(POINTL screen) const;
    DWORD EffectFor(DWORD keyState, const std::string& destDir) const;

    LONG refCount_ = 1;
    HWND hwnd_ = nullptr;
    ui::AppUi& ui_;
    const float& dpiScale_;
    DropHandler onDrop_;

    IDropTargetHelper* helper_ = nullptr;
    std::vector<std::string> dragPaths_;
    std::string destDir_;
};

}  // namespace kite::win
