# Kite

Windows エクスプローラーの代替を目指す、C++ 製の軽量ファイラー。

名前は「凧」から。軽く、速く、風向きが変わればすぐ向きを変える。菱形のシルエットは
ペイン分割の暗喩でもある。実行ファイル名・設定フォルダ名ともに `Kite` を使う。

現在は**プロトタイプ（v0.1）**。骨格となる機能（タブ／ペイン分割／セッション／
ブックマーク／全操作のキーバインド／i18n／ダークテーマ／非同期列挙／拡張コンテキスト
メニュー）は実際に動作する。

---

## ビルド

Visual Studio 2022（C++ ワークロード）だけあればよい。CMake・Ninja・ctest はすべて
VS に同梱されているので、追加インストールは不要。

**Developer PowerShell for VS 2022** で:

```bash
cmake --preset release
```

```bash
cmake --build --preset release
```

```bash
ctest --preset release
```

出力は `build\release\kite.exe`。CRT を静的リンクしているので、この 1 ファイルだけで動く。
`debug` プリセットも同じ 3 コマンドで使える。

開発者プロンプトでない普通の PowerShell からは、MSVC 環境を自前で読み込む
`build.ps1` を使う:

```powershell
.\build.ps1 -Run
```

## テスト

```bash
ctest --preset release
```

12 スイート・約 160 ケース。テストは `kite_core`（`core/` と `ui/`）だけをリンクする。
OS 非依存のはずの層に Windows ヘッダが紛れ込めばテストのビルドが壊れる、という形で
レイヤ分離そのものを検査している。

個別に走らせる場合:

```bash
build/release/kite_tests.exe --filter app.
```

---

## 実測値（開発機: Core i7 / Intel Iris Xe / Windows 11）

| 項目 | 値 |
| --- | --- |
| 起動（ウォーム、入力受付まで） | **145〜320 ms** |
| 起動（コールド） | 約 550 ms |
| ワーキングセット（1 ペイン / 1 タブ） | 約 66 MB |
| 実行ファイルサイズ | 約 501 KB（単一ファイル、依存 DLL なし） |

起動時間は `WaitForInputIdle` 到達までを PowerShell の `Start-Process` 経由で測ったもので、
プロセス生成のオーバーヘッドを含む。同一バイナリでも計測時のマシン負荷で 145 ms から
320 ms まで振れたため、単一の代表値ではなく範囲で示している。ファイル監視スレッドの
追加によるコストは A/B 計測で有意差なしだった。

ワーキングセットの大半は Direct3D 11 / DXGI のグラフィックススタックが占める。
Kite 自身のデータ構造は数百 KB 程度。ここは削減余地があり、下の「今後」を参照。

なお `OleInitialize` とシェルのドラッグ画像ヘルパーは 100 ms 以上かかるため、
ドラッグ＆ドロップの登録は初回描画から 200 ms 後のタイマーに逃がしてある。

---

## ドキュメント

| ファイル | 内容 |
| --- | --- |
| [CLAUDE.md](CLAUDE.md) | 作業ブリーフ。設計上の制約、機能の追加手順、踏んだ罠 |
| [docs/ROADMAP.md](docs/ROADMAP.md) | 要件別の達成状況と、優先順位付きの未実装項目 |
| [docs/Doxyfile](docs/Doxyfile) | API ドキュメント生成の設定 |

ヘッダには日本語の Doxygen コメントを付けてある。リポジトリのルートから
`doxygen docs/Doxyfile` を実行すると `build/doxygen/html/index.html` が生成される
（`@todo` の一覧は同 `todo.html`）。

## アーキテクチャ

クロスプラットフォーム化を前提に、3 層で厳密に分離している。

```
src/
  core/        OS 非依存。Windows ヘッダを一切 include しない
    base/        UTF-8 / パス / INI / 書式 / 自然順ソート
    fs/          IFileSystem 抽象 + 非同期 DirectoryLoader
    model/       Tab → Pane → SplitNode ツリー → Session → Workspace
    input/       Cmd テーブル・キーコード・キーマップ
    i18n/        文字列テーブル
    theme/       配色とメトリクス
    app/         App（コントローラ）。全コマンドの唯一のディスパッチ点
  ui/          OS 非依存。抽象 ui::Renderer に対してのみ描画する
    Renderer.h   描画プリミティブのインターフェース
    AppUi.cpp    レイアウト・描画・ヒットテスト
    Glyphs.cpp   フォルダ／ファイル／ドライブ等のベクタアイコン
  platform/win/  Windows ヘッダが現れる唯一の場所
    D2DRenderer   Direct2D 1.1 + DXGI スワップチェーン
    WinFileSystem Win32 API による列挙とファイル操作
    WinShell      クリップボード / ShellExecute
    WinWindow     HWND・メッセージループ・入力変換（IHost 実装）
    WinPlatform   ファイル入出力・時刻・UI 言語
    ShellHostClient  コンテキストメニューを別プロセスへ依頼する側
    ShellMenu     IContextMenu 本体。kite_shellhost.exe だけがリンクする
tools/shellhost/ kite_shellhost.exe。他人のコードが動く唯一のプロセス
```

