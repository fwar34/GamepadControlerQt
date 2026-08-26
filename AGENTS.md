# AGENTS.md

## Project

Windows native gamepad → keyboard/mouse mapper (Qt Widgets GUI). Single-package C++17 project, no monorepo. Ported from `L:\steamlike` Android project, stripped of TCP bridge and Android services.

## Build

CMake 3.16+ / Ninja / Qt Widgets (Qt 5.12+ or Qt 6). Two separate build directories for the two Qt versions:

```powershell
# Qt 6 (msys2 UCRT64)
cmake -S . -B build-qt6 -G "Ninja" `
  -DCMAKE_PREFIX_PATH=I:/msys64/ucrt64 `
  -DCMAKE_CXX_COMPILER=I:/msys64/ucrt64/bin/g++.exe `
  -DCMAKE_MAKE_PROGRAM=I:/msys64/ucrt64/bin/ninja.exe `
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-qt6

# Qt 5 (H:\Qt\5.15.2\mingw81_64)
cmake -S . -B build -G "Ninja" `
  -DCMAKE_PREFIX_PATH=H:/Qt/5.15.2/mingw81_64 `
  -DCMAKE_CXX_COMPILER=H:/Qt/Tools/mingw810_64/bin/g++.exe `
  -DCMAKE_MAKE_PROGRAM=I:/msys64/ucrt64/bin/ninja.exe `
  -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Qt 5 runtime requires `H:\Qt\5.15.2\mingw81_64\bin` on PATH.

## Architecture

Single-direction data flow:

```
XInputGamepadSource → SteamInput → KeyboardMouseMapper → Windows SendInput
```

- **XInputGamepadSource** (`src/gamepad/`): QTimer 125Hz polling XInput, emits buttonChanged/stickChanged signals
- **SteamInput** (`src/core/SteamInput.h`): Mapping engine — operation-set switching (`switchOperationSet`) + layer management + key query + input dispatch
- **KeyboardMouseMapper** (`src/core/KeyboardMouseMapper.h`): Keyboard/mouse executor, owns dedicated look thread
- **InputInjector** (`src/core/InputInjector.h`): Injector interface + Windows SendInput implementation

## Data Model (three layers, top → bottom)

`OperationSet` (container) → `commonLayer` + up to 10 `OperationLayer`s → per-button `KeyMapping`. `ControllerProfile` holds a list of `operationSets` plus `activeOperationSetId`. Switching an operation set swaps all layers beneath it as a whole; runtime layer queries only run inside the currently active set.

## Critical Design Rules

- **KeyCode uses Android constants**: Config and core layer store Android KeyEvent values (Space=62, W=51, etc.). Runtime converts to Windows VK via `androidKeyCodeToWindowsVK()`. Never use Windows VK constants in config or core logic.
- **Layer query order**: Inside the current active operation set, from last-activated operation layer back to common layer, returns first hit.
- **Operation-set switch safety**: Before switching / adding / deleting operation sets, always call `deactivateAllLayers()` — `QVector<OperationSet>` growth can invalidate pointers of activated layers (dangling pointer → stuck keys / crash). Keep at least one operation set.
- **Precise release**: Release by "injected state", not current layer mapping — prevents stuck keys on layer switch.
- **Thread model**: Gamepad polling thread runs button/stick mapping via `Qt::DirectConnection` (no event queue); look thread runs fixed 8ms tick for mouse movement; main thread runs `releaseAllInputs` (stop / foreground switch / disconnect). A QMutex serializes injected-state access between gamepad thread and main thread; injector uses QMutex internally.
- **Connection debounce**: 3 consecutive poll failures = disconnect; disconnect calls `releaseAllInputs()`.

## Gotchas

- **UAC / Administrator**: `src/app.manifest` declares `requireAdministrator`. The exe launches elevated because target games (e.g. WoW) run high-integrity — without elevation, SendInput is silently blocked by UIPI. Don't remove the manifest unless you don't need to inject into elevated windows.
- **WIN32 subsystem**: CMake uses `add_executable(... WIN32 ...)` — no console window. Debug output goes to Qt debug or a file.
- **Signal connections use DirectConnection**: Gamepad polling thread → SteamInput, and SteamInput → KeyboardMouseMapper, both use `Qt::DirectConnection` for low latency. Handlers run on the gamepad thread, never the GUI thread. Never fall back to QueuedConnection: if injection ran on the main thread, an injected mouse-press landing on this program's own title bar enters Windows' non-client modal tracking loop that blocks the main thread, so the injected mouse-up can never be queued → stuck keys / frozen UI. DirectConnection lets the gamepad thread still send mouse-up and break the modal loop.
- **Factory returns raw pointer**: `createWindowsInputInjector()` returns `InputInjector*`; caller must `delete` it.

## Source Encoding

CMakeLists.txt enforces UTF-8 (`-finput-charset=UTF-8 -fexec-charset=UTF-8` / `/utf-8`). Source files may contain Chinese comments and strings.

## Config File

- Path: `<exe dir>\steamlike_config.json`
- Format: JSON version=2, compatible with Android `steamlike_config.json`
- Root holds `activeOperationSet` + `operationSets` array (each set: `id`/`name`/`commonLayer`/`layers`). Old v2 configs (top-level `commonLayer`/`layers`) auto-wrap into a single "默认操作集" (Set1) on load.
- `triggerButton` is UI-only display, does not affect runtime layer switching (actual switching driven by common layer's SwitchLayer mapping)
- `.gitignore` excludes `steamlike_config.json`

## Git

- Commit messages in Chinese (per `.trae/rules/git-commit-message.md`)
- If `git push` fails, check for local proxy config and push through it

## No Tests / No CI / No Linter

Verification is compile-pass + real gamepad testing.
