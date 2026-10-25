#include "menu/fluent_menu_impl.h"
#include "menu/context_menu.h"
#include "shell/desktop_host.h"
#include <windowsx.h>
#include <algorithm>
#include <cmath>

namespace FluentMenuImpl {

// Maps a point (given in this popup's surface coordinates) to the popup
// that should handle it. Points inside the surface are handled here.
// Points in the shadow margin that overlap the parent's surface are
// translated into the parent's space and handed to it: a flyout hugs the
// parent's right edge, so its left shadow margin covers the parent's
// chevron column — without this forwarding the parent never sees the
// mouse there and can neither un-highlight its row nor close the flyout.
// Margin points over neither surface stay here (a harmless no-op hover).
MenuPopup* MenuPopup::RoutePoint(int& mx, int& my) {
    if (mx >= 0 && my >= 0 && mx < w && my < h) return this;
    if (parent) {
        int px = mx + (x - parent->x);
        int py = my + (y - parent->y);
        if (px >= 0 && py >= 0 && px < parent->w && py < parent->h) {
            mx = px; my = py;
            return parent;
        }
    }
    return this;
}

int MenuPopup::HitTest(int mx, int my) const {
    float padH = sess->mt.padH * sess->s;
    if (mx < (int)padH || mx > w - (int)padH) return -1;
    float cy = (float)my + (float)scroll;
    for (size_t i = 0; i < rowY.size(); i++)
        if (cy >= rowY[i] && cy < rowY[i] + rowH[i])
            return (*items)[i].sep ? -1 : (int)i;
    return -1;
}

void MenuPopup::SetHover(int idx) {
    if (idx == hover) return;
    hover = idx;
    Repaint();
}

void MenuPopup::TryOpenSub(int idx, bool immediate) {
    if (idx < 0 || idx >= (int)items->size()) return;
    const CtxItem& it = (*items)[idx];
    if (!it.hasSub || it.disabled || it.children.empty()) return;
    if ((int)sess->popups.size() > depth + 1 &&
        sess->popups[depth + 1]->parentItem == idx)
        return;                                  // this row's flyout is already open
    CancelPendingSub();
    if (!immediate) {
        pendingSub = idx;
        openTimer = SetTimer(hwnd, kSubTimerId, kSubOpenDelayMs, nullptr);
        return;
    }
    OpenSub(idx);
}

void MenuPopup::OpenSub(int idx) {
    CancelPendingSub();
    if (idx < 0 || idx >= (int)items->size()) return;
    if ((int)sess->popups.size() > depth + 1) sess->CloseDeeperThan(depth);

    // Align the flyout's first row with this row, hugging the parent edge.
    POINT at;
    at.x = x + w - (int)(4 * sess->s);
    at.y = y + (int)(rowY[idx] - scroll - sess->mt.padV * sess->s);

    auto* child = new MenuPopup();
    if (child->Create(sess, &(*items)[idx].children, depth + 1, this, idx, at))
        sess->popups.push_back(child);
    else
        delete child;
}

void MenuPopup::CancelPendingSub() {
    if (openTimer && hwnd) KillTimer(hwnd, openTimer);
    openTimer = 0;
    pendingSub = -1;
}

void MenuPopup::OnMouse(int mx, int my) {
    if (!tracking) {
        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
        TrackMouseEvent(&tme);
        tracking = true;
    }
    int idx = HitTest(mx, my);
    SetHover(idx);

    // Moving off the row that owns an open flyout closes that flyout — except
    // while the button is held: drag-select stays on this level.
    if (!pressed && (int)sess->popups.size() > depth + 1 &&
        sess->popups[depth + 1]->parentItem != idx) {
        sess->CloseDeeperThan(depth);
    }

    if (pressed) return;                         // no delayed opens mid-drag
    if (idx >= 0 && (*items)[idx].hasSub && !(*items)[idx].disabled) {
        if (pendingSub != idx) { CancelPendingSub(); TryOpenSub(idx, false); }
    } else if (pendingSub >= 0) {
        CancelPendingSub();
    }
}

void MenuPopup::OnLeave() {
    tracking = false;
    CancelPendingSub();
    // Keep the parent row of an open flyout highlighted; otherwise un-hover.
    int keep = -1;
    if ((int)sess->popups.size() > depth + 1 && sess->popups[depth + 1]->parent == this)
        keep = sess->popups[depth + 1]->parentItem;
    SetHover(keep);
}

void MenuPopup::OnClick(int mx, int my) {
    int idx = HitTest(mx, my);
    if (idx < 0) return;
    const CtxItem& it = (*items)[idx];
    if (it.disabled) return;
    SetHover(idx);
    if (it.hasSub) { TryOpenSub(idx, true); return; }
    sess->Select(it.id);
}

void MenuPopup::OnWheel(int delta) {
    if (contentH <= h) return;
    if ((int)sess->popups.size() > depth + 1) sess->CloseDeeperThan(depth);
    float step = sess->mt.rowH * sess->s * 3.0f;
    int ns = clampi(scroll - (int)((delta / (float)WHEEL_DELTA) * step),
                    0, contentH - h);
    if (ns == scroll) return;
    scroll = ns;
    hover = -1;
    Repaint();
}

void MenuPopup::OnKey(UINT vk) {
    int n = (int)items->size();
    auto usable = [&](int i) { return i >= 0 && i < n && !(*items)[i].sep; };

    switch (vk) {
    case VK_DOWN:
    case VK_UP: {
        int dir = (vk == VK_DOWN) ? 1 : -1;
        int i = hover;
        for (int k = 0; k < n; k++) {
            i += dir;
            if (i >= n) i = 0;
            if (i < 0)  i = n - 1;
            if (usable(i)) break;
        }
        if (usable(i)) { SetHover(i); EnsureVisible(i); }
        return;
    }
    case VK_RETURN:
    case VK_RIGHT: {
        if (!usable(hover) || (*items)[hover].disabled) return;
        if (!(*items)[hover].hasSub) {
            if (vk == VK_RETURN) sess->Select((*items)[hover].id);
            return;
        }
        TryOpenSub(hover, true);
        if ((int)sess->popups.size() > depth + 1 &&
            sess->popups[depth + 1]->parentItem == hover) {
            MenuPopup* child = sess->popups[depth + 1];
            SetFocus(child->hwnd);
            for (int c = 0; c < (int)child->items->size(); c++)
                if (!(*child->items)[c].sep) { child->SetHover(c); break; }
        }
        return;
    }
    case VK_ESCAPE:
    case VK_LEFT: {
        if (depth == 0) { sess->CloseAll(); return; }
        // Copy what we need first: CloseDeeperThan destroys `this`.
        MenuPopup* par = parent;
        int parIdx = parentItem;
        sess->CloseDeeperThan(depth - 1);
        if (par && par->hwnd) {
            SetFocus(par->hwnd);
            par->SetHover(parIdx);
        }
        return;
    }
    }
}

void MenuPopup::EnsureVisible(int idx) {
    if (idx < 0 || idx >= (int)rowY.size() || contentH <= h) return;
    int ns = scroll;
    float top = rowY[idx], bot = top + rowH[idx];
    if (top < (float)ns)            ns = (int)top;
    else if (bot > (float)(ns + h)) ns = (int)std::ceil(bot - h);
    ns = clampi(ns, 0, contentH - h);
    if (ns == scroll) return;
    scroll = ns;
    Repaint();
}

LRESULT CALLBACK MenuPopup::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* self = (MenuPopup*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_MENUDISMISS:
        if (self && self->sess) self->sess->CloseAll();
        return 0;
    case WM_CREATE: {
        auto* cs = (CREATESTRUCTW*)lp;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
        return 0;
    }
    case WM_MOUSEMOVE: {
        if (self) {
            // Window origin sits `shadow` px outside the surface.
            int mx = GET_X_LPARAM(lp) - self->shadow;
            int my = GET_Y_LPARAM(lp) - self->shadow;
            MenuPopup* t = self->RoutePoint(mx, my);
            if (t != self) self->SetHover(-1);   // cursor left the surface
            t->OnMouse(mx, my);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        if (self) self->OnLeave();
        return 0;
    case WM_LBUTTONDOWN: {
        if (self) {
            int mx = GET_X_LPARAM(lp) - self->shadow;
            int my = GET_Y_LPARAM(lp) - self->shadow;
            MenuPopup* t = self->RoutePoint(mx, my);
            // A press in the shadow margin that no surface owns is
            // visually a press OUTSIDE the menu → dismiss, like the
            // classic menus. (The session loop only sees presses that
            // miss every popup window; this one hit ours.)
            if (t == self && (mx < 0 || my < 0 || mx >= self->w || my >= self->h)) {
                self->sess->CloseAll();
                return 0;
            }
            t->pressed = true;
            SetCapture(hwnd);
            t->OnMouse(mx, my);
        }
        return 0;
    }
    case WM_LBUTTONUP: {
        if (self) {
            int mx = GET_X_LPARAM(lp) - self->shadow;
            int my = GET_Y_LPARAM(lp) - self->shadow;
            MenuPopup* t = self->RoutePoint(mx, my);
            t->pressed = false;
            if (GetCapture() == hwnd) ReleaseCapture();
            // A press that started on a row and was released out in the
            // margin = released outside the menu → cancel (no selection).
            if (t == self && (mx < 0 || my < 0 || mx >= self->w || my >= self->h)) {
                self->sess->CloseAll();
                return 0;
            }
            t->OnClick(mx, my);
        }
        return 0;
    }
    case WM_RBUTTONUP: {
        // TPM_RIGHTBUTTON parity: a right-release on a row also picks the
        // item (the legacy call sites both track with TPM_RIGHTBUTTON); a
        // release out in the unowned margin dismisses like a right-click
        // outside any system menu.
        if (self) {
            int mx = GET_X_LPARAM(lp) - self->shadow;
            int my = GET_Y_LPARAM(lp) - self->shadow;
            MenuPopup* t = self->RoutePoint(mx, my);
            if (t == self && (mx < 0 || my < 0 || mx >= self->w || my >= self->h)) {
                self->sess->CloseAll();
                return 0;
            }
            t->OnClick(mx, my);
        }
        return 0;
    }
    case WM_MOUSEWHEEL:
        if (self) self->OnWheel(GET_WHEEL_DELTA_WPARAM(wp));
        return 0;
    case WM_KEYDOWN:
        if (self) self->OnKey((UINT)wp);   // may destroy `self` (Esc on flyout)
        return 0;
    case WM_TIMER:
        if (self && wp == self->openTimer && self->pendingSub >= 0)
            self->OpenSub(self->pendingSub);
        return 0;
    case WM_DESTROY:
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        if (self) { self->hwnd = nullptr; self->openTimer = 0; }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace FluentMenuImpl
