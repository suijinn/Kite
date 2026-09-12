# Kite リファクタリング仕様書

冗長な処理の除外・設計の見直し・リソース負荷の効率化を目的とした、コード修正作業の
**変更仕様と設計**。ここに書いてあることだけを実施すれば作業が完了する粒度で、項目ごとに
「現状 → 変更後 → 影響ファイル → 検証 → 完了条件」を持つ。

構造の説明は [architecture.md](architecture.md)、個々の判断の理由は
[../CLAUDE.md](../CLAUDE.md) にある。**この文書は両者を変えない。** 変えるのはコードの
形だけで、機能・見た目・キー割り当て・ini の互換はすべて据え置く（§1）。

対象コミット: `f8ae0d0`（0.1.15）。

---

## 0. 要約

| 分類 | 項目数 | 主なもの |
| --- | --- | --- |
| A. 設計（構造） | 8 | `App` の責務分割、非同期ワーカー 5 実装の骨格統合、再描画要求の一本化、`AppUi` のドラッグ状態の型化 |
| B. 冗長処理 | 8 | `Tab::Rebuild` の比較関数内での割り当て、`Strings::Get` の毎回の `std::string` 構築、F1 一覧の毎フレーム再構築、ステータス期限の 500 ms 全面再描画 |
| C. リソース | 3 | 起動時スレッド 9 本 → 3 本、`MeasureText` キャッシュの全消し |

**期待する効果（計測で確認する。§6）**

| 指標 | 現状 | 目標 |
| --- | --- | --- |
| 起動直後の常駐スレッド（UI 除く） | 10 本（Loader 2 + FileOps 4 + Search 1 + FolderSize 2 + Watcher 1） | 3 本（Loader 2 + Watcher 1） |
| `Tab::Rebuild`（10 万件・拡張子順） | 比較ごとに 4 回の割り当て（推定 700 万回/回） | 割り当て 0 回/比較 |
| 1 フレームのヒープ割り当て | 数百回（行ごとの文字列整形・キー引き） | 半減（目安） |
| `App.h` / `App.cpp` | 1,236 / 2,820 行、メンバ 55 個 | 600 / 1,200 行以下 |
| `AppUi::OnMouse` | 620 行・ドラッグ状態フィールド約 40 個 | variant 1 つ、関数 200 行以下 |
| `host_.Invalidate()` の呼び出し箇所（`core/app`） | 100 か所 | 10 か所以下 |

---

## 1. 前提（変えないもの）

リファクタリングなので、次はすべて**不変条件**。破っていたらその変更は誤り。

1. **3 層分離**。`core/` と `ui/` に Windows ヘッダを入れない。`tests/` は `kite_core` だけを
   リンクする。CI の grep がこれを見ている。
2. **`App::Execute` はコマンド 1 つに `case` 1 つ**（`AppCommands.cpp`）。表であること
   自体が設計なので分割しない。
3. **描画は毎フレーム組み直す**（`AppUi`）。保持型ウィジェットも差分再描画も入れない。
   全面再描画が 1 ms を切っている以上、そこを速くする意味が無い。
4. **ホバーは「どの行に乗っているか」を覚えない**（CLAUDE.md「制御フロー」）。
5. **ワーカーの本数と役割**（検索 1 本・サイズ 2 本・ファイル操作 4 本・列挙 2 本）は
   理由ごと据え置く。変えるのは「いつ作るか」（§C-1）だけ。
6. **`Entry::address` は実フォルダの項目で空のまま**。全項目にフルパスを持たせない
   （10 万件で 1 本ずつ `std::string` が増える）。
7. **`PumpLoader` の回収順**（ファイル操作 → 検索 → サイズ → 列挙）。順序に意味がある
   （完了が一覧の取り直しを要求する）。
8. **CLAUDE.md に理由が書かれた挙動**はすべて据え置く。特に: 押しただけでは選択を
   壊さない（`pendingUnmark_`）、`Tab::visible` の負の添字、`keys.ini` は書かれた
   とおりに読む、`fileOpStatus` は覚えずに組み立てる（§B-7 参照）。
9. **公開 API の互換**。`App` と `AppUi` のうちテストと `platform/` が呼ぶメソッドは、
   分割後も同じ名前で呼べる（転送でよい）。テストを書き換えるのは「検査している
   意味が変わらない範囲」だけ（§A-4 の `invalidateCount` が唯一の例外）。
10. **Doxygen 警告 0**、`/W4` 警告 0、`.clang-format` は掛けない（既存コードが従って
    いないので差分が汚れる）。

---

## 2. 現状分析

### 2.1 大きさ

| ファイル | 行数 | 備考 |
| --- | --- | --- |
| `core/app/App.h` | 1,236 | メンバ変数 55 個、`PromptKind`・`Prompt`・`Composition`・`PendingFileOp`・`SidebarSection`・`FolderSizeMode` … を同居 |
| `core/app/App.cpp` | 2,820 | 状態・移動・一覧・入力欄・IME・ファイル操作・Undo・検索・サイズ・切り取り印・既定マネージャ・チューザ |
| `core/app/AppCommands.cpp` | 823 | `Execute` の 134 `case`（据え置き） |
| `core/app/AppConfig.cpp` | 773 | 設定とセッションの読み書き（据え置き） |
| `ui/AppUi.cpp` | 1,757 | レイアウト・描画 |
| `ui/AppUiMouse.cpp` | 1,152 | `OnMouse` が 528〜1152 行目の **1 関数 620 行** |
| `ui/AppUiOverlays.cpp` | 860 | F1・キー設定・設定・チューザ |
| `tests/` | 25 スイート・686 ケース | `test_app` 171、`test_appui` 123 |

### 2.2 スレッド

`App::Init` が `DirectoryLoader`(2)・`FileOpQueue`(4)・`SearchJob`(1)・`FolderSizeJob`(2) を
**起動時に**作り、`main_win.cpp` が `WinDirectoryWatcher`(1) を作る。`WinIconProvider` だけが
最初の要求まで起動しない。ファイル操作の 4 本は最初の操作まで一度も走らない。

5 つのワーカークラス（`DirectoryLoader` / `SearchJob` / `FolderSizeJob` / `FileOpQueue` /
`WinIconProvider`）は、`mutex_ + cv_ + queue_ + done_ + stop_ + threads_ + Request/Drain/busy`
という**同じ骨格を別々に書いている**。違うのは Job→Result の中身と、「次にどれを走らせて
よいか」（FileOps の衝突判定、Search の打ち切り）だけ。

