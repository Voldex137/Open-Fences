#pragma once

#include <string>

// Where files go when they are dropped from the desktop into a fence. The
// fence is a view of the desktop, so a dropped desktop item is MOVED into the
// storage folder (no duplicate stays on the desktop) and the fence icon points
// at the moved file. Removing the icon, or deleting the fence, moves the file
// back to the desktop.
namespace Storage {

// The storage folder (setting, saved in config.json). Defaults to
// "<Documents>\Open Fences".
std::wstring Folder();
void SetFolder(const std::wstring& path);
// True while the user has not picked a folder (the default is in use).
bool IsDefault();

// True for a file or folder that sits directly on the user's or the public desktop.
bool IsDesktopItem(const std::wstring& path);

// True for a path inside the storage folder.
bool IsStored(const std::wstring& path);

// A panel's files live in a subfolder of the storage folder named after the panel.
// The name is made safe for the file system (illegal characters become "_", trailing
// dots and spaces are dropped, an empty name becomes "Panel").
std::wstring SafeFolderName(const std::wstring& panelTitle);
std::wstring PanelFolder(const std::wstring& panelTitle);

// Moves a desktop item into the panel's subfolder under a unique name. Returns the
// new path, or an empty string when the item is not on the desktop or the move
// failed (the caller then keeps the original path).
std::wstring AdoptFromDesktop(const std::wstring& path, const std::wstring& panelTitle);

// A file that is already stored (in the storage root, or another panel's folder) is
// moved into this panel's folder. Empty when it is not stored, already there, or the
// move failed.
std::wstring MoveIntoPanel(const std::wstring& path, const std::wstring& panelTitle);

// True for a stored file that sits directly in the storage folder (the layout before
// panels had their own folders).
bool IsInStorageRoot(const std::wstring& path);

// The panel was renamed: its folder is renamed too (merged into an existing folder of
// that name). Returns true when files may have moved, so paths must be rewritten with
// ReplacePanelFolder.
bool RenamePanelFolder(const std::wstring& oldTitle, const std::wstring& newTitle);
std::wstring ReplacePanelFolder(const std::wstring& path, const std::wstring& oldTitle,
                                const std::wstring& newTitle);

// Deletes the panel's folder if it is empty (the panel went away).
void RemovePanelFolderIfEmpty(const std::wstring& panelTitle);

// Moves a stored item back onto the desktop under a unique name. Returns the new
// path, or an empty string when it is not stored or the move failed.
std::wstring ReturnToDesktop(const std::wstring& path);

}  // namespace Storage
