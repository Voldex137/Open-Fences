#include "icon_sort.h"

#include <shlwapi.h>

int CompareIcons(const IconEntry& a, const IconEntry& b, int column, bool asc) {
    if (a.isDir != b.isDir) return a.isDir ? -1 : 1;
    int r = 0;
    switch (column) {
    case 1: // Date modified — newest first descending (Explorer default)
        r = CompareFileTime(&a.lastWrite, &b.lastWrite); break;
    case 2: // Size
        r = a.size < b.size ? -1 : (a.size > b.size ? 1 : 0); break;
    case 3: // Type (extension)
        r = StrCmpLogicalW(a.ext.c_str(), b.ext.c_str()); break;
    default: // Name
        r = StrCmpLogicalW(a.name.c_str(), b.name.c_str()); break;
    }
    if (!asc) r = -r;
    if (r == 0) r = StrCmpLogicalW(a.name.c_str(), b.name.c_str());
    return r;
}
