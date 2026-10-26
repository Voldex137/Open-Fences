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

// One frame of the panel, section by section (layout comes from ComputeLayout).

namespace {

D2D1_RECT_F ToRectF(const RECT& rc) {
    return D2D1_RECT_F{ (float)rc.left, (float)rc.top, (float)rc.right, (float)rc.bottom };
}

}  // namespace

void SettingsPanel::FillRounded(const RECT& rc, float radius, ID2D1Brush* brush) {
    m_render->Target()->FillRoundedRectangle(D2D1_ROUNDED_RECT{ ToRectF(rc), radius, radius }, brush);
}

void SettingsPanel::DrawString(const std::wstring& text, const RECT& rc, IDWriteTextFormat* format,
                               ID2D1Brush* brush) {
    m_render->Target()->DrawText(text.c_str(), (UINT32)text.size(), format, ToRectF(rc), brush,
                                 D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

// Hover outline of a button or swatch.
void SettingsPanel::DrawHoverRing(const RECT& rc, int hitId) {
    if (m_hover != hitId) return;
    m_render->Target()->DrawRoundedRectangle(
        D2D1_ROUNDED_RECT{ ToRectF(rc), 6 * m_s, 6 * m_s }, m_brHover.Get(), 2.0f);
}

// Caption above a control, and the value shown at the right end of that line.
void SettingsPanel::DrawLabel(const std::wstring& text, float y, float x) {
    float s = m_s;
    if (x < 0) x = 20 * s;
    m_render->Target()->DrawText(text.c_str(), (UINT32)text.size(), m_fmtLabel.Get(),
        D2D1_RECT_F{ x, y, x + 300 * s, y + 18 * s }, m_brText2.Get());
}

void SettingsPanel::DrawValueRight(const std::wstring& text, float y) {
    float s = m_s, W = (float)m_panelW;
    m_render->Target()->DrawText(text.c_str(), (UINT32)text.size(), m_fmtValue.Get(),
        D2D1_RECT_F{ W - 100 * s, y, W - 20 * s, y + 18 * s }, m_brText.Get());
}

void SettingsPanel::DrawPanel() {
    ComputeLayout();
    if (!m_render->Target()) return;
    m_render->BeginDraw();

    const FenceAppearance& app = App();
    DrawSurface();
    DrawHeader();
    DrawFontList();
    DrawSliders(app);
    DrawColorSwatches(app);
    DrawSegments(L10n::Tr("settings.title_position"), m_titlePosLblY,
                 { L10n::Tr("settings.align.left"), L10n::Tr("settings.align.center"),
                   L10n::Tr("settings.align.right") },
                 m_titlePosSeg, app.titleAlign, HIT_TITLEPOS0);
    DrawLabel(L10n::Tr("settings.title_height"), m_thLblY);
    DrawSegments(L10n::Tr("settings.display_mode"), m_displayModeLblY,
                 { L10n::Tr("settings.mode.grid"), L10n::Tr("settings.mode.list") },
                 m_displayModeSeg, app.displayMode, HIT_DISPMODE0);
    DrawSegments(L10n::Tr("settings.fill_direction"), m_fillDirLblY,
                 { L10n::Tr("settings.fill.horizontal"), L10n::Tr("settings.fill.vertical") },
                 m_fillDirSeg, app.fillDir, HIT_FILLDIR0);
    DrawSegments(L10n::Tr("settings.auto_fit"), m_autoFitLblY,
                 { L10n::Tr("settings.on"), L10n::Tr("settings.off") },
                 m_autoFitSeg, app.autoReflow ? 0 : 1, HIT_AUTOFIT0);
    DrawSegments(L10n::Tr("settings.collapse_style"), m_collapseLblY,
                 { L10n::Tr("settings.collapse.bar"), L10n::Tr("settings.collapse.icon") },
                 m_collapseSeg, app.collapseStyle, HIT_COLLAPSE0);
    DrawSegments(L10n::Tr("settings.title_icon"), m_titleIconLblY,
                 { L10n::Tr("settings.off"), L10n::Tr("settings.on") },
                 m_titleIconSeg, app.titleIcon ? 1 : 0, HIT_TITLEICON0);
    int iconSource = 0;
    if (app.iconPath[0]) {
        DWORD attrs = GetFileAttributesW(app.iconPath);
        iconSource = (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) ? 2 : 1;
    }
    DrawSegments(L10n::Tr("settings.icon_source"), m_iconPickLblY,
                 { L10n::Tr("settings.icon.folder"), L10n::Tr("settings.icon.file"),
                   L10n::Tr("settings.icon.folder_pick") },
                 m_iconPickSeg, iconSource, HIT_ICONPICK0);
    DrawSegments(L10n::Tr("settings.tile_snap"), m_tileSnapLblY,
                 { L10n::Tr("settings.on"), L10n::Tr("settings.off") },
                 m_tileSnapSeg, app.tileSnap ? 0 : 1, HIT_TILESNAP0);
    DrawTitleHeightStepper(app);
    DrawDeleteButton();
    DrawPresetsButton();

    m_render->EndDraw();
}

// Solid dark fill (deliberately NOT frosted glass: the fence owns that effect;
// a settings panel reads better opaque), rounded, bordered.
void SettingsPanel::DrawSurface() {
    auto* t = m_render->Target();
    float s = m_s;
    float W = (float)m_panelW, H = (float)m_panelH;
    float r = 12 * s;
    t->Clear(D2D1::ColorF(0, 0, 0, 0));
    D2D1_ROUNDED_RECT rr{ { 0, 0, W, H }, r, r };
    t->FillRoundedRectangle(rr, m_brTint.Get());
    t->FillRectangle(D2D1_RECT_F{ 0, 0, W, 1.5f * s }, m_brHighlight.Get());
    t->DrawRoundedRectangle(rr, m_brBorder.Get(), 1.0f);
    // Divider between the two columns.
    t->FillRectangle(D2D1_RECT_F{ 355 * s, 52 * s, 356 * s, H - 16 * s }, m_brBorder.Get());
}

void SettingsPanel::DrawHeader() {
    float s = m_s, W = (float)m_panelW;
    DrawString(L10n::Tr(m_fence ? "settings.title" : "settings.defaults_title"),
               RECT{ (LONG)(20 * s), 0, (LONG)(W - 40 * s), (LONG)(44 * s) },
               m_fmtHeader.Get(), m_brText.Get());
    if (m_hover == HIT_CLOSE) FillRounded(m_closeBtn, 6 * s, m_brHover.Get());
    DrawString(L"\u2715", m_closeBtn, m_fmtSymbol.Get(), m_brText2.Get());
}

void SettingsPanel::DrawFontList() {
    auto* t = m_render->Target();
    float s = m_s;
    DrawLabel(L10n::Tr("settings.font"), m_fontLblY);
    FillRounded(m_fontList, 6 * s, m_brField.Get());
    int total = (int)m_fonts.size();
    for (int row = 0; row < m_fontRows; row++) {
        int idx = m_fontScrollTop + row;
        if (idx >= total) break;
        float ry = (float)m_fontList.top + row * m_rowH;
        RECT rowR{ m_fontList.left + 2, (LONG)ry, m_fontList.right - 2, (LONG)(ry + m_rowH) };
        bool sel = (idx == m_selFont);
        bool hov = (m_hover == HIT_FONT_LIST && m_hoverRow == row);
        if (sel)      FillRounded(rowR, 5 * s, m_brSel.Get());
        else if (hov) FillRounded(rowR, 5 * s, m_brHover.Get());

        // WYSIWYG: render each font name in its own face
        ComPtr<IDWriteTextFormat> rf;
        m_render->WriteFactory()->CreateTextFormat(m_fonts[idx].c_str(), nullptr,
            DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 13 * s, L"en-US", &rf);
        if (rf) {
            rf->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            RECT tr{ rowR.left + 8, rowR.top, rowR.right - 8, rowR.bottom };
            t->DrawText(m_fonts[idx].c_str(), (UINT32)m_fonts[idx].size(), rf.Get(),
                        ToRectF(tr), sel ? m_brText.Get() : m_brText2.Get(),
                        D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }
    }
    // Scrollbar
    FillRounded(m_fontScroll, 3 * s, m_brField.Get());
    if (total > m_fontRows) {
        float trackH = (float)(m_fontScroll.bottom - m_fontScroll.top);
        float thumbH = (std::max)(20 * s, trackH * m_fontRows / total);
        float range = (float)(total - m_fontRows);
        float pos = range > 0 ? (m_fontScrollTop / range) : 0.0f;
        float thumbTop = (float)m_fontScroll.top + pos * (trackH - thumbH);
        RECT thumb{ m_fontScroll.left, (LONG)thumbTop, m_fontScroll.right, (LONG)(thumbTop + thumbH) };
        FillRounded(thumb, 3 * s, m_brTrack.Get());
    }
}

void SettingsPanel::DrawSlider(const RECT& rc, float t01) {
    auto* t = m_render->Target();
    float s = m_s;
    float cy = (rc.top + rc.bottom) / 2.0f;
    float x0 = rc.left + 9 * s, x1 = rc.right - 9 * s;
    D2D1_RECT_F track{ x0, cy - 2 * s, x1, cy + 2 * s };
    t->FillRoundedRectangle(D2D1_ROUNDED_RECT{ track, 2 * s, 2 * s }, m_brField.Get());
    float fx = x0 + clampf(t01, 0, 1) * (x1 - x0);
    D2D1_RECT_F filled{ x0, cy - 2 * s, fx, cy + 2 * s };
    t->FillRoundedRectangle(D2D1_ROUNDED_RECT{ filled, 2 * s, 2 * s }, m_brAccent.Get());
    t->FillEllipse(D2D1_ELLIPSE{ { fx, cy }, 9 * s, 9 * s }, m_brThumb.Get());
}

void SettingsPanel::DrawSliders(const FenceAppearance& app) {
    int sizeVal = (int)(app.fontSize / m_s + 0.5f);
    int opVal = (int)(app.bg[3] * 100 + 0.5f);

    DrawLabel(L10n::Tr("settings.font_size"), m_sizeLblY);
    DrawValueRight(std::to_wstring(sizeVal), m_sizeLblY);
    DrawSlider(m_sizeSlider, (sizeVal - 8) / (float)(24 - 8));

    DrawLabel(L10n::Tr("settings.opacity"), m_opLblY);
    DrawValueRight(std::to_wstring(opVal) + L"%", m_opLblY);
    DrawSlider(m_opSlider, (opVal - 30) / (float)(100 - 30));
}

// A preset swatch: filled, hover ring, and an accent ring on the current one.
void SettingsPanel::DrawSwatch(const RECT& rc, ID2D1Brush* fill, bool selected, int hitId) {
    auto* t = m_render->Target();
    float s = m_s;
    FillRounded(rc, 6 * s, fill);
    DrawHoverRing(rc, hitId);
    if (selected)
        t->DrawRoundedRectangle(D2D1_ROUNDED_RECT{ ToRectF(rc), 6 * s, 6 * s },
                                m_brSwatchRing.Get(), 2.0f * s);
}

// The "..." button after a swatch row (opens the system colour dialog).
void SettingsPanel::DrawMoreButton(const RECT& rc, int hitId) {
    FillRounded(rc, 6 * m_s, m_brField.Get());
    DrawHoverRing(rc, hitId);
    DrawString(L"\u2026", rc, m_fmtSymbol.Get(), m_brText2.Get());
}

void SettingsPanel::DrawColorSwatches(const FenceAppearance& app) {
    // Which preset (if any) matches the current colours.
    int panelSel = -1, titleSel = -1;
    for (int i = 0; i < 8; i++) {
        if (fabsf(app.bg[0] - kPanelColors[i][0]) < 0.01f &&
            fabsf(app.bg[1] - kPanelColors[i][1]) < 0.01f &&
            fabsf(app.bg[2] - kPanelColors[i][2]) < 0.01f) panelSel = i;
        if (fabsf(app.title[0] - kTitleColors[i][0]) < 0.01f &&
            fabsf(app.title[1] - kTitleColors[i][1]) < 0.01f &&
            fabsf(app.title[2] - kTitleColors[i][2]) < 0.01f) titleSel = i;
    }

    DrawLabel(L10n::Tr("settings.panel_color"), m_panelLblY);
    for (int i = 0; i < 8; i++)
        DrawSwatch(m_panelSwatch[i], m_brPanelSw[i].Get(), i == panelSel, HIT_PANEL_SWATCH0 + i);
    DrawMoreButton(m_panelMore, HIT_PANEL_MORE);

    DrawLabel(L10n::Tr("settings.title_color"), m_titleLblY);
    for (int i = 0; i < 8; i++)
        DrawSwatch(m_titleSwatch[i], m_brTitleSw[i].Get(), i == titleSel, HIT_TITLE_SWATCH0 + i);
    DrawMoreButton(m_titleMore, HIT_TITLE_MORE);
}

// A labelled segmented control (title alignment, display mode).
void SettingsPanel::DrawSegments(const wchar_t* caption, float labelY,
                                 std::initializer_list<const wchar_t*> labels,
                                 const RECT* rects, int active, int hitBase) {
    DrawLabel(caption, labelY, (float)rects[0].left);
    int i = 0;
    for (const wchar_t* label : labels) {
        bool isActive = (active == i);
        FillRounded(rects[i], 6 * m_s, isActive ? m_brAccent.Get() : m_brField.Get());
        if (!isActive) DrawHoverRing(rects[i], hitBase + i);
        DrawString(label, rects[i], m_fmtCenter.Get(), isActive ? m_brOnAccent.Get() : m_brText2.Get());
        i++;
    }
}

void SettingsPanel::DrawTitleHeightStepper(const FenceAppearance& app) {
    float s = m_s;
    FillRounded(m_thMinus, 6 * s, m_brField.Get());
    DrawHoverRing(m_thMinus, HIT_TITLEH_MINUS);
    DrawString(L"\u2212", m_thMinus, m_fmtSymbol.Get(), m_brText.Get());
    FillRounded(m_thPlus, 6 * s, m_brField.Get());
    DrawHoverRing(m_thPlus, HIT_TITLEH_PLUS);
    DrawString(L"+", m_thPlus, m_fmtSymbol.Get(), m_brText.Get());
    int value = (int)(app.titleH / s + 0.5f);
    RECT valueRect{ m_thMinus.right, m_thMinus.top, m_thPlus.left, m_thMinus.bottom };
    DrawString(std::to_wstring(value), valueRect, m_fmtCenter.Get(), m_brText.Get());
}

void SettingsPanel::DrawPresetsButton() {
    float s = m_s;
    D2D1_RECT_F sep{ m_presetsBtn.left * 1.0f, m_sepY, m_presetsBtn.right * 1.0f, m_sepY + 1 * s };
    m_render->Target()->FillRectangle(sep, m_brBorder.Get());
    FillRounded(m_presetsBtn, 8 * s, m_brField.Get());
    DrawHoverRing(m_presetsBtn, HIT_PRESETS);
    DrawString(std::wstring(L10n::Tr("settings.presets")) + L"  \u25BE", m_presetsBtn,
               m_fmtCenter.Get(), m_brText.Get());
}

void SettingsPanel::DrawDeleteButton() {
    if (!m_fence) return;
    float s = m_s;
    FillRounded(m_deleteBtn, 8 * s, m_brDanger.Get());
    if (m_hover == HIT_DELETE_FENCE) FillRounded(m_deleteBtn, 8 * s, m_brHover.Get());
    DrawString(L10n::Tr("settings.delete_fence"), m_deleteBtn, m_fmtCenter.Get(), m_brThumb.Get());
}
