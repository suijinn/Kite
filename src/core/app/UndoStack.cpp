#include "core/app/UndoStack.h"

namespace kite {

void UndoStack::Push(UndoAction action) {
    // 完全削除より前の履歴は捨てる。残しても届かないうえ、届いたら害になる ─
    // 消えたファイルはそのままに、その前の名前変更だけが巻き戻ることになる。
    // ごみ箱への削除は戻せるので、ここには入らない。
    if (action.kind == UndoKind::Erase) actions_.clear();

    actions_.push_back(std::move(action));
    if (actions_.size() > kLimit) actions_.erase(actions_.begin());
}

void UndoStack::Pop() {
    if (!actions_.empty()) actions_.pop_back();
}

}  // namespace kite
