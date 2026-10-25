#pragma once

#include <windows.h>
#include <oleidl.h>

// OLE drop target for a fence window. It accepts filesystem paths (CF_HDROP)
// and shell namespace objects (CFSTR_SHELLIDLIST, e.g. "This PC", Recycle Bin)
// and adds them to the fence as icons. Returned with a reference count of 1:
// release it after RegisterDragDrop has taken its own reference.
IDropTarget* CreateFenceDropTarget(HWND hwnd);
