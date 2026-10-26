# openFences

An open-source alternative to Stardock Fences: a Windows desktop icon grouping tool built natively with C++/Win32.

## Core architecture

- **Language:** C++17 (MSVC v141 toolchain, VS2017), `CMAKE_CXX_STANDARD 20`
- **Windows:** native Win32 `WS_CHILD` windows attached under explorer's `SysListView32` desktop icon list
- **Rendering:** Direct2D + DirectWrite + WIC, `WS_EX_LAYERED` + `UpdateLayeredWindow` pipeline
- **Serialization:** nlohmann/json, config file `%APPDATA%\Open Fences\config.json`
- **Build:** CMake 3.20+, MSVC (VS2017 generator), x64, `/MT` statically linked CRT
- **Output:** a single exe (~780 KB), no dependencies, portable

## Source files (`app/src/<folder>/`, headers in `app/include/<folder>/`)

Includes carry the folder: `#include "fence/fence_window.h"`.

| Folder | Files | Responsibility |
|--------|-------|----------------|
| `src/` | `main.cpp` | Entry point, owner window (timers, hotkey, explorer restart) |
| `fence/` | `fence_manager.cpp` | The list of fences: create, remove, rebuild after an explorer restart, hide-all |
| | `fence_window.cpp` | Window lifecycle, drawing glue, `WndProc` dispatcher |
| | `fence_mouse.cpp`, `fence_keyboard.cpp` | Mouse (press, drag, resize, marquee, wheel) and keyboard/timer handlers |
| | `fence_menu.cpp` | Fence and icon context menus, "Map Folder", shell "New" items |
| | `fence_layout.cpp`, `icon_grid.cpp` | Grid/list layout; pure cell geometry (tested) |
| | `fence_selection.cpp`, `fence_rename.cpp` | Selection and marquee; in-place rename of the fence and of icons |
| | `fence_sort_filter.cpp`, `icon_sort.cpp` | Sort state and search filter; icon ordering (tested) |
| | `fence_source_folder.cpp` | Folder-backed fences: enumeration and change watching |
| | `fence_drop_target.cpp`, `fence_drag_source.cpp` | OLE drop target (files and shell objects); icons dragged out of a fence become a shell drag |
| | `stack_chain.cpp`, `fence_stack.cpp` | Stacks of folded fences: the chain data model (pure, tested) and its application to the windows |
| | `text_edit.cpp` | Single-line editing state with caret and selection (pure, tested); keys and clipboard live in `fence_keyboard.cpp` |
| `render/` | `render.cpp` + `render_styles/_icons/_backdrop/_surface.cpp` | `RenderContext`: brushes and fonts, icon cache, frosted-glass backdrop, DIB/present |
| | `render_draw.cpp` | One frame: title bar, search box, icon grid/list, scrollbar, marquee |
| | `image_filters.cpp`, `list_scrollbar.cpp` | Pure pixel filters (blur, saturation, noise) and scrollbar geometry (tested) |
| | `icon_extract.cpp`, `menu_icons.cpp` | Icon pixel extraction; programmatic menu glyphs |
| `menu/` | `context_menu.cpp` | `FluentMenu::Run` and the menu session (nested message loop) |
| | `menu_model.cpp`, `menu_popup*.cpp`, `menu_hook.cpp` | HMENU model; popup windows (create/layout, draw, input); dismissal mouse hook |
| | `shell_context_menu.cpp` | Explorer's file menu and the shell "New" submenu |
| `settings/` | `settings_panel*.cpp` | Appearance panel: lifecycle, style, layout, drawing, input |
| `shell/` | `desktop_host.cpp`, `desktop_icons.cpp` | Desktop window host, DPI, the desktop icon grid; hiding the icon list while keeping fences |
| | `desktop_selection.cpp` | Mirrors the desktop rubber band onto folded tiles; drags tiles and desktop icons together |
| | `shell_integration.cpp` | Explorer menu entries (HKCU registry verbs) and the command line they send to the running instance |
| | `explorer_sort.cpp` | Explorer's sort order of a folder |
| `tray/` | `tray.cpp` | Notification icon and its menu |
| `core/` | `config.cpp`, `localization.cpp` | `config.json` persistence, autostart; key-based localization (`L10n::Tr`) |
| | `appearance_json.cpp`, `presets.cpp` | A fence's editable look as JSON (tested); default look and named presets |
| | `storage.cpp`, `app_paths.cpp` | The storage folder with one subfolder per fence; settings folder and portable mode (`portable.flag`) |

## Build and run

```bash
cmake -B build -G "Visual Studio 15 2017" -A x64
cmake --build build --config Release --target openfences
build/Release/openfences.exe
```

Packaging: `powershell -File tools/package.ps1` builds Release and writes `dist/OpenFences-<version>-portable.zip` and `-setup.exe` (Inno Setup script in `installer/`). The version comes from `project(... VERSION x.y.z)` in `CMakeLists.txt`. Publish to GitHub (push, tag, release) only when the owner asks.

`build/` is build output and is git-ignored; never commit it. Research spikes live in `tools/` (build with `-DOPENFENCES_BUILD_TOOLS=ON`); `experiments/legacy-hook/` is unused; design notes are in `docs/`.

## Coding conventions

- No comments that explain *what* the code does (names are the documentation); comments only explain *why*
- No heap allocations per frame or on hot paths (reuse brushes via `SetColor`)
- All GDI/D2D/WIC resources are released RAII-style when the window is destroyed
- Consistent UI: colours, spacing and fonts are defined as global constants, never hard-coded
- User-visible strings go through `L10n::Tr("key")`, never inline literals; add every new key to **all** files in `app/resources/lang` (see `docs/LOCALIZATION.md`)
