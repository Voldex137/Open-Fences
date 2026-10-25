# openFences (C++)

> Translated from the upstream Chinese design document. It describes the original design (written after a Rust prototype); the implementation has since diverged in places (for example the app now uses `WS_CHILD` windows under the desktop list view), so treat it as background, not as a specification.

## Goal

An open-source alternative to Stardock Fences 6. C++ + Win32 API + Direct2D, one process + a Shell Extension DLL.

## Key lessons (from the Rust prototype)

1. **Z-order:** `WS_EX_LAYERED` windows are composed independently by DWM and always float on top. The correct approach is a Shell Extension DLL injected into explorer that draws directly on the desktop ListView.
2. **Icon interaction:** desktop icons must be read and written through the `IFolderView2` COM interface. Cross-process `WriteProcessMemory` crashes explorer.
3. **Hook injection:** `SetWindowsHookEx(WH_GETMESSAGE)` plus an exported hook procedure; the DLL runs inside the explorer process.
4. **Per-pixel alpha:** a WIC bitmap + D2D `WicBitmapRenderTarget` + `UpdateLayeredWindow` is the only workable approach.

## Architecture

```
┌─────────────────────────────────┐
│  openfences.exe (main process)  │
│  ┌───────────────────────────┐  │
│  │ Configuration (JSON)      │  │
│  │ Fence management          │  │
│  │ System tray               │  │
│  │ Theme / styles            │  │
│  └───────────────────────────┘  │
│            │                    │
│    WM_COPYDATA (IPC)            │
│            ↓                    │
└─────────────────────────────────┘
             │
┌─────────────────────────────────┐
│  openfences_hook.dll (explorer) │
│  ┌───────────────────────────┐  │
│  │ Hook entry → GetMsgProc   │  │
│  │ Subclass SysListView32    │  │
│  │ D2D overlay drawing       │  │
│  │ LVM_SETITEMPOSITION hook  │  │
│  │ IFolderView2 COM listing  │  │
│  └───────────────────────────┘  │
└─────────────────────────────────┘
```

## Tech stack

| Item | Choice |
|------|--------|
| Language | C++20 |
| Build | CMake 3.20+ |
| Package management | vcpkg (manifest mode) |
| Rendering | Direct2D + DirectWrite + WIC |
| Shell | IFolderView2 COM (`shobjidl.h`) |
| Injection | `SetWindowsHookEx(WH_GETMESSAGE)` |
| JSON | nlohmann/json (header-only) |
| Tests | GoogleTest |

## Project layout

```
openfences/
├── CMakeLists.txt
├── vcpkg.json
├── src/
│   ├── main.cpp              # entry point, COM initialisation
│   ├── app.h/cpp             # application lifecycle
│   ├── fence_manager.h/cpp   # fence data management
│   ├── tray.h/cpp            # system tray
│   ├── config.h/cpp          # JSON persistence
│   ├── theme.h/cpp           # theme constants
│   └── render/
│       ├── d2d_context.h/cpp   # D2D rendering context
│       └── fence_renderer.h/cpp# fence container drawing
├── hook/
│   ├── CMakeLists.txt
│   ├── dllmain.cpp           # DllMain + exported GetMsgProc
│   ├── listview_subclass.h/cpp # SysListView32 subclassing
│   ├── icon_manager.h/cpp    # IFolderView2 icon enumeration
│   └── ipc.h/cpp             # WM_COPYDATA protocol
└── tests/
    ├── CMakeLists.txt
    └── test_config.cpp
```

## Data flow

```
1. The user starts openfences.exe
2. The exe loads config.json → creates the fence data
3. The exe calls LoadLibrary("openfences_hook.dll") → SetWindowsHookEx → injection into explorer
4. The hook DLL's GetMsgProc fires for the first time → finds SysListView32 → subclasses it
5. exe → WM_COPYDATA → DLL: "these are the fence regions [(x,y,w,h), ...]"
6. The DLL subclass procedure intercepts LVM_SETITEMPOSITION:
   - checks whether the target coordinates fall inside a fence
   - inside a fence → constrain to its bounds
   - outside → let it through
7. The DLL enumerates icons through IFolderView2 (name, path, icon image)
8. The exe can send a new fence layout → the DLL updates the constraint regions
9. Exit: exe → WM_COPYDATA clears the fences → unhook → save config
```

