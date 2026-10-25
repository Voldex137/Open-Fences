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

FenceWindow::IconGrid FenceWindow::GetIconGrid() const {
    const auto& app = m_render->Appearance();
    float W = (float)m_render->Width(), H = (float)m_render->Height();
    float ct = ContentTop();
    IconGrid g;
    if (app.displayMode == 1) {   // list mode: single column, compact rows
        g.cw = W - 2 * kGridPad;
        g.ch = app.iconSize * 2.6f;  // one line of text + small icon + padding
        g.x0 = kGridPad;
        g.y0 = ct + kGridPad;
        g.cols = 1;
        int visRows = (std::max)(1, (int)((H - kGridPad - g.y0) / g.ch));
        g.rows = (std::max)(visRows, VisibleCount());
    } else {
        g.cw = app.cellW; g.ch = app.cellH;
        g.x0 = kGridPad;
        g.y0 = ct + kGridPad;
        g.cols = (std::max)(1, (int)((W - 2 * kGridPad) / g.cw));
        g.rows = (std::max)(1, (int)((H - kGridPad - g.y0) / g.ch));
    }
    return g;
}

int FenceWindow::CellOf(const IconEntry& e) const {
    IconGrid g = GetIconGrid();
    // Map the icon's *center* so a half-cell offset still rounds to the cell
    // the icon visually belongs to.
    int c = (int)floorf((e.x + g.cw * 0.5f - g.x0) / g.cw);
    int r = (int)floorf((e.y + g.ch * 0.5f - g.y0) / g.ch);
    c = (std::min)((std::max)(c, 0), g.cols - 1);
    r = (std::min)((std::max)(r, 0), g.rows - 1);
    return r * g.cols + c;
}

void FenceWindow::PlaceIconAt(IconEntry& e, int cell) const {
    IconGrid g = GetIconGrid();
    int c = cell % g.cols, r = cell / g.cols;
    // Whole-pixel positions: fractional offsets force bilinear resampling
    // and make the glyph look blurry.
    e.x = floorf(g.x0 + c * g.cw + 0.5f);
    e.y = floorf(g.y0 + r * g.ch + 0.5f);
}

std::vector<int> FenceWindow::OccupiedCells(const std::vector<int>& excludes) const {
    std::vector<int> occ;
    occ.reserve(m_icons.size());
    for (int i = 0; i < (int)m_icons.size(); i++)
        if (std::find(excludes.begin(), excludes.end(), i) == excludes.end())
            occ.push_back(CellOf(m_icons[i]));
    return occ;
}

int FenceWindow::FindFreeCell(int prefer, const std::vector<int>& occupied) const {
    IconGrid g = GetIconGrid();
    auto taken = [&](int idx) {
        return std::find(occupied.begin(), occupied.end(), idx) != occupied.end();
    };
    if (!taken(prefer)) return prefer;
    // Walk Chebyshev rings around the preferred cell, columns first so a
    // crowded drop fills downward, then to the right — desktop-like.
    int pc = prefer % g.cols, pr = prefer / g.cols;
    auto dist = [](int a, int b) { int d = a - b; return d < 0 ? -d : d; };
    for (int d = 1, maxD = (std::max)(g.cols, g.rows); d <= maxD; d++) {
        for (int c = pc - d; c <= pc + d; c++) {
            for (int r = pr - d; r <= pr + d; r++) {
                if ((std::max)(dist(c, pc), dist(r, pr)) != d) continue;   // ring only
                if (c < 0 || c >= g.cols || r < 0 || r >= g.rows) continue;
                int idx = r * g.cols + c;
                if (!taken(idx)) return idx;
            }
        }
    }
    return prefer;   // fence completely full — overlap is unavoidable
}

