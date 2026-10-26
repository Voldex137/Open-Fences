# openFences

**English** | [Русский](README.ru.md) | [简体中文](README.zh-CN.md)

> A lightweight Windows desktop icon grouping tool — an open-source alternative to Stardock Fences.

![platform](https://img.shields.io/badge/platform-Windows%2010%2F11-blue)
![lang](https://img.shields.io/badge/language-C%2B%2B17-%23f34b7d)
![license](https://img.shields.io/badge/license-MIT-green)

This repository is a fork of [weiweigogo/openFrence](https://github.com/weiweigogo/openFrence) (MIT). The fork focuses on documentation in English, Russian and Chinese, proper multi-language UI support (Russian added), and, longer term, a Linux port (see [Roadmap](#roadmap)).

---

## Features

- **Desktop icon grouping** — create translucent frosted-glass "fences" on the desktop; drag icons into them to group them
- **Frosted glass look** — Acrylic-style background: downsampled blur, saturation boost and noise, close to the native Windows 11 look
- **Windows 11 style context menu** — custom-drawn Fluent Design menu with rounded corners, blur background and soft shadow
- **System tray** — new fence, hide/show everything, language, start with Windows, desktop shortcut
- **Persistent configuration** — fence layout, icon positions and appearance are saved to `%APPDATA%\Open Fences\config.json`
- **Multi-language UI** — English, Russian and Chinese; the language follows Windows by default, and a new one is just a JSON file (see [docs/LOCALIZATION.md](docs/LOCALIZATION.md))
- **Tiny** — a single exe (~780 KB), no dependencies, no VC++ runtime needed, portable

---

## Quick start

> **Security note.** Upstream committed prebuilt `.exe`/`.dll` files into its repository; this fork removed them. Do not run prebuilt binaries from unknown sources, and prefer building from source. The current app does not inject any DLL into Explorer (the `experiments/legacy-hook` directory is an unused leftover of an early design).

1. Build it from source (see [Build](#build)).
2. Put `openfences.exe` anywhere and run it.
3. Right-click the tray icon to set the language, enable autostart or create a desktop shortcut.
4. Drag desktop icons into a fence.

> **Tip:** if desktop icons are hidden and the fences disappear, right-click the tray icon and choose "Show desktop icons". The fences wait on the desktop view until the icon list returns.

---

## Build

```bash
# Prerequisites: Visual Studio 2017+ (with the Windows 10 SDK), CMake 3.20+

git clone <this repository>
cd openFences
cmake -B build -G "Visual Studio 15 2017" -A x64
cmake --build build --config Release --target openfences
```

Output: `build/Release/openfences.exe` (~780 KB, statically linked CRT).

---

## Project layout

```
openFences/
├── CMakeLists.txt
├── app/                       # the application
│   ├── include/               # headers, mirrored by the folders under src/
│   ├── src/
│   │   ├── main.cpp           # entry point, owner window (timers, hotkey)
│   │   ├── fence/             # the fence window and the list of fences: layout, mouse, keyboard, menus, sorting, mapped folders
│   │   ├── render/            # Direct2D rendering, backdrop filters, icon extraction, menu glyphs
│   │   ├── menu/              # Windows 11 style context menu, Explorer shell menus
│   │   ├── settings/          # appearance settings panel
│   │   ├── shell/             # desktop host / DPI, desktop icon list, Explorer sort order
│   │   ├── tray/              # notification-area icon and menu
│   │   └── core/              # config.json, localization, COM pointer helper, resource ids
│   └── resources/             # app.ico, lang/*.json (one file per UI language)
├── tools/                     # research spikes and diagnostics (off by default)
├── tests/                     # unit tests (localization, icon grid, icon sorting, image filters, list scrollbar)
├── experiments/legacy-hook/   # unused DLL from an early design
├── docs/                      # design notes, LOCALIZATION.md, BUGS.md
├── CLAUDE.md                  # development guide for AI assistants
└── README*.md
```

---

## Configuration

Config file: `%APPDATA%\Open Fences\config.json`

```json
{
  "version": 1,
  "language": "en",
  "hideAll": false,
  "fences": [
    {
      "id": "f1",
      "title": "Apps",
      "x": 75, "y": 75,
      "w": 420, "h": 420,
      "collapsed": false,
      "icons": [ ... ],
      "bg": [0.08, 0.08, 0.10, 0.55],
      "titleColor": [0.05, 0.05, 0.07, 0.80],
      "fontSize": 18.0,
      "titleAlign": 0
    }
  ]
}
```

`language`: a language code (`en`, `ru`, `zh-CN`). Missing means "follow the Windows language". Old numeric values (`0` = English, `1` = Chinese) are converted on load.

---

## Tech stack

- **Language:** C++17
- **Windows:** Win32 `WS_CHILD` + `WS_EX_LAYERED` + `UpdateLayeredWindow`
- **Rendering:** Direct2D, DirectWrite, WIC
- **JSON:** [nlohmann/json](https://github.com/nlohmann/json)
- **Build:** CMake + MSVC (VS2017+), x64, static CRT (`/MT`)

---

## Roadmap

- [x] Trilingual documentation (English, Russian, Chinese)
- [x] Russian UI and a key-based localization system (one JSON file per language, system language detection)
- [ ] Remove committed build artifacts from version control
- [ ] Linux port (tracked upstream in issue #8). The current code is Win32 / Direct2D, so this means a new UI backend; the desktop-icon model differs per desktop environment (KDE Plasma, GNOME, ...)

---

## License

[MIT](LICENSE). The original copyright notice of the upstream author is kept.

## Acknowledgments

- Upstream project: [weiweigogo/openFrence](https://github.com/weiweigogo/openFrence)
- [nlohmann/json](https://github.com/nlohmann/json)
- Stardock Fences for the original idea
