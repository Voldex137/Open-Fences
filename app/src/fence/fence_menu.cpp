#include "fence/fence_window.h"
#include "core/localization.h"
#include "settings/settings_panel.h"
#include "menu/context_menu.h"
#include "render/menu_icons.h"
#include "core/config.h"
#include "core/storage.h"
#include "menu/shell_context_menu.h"
#include <windowsx.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <algorithm>
#include <functional>
#include <vector>

namespace {

// Command ids of the fence menu. The shell "New" submenu owns everything from
// ShellMenu::kCmdNewFirst upwards, so these stay below it.
enum FenceMenuCmd : int {
    kCmdRename       = 1,
    kCmdDeleteFence  = 2,
    kCmdRemoveIcon   = 3,
    kCmdAppearance   = 4,
    kCmdMapFolder    = 5,
    kCmdUnmapFolder  = 6,
    kCmdSortName     = 10,
    kCmdSortDate     = 11,
    kCmdSortType     = 12,
    kCmdSortSize     = 13,
    kCmdSortAscend   = 14,
    kCmdSortDescend  = 15,
};

// Sort-column ids used by FenceWindow::SetSort, indexed by the sort command
// (Name, Date, Type, Size).
int SortColumnOf(int cmd) {
    switch (cmd) {
    case kCmdSortName: return 0;
    case kCmdSortDate: return 1;
    case kCmdSortType: return 3;
    default:           return 2;   // kCmdSortSize
    }
}

void AttachGlyph(GlyphBitmapSet& glyphs, int sizePx, HMENU menu, UINT item, bool byPosition,
                 MenuGlyph glyph) {
    HBITMAP bmp = glyphs.add(glyph, sizePx);
    if (!bmp) return;
    MENUITEMINFOW mii = { sizeof(mii) };
    mii.fMask = MIIM_BITMAP;
    mii.hbmpItem = bmp;
    SetMenuItemInfoW(menu, item, byPosition ? TRUE : FALSE, &mii);
}

}  // namespace

// Remove icon `idx` from the fence, repairing the selection/hover bookkeeping.
void FenceWindow::RemoveIconAt(int idx) {
    if (idx < 0 || idx >= (int)m_icons.size()) return;
    // Drop the removed path from the selection set so no entry
    // dangles pointing at a dead icon.
    // A file that was moved into the storage folder goes back to the desktop.
    if (Storage::IsStored(m_icons[idx].path)) Storage::ReturnToDesktop(m_icons[idx].path);
    auto& sel = m_selectedPaths;
    sel.erase(std::remove(sel.begin(), sel.end(), m_icons[idx].path), sel.end());
    m_icons.erase(m_icons.begin() + idx);
    if (m_hoverIcon == idx)      m_hoverIcon = -1;
    else if (m_hoverIcon > idx)  m_hoverIcon--;
    if (SearchActive()) RebuildFilter();
    Redraw();
    Config::MarkDirty();
}

