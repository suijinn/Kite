# Kite

[![ci](https://github.com/suijinn/Kite/actions/workflows/ci.yml/badge.svg)](https://github.com/suijinn/Kite/actions/workflows/ci.yml)

Windows エクスプローラーの代替を目指す、C++ 製の軽量ファイラー。

名前は「凧」から。軽く、速く、風向きが変わればすぐ向きを変える。菱形のシルエットは
ペイン分割の暗喩でもある。実行ファイル名・設定フォルダ名ともに `Kite` を使う。

現在は**プロトタイプ（v0.1）**。骨格となる機能（タブ／ペイン分割／セッション／
ブックマーク／全操作のキーバインド／i18n／ダークテーマ／非同期列挙／シェル拡張）は
実際に動作する。

---

## ダウンロード

[Releases](https://github.com/suijinn/Kite/releases/latest) の
`kite-vx.y.z-win-x64.zip`。展開して、中身は**同じフォルダのまま**置く。

| ファイル | 役割 |
| --- | --- |
| `kite.exe` | 本体。インストール不要、CRT は静的リンク |
| `kite_shellhost.exe` | サードパーティのシェル拡張（コンテキストメニューとアイコンオーバーレイ）を動かす別プロセス |
| `config\` | 設定の置き場所。フォルダごと持ち運べる（下記「設定ファイル」） |

`kite.exe` はホストを**自分と同じフォルダから**起動する。片方だけを移すと右クリックで
「シェルメニューを表示できませんでした」になる。

## 主な機能

- タブ（追加・複製・復元・番号ジャンプ、ドラッグで並べ替え／別ペインへ移動）
- ペイン分割（左右／上下、入れ子、ドラッグで比率変更、方向キーでフォーカス移動）
- セッション（複数保持、即時切り替え、リネーム、分割レイアウトごと保存）
- サイドバー（クイックアクセス／ブックマーク／ドライブ + 空き容量バー）。区画ごとに
  折り畳め、ドラッグで並べ替えられる。ブックマークは `Alt+Shift+1..8` で直行
- ファイルのドラッグ＆ドロップ（Kite 内、エクスプローラーとの相互。同一ボリュームは
  移動・別ボリュームはコピー、`Ctrl` でコピー固定・`Shift` で移動固定）
- ファイル操作（新規フォルダ／新規ファイル／リネーム／削除（ごみ箱・完全）／
  コピー・切り取り・貼り付け／パス・名前のコピー）
- シェルのコンテキストメニューと実アイコン（オーバーレイ含む）。エクスプローラーと同じく
  拡張動詞は `Shift` を押したときだけ出る
- 非同期ディレクトリ列挙（ネットワーク・クラウドでも固まらない）と、
  ファイルシステム監視による自動更新
- ソート（名前／拡張子／サイズ／更新日時、自然順、フォルダ優先）、絞り込み（`Ctrl+F`）
- パンくずを兼ねたアドレスバー（`Ctrl+L`）と、パス入力の補完
- 選択（範囲・トグル・全選択・反転、余白から引く選択の枠）。`Space` で付けた印は
  カーソル移動で消えないので、離れた項目をいくつでも拾える
- 文字サイズの変更（`Ctrl++` / `Ctrl+-` / `Ctrl+0`）。行やバーの高さも一緒に伸縮するので、
  大きくしても文字が切れない
- ダーク／ライトテーマ（タイトルバーとシェルメニューも追従）、設定ファイルで配色変更可
- 日本語／英語 UI（OS 設定から自動判定、`lang.<code>.ini` で追加・上書き可能）
- キーバインドの一覧（`F1`）と GUI 設定（`Ctrl+F1`）、`keys.ini` による変更
- 日本語 IME の変換候補ウィンドウをキャレット位置に追従
- ウィンドウ位置・セッション・ブックマークの永続化

未実装（検索・サムネイル・仮想フォルダなど）と今後の計画は
**[docs/ROADMAP.md](docs/ROADMAP.md)**。直近の最重要項目は単一インスタンス化、
「PC」「ごみ箱」などの仮想フォルダ、検索。

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
| 親フォルダへ | `Backspace` / `Alt+Up`（一覧先頭の `..`、空きスペースのダブルクリックも可） |
| 戻る／進む | `Alt+Left` / `Alt+Right`（マウスの戻る／進むボタンも可） |
| パスを編集 | `Ctrl+L` |
| 絞り込み | `Ctrl+F` |
| コンテキストメニュー | `Menu` キー、または右クリック |
| 拡張コンテキストメニュー | `Shift+F10`、または `Shift`+右クリック |
| 現在のフォルダーのコンテキストメニュー | `Ctrl+Menu`（拡張は `Ctrl+Shift+F10`） |
| ここでターミナルを開く | `Ctrl+`` ` `` / `Alt+T` |
| 文字を大きく／小さく／既定へ | `Ctrl++` / `Ctrl+-` / `Ctrl+0` |
| サイドバーの表示切り替え | `Ctrl+B` |
| ダーク／ライト切り替え | `Ctrl+Shift+M` |
| ショートカット一覧 | `F1` |

`Alt+V` / `Alt+H` を分割に充てているのは、JIS 配列でも押しやすいため
（`Ctrl+\` は JIS では位置が異なる）。

変更は `Ctrl+F1` の設定画面か、設定フォルダの `keys.ini` を編集して `Ctrl+Alt+C` で
再読み込み。初回起動時に全デフォルトを書き出してあるので、そのファイルがそのまま
一覧兼リファレンスになる。

```ini
[keys]
pane.split_left_right = Ctrl+Alt+V   ; 追加（複数行書けば複数の割り当て）
tab.close             = none         ; 既定を全部外す
```

## 設定ファイル

置き場所は 2 通り。**`kite.exe` の隣に `config` フォルダが在ればそこ**、無ければ
`%APPDATA%\Kite\`。zip には `config` が入っているので、展開してそのまま使えば設定を
含めた全部がそのフォルダの中で完結する（フォルダごと USB へ移しても設定が付いて来る）。
`config` を消せば `%APPDATA%\Kite\` に切り替わる。

判定は起動時の 1 回だけで、無いほうを勝手に作ることはしない。今どちらを使っているかは
`Ctrl+Alt+,`（設定フォルダを開く）で確かめられる。

> `Program Files` や書き込み禁止のメディアに展開した場合、設定は保存されない。
> その場合は初回起動時に「書き込めませんでした」と出るので、書き込める場所へ
> 移すこと（終了時の保存失敗だけは、表示する場所がもう無いため出ない）。

中身はどちらでも同じで、すべてプレーンな INI なので手で編集できる。

| ファイル | 内容 |
| --- | --- |
| `settings.ini` | テーマ・言語・表示設定・ウィンドウ位置・配色の上書き・サイドバーの並び |
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
font_scale = 1.0    ; Ctrl++ / Ctrl+- が書き換える倍率。上の値に掛かる
```

---

## ビルドとテスト

Visual Studio 2022（C++ ワークロード）だけあればよい。CMake・Ninja・ctest はすべて
VS に同梱されているので、追加インストールは不要。**Developer PowerShell for VS 2022** で:

```bash
cmake --preset release
cmake --build --preset release
ctest --preset release
```

出力は `build\release\` に `kite.exe`・`kite_shellhost.exe`・`kite_tests.exe`。
`debug` プリセットも同じ 3 コマンドで使える。開発者プロンプトでない普通の
PowerShell からは、MSVC 環境を自前で読み込む `build.ps1 -Run` を使う。

テストは 18 スイート・348 ケース。リンクするのは `kite_core`（`core/` と `ui/`）
だけで、OS 非依存のはずの層に Windows ヘッダが紛れ込めばテストのビルドが壊れる、
という形でレイヤ分離そのものを検査している。個別に走らせる場合:

```bash
build/release/kite_tests.exe --filter app.
```

## 実測値（開発機: Core i7 / Intel Iris Xe / Windows 11）

| 項目 | 値 |
| --- | --- |
| 起動（ウォーム、入力受付まで） | **145〜320 ms** |
| 起動（コールド） | 約 550 ms |
| ワーキングセット（1 ペイン / 1 タブ） | 約 66 MB |
| 実行ファイルサイズ | `kite.exe` 約 655 KB + `kite_shellhost.exe` 約 145 KB（依存 DLL なし） |

起動時間は `WaitForInputIdle` 到達までを `Start-Process` 経由で測ったもので、プロセス
生成のオーバーヘッドを含む。同一バイナリでも計測時のマシン負荷で 145〜320 ms まで
振れたため、単一の代表値ではなく範囲で示している。

ワーキングセットの大半は Direct3D 11 / DXGI のグラフィックススタックが占める。
Kite 自身のデータ構造は数百 KB 程度。削減余地は ROADMAP の P4-1 に。

## アーキテクチャ

クロスプラットフォーム化を前提に、3 層で厳密に分離している。

```
src/
  core/        OS 非依存。Windows ヘッダを一切 include しない
    base/        UTF-8 / パス / INI / 書式 / 自然順ソート
    fs/          IFileSystem 抽象 + 非同期 DirectoryLoader
    model/       Tab → Pane → SplitNode ツリー → Session → Workspace
    input/       Cmd テーブル・キーコード・キーマップ・パス補完
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
    ShellMenu     IContextMenu 本体。kite_shellhost.exe だけがリンクする
    ShellIcons    SHGetFileInfo 本体。同上
tools/shellhost/ kite_shellhost.exe。他人のコードが動く唯一のプロセス
```

**別 OS へ移すときに書くもの**は `platform/` 配下だけ。具体的には `ui::Renderer`、
`fs::IFileSystem`、`IShellIntegration`、`IIconProvider`、`IHost`、そして
`core/base/Platform.h` の 5 つの実装。`core/` と `ui/` は 1 行も変わらない。

主要な設計判断（理由はいずれも [CLAUDE.md](CLAUDE.md) に）:

- **文字列は内部すべて UTF-8。** UTF-16 への変換は `platform/win/WinUtf.cpp` の中だけ。
- **列挙は Win32 API 直叩き**（`IShellFolder` を使わない）。`FindFirstFileEx` を
  `FindExInfoBasic` + `FIND_FIRST_EX_LARGE_FETCH` で回す。列挙のためだけに
  サードパーティのシェル拡張 DLL を読み込むことがなくなる。
- **列挙もアイコン取得も必ずワーカースレッド。** ネットワーク共有・スリープ中の USB・
  冷えたクラウドフォルダは 1 回の `FindFirstFile` で数秒ブロックする。
- **クラウドのプレースホルダを実体化しない。** `RECALL_ON_OPEN` /
  `RECALL_ON_DATA_ACCESS` を検出して雲アイコンで表示し、中身には一切触れない。
- **サードパーティのシェル拡張は `kite_shellhost.exe` の中だけで動かす。**
  コンテキストメニューもアイコンオーバーレイも、壊れて失われるのはホストプロセス
  だけで済む。次の要求で黙って起動し直す。
- **描画は Direct2D 1.1 + DXGI フリップスワップチェーン。** `ID2D1HwndRenderTarget`
  は開発機（Intel Iris Xe）で `EndDraw` が S_OK を返しながら何も描かなかった。
- **全操作がコマンド。** テキスト入力欄以外、UI は生のキー入力に反応しない。
  そのため「どの操作にもキーを割り当てられる」が構造的に保証される。
- **セッション切り替えは即座。** 全セッションが常駐し、裏に回ったセッションの
  非アクティブタブだけが一覧データを解放する。

## ドキュメント

| ファイル | 内容 |
| --- | --- |
| [CLAUDE.md](CLAUDE.md) | 作業ブリーフ。設計上の制約、機能の追加手順、踏んだ罠 |
| [docs/ROADMAP.md](docs/ROADMAP.md) | 要件別の達成状況と、優先順位付きの未実装項目 |
| [docs/Doxyfile](docs/Doxyfile) | API ドキュメント生成の設定 |

ヘッダには日本語の Doxygen コメントを付けてある。リポジトリのルートから
`doxygen docs/Doxyfile` を実行すると `build/doxygen/html/index.html` が生成される
（`@todo` の一覧は同 `todo.html`）。