## Implementation order

### Phase 1: skeleton (exe process)
- CMake project + vcpkg manifest
- Win32 message loop + window creation
- D2D rendering context + fence rounded rectangle
- Per-pixel alpha (WIC + `UpdateLayeredWindow`)
- System tray + context menu

### Phase 2: fence interaction
- Drag to move (`WM_NCLBUTTONDOWN` + `HTCAPTION`)
- Context menu (appearance adjustments)
- Double-click to rename
- JSON configuration persistence

### Phase 3: hook DLL
- DLL project → `DllMain` + exported `GetMsgProc`
- `GetMsgProc` → on first call find `SysListView32` → `SetWindowSubclass`
- The subclass procedure intercepts `LVM_SETITEMPOSITION` → constrain to the fence region
- Receive `WM_COPYDATA` → update the list of fence coordinates
- IFolderView2 COM → enumerate icons (name + icon image)

### Phase 4: icon rendering
- `SHGetFileInfo` → obtain the `HICON`
- Convert to a D2D bitmap → draw icon + name inside the fence
- Automatic grid layout

### Phase 5: advanced features
- Rule engine UI (automatic categorisation)
- Multi-monitor support
- Follow the system theme (Light/Dark)
- Layout snapshots / restore

---

## Key code snippets

### Hook DLL export

```cpp
// hook/dllmain.cpp
HINSTANCE g_hInst;
HWND g_hListView = NULL;
HHOOK g_hHook = NULL;

BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_hInst = hinst;
        DisableThreadLibraryCalls(hinst);
    }
    return TRUE;
}

extern "C" __declspec(dllexport)
LRESULT CALLBACK GetMsgProc(int code, WPARAM wParam, LPARAM lParam) {
    if (code >= 0 && g_hListView == NULL) {
        // first call: find the desktop ListView and subclass it
        HWND progman = FindWindow(L"Progman", NULL);
        HWND defview = FindWindowEx(progman, NULL, L"SHELLDLL_DefView", NULL);
        g_hListView = FindWindowEx(defview, NULL, L"SysListView32", NULL);
        if (g_hListView) {
            SetWindowSubclass(g_hListView, ListViewSubclassProc, 0, 0);
        }
    }
    return CallNextHookEx(NULL, code, wParam, lParam);
}
```

### Subclass: intercepting icon positions

```cpp
// hook/listview_subclass.cpp
LRESULT CALLBACK ListViewSubclassProc(
    HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
    UINT_PTR, DWORD_PTR)
{
    if (msg == WM_COPYDATA) {
        auto* cds = (COPYDATASTRUCT*)lp;
        if (cds->dwData == 1) { // SET_FENCES
            // update fence coordinates
            g_fences.assign((FenceRect*)cds->lpData,
                (FenceRect*)cds->lpData + cds->cbData / sizeof(FenceRect));
        }
        return TRUE;
    }
    if (msg == LVM_SETITEMPOSITION) {
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        ConstrainToFences(x, y); // constrain to the owning fence
        lp = MAKELPARAM(x, y);
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}
```

### The exe installs the hook

```cpp
// src/app.cpp
HMODULE hDll = LoadLibrary(L"openfences_hook.dll");
auto* GetMsgProc = (HOOKPROC)GetProcAddress(hDll, "GetMsgProc");
DWORD tid = GetWindowThreadProcessId(hListView, NULL);
HHOOK hook = SetWindowsHookEx(WH_GETMESSAGE, GetMsgProc, hDll, tid);
```

### IPC: sending the fence layout

```cpp
// src/app.cpp
COPYDATASTRUCT cds;
cds.dwData = 1; // SET_FENCES
cds.cbData = fences.size() * sizeof(FenceRect);
cds.lpData = fences.data();
SendMessage(hListView, WM_COPYDATA, 0, (LPARAM)&cds);
```

## vcpkg dependencies

```json
{
  "name": "openfences",
  "dependencies": [
    "nlohmann-json"
  ]
}
```

Everything else is a system API: D2D (`d2d1.h`), WIC (`wincodec.h`), DirectWrite (`dwrite.h`), Shell (`shobjidl.h`).

## Single process vs two processes

The final form is two processes (exe + hook DLL), but development starts with a single-process exe and the hook DLL is added in Phase 3. The exe from Phases 1-2 can run and be tested on its own.
