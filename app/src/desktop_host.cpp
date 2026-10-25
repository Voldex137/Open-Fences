#include "desktop_host.h"

#ifndef DPI_AWARENESS_CONTEXT_UNAWARE
DECLARE_HANDLE(DPI_AWARENESS_CONTEXT);
#endif
typedef DPI_AWARENESS_CONTEXT (WINAPI *GetWinDpiCtxT)(HWND);
typedef DPI_AWARENESS_CONTEXT (WINAPI *SetThreadDpiCtxT)(DPI_AWARENESS_CONTEXT);

namespace Desktop {

float DpiScale() {
    HDC h = GetDC(nullptr);
    int d = GetDeviceCaps(h, LOGPIXELSX);
    ReleaseDC(nullptr, h);
    return d / 96.0f;
}

HostDpiScope::HostDpiScope(HWND host) {
    if (!host) return;
    HMODULE u32 = GetModuleHandleW(L"user32.dll");
    auto getCtx = (GetWinDpiCtxT)(void*)GetProcAddress(u32, "GetWindowDpiAwarenessContext");
    auto setCtx = (SetThreadDpiCtxT)(void*)GetProcAddress(u32, "SetThreadDpiAwarenessContext");
    if (!getCtx || !setCtx) return;
    DPI_AWARENESS_CONTEXT hostCtx = getCtx(host);
    m_previous = hostCtx ? (void*)setCtx(hostCtx) : nullptr;
}

HostDpiScope::~HostDpiScope() {
    if (!m_previous) return;
    HMODULE u32 = GetModuleHandleW(L"user32.dll");
    auto setCtx = (SetThreadDpiCtxT)(void*)GetProcAddress(u32, "SetThreadDpiAwarenessContext");
    if (setCtx) setCtx((DPI_AWARENESS_CONTEXT)m_previous);
}

HWND FindIconListHost() {
    // On Windows 11 the desktop is one layered surface owned by
    // SHELLDLL_DefView (it paints the wallpaper *and* composites the icon
    // list). Arbitrary children of Progman/WorkerW are never painted into
    // that surface and therefore never appear on screen. The one window
    // whose normal child painting the shell does composite is the desktop
    // icon list (SysListView32), so fences parent themselves to it — they
    // render, and they become part of the desktop tree (immune to Win+D).
    auto listViewOf = [](HWND shell) -> HWND {
        if (!shell) return nullptr;
        for (HWND dv = FindWindowExW(shell, nullptr, L"SHELLDLL_DefView", nullptr); dv;
             dv = FindWindowExW(shell, dv, L"SHELLDLL_DefView", nullptr)) {
            HWND lv = FindWindowExW(dv, nullptr, L"SysListView32", nullptr);
            if (lv) return lv;
        }
        return nullptr;
    };
    HWND progman = FindWindowW(L"Progman", nullptr);
    if (!progman) return nullptr;
    if (HWND lv = listViewOf(progman)) return lv;
    // Wallpaper/slideshow layouts: Progman → WorkerW → SHELLDLL_DefView
    for (HWND ww = FindWindowExW(progman, nullptr, L"WorkerW", nullptr); ww;
         ww = FindWindowExW(progman, ww, L"WorkerW", nullptr))
        if (HWND lv = listViewOf(ww)) return lv;
    // Rare shell states put the WorkerW at the top level
    for (HWND ww = FindWindowExW(nullptr, nullptr, L"WorkerW", nullptr); ww;
         ww = FindWindowExW(nullptr, ww, L"WorkerW", nullptr))
        if (HWND lv = listViewOf(ww)) return lv;
    return progman;   // no icon list (icons disabled?) — best effort
}

}  // namespace Desktop
