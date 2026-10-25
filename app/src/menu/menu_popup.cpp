#include "menu/fluent_menu_impl.h"
#include "menu/context_menu.h"
#include "shell/desktop_host.h"
#include <windowsx.h>
#include <algorithm>
#include <cmath>

namespace FluentMenuImpl {

bool MenuPopup::Create(MenuSession* se, const std::vector<CtxItem>* it, int dep,
                       MenuPopup* par, int parItemIdx, POINT at) {
    sess = se;
    items = it;
    depth = dep;
    parent = par;
    parentItem = parItemIdx;
    if (!items || items->empty()) return false;

    static bool clsReg = false;
    if (!clsReg) {
        WNDCLASSEXW wc = {sizeof(wc)};
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = WndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = L"OpenFencesFluentMenu";
        if (!RegisterClassExW(&wc)) return false;
        clsReg = true;
    }

    Layout();                        // → w, rowY/rowH, contentH, text layouts

    // Soft drop shadow: the surface floats on a translucent margin drawn as
    // fading concentric strokes (see DrawMenu). The layered window is
    // enlarged by this margin on every side, but ALL placement math below
    // stays in surface coordinates — clamping, flipping and flyout
    // alignment are unchanged; the window simply hangs `shadow` pixels
    // past the surface (legal for ULW; Win11's own menus do the same).
    shadow = (int)(12 * sess->s + 0.5f);

    float s = sess->s;
    HMONITOR mon = MonitorFromPoint(at, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = {sizeof(mi)};
    GetMonitorInfoW(mon, &mi);

    // Taller than the work area → clamp the height; the wheel scrolls it.
    int maxH = (mi.rcWork.bottom - mi.rcWork.top) - (int)(16 * s);
    if (maxH < (int)(60 * s)) return false;
    h = (std::min)(contentH, maxH);

    // The layered window hangs `shadow` px past the surface on every side
    // (see DrawMenu); keep the entire window inside the work area so the
    // soft drop-shadow is not clipped by the taskbar or display edge.
    const int S = shadow;
    int winW = w + 2 * S, winH = h + 2 * S;

    x = at.x;
    y = at.y;
    if (depth == 0) {
        // Root: clamp to the work area, flipping above the click when needed.
        if (y + winH > mi.rcWork.bottom) y = at.y - h;
        if (x + winW > mi.rcWork.right)  x = mi.rcWork.right - w - S;
        x = clampi(x, mi.rcWork.left + S,
                   (std::max)(mi.rcWork.left + S, mi.rcWork.right - winW + S));
        y = clampi(y, mi.rcWork.top + S,
                   (std::max)(mi.rcWork.top + S,  mi.rcWork.bottom - winH + S));
    } else {
        // Flyout: `at` sits on the parent's right edge; flip left if clipped.
        if (x + winW > mi.rcWork.right && parent)
            x = parent->x - w + (int)(4 * s);
        x = clampi(x, mi.rcWork.left + S,
                   (std::max)(mi.rcWork.left + S, mi.rcWork.right - winW + S));
        y = clampi(y, mi.rcWork.top + S,
                   (std::max)(mi.rcWork.top + S,  mi.rcWork.bottom - winH + S));
    }

    render = std::make_unique<RenderContext>((UINT)(w + 2 * shadow),
                                             (UINT)(h + 2 * shadow));
    if (!render->Target()) return false;
    MakeBrushes();
    BuildIcons();

    hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW,
        L"OpenFencesFluentMenu", L"", WS_POPUP,
        x - shadow, y - shadow, w + 2 * shadow, h + 2 * shadow,
        nullptr, nullptr, GetModuleHandleW(nullptr), this);
    if (!hwnd) return false;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)this);

    CaptureBackdrop();

    // Draw + Present before ShowWindow so the first visible frame is clean.
    Repaint();
    ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    // Stay above every normal window (including the taskbar — the geometry
    // clamp already prevents overlap) so no part of the menu is ever
    // obscured.  The LL hook dismisses on the first outside click, so the
    // topmost flag does not outstay its welcome.
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    if (depth == 0) SetForegroundWindow(hwnd);   // flyouts never steal activation
    SetFocus(hwnd);
    return true;
}

