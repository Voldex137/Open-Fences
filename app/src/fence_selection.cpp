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

// Path-keyed membership: the bring-to-front reorder a drag performs must
// not drop the selection, so no selection state may hang off indexes.
static bool ContainsPath(const std::vector<std::wstring>& paths,
                         const std::wstring& path) {
    return std::find(paths.begin(), paths.end(), path) != paths.end();
}

bool FenceWindow::IsPathSelected(const std::wstring& path) const {
    return ContainsPath(m_selectedPaths, path);
}

void FenceWindow::ClearSelection() { m_selectedPaths.clear(); }

int FenceWindow::SelectedIndex() const {
    for (int i = 0; i < (int)m_icons.size(); i++)
        if (IsPathSelected(m_icons[i].path)) return i;
    return -1;
}

void FenceWindow::BringSelectedToFront() {
    // Selected icons draw above the rest (draw order = vector order). The
    // relative order inside each group is kept, and selection is path-keyed,
    // so callers must re-locate any index they still hold.
    if (m_selectedPaths.empty()) return;
    std::vector<IconEntry> rest, sel;
    rest.reserve(m_icons.size());
    sel.reserve(m_selectedPaths.size());
    for (auto& e : m_icons) {
        if (IsPathSelected(e.path)) sel.push_back(std::move(e));
        else                        rest.push_back(std::move(e));
    }
    m_icons.clear();
    for (auto& e : rest) m_icons.push_back(std::move(e));
    for (auto& e : sel)  m_icons.push_back(std::move(e));
    if (SearchActive()) RebuildFilter();
}

RECT FenceWindow::MarqueeRect() const {
    // Anchor→cursor, normalized and clamped to the fence body: the band
    // never paints over the title bar even when the captured cursor sweeps
    // above it, and it cannot leak past the window edges.
    int w = (int)m_render->Width(), h = (int)m_render->Height();
    int top = (int)(ContentTop() + 0.5f);
    int l = (std::min)(m_marqAX, m_marqCX), r = (std::max)(m_marqAX, m_marqCX);
    int t = (std::min)(m_marqAY, m_marqCY), b = (std::max)(m_marqAY, m_marqCY);
    l = (std::min)((std::max)(l, 0), w);
    r = (std::min)((std::max)(r, 0), w);
    t = (std::min)((std::max)(t, top), h);
    b = (std::min)((std::max)(b, top), h);
    return RECT{ l, t, r, b };
}

void FenceWindow::UpdateMarqueeSelection() {
    RECT band = MarqueeRect();
    IconGrid g = GetIconGrid();
    std::vector<std::wstring> next;
    if (m_marqueeAdd) next = m_marqueeBase;   // Ctrl: keep what was selected
    next.reserve(next.size() + m_icons.size());
    float adjY = 0;
    if (m_render->Appearance().displayMode == 1) adjY = -m_listScroll;
    for (int i = 0; i < (int)m_icons.size(); i++) {
        const auto& ic = m_icons[i];
        if (SearchActive() && !m_filterBits[i]) continue;
        // Test against the same cell rect HitTestIcon clicks against, and
        // count mere intersection (not containment) — desktop behavior.
        long visY = (long)(ic.y + adjY);
        RECT cell{ (long)ic.x, visY,
                   (long)(ic.x + g.cw), visY + (long)g.ch };
        RECT inter;
        if (!IntersectRect(&inter, &band, &cell)) continue;
        if (!ContainsPath(next, ic.path)) next.push_back(ic.path);
    }
    m_selectedPaths = std::move(next);
}
