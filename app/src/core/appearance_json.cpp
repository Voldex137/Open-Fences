#include "core/appearance_json.h"

#include <windows.h>
#include <cwchar>
#include <string>

using json = nlohmann::json;

namespace {

std::string ToUtf8(const wchar_t* w) {
    int len = (int)wcslen(w);
    if (len == 0) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w, len, nullptr, 0, nullptr, nullptr);
    std::string s((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, len, s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring FromUtf8(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
    return w;
}

// A JSON array of 4 numbers into rgba; untouched when missing or malformed.
void GetColor4(const json& j, const char* key, float out[4]) {
    if (!j.contains(key) || !j[key].is_array() || j[key].size() != 4) return;
    for (int i = 0; i < 4; i++)
        if (j[key][i].is_number()) out[i] = j[key][i].get<float>();
}

template <size_t N>
void GetString(const json& j, const char* key, wchar_t (&out)[N]) {
    if (!j.contains(key) || !j[key].is_string()) return;
    std::wstring w = FromUtf8(j[key].get<std::string>());
    wcscpy_s(out, N, w.c_str());
}

}  // namespace

namespace AppearanceJson {

json ToJson(const FenceAppearance& app) {
    json j;
    j["bg"]            = { app.bg[0], app.bg[1], app.bg[2], app.bg[3] };
    j["titleColor"]    = { app.title[0], app.title[1], app.title[2], app.title[3] };
    j["titleAlign"]    = app.titleAlign;
    j["displayMode"]   = app.displayMode;
    j["fillDir"]       = app.fillDir;
    j["autoReflow"]    = app.autoReflow;
    j["titleH"]        = app.titleH;   // physical px (appearance is pre-scaled)
    j["fontSize"]      = app.fontSize;
    j["fontName"]      = ToUtf8(app.fontName);
    j["collapseStyle"] = app.collapseStyle;
    j["titleIcon"]     = app.titleIcon;
    j["iconPath"]      = ToUtf8(app.iconPath);
    return j;
}

void FromJson(const json& j, FenceAppearance& app) {
    if (!j.is_object()) return;
    GetColor4(j, "bg", app.bg);
    GetColor4(j, "titleColor", app.title);
    app.titleAlign    = j.value("titleAlign", app.titleAlign);
    app.displayMode   = j.value("displayMode", app.displayMode);
    app.fillDir       = j.value("fillDir", app.fillDir);
    app.autoReflow    = j.value("autoReflow", app.autoReflow);
    app.titleH        = j.value("titleH", app.titleH);
    app.fontSize      = j.value("fontSize", app.fontSize);
    app.collapseStyle = j.value("collapseStyle", app.collapseStyle);
    app.titleIcon     = j.value("titleIcon", app.titleIcon);
    GetString(j, "fontName", app.fontName);
    if (app.fontName[0] == L'\0') wcscpy_s(app.fontName, L"Segoe UI");
    GetString(j, "iconPath", app.iconPath);
}

void CopyStyle(FenceAppearance& dst, const FenceAppearance& src) {
    memcpy(dst.bg, src.bg, sizeof(dst.bg));
    memcpy(dst.title, src.title, sizeof(dst.title));
    dst.titleAlign    = src.titleAlign;
    dst.displayMode   = src.displayMode;
    dst.fillDir       = src.fillDir;
    dst.autoReflow    = src.autoReflow;
    dst.titleH        = src.titleH;
    dst.fontSize      = src.fontSize;
    dst.collapseStyle = src.collapseStyle;
    dst.titleIcon     = src.titleIcon;
    wcscpy_s(dst.fontName, src.fontName);
    wcscpy_s(dst.iconPath, src.iconPath);
}

}  // namespace AppearanceJson
