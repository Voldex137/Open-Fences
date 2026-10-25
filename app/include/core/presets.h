#pragma once

#include "render/render.h"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

// The default look of new fences and the named presets the user saved. Both hold
// only the editable style (see AppearanceJson::CopyStyle) and live in config.json.
namespace Presets {

struct Preset {
    std::wstring name;
    FenceAppearance appearance;
};

// False until the user edits the defaults; new fences then keep the built-in look.
bool HasDefault();
// The default look (seeded from the built-in look, scaled to the screen, on first use).
FenceAppearance& Default();
void SetDefault(const FenceAppearance& src);

std::vector<Preset>& All();
// Adds a preset, or overwrites the one with the same name.
void Save(const std::wstring& name, const FenceAppearance& src);
void Overwrite(size_t index, const FenceAppearance& src);
void Remove(size_t index);
// "Preset 1", "Preset 2", ...: the first name not taken.
std::wstring NextFreeName();

nlohmann::json ToJson();
void FromJson(const nlohmann::json& j);

}  // namespace Presets