### 2.3 1 フレームの仕事（全面再描画 1 回あたり）

`AppUi::Paint` は毎フレーム全部を組み直す（設計どおり）。その中で**結果が前フレームと
変わらないのに毎回やり直しているもの**を挙げる。

| 場所 | 行ごと／回ごとに起きること | 種類 |
| --- | --- | --- |
| `PaintList` | `fs::EntryPath` を **2 回**組む（`IsCut`/`IconFor` 用と `FolderSizeFor` の中） | 割り当て |
| `PaintList` | `FormatDateTime`（`localtime_s`）、`FormatSize`、`path::Extension`（内部で 2 回割り当て） | 割り当て・OS 呼び出し |
| `PaintList` / 全体 | `Strings::Get(string_view)` が `map_.find(std::string(key))` で**呼ぶたびに `std::string` を構築** | 割り当て |
| `PaintList` | `App::FolderSizeFor` → `AutoCountEligible` が不適格（ネットワーク・非固定ディスク）な行について**毎フレーム `roots_` を走査**（結果を表に残さない） | 再計算 |
| `PaintSidebar` | `Region::path` に行ごとの `std::string` をコピー | 割り当て |
| `PaintStatusBar` | `fileOpStatus()` / `searchStatus()` / `folderSizeStatus()` / `folderSizeDetail()` を毎回 `Format` | 割り当て |
| `PaintKeyHelp`（F1 表示中） | 134 コマンドの `lines` を**毎フレーム構築**し、`KeyMap::ChordsFor` が `order_` を線形走査（134 × 全バインド） | 再計算 |
| `PaintCommandPalette` / `PaintPlaces` | 見えている行の `MeasureText` ×3（キャッシュあり） | 小 |
| `D2DRenderer::UpdateTheme` | `theme_ = theme;` を**無条件に**（`std::string` 2 本のコピー） | 割り当て |
| `D2DRenderer::DrawText` | `ToWide` で UTF-16 に変換 | 割り当て（必要） |

### 2.4 再描画の引き金

- `SetStatus` のたびに `WinWindow::Paint` が **500 ms タイマー**を張り、期限（4 秒）まで
  約 8 回の全面再描画で期限切れを検出する。
- `icons_->hasPendingRequests()` のときの追加 1 フレーム（必要）。
- ホバーは矩形が変わったときだけ（抑制済み）。
- `host_.Invalidate()` が `core/app` に 100 か所。CLAUDE.md「`App::SetStatus` は自分で
  再描画を要求する」の罠のとおり、**抜けると「次の無関係な打鍵まで出ない」**形の不具合が
  戻る構造。

### 2.5 一覧の組み直し（`Tab::Rebuild`）

```cpp
case SortKey::Ext:
    cmp = path::NaturalCompare(path::Extension(a.name), path::Extension(b.name));
```

`path::Extension` は `FileName` で 1 本、`ToLowerAscii` で 1 本の `std::string` を作る。
比較 1 回で 4 回の割り当て。`std::stable_sort` の比較回数は n log n なので、10 万件で
**約 170 万比較 × 4 ≈ 700 万回の割り当て**が並べ替え 1 回ごとに起きる。絞り込みも
`utf8::ToLowerAscii(e.name)` を項目ごとに毎回作り、`Ctrl+F` の打鍵ごとに繰り返す。

### 2.6 重複した走査

- `Session::Panes()` は毎回 `std::vector<Pane*>` を作る。呼び出し 18 か所。
- 「全セッション × 全ペイン × 全タブ」の 3 重ループが `App.cpp` に 6 か所
  （`PumpLoader` に 2 つ、`PumpFolderSizes`・`PumpSearch`・`RefreshTabsShowing`・
  `AppConfig`）。
- `const_cast<App&>(app_)` が `AppUi.cpp` に 3 か所（`workspace()` と `HasBookmark` に
  const 版が無いため）。

---

## 3. 変更仕様 ─ A. 設計

### A-1 `App` の責務分割

**現状**: `App` が状態の所有・配線・コマンド実行に加え、7 つの機能の実装を抱えている。
`App.h` 1,236 行はヘッダとしてすでに読めない。

**変更後**: `App` は「所有・配線・`Execute`・入力の振り分け」だけを持ち、機能ごとの
**部品**に委譲する。部品は `App&` を持たず、要る依存だけを構築時に受ける（`Workspace&`、
`IHost&`、`const Strings&`、`fs::IFileSystem&` …）。**判断は 1 か所**という現在の約束
（CLAUDE.md の各節）は、部品の中でそのまま守る。

| 部品（新ファイル、`core/app/`） | 移す状態 | 移す関数（`App.cpp` の行） |
| --- | --- | --- |
| `FolderSizes.h/.cpp` | `sizes_`, `sizeJob_`, `resortSizes_`, `folderSizeMode_` | 269〜450: `FolderSizeFor`, `RequestFolderSize(sIn)`, `SyncFolderSizesForSort`, `AutoCountEligible`, `ApplyFolderSizes`, `PumpFolderSizes`, `StopFolderSizes`, `folderSizesBusy/Status/Detail` |
| `Searching.h/.cpp` | `search_` | 705〜830: `StartSearch`, `CancelSearch`, `SyncSearchQuery`, `PumpSearch`, `searchStatus` |
| `FileOps.h/.cpp` | `fileOps_`, `pendingOps_`, `undo_`, `cutPaths_` | 1044〜1090（切り取り印）, 2429〜2820: `DoDelete`, `DoPaste`, `DuplicateInPlace`, `DoUndo`, `QueueFileOp`, `PumpFileOps`, `FinishFileOp`, `fileOpStatus`, `fileOpsBusy`, `PerformDrop` の依頼部分 |
| `Prompting.h/.cpp` | `prompt_`, `composition_`, `complete_`, `completeToken_`, `completeRequested_` | 1590〜1975: `BeginPrompt`, `CancelPrompt`, `ApplyPrompt`, `HandlePromptKey`, `acceptsText`, `Set/EndComposition`, `SyncCompletion`, `RequestCompletion`, `MoveCompletion`, `ChooseCompletion`, `CancelInlineEdit`, `CopyFieldToClipboard`, `PasteIntoField` |
| `Pickers.h/.cpp` | `placePicker_`, `commandPalette_` | 2237〜2430: `CollectOpenTabs`, `CollectHistory`, `OpenPlacePicker`, `OpenCommandPalette`, `SyncPickerMode`, `ChoosePlace`, `RunPaletteCommand`, `PickerClipboardKey` |
| （据え置き） | `keymap_`, `keyEditor_`, `settingsEditor_`, `strings_`, `theme_`, `settings_`, `workspace_`, `roots_`, `quickAccess_`, `*Order_`, `columns_`, 各種フラグ | `AppConfig.cpp` はそのまま |

