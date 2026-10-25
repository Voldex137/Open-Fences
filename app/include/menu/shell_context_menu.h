#pragma once

#include <windows.h>
#include <shobjidl.h>
#include <string>
#include <vector>

// Explorer's own context menus, hosted inside a fence: the right-click menu of
// a single file, and the shell "New" submenu (NewMenuHandler).
namespace ShellMenu {

// Command-id base the "New" handler owns inside the fence menu (above our own ids).
constexpr int kCmdNewFirst = 0x200;

// IContextMenu for the file or folder at `path` (any parse name the shell accepts).
HRESULT CreateForPath(const std::wstring& path, IContextMenu** menu);

// Shows the Explorer menu for `path` at `ptScreen` (screen coordinates) with a
// "remove from fence" entry appended. Returns false only when no system menu
// could be produced (the caller falls back to the plain fence menu). Sets
// `invoked` when a shell command ran, `removeChosen` for our own verb, and
// `renameChosen` when the shell's rename verb was picked (the caller then starts
// the in-place rename instead of handing it to Explorer).
bool RunForPath(HWND hwnd, const std::wstring& path, const std::wstring& displayName,
                POINT ptScreen, bool& invoked, bool& removeChosen, bool& renameChosen);

// The shell "New" handler, initialised with `folder` as the target folder.
HRESULT CreateNewMenuHandler(PCIDLIST_ABSOLUTE folder, IContextMenu** menu);

// The handler creates the file but does not report its name; diff the folder
// listing around InvokeCommand to find it.
void SnapshotFolderNames(const std::wstring& dir, std::vector<std::wstring>& names);

// The "New" menu session that is currently on screen. While set, the fence
// window forwards the menu messages the handler needs (see ForwardMenuMessage).
void SetActiveNewMenu(IContextMenu* menu);
IContextMenu* ActiveNewMenu();

// Forwards WM_INITMENUPOPUP / WM_DRAWITEM / WM_MEASUREITEM to a shell handler,
// the same way Explorer does.
void ForwardMenuMessage(IContextMenu* menu, UINT msg, WPARAM wp, LPARAM lp);

}  // namespace ShellMenu
