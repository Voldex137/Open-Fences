#include "core/app_paths.h"

#include <windows.h>
#include <shlobj.h>

namespace AppPaths {

std::wstring ExeDir() {
    wchar_t path[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring s(path, n);
    return s.substr(0, s.find_last_of(L"\\/"));
}

bool IsPortable() {
    static const bool portable =
        GetFileAttributesW((ExeDir() + L"\\portable.flag").c_str()) != INVALID_FILE_ATTRIBUTES;
    return portable;
}

std::wstring DataDir() {
    if (IsPortable()) return ExeDir() + L"\\data";
    wchar_t appdata[MAX_PATH] = {};
    SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, appdata);
    return std::wstring(appdata) + L"\\Open Fences";
}

}  // namespace AppPaths
