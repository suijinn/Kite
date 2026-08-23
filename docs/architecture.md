# Kite のアーキテクチャ

Kite が**どう組まれているか**を 1 か所にまとめた文書。読む順は README →ここ→ 該当箇所の
CLAUDE.md。

| 文書 | 役割 |
| --- | --- |
| [../README.md](../README.md) | 使う人向け。何ができて、どう入れて、どのキーで動くか |
| **この文書** | 設計の全体像。層・プロセス・データモデル・境界・テスト戦略 |
| [../CLAUDE.md](../CLAUDE.md) | 編集者向けの規則。機能ごとに「なぜそう決めたか」「何をしてはならないか」 |
| [ROADMAP.md](ROADMAP.md) | 達成状況と未実装項目の優先順位 |

ここには**構造**だけを書く。個々の判断の理由（なぜ選択はカーソル移動で消えないのか、
なぜ `_copy` が ASCII なのか）は CLAUDE.md にあり、この文書からは節名で参照する。

---

## 1. プロセス構成

`kite.exe` は自分のプロセスでサードパーティのコードを一切動かさない。シェル拡張 DLL が
起き上がる経路は 3 つあり、どれも `kite_shellhost.exe` の中に隔離してある。

```
                    ┌──────────────────────────────────────┐
                    │ kite.exe                             │
                    │   UI スレッド（描画・入力）           │
                    │   DirectoryLoader ワーカー × 2        │
                    │   WinIconProvider ワーカー            │
                    │   WinDirectoryWatcher ワーカー        │
                    └───┬──────────┬──────────┬────────────┘
                        │          │          │  名前付きパイプ
        ┌───────────────┘          │          └───────────────┐
        ▼                          ▼                          ▼
 kite_shellhost.exe        kite_shellhost.exe         kite_shellhost.exe
 （メニュー用）             （アイコン用）              （列挙用）
 IContextMenu               SHGetFileInfo              IShellFolder
 TrackPopupMenu             + ADDOVERLAYS              EnumObjects
 InvokeCommand              上限 8 秒                   上限 30 秒
 IFileOperation（書庫の取り出し）
```

- **ホストは 3 つ**。メニューのホストは表示中ずっと `TrackPopupMenu` の中にいるので、
  共有するとメニューを開いている間アイコンも仮想フォルダの列挙も止まる。
- **どれも最初の要求まで起動しない。** 右クリックもシェルアイコンも仮想フォルダも使わない
  起動では、プロセスは 1 つのまま。ホストは 60 秒使われなければ自分で終了する。
- **やりとりするのは文字列と画素だけ。** PIDL もハンドルも渡らない。書式は
  `platform/win/ShellHostProtocol.h`（Windows 非依存で、`tests/test_hostproto.cpp` が直接
  検証する）。書式を変えたら `kMagic` も変える ─ 版の食い違うホストとは握手で失敗させる。
- **時間制限があるのはアイコン（8 秒）と列挙（30 秒）だけ。** どちらも中断できない
  API を呼ぶので、返ってこない相手にできるのはホストごと捨てることだけ。メニューには
  制限を付けていない（開いたままのメニューを勝手に消すほうが実害が大きい）。

詳細と、ここを触るときの注意は CLAUDE.md「シェル拡張の隔離」。

## 2. 層分離

このプロジェクト最大の設計制約。**`core/` と `ui/` は Windows を知らない。**

```
src/
  core/          OS 呼び出しを行わない。例外は core/base/Platform.h の 5 つの自由関数のみ
    base/          UTF-8 / パス / INI / 書式 / 自然順ソート / Platform.h
    fs/            IFileSystem 抽象、非同期 DirectoryLoader、DirectoryWatcher、VirtualPath
    model/         Tab → Pane → SplitNode ツリー → Session → Workspace
    input/         コマンド表・キーマップ・TextField・パス補完・型入力ジャンプ・キー設定
    i18n/          文字列テーブル（en / ja 内蔵）
    theme/         配色とメトリクス
    app/           App（唯一のディスパッチ点）、UndoStack、IconCache、
                   PickerList / PlacePicker / CommandPalette、SettingsEditor、ConfigDir
      App.cpp        状態・移動・一覧・入力欄・ファイル操作
      AppCommands.cpp App::Execute（コマンド 1 つに case 1 つ）
      AppConfig.cpp  設定とセッションの読み書き、設定画面との受け渡し
  ui/            OS 非依存。抽象 ui::Renderer に対してのみ描画する
    Renderer.h     描画プリミティブのインターフェース
    AppUi.cpp      レイアウト・描画・ヒットテスト
    AppUiOverlays.cpp F1・Ctrl+Shift+,・Ctrl+,・Ctrl+P・Ctrl+Shift+P の 1 枚もの
    AppUiMouse.cpp 当たり判定の振り分けとドラッグ
    Glyphs.cpp     シェルアイコンが届くまでのベクタ描画
  platform/win/  Windows ヘッダが現れる唯一の場所
  main_win.cpp   起動の入口（単一インスタンスの振り分け）
tools/shellhost/ kite_shellhost.exe。他人のコードが動く唯一のプロセス
tests/           kite_core だけをリンクする
```