- **`PromptKind` / `Prompt` / `Composition` は `Prompting.h` へ**、`PendingFileOp` は
  `FileOps.h` へ、`SidebarSection` と `FolderSizeMode` は使う側のヘッダへ。`App.h` に
  残るのは `App` クラスと `SettingsValues` の受け渡しだけ。
- **`ApplyPrompt` の分岐**（`PromptKind` ごとに移動・名前変更・作成・削除確認…を実行する）
  は入力欄の責務ではなく `App` の責務。`Prompting` は「何を尋ねていて、確定したら
  どの `PromptKind` で何という文字列か」を返し、実行は `App::ApplyPrompt` に残す。
  `HandlePromptKey` も同様に「入力欄が消費したか／確定したか／取り消したか」を返す。
- **`FolderSizes` は `roots_` を参照で受ける**（`AutoCountEligible` が要る）。`RefreshRoots`
  の後に `FolderSizes::RootsChanged()` を呼ぶ（§B-4 (b) の表を捨てるため）。
- **`App::PumpLoader` は `App::Pump()` に改名**し、部品の `Pump` を §1-7 の固定順で呼ぶ。
  `PumpLoader` は `WinWindow` と `main_win.cpp` が呼ぶので**転送関数として残す**。
- **`App` の public メソッドは残す**（転送 1 行）。`AppUi` とテストの呼び出しを 1 度に
  書き換えない。転送を消すのは A-1 完了後の別 PR。
- **`AppUi` からは部品を直接引けるようにする**（`app_.folderSizes()`、`app_.prompting()`）。
  転送関数を消す段階で切り替える。

**影響ファイル**: `core/app/App.h`, `App.cpp`, `AppCommands.cpp`, `AppConfig.cpp`,
新規 5 組、`ui/AppUi*.cpp`（`app_.prompt()` 等の呼び出し）、`tests/test_app.cpp`
（インクルードのみ）、`CMakeLists.txt`、`docs/architecture.md` §2 の表。

**検証**: `test_app` / `test_appui` / `test_fileops` / `test_foldersize` / `test_search` /
`test_placepicker` / `test_commandpalette` が**無変更で**通ること。

**完了条件**: `App.h` ≤ 600 行、`App.cpp` ≤ 1,200 行。部品はどれも `App.h` を
インクルードしない（循環が無いことを `#include` の grep で確認）。

**順序**: `FolderSizes` → `Searching` → `Pickers` → `FileOps` → `Prompting`。境界が
はっきりしている順で、`Prompting` は `App` との往復（`ApplyPrompt`）が多いので最後。

### A-2 非同期ワーカーの骨格を 1 つにする

**現状**: §2.2 の 5 実装。

**変更後**: `core/fs/JobQueue.h` に 1 つのテンプレートを置く。

```cpp
namespace kite::fs {

/// 「依頼を積み、ワーカーが 1 つずつ処理し、結果を UI スレッドが回収する」の骨格。
template <class Job, class Result>
class JobQueue {
public:
    using Run = std::function<Result(const Job&, const std::atomic<bool>& stop)>;
    /// 次に走らせてよい依頼の添字。既定は先頭。FileOpQueue が衝突判定をここに置く
    using Pick = std::function<size_t(const std::deque<Job>& queue,
                                      const std::vector<Job>& running)>;

    JobQueue(IWakeSink& wake, int workers, Run run, Pick pick = {});
    ~JobQueue();  // stop を立てて join

    uint64_t Request(Job job);            // 最初の Request でワーカーを起動
    void Drain(std::vector<Result>& out); // UI スレッド
    bool busy() const;
    int pending() const;
    int running() const;
    void Cancel(...);                     // 実装は各クラスに残す（下記）
};

}  // namespace kite::fs
```

| クラス | `JobQueue` に乗せるもの | 残すもの |
| --- | --- | --- |
| `DirectoryLoader` | 全部 | トークン採番（`Request` の戻り値） |
| `FolderSizeJob` | 骨格 | `claimed_`（同じパスを 2 度歩かない）、`epoch_`（`CancelAll`）、`Publish` の 150 ms まとめ |
| `SearchJob` | 骨格（ワーカー 1 本） | `active_` トークンによる打ち切り、`kSearchBatch` / `kSearchFlushMs` のまとめ |
| `FileOpQueue` | 骨格 | `FindRunnable` を `Pick` として渡す、`active_` の触る場所 |
| `WinIconProvider` | 骨格（ワーカー 1 本、遅延起動） | ホストの世代管理、画素の保持、`Pump` のアップロード |

- **`Run` に `stop` を渡す**のは、検索とサイズが「フォルダの切れ目で抜ける」ため。
  `DirectoryLoader` と `FileOpQueue` は無視する（`List` も `SHFileOperation` も中断できない）。
- **public API は 5 クラスとも変えない。** `tests/test_fileops.cpp` の `Gate` /
  `WaitForGate` による並列の検査、`test_search` / `test_foldersize` の打ち切りの検査が
  そのまま骨格の検査になる。
- **遅延起動は骨格の既定**（§C-1）。

**影響ファイル**: 新規 `core/fs/JobQueue.h`、`DirectoryLoader.*`, `SearchJob.*`,
`FolderSize.*`, `FileOpQueue.*`, `platform/win/WinIconProvider.*`。

**検証**: 上記 3 スイート + `test_app`（列挙の到着）+ `test_iconcache`。新規に
`tests/test_jobqueue.cpp`（起動が遅延する／`Pick` が呼ばれる／停止で join する）を
足し、`KITE_TEST_SUITES` に登録。

**完了条件**: 5 クラスの `.cpp` 合計が 300 行以上減る。`std::condition_variable` の
出現が `core/fs` で `JobQueue.h` の 1 か所（`WinDirectoryWatcher` は IOCP なので対象外）。

### A-3 再描画要求の一本化

**現状**: §2.4。100 か所。

