#include "fence/fence_window.h"
#include "settings/settings_panel.h"
#include <cmath>
#include <algorithm>
#include <cstdlib>
#include <vector>
#include "fence/fence_manager.h"
#include "render/list_scrollbar.h"
#include "shell/desktop_host.h"
#include "core/config.h"
#include <windowsx.h>
#include <shellapi.h>
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

// ── Button press ──

LRESULT FenceWindow::OnLButtonDown(UINT msg, WPARAM wp, LPARAM lp) {
    const int mx = GET_X_LPARAM(lp), my = GET_Y_LPARAM(lp);

    // List-mode scrollbar hit test (before icon/chevron/move handling)
    if (PressListScrollbar(mx, my)) return 0;

    // A click anywhere outside the renaming icon's label commits the
    // rename first (Explorer behavior), then the click acts normally.
    if (m_renameIcon >= 0 && HitTestIcon(mx, my) != m_renameIcon)
        ExitIconRename(true);

    if (m_collapsed && m_render->Appearance().collapseStyle == 1 && !m_renaming) {
        PressTile(mx, my);
        return 0;
    }

    const int titleH = (int)m_render->Appearance().titleH;
    if (!m_renaming && my < titleH) {
        PressTitleBar(mx, my);
        return 0;
    }
    // Search-box strip (mapped fences): focus the box on click, consume the event
    if (!m_renaming && IsMapped() && my >= titleH && my < (int)ContentTop()) {
        FocusSearch();
        return 0;
    }

    int hit = HitTestIcon(mx, my);
    if (hit >= 0) BeginIconDrag(hit, mx, my);
    else          BeginMarquee(mx, my, (wp & MK_CONTROL) != 0);
    return 0;
}

// Click on the list scrollbar: page up/down on the track, or start dragging the
// thumb. Returns false when the press is not on the scrollbar.
bool FenceWindow::PressListScrollbar(int mx, int my) {
    if (ListMaxScroll() <= 0) return false;
    ListScrollbar bar = CurrentListScrollbar();
    if (!bar.visible || (float)mx < bar.trackX || (float)my < ContentTop()) return false;

    if ((float)my < bar.thumbTop) {
        m_listScroll -= bar.viewportHeight;
        ClampListScroll();
        Redraw();
    } else if ((float)my > bar.thumbTop + bar.thumbHeight) {
        m_listScroll += bar.viewportHeight;
        ClampListScroll();
        Redraw();
    } else {
        m_scrollbarDrag = true;
        m_scrollDragStartY = (float)my;
        m_scrollDragStartOffset = m_listScroll;
        SetCapture(m_hwnd);
    }
    return true;
}

