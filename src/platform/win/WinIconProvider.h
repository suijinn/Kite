/// @file
/// @brief Windows 版のシェルアイコン取得。

#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "core/app/IconProvider.h"
#include "core/fs/DirectoryLoader.h"
#include "platform/win/D2DRenderer.h"
#include "platform/win/IconHostClient.h"

namespace kite::win {

/// @brief 別プロセスのホストからシェルアイコンを取ってくる IIconProvider 実装。
///
/// 流れは列挙（fs::DirectoryLoader）と同じ形をしている。UI スレッドが要求を積み、
/// ワーカーが取りに行き、`IHost::Wake()` を経て UI スレッドが回収する。違うのは
/// 回収の最後にビットマップをレンダラへ渡すところだけで、そのため取り込みは
/// 描画の直前に行う。
///
/// 画素は取り込んだ後も保持する。Direct2D のデバイスが失われるとビットマップは
/// 消えるが、パイプに聞き直さず入れ直せる（1 枚 4 KB 程度で、実際に保持するのは
/// 見た目が異なるものだけ）。
class WinIconProvider final : public IIconProvider {
public:
    /// @brief ワーカースレッドを起動する。
    /// @param[in] wake 取得完了の通知先。本オブジェクトより長生きすること
    explicit WinIconProvider(fs::IWakeSink& wake);

    /// @brief ワーカースレッドの停止を待って破棄する。
    ~WinIconProvider() override;

    WinIconProvider(const WinIconProvider&) = delete;
    WinIconProvider& operator=(const WinIconProvider&) = delete;

    /// @copydoc IIconProvider::IconFor
    uint32_t IconFor(const std::string& path) override;

    /// @brief 要求の送出と結果の取り込みを行う。
    /// @param[in,out] renderer ビットマップの渡し先
    /// @note 描画の直前に UI スレッドから呼ぶこと
    void Pump(D2DRenderer& renderer);

    /// @brief アイコンの 1 辺のピクセル数を設定する。
    /// @param[in] pixels 希望する寸法。行の高さと DPI から決める
    /// @note 変えると取得済みのアイコンはすべて捨てて取り直す
    void SetPixelSize(uint32_t pixels);

private:
    void WorkerMain();

    fs::IWakeSink& wake_;
    IconCache cache_;
    IconHostClient client_;

    std::mutex mutex_;
    std::condition_variable cv_;
    std::vector<std::string> queue_;                    ///< ワーカー待ちのパス
    std::vector<std::string> inFlight_;                 ///< ワーカーが処理中のパス
    std::vector<std::pair<std::string, uint32_t>> done_;///< 取得できた対応
    std::vector<shellhost::IconImage> images_;          ///< 未アップロードの画素
    bool stop_ = false;
    bool failed_ = false;                               ///< 直前のバッチが失敗した

    std::unordered_map<uint32_t, shellhost::IconImage> pixels_;  ///< 再アップロード用
    uint32_t pixelSize_ = 16;
    std::atomic<uint32_t> requestedSize_{ 16 };
    std::thread worker_;
};

}  // namespace kite::win