**変更後**: **入口で 1 回**。「入力を受け取った」「ワーカーの結果を回収した」は必ず何かを
変えたとみなし、次の 5 つの末尾で `host_.Invalidate()` を呼ぶ。

| 入口 | 備考 |
| --- | --- |
| `App::OnKey` | 消費しなかった（`false`）ときも呼んでよい（合成されるので代金は無い） |
| `App::OnChar` | 同上 |
| `App::Pump()`（A-1） | 何も届いていなければ呼ばない（現状の `PumpLoader` と同じ） |
| `AppUi::OnMouse` | `Move` で矩形が変わらなければ呼ばない（現状の抑制を維持） |
| `App::SetStatus` | **残す**。期限タイマーの経路（`WinWindow::Paint` が張る）がここに依存している |

- 個別の `host_.Invalidate()` は、上の入口を**通らない経路**だけ残す: `SetStatus`、
  `ClearCutMarks`（`SyncCutMarks` はフォーカス復帰から呼ばれる）、`SetWindowActive`、
  `OpenForwardedPaths`（別プロセスからの `WM_COPYDATA`）、ドロップ（`PerformDrop`）。
- **`AppUi` の `app_.host().Invalidate()`** も同様に `OnMouse` の末尾 1 回に畳む。
  `SetDropFeedback` / `ClearDropFeedback` は OS のドロップターゲットから来るので残す。

**影響ファイル**: `core/app/App.cpp`, `AppCommands.cpp`, `AppConfig.cpp`, `ui/AppUiMouse.cpp`,
`tests/test_app.cpp`。

**検証**: `test_app` に `FakeHost::invalidateCount` を見ているケースがある。**「1 以上」の
検査はそのまま**、「ちょうど N」の検査は `≥ 1` に書き直す（検査している意味は
「描き直しが頼まれた」なので変わらない）。加えて、`Ctrl+C` 直後に `invalidateCount` が
増えること（CLAUDE.md の罠の再現ケース）を残す。

**完了条件**: `git grep -c "Invalidate()" src/core/app` ≤ 10。

### A-4 `AppUi` のドラッグ状態を型で表す

**現状**: `Drag` 列挙 16 種と、種別ごとのフィールド約 40 個が `AppUi` に平置き。
`CancelDrag` はそれらを 1 つずつ初期値に戻す列挙で、フィールドを足すたびに抜ける。
`OnMouse` 620 行は「`drag_` が X なら」の分岐が Move / Up / Down の 3 か所に散る。

**変更後**: 「今おこなっている 1 つの操作」を `std::variant` 1 つにする。

```cpp
struct SplitterDrag   { SplitNode* node; float origin; float ratio; };
struct TabDrag        { Pane* pane; int index; bool started;  // Pending ↔ 本番
                        Pane* dropPane; int dropIndex; RectF marker; bool outside; };
struct FileDrag       { bool started; };                        // PendingFile
struct MarqueeDrag    { Pane* pane; Tab* tab; float ax, ay, x, y;
                        std::vector<uint8_t> base; };
enum class ReorderKind : uint8_t { Sidebar, Section, Session, Column };
struct ReorderDrag    { ReorderKind kind; SidebarSection section; int index; bool started;
                        int dropIndex; RectF marker;
                        std::string pendingPath; bool pendingNewTab; };  // Sidebar のみ
struct ColumnWidthDrag{ int index; float right; };
struct TabBarWidthDrag{ float left; float max; };
using DragState = std::variant<std::monostate, SplitterDrag, TabDrag, FileDrag, MarqueeDrag,
                               ReorderDrag, ColumnWidthDrag, TabBarWidthDrag>;
DragState drag_;
```

- **`Pending*` は `started` フラグ**。昇格は「6 px 動いたら `started = true`」の 1 か所。
- **並べ替え 4 種は 1 型**。`Resolve*Drop` → 印 → `Finish*Drag` の 3 段が同型なので、
  `kind` で `Resolve` / `Finish` の関数ポインタ表を引く（現状の `propose` ラムダの一般化）。
- **`CancelDrag` は `drag_ = {}`**。`pendingUnmark_` は別のフラグのまま（ドラッグの
  状態ではなく「離したら印を捨てる」の約束）。
- **`OnMouse` は `Down` / `Move` / `Up` の 3 関数に分け、それぞれ `std::visit`**。
  各 visitor は現状の分岐の中身をそのまま移す。挙動は 1 つも変えない。
- `PointerOver` の「光らせない 4 つ」の判定は `std::holds_alternative` の列に置き換える。

**影響ファイル**: `ui/AppUi.h`, `ui/AppUiMouse.cpp`（`AppUi.cpp` は描画側で `drag_` を
読む箇所のみ）。

**検証**: `test_appui` 123 ケースが無変更で通ること。ドラッグの検査はすべて
`OnMouse` の入出力で書かれているので、内部表現の変更を検出しない。

**完了条件**: `AppUi.h` のドラッグ関連フィールドが `drag_` と `pendingUnmark_` の 2 つ。
`OnMouse` 本体 ≤ 200 行。

### A-5 モデルへの UI 書き戻しを 1 つの構造体に

**現状**: `Pane::listHeight` / `rowHeight` / `rowsPerPage` / `listArea` / `tabRows` /
`tabRowsPerPage` を `AppUi` が毎フレーム書き、`App` が `PageUp/Down`・
`EnsureCursorVisible`・`CursorRowAnchor` で読む。core → ui の逆流が 6 フィールドに散る。

**変更後**: `Pane::Viewport viewport;` にまとめる（意味も値もそのまま）。完全に無くすことは
できない（`App` は行の座標を自分では持てない）ので、**逆流が 1 か所に見える形**にする
だけ。`Tab::scroll` は据え置き（あれはモデルの状態）。

**影響ファイル**: `core/model/Workspace.h`, `App.cpp`, `AppCommands.cpp`, `ui/AppUi.cpp`。
**検証**: `test_app`（PageDown）、`test_appui`。**完了条件**: `Pane` の float / int
フィールドが `viewport` と `tabScroll` / `tabScrollFor` / `active` だけ。

### A-6 走査ヘルパの集約

**現状**: §2.6。

**変更後**:

```cpp
// Workspace.h
template <class F> void Session::ForEachPane(F&& f) const;   // vector を作らない
template <class F> void Workspace::ForEachTab(F&& f);        // 全セッション
Tab* Workspace::FindTabByLoadToken(uint64_t token);           // PumpLoader 用
std::vector<Tab*> Workspace::VisibleTabs();                   // アクティブセッションの各ペインの activeTab
```

