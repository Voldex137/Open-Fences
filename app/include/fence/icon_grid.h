#pragma once

#include <utility>
#include <vector>

// Geometry of a fence's icon grid, independent of any window: cell (r, c) has
// index r * cols + c and its top-left corner at (x0 + c * cw, y0 + r * ch).
struct IconGrid {
    int cols, rows;
    float x0, y0, cw, ch;
};

namespace IconGridMath {

// Nearest cell for an icon whose top-left is at (x, y). The icon's centre is
// mapped, so a half-cell offset rounds to the cell the icon visually belongs
// to; positions outside the grid clamp to the border cells.
int CellOf(const IconGrid& g, float x, float y);

// Top-left corner of `cell`, rounded to whole pixels: fractional offsets force
// bilinear resampling and make the glyph look blurry.
void CellOrigin(const IconGrid& g, int cell, float& x, float& y);

// `prefer` if it is free, otherwise the nearest free cell (Chebyshev rings,
// columns first so a crowded drop fills downward, then to the right). `prefer`
// again when the grid is completely full.
int FindFreeCell(const IconGrid& g, int prefer, const std::vector<int>& occupied);

// Free cell for an icon pushed out of `prefer`: same row first, on the side of
// the drag direction `dirX` (< 0 left, otherwise right), then the other side;
// a completely full row spills downward before upward. `prefer` when full.
int FindPushCell(const IconGrid& g, int prefer, const std::vector<int>& occupied, int dirX);

// Cell for the k-th icon of a sequence: row by row (left to right) when
// `vertical` is false, column by column (top to bottom) when it is true.
// Wraps into the next row/column; k past the grid clamps to the last cell.
int CellForSequence(const IconGrid& g, int k, bool vertical);

// Indices of `positions` (icon top-left corners) in reading order, judged by the
// row/column each icon sits in WITHOUT clamping to the grid, so a layout made on
// a larger grid keeps its order. Horizontal: by row, then column; vertical: by
// column, then row. Ties keep the original order.
std::vector<int> ReadingOrder(const IconGrid& g,
                              const std::vector<std::pair<float, float>>& positions,
                              bool vertical);

}  // namespace IconGridMath
