#pragma once

#include <windows.h>
#include <vector>

// Folded icon tiles and ordinary desktop icons form ONE selection:
//  * while the user sweeps a selection rectangle on the empty desktop, every tile inside
//    it is drawn selected, like the real icons (Explorer does not know about our windows,
//    so the rectangle is rebuilt from the mouse: press point -> cursor);
//  * dragging a selected desktop icon carries the selected tiles along, and dragging a
//    selected tile carries the selected desktop icons (below).
namespace DesktopSelection {

// Call every ~40 ms from the owner window's timer.
void Poll();

// A selected ordinary desktop icon and where it was (icon-list client coordinates).
struct Item {
    int index;
    POINT start;
};

// The icons currently selected on the desktop.
std::vector<Item> SelectedItems();

// Puts every item at its start position plus (dx, dy).
void OffsetItems(const std::vector<Item>& items, int dx, int dy);

// Moves the dragged tiles (first = the one under the cursor) onto the desktop grid, one
// after the other, so they never share a slot. Tiles of the group not placed yet do not
// block slots.
void SettleTiles(const std::vector<HWND>& tiles);

}  // namespace DesktopSelection