- `Session::Panes()` は**並びが要る場所**（`GotoPane`、`PaneInDirection`、チューザ）に
  残し、単に舐めるだけの 12 か所を `ForEachPane` に置き換える。
- `App.cpp` の 3 重ループ 6 か所を `ForEachTab` / `FindTabByLoadToken` / `VisibleTabs` に。

**検証**: `test_workspace` に 3 つの新ケース。**完了条件**: `App.cpp` から
`for (const std::unique_ptr<Session>& s : workspace_.sessions)` が消える。

### A-7 `const` の欠けと重複した表示名

- `App::workspace() const`、`App::HasBookmark(...) const`、`App::DisplayName(...) const`
  を足し、`AppUi.cpp` の `const_cast<App&>` 3 か所を消す。
- `App::DisplayName(const Tab&)` は `listing.title` があればそれ、無ければ
  `DisplayNameOf(tab.path)`、に畳む（現状同じ判断が 2 か所にある）。`Tab::title()` は
  列挙前にも答える必要があるので別物のまま（CLAUDE.md「履歴の表示名」）。

**検証**: `test_app`（表示名）。

### A-8 `Strings` の引き方

**現状**: `Get(std::string_view)` が `map_.find(std::string(key))`。`Format` は
`"{" + std::to_string(i) + "}"` を毎回作る。**表示中の全画面から毎フレーム呼ばれる**。

**変更後**: C++20 の透過的ハッシュで `string_view` のまま引く。

```cpp
struct SvHash { using is_transparent = void;
                size_t operator()(std::string_view s) const noexcept { return std::hash<std::string_view>{}(s); } };
std::unordered_map<std::string, std::string, SvHash, std::equal_to<>> map_;
```

`Format` の差し込みトークンは `"{0}".."{9}"` の固定表。`Label` も同じ引き方。

**検証**: `test_strings` 無変更。**完了条件**: `Strings::Get` にヒープ割り当てが無い
（見つからなかったときの `fallback` 除く）。

---

## 4. 変更仕様 ─ B. 冗長処理

### B-1 `Tab::Rebuild` の比較関数から割り当てを消す【最優先】

**現状**: §2.5。

**変更後**: 並べ替えの前に**キー列を 1 回だけ作る**。

```cpp
// PathUtil.h に追加。既存の Extension()（小文字の std::string を返す）はこの上に置く
std::string_view ExtensionView(std::string_view name);  // 割り当てなし。小文字化はしない

// Tab::Rebuild の中
struct SortKeyCache { std::string_view ext; };           // entries[i].name の中を指す
std::vector<SortKeyCache> keys(entries.size());
for (...) keys[i].ext = path::ExtensionView(entries[i].name);
// 比較: path::NaturalCompareIgnoreCase(keys[lhs].ext, keys[rhs].ext)
```

- **`NaturalCompare` の大文字小文字を畳む版**を足す（`ToLowerAscii` の結果を比較して
  いたのと同じ順序になること。ASCII のみ畳む ─ 現状と同じ）。
- **絞り込み**も同じ: `needle` が空でなければ、`entries[i].name` を小文字化した写しを
  **`Tab` に 1 本だけ**持つ（`std::vector<std::string> lowerNames_`、`listing` が届いたときに
  作り、`DropListing` で捨てる）。`Ctrl+F` の打鍵ごとに 10 万本作り直すのをやめる。
  ※ **これは §1-6 に反しない** ─ フルパスではなく名前の写しで、`listing` と寿命が同じ。
  絞り込みを一度も使わないタブでは作らない（初回の非空 `needle` で作る）。
- `SortKey::Size` / `Date` / `Age` / `Name` の比較は割り当てが無いので据え置き。

**影響ファイル**: `core/base/PathUtil.h/.cpp`, `core/model/Workspace.h/.cpp`。

**検証**: `test_tab`（拡張子順・絞り込みの既存ケース）+ `test_pathutil` に
`ExtensionView` と `NaturalCompareIgnoreCase` のケース。**性能スモーク**として
`test_tab` に「10 万件を拡張子順で `Rebuild` して例外なく終わる」を足す（時間は
assert しない ─ CI の揺れで落ちるテストにしない。時間は §6 で手元計測）。

**完了条件**: 比較関数の中に `std::string` の構築が無い（コードレビューで確認）。

### B-2 `PaintList` の行ごとの重複

| 項目 | 変更 |
| --- | --- |
| (a) `fs::EntryPath` を 2 回 | `App::FolderSizeFor(const std::string& full, const fs::Entry&)` にシグネチャを変え、`PaintList` が組んだ `full` を渡す |
| (b) `AutoCountEligible` を毎フレーム | `FolderSizeCache` に「訊かない」印を追加: `SizeState::Skipped`（自動では数えない。頼まれれば数える）。`FolderSizeFor` が不適格と判断したら `Skipped` で表に入れ、次のフレームからは `Get` の 1 回で終わる。`RefreshRoots` 後（`RootsChanged`）と `[ui] folder_sizes` の変更で捨てる。**`Skipped` は `<DIR>` のまま**（列の見た目は変わらない）。`Ctrl+Shift+S` の `force` は `Skipped` も数える |
| (c) `path::Extension(e.name)` | `ExtensionView`（B-1）に置き換え。表示は小文字化しない（現状も `Extension` が小文字を返しているので**見た目が変わる**。CLAUDE.md「拡張子は書かれたまま残す」に合わせて元の綴りで出す方が正しいが、これは見た目の変更なので**B-2 (c) だけは別 PR に切り出し、変更を明記する**） |
| (d) `str.Get("ui.dir_marker")` 等 | A-8 で解消 |
| (e) `FormatDateTime` の `localtime_s` | 据え置き。行数 ≤ 60 で計測上無視できる。**列挙時に整形して `Entry` に持たせる案は採らない**（`Entry` は列挙の答えであり表示形式を持たない） |

**検証**: `test_foldersize`（`Skipped` の遷移 3 ケース追加）、`test_appui`（列の描画）。

### B-3 F1 一覧とチューザの行を毎フレーム組み直さない

**現状**: `PaintKeyHelp` が 134 行の `lines` を毎フレーム構築し、`KeyMap::ChordsFor` が
`order_` を線形走査する（134 × バインド数 ≈ 2 万回の比較 / フレーム）。

