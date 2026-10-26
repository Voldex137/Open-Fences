#pragma once

// Mirrors the desktop's rubber-band selection onto the folded icon tiles: while
// the user sweeps a selection rectangle on the empty desktop, every tile inside it
// is drawn selected, like the real desktop icons. Explorer does not know about our
// windows, so the rectangle is rebuilt from the mouse (press point -> cursor).
namespace DesktopSelection {

// Call every ~40 ms from the owner window's timer.
void Poll();

}  // namespace DesktopSelection
