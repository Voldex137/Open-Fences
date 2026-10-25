#pragma once

#include <windows.h>
#include <memory>
#include <vector>

#include "fence/fence_window.h"

// The set of fences on the desktop: creating, removing, rebuilding after an
// explorer restart, and the "hide all fences" switch. The config module
// saves and restores through this interface.
namespace FenceManager {

using FenceList = std::vector<std::unique_ptr<FenceWindow>>;

FenceList& All();

// Takes ownership of a fence that was just built (config load).
void Add(std::unique_ptr<FenceWindow> fence);

// A new default-sized fence with a fresh id; marks the config dirty.
FenceWindow* CreateNew();

// Removes (and destroys) the fence whose window is `hwnd`; marks the config dirty.
void Remove(HWND hwnd);

// Explorer restarted: the desktop tree took the child fences with it. Recreate
// them from their saved state under the fresh desktop.
void Rebuild();

// "Hide all fences": persisted by the config.
bool AllHidden();
void SetAllHidden(bool hidden);

// The window handles of every fence, for the desktop icon helpers.
std::vector<HWND> Handles();

}  // namespace FenceManager