LRESULT FenceWindow::OnRButtonUp(UINT msg, WPARAM wp, LPARAM lp) {
    POINT cli{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
    POINT screen = cli;
    ClientToScreen(m_hwnd, &screen);

    // Detect which icon was clicked (same geometry as rendering)
    int iconHit = m_icons.empty() ? -1 : HitTestIcon(cli.x, cli.y);

    // Right-click selects too, so the menu appears over the selection
    // plate exactly like the Explorer desktop. A press on an icon that is
    // ALREADY selected keeps the whole multi-selection (Explorer keeps the
    // group intact so the menu verbs apply to every member); otherwise
    // the selection collapses to the pressed icon.
    if (iconHit >= 0) {
        if (!IsPathSelected(m_icons[iconHit].path))
            SelectOnly(iconHit);
        m_hoverIcon = iconHit;
        Redraw();
    }

    // Icon → the real Explorer context menu; if the shell cannot provide
    // one, fall back to the fence menu.
    m_modalUi = true;   // the menu lives in other windows; a click there is not "outside"
    if (iconHit >= 0 && ShowIconShellMenu(iconHit, screen)) {
        m_modalUi = false;
        return 0;
    }

    ShowFenceMenu(iconHit, screen, cli);
    m_modalUi = false;
    return 0;
}

// Explorer's menu for one icon (Open / Cut / Copy / Delete / Properties ...)
// with our fence-only "remove from fence" verb appended. Returns false when the
// shell could not produce a menu.
bool FenceWindow::ShowIconShellMenu(int iconHit, POINT screenPt) {
    bool invoked = false, removeChosen = false, renameChosen = false;

    // The menu applies to the whole selection when the clicked icon is part of it.
    // The shell builds one menu per folder, so only the selected icons that sit in
    // the clicked icon's folder join the menu; "remove" still covers all of them.
    auto parentOf = [](const std::wstring& p) {
        size_t cut = p.find_last_of(L"\\/");
        return cut == std::wstring::npos ? std::wstring() : p.substr(0, cut);
    };
    std::wstring clickedParent = parentOf(m_icons[iconHit].path);
    std::vector<std::wstring> paths{ m_icons[iconHit].path };
    std::vector<int> selected;   // every selected icon, the clicked one first
    selected.push_back(iconHit);
    for (int i = 0; i < (int)m_icons.size(); i++) {
        if (i == iconHit || !IsPathSelected(m_icons[i].path)) continue;
        selected.push_back(i);
        if (_wcsicmp(parentOf(m_icons[i].path).c_str(), clickedParent.c_str()) == 0)
            paths.push_back(m_icons[i].path);
    }

    if (!ShellMenu::RunForPaths(m_hwnd, paths, m_icons[iconHit].name, screenPt,
                                invoked, removeChosen, renameChosen))
        return false;

    if (renameChosen) {
        // Rename right here in the fence instead of letting the
        // shell rename the desktop/Explorer copy.
        SelectOnly(iconHit);
        EnterIconRename(iconHit);
    } else if (removeChosen) {
        // Highest index first, so the remaining indices stay valid.
        std::sort(selected.begin(), selected.end(), std::greater<int>());
        for (int idx : selected) RemoveIconAt(idx);
    } else if (invoked) {
        // If the shell command got rid of files (e.g. Delete),
        // don't leave dangling icons behind in the fence.
        std::sort(selected.begin(), selected.end(), std::greater<int>());
        for (int idx : selected)
            if (idx < (int)m_icons.size() &&
                GetFileAttributesW(m_icons[idx].path.c_str()) == INVALID_FILE_ATTRIBUTES)
                RemoveIconAt(idx);
    }
    return true;
}

// Fence-level menu: background click, or the fallback when the shell menu fails.
void FenceWindow::ShowFenceMenu(int iconHit, POINT screenPt, POINT clientPt) {
    HMENU menu = CreatePopupMenu();
    if (iconHit >= 0) {
        std::wstring remove = std::wstring(L10n::Tr("fence.remove")) + L" \"" +
                              m_icons[iconHit].name + L"\"";
        AppendMenuW(menu, MF_STRING, kCmdRemoveIcon, remove.c_str());
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    }

    AppendMenuW(menu, MF_STRING, kCmdRename, L10n::Tr("fence.rename"));
    AppendMenuW(menu, MF_STRING, kCmdAppearance, L10n::Tr("fence.appearance"));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    // Explorer's own "New" submenu, queried straight into the fence menu.
    // Mapped fences create into their folder; unmapped ones point the
    // handler at the desktop PIDL itself so the entries match the system
    // desktop menu exactly (e.g. "Shortcut").
    PIDLIST_ABSOLUTE pidlNewFolder = nullptr;
    if (IsMapped())
        SHParseDisplayName(m_sourceFolder.c_str(), nullptr, &pidlNewFolder, 0, nullptr);
    else
        SHGetSpecialFolderLocation(nullptr, CSIDL_DESKTOP, &pidlNewFolder);
    ComPtr<IContextMenu> newMenu;
    UINT newMenuPos = 0;
    bool newMenuAdded = false;
    if (pidlNewFolder && SUCCEEDED(ShellMenu::CreateNewMenuHandler(pidlNewFolder, &newMenu))) {
        newMenuPos = (UINT)GetMenuItemCount(menu);
        HRESULT hr = newMenu->QueryContextMenu(menu, newMenuPos, (UINT)ShellMenu::kCmdNewFirst,
                                               0x7FFF, CMF_NORMAL);
        if (SUCCEEDED(hr)) {
            // The handler fills its submenu lazily on WM_INITMENUPOPUP —
            // the same message Explorer forwards before showing it.
            HMENU newSub = GetSubMenu(menu, newMenuPos);
            if (newSub)
                ShellMenu::ForwardMenuMessage(newMenu.Get(), WM_INITMENUPOPUP, (WPARAM)newSub, 0);
            newMenuAdded = true;
            ShellMenu::SetActiveNewMenu(newMenu.Get());
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        } else {
            newMenu.Reset();
        }
    }

    AppendMenuW(menu, MF_STRING, kCmdDeleteFence, L10n::Tr("fence.delete"));

    // Sort-by submenu (mapped fences only)
    if (!m_sourceFolder.empty()) {
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        HMENU sortMenu = CreatePopupMenu();
        auto addSortItem = [&](UINT id, const char* key) {
            UINT flags = MF_STRING;
            if (m_sortColumn == SortColumnOf((int)id)) flags |= MF_CHECKED;
            AppendMenuW(sortMenu, flags, id, L10n::Tr(key));
        };
        addSortItem(kCmdSortName, "fence.sort.name");
        addSortItem(kCmdSortDate, "fence.sort.date_modified");
        addSortItem(kCmdSortType, "fence.sort.type");
        addSortItem(kCmdSortSize, "fence.sort.size");
        AppendMenuW(sortMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(sortMenu, MF_STRING | (m_sortAscending ? MF_CHECKED : 0),
                    kCmdSortAscend, L10n::Tr("fence.sort.ascending"));
        AppendMenuW(sortMenu, MF_STRING | (!m_sortAscending ? MF_CHECKED : 0),
                    kCmdSortDescend, L10n::Tr("fence.sort.descending"));
        AppendMenuW(menu, MF_POPUP, (UINT_PTR)sortMenu, L10n::Tr("fence.sort_by"));
    }

    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kCmdMapFolder, L10n::Tr("fence.map_folder"));
    if (!m_sourceFolder.empty())
        AppendMenuW(menu, MF_STRING, kCmdUnmapFolder, L10n::Tr("fence.unmap_folder"));

    // Fluent glyph icons. The set owns the HBITMAPs until the menu
    // interaction below has fully returned (MIIM_BITMAP is by reference).
    GlyphBitmapSet glyphs;
    {
        int gsz = MenuGlyphSizePx();
        if (iconHit >= 0) AttachGlyph(glyphs, gsz, menu, kCmdRemoveIcon, false, MenuGlyph::Remove);
        AttachGlyph(glyphs, gsz, menu, kCmdRename, false, MenuGlyph::Pencil);
        AttachGlyph(glyphs, gsz, menu, kCmdAppearance, false, MenuGlyph::Sliders);
        // The New popup is addressed by position: the shell inserts it
        // without a command id of its own.
        if (newMenuAdded) AttachGlyph(glyphs, gsz, menu, newMenuPos, true, MenuGlyph::Plus);
        AttachGlyph(glyphs, gsz, menu, kCmdDeleteFence, false, MenuGlyph::Trash);
        AttachGlyph(glyphs, gsz, menu, kCmdMapFolder, false, MenuGlyph::Folder);
        if (!m_sourceFolder.empty())
            AttachGlyph(glyphs, gsz, menu, kCmdUnmapFolder, false, MenuGlyph::Remove);
    }

    SetForegroundWindow(m_hwnd);
    // Prefer the Win11 Fluent rendering; -1 means it could not be built
    // (empty model / D2D failure) → classic menu, the same fallback the
    // shell icon menu uses. The Fluent loop swallows the dismiss click,
    // which is exactly what a context menu must do over a surface where
    // a stray press would otherwise start a fence drag.
    int cmd = FluentMenu::Run(m_hwnd, menu, screenPt);
    if (cmd < 0)
        cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                             screenPt.x, screenPt.y, 0, m_hwnd, nullptr);

    switch (cmd) {
    case kCmdRename:      EnterRename(); break;
    case kCmdDeleteFence:
        if (s_owner) PostMessage(s_owner, WM_FENCE_DELETE, (WPARAM)m_hwnd, 0);
        break;
    case kCmdRemoveIcon:  RemoveIconAt(iconHit); break;
    case kCmdAppearance:  SettingsPanel::Run(this); break;
    case kCmdMapFolder:   ChooseAndMapFolder(); break;
    case kCmdUnmapFolder: UnmapFolder(); break;
    case kCmdSortName: case kCmdSortDate: case kCmdSortType: case kCmdSortSize: {
        int column = SortColumnOf(cmd);
        bool naturalAscending = (column == 0 || column == 3);   // Name/Type default ascending
        SetSort(column, naturalAscending);
        break;
    }
    case kCmdSortAscend:  SetSort(m_sortColumn, true); break;
    case kCmdSortDescend: SetSort(m_sortColumn, false); break;
    default:
        // An entry from the shell "New" submenu: hand it back to the
        // handler, which creates the file in the target folder.
        if (cmd > ShellMenu::kCmdNewFirst && cmd <= 0x7FFF && newMenu)
            CreateShellNewItem(newMenu.Get(), cmd, clientPt);
        break;
    }

    // Destroy only after dispatch: the shell "New" handler keeps its
    // cached submenu alive until InvokeCommand runs — destroying the
    // menu first makes it fail with E_FAIL.
    DestroyMenu(menu);
    ShellMenu::SetActiveNewMenu(nullptr);
    if (pidlNewFolder) CoTaskMemFree(pidlNewFolder);
}

