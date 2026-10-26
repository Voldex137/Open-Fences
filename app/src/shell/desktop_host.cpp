#include "shell/desktop_host.h"
#include <commctrl.h>

#ifndef DPI_AWARENESS_CONTEXT_UNAWARE
DECLARE_HANDLE(DPI_AWARENESS_CONTEXT);
#endif
typedef DPI_AWARENESS_CONTEXT (WINAPI *GetWinDpiCtxT)(HWND);
typedef DPI_AWARENESS_CONTEXT (WINAPI *SetThreadDpiCtxT)(DPI_AWARENESS_CONTEXT);

namespace Desktop {

void EnableDpiAwareness() {
    HMODULE u32 = GetModuleHandleW(L"user32.dll");
    if (u32) {
        typedef BOOL (WINAPI* SetCtxT)(HANDLE);
        auto setCtx = (SetCtxT)(void*)GetProcAddress(u32, "SetProcessDpiAwarenessContext");
        if (setCtx && setCtx((HANDLE)-4 /* DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 */)) return;
    }
    HMODULE shcore = LoadLibraryW(L"shcore.dll");
    if (shcore) {
        typedef HRESULT (WINAPI* SetDpiAwarenessT)(int);
        auto setAwareness = (SetDpiAwarenessT)(void*)GetProcAddress(shcore, "SetProcessDpiAwareness");
        HRESULT hr = setAwareness ? setAwareness(2 /* PROCESS_SYSTEM_DPI_AWARE */) : E_FAIL;
        FreeLibrary(shcore);
        if (SUCCEEDED(hr)) return;
    }
    SetProcessDPIAware();
}

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

IconGrid QueryIconGrid() {
    IconGrid g;
    HWND lv = FindIconListHost();
    wchar_t cls[64] = {};
    if (!lv || !GetClassNameW(lv, cls, 64) || wcscmp(cls, L"SysListView32")) return g;

    DWORD_PTR spacing = 0;
    if (!SendMessageTimeoutW(lv, LVM_GETITEMSPACING, FALSE, 0, SMTO_ABORTIFHUNG, 250, &spacing))
        return g;
    g.cellW = (int)LOWORD(spacing);
    g.cellH = (int)HIWORD(spacing);
    if (g.cellW < 16 || g.cellH < 16) return g;

    // Anchor: where the glyph of the first desktop icon is. LVM_GETITEMRECT fills
    // a RECT in explorer's address space, hence the remote buffer. Without any
    // icon the anchor is estimated from the spacing.
    g.glyphCenterX = g.cellW / 2;
    g.glyphTop = 4;
    DWORD pid = 0;
    GetWindowThreadProcessId(lv, &pid);
    HANDLE proc = OpenProcess(PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE, FALSE, pid);
    if (proc) {
        void* mem = VirtualAllocEx(proc, nullptr, sizeof(RECT), MEM_COMMIT, PAGE_READWRITE);
        if (mem) {
            RECT in = {};
            in.left = LVIR_ICON;   // LVM_GETITEMRECT reads the kind here
            DWORD_PTR res = 0;
            RECT icon = {};
            if (WriteProcessMemory(proc, mem, &in, sizeof(in), nullptr) &&
                SendMessageTimeoutW(lv, LVM_GETITEMRECT, 0, (LPARAM)mem, SMTO_ABORTIFHUNG, 250, &res) &&
                res && ReadProcessMemory(proc, mem, &icon, sizeof(icon), nullptr) &&
                icon.right > icon.left) {
                g.glyphCenterX = (icon.left + icon.right) / 2;
                g.glyphTop = icon.top + 2;   // the icon rect has 2px of padding above the glyph
            }
            VirtualFreeEx(proc, mem, 0, MEM_RELEASE);
        }
        CloseHandle(proc);
    }
    g.list = lv;
    g.ok = true;
    return g;
}

}  // namespace Desktop
