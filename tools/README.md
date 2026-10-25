# tools/

One-off research programs the original author used to probe Windows behaviour while designing openFences. They are **not** part of the application and are not built by default. Enable them with `-DOPENFENCES_BUILD_TOOLS=ON`.

| File | Question it answers |
|------|---------------------|
| `ctxmenu_spike.cpp` | Can the Windows 11 "modern" context menu (`IExplorerCommand`) for a file be enumerated and re-rendered? |
| `region_spike.cpp` | Can the desktop list view's window region be blanked to hide icons while the fence child windows stay visible and clickable? |
| `vis_probe.cpp` | What exactly does Explorer hide when "Show desktop icons" is turned off? |
| `desktop_diag.cpp` | Desktop window hierarchy diagnostic for Windows 11 |
| `icon_diag.cpp` | What does the shell image list actually contain for a file (JUMBO / EXTRALARGE / LARGE)? |
| `test_newmenu.cpp` | Temporary probe of the shell "New" menu handler's `InvokeCommand` contract (not wired into CMake) |
