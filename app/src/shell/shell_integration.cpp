#include "shell/shell_integration.h"
#include "core/app_paths.h"
#include "core/com_ptr.h"
#include "core/localization.h"
#include "fence/fence_manager.h"
#include "fence/fence_window.h"

#include <windows.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <shlwapi.h>
#include <vector>

namespace ShellIntegration {

namespace {

bool g_enabled = !AppPaths::IsPortable();
// The entries live in HKCU\Software\Classes, merged into Explorer's menus.
const wchar_t* const kDesktopVerb = L"Software\\Classes\\DesktopBackground\\Shell\\OpenFences.NewFence";
const wchar_t* const kFileVerb    = L"Software\\Classes\\*\\shell\\OpenFences.MoveTo";
const wchar_t* const kDirVerb     = L"Software\\Classes\\Directory\\shell\\OpenFences.MoveTo";
const wchar_t* const kStore       = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\CommandStore\\shell";
const wchar_t* const kSubPrefix   = L"OpenFences.Panel.";

void SetValue(const std::wstring& subkey, const wchar_t* name, const std::wstring& value) {
    RegSetKeyValueW(HKEY_CURRENT_USER, subkey.c_str(), name, REG_SZ, value.c_str(),
                    (DWORD)((value.size() + 1) * sizeof(wchar_t)));
}

std::wstring ExePath() {
    wchar_t exe[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    return exe;
}

// "&" starts a mnemonic in classic menus; "&&" is a literal ampersand.
std::wstring Escape(const std::wstring& s) {
    std::wstring out;
    for (wchar_t c : s) { out.push_back(c); if (c == L'&') out.push_back(L'&'); }
    return out;
}

void DeleteTree(const std::wstring& subkey) {
    RegDeleteTreeW(HKEY_CURRENT_USER, subkey.c_str());
}

// Removes every sub-verb written by an earlier Refresh.
void DeleteSubVerbs() {
    HKEY store = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kStore, 0, KEY_READ, &store) != ERROR_SUCCESS) return;
    std::vector<std::wstring> names;
    wchar_t name[256];
    for (DWORD i = 0;; i++) {
        DWORD len = _countof(name);
        if (RegEnumKeyExW(store, i, name, &len, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
        if (wcsncmp(name, kSubPrefix, wcslen(kSubPrefix)) == 0) names.push_back(name);
    }
    RegCloseKey(store);
    for (const auto& n : names) DeleteTree(std::wstring(kStore) + L"\\" + n);
}

void Remove() {
    DeleteTree(kDesktopVerb);
    DeleteTree(kFileVerb);
    DeleteTree(kDirVerb);
    DeleteSubVerbs();
}

void WriteMoveVerb(const wchar_t* key, const std::wstring& exe, const std::wstring& subCommands) {
    SetValue(key, L"MUIVerb", L10n::Tr("shell.move_to_panel"));
    SetValue(key, L"Icon", exe + L",0");
    SetValue(key, L"SubCommands", subCommands);
}

}  // namespace

bool Enabled() { return g_enabled; }

void SetEnabled(bool on) {
    g_enabled = on;
    Refresh();
}

void Refresh() {
    if (!g_enabled) { Remove(); return; }
    const std::wstring exe = ExePath();

    // Desktop background: "Create panel".
    SetValue(kDesktopVerb, L"MUIVerb", L10n::Tr("shell.new_panel"));
    SetValue(kDesktopVerb, L"Icon", exe + L",0");
    SetValue(std::wstring(kDesktopVerb) + L"\\command", nullptr, L"\"" + exe + L"\" --new-fence");

    // Files and folders: "Move to panel" with one sub-verb per panel.
    DeleteSubVerbs();
    std::wstring list;
    int n = 0;
    for (const auto& f : FenceManager::All()) {
        FenceData d = f->GetData();
        std::wstring verb = kSubPrefix + std::to_wstring(++n);
        std::wstring key = std::wstring(kStore) + L"\\" + verb;
        SetValue(key, L"MUIVerb", Escape(d.title.empty() ? d.id : d.title));
        SetValue(key + L"\\command", nullptr, L"\"" + exe + L"\" --move-to " + d.id + L" \"%1\"");
        if (!list.empty()) list += L";";
        list += verb;
    }
    if (list.empty()) {
        DeleteTree(kFileVerb);
        DeleteTree(kDirVerb);
    } else {
        WriteMoveVerb(kFileVerb, exe, list);
        WriteMoveVerb(kDirVerb, exe, list);
    }
}

void Unregister() { Remove(); }

// ── Command line ──

bool ParseCommandLine(Command& out) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return false;
    bool found = false;
    for (int i = 1; i < argc; i++) {
        if (wcscmp(argv[i], L"--new-fence") == 0) {
            out.text = L"new";
            found = true;
        } else if (wcscmp(argv[i], L"--move-to") == 0 && i + 2 < argc) {
            out.text = std::wstring(L"move\t") + argv[i + 1] + L"\t" + argv[i + 2];
            found = true;
            i += 2;
        }
    }
    LocalFree(argv);
    return found;
}

bool ForwardToRunningInstance(const Command& cmd) {
    HWND owner = FindWindowW(L"OpenFencesOwner", nullptr);
    if (!owner) return false;
    COPYDATASTRUCT cds = {};
    cds.dwData = 1;
    cds.cbData = (DWORD)((cmd.text.size() + 1) * sizeof(wchar_t));
    cds.lpData = (PVOID)cmd.text.c_str();
    DWORD_PTR result = 0;
    SendMessageTimeoutW(owner, WM_COPYDATA, 0, (LPARAM)&cds, SMTO_ABORTIFHUNG, 5000, &result);
    return true;
}

void Execute(const std::wstring& text) {
    if (text == L"new") {
        FenceManager::CreateNew();
        return;
    }
    if (text.rfind(L"move\t", 0) != 0) return;
    size_t second = text.find(L'\t', 5);
    if (second == std::wstring::npos) return;
    std::wstring id = text.substr(5, second - 5);
    std::wstring path = text.substr(second + 1);

    for (auto& f : FenceManager::All()) {
        if (f->GetData().id != id) continue;
        // Same display name Explorer shows for the item.
        std::wstring name;
        ComPtr<IShellItem> item;
        if (SUCCEEDED(SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(&item))) && item) {
            LPWSTR display = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_NORMALDISPLAY, &display)) && display) name = display;
            CoTaskMemFree(display);
        }
        if (name.empty()) name = PathFindFileNameW(path.c_str());
        f->AddDroppedItem(name, path, 0.0f, 0.0f);
        return;
    }
}

}  // namespace ShellIntegration
