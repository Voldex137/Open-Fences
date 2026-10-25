# Bugs and smells found while refactoring

Fixed ones are kept as a record. Newest first.

## Fixed

- **Untranslated UI strings in the renderer:** "Search…", "Loading…" and "No results" were hard-coded English;
  they are localization keys now (`fence.search_hint`, `fence.loading`, `fence.no_results`).
- **Icon extraction for rows outside the viewport:** list mode loaded the shell icon of every row, including rows
  scrolled out of view; it now loads only the visible ones.
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

- **Brushes allocated on every repaint.** `render_draw.cpp` creates solid-colour brushes (selection plates,
  search box, caret, rename box, scrollbar, drag-over ring) per frame, although the project's own rule is "no
  per-frame heap allocations" and the title bar / marquee / chevron already use cached brushes. Candidates for a
  small brush cache keyed by colour.
