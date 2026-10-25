#include "tray/tray.h"
#include "core/config.h"
#include "core/localization.h"
#include "core/resource.h"
#include "fence/fence_manager.h"
#include "menu/context_menu.h"
#include "render/menu_icons.h"
#include "shell/desktop_icons.h"

#include <shellapi.h>
#include <shlobj.h>
#include <shlguid.h>
#include <string>

namespace Tray {

namespace {

enum TrayCmd : int {
    kCmdNewFence          = 100,
    kCmdToggleFences      = 101,
    kCmdExit              = 102,
    kCmdToggleIcons       = 103,
    kCmdAutoStart         = 112,
    kCmdConfigLocation    = 113,
    kCmdCreateShortcut    = 114,
    kCmdLanguageBase      = 200,   // one command per loaded language
};

void CreateDesktopShortcut() {
    wchar_t desktop[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_DESKTOPDIRECTORY, nullptr, 0, desktop)))
        return;
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring lnk = std::wstring(desktop) + L"\\openFences.lnk";

    IShellLinkW* sl = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                IID_IShellLinkW, (void**)&sl)))
        return;
    sl->SetPath(exe);
    sl->SetDescription(L"openFences");
    IPersistFile* pf = nullptr;
    if (SUCCEEDED(sl->QueryInterface(IID_IPersistFile, (void**)&pf))) {
        pf->Save(lnk.c_str(), TRUE);
        pf->Release();
    }
    sl->Release();
}

HMENU BuildMenu() {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, kCmdNewFence, L10n::Tr("tray.new_fence"));
    AppendMenuW(menu, MF_STRING, kCmdToggleFences, FenceManager::AllHidden()
        ? L10n::Tr("tray.show_all_fences")
        : L10n::Tr("tray.hide_all_fences"));
    AppendMenuW(menu, MF_STRING, kCmdToggleIcons, DesktopIcons::HiddenByUs()
        ? L10n::Tr("tray.show_desktop_icons")
        : L10n::Tr("tray.hide_desktop_icons"));

    // Language is a global setting, so it lives here (tray) rather than in the
    // per-fence appearance panel.
    HMENU langMenu = CreatePopupMenu();
    const auto& languages = L10n::Languages();
    for (size_t i = 0; i < languages.size(); ++i)
        AppendMenuW(langMenu,
            MF_STRING | (languages[i].code == L10n::CurrentCode() ? MF_CHECKED : 0),
            kCmdLanguageBase + (UINT)i, languages[i].name.c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)langMenu, L10n::Tr("tray.language"));
    AppendMenuW(menu, MF_STRING | (Config::AutoStartEnabled() ? MF_CHECKED : 0),
        kCmdAutoStart, L10n::Tr("tray.start_with_windows"));
    AppendMenuW(menu, MF_STRING, kCmdConfigLocation, L10n::Tr("tray.config_location"));
    AppendMenuW(menu, MF_STRING, kCmdCreateShortcut, L10n::Tr("tray.create_shortcut"));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kCmdExit, L10n::Tr("tray.exit"));
    return menu;
}

// Fluent glyph icons, by menu position (0 New Fence, 1 Hide/Show All Fences,
// 2 Hide/Show Desktop Icons, 3 separator, 4 Language, 5 Start with Windows,
// 6 Config File Location, 7 separator, 8 Exit). The set owns the HBITMAPs
// until the menu interaction has fully returned (MIIM_BITMAP is by reference).
void AttachGlyphs(HMENU menu, GlyphBitmapSet& glyphs) {
    int size = MenuGlyphSizePx();
    auto icon = [&](UINT pos, MenuGlyph g) {
        HBITMAP b = glyphs.add(g, size);
        if (!b) return;
        MENUITEMINFOW mii = { sizeof(mii) };
        mii.fMask = MIIM_BITMAP;
        mii.hbmpItem = b;
        SetMenuItemInfoW(menu, pos, TRUE, &mii);
    };
    icon(0, MenuGlyph::Plus);
    icon(1, FenceManager::AllHidden() ? MenuGlyph::Eye : MenuGlyph::EyeOff);
    icon(2, MenuGlyph::Grid);
    icon(4, MenuGlyph::Globe);
    icon(5, MenuGlyph::Rocket);
    icon(6, MenuGlyph::Folder);
    icon(8, MenuGlyph::Power);
}

void Execute(HWND owner, int cmd) {
    const int languageCount = (int)L10n::Languages().size();
    if (cmd == kCmdNewFence) {
        FenceManager::CreateNew();
    } else if (cmd == kCmdToggleFences) {
        FenceManager::SetAllHidden(!FenceManager::AllHidden());
        Config::MarkDirty();
    } else if (cmd == kCmdToggleIcons) {
        DesktopIcons::Toggle(FenceManager::Handles());
    } else if (cmd >= kCmdLanguageBase && cmd < kCmdLanguageBase + languageCount) {
        L10n::SetLanguage(L10n::Languages()[cmd - kCmdLanguageBase].code);
        Config::MarkDirty();
    } else if (cmd == kCmdAutoStart) {
        Config::SetAutoStart(!Config::AutoStartEnabled());
    } else if (cmd == kCmdConfigLocation) {
        Config::RevealFile();
    } else if (cmd == kCmdCreateShortcut) {
        CreateDesktopShortcut();
    } else if (cmd == kCmdExit) {
        Config::SaveNow();   // do not ride on the debounce for a real exit
        RemoveIcon(owner);
        PostQuitMessage(0);
    }
}

}  // namespace

void AddIcon(HWND owner) {
    NOTIFYICONDATAW nid = { sizeof(nid) };
    nid.hWnd = owner; nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = kMessage;
    nid.hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_APP));
    if (!nid.hIcon) nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(nid.szTip, L"openFences");
    Shell_NotifyIconW(NIM_ADD, &nid);
    // Bust the tray icon cache so a rebuilt exe with a new icon is picked up
    // immediately (Windows keys the cache by executable path).
    SHChangeNotify(SHCNE_UPDATEIMAGE, SHCNF_DWORD,
                   (LPVOID)(DWORD_PTR)GetCurrentProcessId(), nullptr);
}

void RemoveIcon(HWND owner) {
    NOTIFYICONDATAW nid{ sizeof(NOTIFYICONDATAW), owner, 1 };
    Shell_NotifyIconW(NIM_DELETE, &nid);
}

void ShowMenu(HWND owner) {
    POINT pt;
    GetCursorPos(&pt);
    HMENU menu = BuildMenu();
    GlyphBitmapSet glyphs;
    AttachGlyphs(menu, glyphs);

    SetForegroundWindow(owner);
    // Prefer the Win11 Fluent rendering; fall back to the classic menu only
    // if it could not be built (-1). The Fluent loop swallows the click that
    // dismisses the menu, exactly right over the desktop, where a leaked
    // press would start a selection band or icon drag.
    int cmd = FluentMenu::Run(owner, menu, pt);
    if (cmd < 0)
        cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, owner, nullptr);
    DestroyMenu(menu);   // also destroys the attached language submenu
    Execute(owner, cmd);
}

}  // namespace Tray
