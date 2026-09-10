#include "core/fs/TreeWalk.h"

#include <deque>
#include <utility>

namespace kite::fs {

void WalkTree(IFileSystem& fsys, const std::string& root, const TreeWalkAlive& alive,
              const TreeWalkVisit& visit) {
    if (root.empty()) return;

    std::deque<std::string> queue;
    queue.push_back(root);

    while (!queue.empty()) {
        // 打ち切りが効くのはここ ─ `List()` 1 回ぶんは戻ってくるまで止められない。
        if (alive && !alive()) return;

        const std::string dir = std::move(queue.front());
        queue.pop_front();

        const ListResult result = fsys.List(dir);
        // 読めなかったフォルダでも訪問子には渡す。1 つのアクセス拒否で歩きそのものを
        // 失敗にすると、`C:\` からの歩きは `System Volume Information` に当たった
        // 時点で必ず終わる ─ 頼まれたのは «読めるところを全部» である。
        if (visit && !visit(dir, result)) return;
        if (result.status != Status::Ok) continue;

        for (const Entry& e : result.entries) {
            if (!e.isDir()) continue;
            // **リンクの先へは降りない。** 辿れば輪に入るうえ、同じ木を 2 度
            // 数えることになる。
            if (Has(e.attrs, Attr::Link)) continue;
            queue.push_back(EntryPath(dir, e));
        }
    }
}

}  // namespace kite::fs
