#include "render/render.h"
#include "shell/desktop_host.h"
#include <commctrl.h>
#include <algorithm>

void RenderContext::ScaleAppearance() {
    // FenceAppearance defaults are designed at 96 DPI; grow them to physical
    // pixels so fences keep the same apparent size at any display scaling.
    float s = m_dpiX / 96.0f;
    m_app.r        *= s;
    m_app.titleH   *= s;
    m_app.searchH  *= s;
    m_app.fontSize *= s;
    m_app.iconSize *= s;
    m_app.cellW    *= s;
    m_app.cellH    *= s;
    m_app.imgSize  *= s;
}

int QueryDesktopIconSizePx() {
    // Fences should read like the desktop itself, so this asks the desktop's
    // own listview how big its icons currently are. Handle-based APIs do not
    // cross the process boundary into explorer (its HIMAGELIST is unusable
    // here), but LVM_GETITEMRECT can be made to: the RECT it fills lives in
    // *explorer's* address space, allocated by us. The LVIR_ICON height is
    // the drawn glyph size plus a fixed 4px of vertical padding — measured
    // live: medium icons (48px glyphs at 150% DPI) report 52, and Ctrl+wheel
    // zoomed to the jumbo plateau (256px) reports 259. Explorer coordinates
    // are physical pixels (Progman runs Per-Monitor-V2 aware), matching this
    // render target's 1-DIP-1-pixel layout.
    HWND lv = Desktop::FindIconListHost();
    wchar_t cls[64] = {};
    if (!lv || !GetClassNameW(lv, cls, 64) || wcscmp(cls, L"SysListView32"))
        lv = nullptr;   // DesktopHost falls back to Progman when the list is gone
    if (lv) {
        DWORD pid = 0;
        GetWindowThreadProcessId(lv, &pid);
        HANDLE proc = OpenProcess(PROCESS_VM_OPERATION | PROCESS_VM_READ |
                                  PROCESS_VM_WRITE, FALSE, pid);
        if (proc) {
            int height = 0;
            void* mem = VirtualAllocEx(proc, nullptr, sizeof(RECT),
                                       MEM_COMMIT, PAGE_READWRITE);
            if (mem) {
                RECT in = {};
                in.left = LVIR_ICON;   // LVM_GETITEMRECT reads the kind here
                if (WriteProcessMemory(proc, mem, &in, sizeof(RECT), nullptr)) {
                    DWORD_PTR res = 0;
                    if (SendMessageTimeoutW(lv, LVM_GETITEMRECT, 0, (LPARAM)mem,
                                            SMTO_ABORTIFHUNG, 250, &res) && res) {
                        RECT back = {};
                        if (ReadProcessMemory(proc, mem, &back, sizeof(RECT), nullptr))
                            height = (int)(back.bottom - back.top);
                    }
                }
                VirtualFreeEx(proc, mem, 0, MEM_RELEASE);
            }
            CloseHandle(proc);
            if (height >= 20) return height - 4;
        }
    }
    // Live read unavailable (explorer restarting, list empty, access
    // denied): fall back to the persisted IconSize. Explorer writes it
    // lazily, so it lags a wheel-zoom session — but it is still right after
    // View-menu changes and boots, which is when this path runs.
    DWORD logical = 0, cb = sizeof(logical);
    if (RegGetValueW(HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\Shell\\Bags\\1\\Desktop",
            L"IconSize", RRF_RT_DWORD, nullptr, &logical, &cb) == ERROR_SUCCESS
        && logical >= 16 && logical <= 256) {
        HDC hdc = GetDC(nullptr);
        float dpi = (float)GetDeviceCaps(hdc, LOGPIXELSX);
        ReleaseDC(nullptr, hdc);
        return (int)(logical * (dpi / 96.0f) + 0.5f);
    }
    return 0;
}

bool RenderContext::AdoptDesktopIconSize(int physicalPx) {
    float g = (float)physicalPx;
    if (g < 16.0f || g > 256.0f) return false;   // implausible — keep as-is
    float k = g / m_app.imgSize;
    if (k > 0.99f && k < 1.01f) return false;
    m_app.imgSize = g;
    m_app.cellW *= k;   // cells follow the glyph so spacing stays proportional
    m_app.cellH *= k;
    // Cached glyphs were picked (and sized) for the old glyph box; drop them
    // so the next redraw re-selects renditions for the new size.
    m_iconCache.clear();
    return true;
}

