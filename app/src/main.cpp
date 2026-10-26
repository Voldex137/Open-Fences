#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>

#include "core/config.h"
#include "core/localization.h"
#include "fence/fence_manager.h"
#include "render/render.h"
#include "shell/desktop_host.h"
#include "shell/desktop_icons.h"
#include "shell/shell_integration.h"
#include "shell/desktop_selection.h"
#include "tray/tray.h"

namespace {

HWND g_owner = nullptr;
UINT g_taskbarCreated = 0;   // broadcast when explorer (re)starts

// Owner-window timers. Config::MarkDirty arms the save timer (id 3) itself.
constexpr UINT_PTR kTimerDesktopWatch = 2;   // icon visibility / icon size / anchors, every 300 ms
constexpr UINT_PTR kTimerConfigSave   = 3;   // debounced config save
constexpr UINT_PTR kTimerSelection    = 6;   // 40 ms: mirrors the desktop rubber band onto folded tiles
constexpr int kHotkeyToggleIcons = 1;        // Ctrl+Shift+H

// Every 300 ms: keep the fences alive across explorer's own icon hide/show,
// and follow the desktop icon size and position.
void OnDesktopWatchTimer() {
    auto& fences = FenceManager::All();
    DesktopIcons::WatchExternalVisibility(FenceManager::Handles());
    // One live measurement per tick, shared by all fences (the query opens
    // explorer's process, not worth repeating per fence).
    int px = QueryDesktopIconSizePx();
    for (auto& f : fences) {
        // Display topology changes move the desktop icon list; keep every
        // fence at its saved screen position.
        f->RestoreAnchor();
        if (px > 0) f->SyncDesktopIconSize(px);
    }
}

LRESULT CALLBACK OwnerWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == g_taskbarCreated && g_taskbarCreated) {
        FenceManager::Rebuild();
        return 0;
    }
    switch (msg) {
    case WM_CREATE:
        Tray::AddIcon(hwnd);
        return 0;
    case Tray::kMessage:
        if (LOWORD(lp) == WM_RBUTTONUP) Tray::ShowMenu(hwnd);
        return 0;
    case WM_HOTKEY:
        if (wp == kHotkeyToggleIcons) DesktopIcons::Toggle(FenceManager::Handles());
        return 0;
    case WM_DISPLAYCHANGE:
    case WM_SETTINGCHANGE:
        // Resolution / personalization broadcasts only reach top-level
        // windows; the fences are children of explorer's desktop tree, so fan
        // out here. Each fence debounces the actual re-capture on its own
        // timer; without this the frosted glass keeps showing stale wallpaper
        // pixels after a resolution switch.
        for (auto& f : FenceManager::All()) f->ScheduleBackdropRefresh();
        return 0;
    case WM_TIMER:
        if (wp == kTimerDesktopWatch) OnDesktopWatchTimer();
        else if (wp == kTimerConfigSave) Config::SaveNow();
        else if (wp == kTimerSelection) DesktopSelection::Poll();
        return 0;
    case WM_COPYDATA: {   // a command forwarded by a second process (Explorer menu entry)
        auto* cds = (COPYDATASTRUCT*)lp;
        if (cds && cds->dwData == 1 && cds->lpData && cds->cbData >= sizeof(wchar_t))
            ShellIntegration::Execute(std::wstring((const wchar_t*)cds->lpData));
        return TRUE;
    }
    case WM_FENCE_DELETE:
        FenceManager::Remove((HWND)wp);
        return 0;
    case WM_DESTROY:
        Config::SaveNow();   // safety net; the tray Exit path saves too
        Tray::RemoveIcon(hwnd);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int) {
    // A second instance would draw the fences twice on the same desktop; a named
    // mutex allows only one (the system releases it if the process crashes, so it
    // cannot leave a stale lock).
    // Instructions from the Explorer menu entries come first: "--unregister-shell" (the
    // uninstaller), and commands a running instance should carry out for us.
    if (wcsstr(GetCommandLineW(), L"--unregister-shell")) {
        ShellIntegration::Unregister();
        return 0;
    }
    ShellIntegration::Command command;
    bool hasCommand = ShellIntegration::ParseCommandLine(command);
    if (hasCommand && ShellIntegration::ForwardToRunningInstance(command)) return 0;

    HANDLE singleInstance = CreateMutexW(nullptr, TRUE, L"openFences_SingleInstance");
    if (!singleInstance || GetLastError() == ERROR_ALREADY_EXISTS) return 0;

    Config::MigrateLegacyData();   // before anything reads the settings
    Desktop::EnableDpiAwareness();
    ComInit com;
    InitCommonControls();
    FenceWindow::RegisterClass(hInst);

    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = OwnerWndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"OpenFencesOwner";
    RegisterClassExW(&wc);

    g_owner = CreateWindowExW(0, L"OpenFencesOwner", nullptr, WS_POPUP,
        0, 0, 0, 0, nullptr, nullptr, hInst, nullptr);

    FenceWindow::SetOwner(g_owner);
    RegisterHotKey(g_owner, kHotkeyToggleIcons, MOD_CONTROL | MOD_SHIFT, 'H');
    g_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");

    // Without a saved language the UI follows the system language (English as fallback).
    L10n::Init();
    L10n::SetLanguage(L10n::DetectSystemLanguage());

    // Restore the persisted layout (language, fences, icons, appearance). A
    // first run, an unreadable config and an empty saved list all start with no
    // fences; the user creates them from the tray menu.
    Config::LoadApp();
    if (hasCommand) ShellIntegration::Execute(command.text);   // started by an Explorer menu entry
    ShellIntegration::Refresh();   // panel list / language / exe path may have changed

    // If explorer's icons are hidden right now (we restarted, or the user
    // hid them before launching us), park immediately instead of sitting
    // invisible under the hidden list until the first timer tick.
    DesktopIcons::WatchExternalVisibility(FenceManager::Handles());

    // Follow desktop icon zoom live (Ctrl+wheel / View menu): each tick
    // measures the icon list's live glyph size out of explorer's address
    // space (the registry value is persisted lazily and lags wheel zoom) and
    // every fence re-adopts it. Each fence early-outs unless the size changed.
    SetTimer(g_owner, kTimerDesktopWatch, 300, nullptr);
    SetTimer(g_owner, kTimerSelection, 40, nullptr);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    FenceManager::All().clear();
    return 0;
}
