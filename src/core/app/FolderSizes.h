/// @file
/// @brief フォルダの合計サイズについての判断をまとめたもの。
///
/// サイズ列がフォルダについて何か言えるのは、その木を歩いた後だけ。歩くのは
/// `fs::FolderSizeJob`（検索とまったく同じ `fs::WalkTree` の上）で、覚えるのは
/// `fs::FolderSizeCache` ─ **表が正で、`fs::Entry::size` に書き戻すのは並べ替えの
/// ための写し**。
///
/// 自動で頼むのは画面に出ている行だけ（`For()` が描画から呼ばれる）で、シェル
/// アイコンとまったく同じ形になっている ─ 1 万件のフォルダでも頼むのは数十件で
/// 済む。例外はサイズで並べ替えているときで、そこは値が要るのが画面に出ている行
/// だけではない（`SyncForSort`）。
///
/// **`App` を持たない。** 要る依頼だけを構築時に受け取る ─ ファイルシステム、
/// 表示文字列、そして «自動で歩いてよい場所か» を答えるためのドライブ一覧。

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/app/SettingsEditor.h"
#include "core/fs/FileSystem.h"
#include "core/fs/FolderSize.h"
#include "core/i18n/Strings.h"
#include "core/model/Workspace.h"

namespace kite {

/// @brief フォルダのサイズを数える側。表とワーカーを持つ。
class FolderSizes {
public:
    /// @brief 依存を結び付けて構築する。**ワーカーはまだ作らない。**
    /// @param[in] fsys 歩きに使うファイルシステム。本オブジェクトより長生きすること
    /// @param[in] strings ステータス行の文言を引く表。同上
    /// @param[in] roots ドライブ一覧。`AutoCountEligible` が «固定ディスクか» を
    ///            ここで判定する。同上（`App::RefreshRoots` が差し替える実体）
    FolderSizes(fs::IFileSystem& fsys, const Strings& strings,
                const std::vector<fs::Root>& roots);

    ~FolderSizes();

    FolderSizes(const FolderSizes&) = delete;
    FolderSizes& operator=(const FolderSizes&) = delete;

    /// @brief ワーカーを用意する。
    /// @param[in] wake 数え終わりの通知先。本オブジェクトより長生きすること
    /// @note スレッドが立つのは最初の依頼のとき（`fs::JobQueue`）
    void Start(fs::IWakeSink& wake);

    /// @brief ワーカーを畳む。
    /// @note 歩いているだけなので途中で捨ててよい ─ 何も書き換えていない
    void Shutdown();

    /// @brief 数え方の設定を返す。
    /// @return 現在の設定
    FolderSizeMode mode() const { return mode_; }

    /// @brief 数え方の設定を変える。
    /// @param[in] mode 新しい設定
    /// @note `Off` にしたら表も捨てること（呼ぶ側の `Clear()`）─ 残すと、
    ///       数えない設定なのに数えた値が並び、しかも二度と新しくならない
    void SetMode(FolderSizeMode mode) { mode_ = mode; }

    /// @brief 覚えている表を返す。
    /// @return 表への参照。無効化（`ForgetChanged` / `ForgetRelated`）に使う
    fs::FolderSizeCache& cache() { return cache_; }

    /// @brief フォルダの合計サイズを返し、必要なら数え始める。
    /// @param[in] dir 行が並んでいるフォルダのパス
    /// @param[in] entry 対象の項目
    /// @return 数えた結果。フォルダでない・切ってある・まだ何も無いときは
    ///         状態が `fs::SizeState::Unknown` のもの
    /// @note UI 層が描画のたびに呼ぶ。**自動で数えるのは画面に出ている行だけ**
    ///       ─ ここが呼ばれるのがその行だけなので、1 万件のフォルダでも頼むのは
    ///       数十件で済む（シェルアイコンとまったく同じ形）
    /// @note リンク（ジャンクション・シンボリックリンク）は数えない。歩きが
    ///       リンクの先へ降りない以上、その中身は誰も数えていない
    fs::FolderSize For(const std::string& dir, const fs::Entry& entry);

    /// @brief フォルダ 1 つを数えるようワーカーに頼む。
    /// @param[in] path 数えるフォルダ
    /// @param[in] force 答えが出ているもの・やめたものも数え直す
    /// @note 表がまだ知らないときだけ実際に頼む ─ 描画から毎フレーム呼ばれる
    ///       経路があるので、二重の依頼を止めているのは `FolderSizeCache::Request`
    /// @note **頼まれたときは数え直す**（`force`）。数えた値は «そのときの値» なので、
    ///       明示的な «数えて» に «もう知っている» と答えるのは嘘に近い
    void Request(const std::string& path, bool force = false);

