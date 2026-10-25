#pragma once

#include <windows.h>
#include <vector>

// Showing and hiding the desktop icon list (SysListView32) without losing the
// fences, which are its child windows. While the list is hidden the fences are
// parked one level up on SHELLDLL_DefView (same desktop tree, same visibility
// and hit-testing, only the icon list itself is gone).
namespace DesktopIcons {

// The desktop icon list, or null when there is none (explorer not up).
HWND FindList();

// Moves a child fence to `newParent` without changing its screen position. A
// popup-fallback fence (not a child window) is left alone.
void ReparentFence(HWND fence, HWND newParent);

// Hides or shows the icon list ourselves (tray menu / Ctrl+Shift+H).
void Toggle(const std::vector<HWND>& fences);

// True while we hold the icon list hidden.
bool HiddenByUs();

// Explorer's own "Show desktop icons" switch hides the list from outside. Call
// periodically: it parks the fences while the list is gone and moves them
// back when it returns.
void WatchExternalVisibility(const std::vector<HWND>& fences);

// A fence created while the icon list is hidden is parked with the others.
void ParkIfHidden(HWND fence);

// Explorer restarted and the fences were recreated under the fresh desktop:
// forget the external parking before the rebuild, and re-hide the list after it
// when we had it hidden.
void BeginRebuild();
void EndRebuild(const std::vector<HWND>& fences);

}  // namespace DesktopIcons
