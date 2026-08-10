# CLAUDE.md

Kite — Windows エクスプローラーを置き換えることを目的とした、C++20 製の軽量ファイラー。
このファイルは、このリポジトリを編集する人（人間・エージェントを問わず）向けの作業指針。

ロードマップと未実装項目は [docs/ROADMAP.md](docs/ROADMAP.md) を参照。

## ビルドとテスト

**Developer PowerShell for VS 2022** から実行する。CMake・Ninja・ctest はすべて
Visual Studio に同梱されているので、追加インストールは不要。

```bash
cmake --preset release
cmake --build --preset release
ctest --preset release
```

生成物は `build/release/` に `kite.exe`、`kite_shellhost.exe`、`kite_tests.exe`。
同じ 3 コマンドで使える `debug` プリセットもある。

`kite.exe` は `kite_shellhost.exe` を**自分と同じフォルダから**起動する。片方だけを
別の場所へコピーするとコンテキストメニューが出なくなる（「シェルメニューを表示
できませんでした」と出る）。

開発者プロンプトでない普通の PowerShell からは、MSVC 環境を自前で読み込む
`build.ps1` を使う。

開発者プロンプト以外で `cmake --build build/release` を直接叩くと
「`<mutex>` を開けません」という紛らわしいエラーになる。これはコードの問題ではなく
`INCLUDE` 環境変数が未設定なだけ。

1 スイートだけ回す高速ループ:

```bash
build/release/kite_tests.exe --filter app.
```

`--list` で全テスト名を列挙できる。

## アーキテクチャ

3 層に厳密分離。これがこのプロジェクト最大の設計制約であり、将来他 OS へ移せる根拠。

```
src/core/       OS 呼び出しを一切行わない。例外は core/base/Platform.h が宣言する
                5 つの自由関数のみ。ここに <windows.h> を include してはならない。
src/ui/         レイアウト・描画・ヒットテスト・ドラッグ処理。描画は抽象 ui::Renderer
                経由のみ。
src/platform/   Windows ヘッダが現れる唯一の場所。
tools/shellhost/ kite_shellhost.exe。サードパーティのシェル拡張が動く唯一のプロセス。
tests/          kite_core だけをリンクする。OS 非依存であるべき core/ ui/ に Windows
                ヘッダが紛れ込めばテストのビルドが壊れる。これが分離の防波堤。
```

CMake ターゲットは 3 つ。`kite_core` = `core/` + `ui/`、`kite` = `kite_core` +
`platform/`（`ShellMenu.cpp` を除く）、`kite_shellhost` = `tools/shellhost/` +
`ShellMenu.cpp` + `ShellPipe.cpp` + `WinUtf.cpp`。

**`ShellMenu.cpp` を `kite` ターゲットに足してはならない。** これが `IContextMenu` を
触る唯一のファイルであり、`kite.exe` にリンクした時点で隔離の意味が消える。

他 OS へ移植する際に実装するのは以下だけで、それ以外は書き換えない:

| 境界 | ヘッダ |
| --- | --- |
| 描画 | `ui/Renderer.h` |
| ファイルシステム | `core/fs/FileSystem.h` |
| 変更通知 | `core/fs/DirectoryWatcher.h` |
| シェル／クリップボード | `core/app/Host.h` の `IShellIntegration` |
| ウィンドウ機能 | `core/app/Host.h` の `IHost` |
| ファイル入出力・時刻・ロケール | `core/base/Platform.h` |

### データモデル

```
Workspace -> Session[]（1 つがアクティブ）
Session   -> SplitNode ツリー。葉が 1 つの Pane を持つ
Pane      -> Tab[]（1 つがアクティブ）
Tab       -> パス + 一覧 + 表示状態 + 履歴
```

全セッションが常駐しているので、切り替えはインデックスの移動だけで済む。
背面に回ったセッションの非アクティブタブは一覧データを解放し、常駐メモリを
画面に映っている量に比例させる。

### 制御フロー

`App`（`core/app/App.cpp`）が唯一のディスパッチ点。テキスト入力欄を除き、UI は生の
キー入力に一切反応しない。キーマップが和音を `Cmd` に変換し、`App::Execute` が実行する。
「どの操作にもキーを割り当てられる」が機能ではなく構造として保証されるのはこのため。

