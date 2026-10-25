#pragma once

#include <windows.h>
#include <string>

// One icon inside a fence. Plain data, so sorting and layout logic can be
// tested without any rendering code.
struct IconEntry {
    std::wstring name;
    std::wstring path;  // full file path for icon extraction
    std::wstring ext;   // extension incl. '.' (PathFindExtensionW), for Type sort
    ULONGLONG size = 0;
    FILETIME lastWrite = {};
    bool isDir = false;
    float x = 0.0f;     // cell top-left inside the fence client area; icons
    float y = 0.0f;     // rest on the icon grid (snapped on drop/release)
};
