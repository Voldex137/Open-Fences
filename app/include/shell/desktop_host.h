#pragma once

#include <windows.h>

// Helpers for the Windows desktop window tree and display scaling.
namespace Desktop {

// The window fences attach to: the desktop icon list (SysListView32) of the
// shell, or Progman when there is no icon list. Null when explorer is not up.
HWND FindIconListHost();

// The grid the desktop snaps its own icons to, in the icon list's client
// coordinates (physical pixels). `cellW` x `cellH` is the spacing between
// cells; `glyphCenterX` / `glyphTop` locate the glyph of one real desktop icon
// (the anchor cell), so every other cell's glyph sits at that plus whole cells.
struct IconGrid {
    bool ok = false;
    HWND list = nullptr;
    int cellW = 0, cellH = 0;
    int glyphCenterX = 0, glyphTop = 0;
};
IconGrid QueryIconGrid();

// Makes the process DPI aware, preferring Per-Monitor V2. Without it Windows
// virtualizes the process at 96 DPI on scaled displays and bitmap-upscales every
// window (blurry icons and text at 125% / 150%). Per-Monitor V2 is also the
// awareness context explorer's Progman runs with, and CreateWindowExW refuses to
// create a WS_CHILD whose context differs from the parent's. Call before any
// window is created.
void EnableDpiAwareness();

// Process DPI scale factor (1.0 at 96 DPI). The process is DPI aware.
float DpiScale();

// While alive, the calling thread uses the DPI awareness of `host`. The
// desktop parent can run on a different awareness than this process, and
// Windows refuses to create a child window across that boundary.
class HostDpiScope {
public:
    explicit HostDpiScope(HWND host);
    ~HostDpiScope();
    HostDpiScope(const HostDpiScope&) = delete;
    HostDpiScope& operator=(const HostDpiScope&) = delete;
private:
    void* m_previous = nullptr;   // DPI_AWARENESS_CONTEXT to restore, null = nothing to do
};

}  // namespace Desktop