ディレクトリ列挙は UI スレッドで絶対に動かさない。冷えたネットワーク共有は 1 回の
`FindFirstFile` で数秒ブロックする。リクエストは `DirectoryLoader` を通り、結果は
`IHost::Wake()` の後に `App::PumpLoader` が回収する。

### シェル拡張の隔離

コンテキストメニューだけは自プロセスで処理しない。

```
kite.exe                                  kite_shellhost.exe
  App::ShowContextMenuAt
  → WinShell::ShowContextMenu
    → ShellHostClient  ── 名前付きパイプ ──→  ReadPipeFrame
                                              ShowShellContextMenu
                                                QueryContextMenu
                                                TrackPopupMenu
                                                InvokeCommand
       応答（出せた／実行した／失敗） ←───────  WritePipeFrame
```

送るのはパスと画面座標だけ、受け取るのは結果の種別だけ。書式は
`platform/win/ShellMenuProtocol.h`（Windows 非依存。`tests/test_shellproto.cpp` が
直接検証している）。

知っておくべき点:

- **メニューが閉じるまで `ShowContextMenu` は戻らない。** ただし待っている間も
  `ShellPipe` が呼び出し側のメッセージを回すので、ウィンドウは再描画され続ける。
  クライアント領域への入力だけは捨てる（同一プロセスの `TrackPopupMenu` と同じ挙動）。
  `WM_APP` 以降は**あえて捨てずキューに残す** ─ 列挙完了通知をここで処理すると
  `App::ShowContextMenuAt` の内側から `App` に再入する。
- **ホストは最初のメニュー要求で起動する。** 起動パスに COM を置かない方針のため。
  一定時間使われなければ自分で終了し、拡張 DLL の分のメモリを返す。落ちた場合と
  区別せず、次の要求で黙って起動し直す。
- **ダイアログの親は Kite 本体のウィンドウ。** `TrackPopupMenu` はホストの隠し
  ウィンドウ（呼び出しスレッド所有が必須）だが、`InvokeCommand` に渡す `hwnd` は
  Kite 側にしている。隠しウィンドウを親にすると削除の確認や進捗が背後に回る。
- **ホストはジョブオブジェクトに入れる。** Kite が強制終了されても道連れにするため。
  ただし `SILENT_BREAKAWAY_OK` 付き ─ メニューが起動したプロセス（7-Zip の展開など）
  までジョブに入れてしまうと、Kite 終了時に一緒に殺してしまう。

## 追加のしかた

**コマンドを足す** — `KITE_COMMAND_LIST`（`core/input/Commands.h`）に 1 行、
`App::Execute` に `case` を 1 つ、両言語テーブル（`core/i18n/Strings.cpp`）にラベル、
たいていは `core/input/KeyMap.cpp` に既定バインドも足す。ラベル漏れは
`test_strings.cpp` が、和音の重複は `test_keymap.cpp` が落として教えてくれる。

**ユーザーに見える文字列を足す** — `core/i18n/Strings.cpp` の `kEn` と `kJa` の両方に
追加する。それ以外の場所に表示文字列を直書きしない。

**テーマ色を足す** — `Theme` にフィールド、`Theme::Dark()` と `Theme::Light()` に既定値、
`Theme::ApplyIni` に `ReadColor` の行を追加。

**言語を足す** — 再ビルド不要。利用者が設定フォルダに `lang.<code>.ini` を置けばよい。
組み込みテーブルは `en` と `ja` のみ。

## 開発手順

コードを変更したら、次の 2 つは必ず満たしてから完了とすること。

### 1. ヘッダに Doxygen コメント（必須）

`src/**/*.h` に宣言を追加・変更したら、必ず Doxygen コメントを書く。記述は日本語。
対象はヘッダのみで、`.cpp` 内部の関数は対象外。

関数・メソッドには最低限これを書く:

- `@brief` … 何をするか 1 行。必須
- `@param[in]` / `@param[out]` / `@param[in,out]` … 全引数に必須。方向指定を省略しない
- `@return` … 戻り値がある場合は必須。「失敗時 nullptr」のような異常系まで書く

