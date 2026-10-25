#pragma once

#include <windows.h>

// Pixel filters used to build the frosted-glass backdrop. All of them work on
// top-down 32-bit pixels in B,G,R,X byte order (w * h * 4 bytes) and need no
// graphics API.
namespace ImageFilters {

// Horizontal / vertical box blur of radius r (window 2r + 1, edges clamped).
// Reads `src`, writes `dst`; the output alpha byte is forced to 255.
void BlurRows(BYTE* dst, const BYTE* src, int w, int h, int r);
void BlurCols(BYTE* dst, const BYTE* src, int w, int h, int r);

// 2x2 box-average downsample into (w/2) x (h/2); needs w, h >= 2.
void HalfSizeBox(BYTE* dst, const BYTE* src, int w, int h);

// Pulls each colour channel away from the pixel's luma in place
// (1.0 = unchanged, 1.25 = +25 %, 0 = grey).
void Saturate(BYTE* px, int w, int h, float amount);

// Fixed-seed grain of -4 .. +3 levels per colour channel, in place. The seed is
// constant so the result is reproducible.
void ApplyNoise(BYTE* px, int w, int h);

}  // namespace ImageFilters
