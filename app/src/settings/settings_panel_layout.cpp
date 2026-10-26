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

void SettingsPanel::ComputeLayout() {
    float s = m_s;
    m_hits.clear();
    m_hits.reserve(32);

    auto R = [&](float x, float y, float w, float h) {
        return RECT{ (LONG)x, (LONG)y, (LONG)(x + w), (LONG)(y + h) };
    };
    auto addHit = [&](int id, RECT rc) { m_hits.push_back({id, rc}); };

    float W = (float)m_panelW;
    float cx = 20 * s;                 // content left
    float cw = 320 * s;                // content width
    m_rowH = 26 * s;

    // Header
    m_closeBtn = R(W - 40 * s, 6 * s, 32 * s, 32 * s);
    addHit(HIT_CLOSE, m_closeBtn);

    // Font label + list viewport + scrollbar
    m_fontLblY = 52 * s;
    float listTop = 70 * s;
    float sbW = 8 * s, gap = 4 * s;
    float listW = cw - sbW - gap;
    float listH = m_fontRows * m_rowH;
    m_fontList = R(cx, listTop, listW, listH);
    addHit(HIT_FONT_LIST, m_fontList);
    m_fontScroll = R(cx + listW + gap, listTop, sbW, listH);
    addHit(HIT_FONT_SCROLL, m_fontScroll);

    float listBot = listTop + listH;

    // Font size
    m_sizeLblY = listBot + 12 * s;
    m_sizeSlider = R(cx, listBot + 30 * s, cw, 26 * s);
    addHit(HIT_SIZE_SLIDER, m_sizeSlider);

    // Opacity
    m_opLblY = listBot + 66 * s;
    m_opSlider = R(cx, listBot + 84 * s, cw, 26 * s);
    addHit(HIT_OPACITY_SLIDER, m_opSlider);

    // Panel color swatches
    m_panelLblY = listBot + 122 * s;
    float sw = 26 * s, swGap = 6 * s;
    float swY = listBot + 140 * s;
    for (int i = 0; i < 8; i++) {
        m_panelSwatch[i] = R(cx + i * (sw + swGap), swY, sw, sw);
        addHit(HIT_PANEL_SWATCH0 + i, m_panelSwatch[i]);
    }
    m_panelMore = R(cx + 8 * (sw + swGap), swY, sw, sw);
    addHit(HIT_PANEL_MORE, m_panelMore);

    // Title bar color swatches
    m_titleLblY = listBot + 176 * s;
    float twY = listBot + 194 * s;
    for (int i = 0; i < 8; i++) {
        m_titleSwatch[i] = R(cx + i * (sw + swGap), twY, sw, sw);
        addHit(HIT_TITLE_SWATCH0 + i, m_titleSwatch[i]);
    }
    m_titleMore = R(cx + 8 * (sw + swGap), twY, sw, sw);
    addHit(HIT_TITLE_MORE, m_titleMore);

    // Title name position (left / center / right) — 3-way segmented control
    m_titlePosLblY = listBot + 232 * s;
    float segW = (cw - 8 * s) / 3, tpY = listBot + 250 * s;
    for (int i = 0; i < 3; i++) {
        m_titlePosSeg[i] = R(cx + i * (segW + 4 * s), tpY, segW, 26 * s);
        addHit(HIT_TITLEPOS0 + i, m_titlePosSeg[i]);
    }

    // Title bar height stepper
    m_thLblY = listBot + 288 * s;
    float btn = 26 * s, stY = listBot + 306 * s;
    m_thMinus = R(cx, stY, btn, btn);
    addHit(HIT_TITLEH_MINUS, m_thMinus);
    m_thPlus = R(cx + cw - btn, stY, btn, btn);
    addHit(HIT_TITLEH_PLUS, m_thPlus);

    // ── Second column: layout behaviour, collapse, presets ──
    float cx2 = 372 * s, cw2 = 308 * s;
    float half = (cw2 - 4 * s) / 2, third = (cw2 - 8 * s) / 3;
    auto row = [&](int k, float& labelY, int n, float w, RECT* out, int hitBase) {
        labelY = (52 + k * 56) * s;
        float y = (70 + k * 56) * s;
        for (int i = 0; i < n; i++) {
            out[i] = R(cx2 + i * (w + 4 * s), y, w, 26 * s);
            addHit(hitBase + i, out[i]);
        }
    };
    row(0, m_displayModeLblY, 2, half, m_displayModeSeg, HIT_DISPMODE0);
    row(1, m_fillDirLblY,     2, half, m_fillDirSeg,     HIT_FILLDIR0);
    row(2, m_autoFitLblY,     2, half, m_autoFitSeg,     HIT_AUTOFIT0);
    row(3, m_collapseLblY,    2, half, m_collapseSeg,    HIT_COLLAPSE0);
    row(4, m_titleIconLblY,   2, half, m_titleIconSeg,   HIT_TITLEICON0);
    row(5, m_iconPickLblY,    3, third, m_iconPickSeg,   HIT_ICONPICK0);

    row(6, m_tileSnapLblY,    2, half, m_tileSnapSeg,    HIT_TILESNAP0);

    m_sepY = 448 * s;
    m_presetsBtn = R(cx2, 460 * s, cw2, 30 * s);
    addHit(HIT_PRESETS, m_presetsBtn);
    // Language lives in the tray menu; the defaults have no fence to delete.
    if (m_fence) {
        m_deleteBtn = R(cx2, 502 * s, cw2, 30 * s);
        addHit(HIT_DELETE_FENCE, m_deleteBtn);
    }
}