**変更後**:

- `KeyMap` に `std::unordered_map<Cmd, std::vector<Chord>> byCommand_` を `order_` と併せて
  維持し、`ChordsFor` を O(1) にする。`Bind` / `Unbind` / `UnbindCommand` / `LoadDefaults`
  の 4 か所で両方を更新する（`order_` は表示順のため残す）。
- F1 の `lines` は **`keyHelp_` が立ったとき**と**キーマップ／言語が変わったとき**に
  `App` 側で組む（`App::keyHelpLines()`、`KeyEditor` の変更と `LoadLanguage` で無効化）。
  `PaintKeyHelp` は列の幅を測って描くだけ。
- チューザ（`PlacePicker` / `CommandPalette`）はすでに `Open` 時に行を組んでいる。据え置き。

**検証**: `test_keymap` に `byCommand_` と `order_` が食い違わないケース（Bind → Unbind →
Bind の往復）。`test_appui` の F1 のケース無変更。

### B-4 `Region` から `std::string` を抜く

**現状**: `Region::path` を `PaintSidebar` が行ごとにコピー。`regions_.clear()` が
文字列の解放を伴う。

**変更後**: `Region` を POD に。サイドバーの行は `section + index` で `App` の配列を引ける
（`quickAccess()[i].path`、`bookmarks[i].path`、`roots()[i].path`）ので、クリック時に
`app_.SidebarPath(section, index)` で解決する。`Hit::Crumb` のパスも同じく
`index` からパンくずを組み直す（`PaintPathBar` の分割を関数に切り出し、両方から呼ぶ）。

**検証**: `test_appui`（サイドバーのクリック・ドロップ先）。**完了条件**:
`sizeof(Region)` に `std::string` が含まれない。

### B-5 ステータス期限の全面再描画を 1 回に

**現状**: `SetStatus` から 4 秒間、500 ms ごとに全面再描画。

**変更後**: `WinWindow::Paint` は `statusUntilMs - now` を残り時間として **1 回だけ**
タイマーを張る（`SetTimer` は同じ id なら張り直し）。期限のフレームで
`statusExpired()` が真になり、`PaintStatusBar` が右を空にする。`App` 側は無変更。

**検証**: 手動（`Ctrl+C` の後、4 秒で消える）。`test_app` の `statusExpired` は無変更。

### B-6 `D2DRenderer::UpdateTheme` の毎フレームコピー

**変更後**: レンダラが持つのはフォントの 4 項目（`fontFamily`, `monoFamily`, `fontSize`,
`uiScale`）だけにし、`FontSpec` 構造体で比較・代入する。`Theme` 全体を持たない。
（同じ関数の隣にある `ApplyDarkTitleBar()` はすでに変化時だけ `DwmSetWindowAttribute` を
呼ぶ形になっており、対象外。）

**検証**: 手動（テーマ切り替え、`Ctrl++`）。

### B-7 ステータス行の文言（任意）

`fileOpStatus()` / `searchStatus()` / `folderSizeStatus()` / `folderSizeDetail()` は
毎フレーム `Format` する。CLAUDE.md「その文言は覚えずに毎回組み立てる」の理由は
«依頼時に組むと古くなる» なので、**走り始め・完了のたびに組み直す**形なら約束は
守れる。ただし 4 本の `Format` は計測上小さいので、**A-1 の部品化のついでに各部品が
`status()` を返す形に揃えるだけ**とし、キャッシュは入れない。

### B-8 `WinDirectoryWatcher` の 100 ms 起床

監視が 0 件・デバウンス待ちも 0 件のときは `GetQueuedCompletionStatus` の待ち時間を
`INFINITE` にする（何も張られていない間、10 回/秒の空回りをやめる）。デバウンス中は
現状の 100 ms のまま。

**検証**: 手動（監視の無い仮想フォルダだけを開いてプロセスの CPU 時間を見る）。

---

## 5. 変更仕様 ─ C. リソース

### C-1 ワーカーの遅延起動

A-2 の `JobQueue` が「最初の `Request` でワーカーを起動する」を既定に持つので、
`App::Init` の 4 つはオブジェクトだけ作りスレッドは作らない。`WinIconProvider` が
すでにしていることの一般化。

- **本数は変えない**（§1-5）。
- **共有プールにはしない。** `FileOpQueue` は `SHFileOperation` でスレッドを占有するので
  他と混ぜられず、検索とサイズの本数の違いには理由が書かれている。
- 「空のまま N 秒で畳む」は**入れない**。`FolderSizeJob` は描画のたびに頼まれるので
  畳んでも次のフレームで起きる。

**完了条件**: 起動直後のスレッド数（タスクマネージャーまたは
`Get-Process kite | % Threads`）が UI + Loader 2 + Watcher 1 + ランタイム分。

### C-2 `MeasureText` キャッシュの全消し

**現状**: 4,096 件で `clear()`。10 万件のフォルダをスクロールし続けると 4,096 行ごとに
見えている行を全部測り直す（`IDWriteTextLayout` の生成）。

**変更後**: 2 世代（`fresh_` / `stale_`）。上限で `stale_ = move(fresh_)` にし、引くときは
`fresh_` → `stale_` の順、`stale_` で当たれば `fresh_` へ昇格。いつでも直近 4,096〜8,192 件が
残る。

**検証**: 手動。`FakeRenderer` は計測を固定値で返すので対象外。

### C-3 触らないもの（確認済み）

| 項目 | 理由 |
| --- | --- |
| `IconCache` / `FolderSizeCache` の上限 4,096 | 頼むのは画面に出ている行だけなので届かない |
| `ShellPipe` の 100 ms 自己起床 | メニュー表示中だけ。`MWMO_INPUTAVAILABLE` の罠が理由（CLAUDE.md） |
| `hasPendingRequests()` の追加 1 フレーム | 無いとキーボードで開いたフォルダのアイコンが出ない |
| D3D11 / DXGI のワーキングセット約 60 MB | ROADMAP P4-1。先に計測 |

---

## 6. 計測（Phase 0）

変更の前後で同じ数字を取る。**目標値に届かない項目は、実施せずに理由を記録する。**

