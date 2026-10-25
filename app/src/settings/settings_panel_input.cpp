#include "settings/settings_panel.h"
#include "settings/settings_internal.h"
#include "shell/desktop_host.h"
#include "core/localization.h"
#include "fence/fence_window.h"
#include "core/config.h"
#include <windowsx.h>
#include <commdlg.h>
#include <algorithm>
#include <cmath>

int SettingsPanel::HitTest(int mx, int my) const {
    POINT p{mx, my};
    for (int i = (int)m_hits.size() - 1; i >= 0; i--)
        if (PtInRect(&m_hits[i].rc, p)) return m_hits[i].id;
    return HIT_NONE;
}

void SettingsPanel::PickFont(int idx) {
    if (idx < 0 || idx >= (int)m_fonts.size()) return;
    auto& app = m_fence->GetRender().Appearance();
    wcscpy_s(app.fontName, _countof(app.fontName), m_fonts[idx].c_str());
    m_selFont = idx;
    if (idx < m_fontScrollTop) m_fontScrollTop = idx;
    if (idx >= m_fontScrollTop + m_fontRows) m_fontScrollTop = idx - m_fontRows + 1;
    ApplyToFence();
    Repaint();
}

void SettingsPanel::SetSizeValue(int v) {
    v = clampi(v, 8, 24);
    auto& app = m_fence->GetRender().Appearance();
    int cur = (int)(app.fontSize / m_s + 0.5f);
    if (v != cur) { app.fontSize = (float)v * m_s; ApplyToFence(); Repaint(); }
}

void SettingsPanel::SetOpacityValue(int v) {
    v = clampi(v, 30, 100);
    auto& app = m_fence->GetRender().Appearance();
    int cur = (int)(app.bg[3] * 100 + 0.5f);
    if (v != cur) {
        app.bg[3] = v / 100.0f;
        app.title[3] = (std::min)(1.0f, v / 100.0f + 0.10f);
        ApplyToFence();
        Repaint();
    }
}

void SettingsPanel::SetSizeFromX(int mx) {
    float x0 = m_sizeSlider.left + 9 * m_s, x1 = m_sizeSlider.right - 9 * m_s;
    float t = x1 > x0 ? ((float)mx - x0) / (x1 - x0) : 0;
    SetSizeValue(8 + (int)(clampf(t, 0, 1) * (24 - 8) + 0.5f));
}

void SettingsPanel::SetOpacityFromX(int mx) {
    float x0 = m_opSlider.left + 9 * m_s, x1 = m_opSlider.right - 9 * m_s;
    float t = x1 > x0 ? ((float)mx - x0) / (x1 - x0) : 0;
    SetOpacityValue(30 + (int)(clampf(t, 0, 1) * (100 - 30) + 0.5f));
}

void SettingsPanel::AdjustTitleH(float deltaDesign) {
    auto& app = m_fence->GetRender().Appearance();
    float nv = clampf(app.titleH + deltaDesign * m_s, 16 * m_s, 80 * m_s);
    if (fabsf(nv - app.titleH) > 0.5f) {
        app.titleH = nv;
        ApplyToFence();
        m_fence->RelayoutIcons();   // the grid's top edge moved; re-seat icons
        Repaint();
    }
}

void SettingsPanel::SetPanelColor(int idx) {
    auto& app = m_fence->GetRender().Appearance();
    app.bg[0] = kPanelColors[idx][0];
    app.bg[1] = kPanelColors[idx][1];
    app.bg[2] = kPanelColors[idx][2];
    ApplyToFence();
    Repaint();
}

void SettingsPanel::SetTitleColor(int idx) {
    auto& app = m_fence->GetRender().Appearance();
    app.title[0] = kTitleColors[idx][0];
    app.title[1] = kTitleColors[idx][1];
    app.title[2] = kTitleColors[idx][2];
    ApplyToFence();
    Repaint();
}

void SettingsPanel::ChoosePanelColor() {
    auto& app = m_fence->GetRender().Appearance();
    CHOOSECOLORW cc = {sizeof(cc)};
    static COLORREF cust[16] = {};
    cc.lpCustColors = cust;
    cc.rgbResult = RGB((int)(app.bg[0] * 255), (int)(app.bg[1] * 255), (int)(app.bg[2] * 255));
    cc.Flags = CC_RGBINIT | CC_FULLOPEN | CC_ANYCOLOR;
    cc.hwndOwner = m_hwnd;
    if (ChooseColorW(&cc)) {
        app.bg[0] = GetRValue(cc.rgbResult) / 255.0f;
        app.bg[1] = GetGValue(cc.rgbResult) / 255.0f;
        app.bg[2] = GetBValue(cc.rgbResult) / 255.0f;
        ApplyToFence();
        Repaint();
    }
}

void SettingsPanel::ChooseTitleColor() {
    auto& app = m_fence->GetRender().Appearance();
    CHOOSECOLORW cc = {sizeof(cc)};
    static COLORREF cust[16] = {};
    cc.lpCustColors = cust;
    cc.rgbResult = RGB((int)(app.title[0] * 255), (int)(app.title[1] * 255), (int)(app.title[2] * 255));
    cc.Flags = CC_RGBINIT | CC_FULLOPEN | CC_ANYCOLOR;
    cc.hwndOwner = m_hwnd;
    if (ChooseColorW(&cc)) {
        app.title[0] = GetRValue(cc.rgbResult) / 255.0f;
        app.title[1] = GetGValue(cc.rgbResult) / 255.0f;
        app.title[2] = GetBValue(cc.rgbResult) / 255.0f;
        ApplyToFence();
        Repaint();
    }
}

