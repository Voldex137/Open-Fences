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

static int CALLBACK EnumFontFamProc(const LOGFONTW* lf, const TEXTMETRICW*,
                                    DWORD, LPARAM lp) {
    auto* v = (std::vector<std::wstring>*)lp;
    if (v->size() >= 1024) return 0;            // bounded; stop enumerating
    if (lf->lfFaceName[0] == L'@') return 1;    // skip vertical (@) faces
    v->push_back(lf->lfFaceName);
    return 1;
}

void SettingsPanel::CreatePanelBrushes() {
    auto* t = m_render->Target();
    auto mk = [&](float r, float g, float b, float a, ID2D1SolidColorBrush** out) {
        t->CreateSolidColorBrush(D2D1::ColorF(r, g, b, a), out);
    };
    mk(0.125f, 0.125f, 0.14f, 1.0f, &m_brTint);        // solid surface (no translucency)
    mk(1, 1, 1, 0.08f, &m_brBorder);
    mk(1, 1, 1, 0.05f, &m_brHighlight);
    mk(1, 1, 1, 0.92f, &m_brText);
    mk(1, 1, 1, 0.55f, &m_brText2);
    mk(0.30f, 0.76f, 1.0f, 1.0f, &m_brAccent);         // #4CC2FF
    mk(1, 1, 1, 0.06f, &m_brHover);
    mk(0.30f, 0.76f, 1.0f, 0.22f, &m_brSel);
    mk(0.98f, 0.28f, 0.24f, 1.0f, &m_brDanger);
    mk(1, 1, 1, 0.35f, &m_brTrack);
    mk(1, 1, 1, 0.92f, &m_brThumb);
    mk(1, 1, 1, 0.05f, &m_brField);                    // input wells / buttons
    mk(0.30f, 0.76f, 1.0f, 0.90f, &m_brSwatchRing);
    // Preset color swatches: build once, reuse on every redraw.
    for (int i = 0; i < 8; i++) {
        mk(kPanelColors[i][0], kPanelColors[i][1], kPanelColors[i][2], 1.0f, &m_brPanelSw[i]);
        mk(kTitleColors[i][0], kTitleColors[i][1], kTitleColors[i][2], 1.0f, &m_brTitleSw[i]);
    }
}

void SettingsPanel::CreatePanelFormats() {
    auto* dw = m_render->WriteFactory();
    float s = m_s;
    auto mk = [&](float px, DWRITE_FONT_WEIGHT w, IDWriteTextFormat** out) {
        dw->CreateTextFormat(L"Segoe UI", nullptr, w, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, px, L"en-US", out);
    };
    mk(15 * s, DWRITE_FONT_WEIGHT_SEMI_BOLD, &m_fmtHeader);
    mk(13 * s, DWRITE_FONT_WEIGHT_NORMAL, &m_fmtLabel);
    mk(13 * s, DWRITE_FONT_WEIGHT_SEMI_BOLD, &m_fmtValue);
    mk(14 * s, DWRITE_FONT_WEIGHT_NORMAL, &m_fmtSymbol);
    mk(13 * s, DWRITE_FONT_WEIGHT_SEMI_BOLD, &m_fmtCenter);

    if (m_fmtHeader) m_fmtHeader->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    if (m_fmtLabel)  m_fmtLabel->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    if (m_fmtSymbol) {
        m_fmtSymbol->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        m_fmtSymbol->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    }
    if (m_fmtCenter) {
        m_fmtCenter->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        m_fmtCenter->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    }
    if (m_fmtValue) {
        m_fmtValue->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        m_fmtValue->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
    }
}

void SettingsPanel::LoadFonts() {
    HDC hdc = GetDC(nullptr);
    LOGFONTW lf = {};
    lf.lfCharSet = DEFAULT_CHARSET;
    EnumFontFamiliesExW(hdc, &lf, EnumFontFamProc, (LPARAM)&m_fonts, 0);
    ReleaseDC(nullptr, hdc);

    // Enumeration reports each charset variant; dedupe by face name.
    std::sort(m_fonts.begin(), m_fonts.end());
    m_fonts.erase(std::unique(m_fonts.begin(), m_fonts.end()), m_fonts.end());

    const auto& app = m_fence->GetRender().Appearance();
    for (int i = 0; i < (int)m_fonts.size(); i++) {
        if (lstrcmpiW(m_fonts[i].c_str(), app.fontName) == 0) { m_selFont = i; break; }
    }
    if (m_selFont >= 0) {   // bring the current font into view
        m_fontScrollTop = clampi(m_selFont, 0,
            (std::max)(0, (int)m_fonts.size() - m_fontRows));
    }
}