| 指標 | 取り方 |
| --- | --- |
| 1 フレームの所要時間 | `WinWindow::Paint` の前後で `QueryPerformanceCounter`。`KITE_PROFILE` 定義時だけ `OutputDebugString` に出す（既定 OFF、リリースに残さない） |
| 1 フレームのヒープ割り当て回数 | 同じ区間で `_CrtSetAllocHook` のカウンタ（Debug 構成のみ） |
| `Tab::Rebuild` の時間 | `test_tab` の性能スモーク（10 万件・拡張子順・絞り込み 1 文字）を手元で `--filter tab.` で走らせ、`QueryPerformanceCounter` で計測した値を本文書 §8 に記録 |
| 起動直後のスレッド数 | `(Get-Process kite).Threads.Count` |
| CPU 時間（アイドル 60 秒） | `(Get-Process kite).TotalProcessorTime` の差分。B-5 / B-8 の効果 |

計測環境: 開発機（architecture.md §11 と同じ）。1 万件と 10 万件のフォルダを
`tests/` の `FakeFileSystem` ではなく実ディスクに用意する（`fsutil file createnew` で可）。

---

## 7. 実施順序

各フェーズは PR 単位。**1 PR = 1 項目**を原則にし、`ctest --preset release` と
`doxygen docs/Doxyfile`（警告 0）と CI が通ってから次へ。

| Phase | 項目 | 規模 | 依存 | 状態 |
| --- | --- | --- | --- | --- |
| 0 | §6 の計測基盤と基準値 | S | ─ | 部分（§8。`KITE_PROFILE` は入れていない ─ 下記） |
| 1 | B-1（`Rebuild`）、A-8（`Strings`）、B-6（Theme コピー）、B-4（`Region`）、B-3（F1 / `ChordsFor`）、B-5（期限タイマー）、C-2、B-8、A-6、A-7、A-5 | 各 S | 互いに独立 | **完了** |
| 2 | A-2（`JobQueue`）→ C-1（遅延起動） | M | ─ | **完了**（`WinIconProvider` を除く。下記） |
| 3 | A-3（`Invalidate` 一本化） | M | A-1 の前に済ませる（部品に散らないように） | **完了** |
| 4 | A-1（`App` 分割） | L（5 PR） | A-2, A-3, A-6 | **`FolderSizes` / `Searching` のみ。** 残り 3 つは見送り（下記） |
| 5 | A-4（ドラッグ状態の variant） | M | ─（Phase 1 の後ならいつでも） | **完了** |
| 6 | B-2 (b)（`Skipped`）、B-2 (a) | S | A-1 | **完了**（B-7 は下記） |
| 別 | B-2 (c)（拡張子列の綴り。見た目が変わる） | S | 変更として告知 | 未着手 |

**Phase 0 について**: `KITE_PROFILE` の計測基盤は入れていない。1 フレームの所要時間も
割り当て回数も、入れて測るより先に「毎フレーム同じ答えを組み直している場所」を
潰すほうが確実で、その場所は §2.3 の表がすでに名指ししていた ─ 計測のための
コードをリリース構成から外す仕掛けごと、まだ誰も読んでいない数字のために足すことに
なる。Phase 2 以降で必要になったら入れる。代わりに、外から測れるもの（`Rebuild` の
時間、アイドルの CPU 時間、スレッド数）は §8 に実測で入れてある。

Phase 1 だけで §0 の「1 フレームの割り当て半減」と「`Rebuild` の割り当て 0」が
達成できる見込み。Phase 2〜4 が「設計の見直し」の本体。

---

## 8. 受け入れ基準（全体）

- `ctest --preset release` 全通過、`doxygen` 警告 0（言語未更新の 1 件を除く）、
  `/W4` 警告 0、CI の層分離 grep 通過。
- 機能・キー割り当て・`settings.ini` / `keys.ini` / `sessions.ini` / `bookmarks.ini` の
  書式・画面の見た目に変更が無い（B-2 (c) を除く）。`test_appui` の「どこに何色を
  塗ったか」の検査がそれを保証する。
- §0 の表の目標値を §6 の方法で確認し、ここに実測値を追記する。
- `docs/architecture.md` §2（ファイル構成）と §5（非同期の作り）を A-1 / A-2 の結果に
  合わせて更新する。CLAUDE.md は「なぜ」の文書なので、**理由が変わらない限り触らない**
  （関数名の参照だけ追随させる）。

### 実測値

Phase 1 完了時点（開発機、Release、`build/Release`）。

| 指標 | 変更前 | 変更後 | 取り方 |
| --- | --- | --- | --- |
| `Rebuild` 10 万件（拡張子順 + 絞り込み 1 語） | 505 ms | 280 ms | `kite_tests --filter tab.rebuilds_a_large` を 3 回、2 回目以降の中央値 |
| アイドル 20 秒の CPU 時間 | ─ | 0 ms | `(Get-Process kite).TotalProcessorTime` の差分（起動から 30 秒置いてから） |
| 起動直後のスレッド数（ランタイム込み） | 20 本 | 15 本 | `(Get-Process kite).Threads.Count`。消えるのはファイル操作 4 + 検索 1 |
| `host_.Invalidate()`（`core/app`） | 100 か所 | 10 か所 | `git grep -c "Invalidate()" src/core/app` |
| `AppUi::OnMouse` | 620 行・ドラッグのフィールド約 30 個 | 39 行・`drag_` と `pendingUnmark_` の 2 つ | ─ |
| ワーカー 4 クラスの `.cpp` 合計 | 731 行 | 542 行 | `JobQueue.h` は 252 行（うち約 120 行が説明） |
| `App.h` / `App.cpp` | 1,236 / 2,820 行 | 1,220 / 2,549 行 | 目標未達。理由は §8 の «A-1 の残り 3 つ» |
| テスト | 25 スイート・686 ケース | 30 スイート・886 ケース | ─ |

1 フレームの所要時間と割り当て回数は測っていない（Phase 0 の項を参照）。Phase 1 が
実際に消したのは、数えられる形では次の 4 つ:

- 並べ替え 1 回あたり約 700 万回の `std::string` 構築（10 万件・拡張子順）
- 絞り込みの打鍵 1 回あたり項目数ぶんの `std::string` 構築
- 1 フレームあたり `Strings::Get`/`Label` の呼び出し回数ぶんの鍵の写し
- F1 表示中の 1 フレームあたり 134 行の再構築と約 2 万回の線形探索

### 計画からの逸脱（実施時に判断したもの）

