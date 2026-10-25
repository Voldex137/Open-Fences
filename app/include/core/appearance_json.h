#pragma once

#include "render/render.h"
#include <nlohmann/json.hpp>

// The user-editable part of a fence's look (what the settings panel changes) as
// JSON: colours, title, font, display mode, fill direction, collapse style and
// icon. Used by config.json for every fence, for the default look and for presets.
namespace AppearanceJson {

nlohmann::json ToJson(const FenceAppearance& app);

// Reads the fields present in `j`; everything missing keeps its current value,
// so a file written by an older version loads cleanly.
void FromJson(const nlohmann::json& j, FenceAppearance& app);

// Copies only the user-editable fields (not DPI-derived metrics such as cell size).
void CopyStyle(FenceAppearance& dst, const FenceAppearance& src);

}  // namespace AppearanceJson