int FenceWindow::FindPushCell(int prefer, const std::vector<int>& occupied, int dirX) const {
    IconGrid g = GetIconGrid();
    auto taken = [&](int idx) {
        return std::find(occupied.begin(), occupied.end(), idx) != occupied.end();
    };
    int pc = prefer % g.cols, pr = prefer / g.cols;
    int maxD = (std::max)(g.cols, g.rows);
    // Scan one row outward from pc: the drag-direction side first, then the
    // other side, nearest cells first — pushed icons slide along the row.
    auto scanRow = [&](int r) -> int {
        if (r < 0 || r >= g.rows) return -1;
        for (int dc = 0; dc <= maxD; dc++) {
            int passes = (dc == 0) ? 1 : 2;
            for (int pass = 0; pass < passes; pass++) {
                int side;
                if (dc == 0)       side = 0;
                else if (pass == 0) side = (dirX < 0) ? -1 : 1;
                else                side = (dirX < 0) ? 1 : -1;
                int c = pc + side * dc;
                if (c < 0 || c >= g.cols) continue;
                int idx = r * g.cols + c;
                if (!taken(idx)) return idx;
            }
        }
        return -1;
    };
    // Same row first; only a completely full row spills, downward before up.
    for (int dr = 0; dr <= maxD; dr++) {
        if (dr == 0) {
            int f = scanRow(pr);
            if (f >= 0) return f;
        } else {
            int f = scanRow(pr + dr);
            if (f >= 0) return f;
            f = scanRow(pr - dr);
            if (f >= 0) return f;
        }
    }
    return prefer;   // fence completely full — overlap unavoidable
}

void FenceWindow::SaveGridLayout() {
    m_gridH = (int)m_render->Height();
    m_gridPositions.clear();
    m_gridPositions.reserve(m_icons.size());
    for (const auto& ic : m_icons)
        m_gridPositions.push_back({ic.x, ic.y});
}

