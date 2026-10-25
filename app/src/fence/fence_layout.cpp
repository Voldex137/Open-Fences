#include "fence/fence_window.h"
#include "core/layout_metrics.h"
#include "shell/desktop_host.h"
#include "core/config.h"
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
    return IconGridMath::CellOf(GetIconGrid(), e.x, e.y);
}

void FenceWindow::PlaceIconAt(IconEntry& e, int cell) const {
    IconGridMath::CellOrigin(GetIconGrid(), cell, e.x, e.y);
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
    return IconGridMath::FindFreeCell(GetIconGrid(), prefer, occupied);
}

int FenceWindow::FindPushCell(int prefer, const std::vector<int>& occupied, int dirX) const {
    return IconGridMath::FindPushCell(GetIconGrid(), prefer, occupied, dirX);
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

ListScrollbar FenceWindow::CurrentListScrollbar() const {
    float rowH = m_render->Appearance().iconSize * 2.6f;
    return ComputeListScrollbar((float)m_render->Width(), (float)m_render->Height(), ContentTop(),
                                (float)VisibleCount() * rowH, m_listScroll);
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

void FenceWindow::ReflowIcons() {
    if (m_collapsed) return;
    const auto& app = m_render->Appearance();
    if (app.displayMode == 1 || SearchActive()) { RelayoutIcons(); return; }
    IconGrid g = GetIconGrid();
    std::vector<std::pair<float, float>> pos;
    pos.reserve(m_icons.size());
    for (auto& ic : m_icons) pos.push_back({ ic.x, ic.y });
    auto order = IconGridMath::ReadingOrder(g, pos, app.fillDir == 1);
    for (int k = 0; k < (int)order.size(); k++)
        PlaceIconAt(m_icons[order[k]], IconGridMath::CellForSequence(g, k, app.fillDir == 1));
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
