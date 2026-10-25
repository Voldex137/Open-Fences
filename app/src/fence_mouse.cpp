#include "fence_window.h"
#include "fence_metrics.h"
#include "desktop_host.h"
#include "localization.h"
#include "settings_panel.h"
#include "context_menu.h"
#include "menu_icons.h"
#include "config.h"
#include "explorer_sort.h"
#include "shell_context_menu.h"
#include <windowsx.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shlwapi.h>
#include <commctrl.h>
#include <algorithm>
#include <cmath>

// Edge hit-test shared by WM_NCHITTEST (resize behavior) and cursor picking.
// Returns HTLEFT/HTRIGHT/… near a border, HTCLIENT anywhere in the body.
// `collapsed` fences are a bare title pill — nothing to resize there.
static int EdgeHit(HWND hwnd, bool collapsed, int mx, int my) {
    if (collapsed) return HTCLIENT;
    RECT rc; GetClientRect(hwnd, &rc);
    const int E = (int)(8.0f * Desktop::DpiScale() + 0.5f);
    bool L = mx < E, R = mx > rc.right - E;
    bool T = my < E, B = my > rc.bottom - E;
    if (T && L) return HTTOPLEFT;
    if (T && R) return HTTOPRIGHT;
    if (B && L) return HTBOTTOMLEFT;
    if (B && R) return HTBOTTOMRIGHT;
    if (L) return HTLEFT;
    if (R) return HTRIGHT;
    if (T) return HTTOP;
    if (B) return HTBOTTOM;
    return HTCLIENT;
}

// Pointer shape matching a WM_NCHITTEST code.
static LPCWSTR CursorForHT(int ht) {
    switch (ht) {
    case HTTOPLEFT: case HTBOTTOMRIGHT: return IDC_SIZENWSE;
    case HTTOPRIGHT: case HTBOTTOMLEFT: return IDC_SIZENESW;
    case HTLEFT:     case HTRIGHT:      return IDC_SIZEWE;
    case HTTOP:      case HTBOTTOM:     return IDC_SIZENS;
    default:                            return IDC_ARROW;
    }
}

LRESULT FenceWindow::OnNcHitTest(UINT msg, WPARAM wp, LPARAM lp)     {
    POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
    ScreenToClient(m_hwnd, &pt);
    return EdgeHit(m_hwnd, m_collapsed, pt.x, pt.y);
}

LRESULT FenceWindow::OnSetCursor(UINT msg, WPARAM wp, LPARAM lp)     {
    // A fence is a layered child of *another process's* desktop list, so
    // the usual cursor plumbing (class cursor applied through the parent
    // chain) never reaches it — without this the pointer keeps whatever
    // shape it had before entering. Recompute the hit from the live
    // cursor position: during a resize drag the pointer can leave the
    // edge zone, and we still want the sizing shape in that case.
    if (this) {
        int ht = m_resizeMask
            ? m_resizeHT
            : [&] { POINT p; GetCursorPos(&p); ScreenToClient(m_hwnd, &p);
                    return EdgeHit(m_hwnd, m_collapsed, p.x, p.y); }();
        SetCursor(LoadCursorW(nullptr, CursorForHT(ht)));
        return TRUE;
    }
    return DefWindowProcW(m_hwnd, msg, wp, lp);
}

LRESULT FenceWindow::OnNcLButtonDown(UINT msg, WPARAM wp, LPARAM lp)     {
    // Edge resize. DefWindowProc's size loop only works for top-level
    // windows, so for our desktop-child fences we run it manually:
    // remember the starting rect/cursor and follow WM_MOUSEMOVE.
    int code = (int)wp;
    if (code >= HTLEFT && code <= HTBOTTOMRIGHT) {
        int mask = 0;
        if (code == HTLEFT || code == HTTOPLEFT || code == HTBOTTOMLEFT)      mask |= 1;
        if (code == HTRIGHT || code == HTTOPRIGHT || code == HTBOTTOMRIGHT)   mask |= 2;
        if (code == HTTOP || code == HTTOPLEFT || code == HTTOPRIGHT)         mask |= 4;
        if (code == HTBOTTOM || code == HTBOTTOMLEFT || code == HTBOTTOMRIGHT) mask |= 8;
        POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };   // screen coords
        RECT wr; GetWindowRect(m_hwnd, &wr);
        HWND parent = GetParent(m_hwnd);
        if (parent) {                   // screen → parent-client
            ScreenToClient(parent, &pt);
            POINT a{ wr.left, wr.top }, b{ wr.right, wr.bottom };
            ScreenToClient(parent, &a);
            ScreenToClient(parent, &b);
            wr = { a.x, a.y, b.x, b.y };
        }
        m_resizeMask = mask;
        m_resizeHT = code;      // keep this sizing shape for the drag
        m_resizeStart = wr;
        m_resizePtX = pt.x;
        m_resizePtY = pt.y;
        SetCapture(m_hwnd);
        SetCursor(LoadCursorW(nullptr, CursorForHT(code)));
        return 0;
    }
    return DefWindowProcW(m_hwnd, msg, wp, lp);
}

