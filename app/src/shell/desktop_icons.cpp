#include "shell/desktop_icons.h"

namespace DesktopIcons {

namespace {

bool g_hiddenByUs = false;

// Set by WatchExternalVisibility when EXPLORER hid the icon list (desktop
// right-click, View, Show desktop icons) and the fences were parked on
// SHELLDLL_DefView to survive it. Distinct from g_hiddenByUs, which means WE
// hid the list via Toggle.
bool g_parkedForExplorer = false;

void ReparentAll(const std::vector<HWND>& fences, HWND newParent) {
    for (HWND fence : fences) ReparentFence(fence, newParent);
}

}  // namespace

HWND FindList() {
    HWND progman = FindWindowW(L"Progman", nullptr);
    if (!progman) return nullptr;
    HWND defview = FindWindowExW(progman, nullptr, L"SHELLDLL_DefView", nullptr);
    if (!defview) return nullptr;
    return FindWindowExW(defview, nullptr, L"SysListView32", nullptr);
}

// SetParent across the process boundary to explorer-owned parents is supported
// (the fences already cross it at birth), but only for windows that are
// children. HWND_TOP keeps the fence above its new siblings, matching the way
// fences sit above the desktop icons under the list view.
void ReparentFence(HWND fence, HWND newParent) {
    if (!fence || !IsWindow(fence) || !newParent) return;
    if (!(GetWindowLongW(fence, GWL_STYLE) & WS_CHILD)) return;
    if (GetParent(fence) == newParent) return;
    RECT rc;
    if (!GetWindowRect(fence, &rc)) return;
    if (!SetParent(fence, newParent)) return;
    POINT p{ rc.left, rc.top };
    ScreenToClient(newParent, &p);
    SetWindowPos(fence, HWND_TOP, p.x, p.y, rc.right - rc.left, rc.bottom - rc.top,
                 SWP_NOACTIVATE);
}

bool HiddenByUs() { return g_hiddenByUs; }

void Toggle(const std::vector<HWND>& fences) {
    HWND lv = FindList();
    HWND defview = lv ? GetParent(lv) : nullptr;
    if (!lv || !defview) return;
    // The fences are children of the icon list, so plainly hiding the list
    // would hide them too. Instead park them one level up for as long as the
    // list stays hidden.
    if (!g_hiddenByUs) {
        ReparentAll(fences, defview);
        ShowWindow(lv, SW_HIDE);
        g_hiddenByUs = true;
    } else {
        ShowWindow(lv, SW_SHOW);
        ReparentAll(fences, lv);
        g_hiddenByUs = false;
    }
}

// Explorer's own switch hides SysListView32 from outside our control. The
// fences are its WS_CHILD windows, so they vanish with it; Win32 gives a child
// no way to opt out of an ancestor's visibility. Poll for the external hide and
// park the fences on SHELLDLL_DefView for as long as the list is gone, then move
// them back when the list returns. (If explorer hides DefView as well there is
// no visible desktop surface left to sit on; the fences simply reappear with
// the tree.) Called from the 300 ms owner timer; IsWindowVisible is cheap.
void WatchExternalVisibility(const std::vector<HWND>& fences) {
    if (g_hiddenByUs) return;              // our own toggle owns the parking
    HWND lv = FindList();
    if (!lv) return;
    bool shown = IsWindowVisible(lv) != FALSE;
    if (!shown && !g_parkedForExplorer) {
        HWND defview = GetParent(lv);
        if (!defview) return;
        ReparentAll(fences, defview);
        g_parkedForExplorer = true;
    } else if (shown && g_parkedForExplorer) {
        ReparentAll(fences, lv);
        g_parkedForExplorer = false;
    }
}

void ParkIfHidden(HWND fence) {
    if (!g_hiddenByUs && !g_parkedForExplorer) return;
    // It was created under the hidden icon list; park it with the rest.
    HWND lv = FindList();
    if (lv) ReparentFence(fence, GetParent(lv));
}

// The fences are recreated under the fresh icon list; drop the parked state (the
// watcher re-parks on the next tick if explorer still hides the list).
void BeginRebuild() { g_parkedForExplorer = false; }

// The fresh desktop came back with its icons visible; if the user had them
// hidden, re-hide them (and park the fences on SHELLDLL_DefView again, exactly
// like Toggle does).
void EndRebuild(const std::vector<HWND>& fences) {
    if (!g_hiddenByUs) return;
    HWND lv = FindList();
    HWND defview = lv ? GetParent(lv) : nullptr;
    if (lv && defview) {
        ReparentAll(fences, defview);
        ShowWindow(lv, SW_HIDE);
    } else {
        g_hiddenByUs = false;   // no icon list right now; drop stale state
    }
}

}  // namespace DesktopIcons
