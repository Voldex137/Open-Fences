# Localization

openFences looks up every user-visible string by **key** in a per-language JSON file.

## Files

```
app/resources/lang/
├── en.json      English (reference language and fallback)
├── ru.json      Русский
└── zh-CN.json   中文
```

Each file looks like this:

```json
{
  "_meta": { "code": "ru", "name": "Русский" },

  "tray.exit": "Выход",
  "fence.rename": "Переименовать"
}
```

- `_meta.code` — language tag: a short code (`en`, `ru`) or code + region (`zh-CN`, `pt-BR`). It is stored in `config.json`.
- `_meta.name` — the name of the language **in that language**; it is what the tray "Language" menu shows. The menu is built from the files found at startup, so no code knows the list of languages.
- Every other key is a string. Keys that start with `_` are ignored.
- A key missing from a language falls back to English, then to the key text itself.

## Adding a language

1. Copy `app/resources/lang/en.json` to `app/resources/lang/<code>.json`.
2. Set `_meta.code` and `_meta.name`, translate the values (keep the keys unchanged).
3. Rebuild (CMake picks the new file up automatically) — the file is embedded in `openfences.exe` and the language appears in the tray menu.

To try a translation **without rebuilding**, put the file in `lang\` next to `openfences.exe` or in `%APPDATA%\Open Fences\lang\`. A file there replaces the embedded language with the same code.

## Which language is used

1. The language saved in `config.json` (`"language": "ru"`). Old configs stored a number (`0` = English, `1` = Chinese); it is converted on load.
2. Otherwise the best match for the Windows display languages, in the user's priority order: an exact tag (`zh-CN`), then the same primary language (`ru-RU` → `ru`, `en-GB` → `en`).
3. Otherwise English.

## Using strings in code

```cpp
#include "localization.h"
AppendMenuW(menu, MF_STRING, id, L10n::Tr("tray.exit"));
```

`Tr` does not allocate, so it is safe to call while painting. Add the new key to **every** file in `app/resources/lang`; `tests/l10n_test.cpp` checks that the main keys exist in all languages
(`cmake -B build -DOPENFENCES_BUILD_TESTS=ON`, then build and run `l10n_test`).