LRESULT FenceWindow::OnLButtonDown(UINT msg, WPARAM wp, LPARAM lp)     {
    int mx = GET_X_LPARAM(lp), my = GET_Y_LPARAM(lp);

    // List-mode scrollbar hit test (before icon/chevron/move handling)
    float maxS = ListMaxScroll();
    if (maxS > 0) {
        const auto& app = m_render->Appearance();
        float bt = ContentTop();
        float w = (float)m_render->Width();
        float h = (float)m_render->Height();
        float rowH = app.iconSize * 2.6f;
        float sbW = 6.0f, sbPad = 2.0f;
        float trackX = w - sbW - sbPad;
        if ((float)mx >= trackX && (float)my >= bt) {
            float trackH = h - bt - sbPad * 2;
            float totalH = (float)VisibleCount() * rowH;
            float contentH = h - bt;
            float thumbH = (std::max)(20.0f, trackH * contentH / totalH);
            float thumbY = bt + sbPad + (m_listScroll / maxS) * (trackH - thumbH);
            if ((float)my < thumbY) {
                m_listScroll -= contentH;
                ClampListScroll();
                Redraw();
                return 0;
            } else if ((float)my > thumbY + thumbH) {
                m_listScroll += contentH;
                ClampListScroll();
                Redraw();
                return 0;
            } else {
                m_scrollbarDrag = true;
                m_scrollDragStartY = (float)my;
                m_scrollDragStartOffset = m_listScroll;
                SetCapture(m_hwnd);
                return 0;
            }
        }
    }

    // A click anywhere outside the renaming icon's label commits the
    // rename first (Explorer behavior), then the click acts normally.
    if (m_renameIcon >= 0 && HitTestIcon(mx, my) != m_renameIcon)
        ExitIconRename(true);

    if (!m_renaming && my < (int)m_render->Appearance().titleH) {
        // Top-right corner of the title bar = collapse/expand arrow.
        int arrowL = (int)(m_render->Width() - m_render->Appearance().titleH);
        if (mx >= arrowL) {
            m_chevronDown = true;
            m_chevronHover = true;
            SetCapture(m_hwnd);
            Redraw();
            return 0;
        }
        // Rest of the title bar → drag the whole fence. Manual capture
        // drag: DefWindowProc's HTCAPTION loop doesn't move child windows.
        m_moveOffX = mx;
        m_moveOffY = my;
        m_moving = true;
        SetCapture(m_hwnd);
        return 0;
    }
    // Search-box strip (mapped fences): focus the box on click, consume the event
    if (!m_renaming && IsMapped() &&
        my >= (int)m_render->Appearance().titleH &&
        my < (int)ContentTop()) {
        FocusSearch();
        return 0;
    }
    // Icon → select it and arm a drag inside the fence
    int hit = HitTestIcon(mx, my);
    if (hit >= 0) {
        std::wstring pressedPath = m_icons[hit].path;
        // Explorer selection rule: pressing an already-selected icon
        // keeps the multi-selection (the whole group will drag
        // together); pressing any other icon collapses it to a single.
        if (!IsPathSelected(pressedPath)) SelectOnly(hit);
        // Bring the WHOLE selection to the front so it draws above the
        // rest while dragging. That reorders the vector, so re-locate
        // every index by path afterwards.
        BringSelectedToFront();
        m_dragGroup.clear();
        m_dragExcludes.clear();
        for (int i = 0; i < (int)m_icons.size(); i++) {
            if (!IsPathSelected(m_icons[i].path)) continue;
            m_dragGroup.push_back({ i, m_icons[i].x, m_icons[i].y });
            m_dragExcludes.push_back(i);
            if (m_icons[i].path == pressedPath) hit = i;
        }
        m_hoverIcon = hit;
        m_dragIcon = hit;         // the pressed icon leads the group
        m_dragDirX = 0;
        m_dragMoved = false;      // real drag starts past the
        m_dragGrabX = mx;         // system drag distance — a plain
        m_dragGrabY = my;         // click must not disturb anything
        m_dragHoverCell = CellOf(m_icons[hit]);
        SetCapture(m_hwnd);
        Redraw();
    } else {
        // Empty body → marquee multi-select (desktop style). The press
        // arms the band; the click/drag threshold in WM_MOUSEMOVE
        // decides whether it becomes a plain click (which clears the
        // selection) or a live rubber band. Ctrl keeps the current
        // selection and adds to it instead of replacing it.
        m_marquee = true;
        m_marqueeMoved = false;
        m_marqueeAdd = (wp & MK_CONTROL) != 0;
        if (m_marqueeAdd) {
            m_marqueeBase = m_selectedPaths;
        } else {
            m_marqueeBase.clear();
            ClearSelection();
        }
        m_marqAX = m_marqCX = mx;
        m_marqAY = m_marqCY = my;
        SetCapture(m_hwnd);
        Redraw();
    }
    return 0;
}