bool RenderContext::CreateBrushes() {
    auto b = [&](const float* c, ID2D1SolidColorBrush** out) {
        D2D1_COLOR_F color{c[0], c[1], c[2], c[3]};
        m_d2dTarget->CreateSolidColorBrush(color, out);
    };
    b(m_app.bg,       &m_bgBrush);
    b(m_app.title,    &m_titleBrush);
    b(m_app.sep,      &m_sepBrush);
    b(m_app.text,     &m_textBrush);
    b(m_app.iconText, &m_iconTextBrush);
    b(m_app.accent,   &m_accentBrush);
    // Rubber-band marquee: accent rgb like the selection plate, light fill +
    // stronger edge. Created once here (never per frame) because a live band
    // repaints on every mouse move.
    m_d2dTarget->CreateSolidColorBrush(
        D2D1::ColorF(m_app.accent[0], m_app.accent[1], m_app.accent[2], 0.26f),
        &m_marqueeFillBrush);
    m_d2dTarget->CreateSolidColorBrush(
        D2D1::ColorF(m_app.accent[0], m_app.accent[1], m_app.accent[2], 0.75f),
        &m_marqueeEdgeBrush);

    // Border gradient: brighter at the top edge (key-light direction),
    // dimmer towards the bottom — the flat 1px white ring read as "dead".
    // RGB follows the shared border color; only the alphas are styled.
    {
        const float kBorderAlphaTop = 0.16f;
        const float kBorderAlphaBottom = 0.05f;
        D2D1_GRADIENT_STOP stops[2] = {
            { 0.0f, { m_app.border[0], m_app.border[1], m_app.border[2], kBorderAlphaTop } },
            { 1.0f, { m_app.border[0], m_app.border[1], m_app.border[2], kBorderAlphaBottom } },
        };
        ComPtr<ID2D1GradientStopCollection> stopsCol;
        m_d2dTarget->CreateGradientStopCollection(stops, 2,
            D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, &stopsCol);
        if (stopsCol) {
            D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES gp = {};
            gp.startPoint = { 0.0f, 0.0f };
            // m_height is set before every CreateBrushes call (ctor and
            // Resize both order it so); guard the degenerate case anyway.
            gp.endPoint = { 0.0f, (float)(std::max)(1u, m_height) };
            m_d2dTarget->CreateLinearGradientBrush(gp, stopsCol.Get(), &m_borderGradBrush);
        }
    }

    // Top-edge highlight strip. Cached here because a live marquee / drag
    // repaints every mouse move — the redraw path must not allocate.
    m_d2dTarget->CreateSolidColorBrush(
        D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.07f), &m_topHiBrush);

    // Collapse chevron. Idle color is deliberately dimmer than the title
    // text (it is a control, not content); DrawFence brightens it on hover
    // and dims it on press via SetColor — no per-frame brush allocation.
    m_d2dTarget->CreateSolidColorBrush(
        D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.62f), &m_chevronBrush);
    // Caption-button style hover/press plate behind the chevron; alpha is
    // tuned per state (hover vs press) with SetColor at draw time.
    m_d2dTarget->CreateSolidColorBrush(
        D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.08f), &m_chevronPlate);
    return true;
}

void RenderContext::BuildChevronPath() {
    // ONE open figure with two segments: the apex is then a true round JOIN.
    // (The old implementation drew two separate lines — their round CAPS
    // overlapped at the apex and produced a visible bulge.) Points up;
    // DrawFence mirrors it about the title-bar midline for the collapsed
    // state. Geometry lives in layout pixels and follows the title-bar
    // height, so it is rebuilt with the styles and on Resize.
    m_chevronPath.Reset();
    if (!m_d2dFactory) return;
    float cx = (float)m_width - m_app.titleH * 0.5f;   // arrow box center
    float cy = m_app.titleH * 0.5f;
    float a  = m_app.titleH * 0.16f;   // half width; 90° aperture (Fluent)
    ComPtr<ID2D1PathGeometry> path;
    if (FAILED(m_d2dFactory->CreatePathGeometry(&path)) || !path) return;
    ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(path->Open(&sink)) || !sink) return;
    sink->BeginFigure({cx - a, cy + a * 0.55f}, D2D1_FIGURE_BEGIN_HOLLOW);
    sink->AddLine({cx, cy - a * 0.45f});
    sink->AddLine({cx + a, cy + a * 0.55f});
    sink->EndFigure(D2D1_FIGURE_END_OPEN);
    sink->Close();
    m_chevronPath = path;
}