// A folded icon tile behaves like a desktop icon: after a move it snaps to the
// desktop's own icon grid, glyph on glyph, and never shares a slot with another
// folded tile (the nearest free slot is taken instead).
void FenceWindow::SnapTileToDesktopGrid() {
    Desktop::IconGrid g = Desktop::QueryIconGrid();
    if (!g.ok) return;
    RECT wr;
    if (!GetWindowRect(m_hwnd, &wr)) return;
    const float glyphInset = 6.0f;   // where RenderContext::DrawCollapsedTile puts the glyph
    const int tileW = wr.right - wr.left;
    const int glyphDy = (int)glyphInset;

    // Slot (column n, row m) of a window rect: its glyph centre / top against the anchor glyph.
    auto slotOf = [&](const RECT& r, int& n, int& m) {
        POINT p{ r.left, r.top };
        ScreenToClient(g.list, &p);
        n = (int)std::lround((double)(p.x + (r.right - r.left) / 2 - g.glyphCenterX) / g.cellW);
        m = (int)std::lround((double)(p.y + glyphDy - g.glyphTop) / g.cellH);
    };

    int n, m;
    slotOf(wr, n, m);

    // Slots taken by the other folded tiles.
    std::vector<std::pair<int, int>> taken;
    for (auto& f : FenceManager::All()) {
        if (f->Hwnd() == m_hwnd || !f->IsCollapsed() || f->GetRender().Appearance().collapseStyle != 1) continue;
        RECT r;
        if (!GetWindowRect(f->Hwnd(), &r)) continue;
        int fn, fm;
        slotOf(r, fn, fm);
        taken.push_back({ fn, fm });
    }
    RECT listRc;
    GetClientRect(g.list, &listRc);
    auto fits = [&](int cn, int cm) {   // the whole tile stays inside the icon area
        int left = g.glyphCenterX + cn * g.cellW - tileW / 2;
        int top = g.glyphTop + cm * g.cellH - glyphDy;
        return left >= 0 && top >= 0 && left + tileW <= listRc.right && top + (wr.bottom - wr.top) <= listRc.bottom;
    };
    auto isTaken = [&](int cn, int cm) {
        for (auto& t : taken) if (t.first == cn && t.second == cm) return true;
        return false;
    };
    // Nearest free slot: the wanted one, else rings of growing radius around it.
    int bestN = n, bestM = m;
    bool found = fits(n, m) && !isTaken(n, m);
    for (int radius = 1; !found && radius < 60; radius++) {
        double bestD = 1e18;
        for (int dn = -radius; dn <= radius; dn++)
            for (int dm = -radius; dm <= radius; dm++) {
                if ((std::max)(std::abs(dn), std::abs(dm)) != radius) continue;
                if (!fits(n + dn, m + dm) || isTaken(n + dn, m + dm)) continue;
                double d = (double)dn * dn + (double)dm * dm;
                if (d < bestD) { bestD = d; bestN = n + dn; bestM = m + dm; found = true; }
            }
    }
    if (!found) return;

    POINT snapped{ g.glyphCenterX + bestN * g.cellW - tileW / 2, g.glyphTop + bestM * g.cellH - glyphDy };
    ClientToScreen(g.list, &snapped);
    HWND parent = GetParent(m_hwnd);
    if (parent) ScreenToClient(parent, &snapped);
    SetWindowPos(m_hwnd, nullptr, snapped.x, snapped.y, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    Config::MarkDirty();
}

// Folder mode: a fence opened from its icon tile folds back when a mouse press
// that started outside the window ends outside it. Judged on button RELEASE so
// a file dragged in from the desktop (pressed outside, released inside) still drops.
void FenceWindow::CollapseOnOutsideClick() {
    if (!m_popped || m_collapsed) { KillTimer(m_hwnd, 4); m_popped = false; return; }
    bool down = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) || (GetAsyncKeyState(VK_RBUTTON) & 0x8000);
    bool released = m_pollWasDown && !down;
    m_pollWasDown = down;
    if (!released) return;
    if (m_modalUi || SettingsPanel::IsOpen() || m_dragIcon >= 0 || m_moving || m_resizeMask || m_marquee ||
        m_renaming || m_renameIcon >= 0)
        return;
    POINT c; GetCursorPos(&c);
    RECT wr; GetWindowRect(m_hwnd, &wr);
    if (PtInRect(&wr, c)) return;
    KillTimer(m_hwnd, 4);
    m_popped = false;
    ToggleCollapse();
}

// Press on a collapsed icon tile: a plain click expands the fence, a drag moves it
// (OnLButtonUp tells them apart by how far the cursor travelled).
void FenceWindow::PressTile(int mx, int my) {
    m_moveOffX = mx;
    m_moveOffY = my;
    m_moving = true;
    m_tilePress = true;
    GetCursorPos(&m_tilePressPt);
    SetCapture(m_hwnd);
}

// Press inside the title bar: the collapse arrow at the right end, or a drag of
// the whole fence anywhere else.
void FenceWindow::PressTitleBar(int mx, int my) {
    // Top-right corner of the title bar = collapse/expand arrow.
    int arrowL = (int)(m_render->Width() - m_render->Appearance().titleH);
    if (mx >= arrowL) {
        m_chevronDown = true;
        m_chevronHover = true;
        SetCapture(m_hwnd);
        Redraw();
        return;
    }
    // Rest of the title bar: drag the whole fence. Manual capture
    // drag: DefWindowProc's HTCAPTION loop doesn't move child windows.
    m_moveOffX = mx;
    m_moveOffY = my;
    m_moving = true;
    SetCapture(m_hwnd);
}