LRESULT FenceWindow::OnMouseMove(UINT msg, WPARAM wp, LPARAM lp)     {
    // Belt and suspenders alongside WM_SETCURSOR: mouse moves are routed
    // straight to this window, so setting the shape here works even if the
    // desktop list parent swallows WM_SETCURSOR. lParam is client coords.
    if (this) {
        int ht = m_resizeMask
            ? m_resizeHT
            : EdgeHit(m_hwnd, m_collapsed, GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        SetCursor(LoadCursorW(nullptr, CursorForHT(ht)));
    }
    // List-mode scrollbar thumb drag
    if (m_scrollbarDrag) {
        float dy = (float)GET_Y_LPARAM(lp) - m_scrollDragStartY;
        float maxS = ListMaxScroll();
        if (maxS > 0) {
            const auto& app = m_render->Appearance();
            float rowH = app.iconSize * 2.6f;
            float bt = ContentTop();
            float h = (float)m_render->Height();
            float sbPad = 2.0f;
            float trackH = h - bt - sbPad * 2;
            float totalH = (float)VisibleCount() * rowH;
            float contentH = h - bt;
            float thumbH = (std::max)(20.0f, trackH * contentH / totalH);
            float range = trackH - thumbH;
            if (range > 0) {
                m_listScroll = m_scrollDragStartOffset + (dy / range) * maxS;
                ClampListScroll();
                Redraw();
            }
        }
        return 0;
    }

    // Hover plate (Explorer style). No capture is active in this branch,
    // so re-arm leave tracking on every move and hit-test the cursor.
    if (!m_collapsed && !m_moving &&
        !m_resizeMask && m_dragIcon < 0 && !m_marquee) {
        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, m_hwnd, 0 };
        TrackMouseEvent(&tme);
        int h = HitTestIcon(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        if (h != m_hoverIcon) {
            m_hoverIcon = h;
            Redraw();
        }
    }
    // Chevron hover. Works expanded AND collapsed (the arrow is the only
    // control a collapsed fence has), and also while the button is
    // pressed — capture keeps the moves coming here, so the plate follows
    // the cursor on and off the button (under-capture coordinates may be
    // negative, hence the explicit >= 0 checks).
    if (!m_renaming && !m_moving &&
        !m_resizeMask && m_dragIcon < 0 && !m_marquee) {
        int mx = GET_X_LPARAM(lp), my = GET_Y_LPARAM(lp);
        int th = (int)m_render->Appearance().titleH;
        int arrowL = (int)(m_render->Width() - m_render->Appearance().titleH);
        bool in = my >= 0 && my < th && mx >= arrowL;
        if (in != m_chevronHover) {
            m_chevronHover = in;
            Redraw();
        }
    }
    if (m_marquee) {
        int mx = GET_X_LPARAM(lp), my = GET_Y_LPARAM(lp);
        if (!m_marqueeMoved) {
            // The same click/drag discrimination icon drags use: a press
            // that never travels the system drag distance stays a plain
            // click (settled in WM_LBUTTONUP).
            int t = GetSystemMetrics(SM_CXDRAG);
            if (abs(mx - m_marqAX) < t && abs(my - m_marqAY) < t)
                return 0;
            m_marqueeMoved = true;
        }
        m_marqCX = mx;
        m_marqCY = my;
        UpdateMarqueeSelection();
        Redraw();
        return 0;
    }
    if (m_moving) {
        // Cursor to parent-client coords, keep the grabbed offset
        POINT p{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        ClientToScreen(m_hwnd, &p);
        HWND parent = GetParent(m_hwnd);
        if (parent) ScreenToClient(parent, &p);
        SetWindowPos(m_hwnd, nullptr, p.x - m_moveOffX, p.y - m_moveOffY,
            0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    if (m_resizeMask) {
        POINT p{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        ClientToScreen(m_hwnd, &p);
        HWND parent = GetParent(m_hwnd);
        if (parent) ScreenToClient(parent, &p);
        int dx = p.x - m_resizePtX;
        int dy = p.y - m_resizePtY;
        RECT r = m_resizeStart;
        if (m_resizeMask & 1) r.left   += dx;
        if (m_resizeMask & 2) r.right  += dx;
        if (m_resizeMask & 4) r.top    += dy;
        if (m_resizeMask & 8) r.bottom += dy;
        // Enforce a sane minimum so the fence can't vanish
        int th  = (int)(m_render->Appearance().titleH + 0.5f);
        int minW = (int)(120.0f * Desktop::DpiScale() + 0.5f);
        int minH = th + (int)(40.0f * Desktop::DpiScale() + 0.5f);
        if (r.right - r.left < minW) {
            if (m_resizeMask & 1) r.left = r.right - minW;
            else                        r.right = r.left + minW;
        }
        if (r.bottom - r.top < minH) {
            if (m_resizeMask & 4) r.top = r.bottom - minH;
            else                        r.bottom = r.top + minH;
        }
        SetWindowPos(m_hwnd, nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top,
            SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    if (m_dragIcon >= 0) {
        int mx = GET_X_LPARAM(lp), my = GET_Y_LPARAM(lp);
        if (!m_dragMoved) {
            // Explorer's click/drag discrimination: an icon press only
            // becomes a drag once the cursor travels the system drag
            // distance. Until then the icons stay put (click = select).
            int t = GetSystemMetrics(SM_CXDRAG);
            if (abs(mx - m_dragGrabX) < t &&
                abs(my - m_dragGrabY) < t)
                return 0;
            m_dragMoved = true;
            // Stale overlaps under the lifted group resolve at drag
            // start — squatters are pushed to the nearest free cells.
            PushGroupOut();
        }
        // The whole selection rides ONE shared delta (Explorer group
        // drag). For a single-icon selection this is exactly the old
        // offset-follow behavior.
        float dx = (float)(mx - m_dragGrabX);
        float dy = (float)(my - m_dragGrabY);
        for (const auto& d : m_dragGroup) {
            if (d.idx < 0 || d.idx >= (int)m_icons.size()) continue;
            float nx = d.x + dx, ny = d.y + dy;
            ClampIconPos(nx, ny);
            m_icons[d.idx].x = floorf(nx + 0.5f);   // whole pixels
            m_icons[d.idx].y = floorf(ny + 0.5f);
        }
        // Push-aside: whenever the pressed icon enters a new cell, sweep
        // every cell the group now covers — bystanders flow out of the
        // way mid-drag, along the row in the drag's direction.
        int hovered = CellOf(m_icons[m_dragIcon]);
        if (hovered != m_dragHoverCell) {
            FenceWindow::IconGrid g = GetIconGrid();
            int dc = (hovered % g.cols) - (m_dragHoverCell % g.cols);
            if (dc != 0) m_dragDirX = dc > 0 ? 1 : -1;
            m_dragHoverCell = hovered;
            PushGroupOut();
        }
        Redraw();
    }
    return 0;
}

LRESULT FenceWindow::OnLButtonUp(UINT msg, WPARAM wp, LPARAM lp)     {
    if (m_scrollbarDrag) {
        m_scrollbarDrag = false;
        ReleaseCapture();
        return 0;
    }
    if (m_chevronDown) {
        m_chevronDown = false;
        ReleaseCapture();
        POINT p; GetCursorPos(&p); ScreenToClient(m_hwnd, &p);
        int th = (int)m_render->Appearance().titleH;
        int arrowL = (int)(m_render->Width() - m_render->Appearance().titleH);
        bool inside = p.y >= 0 && p.y < th && p.x >= arrowL;
        m_chevronHover = inside;
        if (inside) ToggleCollapse();
        else        Redraw();
        return 0;
    }
    bool wasBusy = m_dragIcon >= 0 || m_moving ||
                   m_resizeMask || m_marquee;
    if (m_marquee) {
        // A press that never passed the drag threshold was a plain click
        // on empty ground — the selection was already cleared at press
        // time (additive mode left it alone). A real sweep keeps the
        // band's final selection. Either way: drop the band's state.
        m_marquee = false;
        m_marqueeMoved = false;
        m_marqueeAdd = false;
        m_marqueeBase.clear();
        Redraw();   // erase the rubber band
    }
    if (m_dragIcon >= 0) {
        // Mouse capture routed every move to the drag path, so the hover
        // plate is stale — re-derive it from the live cursor before the
        // settle redraw paints the final frame.
        POINT p; GetCursorPos(&p); ScreenToClient(m_hwnd, &p);
        m_hoverIcon = HitTestIcon(p.x, p.y);
        SettleDragGroup();
    }
    bool geomChanged = m_moving || m_resizeMask;
    m_moving = false;
    m_resizeMask = 0;
    if (wasBusy) ReleaseCapture();
    if (geomChanged) Config::MarkDirty();   // fence moved / resized
    return 0;
}

LRESULT FenceWindow::OnCaptureChanged(UINT msg, WPARAM wp, LPARAM lp)     {
    // Capture stolen mid-gesture: settle the group instead of leaving it
    // overlapping somebody, and drop any half-drawn rubber band. (Our own
    // ReleaseCapture in WM_LBUTTONUP gets here with the drag/marquee
    // state already cleared — no double settle.)
    if (this) {
        if (m_chevronDown) {   // press disarmed without a release
            m_chevronDown = false;
            Redraw();
        }
        if (m_dragIcon >= 0) SettleDragGroup();
        if (m_marquee) {
            m_marquee = false;
            m_marqueeMoved = false;
            m_marqueeAdd = false;
            m_marqueeBase.clear();
            Redraw();
        }
        if (m_moving || m_resizeMask) Config::MarkDirty();
        m_moving = false;
        m_resizeMask = 0;
    }
    return 0;
}

LRESULT FenceWindow::OnMouseLeave(UINT msg, WPARAM wp, LPARAM lp)     {
    // Cursor left the fence: drop the hover states. (While a capture is
    // active — drag/move/resize/chevron press — leave notifications are
    // suppressed.)
    if (m_dragIcon < 0 &&
        !m_moving && !m_resizeMask && !m_marquee) {
        bool dirty = false;
        if (m_hoverIcon >= 0) { m_hoverIcon = -1;       dirty = true; }
        if (m_chevronHover)   { m_chevronHover = false; dirty = true; }
        if (dirty) Redraw();
    }
    return 0;
}

LRESULT FenceWindow::OnLButtonDblClk(UINT msg, WPARAM wp, LPARAM lp)     {
    int mx = GET_X_LPARAM(lp);
    int my = GET_Y_LPARAM(lp);
    if (my < (int)m_render->Appearance().titleH) {
        // Ignore the collapse-arrow box so it doesn't trigger a rename
        int arrowL = (int)(m_render->Width() - m_render->Appearance().titleH);
        if (mx >= arrowL) return 0;
        EnterRename();
        return 0;
    }
    // Search-box strip: no action on double-click
    if (IsMapped() && my < (int)ContentTop()) return 0;
    // Double-click on an icon → open it
    int hit = HitTestIcon(mx, my);
    if (hit >= 0) {
        ShellExecuteW(m_hwnd, L"open", m_icons[hit].path.c_str(),
                      nullptr, nullptr, SW_SHOW);
    }
    return 0;
}

LRESULT FenceWindow::OnMouseWheel(UINT msg, WPARAM wp, LPARAM lp)     {
    float maxS = ListMaxScroll();
    if (maxS <= 0) return 0;
    float rowH = m_render->Appearance().iconSize * 2.6f;
    int steps = GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA;
    m_listScroll -= steps * rowH * 3;
    ClampListScroll();
    Redraw();
    return 0;
}
