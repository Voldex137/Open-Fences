#include "language.h"

#include <windows.h>
#include <string>

namespace Language {

int DetectSystem() {
    // Preferred list first: its head is the language the user actually sees.
    ULONG count = 0, size = 0;
    if (GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, nullptr, &size) && size > 1) {
        std::wstring names(size, L'\0');
        if (GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, names.data(), &size) &&
            names.compare(0, 2, L"zh") == 0)
            return LANG_ID_ZH;
        if (names[0] != L'\0') return LANG_ID_EN;
    }

    // Older/unusual setups: fall back to the numeric UI language id.
    LANGID id = GetUserDefaultUILanguage();
    return PRIMARYLANGID(id) == LANG_CHINESE ? LANG_ID_ZH : LANG_ID_EN;
}

}  // namespace Language
