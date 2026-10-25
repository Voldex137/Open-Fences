#include "render/render.h"
#include <algorithm>

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
