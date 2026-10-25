#include "fence/icon_grid.h"

#include <algorithm>
#include <cmath>

namespace IconGridMath {

int CellOf(const IconGrid& g, float x, float y) {
    int c = (int)floorf((x + g.cw * 0.5f - g.x0) / g.cw);
    int r = (int)floorf((y + g.ch * 0.5f - g.y0) / g.ch);
    c = (std::min)((std::max)(c, 0), g.cols - 1);
    r = (std::min)((std::max)(r, 0), g.rows - 1);
    return r * g.cols + c;
}

void CellOrigin(const IconGrid& g, int cell, float& x, float& y) {
    int c = cell % g.cols, r = cell / g.cols;
    x = floorf(g.x0 + c * g.cw + 0.5f);
    y = floorf(g.y0 + r * g.ch + 0.5f);
}

int FindFreeCell(const IconGrid& g, int prefer, const std::vector<int>& occupied) {
    auto taken = [&](int idx) {
        return std::find(occupied.begin(), occupied.end(), idx) != occupied.end();
    };
    if (!taken(prefer)) return prefer;
    // Walk Chebyshev rings around the preferred cell, columns first so a
    // crowded drop fills downward, then to the right — desktop-like.
    int pc = prefer % g.cols, pr = prefer / g.cols;
    auto dist = [](int a, int b) { int d = a - b; return d < 0 ? -d : d; };
    for (int d = 1, maxD = (std::max)(g.cols, g.rows); d <= maxD; d++) {
        for (int c = pc - d; c <= pc + d; c++) {
            for (int r = pr - d; r <= pr + d; r++) {
                if ((std::max)(dist(c, pc), dist(r, pr)) != d) continue;   // ring only
                if (c < 0 || c >= g.cols || r < 0 || r >= g.rows) continue;
                int idx = r * g.cols + c;
                if (!taken(idx)) return idx;
            }
        }
    }
    return prefer;   // fence completely full — overlap is unavoidable
}

int FindPushCell(const IconGrid& g, int prefer, const std::vector<int>& occupied, int dirX) {
    auto taken = [&](int idx) {
        return std::find(occupied.begin(), occupied.end(), idx) != occupied.end();
    };
    int pc = prefer % g.cols, pr = prefer / g.cols;
    int maxD = (std::max)(g.cols, g.rows);
    // Scan one row outward from pc: the drag-direction side first, then the
    // other side, nearest cells first — pushed icons slide along the row.
    auto scanRow = [&](int r) -> int {
        if (r < 0 || r >= g.rows) return -1;
        for (int dc = 0; dc <= maxD; dc++) {
            int passes = (dc == 0) ? 1 : 2;
            for (int pass = 0; pass < passes; pass++) {
                int side;
                if (dc == 0)       side = 0;
                else if (pass == 0) side = (dirX < 0) ? -1 : 1;
                else                side = (dirX < 0) ? 1 : -1;
                int c = pc + side * dc;
                if (c < 0 || c >= g.cols) continue;
                int idx = r * g.cols + c;
                if (!taken(idx)) return idx;
            }
        }
        return -1;
    };
    // Same row first; only a completely full row spills, downward before up.
    for (int dr = 0; dr <= maxD; dr++) {
        if (dr == 0) {
            int f = scanRow(pr);
            if (f >= 0) return f;
        } else {
            int f = scanRow(pr + dr);
            if (f >= 0) return f;
            f = scanRow(pr - dr);
            if (f >= 0) return f;
        }
    }
    return prefer;   // fence completely full — overlap unavoidable
}

}  // namespace IconGridMath
