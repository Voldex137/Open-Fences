# Bugs and smells found while refactoring

Fixed ones are kept as a record. Newest first.

## Fixed

- **Debug log written to a hard-coded path.** `NewDbg` appended to `D:\person\openFences\newmenu_dbg.log` on every
  menu/keyboard event (a "TEMP DEBUG" leftover of the original author). Removed together with all its call sites.
- **Address of a temporary** (`&NOTIFYICONDATAW{...}`) in the tray exit code: rejected by the current MSVC
  conformance mode. Uses a local variable now.
- **Hard-coded Chinese UI by default** and three starter fences created on the first run. The language now follows
  the system language; a first run starts with no fences.
- **Prebuilt binaries committed** (`cpp/build`, `cpp/hook/build`): removed and git-ignored.
- **`DpiScale()` copied into three files** (`fence_window.cpp`, `context_menu.cpp`, `settings_panel.cpp`):
  one shared `Desktop::DpiScale()` now.
- **Unused sort command ids** (`kCmdSortBy*`) next to the shell menu code: removed.

## Open

(none yet)