CMake ターゲットは 3 つ:

| ターゲット | 中身 |
| --- | --- |
| `kite_core` | `core/` + `ui/` |
| `kite` | `kite_core` + `platform/`（`ShellMenu.cpp` / `ShellIcons.cpp` / `ShellFolder.cpp` を**除く**） |
| `kite_shellhost` | `tools/shellhost/` + 上の 3 ファイル + `ShellPipe.cpp` + `WinUtf.cpp` |

**`ShellMenu.cpp`（`IContextMenu`）・`ShellIcons.cpp`（アイコンオーバーレイ）・
`ShellFolder.cpp`（`IShellFolder` の列挙）を `kite` ターゲットに足してはならない。**
リンクした時点で隔離の意味が消える。CI が検査している。

**テストが層分離の防波堤。** `kite_tests` は `kite_core` だけをリンクするので、`core/` や
`ui/` に Windows ヘッダが紛れ込めばビルドが壊れる。ただし Windows 上では
`#include <windows.h>` を足しても通ってしまうため、CI では grep も併用している（§10）。

### 移植の境界

別 OS へ移すときに実装するのは以下だけで、`core/` と `ui/` は 1 行も変わらない。

| 境界 | ヘッダ | Windows 実装 |
| --- | --- | --- |
| 描画 | `ui/Renderer.h` | `D2DRenderer` |
| ファイルシステム | `core/fs/FileSystem.h` | `WinFileSystem` |
| 変更通知 | `core/fs/DirectoryWatcher.h` | `WinDirectoryWatcher` |
| シェル／クリップボード | `core/app/Host.h` の `IShellIntegration` | `WinShell` |
| アイコン | `core/app/IconProvider.h` の `IIconProvider` | `WinIconProvider` |
| ウィンドウ機能 | `core/app/Host.h` の `IHost` | `WinWindow` |
| ファイル入出力・時刻・ロケール | `core/base/Platform.h`（5 つの自由関数） | `WinPlatform` |

残る OS 依存は `path::kSep` が `'\\'` 固定なことくらい（ROADMAP P5-1）。

## 3. データモデル

```
Workspace ─ Session[]（1 つがアクティブ）
            └ SplitNode ツリー（葉が Pane を 1 つ持つ）
              └ Pane ─ Tab[]（1 つがアクティブ）
                       └ Tab ─ パス + 一覧 + 表示状態 + 履歴 + 選択
```

- **全セッションが常駐する。** 切り替えはインデックスの移動だけ。背面に回ったセッションの
  非アクティブタブは一覧データを解放するので、常駐メモリは画面に映っている量に比例する。
- **`Tab::visible` は `listing.entries` への添字だけではない。** 先頭に `..` を表す
  `Tab::kParentRow`（= -1）が入りうるので、`Tab::EntryAt()` / `IsParentRow()` を通す。
  件数は `Tab::ItemCount()`。
- **選択はカーソル移動で消えない。** `Space` が印を付けて 1 行進む方式で、範囲選択は
  「伸縮を始める前の印」を土台に毎回引き直す。
- **タブごとの表示設定（ソート・隠しファイル）は永続化していない。** 保存されるのは
  セッション名・分割レイアウト・タブのパスだけで、読み込み時は既定値から作られる
  （ROADMAP P3-13）。

理由と例外は CLAUDE.md「データモデル」。

## 4. 制御フロー

**`App::Execute`（`core/app/AppCommands.cpp`）が唯一のディスパッチ点。** UI は生のキー
入力に反応しない。

```
WinWindow（WM_KEYDOWN → Chord）
  → App::OnKey
    → KeyMap が Chord を Cmd に変換
      → App::Execute（唯一の switch）
        → Workspace を書き換え / IFileSystem・IShellIntegration を呼ぶ
          → IHost::Invalidate()
            → WinWindow::Paint → AppUi::Paint → ui::Renderer
```

