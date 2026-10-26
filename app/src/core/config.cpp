#include "core/config.h"
#include "fence/fence_window.h"
#include "fence/fence_manager.h"
#include "core/localization.h"
#include "core/storage.h"
#include "core/app_paths.h"
#include "core/appearance_json.h"
#include "core/presets.h"
#include <nlohmann/json.hpp>
#include <shlobj.h>     // SHGetFolderPathW (CSIDL_APPDATA)
#include <shellapi.h>   // ShellExecuteW (reveal in Explorer)
#include <vector>
#include <memory>
#include <cwchar>

using json = nlohmann::json;

// App state owned by main.cpp — serialized here on save, rebuilt on load.

namespace {

// Global setting: every fence starts folded, whatever state it was left in.
bool g_collapseOnStart = false;

// nlohmann stores narrow strings as UTF-8; the app works in UTF-16.
std::string ToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(),
                                nullptr, 0, nullptr, nullptr);
    std::string s((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(),
                        s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring FromUtf8(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(),
                                nullptr, 0);
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(),
                        w.data(), n);
    return w;
}

json SaveFence(FenceWindow& f) {
    FenceData d = f.GetData();
    const FenceAppearance& app = f.GetRender().Appearance();
    json jf;
    jf["id"]        = ToUtf8(d.id);
    jf["title"]     = ToUtf8(d.title);
    jf["x"]         = d.x;          // screen coords; the ctor converts to
    jf["y"]         = d.y;          // parent-client on creation
    jf["w"]         = d.w;
    jf["h"]         = d.h;
    jf["collapsed"] = f.IsCollapsed();
    jf.update(AppearanceJson::ToJson(app));
    jf["source"]    = ToUtf8(d.sourceFolder);
    jf["sortCol"]   = d.sortCol;
    jf["sortAsc"]   = d.sortAsc;
    json icons = json::array();
    for (const IconEntry& e : f.Icons()) {
        icons.push_back({
            { "name", ToUtf8(e.name) },
            { "path", ToUtf8(e.path) },
            { "x", e.x }, { "y", e.y },
        });
    }
    jf["icons"] = std::move(icons);
    return jf;
}

void LoadFence(const json& jf) {
    if (!jf.is_object()) return;
    FenceData fd;
    fd.id    = FromUtf8(jf.value("id", std::string()));
    fd.title = FromUtf8(jf.value("title", std::string()));
    fd.x     = jf.value("x", 0);
    fd.y     = jf.value("y", 0);
    fd.w     = jf.value("w", 0);
    fd.h     = jf.value("h", 0);
    if (fd.w < 40 || fd.h < 20) return;   // degenerate entry — skip

    auto f = std::make_unique<FenceWindow>(fd);
    if (!f->Hwnd()) return;               // no desktop host right now

    // Appearance: restore exactly the fields the settings panel edits.
    FenceAppearance& app = f->GetRender().Appearance();
    AppearanceJson::FromJson(jf, app);
    f->GetRender().RebuildStyles();

    fd.sourceFolder = FromUtf8(jf.value("source", std::string()));

    if (jf.contains("sortCol"))
        f->SetSortPreset(jf.value("sortCol", 0), jf.value("sortAsc", true));

    if (!fd.sourceFolder.empty()) {
        f->MapToFolder(fd.sourceFolder);
    } else {
        // Icons: restore positions as saved, then re-snap onto the current grid
        // (a no-op unless the desktop icon size changed between sessions).
        std::vector<IconEntry> icons;
        if (jf.contains("icons") && jf["icons"].is_array()) {
            icons.reserve(jf["icons"].size());
            for (const auto& ji : jf["icons"]) {
                if (!ji.is_object()) continue;
                IconEntry e;
                e.name = FromUtf8(ji.value("name", std::string()));
                e.path = FromUtf8(ji.value("path", std::string()));
                e.x = ji.value("x", 0.0f);
                e.y = ji.value("y", 0.0f);
                if (e.path.empty()) continue;
                icons.push_back(std::move(e));
            }
        }
        f->SetIcons(icons);
        f->RelayoutIcons();
    }

    // Collapse LAST: while expanded the window still has its saved height,
    // so ToggleCollapse remembers the right m_expandedH.
    // Folder-mode fences (they fold back after a click elsewhere) always start folded.
    if (jf.value("collapsed", false) || g_collapseOnStart || app.collapseStyle == 1)
        f->ToggleCollapse();

    FenceManager::Add(std::move(f));
}

} // namespace

