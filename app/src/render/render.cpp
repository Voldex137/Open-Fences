#include "render/render.h"
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

RenderContext::~RenderContext() {
    m_iconCache.clear();
    if (m_memDC && m_oldBmp) {
        SelectObject(m_memDC, m_oldBmp);
        DeleteObject(m_dib);
        DeleteDC(m_memDC);
    }
}

// ── Icon loading ──

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
