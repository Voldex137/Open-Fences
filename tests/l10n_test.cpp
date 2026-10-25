// Checks the localization layer against the real files in resources/lang
// (copied next to the test exe by CMake as "lang\").
#include "core/localization.h"

#include <windows.h>
#include <cstdio>
#include <string>

static int g_failures = 0;

static void Check(bool ok, const char* what) {
    std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

int main() {
    L10n::Init();
    const auto& langs = L10n::Languages();

    Check(langs.size() >= 3, "at least en, ru and zh-CN are loaded");
    Check(!langs.empty() && langs[0].code == L"en", "English is listed first");

    // Every language must define every key that English defines.
    const char* keys[] = {
        "tray.new_fence", "tray.exit", "fence.rename", "fence.sort.name",
        "dialog.map_folder.confirm", "settings.title", "settings.mode.grid",
    };
    for (const auto& lang : langs) {
        Check(L10n::SetLanguage(lang.code), "SetLanguage accepts a loaded language");
        for (const char* key : keys)
            Check(std::wstring(L10n::Tr(key)) != std::wstring(key, key + strlen(key)),
                  key);
    }

    L10n::SetLanguage(L"ru");
    Check(std::wstring(L10n::Tr("tray.exit")) == L"Выход", "ru translation is used");
    L10n::SetLanguage(L"zh-CN");
    Check(std::wstring(L10n::Tr("tray.exit")) == L"退出", "zh-CN translation is used");

    Check(!L10n::SetLanguage(L"xx"), "unknown language is rejected");
    Check(L10n::CurrentCode() == L"zh-CN", "a rejected switch keeps the current language");

    Check(std::wstring(L10n::Tr("no.such.key")) == L"no.such.key", "missing key returns the key");

    std::wstring detected = L10n::DetectSystemLanguage();
    bool known = false;
    for (const auto& lang : langs) known = known || lang.code == detected;
    Check(known, "system language detection returns a loaded language");
    std::wprintf(L"detected system language: %ls\n", detected.c_str());

    std::printf(g_failures ? "\n%d check(s) failed\n" : "\nall checks passed\n", g_failures);
    return g_failures ? 1 : 0;
}