// Press on an icon: select it and arm a drag inside the fence.
void FenceWindow::BeginIconDrag(int hit, int mx, int my) {
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
    m_dragGrabX = mx;         // system drag distance, so a plain
    m_dragGrabY = my;         // click must not disturb anything
    m_dragHoverCell = CellOf(m_icons[hit]);
    SetCapture(m_hwnd);
    Redraw();
}

// Press on the empty body: marquee multi-select (desktop style). The press arms
// the band; the click/drag threshold in OnMouseMove decides whether it becomes a
// plain click (which clears the selection) or a live rubber band. Ctrl keeps the
// current selection and adds to it instead of replacing it.
void FenceWindow::BeginMarquee(int mx, int my, bool addToSelection) {
    m_marquee = true;
    m_marqueeMoved = false;
    m_marqueeAdd = addToSelection;
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

// ── Pointer movement ──

LRESULT FenceWindow::OnMouseMove(UINT msg, WPARAM wp, LPARAM lp) {
    const int mx = GET_X_LPARAM(lp), my = GET_Y_LPARAM(lp);
    UpdateCursorShape(mx, my);

    if (m_scrollbarDrag) {
        DragListScrollbar(my);
        return 0;
    }
    TrackIconHover(mx, my);
    TrackChevronHover(mx, my);

    if (m_marquee)    { MoveMarquee(mx, my);           return 0; }
    if (m_moving)     { MoveFenceWithCursor(mx, my);   return 0; }
    if (m_resizeMask) { ResizeFenceWithCursor(mx, my); return 0; }
    if (m_dragIcon >= 0) DragIcons(mx, my);
    return 0;
}

// Belt and suspenders alongside WM_SETCURSOR: mouse moves are routed straight to
// this window, so setting the shape here works even if the desktop list parent
// swallows WM_SETCURSOR.
void FenceWindow::UpdateCursorShape(int mx, int my) {
    int ht = m_resizeMask ? m_resizeHT : EdgeHit(m_hwnd, m_collapsed, mx, my);
    SetCursor(LoadCursorW(nullptr, CursorForHT(ht)));
}

void FenceWindow::DragListScrollbar(int my) {
    if (ListMaxScroll() <= 0) return;
    ListScrollbar bar = CurrentListScrollbar();
    float dy = (float)my - m_scrollDragStartY;
    float offset = ScrollForThumbDrag(bar, m_scrollDragStartOffset, dy);
    if (bar.TrackHeight() - bar.thumbHeight > 0) {
        m_listScroll = offset;
        ClampListScroll();
        Redraw();
    }
}

// Hover plate (Explorer style). No capture is active in this state, so re-arm
// leave tracking on every move and hit-test the cursor.
void FenceWindow::TrackIconHover(int mx, int my) {
    if (m_collapsed || m_moving || m_resizeMask || m_dragIcon >= 0 || m_marquee) return;
    TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, m_hwnd, 0 };
    TrackMouseEvent(&tme);
    int hit = HitTestIcon(mx, my);
    if (hit != m_hoverIcon) {
        m_hoverIcon = hit;
        Redraw();
    }
}

// Chevron hover. Works expanded AND collapsed (the arrow is the only control a
// collapsed fence has), and also while the button is pressed: capture keeps the
// moves coming here, so the plate follows the cursor on and off the button
// (under-capture coordinates may be negative, hence the explicit >= 0 checks).
void FenceWindow::TrackChevronHover(int mx, int my) {
    if (m_renaming || m_moving || m_resizeMask || m_dragIcon >= 0 || m_marquee) return;
    int th = (int)m_render->Appearance().titleH;
    int arrowL = (int)(m_render->Width() - m_render->Appearance().titleH);
    bool inside = my >= 0 && my < th && mx >= arrowL &&
                  !(m_collapsed && m_render->Appearance().collapseStyle == 1);
    if (inside != m_chevronHover) {
        m_chevronHover = inside;
        Redraw();
    }
}