void MenuPopup::Layout() {
    const Metrics& mt = sess->mt;
    float s = sess->s;

    anySub = false;
    for (const auto& it : *items)
        if (it.hasSub) { anySub = true; break; }

    // Measure every label; the widest one sets the text column.
    texts.assign(items->size(), nullptr);
    float maxText = 0.0f;
    for (size_t i = 0; i < items->size(); i++) {
        const CtxItem& it = (*items)[i];
        if (it.sep || it.text.empty()) continue;
        ComPtr<IDWriteTextLayout> tl;
        if (FAILED(sess->dw->CreateTextLayout(it.text.c_str(), (UINT32)it.text.size(),
                it.isDefault ? sess->fmtSemi.Get() : sess->fmt.Get(),
                mt.maxTextW * s, mt.rowH * s * 2.0f, &tl)) || !tl)
            continue;
        tl->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        DWRITE_TEXT_METRICS m = {};
        tl->GetMetrics(&m);
        if (m.width > maxText) maxText = m.width;
        texts[i] = tl;
    }

    float need = mt.padH * s * 2.0f + mt.iconCol * s + mt.gap * s + maxText
               + (anySub ? mt.chevr * s : 0.0f);
    w = (int)std::ceil(clampf(need, mt.minW * s, mt.maxW * s));

    // Re-clamp the layouts to the final text column so over-long labels are
    // trimmed at the column edge instead of overflowing the surface.
    float textCol = (float)w - mt.padH * s * 2.0f - mt.iconCol * s - mt.gap * s
                    - (anySub ? mt.chevr * s : 0.0f);
    DWRITE_TRIMMING trim = { DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
    for (auto& tl : texts) {
        if (!tl) continue;
        tl->SetMaxWidth((std::max)(1.0f, textCol));
        tl->SetTrimming(&trim, nullptr);
    }

    // Row geometry in content space (the popup scrolls when height-clamped).
    rowY.clear(); rowH.clear();
    rowY.reserve(items->size()); rowH.reserve(items->size());
    float y = mt.padV * s;
    for (const auto& it : *items) {
        rowY.push_back(y);
        float rh = (it.sep ? mt.sepH : mt.rowH) * s;
        rowH.push_back(rh);
        y += rh;
    }
    contentH = (int)std::ceil(y + mt.padV * s);
    scroll = 0;
}

void MenuPopup::BuildIcons() {
    icons.assign(items->size(), nullptr);
    auto* t = render ? render->Target() : nullptr;
    if (!t) return;
    D2D1_BITMAP_PROPERTIES bp = {};
    bp.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
    bp.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
    bp.dpiX = bp.dpiY = 96.0f;
    for (size_t i = 0; i < items->size(); i++) {
        const CtxItem& it = (*items)[i];
        if (it.iconPx.empty() || it.iconW <= 0 || it.iconH <= 0) continue;
        t->CreateBitmap(D2D1::SizeU((UINT32)it.iconW, (UINT32)it.iconH),
                        it.iconPx.data(), (UINT32)it.iconW * 4, bp, &icons[i]);
    }
}

void MenuPopup::MakeBrushes() {
    auto* t = render->Target();
    if (!t) return;
    auto mk = [&](float r, float g, float b, float a, ID2D1SolidColorBrush** out) {
        t->CreateSolidColorBrush(D2D1::ColorF(r, g, b, a), out);
    };
    mk(0.10f, 0.10f, 0.12f, 0.85f, &brTint);    // surface tint over the backdrop
    mk(1, 1, 1, 0.08f, &brBorder);
    mk(1, 1, 1, 0.05f, &brHi);                  // top edge highlight
    mk(1, 1, 1, 0.93f, &brText);
    mk(1, 1, 1, 0.36f, &brDim);                 // disabled entries
    mk(1, 1, 1, 0.06f, &brHover);               // hover plate
    mk(0, 0, 0, 0.10f, &brPressed);             // pressed plate: the hover
                                                // darkens while the button is
                                                // held, so the click has
                                                // visible feedback before the
                                                // item fires
    mk(1, 1, 1, 0.08f, &brSep);
    mk(1, 1, 1, 0.60f, &brChev);
    mk(1, 1, 1, 0.90f, &brCheck);
    // Shadow bands share one brush; DrawMenu retunes its alpha per band
    // (the redraw path must not allocate).
    mk(0, 0, 0, 1.0f, &brShadow);

    // Fallback surface when no backdrop could be captured: an opaque dark
    // color expressed as a 1×1 bitmap brush (the only "solid" brush we have).
    static const BYTE px[4] = { 24, 24, 28, 255 };   // BGRA, premultiplied
    D2D1_BITMAP_PROPERTIES bp = {};
    bp.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
    bp.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
    bp.dpiX = bp.dpiY = 96.0f;
    ComPtr<ID2D1Bitmap> b1;
    if (SUCCEEDED(t->CreateBitmap(D2D1::SizeU(1, 1), px, 4, bp, &b1)))
        t->CreateBitmapBrush(b1.Get(), D2D1::BitmapBrushProperties(D2D1_EXTEND_MODE_CLAMP),
                             &backBrush);
}

void MenuPopup::CaptureBackdrop() {
    if (!render || w <= 0 || h <= 0) return;
    // Same trick as FenceWindow/SettingsPanel: layered windows are excluded
    // from GDI screen captures, so the popup never lands in its own backdrop.
    // The capture spans surface + shadow margin so the backdrop brush is
    // anchored at the window's top-left (DrawMenu relies on that).
    const int S = shadow;
    const int bw = w + 2 * S, bh = h + 2 * S;
    HDC screenDC = GetDC(nullptr);
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = bw;
    bmi.bmiHeader.biHeight = -bh;                // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP dib = CreateDIBSection(screenDC, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (dib) {
        HDC memDC = CreateCompatibleDC(screenDC);
        HGDIOBJ oldBmp = SelectObject(memDC, dib);
        BitBlt(memDC, 0, 0, bw, bh, screenDC, x - S, y - S, SRCCOPY);
        if (bits) render->SetBackdrop((const BYTE*)bits, bw, bh);
        SelectObject(memDC, oldBmp);
        DeleteDC(memDC);
        DeleteObject(dib);
    }
    ReleaseDC(nullptr, screenDC);
}

void MenuPopup::Repaint() {
    if (!render || !hwnd) return;
    DrawMenu();
    render->Present(hwnd, x - shadow, y - shadow);
}

}  // namespace FluentMenuImpl