void SettingsPanel::ScrollFontList(int deltaRows) {
    int total = (int)m_fonts.size();
    if (total <= m_fontRows) return;
    int newTop = clampi(m_fontScrollTop + deltaRows, 0, total - m_fontRows);
    if (newTop != m_fontScrollTop) { m_fontScrollTop = newTop; Repaint(); }
}

void SettingsPanel::OnLButtonDown(int mx, int my) {
    // Font rows are not in m_hits; resolve them by row index.
    POINT p{mx, my};
    if (PtInRect(&m_fontList, p)) {
        int row = (int)((my - m_fontList.top) / m_rowH);
        PickFont(m_fontScrollTop + row);
        return;
    }
    int id = HitTest(mx, my);
    switch (id) {
    case HIT_CLOSE:         m_closing = true; break;
    case HIT_DELETE_FENCE:  m_deleteFence = true; m_closing = true; break;
    case HIT_SIZE_SLIDER:   m_dragSlider = 1; SetCapture(m_hwnd); SetSizeFromX(mx); break;
    case HIT_OPACITY_SLIDER: m_dragSlider = 2; SetCapture(m_hwnd); SetOpacityFromX(mx); break;
    case HIT_FONT_SCROLL:
        m_scrollDrag = true;
        m_scrollDragStartY = my;
        m_scrollDragStartTop = m_fontScrollTop;
        SetCapture(m_hwnd);
        break;
    case HIT_TITLEH_MINUS:  AdjustTitleH(-4); break;
    case HIT_TITLEH_PLUS:   AdjustTitleH(+4); break;
    case HIT_TITLEPOS0: case HIT_TITLEPOS0 + 1: case HIT_TITLEPOS0 + 2: {
        auto& app = m_fence->GetRender().Appearance();
        int align = id - HIT_TITLEPOS0;
        if (app.titleAlign != align) { app.titleAlign = align; ApplyToFence(); Repaint(); }
        break;
    }
    case HIT_PANEL_MORE:    ChoosePanelColor(); break;
    case HIT_TITLE_MORE:    ChooseTitleColor(); break;
    case HIT_FILLDIR0: case HIT_FILLDIR0 + 1: {
        auto& app = m_fence->GetRender().Appearance();
        int dir = id - HIT_FILLDIR0;
        if (app.fillDir != dir) {
            app.fillDir = dir;
            ApplyToFence();
            m_fence->ReflowIcons();
            Repaint();
        }
        break;
    }
    case HIT_DISPMODE0: case HIT_DISPMODE0 + 1: {
        auto& app = m_fence->GetRender().Appearance();
        int mode = id - HIT_DISPMODE0;
        if (app.displayMode != mode) {
            if (mode == 1) {   // switching to list: save grid layout
                m_fence->SaveGridLayout();
            }
            app.displayMode = mode;
            ApplyToFence();
            if (mode == 1) {
                m_fence->RelayoutIcons();
            } else {
                m_fence->RestoreGridLayout();
                m_fence->RelayoutIcons();
            }
            Repaint();
        }
        break;
    }
    default:
        if (id >= HIT_PANEL_SWATCH0 && id < HIT_PANEL_SWATCH0 + 8) SetPanelColor(id - HIT_PANEL_SWATCH0);
        else if (id >= HIT_TITLE_SWATCH0 && id < HIT_TITLE_SWATCH0 + 8) SetTitleColor(id - HIT_TITLE_SWATCH0);
        break;
    }
}

void SettingsPanel::OnMouseMove(int mx, int my) {
    POINT p{mx, my};
    int oldHover = m_hover, oldRow = m_hoverRow;
    if (PtInRect(&m_fontList, p)) {
        m_hover = HIT_FONT_LIST;
        m_hoverRow = (int)((my - m_fontList.top) / m_rowH);
    } else {
        m_hover = HitTest(mx, my);
        m_hoverRow = -1;
    }
    if (m_hover != oldHover || m_hoverRow != oldRow) Repaint();

    if (m_dragSlider == 1) SetSizeFromX(mx);
    else if (m_dragSlider == 2) SetOpacityFromX(mx);
    else if (m_scrollDrag) {
        int total = (int)m_fonts.size();
        if (total > m_fontRows) {
            float trackH = (float)(m_fontScroll.bottom - m_fontScroll.top);
            float thumbH = (std::max)(20 * m_s, trackH * m_fontRows / total);
            float freeH = trackH - thumbH;
            int dy = my - m_scrollDragStartY;
            int delta = freeH > 1
                ? (int)((dy / freeH) * (total - m_fontRows) + (dy >= 0 ? 0.5f : -0.5f))
                : 0;
            int newTop = clampi(m_scrollDragStartTop + delta, 0, total - m_fontRows);
            if (newTop != m_fontScrollTop) { m_fontScrollTop = newTop; Repaint(); }
        }
    }
}

void SettingsPanel::OnLButtonUp(int, int) {
    if (m_dragSlider || m_scrollDrag) {
        m_dragSlider = 0;
        m_scrollDrag = false;
        ReleaseCapture();
    }
}

void SettingsPanel::OnMouseWheel(int delta, int sx, int sy) {
    POINT p{sx, sy};
    ScreenToClient(m_hwnd, &p);
    if (PtInRect(&m_fontList, p) || PtInRect(&m_fontScroll, p)) {
        ScrollFontList(delta > 0 ? -3 : 3);
    } else if (PtInRect(&m_sizeSlider, p)) {
        auto& app = m_fence->GetRender().Appearance();
        SetSizeValue((int)(app.fontSize / m_s + 0.5f) + (delta > 0 ? 1 : -1));
    } else if (PtInRect(&m_opSlider, p)) {
        auto& app = m_fence->GetRender().Appearance();
        SetOpacityValue((int)(app.bg[3] * 100 + 0.5f) + (delta > 0 ? 1 : -1));
    }
}
