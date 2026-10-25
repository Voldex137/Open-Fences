// The pixel filters behind the frosted-glass backdrop.
#include "render/image_filters.h"
#include "test_util.h"

#include <vector>

using namespace ImageFilters;
using test::Check;

// B,G,R,X pixels, w * h, every pixel set to (b, g, r).
static std::vector<BYTE> Solid(int w, int h, BYTE b, BYTE g, BYTE r) {
    std::vector<BYTE> px((size_t)w * h * 4);
    for (size_t i = 0; i < (size_t)w * h; i++) {
        px[i * 4 + 0] = b; px[i * 4 + 1] = g; px[i * 4 + 2] = r; px[i * 4 + 3] = 0;
    }
    return px;
}

static bool AllEqual(const std::vector<BYTE>& px, BYTE b, BYTE g, BYTE r, BYTE a) {
    for (size_t i = 0; i < px.size() / 4; i++)
        if (px[i * 4] != b || px[i * 4 + 1] != g || px[i * 4 + 2] != r || px[i * 4 + 3] != a) return false;
    return true;
}

static void TestBlur() {
    auto flat = Solid(8, 6, 40, 80, 120);
    std::vector<BYTE> out(flat.size());
    BlurRows(out.data(), flat.data(), 8, 6, 2);
    Check(AllEqual(out, 40, 80, 120, 255), "a flat image stays flat under BlurRows, alpha forced opaque");
    BlurCols(out.data(), flat.data(), 8, 6, 2);
    Check(AllEqual(out, 40, 80, 120, 255), "a flat image stays flat under BlurCols");

    // One bright pixel in a row of 5; radius 1 spreads it over three pixels (window of 3).
    auto row = Solid(5, 1, 0, 0, 0);
    row[2 * 4 + 0] = 90; row[2 * 4 + 1] = 90; row[2 * 4 + 2] = 90;
    std::vector<BYTE> blurred(row.size());
    BlurRows(blurred.data(), row.data(), 5, 1, 1);
    Check(blurred[0 * 4] == 0 && blurred[1 * 4] == 30 && blurred[2 * 4] == 30 &&
          blurred[3 * 4] == 30 && blurred[4 * 4] == 0,
          "radius 1 spreads one pixel into a window of three (90 / 3 = 30)");

    // The vertical pass is the same filter on columns.
    auto col = Solid(1, 5, 0, 0, 0);
    col[2 * 4 + 1] = 90;
    std::vector<BYTE> blurredCol(col.size());
    BlurCols(blurredCol.data(), col.data(), 1, 5, 1);
    Check(blurredCol[1 * 4 + 1] == 30 && blurredCol[2 * 4 + 1] == 30 && blurredCol[3 * 4 + 1] == 30 &&
          blurredCol[0 * 4 + 1] == 0 && blurredCol[4 * 4 + 1] == 0,
          "BlurCols spreads along the column");

    // Brightness is preserved away from the edges (a box filter only moves it around).
    int sumIn = 0, sumOut = 0;
    for (size_t i = 0; i < 5; i++) { sumIn += row[i * 4]; sumOut += blurred[i * 4]; }
    Check(sumIn == sumOut, "blurring keeps the total brightness");
}

static void TestHalfSize() {
    std::vector<BYTE> px(4 * 4 * 4, 0);   // 2x2 image
    auto set = [&](int x, int y, BYTE v) {
        for (int c = 0; c < 3; c++) px[((size_t)y * 2 + x) * 4 + c] = v;
    };
    set(0, 0, 0); set(1, 0, 4); set(0, 1, 8); set(1, 1, 12);
    std::vector<BYTE> out(4);
    HalfSizeBox(out.data(), px.data(), 2, 2);
    Check(out[0] == 6 && out[1] == 6 && out[2] == 6 && out[3] == 255, "2x2 block is averaged ((0+4+8+12+2)/4 = 6)");

    auto flat = Solid(8, 8, 10, 20, 30);
    std::vector<BYTE> half((size_t)4 * 4 * 4);
    HalfSizeBox(half.data(), flat.data(), 8, 8);
    Check(AllEqual(half, 10, 20, 30, 255), "a flat image stays flat when halved");
}

static void TestSaturate() {
    auto px = Solid(4, 4, 50, 100, 200);
    auto original = px;
    Saturate(px.data(), 4, 4, 1.0f);
    Check(px == original, "amount 1.0 leaves the image unchanged");

    Saturate(px.data(), 4, 4, 0.0f);
    bool grey = true;
    for (size_t i = 0; i < 16; i++)
        grey = grey && px[i * 4] == px[i * 4 + 1] && px[i * 4 + 1] == px[i * 4 + 2];
    Check(grey, "amount 0 gives grey pixels (all channels equal)");

    auto vivid = Solid(1, 1, 50, 100, 200);
    Saturate(vivid.data(), 1, 1, 2.0f);
    Check(vivid[2] > 200 - 1 && vivid[0] < 50, "amount > 1 pushes channels away from the luma (red up, blue down)");
    auto extreme = Solid(1, 1, 0, 0, 255);
    Saturate(extreme.data(), 1, 1, 10.0f);
    Check(extreme[2] == 255 && extreme[0] == 0, "results are clamped to 0..255");
}

static void TestNoise() {
    auto a = Solid(16, 16, 128, 128, 128);
    auto b = a;
    ApplyNoise(a.data(), 16, 16);
    ApplyNoise(b.data(), 16, 16);
    Check(a == b, "the noise is reproducible (fixed seed)");

    bool inRange = true, changed = false, alphaUntouched = true;
    for (size_t i = 0; i < 16 * 16; i++) {
        for (int c = 0; c < 3; c++) {
            int d = (int)a[i * 4 + c] - 128;
            inRange = inRange && d >= -4 && d <= 3;
            changed = changed || d != 0;
        }
        alphaUntouched = alphaUntouched && a[i * 4 + 3] == 0;
    }
    Check(inRange, "every channel moves by -4 .. +3 levels");
    Check(changed, "the grain actually changes something");
    Check(alphaUntouched, "the alpha byte is not touched");

    auto dark = Solid(4, 4, 0, 0, 0);
    ApplyNoise(dark.data(), 4, 4);
    bool noWrap = true;
    for (size_t i = 0; i < 16; i++)
        for (int c = 0; c < 3; c++) noWrap = noWrap && dark[i * 4 + c] <= 3;
    Check(noWrap, "values near black stay near black (clamped, no wrap-around)");
}

int main() {
    TestBlur();
    TestHalfSize();
    TestSaturate();
    TestNoise();
    return test::Finish();
}
