#include "render/render.h"
#include "fence/fence_window.h"   // FenceWindow::DesktopHost (desktop tree discovery)
#include "render/icon_extract.h"
#include <shellapi.h>
#include <shlobj.h>
#include <commctrl.h>       // LVM_GETITEMRECT / LVIR_ICON
#include <commoncontrols.h>
#include <algorithm>
#include <cstring>
#include <cstdint>
#include <wchar.h>

RenderContext::RenderContext(UINT w, UINT h) : m_width(w), m_height(h) {
    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, IID_PPV_ARGS(&m_d2dFactory));
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&m_wicFactory));
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
        (IUnknown**)&m_dwFactory);

    m_wicFactory->CreateBitmap(w, h, GUID_WICPixelFormat32bppPBGRA,
        WICBitmapCacheOnLoad, &m_wicBitmap);

    // Real screen DPI — only used to scale the appearance metrics below.
    // The render target itself runs at 96 DPI so that 1 DIP == 1 physical
    // pixel and all layout math (which is done in pixels) stays exact.
    HDC sdpi = GetDC(nullptr);
    m_dpiX = (float)GetDeviceCaps(sdpi, LOGPIXELSX);
    m_dpiY = (float)GetDeviceCaps(sdpi, LOGPIXELSY);
    ReleaseDC(nullptr, sdpi);
    ScaleAppearance();
    AdoptDesktopIconSize(QueryDesktopIconSizePx());   // 0 = keep the design size

    D2D1_RENDER_TARGET_PROPERTIES rt = {};
    rt.type = D2D1_RENDER_TARGET_TYPE_DEFAULT;
    rt.pixelFormat = { DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED };
    rt.dpiX = 96.0f; rt.dpiY = 96.0f;
    rt.usage = D2D1_RENDER_TARGET_USAGE_NONE;
    rt.minLevel = D2D1_FEATURE_LEVEL_DEFAULT;
    m_d2dFactory->CreateWicBitmapRenderTarget(m_wicBitmap.Get(), &rt, &m_d2dTarget);

    // Rounded stroke for the collapse chevron. Factory-level and independent
    // of the render target, so it is created exactly once per RenderContext —
    // the redraw path must not allocate.
    D2D1_STROKE_STYLE_PROPERTIES chevronSp = {};
    chevronSp.startCap = D2D1_CAP_STYLE_ROUND;
    chevronSp.endCap   = D2D1_CAP_STYLE_ROUND;
    chevronSp.lineJoin = D2D1_LINE_JOIN_ROUND;
    m_d2dFactory->CreateStrokeStyle(chevronSp, nullptr, 0, &m_chevronStroke);

    CreateBrushes();
    CreateTextFormats();
    BuildChevronPath();
    BuildTitleBarPaths();
    CreateDibDC();
}

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
    HWND lv = FenceWindow::DesktopHost();
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