// "Map Folder...": pick a folder, confirm (it clears the fence), then show the
// folder's contents in this fence.
void FenceWindow::ChooseAndMapFolder() {
    ComPtr<IFileOpenDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&dialog))))
        return;
    dialog->SetOptions(FOS_PICKFOLDERS | FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR);
    if (FAILED(dialog->Show(m_hwnd))) return;

    ComPtr<IShellItem> picked;
    if (FAILED(dialog->GetResult(&picked))) return;
    LPWSTR path = nullptr;
    if (FAILED(picked->GetDisplayName(SIGDN_FILESYSPATH, &path)) || !path) return;

    int answer = MessageBoxW(m_hwnd, L10n::Tr("dialog.map_folder.confirm"),
                             L10n::Tr("dialog.map_folder.title"),
                             MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
    if (answer == IDYES) {
        GetRender().Appearance().displayMode = 1;
        GetRender().RebuildStyles();
        m_loading = true;
        Redraw();
        // Force paint before blocking enumeration
        GetRender().BeginDraw();
        GetRender().EndDraw();
        GetRender().Present(m_hwnd, m_x, m_y);
        MapToFolder(path);
        m_loading = false;
    }
    CoTaskMemFree(path);
}

// Runs one entry of the shell "New" submenu (new folder, text document, shortcut ...)
// and puts the created item into this fence, in rename mode.
void FenceWindow::CreateShellNewItem(IContextMenu* newMenu, int cmd, POINT clientPt) {
    std::wstring targetDir = m_sourceFolder;
    if (targetDir.empty()) {
        wchar_t buf[MAX_PATH] = {};
        if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_DESKTOPDIRECTORY, nullptr, 0, buf)))
            targetDir = buf;
    }

    std::vector<std::wstring> before, after;
    ShellMenu::SnapshotFolderNames(targetDir, before);
    CMINVOKECOMMANDINFO ici = {};
    ici.cbSize = sizeof(ici);
    ici.hwnd = m_hwnd;
    ici.lpVerb = MAKEINTRESOURCEA(cmd - ShellMenu::kCmdNewFirst);
    ici.nShow = SW_SHOWNORMAL;
    newMenu->InvokeCommand(&ici);
    ShellMenu::SnapshotFolderNames(targetDir, after);

    std::wstring newPath;
    for (const auto& name : after) {
        if (std::find(before.begin(), before.end(), name) == before.end()) {
            newPath = targetDir + L"\\" + name;
            break;
        }
    }
    if (newPath.empty()) return;

    if (IsMapped()) {
        // The watcher re-enumerates on its own; refresh
        // synchronously so the new file is visible and
        // can enter rename mode right away.
        ClearSearch();
        RefreshFromSource();
    } else {
        AddIcon(newPath.substr(newPath.find_last_of(L'\\') + 1), newPath,
                (float)clientPt.x, (float)clientPt.y);
    }
    for (int i = 0; i < (int)m_icons.size(); i++) {
        if (lstrcmpiW(m_icons[i].path.c_str(), newPath.c_str()) == 0) {
            SelectOnly(i);
            EnterIconRename(i);
            break;
        }
    }
}
