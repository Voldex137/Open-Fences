#include "menu/context_menu.h"
#include "menu/fluent_menu_impl.h"
#include "shell/desktop_host.h"
#include <windowsx.h>
#include <algorithm>

// FluentMenu: a self-drawn Windows 11 style context menu. We cannot show
// Explorer's real XAML menu from another process, so the fully populated
// legacy HMENU (text, icons, state, submenus) is read and repainted in the Win11
// dark Fluent style. See context_menu.h for the rationale. The pieces:
//   menu_model.cpp        the model built by walking the HMENU, icon extraction
//   menu_popup*.cpp       one layered Direct2D window per open menu level
//   menu_hook.cpp         the mouse hook that dismisses the menu
//   context_menu.cpp      the session (nested message loop) and the entry points

namespace FluentMenuImpl {

// ── Drawing ──

// ── Interaction ──

// ── Session ──

MenuPopup* MenuSession::PopupAt(HWND h) const {
    if (!h) return nullptr;
    for (auto* p : popups)
        if (p && p->hwnd == h) return p;
    return nullptr;
}

void MenuSession::CloseDeeperThan(int depth) {
    while ((int)popups.size() > depth + 1) {
        MenuPopup* p = popups.back();
        popups.pop_back();
        if (p->hwnd) DestroyWindow(p->hwnd);
        delete p;
    }
}

int MenuSession::RunLoop() {
    AcquireMenuHook();
    MSG msg;
    while (!closed) {
        BOOL got = GetMessageW(&msg, nullptr, 0, 0);
        if (got == 0) {                        // WM_QUIT: re-post and bail out
            PostQuitMessage((int)msg.wParam);
            break;
        }
        if (got == -1) continue;

        MenuPopup* p = PopupAt(msg.hwnd);

        // A click starting outside every menu level dismisses the session and
        // is swallowed so it cannot leak through to a fence (drag / menu).
        if (IsButtonDownMsg(msg.message) && !p) { closed = true; break; }

        // Another application taking activation dismisses the menu too.
        if (msg.message == WM_ACTIVATE && LOWORD(msg.wParam) == WA_INACTIVE && p) {
            closed = true; break;
        }

        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    ReleaseMenuHook();
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Entry point
// ─────────────────────────────────────────────────────────────────────────────

}  // namespace FluentMenuImpl

using namespace FluentMenuImpl;

int FluentMenu::Run(HWND owner, HMENU menu, POINT ptScreen) {
    // Plain-HMENU entry point (no shell verb list): delegates to the full
    // pipeline with a null IContextMenu.
    return Run(owner, nullptr, menu, std::wstring(), ptScreen);
}

// Guard against re-entrant FluentMenu::Run calls: a right-click on a fence or
// tray icon while a menu is already open would nest a second session on top,
// creating a persistent stack of unclosable popups.  Fall back to the classic
// menu (TrackPopupMenu) instead.
static LONG s_flRunDepth = 0;

int FluentMenu::Run(HWND owner, IContextMenu* cm, HMENU menu,
                    const std::wstring& filePath, POINT ptScreen) {
    if (!menu) return -1;
    if (InterlockedIncrement(&s_flRunDepth) > 1) {
        InterlockedDecrement(&s_flRunDepth);
        return -1;   // already showing a Fluent menu on this thread
    }

    MenuSession sess;
    sess.owner = owner;
    sess.cm = cm;
    sess.filePath = filePath;
    sess.s = Desktop::DpiScale();

    // Text formats shared by every popup level.
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
            __uuidof(IDWriteFactory), (IUnknown**)&sess.dw)) || !sess.dw)
        return -1;
    float fontPx = sess.mt.fontPx * sess.s;
    sess.dw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, fontPx, L"en-US",
        &sess.fmt);
    sess.dw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, fontPx, L"en-US",
        &sess.fmtSemi);
    if (!sess.fmt) return -1;
    if (!sess.fmtSemi) sess.fmtSemi = sess.fmt;

    if (!sess.Build(menu)) return -1;          // nothing to show → classic fallback

    bool shown = false;
    auto* root = new MenuPopup();
    if (root->Create(&sess, &sess.model, 0, nullptr, -1, ptScreen)) {
        sess.popups.push_back(root);
        shown = true;
        sess.RunLoop();
    }

    // Tear down every level (deepest first), then the objects.
    while (!sess.popups.empty()) {
        MenuPopup* p = sess.popups.back();
        sess.popups.pop_back();
        if (p->hwnd) DestroyWindow(p->hwnd);
        delete p;
    }

    int ret = shown ? sess.result : -1;
    InterlockedDecrement(&s_flRunDepth);
    return ret;
}