    /// @brief 一覧に並んでいるフォルダをまとめて数えるよう頼む。
    /// @param[in] tab 対象のタブ
    /// @param[in] force 答えが出ているもの・やめたものも数え直す
    /// @note **サイズで並べ替えているときはこれを通る。** 並べ替えの基準がその列で
    ///       ある以上、値が要るのは画面に出ている行だけではない ─ 数えていない
    ///       フォルダは 0 として並ぶので、見えている行だけ数えても順序は整わない
    void RequestAllIn(const Tab& tab, bool force = false);

    /// @brief サイズで並べ替えているタブのために、一覧のフォルダを数え始める。
    /// @param[in] tab 対象のタブ
    /// @note 自動で数える設定で、かつローカルの固定ディスクのときだけ走る。
    ///       手で頼まれたとき（`Cmd::CountFolderSizes`）はこの制限を通さない ─
    ///       共有や USB を数えるのは、それを頼んだ人の判断
    void SyncForSort(const Tab& tab);

    /// @brief 数え終わった値を、そのタブの項目に写す。
    /// @param[in,out] tab 対象のタブ
    /// @return 1 つでも写したら true
    /// @note **正は表のほうで、これは並べ替えのための写し。** `Tab::Rebuild()` は
    ///       `fs::Entry::size` で並べるので、そこに入っていなければサイズ順も
    ///       サイズの塊もフォルダを 0 として扱う。列に出す値は表から直接引く
    ///       （`For`）ので、2 つが食い違って見えることはない
    /// @note 写すのは数え終わったものだけ ─ 途中経過を並べ替えに載せると、
    ///       数えている間ずっと行が動いて目で追えなくなる
    bool ApplyTo(Tab& tab) const;

    /// @brief 数えているものと待っているものをすべて畳む。
    /// @note 途中経過は残さない ─ 止めた値が «数え終わった» 値と同じ顔で
    ///       残ると、どちらなのかを画面が言えない
    void Stop();

    /// @brief 届いたフォルダのサイズを取り込む。
    /// @param[in,out] workspace 値を写す先のタブを持つワークスペース
    /// @return 並べ直したら true。呼ぶ側はカーソルを画面内へ引き戻すこと
    /// @note 並べ替えの基準がサイズのタブは、**数え終わるものが無くなってから
    ///       1 回だけ**並べ直す ─ 1 件確定するたびに並べ替えると、数えている間
    ///       ずっと行が動いて目で追えなくなる
    bool Pump(Workspace& workspace);

    /// @brief 数えているフォルダがあるかを返す。
    /// @return 待ち・実行中のものがあれば true
    bool busy() const;

    /// @brief 数えているフォルダがあることを伝える文字列を返す。
    /// @return ステータス行の右に出す文字列。数えていなければ空
    /// @note 実行中のファイル操作や検索と同じ扱いで、期限では消えない ─ 深い木は
    ///       数十秒かかるので、黙っていると「押したのに何も起きていない」と
    ///       見分けが付かない
    std::string status() const;

    /// @brief カーソル行のフォルダについて、内訳を伝える文字列を返す。
    /// @param[in] tab フォーカスされているタブ
    /// @return ステータス行の左に添える文字列。言うことが無ければ空
    /// @note **ファイル数とフォルダ数はここでしか言わない。** 列を増やすと、
    ///       通常のフォルダでは全行が空になる列をウィンドウ中で持ち回ることに
    ///       なる。読めない枝があったこともここが言う ─ 列の数字の隣に印を足すと、
    ///       同じ列が 2 通りの綴りを持つ
    std::string detail(const Tab& tab) const;

private:
    /// @brief 自動で数えてよい場所かを判定する。
    /// @param[in] path 対象のフォルダ
    /// @return 数えてよければ true
    /// @note 自動で歩くのはローカルの固定ディスクだけ。ネットワーク共有・
    ///       リムーバブル・光学・仮想フォルダは、頼まれたときだけ歩く ─
    ///       「待たせて空を返す機能は無い機能より悪い」と同じ判断で、分単位に
    ///       なりうる歩きを黙って始めない
    bool AutoCountEligible(const std::string& path) const;

    fs::IFileSystem& fs_;
    const Strings& strings_;
    const std::vector<fs::Root>& roots_;

    std::unique_ptr<fs::FolderSizeJob> job_;
    fs::FolderSizeCache cache_;
    FolderSizeMode mode_ = FolderSizeMode::Auto;

    /// サイズで並べているタブを並べ直す必要があるか。**数え終わるものが無く
    /// なってから 1 回だけ**立てて落とす。
    bool resort_ = false;
};

}  // namespace kite
