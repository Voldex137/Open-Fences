#include "menu/fluent_menu_impl.h"

namespace FluentMenuImpl {

namespace {
// One WH_MOUSE_LL hook shared by every menu session; installed while any menu
// can be open, removed when the last one closes.  The instance count handles
// re-entrant sessions too (though a guard in Run now prevents stacking via
// FluentMenu — the counter still protects against the classic TrackPopupMenu
// fallback that nests).
static HHOOK  s_llHook = nullptr;
static LONG   s_hookInstances = 0;

// Low-level mouse hook: any button-down that lands OUTSIDE every open
// FluentMenu window dismisses the session(s) and swallows the click so it
// cannot start a fence drag / marquee selection / app activation shift.
// This works regardless of foreground status — unlike WM_ACTIVATE, which
// only fires when the focus actually changes (a desktop click often does
// not change it, and tray icon right-clicks don't reliably make us
// foreground in the first place).
static LRESULT CALLBACK LLMouseProc(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION && IsButtonDownMsg((UINT)wp)) {
        auto* ms = (const MSLLHOOKSTRUCT*)lp;
        POINT pt = ms->pt;
        bool inside = false;
        HWND m = nullptr;
        while ((m = FindWindowExW(nullptr, m, L"OpenFencesFluentMenu", nullptr)) != nullptr) {
            RECT r{}; GetWindowRect(m, &r);
            if (PtInRect(&r, pt)) { inside = true; break; }
        }
        if (!inside) {
            m = nullptr;
            while ((m = FindWindowExW(nullptr, m, L"OpenFencesFluentMenu", nullptr)) != nullptr)
                PostMessageW(m, WM_MENUDISMISS, 0, 0);
            return 1;   // swallow the dismissal click
        }
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}

}  // namespace

void AcquireMenuHook() {
    if (InterlockedIncrement(&s_hookInstances) == 1)
        s_llHook = SetWindowsHookExW(WH_MOUSE_LL, LLMouseProc,
                                     GetModuleHandleW(nullptr), 0);
}
void ReleaseMenuHook() {
    if (InterlockedDecrement(&s_hookInstances) == 0) {
        if (s_llHook) { UnhookWindowsHookEx(s_llHook); s_llHook = nullptr; }
    }
}

}  // namespace FluentMenuImpl