型・構造体には `@brief` を、public メンバには `///<` を付ける（private メンバは
`EXTRACT_PRIVATE = NO` のため任意。自明でないものだけでよい）。加えて必要に応じて:

- `@pre` … 事前条件（`Session::PaneInDirection()` が直前のレイアウトを要求する、など）
- `@note` … スレッド制約や呼び出し順の注意
- `@todo` … 既知の未対応・制限。憶測で書かず、実際に未実装な箇所だけに書く

基底クラスと同じ説明でよい override は `@copydoc` で参照する。書式の見本は
`core/fs/FileSystem.h` と `ui/Renderer.h`。

**落とし穴**: `///<` は複数宣言子の行では最後の 1 つにしか付かない。
`float l = 0, r = 0;  ///< 端` と書くと `l` が未文書化になる。1 行 1 宣言に分けること。

チェックはリポジトリのルートから:

```bash
doxygen docs/Doxyfile
```

出力は `build/doxygen/html/index.html`、`@todo` の一覧は同 `todo.html`。
`WARN_IF_UNDOCUMENTED` と `WARN_NO_PARAMDOC` を有効にしてあるので、`@brief` や
`@param` の書き漏れは警告として出る。**警告ゼロを維持すること。** ただし
`The selected output language "japanese" has not been updated` の 1 件だけは
Doxygen 側の翻訳が未更新なことによる無害な通知で、消せない（無視してよい）。

doxygen が無ければ `winget install DimitriVanHeesch.Doxygen`。クラス図は
Graphviz に依存するが、未インストール環境でも通るよう `HAVE_DOT = NO` にしてある。
図が欲しければ `winget install Graphviz.Graphviz` を入れて `YES` に変える。

### 2. core 層は単体テストを書く（必須）

`src/core/` に振る舞いを追加・変更したら、`tests/` にテストを足すこと。
`src/ui/` も抽象 `Renderer` の上にあるのでテスト可能で、判断ロジックを含むなら
同様に書く。`src/platform/` は OS に触れるため対象外。

テストは `kite_core`（= `core/` + `ui/`）だけをリンクする。OS 非依存であるべき層に
Windows ヘッダが紛れ込めばテストのビルドが壊れる ─ レイヤ分離そのものの検査を
兼ねているので、この依存関係を崩さないこと。

OS 境界のフェイクは `tests/Fakes.h` に揃えてある（`FakeFileSystem`、`FakeShell`、
`FakeHost`、`FakeWatcher`）。`tests/FakePlatform.cpp` が `core/base/Platform.h` の
5 つの自由関数をメモリ上で実装するので、実ディスクにも実時計にも触れずに `App` を
端から端まで動かせる。非同期ローダーは実スレッドで動くため、ウィンドウと同じように
`test::PumpUntilSettled()` でポンプすること。

スイートを足したら `CMakeLists.txt` の `KITE_TEST_SUITES` にも名前を追加する
（ctest に 1 スイート 1 エントリで登録される）。

```bash
ctest --preset release
build/release/kite_tests.exe --filter app.   # 1 スイートだけ回す
```

判定に使うのは自作の 100 行のハーネス（`tests/TestFramework.h`）。
`KITE_EXPECT` / `KITE_EXPECT_EQ` / `KITE_EXPECT_NE` / `KITE_EXPECT_NEAR` /
`KITE_EXPECT_FALSE` / `KITE_FAIL` がある。

## 規約

- 文字列は**全面的に UTF-8**。UTF-16 は `platform/win/` の内部にのみ存在し、
  `WinUtf.h` が境界で変換する。
- コメントは *why* を書く。*what* は書かない。コードの逐語訳をしない。
- インデント 4 スペース、100 桁上限。`.clang-format` が正。
- `/W4 /permissive-` で警告ゼロを維持する。
- サードパーティ依存なし。テストハーネスが 100 行程度の自作なのもこの方針のため。

## すでに踏んだ罠

いずれも実際に時間を溶かしたので記録しておく。

