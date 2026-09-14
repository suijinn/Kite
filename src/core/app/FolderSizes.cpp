#include "core/app/FolderSizes.h"

#include "core/base/PathUtil.h"
#include "core/base/Platform.h"
#include "core/base/Utf8.h"
#include "core/fs/VirtualPath.h"

namespace kite {

namespace {

// «この場所はそのマウント先に載っているか» ─ マウント先そのものも含む。
// 末尾の区切りは場所によって在ったり無かったりする（`Roots()` の "C:\" と
// タブの "C:\Box"）ので、綴りを揃えてから比べる。
bool Covers(const std::string& path, const std::string& root) {
    if (root.empty()) return false;
    return utf8::EqualsIgnoreCaseAscii(path::Normalize(path), path::Normalize(root)) ||
           path::IsInside(path, root);
}

}  // namespace

FolderSizes::FolderSizes(fs::IFileSystem& fsys, const Strings& strings,
                         const std::vector<fs::Root>& volumes)
    : fs_(fsys), strings_(strings), volumes_(volumes) {}

FolderSizes::~FolderSizes() = default;

void FolderSizes::Start(fs::IWakeSink& wake) {
    job_ = std::make_unique<fs::FolderSizeJob>(fs_, wake);
}

void FolderSizes::Shutdown() { job_.reset(); }

void FolderSizes::SetMode(FolderSizeMode mode) {
    if (mode == mode_) return;
    mode_ = mode;
    // «自動では数えない» はこの設定の下での判断なので、切り替えたら根拠が変わる。
    cache_.ForgetSkipped();
}

fs::FolderSize FolderSizes::For(const std::string& full, const fs::Entry& entry) {
    if (mode_ == FolderSizeMode::Off || !entry.isDir()) return {};
    // 歩きはリンクの先へ降りないので、リンクの中身は誰も数えていない。ここで
    // «数えた» 顔をすると、同じ木を 2 度数えた合計を出すことになる。
    if (fs::Has(entry.attrs, fs::Attr::Link)) return {};

    const fs::FolderSize known = cache_.Get(full);
    // 数え中・数え終わり・失敗・やめた・自動では数えない ─ どれももう答えが
    // 決まっている。`Skipped` がここで止まるのが肝で、そうでないと «この場所は
    // 自動で数えてよいか» を行ごとに毎フレーム訊き直すことになる。
    if (known.state != fs::SizeState::Unknown) return known;
    if (mode_ != FolderSizeMode::Auto) return {};
    // クラウドにしか実体が無いフォルダは、歩くだけで OS が中身を取りに行きうる。
    // 頼まれたときだけ歩く。
    if (fs::Has(entry.attrs, fs::Attr::Placeholder) || fs::Has(entry.attrs, fs::Attr::Offline) ||
        !AutoCountEligible(full)) {
        // 印を立ててから、その印を答えとして返す ─ 1 フレーム目と 2 フレーム目で
        // 違う状態を返しては、同じ問いに 2 通り答えたことになる（列の見た目は
        // どちらも `<DIR>` だが、そこに頼るのは «同じ» の理由として弱い）。
        cache_.Skip(full);
        return cache_.Get(full);
    }

    Request(full);
    return cache_.Get(full);
}

void FolderSizes::Request(const std::string& path, bool force) {
    if (!job_ || path.empty()) return;
    // 仮想フォルダの列挙は kite_shellhost.exe 1 本を直列に通る ─ 再帰的に占有すると、
    // 他のフォルダが 1 つも開けなくなる（検索を断っているのと同じ理由）。
    if (vfs::IsVirtual(path)) return;
    if (cache_.Request(path, force)) job_->Request(path);
}

void FolderSizes::RequestAllIn(const Tab& tab, bool force) {
    if (vfs::IsVirtual(tab.path)) return;
    for (const fs::Entry& e : tab.listing.entries) {
        if (!e.isDir() || fs::Has(e.attrs, fs::Attr::Link)) continue;
        Request(fs::EntryPath(tab.path, e), force);
    }
}

void FolderSizes::SyncForSort(const Tab& tab) {
    if (mode_ != FolderSizeMode::Auto) return;
    if (tab.view.sort != SortKey::Size || tab.search.active) return;
    if (!AutoCountEligible(tab.path)) return;
    RequestAllIn(tab);
}

bool FolderSizes::AutoCountEligible(const std::string& p) const {
    if (p.empty() || vfs::IsVirtual(p)) return false;
    // ネットワークは共有 1 つで分単位になりうる。「待たせて空を返す機能は無い機能
    // より悪い」と同じ判断で、黙って歩き始めない ─ 頼まれれば歩く。
    if (path::UncServerLength(p) > 0) return false;

    // **答えるのは «いちばん深く一致するマウント先»。** ドライブ文字だけを見て
    // いたころは、`C:\Box` のようにフォルダへ載ったクラウドが «C: は固定ディスク»
    // の一言で自動の歩きに入っていた ─ 下に載っているもののほうが新しい答えを
    // 持つ（クラウド側がドライブ文字を取っていれば今までどおりそこで止まる）。
    const fs::Root* best = nullptr;
    size_t depth = 0;
    for (const fs::Root& v : volumes_) {
        if (!Covers(p, v.path)) continue;
        const size_t length = path::Normalize(v.path).size();
        if (best && length <= depth) continue;
        best = &v;
        depth = length;
    }
    // どのボリュームの下か分からないものは «固定ディスクだと分かっている» に
    // 入らない。
    return best && best->kind == fs::RootKind::Fixed;
}

bool FolderSizes::ApplyTo(Tab& tab) const {
    bool changed = false;
    for (fs::Entry& e : tab.listing.entries) {
        if (!e.isDir()) continue;
        const fs::FolderSize value = cache_.Get(fs::EntryPath(tab.path, e));
        // 写すのは数え終わったものだけ ─ 途中経過を並べ替えに載せると、数えて
        // いる間ずっと行が動いて目で追えなくなる。
        if (!value.settled() || e.size == value.bytes) continue;
        e.size = value.bytes;
        changed = true;
    }
    return changed;
}

bool FolderSizes::Pump(Workspace& workspace) {
    if (!job_) return false;
    std::vector<fs::FolderSizeUpdate> updates;
    job_->Drain(updates);

    const uint64_t epoch = job_->epoch();
    bool settledAny = false;
    for (const fs::FolderSizeUpdate& u : updates) {
        // やめた後に届いた «前の代» の答え。取り込めば、止めたはずの値が
        // 数え終わった顔で表に戻る。
        if (u.epoch != epoch) continue;
        if (!cache_.Apply(u)) continue;
        if (u.done) settledAny = true;
    }

    // 写すのは確定が届いたときだけ。途中経過ごとに全項目を引き当て直すと、
    // 10 万件のフォルダで毎回その代金を払うことになる。
    if (settledAny) {
        workspace.ForEachTab([&](Tab& t) {
            if (!ApplyTo(t)) return;
            if (t.view.sort == SortKey::Size) resort_ = true;
        });
    }

    // **並べ直すのは、数えるものが無くなってから 1 回だけ。** 1 件確定するたびに
    // 並べ替えると、数えている間ずっと行が動いて目で追えない ─ 途中経過は
    // «表示» のもので、並べ替えは «確定した値» のもの。
    if (!resort_ || job_->busy()) return false;
    resort_ = false;
    workspace.ForEachTab([](Tab& t) {
        if (t.view.sort == SortKey::Size) t.Rebuild();
    });
    return true;
}

void FolderSizes::Stop() {
    if (job_) job_->CancelAll();
    cache_.ResetInFlight();
    resort_ = false;
}

bool FolderSizes::busy() const {
    return (job_ && job_->busy()) || cache_.countingCount() > 0;
}

std::string FolderSizes::status() const {
    const size_t counting = cache_.countingCount();
    if (counting == 0) return {};
    return strings_.Format("ui.folder_size_counting", { std::to_string(counting) });
}

std::string FolderSizes::detail(const Tab& tab) const {
    const fs::Entry* e = tab.CursorEntry();
    if (!e || !e->isDir()) return {};

    const fs::FolderSize value = cache_.Get(fs::EntryPath(tab.path, *e));
    // そのフォルダ自体を読めなかった。列は `<DIR>` のままなので、理由を言えるのは
    // ここしかない。
    if (value.state == fs::SizeState::Failed) return strings_.Get("ui.err_generic");
    if (!value.known()) return {};

    // **ファイル数とフォルダ数を言うのはここだけ。** 列を増やすと、通常のフォルダ
    // では全行が埋まるとは限らない列をウィンドウ中で持ち回ることになる。
    std::string text = strings_.Format(
        "ui.folder_size_detail", { std::to_string(value.files), std::to_string(value.dirs) });
    // 読めない枝があったことも列には出さない ─ 数字の隣に印を足すと、同じ列が
    // 2 通りの綴りを持つことになる。
    if (value.incomplete) text += " " + strings_.Get("ui.folder_size_incomplete");
    return text;
}

}  // namespace kite