void FenceWindow::RestoreGridLayout() {
    if (SearchActive() || m_gridPositions.size() != m_icons.size()) return;
    for (size_t i = 0; i < m_icons.size(); i++) {
        m_icons[i].x = m_gridPositions[i].first;
        m_icons[i].y = m_gridPositions[i].second;
    }
    if (m_gridH > 0) {
        int th = (int)(m_render->Appearance().titleH + 0.5f);
        int minH = th + (int)(40.0f * Desktop::DpiScale() + 0.5f);
        int h = (std::max)(m_gridH, minH);
        int w = (int)m_render->Width();
        SetWindowPos(m_hwnd, nullptr, 0, 0, w, h,
            SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

void FenceWindow::ClampIconPos(float& x, float& y) const {
    IconGrid g = GetIconGrid();
    float minX = kGridPad;
    float minY = ContentTop() + kGridPad;
    float maxX = (float)m_render->Width()  - g.cw - kGridPad;
    float maxY = (float)m_render->Height() - g.ch - kGridPad;
    // (max)(…, minX) keeps the clamp sane if the fence is smaller than a cell
    x = (std::min)((std::max)(x, minX), (std::max)(maxX, minX));
    y = (std::min)((std::max)(y, minY), (std::max)(maxY, minY));
}

float FenceWindow::ListMaxScroll() const {
    const auto& app = m_render->Appearance();
    if (app.displayMode != 1 || m_icons.empty()) return 0;
    float rowH = app.iconSize * 2.6f;
    float totalH = (float)VisibleCount() * rowH;
    float contentH = (float)m_render->Height() - ContentTop();
    return (std::max)(0.0f, totalH - contentH);
}

void FenceWindow::ClampListScroll() {
    float maxS = ListMaxScroll();
    if (m_listScroll > maxS) m_listScroll = maxS;
    if (m_listScroll < 0) m_listScroll = 0;
}

void FenceWindow::FitToListHeight() {
    if (m_icons.empty()) return;
    const auto& app = m_render->Appearance();
    float rowH = app.iconSize * 2.6f;
    int ct = (int)(ContentTop() + 0.5f);
    int neededH = ct + (int)(kGridPad + VisibleCount() * rowH + kGridPad + 0.5f);
    int minH = ct + (int)(40.0f * Desktop::DpiScale() + 0.5f);
    neededH = (std::max)(neededH, minH);
    int w = (int)m_render->Width();
    SetWindowPos(m_hwnd, nullptr, 0, 0, w, neededH,
        SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void FenceWindow::RelayoutIcons() {
    if (m_collapsed) return;
    if (SearchActive()) { RelayoutFiltered(); return; }
    std::vector<int> taken;
    taken.reserve(m_icons.size());
    for (auto& ic : m_icons) {
        int cell = FindFreeCell(CellOf(ic), taken);
        PlaceIconAt(ic, cell);
        taken.push_back(cell);
    }
    Redraw();
}

void FenceWindow::SyncDesktopIconSize(int physicalPx) {
    if (!m_hwnd || physicalPx <= 0) return;
    // Never fight a live gesture — the icon being dragged (or the whole
    // fence being moved/resized, or a marquee on the move) works in the
    // current metrics; catch up instead.
    if (m_dragIcon >= 0 || m_moving || m_resizeMask || m_marquee) return;
    if (!m_render->AdoptDesktopIconSize(physicalPx)) return;
    if (m_collapsed) {
        // The grid cannot be rebuilt while folded (height = title bar only).
        m_relayoutPending = true;
        return;
    }
    RelayoutIcons();   // re-seats every icon on the resized grid and redraws
}

void FenceWindow::PushOccupantOf(int cell, const std::vector<int>& excludes,
                                 int dirX) {
    for (int j = 0; j < (int)m_icons.size(); j++) {
        if (std::find(excludes.begin(), excludes.end(), j) != excludes.end())
            continue;
        if (CellOf(m_icons[j]) != cell) continue;
        // Occupancy excludes only the floating (dragged) group; the
        // occupant's own claim on `cell` keeps the search from sending it
        // right back. The cells the group lifted from count as free, so
        // pushes naturally flow into the gaps the drag leaves behind. dirX
        // steers the search along the row in the drag's direction.
        PlaceIconAt(m_icons[j], FindPushCell(cell, OccupiedCells(excludes), dirX));
        return;   // one occupant per cell
    }
}

void FenceWindow::PushGroupOut() {
    // Clear bystanders from every cell the dragged group currently covers.
    // Distinct cells only — members folded onto one cell during the drag
    // must not trigger the same sweep twice.
    std::vector<int> covered;
    covered.reserve(m_dragGroup.size());
    for (const auto& d : m_dragGroup) {
        if (d.idx < 0 || d.idx >= (int)m_icons.size()) continue;
        int c = CellOf(m_icons[d.idx]);
        if (std::find(covered.begin(), covered.end(), c) == covered.end())
            covered.push_back(c);
    }
    for (int c : covered) PushOccupantOf(c, m_dragExcludes, m_dragDirX);
}

void FenceWindow::SettleDragGroup() {
    if (m_dragIcon >= 0 && !m_dragGroup.empty()) {
        // Settle the pressed icon first, then the rest of the group in draw
        // order: each snaps to the nearest cell that is free of both the
        // bystanders and the already-settled members. Before the drag
        // threshold every member sits on its own cell, so this settles in
        // place — no visible change for a plain click.
        std::vector<int> order;
        order.reserve(m_dragGroup.size());
        for (const auto& d : m_dragGroup) if (d.idx == m_dragIcon) order.push_back(d.idx);
        for (const auto& d : m_dragGroup) if (d.idx != m_dragIcon) order.push_back(d.idx);
        std::vector<int> taken = OccupiedCells(m_dragExcludes);
        taken.reserve(taken.size() + order.size());
        for (int idx : order) {
            if (idx < 0 || idx >= (int)m_icons.size()) continue;
            int cell = FindFreeCell(CellOf(m_icons[idx]), taken);
            PlaceIconAt(m_icons[idx], cell);
            taken.push_back(cell);
        }
    }
    if (m_dragMoved) Config::MarkDirty();   // icons changed cells
    m_dragIcon = -1;
    m_dragHoverCell = -1;
    m_dragDirX = 0;
    m_dragMoved = false;
    m_dragGroup.clear();
    m_dragExcludes.clear();
    Redraw();
}

float FenceWindow::ContentTop() const {
    const auto& app = m_render->Appearance();
    return app.titleH + (IsMapped() ? app.searchH : 0.0f);
}

void FenceWindow::RelayoutFiltered() {
    if (!SearchActive()) { RelayoutIcons(); return; }
    IconGrid g = GetIconGrid();
    for (int k = 0; k < (int)m_filterIdx.size(); k++)
        PlaceIconAt(m_icons[m_filterIdx[k]], k);
}
