# openFences

An open-source alternative to Stardock Fences: a Windows desktop icon grouping tool built natively with C++/Win32.

## Core architecture

- **Language:** C++17 (MSVC v141 toolchain, VS2017), `CMAKE_CXX_STANDARD 20`
- **Windows:** native Win32 `WS_CHILD` windows attached under explorer's `SysListView32` desktop icon list
- **Rendering:** Direct2D + DirectWrite + WIC, `WS_EX_LAYERED` + `UpdateLayeredWindow` pipeline
- **Serialization:** nlohmann/json, config file `%APPDATA%\openFences\config.json`
- **Build:** CMake 3.20+, MSVC (VS2017 generator), x64, `/MT` statically linked CRT
- **Output:** a single exe (~780 KB), no dependencies, portable

## Source files (`app/src/`, headers in `app/include/`)

| File | Responsibility |
|------|----------------|
| `main.cpp` | Entry point, tray menu, desktop icon management, hide/show scheduling |
| `fence_window.cpp` | Fence window: create, drag, resize, collapse, rename, context menu |
| `render.cpp` | Direct2D rendering pipeline: frosted glass (Acrylic), border, text, layout |
| `context_menu.cpp` | Windows 11 style Fluent custom-drawn menu + low-level hook for dismissal |
| `settings_panel.cpp` | Appearance settings panel (background colour, title colour, alignment, font size, ...) |
| `config.cpp` | Configuration persistence (read/write JSON, autosave, autostart registry key) |
| `localization.cpp` | Key-based localization: loads `app/resources/lang/*.json` (embedded + external), system language detection, `L10n::Tr(key)` |
| `icon_extract.cpp` | Extracts icon pixel data from files and shortcuts |
| `menu_icons.cpp` | Programmatic rasterisation of menu item icons (vector glyphs) |

## Build and run

```bash
cmake -B build -G "Visual Studio 15 2017" -A x64
cmake --build build --config Release --target openfences
build/Release/openfences.exe
```

`build/` is build output and is git-ignored; never commit it. Research spikes live in `tools/` (build with `-DOPENFENCES_BUILD_TOOLS=ON`); `experiments/legacy-hook/` is unused; design notes are in `docs/`.

## Coding conventions

- No comments that explain *what* the code does (names are the documentation); comments only explain *why*
- No heap allocations per frame or on hot paths (reuse brushes via `SetColor`)
- All GDI/D2D/WIC resources are released RAII-style when the window is destroyed
- Consistent UI: colours, spacing and fonts are defined as global constants, never hard-coded
- User-visible strings go through `L10n::Tr("key")`, never inline literals; add every new key to **all** files in `app/resources/lang` (see `docs/LOCALIZATION.md`)
