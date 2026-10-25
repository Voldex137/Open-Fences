#pragma once

#include <windows.h>

// The notification-area icon and its menu. The menu is where fences are
// created and where the global settings live (language, autostart, ...).
namespace Tray {

// Callback message of the icon, delivered to the owner window.
constexpr UINT kMessage = WM_APP + 1;

void AddIcon(HWND owner);
void RemoveIcon(HWND owner);

// Shows the menu at the cursor and carries out the chosen command.
void ShowMenu(HWND owner);

}  // namespace Tray
