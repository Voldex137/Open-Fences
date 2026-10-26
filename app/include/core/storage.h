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

// Moves a desktop item into the storage folder under a unique name. Returns the
// new path, or an empty string when the item is not on the desktop or the move
// failed (the caller then keeps the original path).
std::wstring AdoptFromDesktop(const std::wstring& path);

// Moves a stored item back onto the desktop under a unique name. Returns the new
// path, or an empty string when it is not stored or the move failed.
std::wstring ReturnToDesktop(const std::wstring& path);

}  // namespace Storage
