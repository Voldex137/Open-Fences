#pragma once

#include <string>
#include <vector>

// Key-based UI localization.
//
// Strings live in JSON files, one per language (resources/lang/<code>.json).
// The files are embedded in the exe at build time, and any *.json found in
// "<exe dir>\lang\" or "%APPDATA%\openFences\lang\" is loaded on top, so a
// translation can be added or tested without rebuilding. English is the
// reference language and the fallback for missing keys.
namespace L10n {

struct Language {
    std::wstring code;   // "en", "ru", "zh-CN"
    std::wstring name;   // shown in the language menu, in that language
};

// Loads the embedded and external language files. Call once before Tr().
void Init();

// Available languages, in the order shown in the tray menu.
const std::vector<Language>& Languages();

// Active language code (always one of Languages(), "en" if nothing loaded).
const std::wstring& CurrentCode();

// Switches the language. Returns false (and keeps the current one) for an
// unknown code.
bool SetLanguage(const std::wstring& code);

// Language that best matches the Windows display languages, else "en".
std::wstring DetectSystemLanguage();

// Translation for `key`: active language, then English, then the key itself.
// The pointer stays valid until the next SetLanguage()/Init().
const wchar_t* Tr(const char* key);

}  // namespace L10n