void FenceWindow::MoveMarquee(int mx, int my) {
    if (!m_marqueeMoved) {
        // The same click/drag discrimination icon drags use: a press that never
        // travels the system drag distance stays a plain click (settled in
        // OnLButtonUp).
        int t = GetSystemMetrics(SM_CXDRAG);
        if (abs(mx - m_marqAX) < t && abs(my - m_marqAY) < t) return;
        m_marqueeMoved = true;
    }
    m_marqCX = mx;
    m_marqCY = my;
    UpdateMarqueeSelection();
    Redraw();
}

// Cursor to parent-client coordinates, keeping the grabbed offset.
void FenceWindow::MoveFenceWithCursor(int mx, int my) {
    POINT p{ mx, my };
    ClientToScreen(m_hwnd, &p);
    HWND parent = GetParent(m_hwnd);
    if (parent) ScreenToClient(parent, &p);
    SetWindowPos(m_hwnd, nullptr, p.x - m_moveOffX, p.y - m_moveOffY,
                 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void FenceWindow::ResizeFenceWithCursor(int mx, int my) {
    POINT p{ mx, my };
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
        else                  r.right = r.left + minW;
    }
    if (r.bottom - r.top < minH) {
        if (m_resizeMask & 4) r.top = r.bottom - minH;
        else                  r.bottom = r.top + minH;
    }
    SetWindowPos(m_hwnd, nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top,
                 SWP_NOZORDER | SWP_NOACTIVATE);
}

void FenceWindow::DragIcons(int mx, int my) {
    if (!m_dragMoved) {
        // Explorer's click/drag discrimination: an icon press only becomes a
        // drag once the cursor travels the system drag distance. Until then the
        // icons stay put (click = select).
        int t = GetSystemMetrics(SM_CXDRAG);
        if (abs(mx - m_dragGrabX) < t && abs(my - m_dragGrabY) < t) return;
        m_dragMoved = true;
        // Stale overlaps under the lifted group resolve at drag start:
        // squatters are pushed to the nearest free cells.
        PushGroupOut();
    }
    // The whole selection rides ONE shared delta (Explorer group drag). For a
    // single-icon selection this is exactly the old offset-follow behavior.
    float dx = (float)(mx - m_dragGrabX);
    float dy = (float)(my - m_dragGrabY);
    for (const auto& d : m_dragGroup) {
        if (d.idx < 0 || d.idx >= (int)m_icons.size()) continue;
        float nx = d.x + dx, ny = d.y + dy;
        ClampIconPos(nx, ny);
        m_icons[d.idx].x = floorf(nx + 0.5f);   // whole pixels
        m_icons[d.idx].y = floorf(ny + 0.5f);
    }
    // Push-aside: whenever the pressed icon enters a new cell, sweep every cell
    // the group now covers; bystanders flow out of the way mid-drag, along the
    // row in the drag's direction.
    int hovered = CellOf(m_icons[m_dragIcon]);
    if (hovered != m_dragHoverCell) {
        IconGrid g = GetIconGrid();
        int dc = (hovered % g.cols) - (m_dragHoverCell % g.cols);
        if (dc != 0) m_dragDirX = dc > 0 ? 1 : -1;
        m_dragHoverCell = hovered;
        PushGroupOut();
    }
    Redraw();
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
    if (m_tilePress) {
        m_tilePress = false;
        POINT p; GetCursorPos(&p);
        bool click = abs(p.x - m_tilePressPt.x) < GetSystemMetrics(SM_CXDRAG) &&
                     abs(p.y - m_tilePressPt.y) < GetSystemMetrics(SM_CYDRAG);
        if (click) {
            m_moving = false;
            ReleaseCapture();
            ToggleCollapse();
            m_popped = !m_collapsed;   // opened from the tile: folds back on an outside click
            if (m_popped) { m_pollWasDown = false; SetTimer(m_hwnd, 4, 30, nullptr); }
            return 0;
        }
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
    if (m_moving && m_collapsed && m_render->Appearance().collapseStyle == 1)
        SnapTileToDesktopGrid();
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
        m_tilePress = false;
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