**別 OS へ移すときに書くもの**は `platform/` 配下だけ。具体的には
`ui::Renderer`、`fs::IFileSystem`、`IShellIntegration`、`IHost`、そして
`core/base/Platform.h` の 5 つの実装。`core/` と `ui/` は 1 行も変わらない。

### 主要な設計判断

**文字列は内部すべて UTF-8。** UTF-16 への変換は `platform/win/WinUtf.cpp` の中だけで
起きる。移植時に文字列型を書き換える必要がない。

**列挙は Win32 API 直叩き（IShellFolder を使わない）。** `FindFirstFileEx` を
`FindExInfoBasic` + `FIND_FIRST_EX_LARGE_FETCH` で回す。8.3 名の解決を飛ばし、
ネットワーク／クラウド越しの往復を束ねるので速い。副次的に、列挙のためだけに
サードパーティのシェル拡張 DLL を読み込むことがなくなる。

**列挙は必ずワーカースレッド。** ネットワーク共有・スリープ中の USB・冷えた
クラウドフォルダは 1 回の `FindFirstFile` で数秒ブロックする。`DirectoryLoader` が
リクエストにトークンを振って裏で処理し、UI スレッドは結果を引き取るだけ。
タブが先に移動していれば古い結果はトークン不一致で捨てられる。

**クラウドのプレースホルダを実体化しない。** `FILE_ATTRIBUTE_RECALL_ON_OPEN` /
`RECALL_ON_DATA_ACCESS` を検出して雲アイコンで表示し、中身には一切触れない。
ファイルを開くときも自前で読まず、シェルに委ねる（Box / Google Drive 対策）。

**シェルアイコンは UI スレッドから取らない。** `SHGetFileInfo` は 1 行ごとにシェル
（つまりクラウドプロバイダとオーバーレイハンドラ）を叩き、まさに Kite が避けたい
起動コストとストールを生む。要求は画面に見えている行の分だけを貯め、ワーカー
スレッドから `kite_shellhost.exe` へまとめて投げる。届くまでは同じ位置にベクタ描画の
アイコンが出るので、行がずれることはない。`[ui] shell_icons = false` でシェルを
一切呼ばない運用にも戻せる。

**シェル拡張は別プロセスで動かす。** コンテキストメニューの構築・表示・実行は
`kite_shellhost.exe` が担う。Kite 本体は「パス群と画面座標」をパイプで送り、
「実行したか」だけを受け取る。Box / Google Drive / 7-Zip / TortoiseGit などの
`IContextMenu` ハンドラが確保したメモリを壊しても、裏でスレッドを立てて落ちても、
失われるのはメニュー 1 回分とホストプロセスだけ。次の右クリックで黙って起動し直す。
ホストは最初のメニュー要求で初めて起動し（起動パスに COM を置かないため）、
しばらく使われなければ自分で終了して拡張 DLL の分のメモリを返す。

**描画は Direct2D 1.1 + DXGI フリップスワップチェーン。** 最初は素朴に
`ID2D1HwndRenderTarget` を使ったが、開発機（Intel Iris Xe）では `EndDraw` が S_OK を
返しながら画面に何も出ないという不具合が出た（最小再現コードでも同様）。
Windows 8 以降の推奨経路である D3D11 → `ID2D1Device` → `ID2D1DeviceContext` →
`CreateSwapChainForHwnd` に切り替えて解決。リサイズ時の見た目も良い。

**全操作がコマンド。** テキスト入力欄以外、UI は生のキー入力に一切反応しない。
キーマップが和音を `Cmd` に変換し、`App::Execute` が唯一のディスパッチ点になる。
そのため「どの操作にもキーを割り当てられる」が構造的に保証される。
機能追加は `KITE_COMMAND_LIST` に 1 行 + `Execute` に 1 case。

**セッション切り替えは即座。** 全セッションが常駐しているので切り替えはインデックスの
移動だけ。裏に回ったセッションの非アクティブタブは一覧データを解放し、常駐メモリを
画面に映っている量に比例させる。

---

## 実装済み

- タブ（追加・複製・復元・番号ジャンプ、**ドラッグで並べ替え／別ペインへ移動**）
- **ファイルのドラッグ＆ドロップ**（Kite 内、Explorer との相互、同一ボリュームは移動・
  別ボリュームはコピー、Ctrl でコピー固定・Shift で移動固定。自分自身や自分の
  サブツリーへのドロップは拒否）
