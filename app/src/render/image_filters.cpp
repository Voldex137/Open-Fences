#include "render/image_filters.h"

#include <cstdint>


namespace ImageFilters {

namespace {
inline int ClampIdx(int v, int hi) {
    return v < 0 ? 0 : (v > hi ? hi : v);
}
}  // namespace

void BlurRows(BYTE* dst, const BYTE* src, int w, int h, int r) {
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

void BlurCols(BYTE* dst, const BYTE* src, int w, int h, int r) {
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
void HalfSizeBox(BYTE* dst, const BYTE* src, int w, int h) {
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
void Saturate(BYTE* px, int w, int h, float amount) {
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
void ApplyNoise(BYTE* px, int w, int h) {
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

}  // namespace ImageFilters
