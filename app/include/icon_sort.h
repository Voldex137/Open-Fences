#pragma once

#include "icon_entry.h"

// Ordering of icons in a mapped (folder-backed) fence, like Explorer's: folders
// first, then by `column` (0 name, 1 date modified, 2 size, 3 type), the name
// breaking ties. `asc` false reverses the column order, not the folders-first rule.
// Returns < 0, 0 or > 0 like strcmp.
int CompareIcons(const IconEntry& a, const IconEntry& b, int column, bool asc);
