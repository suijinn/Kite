#include "core/app/Searching.h"

#include "core/base/Utf8.h"

namespace kite {

Searching::Searching(fs::IFileSystem& fsys, const Strings& strings)
    : fs_(fsys), strings_(strings) {}

Searching::~Searching() = default;

void Searching::Start(fs::IWakeSink& wake) { job_ = std::make_unique<fs::SearchJob>(fs_, wake); }

void Searching::Shutdown() { job_.reset(); }

void Searching::Begin(Tab& tab, const std::string& query) {
    if (!job_) return;
    if (tab.search.token) job_->Cancel(tab.search.token);

    tab.search.active = true;
    tab.search.truncated = false;
    tab.search.sieve = query;
    tab.filter = query;

    // 0 件から始める。フォルダの中身を残したまま検索欄を出すと、最初の 1 打鍵が
    // それを消したように見える ─ 検索欄の下に並ぶ行は検索結果でなければならない。
    // 場所そのものについての値（表示名・容量）は残す。タブはまだそこに立っている。
    tab.listing.entries.clear();
    tab.listing.status = fs::Status::Ok;
    tab.listing.message.clear();
    tab.marked.clear();
    tab.groups.clear();
    tab.cursor = 0;
    tab.anchor = 0;
    tab.scroll = 0.0f;
    tab.loaded = true;
    // 走っている列挙のトークンを落とす。残したままだと、後から届いたフォルダの
    // 一覧が、集めたばかりの検索結果を黙って上書きする（PumpLoader はトークンで
    // 突き合わせるので、0 にしておけばその答えはどのタブのものでもなくなる）。
    tab.loadToken = 0;
    tab.search.token = job_->Start(tab.path, query);
    tab.Rebuild();
}

void Searching::Cancel(Tab& tab) {
    if (!tab.search.active) return;
    if (job_ && tab.search.token) job_->Cancel(tab.search.token);
    tab.search = SearchState{};
    // 問いも一緒に捨てる。検索を抜けた先の一覧はこのフォルダの中身で、そこに
    // 検索語が絞り込みとして残っていると、フォルダが空に見える。
    tab.filter.clear();
}

bool Searching::SyncQuery(Tab& tab, const std::string& query) {
    tab.filter = query;
    const std::string sieve = utf8::ToLowerAscii(tab.search.sieve);
    const std::string asked = utf8::ToLowerAscii(query);
    // ふるいが今の問いの部分文字列である限り、«今の問いに当たるもの» はすべて
    // «ふるいに当たるもの» でもある ─ つまり、もう手元にある。前へ打ち足している
    // 限り歩き直しが起きないのはこれが理由で、縮めたときだけディスクを歩き直す。
    //
    // **ただし «全部» を持ち帰った歩きに限る。** 条件は 2 つ:
    //
    // - **まだ歩いている最中なら歩き直す。** そうしないと、ふるいは «最初の 1 文字»
    //   のまま固まる ─ 打ち始めた瞬間に走り出した歩きが、以後どれだけ打ち足しても
    //   «全部持っている» と主張し続けるので、`C:\` から `report` を探すつもりが
    //   «r を含むもの» を集める歩きになる。捨てるのは途中まで集めたものだけで、
    //   同じものは狭い問いで拾い直せる。
    // - **上限で打ち切られた歩きも歩き直す。** あれは «当たりの一部» なので、
    //   絞り込んだ答えが完全である保証がない。広すぎる問いはたいてい歩き始めて
    //   すぐ上限に届くので、歩き直す代金もそこで頭打ちになる。
    const bool complete = !tab.search.running() && !tab.search.truncated;
    if (complete && !sieve.empty() && asked.find(sieve) != std::string::npos) {
        tab.Rebuild();
        return true;
    }
    Begin(tab, query);
    return false;
}

bool Searching::Pump(Workspace& workspace) {
    if (!job_) return false;
    std::vector<fs::SearchBatch> batches;
    job_->Drain(batches);
    if (batches.empty()) return false;

    bool touched = false;
    for (fs::SearchBatch& batch : batches) {
        Tab* target = nullptr;
        workspace.ForEachTab([&](Tab& t) {
            if (t.search.token != 0 && t.search.token == batch.token) target = &t;
        });
        if (!target) {
            // 行き先が無い ─ タブが閉じた、または背面に回って一覧を手放した
            // （`Tab::DropListing`）。歩き続ける理由がもう無いので、ここで畳む。
            job_->Cancel(batch.token);
            continue;
        }

        // まだ動かしていないカーソルは先頭に留める。Rebuild() は «同じ項目の上に
        // 留まる» を約束するので、放っておくと最初に見つかった 1 件を追いかけて
        // 一覧の中を下がっていく ─ 誰も指していないものを指し続けることになる。
        const bool atTop = target->cursor == 0;

        for (fs::Entry& e : batch.entries) target->listing.entries.push_back(std::move(e));
        target->marked.resize(target->listing.entries.size(), 0);
        if (batch.truncated) target->search.truncated = true;
        if (batch.done) target->search.token = 0;
        target->Rebuild();
        if (atTop) target->cursor = target->SkipGroupRows(0, 1);
        touched = true;
    }
    return touched;
}

std::string Searching::status(const Tab& tab) const {
    if (!tab.search.active) return {};
    // まだ何も訊かれていない。案内はここではなく一覧の側が出す ─ 帯の右は
    // «今何が起きたか» で、まだ何も起きていない。
    if (tab.search.sieve.empty()) return {};

    const std::string count =
        strings_.Format("ui.status_items", { std::to_string(tab.ItemCount()) });
    if (tab.search.running()) return strings_.Format("ui.search_running", { count });
    if (tab.search.truncated) {
        return strings_.Format("ui.search_truncated",
                               { count, std::to_string(fs::kSearchMaxResults) });
    }
    return strings_.Format("ui.search_done", { count });
}

}  // namespace kite
