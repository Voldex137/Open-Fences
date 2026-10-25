#include "localization.h"

#include <windows.h>
#include <shlobj.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cwchar>
#include <cwctype>
#include <functional>
#include <string_view>
#include <unordered_map>

using json = nlohmann::json;

namespace {

// Heterogeneous lookup: Tr() is called while painting, so it must not
// allocate a std::string per call.
struct KeyHash {
    using is_transparent = void;
    size_t operator()(std::string_view s) const { return std::hash<std::string_view>{}(s); }
};
using Table = std::unordered_map<std::string, std::wstring, KeyHash, std::equal_to<>>;

struct LangData {
    L10n::Language info;
    Table strings;
};

std::vector<LangData> g_data;
std::vector<L10n::Language> g_list;
size_t g_cur = 0;
int g_en = -1;   // index of the English table, the fallback for missing keys
std::wstring g_curCode = L"en";

std::wstring Widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

std::wstring Lower(std::wstring s) {
    for (auto& c : s) c = (wchar_t)std::towlower(c);
    return s;
}

// Part before the first '-', lower-cased: "zh-CN" -> "zh".
std::wstring Primary(const std::wstring& tag) {
    return Lower(tag.substr(0, tag.find(L'-')));
}

// A file with a code that is already loaded replaces it (external files
// override the embedded ones); a file without a usable code is ignored.
void AddLanguage(const char* data, size_t size) {
    json j = json::parse(data, data + size, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return;

    json meta = j.value("_meta", json::object());
    std::string code = meta.is_object() ? meta.value("code", std::string()) : std::string();
    if (code.empty()) return;

    LangData ld;
    ld.info.code = Widen(code);
    ld.info.name = Widen(meta.value("name", code));
    for (auto it = j.begin(); it != j.end(); ++it) {
        if (it.key().empty() || it.key()[0] == '_' || !it.value().is_string()) continue;
        ld.strings[it.key()] = Widen(it.value().get<std::string>());
    }

    for (auto& existing : g_data) {
        if (Lower(existing.info.code) == Lower(ld.info.code)) { existing = std::move(ld); return; }
    }
    g_data.push_back(std::move(ld));
}

BOOL CALLBACK EmbeddedLangProc(HMODULE mod, LPCWSTR, LPWSTR name, LONG_PTR) {
    if (IS_INTRESOURCE(name) || wcsncmp(name, L"LANG_", 5) != 0) return TRUE;
    HRSRC res = FindResourceW(mod, name, RT_RCDATA);
    if (!res) return TRUE;
    HGLOBAL h = LoadResource(mod, res);
    const void* p = h ? LockResource(h) : nullptr;
    if (p) AddLanguage((const char*)p, SizeofResource(mod, res));
    return TRUE;
}

void LoadDirectory(const std::wstring& dir) {
    WIN32_FIND_DATAW fd;
    HANDLE find = FindFirstFileW((dir + L"\\*.json").c_str(), &fd);
    if (find == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        HANDLE f = CreateFileW((dir + L"\\" + fd.cFileName).c_str(), GENERIC_READ, FILE_SHARE_READ,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f == INVALID_HANDLE_VALUE) continue;
        DWORD size = GetFileSize(f, nullptr), got = 0;
        std::string bytes(size, '\0');
        if (ReadFile(f, bytes.data(), size, &got, nullptr) && got == size) {
            // Skip a UTF-8 BOM that some editors add.
            size_t skip = (bytes.size() >= 3 && (unsigned char)bytes[0] == 0xEF &&
                           (unsigned char)bytes[1] == 0xBB && (unsigned char)bytes[2] == 0xBF) ? 3 : 0;
            AddLanguage(bytes.data() + skip, bytes.size() - skip);
        }
        CloseHandle(f);
    } while (FindNextFileW(find, &fd));
    FindClose(find);
}

std::wstring ExeDir() {
    wchar_t path[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring s(path, n);
    return s.substr(0, s.find_last_of(L"\\/"));
}

std::wstring AppDataDir() {
    wchar_t path[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, path))) return {};
    return std::wstring(path) + L"\\openFences";
}

int IndexOf(const std::wstring& code) {
    std::wstring want = Lower(code);
    for (size_t i = 0; i < g_data.size(); ++i)
        if (Lower(g_data[i].info.code) == want) return (int)i;
    return -1;
}

}  // namespace

namespace L10n {

void Init() {
    g_data.clear();
    EnumResourceNamesW(nullptr, RT_RCDATA, EmbeddedLangProc, 0);
    LoadDirectory(ExeDir() + L"\\lang");
    std::wstring appData = AppDataDir();
    if (!appData.empty()) LoadDirectory(appData + L"\\lang");

    // English first (the reference), the rest alphabetically by code.
    std::sort(g_data.begin(), g_data.end(), [](const LangData& a, const LangData& b) {
        bool ae = Lower(a.info.code) == L"en", be = Lower(b.info.code) == L"en";
        if (ae != be) return ae;
        return Lower(a.info.code) < Lower(b.info.code);
    });
    g_list.clear();
    for (const auto& d : g_data) g_list.push_back(d.info);

    g_en = IndexOf(L"en");
    g_cur = g_en >= 0 ? (size_t)g_en : 0;
    g_curCode = g_data.empty() ? L"en" : g_data[g_cur].info.code;
}

const std::vector<Language>& Languages() { return g_list; }

const std::wstring& CurrentCode() { return g_curCode; }

bool SetLanguage(const std::wstring& code) {
    int i = IndexOf(code);
    if (i < 0) return false;
    g_cur = (size_t)i;
    g_curCode = g_data[g_cur].info.code;
    return true;
}

std::wstring DetectSystemLanguage() {
    ULONG count = 0, size = 0;
    if (GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, nullptr, &size) && size > 1) {
        std::wstring names(size, L'\0');
        if (GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, names.data(), &size)) {
            // Multi-string, in the user's priority order: "ru-RU\0en-US\0\0".
            for (const wchar_t* p = names.c_str(); *p; p += wcslen(p) + 1) {
                std::wstring tag = p;
                int exact = IndexOf(tag);
                if (exact >= 0) return g_data[exact].info.code;
                for (const auto& d : g_data)
                    if (Primary(d.info.code) == Primary(tag)) return d.info.code;
            }
        }
    }
    return L"en";
}

const wchar_t* Tr(const char* key) {
    static std::wstring s_missing;
    if (!g_data.empty()) {
        std::string_view k(key);
        auto it = g_data[g_cur].strings.find(k);
        if (it != g_data[g_cur].strings.end()) return it->second.c_str();
        if (g_en >= 0) {
            it = g_data[g_en].strings.find(k);
            if (it != g_data[g_en].strings.end()) return it->second.c_str();
        }
    }
    s_missing = Widen(key);
    return s_missing.c_str();
}

}  // namespace L10n