- **ファイルシステム監視による自動更新**（`ReadDirectoryChangesW`、250 ms デバウンス）
- ペイン分割（左右／上下、入れ子、ドラッグで比率変更、方向キーでフォーカス移動）
- セッション（複数保持、即時切り替え、リネーム、レイアウトごと保存）
- ブックマーク（サイドバー表示、`Alt+Shift+1..8` で直行）
- サイドバー（クイックアクセス／ブックマーク／ドライブ + 空き容量バー）
- 非同期ディレクトリ列挙、ネットワーク・クラウド対応
- ソート（名前／拡張子／サイズ／更新日時、自然順、フォルダ優先）
- 絞り込み（`Ctrl+F`、入力中もリストをキーボード操作可能）
- パス直接入力（`Ctrl+L`）、パンくずクリック
- 選択（範囲・トグル・全選択・反転）、マウス操作一式
- 拡張コンテキストメニューを 1 アクションで表示
- ファイル操作: 新規フォルダ／新規ファイル／リネーム／削除（ごみ箱・完全）／
  コピー・切り取り・貼り付け（`CF_HDROP`）／パス・名前のコピー
- ダーク／ライトテーマ（タイトルバーも追従）、設定ファイルで配色変更可
- 日本語／英語 UI（OS 設定から自動判定、`lang.<code>.ini` で追加・上書き可能）
- 全コマンドのキーバインド一覧（`F1`）と `keys.ini` による変更
- 日本語 IME の変換候補ウィンドウをキャレット位置に追従
- ウィンドウ位置・セッション・ブックマークの永続化

## 未実装と今後の計画

要件ごとの達成状況、エクスプローラーとの機能差分、優先順位付きの TODO は
**[docs/ROADMAP.md](docs/ROADMAP.md)** にまとめてある。

直近の最重要項目だけ挙げると:

- 単一インスタンス化（既存ウィンドウの新規タブで開く）
- 「PC」「ごみ箱」「ネットワーク」等の仮想フォルダ
- 検索、サムネイル、コマンドパレット

---

## デフォルトのキー割り当て

`F1` でいつでも全一覧が出る。3 つの数字列が並行しているのが基本方針。

| | |
| --- | --- |
| `Ctrl+1..9` | タブを選択 |
| `Alt+1..8` | セッションを選択 |
| `Alt+Shift+1..8` | ブックマークへ移動 |

主なもの:

| 操作 | キー |
| --- | --- |
| 左右に分割（縦分割） | `Alt+V` |
| 上下に分割（横分割） | `Alt+H` |
| ペインを閉じる | `Alt+W` |
| 次／前のペイン | `Tab` / `Shift+Tab` |
| 新しいタブ／閉じる | `Ctrl+T` / `Ctrl+W` |
| 新しいセッション | `Ctrl+Alt+N` |
| 親フォルダへ | `Backspace` / `Alt+Up` |
| 戻る／進む | `Alt+Left` / `Alt+Right`（マウスの戻る／進むボタンも可） |
| パスを編集 | `Ctrl+L` |
| 絞り込み | `Ctrl+F` |
| 拡張コンテキストメニュー | `Menu` キー、または右クリック |
| 通常のコンテキストメニュー | `Shift+F10`、または `Shift`+右クリック |
| 現在のフォルダーのコンテキストメニュー | `Ctrl+Menu` / `Ctrl+Shift+F10` |
| ダーク／ライト切り替え | `Ctrl+Shift+M` |
| ショートカット一覧 | `F1` |

`Alt+V` / `Alt+H` を分割に充てているのは、JIS 配列でも押しやすいため
（`Ctrl+\` は JIS では位置が異なる）。

### 変更方法

`%APPDATA%\Kite\keys.ini` を編集して `Ctrl+Alt+C` で再読み込み。初回起動時に
全デフォルトを書き出してあるので、そのファイルがそのまま一覧兼リファレンスになる。

```ini
[keys]
pane.split_left_right = Ctrl+Alt+V   ; 追加（複数行書けば複数の割り当て）
tab.close             = none         ; 既定を全部外す
```

---

## 設定ファイル

`%APPDATA%\Kite\` に置かれる。すべてプレーンな INI で、手で編集できる。

| ファイル | 内容 |
| --- | --- |
| `settings.ini` | テーマ・言語・表示設定・ウィンドウ位置・配色の上書き |
| `keys.ini` | キー割り当て |
| `sessions.ini` | セッション名とペイン分割レイアウト（タブのパスを含む） |
| `bookmarks.ini` | ブックマーク |
| `lang.<code>.ini` | 任意。UI 文字列の追加・上書き（新言語の追加に再ビルド不要） |

配色は `settings.ini` の `[theme.dark]` / `[theme.light]` で個別に上書きできる。

```ini
[theme.dark]
accent = 3E7BFA
list_bg = 1B1E24

[ui]
font_family = Yu Gothic UI
font_size = 13
row_height = 22
```
