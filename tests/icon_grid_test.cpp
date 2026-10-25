// The icon grid is the part of the fence layout that needs no window: where a
// dropped icon lands, and where a displaced icon goes when its cell is taken.
#include "fence/icon_grid.h"
#include "test_util.h"

#include <vector>

using namespace IconGridMath;
using test::Check;

// 4 columns x 3 rows, 80x90 cells, origin (4, 30): cell index = row * 4 + col.
static IconGrid Grid() { return IconGrid{ 4, 3, 4.0f, 30.0f, 80.0f, 90.0f }; }

static void TestCellOf() {
    IconGrid g = Grid();
    Check(CellOf(g, 4.0f, 30.0f) == 0, "an icon seated on a cell maps to that cell");
    Check(CellOf(g, 4.0f + 80.0f, 30.0f) == 1, "next column");
    Check(CellOf(g, 4.0f, 30.0f + 90.0f) == 4, "next row");
    Check(CellOf(g, 4.0f + 39.0f, 30.0f) == 0, "just under half a cell to the right still rounds to the same cell");
    Check(CellOf(g, 4.0f + 41.0f, 30.0f) == 1, "just over half a cell rounds to the next one");
    Check(CellOf(g, -500.0f, -500.0f) == 0, "far above-left clamps to the first cell");
    Check(CellOf(g, 9000.0f, 9000.0f) == 11, "far below-right clamps to the last cell");
}

static void TestCellOrigin() {
    IconGrid g = Grid();
    float x = 0, y = 0;
    CellOrigin(g, 0, x, y);
    Check(x == 4.0f && y == 30.0f, "cell 0 origin is the grid origin");
    CellOrigin(g, 5, x, y);
    Check(x == 84.0f && y == 120.0f, "cell 5 = row 1, column 1");

    IconGrid frac{ 4, 3, 4.5f, 30.25f, 80.5f, 90.5f };
    CellOrigin(frac, 1, x, y);
    Check(x == (float)(int)x && y == (float)(int)y, "origins are whole pixels");

    bool roundTrip = true;
    for (int cell = 0; cell < g.cols * g.rows; cell++) {
        CellOrigin(g, cell, x, y);
        roundTrip = roundTrip && CellOf(g, x, y) == cell;
    }
    Check(roundTrip, "CellOf(CellOrigin(cell)) == cell for every cell");
}

static void TestFindFreeCell() {
    IconGrid g = Grid();
    Check(FindFreeCell(g, 5, {}) == 5, "a free preferred cell is returned as is");
    Check(FindFreeCell(g, 5, { 0, 1, 2 }) == 5, "other occupied cells do not matter");

    int next = FindFreeCell(g, 5, { 5 });
    Check(next != 5 && next >= 0 && next < 12, "an occupied cell yields a different cell");
    // Ring 1 around (row 1, col 1) is scanned columns first, rows ascending: col 0, row 0 first.
    Check(next == 0, "the nearest ring is searched columns first, top-left before the rest");
    Check(FindFreeCell(g, 5, { 5, 0 }) == 4, "then down the same column");

    std::vector<int> almostFull;
    for (int i = 0; i < 12; i++) if (i != 11) almostFull.push_back(i);
    Check(FindFreeCell(g, 0, almostFull) == 11, "the single free cell is found from the opposite corner");

    std::vector<int> full;
    for (int i = 0; i < 12; i++) full.push_back(i);
    Check(FindFreeCell(g, 6, full) == 6, "a completely full grid returns the preferred cell");

    IconGrid list{ 1, 5, 4.0f, 30.0f, 200.0f, 24.0f };
    Check(FindFreeCell(list, 2, { 2 }) == 1, "a one-column list picks the neighbouring row");
}

static void TestFindPushCell() {
    IconGrid g = Grid();
    // Row 1 holds cells 4..7; push out of cell 5.
    Check(FindPushCell(g, 5, {}, 1) == 5, "a free cell stays where it is");
    Check(FindPushCell(g, 5, { 5 }, 1) == 6, "dragging right pushes to the right neighbour");
    Check(FindPushCell(g, 5, { 5 }, -1) == 4, "dragging left pushes to the left neighbour");
    Check(FindPushCell(g, 5, { 5, 6 }, 1) == 4, "the nearest free cell wins over the drag direction");
    Check(FindPushCell(g, 5, { 4, 5, 6 }, 1) == 7, "at equal distance the drag direction side is tried first");
    Check(FindPushCell(g, 5, { 5, 6, 7 }, 1) == 4, "uses the other side of the row when one side is full");
    Check(FindPushCell(g, 5, { 4, 5, 6, 7 }, 1) == 9, "a full row spills downward first");
    Check(FindPushCell(g, 9, { 4, 5, 6, 7, 8, 9, 10, 11 }, 1) == 1,
          "with the lower rows full it goes up (same column, nearest side first)");

    std::vector<int> full;
    for (int i = 0; i < 12; i++) full.push_back(i);
    Check(FindPushCell(g, 6, full, 1) == 6, "a completely full grid returns the preferred cell");
}

int main() {
    TestCellOf();
    TestCellOrigin();
    TestFindFreeCell();
    TestFindPushCell();
    return test::Finish();
}
