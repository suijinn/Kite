/// @file
/// @brief アイコンの取得口と、その要求をまとめるキャッシュ。
///
/// UI は描画のたびに「このパスのアイコン ID をくれ」と聞くだけ。まだ無ければ 0 が
/// 返り、UI はベクタ描画のアイコンを代わりに描く。実物が届いた次のフレームから
/// 差し替わる。要求されるのは画面に見えている行だけなので、1 万件のフォルダでも
/// 取得しに行くのは数十件で済む。

#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace kite {

/// @brief アイコン画像の取得口。
///
/// 実装は platform/win/WinIconProvider.cpp。core はビットマップそのものを扱わず、
/// 識別子だけを持ち回る。画素を持つのは描画側だけ。
class IIconProvider {
public:
    virtual ~IIconProvider() = default;

    /// @brief パスに対応するアイコンの識別子を返す。
    /// @param[in] path 対象のパス
    /// @return 描画に使える識別子。まだ取得できていなければ 0
    /// @note 0 を返したときは取得を予約する。呼び出し側は代替の描画を行うこと
    /// @note UI スレッドからのみ呼ぶこと
    virtual uint32_t IconFor(const std::string& path) = 0;
};

/// @brief パスからアイコン識別子への対応表と、未取得分の待ち行列。
///
/// アイコンはオーバーレイの分だけパスごとに変わりうる（同じ .cpp でも、
/// コミット済みと変更済みでは違う絵になる）ので、拡張子ではなくパスで引く。
/// そのぶん表は伸びるため、上限を超えたら古いものから捨てる。
///
/// @note OS に触れないのでここだけ単体テストできる。実際の取得は
///       IIconProvider の実装が行う
class IconCache {
public:
    /// @brief 識別子を引き、無ければ取得待ちに積む。
    /// @param[in] path 対象のパス
    /// @return 取得済みなら識別子。未取得・取得中・取得失敗なら 0
    uint32_t Lookup(const std::string& path);

    /// @brief 取得待ちのパスを取り出す。
    /// @param[out] out 取り出したパスの追加先
    /// @param[in] max 一度に取り出す最大数
    /// @note 取り出したパスは「取得中」になり、結果が入るまで再要求されない
    void TakePending(std::vector<std::string>& out, size_t max);

    /// @brief 取得結果を記録する。
    /// @param[in] path 対象のパス
    /// @param[in] iconId 取得できた識別子。0 は「アイコンが無い」を意味する
    /// @note 0 も結果として覚える。取得できなかったパスを毎フレーム聞き直さない
    void Store(const std::string& path, uint32_t iconId);

    /// @brief 取得中だったパスを未取得に戻す。
    /// @param[in] paths 対象のパス列
    /// @note ホストが落ちたときなど、結果が永久に来ない場合に呼ぶ。次に画面へ
    ///       出たときに改めて要求される
    void Reset(const std::vector<std::string>& paths);

    /// @brief 表と待ち行列を空にする。
    void Clear();

    /// @brief 覚えているパスの数を返す。
    /// @return 登録件数
    size_t size() const { return entries_.size(); }

    /// @brief 取得待ちの件数を返す。
    /// @return 待ち行列の長さ
    size_t pendingCount() const { return pending_.size(); }

    /// @brief 表の上限を設定する。
    /// @param[in] capacity 覚えておくパスの最大数。1 未満は 1 に丸める
    void SetCapacity(size_t capacity);

private:
    /// 1 パス分の状態。
    struct Entry {
        uint32_t iconId = 0;   ///< 取得済みの識別子。0 は未取得または不在
        uint64_t used = 0;     ///< 最後に引かれた順番。古い順に捨てるのに使う
        bool resolved = false; ///< 結果が確定したか
        bool inFlight = false; ///< 取得を依頼済みか
    };

    void EvictIfNeeded();

    std::unordered_map<std::string, Entry> entries_;
    std::vector<std::string> pending_;
    uint64_t tick_ = 0;
    size_t capacity_ = 4096;
};

}  // namespace kite
