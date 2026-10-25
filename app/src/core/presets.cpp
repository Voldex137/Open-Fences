#include "core/presets.h"
#include "core/appearance_json.h"
#include "shell/desktop_host.h"

#include <algorithm>

using json = nlohmann::json;

namespace Presets {

namespace {

bool g_hasDefault = false;
FenceAppearance g_default;
std::vector<Preset> g_presets;

std::string ToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring FromUtf8(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
    return w;
}

}  // namespace

bool HasDefault() { return g_hasDefault; }

FenceAppearance& Default() {
    if (!g_hasDefault) {
        // Built-in design values are for 96 DPI; the fence appearance holds pixels.
        float s = Desktop::DpiScale();
        g_default.titleH *= s;
        g_default.fontSize *= s;
        g_hasDefault = true;
    }
    return g_default;
}

void SetDefault(const FenceAppearance& src) {
    AppearanceJson::CopyStyle(Default(), src);
}

std::vector<Preset>& All() { return g_presets; }

void Save(const std::wstring& name, const FenceAppearance& src) {
    for (auto& p : g_presets)
        if (_wcsicmp(p.name.c_str(), name.c_str()) == 0) {
            AppearanceJson::CopyStyle(p.appearance, src);
            return;
        }
    Preset p;
    p.name = name;
    AppearanceJson::CopyStyle(p.appearance, src);
    g_presets.push_back(std::move(p));
}

void Overwrite(size_t index, const FenceAppearance& src) {
    if (index < g_presets.size()) AppearanceJson::CopyStyle(g_presets[index].appearance, src);
}

void Remove(size_t index) {
    if (index < g_presets.size()) g_presets.erase(g_presets.begin() + (ptrdiff_t)index);
}

std::wstring NextFreeName() {
    for (int n = 1;; n++) {
        std::wstring name = L"Preset " + std::to_wstring(n);
        bool taken = std::any_of(g_presets.begin(), g_presets.end(), [&](const Preset& p) {
            return _wcsicmp(p.name.c_str(), name.c_str()) == 0; });
        if (!taken) return name;
    }
}

json ToJson() {
    json j;
    if (g_hasDefault) j["default"] = AppearanceJson::ToJson(g_default);
    json list = json::array();
    for (const auto& p : g_presets) {
        json jp = AppearanceJson::ToJson(p.appearance);
        jp["name"] = ToUtf8(p.name);
        list.push_back(std::move(jp));
    }
    j["presets"] = std::move(list);
    return j;
}

void FromJson(const json& j) {
    if (!j.is_object()) return;
    if (j.contains("default") && j["default"].is_object()) {
        AppearanceJson::FromJson(j["default"], Default());
    }
    g_presets.clear();
    if (j.contains("presets") && j["presets"].is_array()) {
        for (const auto& jp : j["presets"]) {
            if (!jp.is_object() || !jp.contains("name") || !jp["name"].is_string()) continue;
            Preset p;
            p.name = FromUtf8(jp["name"].get<std::string>());
            if (p.name.empty()) continue;
            AppearanceJson::FromJson(jp, p.appearance);
            g_presets.push_back(std::move(p));
        }
    }
}

}  // namespace Presets