これにより「どの操作にもキーを割り当てられる」が機能ではなく構造として保証される。
コマンド表は `KITE_COMMAND_LIST`（`core/input/Commands.h`）の 125 行。

キーマップを通らないものは 4 つだけで、いずれも「入力そのものを対象にする」画面:

| 例外 | 何をするか |
| --- | --- |
| プロンプト（`App::HandlePromptKey`） | 入力欄が出ている間、打鍵は入力欄のもの |
| キー設定（`core/input/KeyEditor`） | 割り当てたい和音を実行してしまわないよう全打鍵を飲み込む |
| 設定画面（`core/app/SettingsEditor`） | 同上 |
| 型入力ジャンプ（`App::TypeAheadChar`） | **キーマップが答えなかった打鍵だけ**を受け取る |

**描画は毎フレーム組み直す。** `AppUi` はレイアウト結果を持ち越さず、描きながら
`Region`（矩形 + `Hit` の種別）を積み、クリックは `Pick`（後ろから引く）で解決する。
ホバーも「どの行に乗っているか」ではなく最後のポインタ座標を持つだけ ─ ホイールで一覧が
動いたときにマウスイベントは来ないため（CLAUDE.md「制御フロー」）。

## 5. 非同期の作り

**ディレクトリ列挙を UI スレッドで動かさない。** 冷えたネットワーク共有は 1 回の
`FindFirstFile` で数秒ブロックする。

```
App::RequestLoad(tab) → DirectoryLoader::Request(path) → トークンを返す
                          ワーカー（既定 2 本）が IFileSystem::List
                          → IHost::Wake()（WM_KITE_WAKE を投げるだけ）
App::PumpLoader() ← UI スレッドが結果を回収し、トークンの一致するタブへ流す
```

- **答えはトークンで突き合わせる。** 打鍵の途中で届いた古い結果は黙って捨てられる。
  アドレスバーの補完も同じ経路を使い、タブとは別のトークンを持つ。
- **アイコンも同じ形**。`IconCache` が要求を貯め、`WinIconProvider` のワーカーがホストへ
  まとめて投げ、結果は次のフレームでアップロードされる。
- **`Wake()` は再描画を意味しない。** 列挙結果が無ければ `PumpLoader` は何もしないので、
  `WM_KITE_WAKE` の側で必ず `Invalidate()` する（CLAUDE.md「すでに踏んだ罠」）。
- **変更通知**は `WinDirectoryWatcher` のワーカーが全ハンドルを所有し、`Watch`/`Unwatch` は
  コマンドを投函するだけ（未完了の読み取りを抱えたままハンドルを閉じないため）。

## 6. シェル連携と仮想フォルダ

実フォルダの列挙は **Win32 API 直叩き**（`FindFirstFileEx` + `FindExInfoBasic` +
`FIND_FIRST_EX_LARGE_FETCH`）。`IShellFolder` を通らないので、一覧を出すためだけに
サードパーティの拡張 DLL が読み込まれることがない。クラウドのプレースホルダは
`RECALL_ON_OPEN` / `RECALL_ON_DATA_ACCESS` を見て検出し、中身には一切触れない。

シェルに降りるのは 4 つの場面だけ:

| 場面 | 経路 |
| --- | --- |
| 右クリックメニュー | `WinShell` → メニュー用ホスト（`IContextMenu`） |
| 実アイコン・オーバーレイ | `WinIconProvider` → アイコン用ホスト（`SHGetFileInfo`） |
| 仮想フォルダ・書庫の列挙 | `WinFileSystem::ListVirtual` → 列挙用ホスト（`IShellFolder`） |
| ファイル操作・起動 | `SHFileOperation` / `ShellExecute`（自プロセス。DLL は動かない） |

**仮想フォルダのパスは `virtual:` で綴る。** 判定規則だけが `core/fs/VirtualPath.h` にあり、
それが Windows のどの場所を指すのかは `platform/win/VirtualNames.h` だけが知っている
（規則を Windows 実装に埋めるとテストできない）。`virtual:` の付いた文字列をプロセスの
外へ出してはならない ─ シェルはその前置を知らない。

