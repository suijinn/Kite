/// @file
/// @brief ファイルシステムの変更通知。
///
/// Kite は個々の変更レコードを一覧へ適用しようとはしない。通知は「このフォルダは
/// 古くなった」という意味しか持たず、タブは通常の非同期経路で列挙し直す。
/// モデルが単純になるうえ、通知が信頼できない・まとめられてしまう環境
/// （ネットワーク共有、クラウド同期フォルダ）でも正しく動く。
///
/// デバウンスはバックエンド側で行う。ファイルコピー中は毎秒数百件のレコードが
/// 出るため、そのつど再列挙するのは監視しないより悪い。

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace kite::fs {

/// @brief 「このフォルダが変化した」という 1 件の通知。
struct ChangeEvent {
    uint64_t watchId = 0;  ///< 対応する監視の識別子
    std::string path;      ///< 変化したディレクトリのパス
};

/// @brief ディレクトリ監視のインターフェース。
class IDirectoryWatcher {
public:
    virtual ~IDirectoryWatcher() = default;

    /// @brief 監視を開始する。同じ識別子の既存監視は置き換える。
    /// @param[in] watchId 監視を識別する値。呼び出し側が採番する
    /// @param[in] path 監視するディレクトリのパス
    /// @note 失敗は意図的に無視する。監視できない場所（オフラインの共有、
    ///       権限不足）は自動更新されないだけで、他の動作には影響しない
    virtual void Watch(uint64_t watchId, const std::string& path) = 0;

    /// @brief 監視を終了する。
    /// @param[in] watchId 終了する監視の識別子。未登録でも安全
    virtual void Unwatch(uint64_t watchId) = 0;

    /// @brief デバウンス済みの通知をすべて取り出す。
    /// @param[out] out 取り出した通知の追加先。既存の要素は保持される
    /// @note UI スレッドからのみ呼ぶこと
    virtual void Drain(std::vector<ChangeEvent>& out) = 0;
};

}  // namespace kite::fs
