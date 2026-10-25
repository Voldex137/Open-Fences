#include "render/render.h"
#include "render/image_filters.h"
#include <algorithm>
#include <cstring>

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
        ImageFilters::HalfSizeBox(down.data(), px.data(), cw, ch);
        px = std::move(down);
        cw /= 2; ch /= 2;
    }

    // Blur on the small image: 2 passes per axis approximate a Gaussian.
    std::vector<BYTE> tmp(px.size());
    const int r = 5;
    for (int pass = 0; pass < 2; pass++) {
        ImageFilters::BlurRows(tmp.data(), px.data(), cw, ch, r);
        ImageFilters::BlurCols(px.data(), tmp.data(), cw, ch, r);
    }

    // Acrylic finishing passes on the blurred image: +25 % saturation so
    // the glass reads colorful instead of gray, then a fixed-seed grain
    // that breaks up the banding a box blur leaves in flat regions.
    ImageFilters::Saturate(px.data(), cw, ch, 1.25f);
    ImageFilters::ApplyNoise(px.data(), cw, ch);

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