- **`<windows.h>` のマクロ**。`CreateDirectory` と `CreateFile` はマクロなので、
  ファイルシステムのインターフェースは `MakeDirectory` / `MakeFile` という名前にしてある。
  `GetObject`、`SendMessage`、`min`/`max`（`NOMINMAX` 設定済み）も同様に注意。
- **`WIN32_LEAN_AND_MEAN`** は `ole2.h` を `windows.h` から外す。`IDataObject` や
  `IDropTarget` に触れるヘッダは自分で `<objidl.h>` を include すること。
- **`ID2D1HwndRenderTarget` は使えない**。少なくとも手元の Intel ドライバでは
  `EndDraw` が `S_OK` を返しながら画面に何も出ない。40 行の最小再現コードでも同じだった。
  レンダラは D3D11 デバイス → `ID2D1Device` → `ID2D1DeviceContext` → DXGI フリップ
  スワップチェーンという現代的な経路を使っている。「単純化」して戻さないこと。
- **D2D のターゲットはウィンドウを表示した後に作る**。そのため最初の `BeginFrame` で
  遅延生成している。
- **`ReadDirectoryChangesW` のハンドル寿命**。読み取りが未完了のままハンドルを閉じると、
  後から完了通知が届く。`WinDirectoryWatcher` は全ハンドルをワーカースレッドが所有し、
  キャンセル通知が届いてから初めてエントリを解放する。`Watch`/`Unwatch` はコマンドを
  投函するだけ。
- **クラウドのプレースホルダ**。列挙中にファイル内容を読まない。ファイルを直接開かず
  シェルに委ねる。`FILE_ATTRIBUTE_RECALL_ON_OPEN` と `RECALL_ON_DATA_ACCESS` が付いた
  ファイルはクラウドにしか実体がなく、触れるとダウンロードが走る。
- **シェルアイコンは意図的に使っていない**。`SHGetFileInfo` は 1 行ごとにシェル（つまり
  クラウドプロバイダ）を呼ぶ。アイコンは `ui/Glyphs.cpp` でベクタ描画している。
- **`OleInitialize` は 100 ms 以上かかる**。ドラッグ＆ドロップの登録は初回描画から
  200 ms 後のタイマーに逃がしてある。起動パスに戻さないこと。同じ理由で
  `kite_shellhost.exe` も最初のメニュー要求まで起動しない。
- **`CMIC_MASK_UNICODE` は `<shellapi.h>` が必要**。実体は `SEE_MASK_UNICODE` で、
  `<shlobj.h>` だけでは「定義されていない識別子」になる。
- **`MsgWaitForMultipleObjects` に `MWMO_INPUTAVAILABLE` を足すと暴走しうる**。
  キューに残したままのメッセージがあると毎回即座に返るため、意図的に処理しない
  メッセージがある待機ループでは 100 % CPU になる。`ShellPipe.cpp` はこのフラグを
  使わず、代わりに 100 ms ごとに自力で起きる。
- **ジョブオブジェクトは子孫まで巻き込む**。`KILL_ON_JOB_CLOSE` だけを付けると、
  シェルメニューが起動したプロセス（7-Zip の展開など）も Kite 終了時に殺される。
  `SILENT_BREAKAWAY_OK` を必ず併せて指定する。
- **テストマクロはオペランドをコピーする**。`KITE_EXPECT_EQ` は `auto&&` ではなく
  `const auto` を使う。生存期間延長はメンバ呼び出しを貫通しないため、
  `container().front()` を参照で束縛するとダングリングし、比較が静かに壊れる。

## 現時点の弱点

- メニューを開いている間、Kite のタイトルバーは非アクティブ表示になる。前景を
  取るのはホストの隠しウィンドウなので避けられない。メニューが閉じれば戻る。
- ホストが応答しなくなった場合（拡張がフォールトではなくデッドロックした場合）、
  待機は終わらない。ウィンドウは再描画され続け、タイトルバーの × で閉じられるが、
  クライアント領域は反応しない。タイムアウトを入れていないのは、開いたままの
  メニューを勝手に消す方が実害が大きいと判断したため。
- サムネイル・検索・仮想フォルダ（ZIP、「PC」、ごみ箱）は未実装。
- キーシーケンスは 1 打鍵のみ。`Chord` は 2 打鍵に拡張できる形にしてある。
