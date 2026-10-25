#pragma once

// UI language codes as stored in config.json ("language"); Russian will extend
// this list.
enum LanguageId { LANG_ID_EN = 0, LANG_ID_ZH = 1 };

namespace Language {
// Picks the UI language from the Windows display language. Anything that is
// not supported, or that cannot be determined, resolves to English.
int DetectSystem();
}