void RenderContext::BuildTitleBarPaths() {
    m_titleBarPath.Reset();
    m_topHiPath.Reset();
    if (!m_d2dFactory) return;
    const float w = (float)m_width, th = m_app.titleH;
    // Match D2D's rounded-rect clamp: a radius larger than half a side
    // collapses, so the window's real top arc is min(r, w/2, h/2).
    const float effR = (std::min)(m_app.r,
        (std::min)((std::min)(w, (float)m_height) * 0.5f, th));
    if (effR <= 0.5f) return;   // degenerate — caller falls back to plain rects

    ComPtr<ID2D1PathGeometry> title;
    if (FAILED(m_d2dFactory->CreatePathGeometry(&title)) || !title) return;
    ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(title->Open(&sink)) || !sink) return;
    sink->BeginFigure({0.0f, effR}, D2D1_FIGURE_BEGIN_FILLED);
    D2D1_ARC_SEGMENT arc = {};
    arc.size = {effR, effR};
    arc.rotationAngle = 0.0f;
    arc.sweepDirection = D2D1_SWEEP_DIRECTION_CLOCKWISE;
    arc.arcSize = D2D1_ARC_SIZE_SMALL;
    arc.point = {effR, 0.0f};      // top-left arc: left edge → top edge
    sink->AddArc(arc);
    sink->AddLine({w - effR, 0.0f});
    arc.point = {w, effR};         // top-right arc
    sink->AddArc(arc);
    sink->AddLine({w, th});
    sink->AddLine({0.0f, th});
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    sink->Close();
    m_titleBarPath = title;

    // Highlight strip clipped to the rounded shape (rect ∩ title path), so
    // it cannot poke into the transparent corner cutouts.
    ComPtr<ID2D1RectangleGeometry> stripRect;
    if (FAILED(m_d2dFactory->CreateRectangleGeometry(
            D2D1_RECT_F{0.0f, 0.0f, w, 1.5f}, &stripRect)) || !stripRect) return;
    ComPtr<ID2D1PathGeometry> strip;
    if (FAILED(m_d2dFactory->CreatePathGeometry(&strip)) || !strip) return;
    ComPtr<ID2D1GeometrySink> ssink;
    if (FAILED(strip->Open(&ssink)) || !ssink) return;
    if (FAILED(m_titleBarPath->CombineWithGeometry(stripRect.Get(),
            D2D1_COMBINE_MODE_INTERSECT, nullptr, 0.25f, ssink.Get()))) {
        ssink->Close();
        return;
    }
    ssink->Close();
    m_topHiPath = strip;
}

bool RenderContext::CreateTextFormats() {
    m_dwFactory->CreateTextFormat(m_app.fontName, nullptr,
        DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, m_app.fontSize, L"en-US", &m_titleFormat);
    m_titleFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    // Vertical centering + configurable horizontal alignment (left/center/right)
    m_titleFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    DWRITE_TEXT_ALIGNMENT ta = DWRITE_TEXT_ALIGNMENT_LEADING;
    if (m_app.titleAlign == 1)      ta = DWRITE_TEXT_ALIGNMENT_CENTER;
    else if (m_app.titleAlign == 2) ta = DWRITE_TEXT_ALIGNMENT_TRAILING;
    m_titleFormat->SetTextAlignment(ta);

    m_dwFactory->CreateTextFormat(m_app.fontName, nullptr,
        DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, m_app.iconSize, L"en-US", &m_iconFormat);
    m_iconFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    m_iconFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);  // center labels under icons

    // Ellipsis for long icon names
    ComPtr<IDWriteInlineObject> ellipsis;
    m_dwFactory->CreateEllipsisTrimmingSign(m_iconFormat.Get(), &ellipsis);
    DWRITE_TRIMMING trim = { DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
    m_iconFormat->SetTrimming(&trim, ellipsis.Get());

    // List-mode format: left-aligned, vertically centered, same font/size as icons
    m_dwFactory->CreateTextFormat(m_app.fontName, nullptr,
        DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, m_app.iconSize, L"en-US", &m_listFormat);
    if (m_listFormat) {
        m_listFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        m_listFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        m_listFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        ComPtr<IDWriteInlineObject> ellipsis2;
        m_dwFactory->CreateEllipsisTrimmingSign(m_listFormat.Get(), &ellipsis2);
        m_listFormat->SetTrimming(&trim, ellipsis2.Get());
    }

    return true;
}

void RenderContext::RebuildStyles() {
    // Release only the brushes and text formats, then rebuild them from the
    // current FenceAppearance. Deliberately does NOT touch the render target,
    // WIC bitmap, DIB, icon cache or backdrop — that keeps this cheap enough
    // to run on every slider tick for live preview.
    m_bgBrush.Reset(); m_titleBrush.Reset(); m_sepBrush.Reset();
    m_textBrush.Reset(); m_iconTextBrush.Reset(); m_accentBrush.Reset();
    m_borderGradBrush.Reset(); m_topHiBrush.Reset();
    m_marqueeFillBrush.Reset(); m_marqueeEdgeBrush.Reset();
    m_chevronBrush.Reset(); m_chevronPlate.Reset();
    m_titleFormat.Reset(); m_iconFormat.Reset(); m_listFormat.Reset();
    CreateBrushes();
    CreateTextFormats();
    BuildChevronPath();   // geometry follows the (possibly changed) titleH
    BuildTitleBarPaths();
    // Label metrics depend on the icon text format — stale after a rebuild.
    m_selLabelPath.clear();
}
