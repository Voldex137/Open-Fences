# openFences development plan

> Translated from the upstream Chinese document. It is a snapshot of the original author's plan and may be partly out of date; the README and the source are authoritative.

## Current status

| Done | Module | State |
|------|--------|-------|
| ✅ | Data model (Fence, FenceManager, Rect, IconRef) | usable |
| ✅ | Standalone Fence window + native dragging | usable |
| ✅ | Per-pixel alpha rendering (WIC → D2D → GDI DIB → UpdateLayeredWindow) | usable |
| ✅ | Title text (DirectWrite, Segoe UI) | usable |
| ✅ | Context menu (Delete) | usable |
| ✅ | Layout algorithm (grid arrangement, min_fence_size) | has unit tests |
| ✅ | Rule engine (OR matching, several condition types) | has unit tests |
| ✅ | Configuration persistence (JSON, %APPDATA%) | has unit tests |
| ✅ | Theme constants | dark palette |
| ❌ | Fence resize | limited by `WS_EX_LAYERED`; waiting for a DXGI swap chain |
| ❌ | Desktop interaction (icon enumeration, drag in/out) | not implemented |
| ❌ | System tray / lifecycle | not implemented |
| ❌ | Quick hide/show | not implemented |

## TODO: Fence resize

- **Problem:** `WS_EX_LAYERED` windows do not support native system resizing (neither `WM_NCLBUTTONDOWN` + HT codes nor `WM_SYSCOMMAND` + `SC_SIZE` work).
  Doing it manually with `SetWindowPos` / `UpdateLayeredWindow` in `WM_MOUSEMOVE` stutters because DWM composes every frame.
- **Root cause:** the `ID2D1HwndRenderTarget` / `UpdateLayeredWindow` + GDI DIB approach is inherently incompatible with resizing layered windows.
- **Solution:** switch to a DXGI swap chain rendering backend (`DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL` + `DXGI_ALPHA_MODE_PREMULTIPLIED`),
  rendering straight into the window's DXGI swap chain so the window keeps both per-pixel alpha and full native window behaviour (resize, DWM composition).

---

## Upcoming work

### Phase 4: application shell
- 4.1 System tray (`Shell_NotifyIcon` + context menu)
- 4.2 Quick hide/show
- 4.3 Integrate configuration persistence (load on start, save on exit)
- 4.4 Start with Windows (registry `Run` key)

### Phase 5: desktop icon interaction
- 5.1 Icon enumeration (`SysListView32`)
- 5.2 Drawing icons inside a fence
- 5.3 Dragging icons in and out

### Phase 6: advanced features
- Multi-monitor support, layout snapshots, theme following, rule editor UI, resize (DXGI swap chain)

### Fork roadmap (additional)
- Russian UI via a string table keyed by language
- Remove committed build artifacts from version control and reorganise the repository (`src/`, `tools/`, `experiments/`, `docs/`) (done in this fork)
- Linux port (upstream issue #8): new rendering/windowing backend, see the README roadmap