- 「PC」の下のドライブのように**実 FS 上の項目はそのまま実パス**（`C:\`）として返る。
  前置が付くのは名前空間拡張と書庫の中だけ。
- **書庫（`.zip` / `.cab`）の縁が、シェル名前空間と実 FS の境目。** `..` は書庫そのものから
  実フォルダへ抜ける。
- 仮想フォルダと書庫の中には **Kite は書き込まない**。監視も張れないので更新は `F5`。
- ごみ箱の項目は「どのフォルダの中の項目か」ごと渡して指す ─ 解析名をそのまま解析すると
  ごみ箱の項目ではなくただのファイルが返り、破壊的な操作になる。

詳細は CLAUDE.md「仮想フォルダ」「ごみ箱の項目を正しく指す」「ZIP をフォルダとして開く」。

## 7. 描画スタック

- **Direct2D 1.1 + DXGI フリップスワップチェーン**（D3D11 デバイス → `ID2D1Device` →
  `ID2D1DeviceContext`）。`ID2D1HwndRenderTarget` は開発機（Intel Iris Xe）で `EndDraw` が
  `S_OK` を返しながら何も描かなかった。「単純化」して戻さないこと。
- ターゲットはウィンドウ表示後に作る（最初の `BeginFrame` で遅延生成）。
- 文字は DirectWrite。書式はサイズ固定で作るので、`WinWindow::Paint` が毎フレーム
  `UpdateTheme` を呼んで変化があれば作り直す。
- **文字サイズの倍率は `Theme` に焼き込む。** `App::ApplyTheme` が「既定 →
  `settings.ini` → 倍率」の順で毎回作り直し、`ui/` 側は倍率を知らない。行やバーの高さも
  同じ率で伸びる（文字を入れる器なので）。
- **シェルアイコンが届くまでは `ui/Glyphs.cpp` のベクタ描画が同じ寸法で場所を持つ**ので、
  届いた瞬間にラベルがずれることはない。`[ui] shell_icons = false` でベクタ描画に固定できる。
- IME の未確定文字列は Kite 自身が入力欄の中に描く（IME の変換窓は自前のフォントと行送りで
  置くのでずれる）。位置の出どころは `AppUi::caretRect()` の 1 つだけ。

## 8. 設定と永続化

**`kite.exe` の隣に `config` フォルダが在ればそこ、無ければ `%APPDATA%\Kite`。** 判定は
起動時の 1 回だけで、在るものを選ぶだけ（無いほうを作らない）。選ぶ規則そのものは
`core/app/ConfigDir.h` の `config::Choose()`（純関数。列挙も存在確認も呼び出し側の仕事）。

| ファイル | 書くタイミング |
| --- | --- |
| `settings.ini` | 設定画面で変えた時点、および終了時 |
| `keys.ini` | 初回起動時に全既定を書き出し、以後は `Ctrl+Shift+,` で変えた時点 |
| `sessions.ini` | 終了時と `Ctrl+S` |
| `bookmarks.ini` | 追加・削除・並べ替えの時点 |

- **設定フォルダへの書き込みは `App::WriteConfigFile` の一本道**。`Program Files` などで
  失敗したらステータス行に出る。`plat::WriteTextFile` を直接呼ばないこと。
- **`--new-window` で起動した単独ウィンドウは何も書かない**（`keys.ini` だけは例外）。
  設定もセッションも起動時の写しでしかなく、後から閉じたほうが本体の変更を上書きする。
- **2 枚目のウィンドウは別プロセス。** `App` も描画資源もローダーのワーカーもウィンドウ
  1 つぶんの寿命に紐付いている。
- **単一インスタンス**の目印（ミューテックス名とウィンドウクラス名）は **exe のフルパス**
  から導く。固定名にすると、別フォルダに展開した 2 つのコピーが同じインスタンスとして
  振る舞い、一方の `config` だけが使われる。

## 9. i18n とテーマ

- 文字列は**全面的に UTF-8**。UTF-16 は `platform/win/` の内部にのみ存在し、`WinUtf.h` が
  境界で変換する。
- 表示文字列は `core/i18n/Strings.cpp` の `kEn` / `kJa` にだけ置く。利用者は
  `lang.<code>.ini` を設定フォルダに置けば再ビルド無しに言語を足せる。
- 配色は `Theme`。ダークの既定は**無彩色**で、色相を持つのはエラー・フォーカス
  （Windows のアクセント色に合わせる）・入力欄の選択の 3 つだけ。`test_theme.cpp` が
  この方針を検査している。

## 10. ビルド・テスト・CI

**x64 Native Tools Command Prompt for VS 2022** から:

```bash
cmake --preset release
cmake --build --preset release
ctest --preset release
```

**素の「Developer PowerShell for VS 2022」は x86 を向く。** そのシェルからビルドすると
コンパイルは通ってリンクだけが `error LNK2001: 外部シンボル _purecall は未解決です` で
落ちる（`LIB` だけが 32 ビットの CRT を指すため）。CMakeLists が `VSCMD_ARG_TGT_ARCH` を見て
configure の時点で止める。開発者プロンプト以外からは `build.ps1`（常に `vcvars64.bat` を
読む）を使う。

テストは **25 スイート・686 ケース**、判定は 100 行の自作ハーネス（`tests/TestFramework.h`）。
OS 境界のフェイクは `tests/Fakes.h` に揃えてあり、`FakePlatform.cpp` が `Platform.h` の
5 つをメモリ上で実装するので、実ディスクにも実時計にも触れずに `App` を端から端まで
動かせる。`FakeRenderer` は塗った矩形・色・文字の外接矩形を覚えるので、**ウィンドウ無しで
1 フレーム描いて「どこに何色を塗ったか」「2 つの文字列が重なっていないか」を検査できる。**

```bash
build/release/kite_tests.exe --filter app.
```

CI（`.github/workflows/ci.yml`、main への push と PR）が見ているもの:

| 見ているもの | 手元での再現 |
| --- | --- |
| ビルド＋全テスト（`-DKITE_WERROR=ON` で `/W4` を `/WX` に格上げ） | `ctest --preset release` |
| 2 つの exe が同じフォルダに出るか | `ls build/release/*.exe` |
| `core/` `ui/` `tests/` に Windows ヘッダが無いか | `git grep -nE '#include *[<"]windows' -- src/core src/ui tests` |
| シェル拡張の 3 ファイルが `kite` に入っていないか | `CMakeLists.txt` の `add_executable(kite WIN32 ...)` |
| Doxygen 警告がゼロか | `doxygen docs/Doxyfile` |
| `*.ps1` が UTF-8 BOM 付きで、PowerShell 5.1 で構文解析できるか | ─ |

Windows しかビルドしないので、**層分離はテストのビルドだけでは検査できない** ─ 上表の
grep がその代わり。

版番号の正は `CMakeLists.txt` の `project(Kite VERSION x.y.z)` 1 行だけで、画面表示・
`.rc` の VERSIONINFO・タグ・Release はすべてそこからの派生物（`cmake/GitVersion.cmake` と
`release.ps1`）。手順は CLAUDE.md「版とアイコン」「リリース」。

## 11. 実測値（開発機: Core i7 / Intel Iris Xe / Windows 11）

| 項目 | 値 |
| --- | --- |
| 起動（ウォーム、入力受付まで） | **145〜320 ms** |
| 起動（コールド） | 約 550 ms |
| ワーキングセット（1 ペイン / 1 タブ） | 約 66 MB |
| 実行ファイルサイズ | `kite.exe` 約 655 KB + `kite_shellhost.exe` 約 145 KB（依存 DLL なし） |

起動時間は `WaitForInputIdle` 到達までを `Start-Process` 経由で測ったもので、プロセス生成の
オーバーヘッドを含む。同一バイナリでも計測時の負荷で 145〜320 ms まで振れたため、単一の
代表値ではなく範囲で示している。内訳の計測は ROADMAP P4-3。

ワーキングセットの大半は Direct3D 11 / DXGI のグラフィックススタックで、Kite 自身の
データ構造は数百 KB 程度。削減の段取りは ROADMAP P4-1（**先に計測すること**）。

## 12. 構造上の制限

設計から直接出てくる制限。個別の弱点は CLAUDE.md「現時点の弱点」に。

- **メニューを開いている間、Kite のタイトルバーは非アクティブ表示になる。** 前景を取るのは
  ホストの隠しウィンドウなので避けられない。
- **ホストがデッドロックした場合、メニューの待機は終わらない**（時間制限を付けていない）。
  ウィンドウは再描画され続け、× で閉じられる。
- **仮想フォルダと書庫の中は監視が効かない。** シェル名前空間に通知を掛けるハンドルが無い。
- **2 枚目以降のウィンドウは状態を保存しない。** 別プロセスであることの帰結。
- **終了時の保存失敗は伝えられない。** ステータス行はもう描かれず、`IHost` にダイアログを
  出す手段が無い（ROADMAP P1-6）。
- **アイコンオーバーレイの枠は OS 全体で 15 個**しかなく、先に登録されたものから埋まる。
  エクスプローラーでも同じで、Kite 側では直せない。