RenderContext::~RenderContext() {
    m_iconCache.clear();
    if (m_memDC && m_oldBmp) {
        SelectObject(m_memDC, m_oldBmp);
        DeleteObject(m_dib);
        DeleteDC(m_memDC);
    }
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

bool RenderContext::CreateDibDC() {
    HDC screenDC = GetDC(nullptr);
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = m_width;
    bmi.bmiHeader.biHeight = -(int)m_height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    m_dib = CreateDIBSection(screenDC, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    m_memDC = CreateCompatibleDC(screenDC);
    m_oldBmp = SelectObject(m_memDC, m_dib);
    ReleaseDC(nullptr, screenDC);
    return m_dib && m_memDC;
}

// ── Icon loading ──

ComPtr<ID2D1Bitmap> RenderContext::MakeBitmapFromPixels(const BYTE* bgra, int w, int h) {
    if (w <= 0 || h <= 0) return nullptr;
    ComPtr<IWICBitmap> wicBmp;
    HRESULT hr = m_wicFactory->CreateBitmapFromMemory(
        (UINT)w, (UINT)h, GUID_WICPixelFormat32bppPBGRA,
        (UINT)w * 4, (UINT)((size_t)w * h * 4), const_cast<BYTE*>(bgra), &wicBmp);
    if (FAILED(hr) || !wicBmp) return nullptr;
    // Render target runs at 96 DPI → WIC bitmap DIP size equals its pixel
    // size, so layout math in pixels stays exact.
    wicBmp->SetResolution(96.0f, 96.0f);
    ComPtr<ID2D1Bitmap> d2dIcon;
    if (FAILED(m_d2dTarget->CreateBitmapFromWicBitmap(wicBmp.Get(), nullptr, &d2dIcon)))
        return nullptr;
    return d2dIcon;
}

ComPtr<ID2D1Bitmap> RenderContext::TryLoadIcon(int iconIndex, int imgList,
                                                bool cropPadded) {
    ComPtr<IImageList> spImageList;
    if (FAILED(SHGetImageList(imgList, IID_PPV_ARGS(&spImageList))) || !spImageList)
        return nullptr;
    HICON hIcon = nullptr;
    if (FAILED(spImageList->GetIcon(iconIndex, ILD_TRANSPARENT, &hIcon)) || !hIcon)
        return nullptr;

    std::vector<BYTE> px;
    int w = 0, h = 0;
    bool ok = ExtractIconPixels(hIcon, px, w, h);
    DestroyIcon(hIcon);
    if (!ok || px.empty()) return nullptr;

    // Normally the canvas is kept intact: its art-to-canvas ratio is the
    // shell's own composition, and scaling the whole bitmap reproduces the
    // exact proportions Explorer shows. The jumbo list is the exception — it
    // pads some entries with enormous margins (tiny art on a huge canvas,
    // seen for types without real high-res art), so when it is the source
    // the glyph is cropped down to its opaque bounds.
    if (cropPadded) {
        int bx0, by0, bx1, by1;
        if (TightBounds(px.data(), w, h, bx0, by0, bx1, by1)) {
            int bw = bx1 - bx0 + 1, bh = by1 - by0 + 1;
            if (bw * 10 < w * 9 || bh * 10 < h * 9) {
                std::vector<BYTE> crop((size_t)bw * bh * 4);
                for (int y = 0; y < bh; y++)
                    memcpy(crop.data() + (size_t)y * bw * 4,
                           px.data() + ((size_t)(by0 + y) * w + bx0) * 4,
                           (size_t)bw * 4);
                px = std::move(crop);
                w = bw; h = bh;
            }
        }
    }

    return MakeBitmapFromPixels(px.data(), w, h);
}

ID2D1Bitmap* RenderContext::LoadFileIcon(const std::wstring& filePath) {
    auto it = m_iconCache.find(filePath);
    if (it != m_iconCache.end()) return it->second.Get();

    ComPtr<ID2D1Bitmap> d2dIcon;

    SHFILEINFOW shfi = {};
    // Namespace paths (::{CLSID}) need PIDL-based resolution;
    // SHGetFileInfoW on a raw parse string does not always resolve the icon.
    bool isNamespacePath = filePath.size() > 2 && filePath[0] == L':' && filePath[1] == L':';
    bool gotIcon = false;
    if (isNamespacePath) {
        PIDLIST_ABSOLUTE pidl = nullptr;
        if (SUCCEEDED(SHParseDisplayName(filePath.c_str(), nullptr, &pidl, 0, nullptr)) && pidl) {
            gotIcon = SHGetFileInfoW((LPCWSTR)pidl, 0, &shfi, sizeof(shfi),
                                     SHGFI_PIDL | SHGFI_SYSICONINDEX) && shfi.iIcon != -1;
            CoTaskMemFree(pidl);
        }
    } else {
        gotIcon = SHGetFileInfoW(filePath.c_str(), 0, &shfi, sizeof(shfi),
                                 SHGFI_SYSICONINDEX) && shfi.iIcon != -1;
    }
    if (gotIcon) {
        // Pick the smallest image list whose canvas still covers the glyph
        // box, then draw the whole bitmap into the box: the shell keeps the
        // art-to-canvas ratio consistent across the LARGE/EXTRALARGE lists,
        // so an uncropped bitmap scaled to the desktop icon size reproduces
        // the desktop's own rendering (minimal scaling keeps it crisp). The
        // jumbo list is only reached for very large glyphs and is cropped by
        // TryLoadIcon, guarding its disproportionately padded entries.
        static const int lists[3] = { SHIL_LARGE, SHIL_EXTRALARGE, SHIL_JUMBO };
        float need = m_app.imgSize;
        for (int L : lists) {
            ComPtr<ID2D1Bitmap> bmp = TryLoadIcon(shfi.iIcon, L, L == SHIL_JUMBO);
            if (!bmp) continue;
            d2dIcon = bmp;
            D2D1_SIZE_F sz = bmp->GetSize();
            if ((std::max)(sz.width, sz.height) + 0.5f >= need) break;
        }
    }

    // Cache failures too: a path that cannot resolve an icon must not
    // re-query the shell on every redraw (redraws run at drag frequency).
    m_iconCache[filePath] = d2dIcon;
    return d2dIcon.Get();
}

// ── Frosted-glass backdrop ──
//
// The DWM acrylic blur (SetWindowCompositionAttribute) is incompatible with
// WS_EX_LAYERED + UpdateLayeredWindow, so we approximate frosted glass here:
// FenceWindow captures the desktop behind the window, these helpers blur the
// pixels, and DrawFence paints them under the translucent panel tint.
//
// Pipeline (Acrylic-style, downsample → blur → upsample): two 2×2 box
// downsamples shrink the capture to ¼ size; the separable sliding-window
// box filter (2 passes per axis, r = 5) then runs on the small image —
// cheap, and effectively ~4× wider than the same radius at full
// resolution; saturation is boosted and a fixed grain baked in (both kill
// the flat color bands a box blur leaves behind). The small texture is
// uploaded through WIC and stretched back to the capture size by the
// bitmap brush's transform (the brush samples 1:1 and would NOT stretch on
// its own), with linear interpolation supplying the final smoothing.

static inline int ClampIdx(int v, int hi) {
    return v < 0 ? 0 : (v > hi ? hi : v);
}

static void BlurRows(BYTE* dst, const BYTE* src, int w, int h, int r) {
    const int div = 2 * r + 1;
    for (int y = 0; y < h; y++) {
        const BYTE* row = src + (size_t)y * w * 4;
        BYTE* out = dst + (size_t)y * w * 4;
        int s0 = 0, s1 = 0, s2 = 0;
        for (int i = -r; i <= r; i++) {
            const BYTE* p = row + (size_t)ClampIdx(i, w - 1) * 4;
            s0 += p[0]; s1 += p[1]; s2 += p[2];
        }
        for (int x = 0; x < w; x++) {
            out[x * 4 + 0] = (BYTE)(s0 / div);
            out[x * 4 + 1] = (BYTE)(s1 / div);
            out[x * 4 + 2] = (BYTE)(s2 / div);
            out[x * 4 + 3] = 255;  // DIB alpha byte is undefined; force opaque
            const BYTE* pa = row + (size_t)ClampIdx(x + r + 1, w - 1) * 4;
            const BYTE* ps = row + (size_t)ClampIdx(x - r, w - 1) * 4;
            s0 += pa[0] - ps[0]; s1 += pa[1] - ps[1]; s2 += pa[2] - ps[2];
        }
    }
}

static void BlurCols(BYTE* dst, const BYTE* src, int w, int h, int r) {
    const int div = 2 * r + 1;
    for (int x = 0; x < w; x++) {
        int s0 = 0, s1 = 0, s2 = 0;
        for (int i = -r; i <= r; i++) {
            const BYTE* p = src + ((size_t)ClampIdx(i, h - 1) * w + x) * 4;
            s0 += p[0]; s1 += p[1]; s2 += p[2];
        }
        for (int y = 0; y < h; y++) {
            BYTE* o = dst + ((size_t)y * w + x) * 4;
            o[0] = (BYTE)(s0 / div);
            o[1] = (BYTE)(s1 / div);
            o[2] = (BYTE)(s2 / div);
            o[3] = 255;
            const BYTE* pa = src + ((size_t)ClampIdx(y + r + 1, h - 1) * w + x) * 4;
            const BYTE* ps = src + ((size_t)ClampIdx(y - r, h - 1) * w + x) * 4;
            s0 += pa[0] - ps[0]; s1 += pa[1] - ps[1]; s2 += pa[2] - ps[2];
        }
    }
}

// 2×2 box-average downsample: each output pixel is the mean of a 2×2 block
// of source pixels. Requires w,h >= 2 (callers guard on >= 8).
static void HalfSizeBox(BYTE* dst, const BYTE* src, int w, int h) {
    const int dw = w / 2, dh = h / 2;
    for (int y = 0; y < dh; y++) {
        const BYTE* r0 = src + (size_t)(2 * y) * w * 4;
        const BYTE* r1 = r0 + (size_t)w * 4;
        BYTE* out = dst + (size_t)y * dw * 4;
        for (int x = 0; x < dw; x++) {
            const BYTE* a = r0 + (size_t)(2 * x) * 4;
            const BYTE* b = r1 + (size_t)(2 * x) * 4;
            for (int c = 0; c < 3; c++)
                out[x * 4 + c] = (BYTE)((a[c] + a[c + 4] + b[c] + b[c + 4] + 2) / 4);
            out[x * 4 + 3] = 255;   // alpha byte is unused; force opaque
        }
    }
}

// Acrylic saturation boost: pull each channel away from the pixel's luma
// (1.0 = unchanged, 1.25 = +25 %). Bytes are in B,G,R order.
static void SaturatePx(BYTE* px, int w, int h, float amount) {
    const size_t n = (size_t)w * h;
    for (size_t i = 0; i < n; i++) {
        BYTE* p = px + i * 4;
        // Cheap integer-weighted luma: (2R + 5G + B) / 8
        const float luma = (2.0f * p[2] + 5.0f * p[1] + p[0]) / 8.0f;
        for (int c = 0; c < 3; c++) {
            int v = (int)(luma + (p[c] - luma) * amount + 0.5f);
            p[c] = (BYTE)(v < 0 ? 0 : (v > 255 ? 255 : v));
        }
    }
}

// Fixed-seed xorshift32 grain, baked into the blurred backdrop. A box blur
// leaves flat color bands in smooth regions (sky, gradient wallpapers); a
// few levels of noise dissolve them the way Acrylic's noise texture does.
// The seed is a constant so the grain is part of the style — it must not
// crawl between re-captures. Applied at ¼ resolution, the grain grows to
// ~4 px blobs on screen.
static void ApplyNoise(BYTE* px, int w, int h) {
    uint32_t s = 0x2545F491u;
    const size_t n = (size_t)w * h;
    for (size_t i = 0; i < n; i++) {
        BYTE* p = px + i * 4;
        for (int c = 0; c < 3; c++) {
            s ^= s << 13; s ^= s >> 17; s ^= s << 5;
            int v = p[c] + (int)(s & 7) - 4;   // −4 … +3 levels
            p[c] = (BYTE)(v < 0 ? 0 : (v > 255 ? 255 : v));
        }
    }
}

void RenderContext::SetBackdrop(const BYTE* bgrx, int w, int h) {
    if (!bgrx || w <= 0 || h <= 0) return;

    std::vector<BYTE> px((size_t)w * h * 4);
    memcpy(px.data(), bgrx, px.size());

    // Downsample twice (2×2 box average per level) to ¼ size before
    // blurring. Each level wants >= 8 px in both dimensions to keep the
    // following blur meaningful — collapsed fences are only ~34 px tall,
    // so they still pass one or two levels.
    int cw = w, ch = h;
    for (int level = 0; level < 2; level++) {
        if (cw < 8 || ch < 8) break;
        // (Not "small": rpcndr.h #defines that token to `char`.)
        std::vector<BYTE> down((size_t)(cw / 2) * (ch / 2) * 4);
        HalfSizeBox(down.data(), px.data(), cw, ch);
        px = std::move(down);
        cw /= 2; ch /= 2;
    }

    // Blur on the small image: 2 passes per axis approximate a Gaussian.
    std::vector<BYTE> tmp(px.size());
    const int r = 5;
    for (int pass = 0; pass < 2; pass++) {
        BlurRows(tmp.data(), px.data(), cw, ch, r);
        BlurCols(px.data(), tmp.data(), cw, ch, r);
    }

    // Acrylic finishing passes on the blurred image: +25 % saturation so
    // the glass reads colorful instead of gray, then a fixed-seed grain
    // that breaks up the banding a box blur leaves in flat regions.
    SaturatePx(px.data(), cw, ch, 1.25f);
    ApplyNoise(px.data(), cw, ch);

    // Upload the SMALL image through WIC → D2D bitmap. The render target
    // runs at 96 DPI, so bitmap pixels map 1:1 to window pixels.
    ComPtr<IWICBitmap> wb;
    HRESULT hr = m_wicFactory->CreateBitmapFromMemory(
        cw, ch, GUID_WICPixelFormat32bppBGRA,
        (UINT)cw * 4, (UINT)px.size(), px.data(), &wb);
    if (FAILED(hr) || !wb) return;
    wb->SetResolution(96.0f, 96.0f);

    ComPtr<ID2D1Bitmap> bmp;
    if (FAILED(m_d2dTarget->CreateBitmapFromWicBitmap(wb.Get(), nullptr, &bmp)) || !bmp)
        return;

    m_backdropBrush.Reset();
    m_backdropBmp = bmp;

    D2D1_BITMAP_BRUSH_PROPERTIES bp = {};
    bp.extendModeX = D2D1_EXTEND_MODE_CLAMP;
    bp.extendModeY = D2D1_EXTEND_MODE_CLAMP;
    bp.interpolationMode = D2D1_BITMAP_INTERPOLATION_MODE_LINEAR;
    m_d2dTarget->CreateBitmapBrush(m_backdropBmp.Get(), bp, &m_backdropBrush);

    // ID2D1BitmapBrush samples source pixels 1:1 — it does NOT stretch the
    // bitmap across the filled rect. Scale the ¼-size texture back up to
    // the capture's original size; the LINEAR interpolation set above does
    // the smoothing on the way up.
    if (m_backdropBrush)
        m_backdropBrush->SetTransform(
            D2D1::Matrix3x2F::Scale((float)w / cw, (float)h / ch));
}

bool RenderContext::Resize(UINT w, UINT h) {
    m_iconCache.clear();
    // Backdrop belongs to the old render target / old size; FenceWindow
    // re-captures it right after the resize settles.
    m_backdropBrush.Reset();
    m_backdropBmp.Reset();
    m_bgBrush.Reset(); m_titleBrush.Reset(); m_sepBrush.Reset();
    m_textBrush.Reset(); m_iconTextBrush.Reset(); m_accentBrush.Reset();
    m_borderGradBrush.Reset(); m_topHiBrush.Reset();
    m_marqueeFillBrush.Reset(); m_marqueeEdgeBrush.Reset();
    m_chevronBrush.Reset(); m_chevronPlate.Reset();
    m_titleFormat.Reset(); m_iconFormat.Reset(); m_listFormat.Reset();
    m_d2dTarget.Reset();
    m_wicBitmap.Reset();

    m_width = w; m_height = h;
    m_wicFactory->CreateBitmap(w, h, GUID_WICPixelFormat32bppPBGRA,
        WICBitmapCacheOnLoad, &m_wicBitmap);

    D2D1_RENDER_TARGET_PROPERTIES rt = {};
    rt.type = D2D1_RENDER_TARGET_TYPE_DEFAULT;
    rt.pixelFormat = { DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED };
    rt.dpiX = 96.0f; rt.dpiY = 96.0f;   // 1 DIP == 1 physical pixel (see ctor)
    rt.usage = D2D1_RENDER_TARGET_USAGE_NONE;
    rt.minLevel = D2D1_FEATURE_LEVEL_DEFAULT;
    m_d2dFactory->CreateWicBitmapRenderTarget(m_wicBitmap.Get(), &rt, &m_d2dTarget);
    RebuildStyles();   // brushes + text formats against the fresh target

    if (m_memDC) { SelectObject(m_memDC, m_oldBmp); DeleteObject(m_dib); DeleteDC(m_memDC); }
    CreateDibDC();
    return true;
}

bool RenderContext::CopyWicToDib() {
    ComPtr<IWICBitmapLock> lock;
    m_wicBitmap->Lock(nullptr, WICBitmapLockRead, &lock);
    UINT bufSize = 0; BYTE* src = nullptr;
    lock->GetDataPointer(&bufSize, &src);

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = m_width;
    bmi.bmiHeader.biHeight = -(int)m_height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    SetDIBits(m_memDC, m_dib, 0, m_height, src, &bmi, DIB_RGB_COLORS);
    return true;
}

bool RenderContext::Present(HWND hwnd, int x, int y) {
    if (!CopyWicToDib()) return false;
    HDC screenDC = GetDC(nullptr);
    BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    POINT ptDst = { x, y }, ptSrc = { 0, 0 };
    // UpdateLayeredWindow positions a WS_CHILD in its parent's client
    // coordinates (it routes through SetWindowPos); callers pass screen
    // coordinates, so translate when the window is a child. On a display
    // whose virtual origin is (0,0) the two spaces coincide — that is the
    // only reason this ever worked single-monitor.
    if (GetWindowLongW(hwnd, GWL_STYLE) & WS_CHILD) {
        HWND parent = GetParent(hwnd);
        if (parent) ScreenToClient(parent, &ptDst);
    }
    SIZE size = { (int)m_width, (int)m_height };
    UpdateLayeredWindow(hwnd, screenDC, &ptDst, &size, m_memDC, &ptSrc, 0, &blend, ULW_ALPHA);
    ReleaseDC(nullptr, screenDC);
    return true;
}