| 項目 | 計画 | 実際 | 理由 |
| --- | --- | --- | --- |
| B-1（絞り込み） | `Tab` に小文字化した名前の写しを 1 本持つ | 写しを持たず `utf8::ContainsLowerAscii()` で直接探す | 写しは `listing.entries` が外から書き換わるたびに無効化が要る（`push_back` する場所が 4 つある）。件数が同じで中身だけ違う一覧に当たると、絞り込みが黙って古い答えを返す。割り当てを消すのが目的なら、写しを作らない探索のほうが無効化の問題ごと無い |
| B-1（比較） | `NaturalCompareIgnoreCase` を足す | 足さない | `NaturalCompare` は `FoldCp` ですでに ASCII の大文字小文字を畳んでいた |
| B-3 | `byCommand_` と `order_` を両方維持する | `order_` を落とす | `ChordsFor` が `order_` の唯一の読み手だった。`ToIni` も F1 も設定画面もコマンド表を順に舐めて 1 コマンドずつ訊くので、表示順は «コマンドの中の割り当て順» だけで足りる。§9 の «表示順に要る» は `ChordsFor` が `order_` を読んでいた頃の話で、索引 1 本にすれば食い違いようが無い |
| A-7 | `DisplayName(const Tab&)` を `listing.title` と `DisplayNameOf` に畳む | 畳まない | 畳むと `vfs::LabelKey` の判定が `listing.title` の後ろに回り、英語 Windows の「PC」がシェルの言語で出る（CLAUDE.md「名前は Kite の言語で呼ぶ」）。重なっているのは vfs の末尾名とパス表示名の 2 段だけ |
| A-7 | `workspace() const` / `HasBookmark() const` を足す | すでに在った | `AppUi` が持つのは `App&` なので `const_cast` は最初から何も外していなかった。3 つとも削除しただけ |
| A-2 | `Request` の戻り値でトークンを配る | 配らない（`void`） | 検索はワーカーが走り出す **前** に `active_` を立てる必要があり、積んでから受け取るのでは間に合わない。フォルダのサイズは «代» で、そもそもトークンを使わない ─ 何で依頼を識別するかはクラスごとに違うので、採番も各クラスに残した |
| A-2 | `WinIconProvider` も骨格に乗せる | 乗せない | あちらはバッチ単位（1 依頼 = 64 件）で、しかも «ホストが入れ替わったら、まだ UI が回収していない結果ごと捨てる» という後始末がある。骨格には «積んだ結果を捨てる» 口が無く、それを 1 か所のために足すのは骨格を歪める ─ 遅延起動（C-1）はもともとあちらが先にやっていたことでもある |
| A-1 | `Pickers` / `FileOps` / `Prompting` も切り出す | 切り出さない | 下記 |
| B-7 | 各部品が `status()` を返す形に揃える | `FolderSizes` / `Searching` のみ | 切り出した 2 つはそうなった。残り 3 つを切り出さない以上、そこだけ揃える意味が無い |

---

### A-1 の残り 3 つを見送った理由

`FolderSizes` と `Searching` は素直に外れた ─ どちらも **葉**で、外から見た口が
«表 1 つ・ワーカー 1 つ・文言 1 つ» に収まる。受け取る依存もファイルシステムと
表示文字列（と、サイズはドライブ一覧）だけで、`App&` を持たずに済んだ。

残り 3 つはそうではない。実際に数えた back-call は:

| 部品 | `App` へ戻る必要があるもの |
| --- | --- |
| `Pickers` | `DisplayName` / `DisplayNameOf`（行の表示名）、`SetStatus`、`CloseAllOverlays`、`FocusPane`、`OpenPath`、`Execute` |
| `FileOps` | `SetStatus`、`ReportFailure`、`BeginPrompt`（削除の確認）、`RefreshTabsShowing`、`ClearCutMarks`、`FolderSizes::cache()`、`IIconProvider::Invalidate` |
| `Prompting` | `ApplyPrompt` の分岐ぜんぶ（移動・名前変更・作成・削除）、`SetStatus`、`EnsureCursorVisible`、`SyncSearchQuery` |

これを «`App&` を持たない部品» にするには、6 本前後の `std::function` を構築時に
渡すことになる。**それは大きなクラスを、大きなクラス + コールバックの網に
置き換えただけ**で、読みやすさは下がる ─ `FinishFileOp` が «ステータス行に言う»
のか «どこかへ飛ぶ» のかが、渡された関数の中身を追わないと分からなくなる。

しかも計画自身の言葉で、そこに残っているものは配線である:
「`App` は「所有・配線・`Execute`・入力の振り分け」だけを持ち」。
`OpenPlacePicker` も `QueueFileOp` も `ApplyPrompt` も、判断そのものは
`PlacePicker` / `fs::FileOpQueue` / `TextField` という別のクラスがすでに持っていて、
`App` に在るのはそれらを繋ぐ部分だけだった。

**正しく切るには、ステータス行と失敗の報告と一覧の取り直しを «出来事» として
受け取る口を先に作ることになる** ─ それは「変えるのはコードの形だけ」（§1）の
外側にある設計変更なので、この文書の範囲では行わない。

そのため **§0 の `App.h` ≤ 600 行 / `App.cpp` ≤ 1,200 行は達成していない**
（1,220 / 2,549 行）。見積もりのほうが、切れる前提で立っていた。

---

## 9. やらないこと（検討して見送ったもの）

| 案 | 見送る理由 |
| --- | --- |
| 差分再描画・保持型ウィジェット | 全面再描画が 1 ms 未満。`AppUi` の「描きながら当たり判定を積む」設計の根拠そのもの |
| `IDWriteTextLayout` を行ごとにキャッシュ | 計測前に入れない。行数 ≤ 60 で `DrawTextW` は十分速い |
| ワーカーの共有プール | `SHFileOperation` がスレッドを占有する。検索 1 本・サイズ 2 本の理由が別々 |
| `Entry` に整形済み文字列・フルパスを持たせる | 10 万件で 1 件ずつ代金を払う（CLAUDE.md `Entry::address`） |
| ホバー行番号のキャッシュ | ホイールで一覧が動いたときに壊れる（CLAUDE.md） |
| `App::Execute` の分割 | 表であることが設計 |
| `Strings` を列挙子で引く（文字列キーをやめる） | `lang.<code>.ini` で利用者が上書きできる仕組みが文字列キー前提 |
| `KeyMap::order_` の廃止 | 表示順（`keys.ini` の並び）に要る |
