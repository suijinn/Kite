/// @file
/// @brief 元に戻せる操作の履歴。
///
/// Kite 自身が行ったファイル操作だけを覚える。シェルの Undo スタック（`FOF_ALLOWUNDO`
/// が積むもの）には呼び出す公開 API が無く、しかも名前の変更は `SHFileOperation` を
/// 通らないので、シェル任せでは «F2 で付けた名前を戻す» という一番よく要る操作が
/// 最初から対象外になる。
///
/// 逆操作を実際に行うのは App で、ここが持つのは「何をしたか」だけ。

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace kite {

/// @brief 元に戻せる操作の種類。
enum class UndoKind : uint8_t {
    Rename,  ///< 名前の変更。逆操作は元の名前へ戻す
    Create,  ///< 新規作成（フォルダ・ファイル）。逆操作はごみ箱へ入れる
    Move,    ///< 移動（切り取り貼り付け、ドラッグ移動）。逆操作は元のフォルダへ戻す
    Copy,    ///< コピー。逆操作は作られた複製をごみ箱へ入れる
    Delete,  ///< ごみ箱への削除。逆操作はごみ箱から元の場所へ戻す
    Erase,   ///< 完全削除。**戻せない印**。詳細は UndoStack::Push()
};

/// @brief 元に戻せる操作 1 つ分。
///
/// `targets` と `origins` は Rename / Move では同じ長さで、同じ添字が同じ 1 件を指す。
/// Create / Copy / Delete は `targets` だけを使い、Erase はどちらも空。
///
/// Delete の `targets` は**消される前のパス**。ごみ箱に入った後の名前ではないのは、
/// 削除の時点で分かっているのがそちらしかないから（`IShellIntegration::RestoreDeleted`）。
struct UndoAction {
    UndoKind kind = UndoKind::Delete;  ///< 何をしたか
    std::vector<std::string> targets;  ///< 操作が作った、または動かした先のパス
    std::vector<std::string> origins;  ///< 動かす前のパス。Rename と Move でだけ使う
};

/// @brief 元に戻せる操作の履歴。新しいものから順に取り出す。
///
/// @note Redo は持たない。逆操作は履歴に積まないので、`Ctrl+Z` を 2 回押しても
///       元の操作は復活しない
class UndoStack {
public:
    /// 覚えておく操作の数。これを超えた分は古いほうから捨てる。
    static constexpr size_t kLimit = 32;

    /// @brief 操作を 1 つ積む。
    /// @param[in] action 積む操作
    /// @note UndoKind::Erase を積むと、**それより前の履歴はすべて捨てる。**
    ///       完全削除の下にある操作は二度と到達できない ─ それを飛び越えて古い操作を
    ///       戻すと、消えたファイルはそのままに、その前の名前変更だけが巻き戻る。
    ///       ごみ箱への削除（UndoKind::Delete）は戻せるので、捨てる理由が無い
    void Push(UndoAction action);

    /// @brief 履歴が空かを返す。
    /// @return 空なら true
    bool empty() const { return actions_.empty(); }

    /// @brief 履歴に積まれている操作の数を返す。
    /// @return 操作の数
    size_t size() const { return actions_.size(); }

    /// @brief 次に元に戻す操作を返す。
    /// @return 最後に積まれた操作。履歴が空なら nullptr
    const UndoAction* top() const { return actions_.empty() ? nullptr : &actions_.back(); }

    /// @brief 次に元に戻す操作を取り除く。
    /// @note 履歴が空なら何もしない
    void Pop();

    /// @brief 履歴をすべて捨てる。
    void Clear() { actions_.clear(); }

private:
    std::vector<UndoAction> actions_;
};

}  // namespace kite
