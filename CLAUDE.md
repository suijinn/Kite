# CLAUDE.md

Kite — a lightweight Windows file manager in C++20, built to replace Explorer.
This file is the working brief for anyone (human or agent) editing the repo.

## Build and test

Everything runs from a **Developer PowerShell for VS 2022** (CMake, Ninja and
ctest all ship inside Visual Studio; nothing else needs installing).

```bash
cmake --preset release
cmake --build --preset release
ctest --preset release
```

Output: `build/release/kite.exe` and `build/release/kite_tests.exe`.
There is also a `debug` preset with the same three commands.

`build.ps1` does the same thing from an ordinary PowerShell by importing the
MSVC environment itself — useful when a shell is not a developer prompt.

Running a bare `cmake --build build/release` from a non-developer shell fails
with confusing "cannot open include file `<mutex>`" errors: that means `INCLUDE`
is unset, not that anything is wrong with the code.

Run one suite directly for a fast loop:

```bash
build/release/kite_tests.exe --filter app.
```

`--list` prints every test name.

## Architecture

Three layers, strictly separated. This is the project's main design constraint
and the reason it can move to another OS later.

```
src/core/       No OS calls at all, beyond the five free functions declared in
                core/base/Platform.h. Never include <windows.h> here.
src/ui/         Layout, painting, hit-testing and drag logic. Draws only
                through the abstract ui::Renderer.
src/platform/   The only place Windows headers appear.
tests/          Links kite_core alone - if a Windows header leaks into core or
                ui, the test build breaks. That is the guardrail.
```

`kite_core` (CMake target) = `core/` + `ui/`. `kite` = `kite_core` + `platform/`.

Porting to another OS means implementing five things and nothing else:

| Seam | Header |
| --- | --- |
| Drawing | `ui/Renderer.h` |
| Filesystem | `core/fs/FileSystem.h` |
| Change notification | `core/fs/DirectoryWatcher.h` |
| Shell / clipboard | `IShellIntegration` in `core/app/Host.h` |
| Window services | `IHost` in `core/app/Host.h` |
| File I/O, clock, locale | `core/base/Platform.h` |

### Data model

```
Workspace -> Session[] (one active)
Session   -> SplitNode tree; every leaf holds one Pane
Pane      -> Tab[] (one active)
Tab       -> path + listing + view state + history
```

All sessions stay resident so switching is an index change. Non-active tabs of
a backgrounded session drop their listings to keep memory flat.

### Control flow

`App` (`core/app/App.cpp`) is the only dispatch point. The UI never reacts to
raw key input except in text fields: the key map turns a chord into a `Cmd` and
`App::Execute` runs it. That is what makes "rebind anything" structural rather
than a feature.

Directory enumeration never runs on the UI thread — a cold network share blocks
for seconds inside a single `FindFirstFile`. Requests go through
`DirectoryLoader`, results are collected in `App::PumpLoader` after
`IHost::Wake()`.

## How to add things

**A command** — one line in `KITE_COMMAND_LIST` (`core/input/Commands.h`), one
`case` in `App::Execute`, a label in both language tables
(`core/i18n/Strings.cpp`), and usually a default binding in
`core/input/KeyMap.cpp`. `test_strings.cpp` fails if a label is missing, and
`test_keymap.cpp` fails on a duplicate chord.

**A user-visible string** — add to both `kEn` and `kJa` in
`core/i18n/Strings.cpp`. Never hard-code display text anywhere else.

**A theme colour** — field in `Theme`, defaults in `Theme::Dark()` and
`Theme::Light()`, plus a `ReadColor` line in `Theme::ApplyIni`.

**A new language** — no rebuild needed: users drop `lang.<code>.ini` into the
config folder. Built-in tables exist only for `en` and `ja`.

## Conventions

- Strings are **UTF-8 everywhere**. UTF-16 exists only inside
  `platform/win/`, converted at the boundary by `WinUtf.h`.
- Comments explain *why*, not *what*. Do not narrate the code.
- 4-space indent, 100-column limit, `.clang-format` is authoritative.
- `/W4 /permissive-` and the build is warning-clean; keep it that way.
- No third-party dependencies. The test harness is 100 lines for this reason.

## Traps already hit here

Recorded because each one cost real time.

- **`<windows.h>` macros.** `CreateDirectory` and `CreateFile` are macros, so
  the filesystem interface uses `MakeDirectory` / `MakeFile`. Watch for the same
  with `GetObject`, `SendMessage`, `min`/`max` (`NOMINMAX` is set).
- **`WIN32_LEAN_AND_MEAN`** keeps `ole2.h` out of `windows.h`. Any header that
  mentions `IDataObject`, `IDropTarget` etc. must include `<objidl.h>` itself.
- **`ID2D1HwndRenderTarget` is not usable.** On at least one Intel driver its
  `EndDraw` returns `S_OK` while nothing ever reaches the screen — reproduced
  with a 40-line standalone program. The renderer uses the modern path instead:
  D3D11 device → `ID2D1Device` → `ID2D1DeviceContext` → DXGI flip swap chain.
  Do not "simplify" it back.
- **The D2D target must be created after the window is visible.** It is built
  lazily on the first `BeginFrame` for that reason.
- **`ReadDirectoryChangesW` handle lifetime.** Closing a directory handle with a
  read outstanding delivers a completion afterwards. `WinDirectoryWatcher` owns
  every handle on its worker thread and frees an entry only once its
  cancellation has arrived; `Watch`/`Unwatch` merely post commands.
- **Cloud placeholders.** Never read file contents during enumeration, and never
  open a file directly — hand it to the shell. `FILE_ATTRIBUTE_RECALL_ON_OPEN`
  and `RECALL_ON_DATA_ACCESS` mark files that only exist in the cloud;
  touching them triggers a download.
- **Shell icons are deliberately not used.** `SHGetFileInfo` calls into the
  shell (and therefore cloud providers) per row. Icons are vector-drawn in
  `ui/Glyphs.cpp`.
- **Test macros copy their operands.** `KITE_EXPECT_EQ` uses `const auto`, not
  `auto&&`: lifetime extension does not reach through a member call, so
  `container().front()` bound by reference dangles and silently corrupts the
  comparison.

## Known gaps

- `IContextMenu` handlers still run **in-process**. They are wrapped in SEH
  guards, but a faulting Box/Google Drive extension can still take the app down.
  The planned fix is an out-of-process `kite_shellhost.exe`; the
  `IShellIntegration` boundary already passes nothing but paths and a screen
  position, so the interface will not change.
- No thumbnails, no search, no virtual folders (ZIP, "This PC", Recycle Bin).
- Key sequences are single chords only; `Chord` is shaped to grow into
  two-key sequences.
