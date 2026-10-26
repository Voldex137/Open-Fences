#include "fence/fence_window.h"
#include "shell/desktop_host.h"
#include "fence/fence_drop_target.h"
#include "menu/shell_context_menu.h"
#include "settings/settings_panel.h"
#include "core/config.h"
#include "core/appearance_json.h"
#include "core/storage.h"
#include <shellapi.h>
#include <shobjidl.h>   // IContextMenu
#include <shlwapi.h>
#include <cmath>

HWND FenceWindow::s_owner = nullptr;


HWND FenceWindow::DesktopHost() { return Desktop::FindIconListHost(); }

FenceWindow::FenceWindow(const FenceData& data)
    : m_title(data.title), m_id(data.id), m_x(data.x), m_y(data.y)
{
    m_render = std::make_unique<RenderContext>(data.w, data.h);

    // Fences live *inside* the desktop window tree (see DesktopHost). That
    // is what makes Win+D leave them alone: the shell's "show desktop" sweep
    // only minimizes foreign top-level windows, never the desktop's own
    // children. If the desktop is not up yet (no explorer), fall back to a
    // plain popup; the owner window re-creates us when explorer (re)starts.
    HWND host = DesktopHost();
    POINT pos{ data.x, data.y };
    if (host) ScreenToClient(host, &pos);   // child coords are parent-relative

    {
        Desktop::HostDpiScope dpiScope(host);
        m_hwnd = CreateWindowExW(
            WS_EX_LAYERED | WS_EX_TOOLWINDOW,
            ClassName(), data.title.c_str(), host ? WS_CHILD : WS_POPUP,
            pos.x, pos.y, data.w, data.h,
            host, nullptr, GetModuleHandle(nullptr), this);
    }

    if (!m_hwnd) return;   // no usable desktop / unexpected failure
    SetWindowLongPtr(m_hwnd, GWLP_USERDATA, (LONG_PTR)this);
    if (host)
        SetWindowPos(m_hwnd, HWND_TOP, 0, 0, 0, 0,   // above the desktop icons
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    IDropTarget* dt = CreateFenceDropTarget(m_hwnd);
    if (FAILED(RegisterDragDrop(m_hwnd, dt)))
        DragAcceptFiles(m_hwnd, TRUE);   // fallback: filesystem paths only
    dt->Release();
    ShowWindow(m_hwnd, SW_SHOW);
    Redraw();
    CaptureBackdrop();   // initial frosted-glass snapshot
}

FenceWindow::~FenceWindow() {
    // If the settings panel is open for this fence, tear it down first so it
    // never keeps referencing a fence that is going away.
    SettingsPanel::CloseActiveFor(this);
    StopSourceWatch();
    // When explorer restarts, the desktop tree takes our child window down
    // with it — by the time we run, the HWND is already dead.
    if (!m_hwnd || !IsWindow(m_hwnd)) return;
    if (m_cursorTimer) KillTimer(m_hwnd, 1);
    RevokeDragDrop(m_hwnd);       // no-op if RegisterDragDrop failed
    DragAcceptFiles(m_hwnd, FALSE); // no-op if DragAcceptFiles was never called
    DestroyWindow(m_hwnd);
}

void FenceWindow::Show() { m_appHidden = false; ShowWindow(m_hwnd, SW_SHOW); Redraw(); }
void FenceWindow::Hide() { m_appHidden = true;  ShowWindow(m_hwnd, SW_HIDE); }

void FenceWindow::Redraw() {
    ClampListScroll();
    std::wstring displayTitle = m_renaming
        ? (m_renameBuf + (m_cursorVisible ? L"|" : L""))
        : m_title;
    // The marquee band only exists once the gesture passes the click
    // threshold — a plain click must not flash it.
    RECT marq = {};
    bool showMarq = m_marquee && m_marqueeMoved;
    if (showMarq) marq = MarqueeRect();
    m_render->BeginDraw();
    // Explorer-style states: hover plate, selection plates, the translucent
    // "lifted" look for the whole dragged selection (only once the drag has
    // actually moved), and the rubber band while one is live.
    FenceViewState view;
    view.filter     = SearchActive() ? &m_filterIdx : nullptr;
    view.searchText = IsMapped() ? &m_searchText : nullptr;
    view.caretOn    = m_searchFocused && m_cursorVisible;
    view.loading    = m_loading;
    std::wstring iconRenameDisplay;
    if (m_renameIcon >= 0) {
        iconRenameDisplay = m_iconRenameBuf + m_iconRenameExt;
        view.renameIcon = m_renameIcon;
        view.iconRenameText = &iconRenameDisplay;
        view.renameCaret = m_cursorVisible ? (int)m_iconRenameCaret : -1;
    }
    m_render->DrawFence(displayTitle, m_icons, m_renaming, m_dragOver, m_collapsed,
                        m_collapsed ? -1 : m_hoverIcon,
                        &m_selectedPaths,
                        m_dragMoved && m_dragIcon >= 0,
                        showMarq ? &marq : nullptr,
                        m_chevronHover, m_chevronDown,
                        m_listScroll, &view);
    m_render->EndDraw();
    m_render->Present(m_hwnd, m_x, m_y);
}

void FenceWindow::SetIcons(const std::vector<IconEntry>& entries) {
    m_icons = entries;
    Redraw();
}

void FenceWindow::AddDroppedItem(const std::wstring& name, const std::wstring& path,
                                 float atX, float atY) {
    std::wstring stored = IsMapped() ? std::wstring() : Storage::AdoptFromDesktop(path);
    AddIcon(name, stored.empty() ? path : stored, atX, atY);
}

void FenceWindow::ReturnStoredItemsToDesktop() {
    for (auto& icon : m_icons) {
        if (!Storage::IsStored(icon.path)) continue;
        std::wstring back = Storage::ReturnToDesktop(icon.path);
        if (!back.empty()) icon.path = back;
    }
}

void FenceWindow::AddIcon(const std::wstring& name, const std::wstring& path,
                          float atX, float atY) {
    // Snap onto the icon grid: the drop point picks the nearest cell, and a
    // taken cell defers to the nearest free one. Dropping a whole file
    // selection therefore fills cells around the drop point instead of
    // stacking every icon on top of the others.
    IconGrid g = GetIconGrid();
    int c = (int)floorf((atX - g.x0) / g.cw);
    int r = (int)floorf((atY - g.y0) / g.ch);
    c = (std::min)((std::max)(c, 0), g.cols - 1);
    r = (std::min)((std::max)(r, 0), g.rows - 1);
    int cell = r * g.cols + c;

    // The same file already lives in this fence: move its icon to the drop
    // cell (pushing the occupant aside) instead of adding a duplicate.
    // Paths are case-insensitive on Windows.
    for (int i = 0; i < (int)m_icons.size(); i++) {
        if (lstrcmpiW(m_icons[i].path.c_str(), path.c_str()) == 0) {
            PushOccupantOf(cell, { i });
            PlaceIconAt(m_icons[i], cell);
            Redraw();
            Config::MarkDirty();
            return;
        }
    }

    IconEntry e;
    e.name = name;
    e.path = path;
    PlaceIconAt(e, FindFreeCell(cell, OccupiedCells({})));
    m_icons.push_back(std::move(e));
    Redraw();
    Config::MarkDirty();
}

int FenceWindow::HitTestIcon(int mx, int my) const {
    IconGrid g = GetIconGrid();
    float adjY = (float)my;
    if (m_render->Appearance().displayMode == 1 && my >= (int)ContentTop())
        adjY += m_listScroll;
    // Topmost icon wins → iterate in reverse draw order
    for (int i = (int)m_icons.size() - 1; i >= 0; i--) {
        if (SearchActive() && !m_filterBits[i]) continue;
        if ((float)mx >= m_icons[i].x && (float)mx < m_icons[i].x + g.cw &&
            adjY >= m_icons[i].y && adjY < m_icons[i].y + g.ch)
            return i;
    }
    return -1;
}

void FenceWindow::SelectOnly(int idx) {
    m_selectedPaths.clear();
    if (idx >= 0 && idx < (int)m_icons.size())
        m_selectedPaths.push_back(m_icons[idx].path);
}

// ── Icon grid ──

// ── Marquee (rubber-band) multi-select ──

void FenceWindow::SetDragOver(bool over) {
    if (m_dragOver != over) {
        m_dragOver = over;
        Redraw();
    }
}

void FenceWindow::CollapsedSize(int& w, int& h) const {
    const FenceAppearance& app = m_render->Appearance();
    if (app.collapseStyle == 1) {   // icon tile: as big as a desktop icon cell
        w = (int)(app.cellW + 0.5f);
        h = (int)(app.cellH + 0.5f);
    } else {                        // title bar: keeps the expanded width
        w = m_expandedW > 0 ? m_expandedW : (int)m_render->Width();
        h = (int)(app.titleH + 0.5f);
    }
}

void FenceWindow::ToggleCollapse() {
    int th = (int)(m_render->Appearance().titleH + 0.5f);
    if (m_collapsed) {
        // Expand back to the remembered full size
        m_collapsed = false;
        int w = m_expandedW > 0 ? m_expandedW : (int)m_render->Width();
        int h = m_expandedH > th ? m_expandedH : th * 4;
        SetWindowPos(m_hwnd, nullptr, 0, 0, w, h,
            SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        // Icon metrics may have changed while folded (desktop icon zoom);
        // the grid is available again now, so apply the deferred relayout.
        if (m_relayoutPending) {
            m_relayoutPending = false;
            RelayoutIcons();
        }
    } else {
        // Collapse down to just the title bar
        m_collapsed = true;
        m_popped = false;
        m_expandedH = (int)m_render->Height();
        m_expandedW = (int)m_render->Width();
        int w, h;
        CollapsedSize(w, h);
        SetWindowPos(m_hwnd, nullptr, 0, 0, w, h,
            SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    // SetWindowPos raises WM_SIZE → Resize + Redraw, and Redraw already knows
    // the new m_collapsed state (arrow direction, icons skipped).
    Config::MarkDirty();
}

void FenceWindow::SyncCollapsedSize() {
    if (!m_collapsed) return;
    int w, h;
    CollapsedSize(w, h);
    SetWindowPos(m_hwnd, nullptr, 0, 0, w, h,
        SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void FenceWindow::ApplyStyle(const FenceAppearance& src) {
    FenceAppearance& app = m_render->Appearance();
    int oldMode = app.displayMode;
    AppearanceJson::CopyStyle(app, src);
    m_render->RebuildStyles();
    SyncCollapsedSize();
    if (!m_collapsed) {
        if (oldMode != app.displayMode) {
            if (app.displayMode == 1) SaveGridLayout();
            else                      RestoreGridLayout();
        }
        if (app.displayMode == 0 && app.autoReflow) ReflowIcons();
        else                                         RelayoutIcons();
    }
    Redraw();
    Config::MarkDirty();
}

void FenceWindow::ScheduleBackdropRefresh() {
    if (!m_hwnd || !IsWindow(m_hwnd)) return;
    // Same debounce the move/resize path uses (WM_WINDOWPOSCHANGED); timer-2
    // fires CaptureBackdrop once things go quiet for 150 ms.
    SetTimer(m_hwnd, 2, 150, nullptr);
}

void FenceWindow::RestoreAnchor() {
    if (!m_hwnd || !IsWindow(m_hwnd)) return;
    if (m_moving || m_resizeMask || m_dragIcon >= 0 || m_marquee) return;
    RECT wr;
    if (!GetWindowRect(m_hwnd, &wr)) return;

    int w = (int)m_render->Width(), h = (int)m_render->Height();

    // An anchor that touches no monitor at all (its monitor was unplugged,
    // or the config was saved under a different topology) would leave the
    // fence unreachable — park it on the nearest monitor's work area. A
    // fence the user dragged only partially off-screen still intersects its
    // monitor, so deliberate placements are left alone.
    RECT anchor{ m_x, m_y, m_x + w, m_y + h };
    POINT c{ m_x + w / 2, m_y + h / 2 };
    HMONITOR mon = MonitorFromPoint(c, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(mi) };
    RECT hit = {};
    if (mon && GetMonitorInfoW(mon, &mi) &&
        !IntersectRect(&hit, &anchor, &mi.rcMonitor)) {
        RECT wa = mi.rcWork;
        int nx = (std::min)((std::max)(m_x, (int)wa.left),
                            (std::max)((int)wa.left, (int)wa.right - w));
        int ny = (std::min)((std::max)(m_y, (int)wa.top),
                            (std::max)((int)wa.top, (int)wa.bottom - h));
        if (nx != m_x || ny != m_y) {
            m_x = nx; m_y = ny;
            Config::MarkDirty();   // the clamped spot becomes the new saved position
        }
    }
    if (wr.left == m_x && wr.top == m_y) return;   // in place, nothing to do

    HWND parent = GetParent(m_hwnd);
    POINT p{ m_x, m_y };
    if (parent) ScreenToClient(parent, &p);
    SetWindowPos(m_hwnd, nullptr, p.x, p.y, 0, 0,
        SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    ScheduleBackdropRefresh();   // the pixels behind the fence changed
}

void FenceWindow::CaptureBackdrop() {
    int w = (int)m_render->Width(), h = (int)m_render->Height();
    if (w <= 0 || h <= 0) return;

    // Snapshot the desktop area behind the fence. Windows excludes
    // WS_EX_LAYERED windows (including this one) from GDI screen captures,
    // so the fence does not end up inside its own backdrop and we do not
    // need to hide the window (which would break an in-progress drag).
    HDC screenDC = GetDC(nullptr);
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;   // top-down rows
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP dib = CreateDIBSection(screenDC, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (dib) {
        HDC memDC = CreateCompatibleDC(screenDC);
        HGDIOBJ oldBmp = SelectObject(memDC, dib);
        BitBlt(memDC, 0, 0, w, h, screenDC, m_x, m_y, SRCCOPY);
        if (bits) m_render->SetBackdrop((const BYTE*)bits, w, h);
        SelectObject(memDC, oldBmp);
        DeleteDC(memDC);
        DeleteObject(dib);
    }
    ReleaseDC(nullptr, screenDC);
    Redraw();
}

FenceData FenceWindow::GetData() const {
    // A collapsed fence reports the size it expands back to, not the folded
    // size: this is what gets saved, and what a restart or rebuild recreates.
    int w = m_collapsed && m_expandedW > 0 ? m_expandedW : (int)m_render->Width();
    int h = m_collapsed && m_expandedH > 0 ? m_expandedH : (int)m_render->Height();
    return { m_id, m_title, m_x, m_y, w, h, true, m_sourceFolder, m_sortColumn, m_sortAscending };
}

// ── Sort ──

// ── Search / filter ──

// ── Folder mapping ──

void FenceWindow::RegisterClass(HINSTANCE hInst) {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = ClassName();
    RegisterClassExW(&wc);
}

LRESULT FenceWindow::OnDropFiles(UINT msg, WPARAM wp, LPARAM lp)     {
    HDROP hdrop = (HDROP)wp;
    SetDragOver(false);

    POINT dropPt = {};
    DragQueryPoint(hdrop, &dropPt);
    float dx = (float)dropPt.x, dy = (float)dropPt.y;

    UINT count = DragQueryFileW(hdrop, 0xFFFFFFFF, nullptr, 0);
    for (UINT i = 0; i < count; i++) {
        WCHAR path[MAX_PATH];
        if (DragQueryFileW(hdrop, i, path, MAX_PATH)) {
            WCHAR display[MAX_PATH];
            wcscpy_s(display, PathFindFileNameW(path));
            PathRemoveExtensionW(display);
            if (display[0])
                AddDroppedItem(display, path, dx, dy);
        }
    }
    DragFinish(hdrop);
    return 0;
}

LRESULT FenceWindow::OnSize(UINT msg, WPARAM wp, LPARAM lp)     {
    UINT w = LOWORD(lp), h = HIWORD(lp);
    if (w > 0 && h > 0 && (w != m_render->Width() || h != m_render->Height())) {
        m_render->Resize(w, h);
        // Re-seat the icons on the resized grid (also resolves overlaps
        // when the fence shrinks). Skip while collapsing: the tiny height
        // would crush every icon's row and the layout would be lost on
        // expand. RelayoutIcons redraws on its own.
        if (m_collapsed)                                      Redraw();
        else if (m_render->Appearance().autoReflow)           ReflowIcons();
        else                                                  RelayoutIcons();
    }
    return 0;
}

LRESULT FenceWindow::OnWindowPosChanged(UINT msg, WPARAM wp, LPARAM lp)     {
    if (this) {
        RECT wr; GetWindowRect(m_hwnd, &wr);
        m_x = wr.left;
        m_y = wr.top;
        // Re-snapshot the frosted-glass backdrop shortly after the
        // window settles. SetTimer resets the countdown, so a continuous
        // move/resize only triggers one capture once it stops.
        SetTimer(m_hwnd, 2, 150, nullptr);
    }
    return DefWindowProcW(m_hwnd, msg, wp, lp);
}

LRESULT FenceWindow::OnFenceRefresh(UINT msg, WPARAM wp, LPARAM lp) {
    if (this) SetTimer(m_hwnd, 5, 500, nullptr);
    return 0;
}

LRESULT FenceWindow::OnShowWindow(UINT msg, WPARAM wp, LPARAM lp) {
    // Win+D ("Show desktop") hides every top-level window that is not
    // part of the shell's desktop tree. A fence belongs *on* the desktop,
    // so resist any hide we did not cause ourselves (m_appHidden is set
    // by Hide()). The re-show is posted, not done in-place: by the time
    // the posted message runs, the hide sweep has finished, so the fence
    // cannot be hidden a second time.
    // (lp != 0 means the hide came from a parent state change — as a
    // desktop child we get those legitimately; only fight direct hides.)
    if (!wp && !lp && !m_appHidden)
        PostMessage(m_hwnd, WM_FENCE_RESHOW, 0, 0);
    return 0;
}

LRESULT FenceWindow::OnFenceReshow(UINT msg, WPARAM wp, LPARAM lp) {
    if (!m_appHidden && !IsWindowVisible(m_hwnd))
        ShowWindow(m_hwnd, SW_SHOWNA);   // don't steal focus from the desktop
    return 0;
}

LRESULT FenceWindow::OnSysCommand(UINT msg, WPARAM wp, LPARAM lp) {
    // Win+M minimizes every window; a toolwindow fence has no taskbar
    // button to bring it back, so simply refuse to minimize.
    if ((wp & 0xFFF0) == SC_MINIMIZE) return 0;
    return DefWindowProcW(m_hwnd, msg, wp, lp);
}

LRESULT FenceWindow::OnDestroy(UINT msg, WPARAM wp, LPARAM lp) {
    if (this) { StopSourceWatch(); SetWindowLongPtr(m_hwnd, GWLP_USERDATA, 0); }
    return 0;
}

LRESULT CALLBACK FenceWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* self = (FenceWindow*)GetWindowLongPtr(hwnd, GWLP_USERDATA);

    // While the 新建 menu session is live, forward the menu messages the
    // shell handler needs (Explorer forwards these to its handlers too):
    // WM_INITMENUPOPUP fills the lazy submenu, DRAW/MEASURE let the classic
    // TrackPopupMenu fallback render the handler's owner-drawn icons.
    if (ShellMenu::ActiveNewMenu() && (msg == WM_INITMENUPOPUP || msg == WM_DRAWITEM ||
                                       msg == WM_MEASUREITEM))
        ShellMenu::ForwardMenuMessage(ShellMenu::ActiveNewMenu(), msg, wp, lp);

    if (msg == WM_CREATE) {
        auto* cs = (CREATESTRUCT*)lp;
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
        return 0;
    }
    // Before WM_CREATE has run the window has no FenceWindow yet.
    if (!self) return DefWindowProcW(hwnd, msg, wp, lp);

    switch (msg) {
    case WM_DROPFILES: return self->OnDropFiles(msg, wp, lp);
    case WM_NCHITTEST: return self->OnNcHitTest(msg, wp, lp);
    case WM_SETCURSOR: return self->OnSetCursor(msg, wp, lp);
    case WM_SIZE: return self->OnSize(msg, wp, lp);
    case WM_WINDOWPOSCHANGED: return self->OnWindowPosChanged(msg, wp, lp);
    case WM_NCLBUTTONDOWN: return self->OnNcLButtonDown(msg, wp, lp);
    case WM_LBUTTONDOWN: return self->OnLButtonDown(msg, wp, lp);
    case WM_MOUSEMOVE: return self->OnMouseMove(msg, wp, lp);
    case WM_LBUTTONUP: return self->OnLButtonUp(msg, wp, lp);
    case WM_CAPTURECHANGED: return self->OnCaptureChanged(msg, wp, lp);
    case WM_MOUSELEAVE: return self->OnMouseLeave(msg, wp, lp);
    case WM_LBUTTONDBLCLK: return self->OnLButtonDblClk(msg, wp, lp);
    case WM_RBUTTONUP: return self->OnRButtonUp(msg, wp, lp);
    case WM_MOUSEWHEEL: return self->OnMouseWheel(msg, wp, lp);
    case WM_CHAR: return self->OnChar(msg, wp, lp);
    case WM_KEYDOWN: return self->OnKeyDown(msg, wp, lp);
    case WM_KILLFOCUS: return self->OnKillFocus(msg, wp, lp);
    case WM_TIMER: return self->OnTimer(msg, wp, lp);
    case WM_FENCE_REFRESH: return self->OnFenceRefresh(msg, wp, lp);
    case WM_SHOWWINDOW: return self->OnShowWindow(msg, wp, lp);
    case WM_FENCE_RESHOW: return self->OnFenceReshow(msg, wp, lp);
    case WM_SYSCOMMAND: return self->OnSysCommand(msg, wp, lp);
    case WM_DESTROY: return self->OnDestroy(msg, wp, lp);
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