namespace Config {

const std::wstring& Path() {
    static const std::wstring p = AppPaths::DataDir() + L"\\config.json";
    return p;
}

bool SaveNow() {
    json j;
    j["version"]  = 1;
    j["language"] = ToUtf8(L10n::CurrentCode());
    if (!Storage::IsDefault()) j["storageFolder"] = ToUtf8(Storage::Folder());   // only a folder the user chose
    j["hideAll"]  = FenceManager::AllHidden();
    j["styles"]   = Presets::ToJson();
    j["collapseOnStart"] = g_collapseOnStart;
    json fences = json::array();
    for (const auto& f : FenceManager::All())
        fences.push_back(SaveFence(*f));
    j["fences"] = std::move(fences);
    // Indented on purpose: the tray menu invites users to open this file.
    std::string txt = j.dump(2);

    // Ensure %APPDATA%\Open Fences exists.
    std::wstring path = Path();
    std::wstring dir = path.substr(0, path.find_last_of(L'\\'));
    CreateDirectoryW(dir.c_str(), nullptr);   // EEXIST is fine

    // Atomic replace: write .tmp, then rename over the real file.
    std::wstring tmp = path + L".tmp";
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    BOOL ok = WriteFile(h, txt.data(), (DWORD)txt.size(), &written, nullptr);
    CloseHandle(h);
    if (!ok || written != (DWORD)txt.size()) {
        DeleteFileW(tmp.c_str());
        return false;
    }
    if (!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileW(tmp.c_str());
        return false;
    }
    return true;
}

void MarkDirty() {
    HWND owner = FenceWindow::Owner();
    if (!owner) return;
    // Same-ID SetTimer restarts the countdown — a burst of changes (a drag
    // fires dozens) collapses into one write once things go quiet.
    SetTimer(owner, 3, 800, nullptr);
}

bool LoadApp() {
    HANDLE h = CreateFileW(Path().c_str(), GENERIC_READ, FILE_SHARE_READ,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD size = GetFileSize(h, nullptr);
    std::vector<char> bytes(size);
    DWORD got = 0;
    BOOL ok = ReadFile(h, bytes.data(), size, &got, nullptr);
    CloseHandle(h);
    if (!ok || got != size) return false;

    // allow_exceptions=false: a corrupt file degrades to "no config", and
    // the caller falls back to the defaults instead of failing to start.
    json j = json::parse(bytes.begin(), bytes.end(), nullptr, false);
    if (j.is_discarded() || !j.is_object()) return false;

    // A missing or unknown value leaves the language chosen at startup (system language).
    // Older configs stored a number: 0 = English, 1 = Chinese.
    if (j.contains("language")) {
        const json& jl = j["language"];
        if (jl.is_string())
            L10n::SetLanguage(FromUtf8(jl.get<std::string>()));
        else if (jl.is_number_integer())
            L10n::SetLanguage(jl.get<int>() == 1 ? L"zh-CN" : L"en");
    }

    g_collapseOnStart = j.value("collapseOnStart", false);
    if (j.contains("styles")) Presets::FromJson(j["styles"]);   // before the fences: they may copy the defaults

    if (j.contains("fences") && j["fences"].is_array())
        for (const auto& jf : j["fences"])
            LoadFence(jf);

    // An empty array is a legitimate state (the user deleted every fence) —
    // only a MISSING file triggers the defaults, so honor it.
    if (j.contains("storageFolder") && j["storageFolder"].is_string())
        Storage::SetFolder(FromUtf8(j["storageFolder"].get<std::string>()));
    if (j.value("hideAll", false)) FenceManager::SetAllHidden(true);
    return true;
}

void RevealFile() {
    SaveNow();   // never reveal a file that does not exist yet
    std::wstring args = L"/select,\"" + Path() + L"\"";
    ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(),
                  nullptr, SW_SHOWNORMAL);
}

bool CollapseOnStart() { return g_collapseOnStart; }

void SetCollapseOnStart(bool on) {
    g_collapseOnStart = on;
    MarkDirty();
}

// Name of the autostart value under HKCU\...\Run.
static const wchar_t* const kRunValue = L"Open Fences";

// Versions before the rename kept everything under the name "openFences": move
// the settings folder and the autostart entry to "Open Fences" once.
void MigrateLegacyData() {
    if (AppPaths::IsPortable()) return;   // a portable copy has no older installed data
    wchar_t appdata[MAX_PATH] = {};
    SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appdata);
    std::wstring oldDir = std::wstring(appdata) + L"\\openFences";
    std::wstring newDir = std::wstring(appdata) + L"\\Open Fences";
    if (GetFileAttributesW(oldDir.c_str()) != INVALID_FILE_ATTRIBUTES &&
        GetFileAttributesW(newDir.c_str()) == INVALID_FILE_ATTRIBUTES)
        MoveFileW(oldDir.c_str(), newDir.c_str());

    static const wchar_t* const kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    wchar_t cmd[MAX_PATH + 4] = {};
    DWORD cb = sizeof(cmd) - sizeof(wchar_t);
    if (RegGetValueW(HKEY_CURRENT_USER, kRunKey, L"openFences", RRF_RT_REG_SZ, nullptr, cmd, &cb) == ERROR_SUCCESS) {
        HKEY key = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &key) == ERROR_SUCCESS) {
            RegSetValueExW(key, kRunValue, 0, REG_SZ, (const BYTE*)cmd, (DWORD)((wcslen(cmd) + 1) * sizeof(wchar_t)));
            RegDeleteValueW(key, L"openFences");
            RegCloseKey(key);
        }
    }
}

bool AutoStartEnabled() {
    return RegGetValueW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        kRunValue, RRF_RT_REG_SZ, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
}

void SetAutoStart(bool enable) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
            0, KEY_SET_VALUE, &key) != ERROR_SUCCESS)
        return;
    if (enable) {
        wchar_t exe[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        // Quote the path — it may contain spaces (C:\Program Files\…).
        std::wstring cmd = std::wstring(L"\"") + exe + L"\"";
        RegSetValueExW(key, kRunValue, 0, REG_SZ,
            (const BYTE*)cmd.c_str(),
            (DWORD)((cmd.size() + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(key, kRunValue);   // absent = disabled
    }
    RegCloseKey(key);
}

} // namespace Config
