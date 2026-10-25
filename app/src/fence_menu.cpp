#include "fence_window.h"
#include "fence_metrics.h"
#include "desktop_host.h"
#include "localization.h"
#include "settings_panel.h"
#include "context_menu.h"
#include "menu_icons.h"
#include "config.h"
#include "explorer_sort.h"
#include "shell_context_menu.h"
#include <windowsx.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shlwapi.h>
#include <commctrl.h>
#include <algorithm>
#include <cmath>

LRESULT FenceWindow::OnRButtonUp(UINT msg, WPARAM wp, LPARAM lp)     {
    POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
    ClientToScreen(m_hwnd, &pt);
    POINT cli{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };

    // Detect which icon was clicked (same geometry as rendering)
    int iconHit = -1;
    if (!m_icons.empty())
        iconHit = HitTestIcon(cli.x, cli.y);

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

    // Remove icon `idx`, repairing selection/hover bookkeeping.
    auto eraseIcon = [&](int idx) {
        if (idx < 0 || idx >= (int)m_icons.size()) return;
        // Drop the removed path from the selection set so no entry
        // dangles pointing at a dead icon.
        auto& sel = m_selectedPaths;
        sel.erase(std::remove(sel.begin(), sel.end(), m_icons[idx].path),
                  sel.end());
        m_icons.erase(m_icons.begin() + idx);
        if (m_hoverIcon == idx)      m_hoverIcon = -1;
        else if (m_hoverIcon > idx)  m_hoverIcon--;
        if (SearchActive()) RebuildFilter();
        Redraw();
        Config::MarkDirty();
    };

    // Icon → show the real Explorer context menu (Open / Cut / Copy /
    // Delete / Properties …) with our fence-only verb appended.
    if (iconHit >= 0) {
        bool invoked = false, removeChosen = false, renameChosen = false;
        if (ShellMenu::RunForPath(m_hwnd, m_icons[iconHit].path,
                                m_icons[iconHit].name, pt,
                                invoked, removeChosen, renameChosen)) {
            if (renameChosen) {
                // Rename right here in the fence instead of letting the
                // shell rename the desktop/Explorer copy.
                SelectOnly(iconHit);
                EnterIconRename(iconHit);
            } else if (removeChosen) {
                eraseIcon(iconHit);
            } else if (invoked) {
                // If the shell command got rid of the file (e.g. Delete),
                // don't leave a dangling icon behind in the fence.
                if (GetFileAttributesW(m_icons[iconHit].path.c_str())
                        == INVALID_FILE_ATTRIBUTES)
                    eraseIcon(iconHit);
            }
            return 0;
        }
        // Shell menu unavailable — fall through to the fence menu below.
    }

    // Fence-level menu (background, or fallback when the shell menu fails)
    HMENU menu = CreatePopupMenu();
    if (iconHit >= 0) {
        std::wstring rm = std::wstring(L10n::Tr("fence.remove")) + L" \"" +
                          m_icons[iconHit].name + L"\"";
        AppendMenuW(menu, MF_STRING, 3, rm.c_str());
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    }

    AppendMenuW(menu, MF_STRING, 1, L10n::Tr("fence.rename"));
    AppendMenuW(menu, MF_STRING, 4, L10n::Tr("fence.appearance"));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    // Explorer's own 新建 submenu, queried straight into the fence menu.
    // Mapped fences create into their folder; unmapped ones point the
    // handler at the desktop PIDL itself so the entries match the system
    // desktop menu exactly (e.g. 快捷方式).
    PIDLIST_ABSOLUTE pidlNewFolder = nullptr;
    if (IsMapped())
        SHParseDisplayName(m_sourceFolder.c_str(), nullptr,
                           &pidlNewFolder, 0, nullptr);
    else
        SHGetSpecialFolderLocation(nullptr, CSIDL_DESKTOP, &pidlNewFolder);
    ComPtr<IContextMenu> spNewCM;
    UINT newMenuPos = 0;
    bool newMenuAdded = false;
    if (pidlNewFolder &&
        SUCCEEDED(ShellMenu::CreateNewMenuHandler(pidlNewFolder, &spNewCM))) {
        newMenuPos = (UINT)GetMenuItemCount(menu);
        HRESULT hrQ = spNewCM->QueryContextMenu(menu, newMenuPos,
                (UINT)ShellMenu::kCmdNewFirst, 0x7FFF, CMF_NORMAL);
        if (SUCCEEDED(hrQ)) {
            // The handler fills its submenu lazily on WM_INITMENUPOPUP —
            // the same message Explorer forwards before showing it.
            HMENU hNewSub = GetSubMenu(menu, newMenuPos);
            if (hNewSub) {
                ShellMenu::ForwardMenuMessage(spNewCM.Get(), WM_INITMENUPOPUP,
                                              (WPARAM)hNewSub, 0);
            }
            newMenuAdded = true;
            ShellMenu::SetActiveNewMenu(spNewCM.Get());
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        } else {
            spNewCM.Reset();
        }
    }

    AppendMenuW(menu, MF_STRING, 2, L10n::Tr("fence.delete"));

    // Sort-by submenu (mapped fences only)
    if (!m_sourceFolder.empty()) {
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        HMENU sortMenu = CreatePopupMenu();
        auto addSortItem = [&](UINT id, int col, const char* key) {
            UINT flags = MF_STRING;
            if (m_sortColumn == col) flags |= MF_CHECKED;
            AppendMenuW(sortMenu, flags, id, L10n::Tr(key));
        };
        addSortItem(10, 0, "fence.sort.name");
        addSortItem(11, 1, "fence.sort.date_modified");
        addSortItem(12, 3, "fence.sort.type");
        addSortItem(13, 2, "fence.sort.size");
        AppendMenuW(sortMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(sortMenu, MF_STRING | (m_sortAscending ? MF_CHECKED : 0),
                    14, L10n::Tr("fence.sort.ascending"));
        AppendMenuW(sortMenu, MF_STRING | (!m_sortAscending ? MF_CHECKED : 0),
                    15, L10n::Tr("fence.sort.descending"));
        AppendMenuW(menu, MF_POPUP, (UINT_PTR)sortMenu,
                    L10n::Tr("fence.sort_by"));
    }

    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, 5, L10n::Tr("fence.map_folder"));
    if (!m_sourceFolder.empty())
        AppendMenuW(menu, MF_STRING, 6, L10n::Tr("fence.unmap_folder"));

    // Fluent glyph icons. The set owns the HBITMAPs until the menu
    // interaction below has fully returned (MIIM_BITMAP is by reference).
    GlyphBitmapSet glyphs;
    {
        int gsz = MenuGlyphSizePx();
        auto icon = [&](UINT id, MenuGlyph g) {
            HBITMAP b = glyphs.add(g, gsz);
            if (!b) return;
            MENUITEMINFOW mii = { sizeof(mii) };
            mii.fMask = MIIM_BITMAP;
            mii.hbmpItem = b;
            SetMenuItemInfoW(menu, id, FALSE, &mii);
        };
        if (iconHit >= 0) icon(3, MenuGlyph::Remove);
        icon(1, MenuGlyph::Pencil);    // Rename
        icon(4, MenuGlyph::Sliders);   // Appearance Settings
        // The New popup is addressed by position: the shell inserts it
        // without a command id of its own.
        if (newMenuAdded) {
            HBITMAP b = glyphs.add(MenuGlyph::Plus, gsz);
            if (b) {
                MENUITEMINFOW mii = { sizeof(mii) };
                mii.fMask = MIIM_BITMAP;
                mii.hbmpItem = b;
                SetMenuItemInfoW(menu, newMenuPos, TRUE, &mii);
            }
        }
        icon(2, MenuGlyph::Trash);     // Delete Fence
        icon(5, MenuGlyph::Folder);    // Map Folder
        if (!m_sourceFolder.empty())
            icon(6, MenuGlyph::Remove); // Unmap Folder
    }

    SetForegroundWindow(m_hwnd);
    // Prefer the Win11 Fluent rendering; -1 means it could not be built
    // (empty model / D2D failure) → classic menu, the same fallback the
    // shell icon menu uses. The Fluent loop swallows the dismiss click,
    // which is exactly what a context menu must do over a surface where
    // a stray press would otherwise start a fence drag.
    int cmd = FluentMenu::Run(m_hwnd, menu, pt);
    if (cmd < 0)
        cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
            pt.x, pt.y, 0, m_hwnd, nullptr);

    if (this) {
        switch (cmd) {
        case 1: EnterRename(); break;
        case 2:
            if (s_owner)
                PostMessage(s_owner, WM_FENCE_DELETE, (WPARAM)m_hwnd, 0);
            break;
        case 3: eraseIcon(iconHit); break;
        case 4: SettingsPanel::Run(this); break;
        case 5: {
            ComPtr<IFileOpenDialog> pDlg;
            if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pDlg)))) {
                pDlg->SetOptions(FOS_PICKFOLDERS | FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR);
                if (SUCCEEDED(pDlg->Show(m_hwnd))) {
                    ComPtr<IShellItem> psi;
                    if (SUCCEEDED(pDlg->GetResult(&psi))) {
                        LPWSTR path = nullptr;
                        if (SUCCEEDED(psi->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
                            int res = MessageBoxW(m_hwnd,
                                L10n::Tr("dialog.map_folder.confirm"),
                                L10n::Tr("dialog.map_folder.title"),
                                MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
                            if (res == IDYES) {
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
                    }
                }
            }
            break;
        }
        case 6: UnmapFolder(); break;
        case 10: case 11: case 12: case 13: {
            // Column IDs map: 10=Name(0), 11=Date(1), 12=Type(3), 13=Size(2)
            static const int colMap[] = { 0, 1, 3, 2 };
            int col = colMap[cmd - 10];
            bool natAsc = (col == 0 || col == 3);   // Name/Type default ascending
            SetSort(col, natAsc);
            break;
        }
        case 14: SetSort(m_sortColumn, true); break;
        case 15: SetSort(m_sortColumn, false); break;
        default:
            // An entry from the shell 新建 submenu: hand it back to the
            // handler, which creates the file in the target folder.
            if (cmd > ShellMenu::kCmdNewFirst && cmd <= 0x7FFF && spNewCM) {
                std::wstring targetDir = m_sourceFolder;
                if (targetDir.empty()) {
                    wchar_t buf[MAX_PATH] = {};
                    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_DESKTOPDIRECTORY,
                                                   nullptr, 0, buf)))
                        targetDir = buf;
                }
                std::vector<std::wstring> before, after;
                ShellMenu::SnapshotFolderNames(targetDir, before);
                CMINVOKECOMMANDINFO ici = {};
                ici.cbSize = sizeof(ici);
                ici.hwnd = m_hwnd;
                ici.lpVerb = MAKEINTRESOURCEA(cmd - ShellMenu::kCmdNewFirst);
                ici.nShow = SW_SHOWNORMAL;
                HRESULT hrInv = spNewCM->InvokeCommand(&ici);
                ShellMenu::SnapshotFolderNames(targetDir, after);
                std::wstring newPath;
                for (const auto& n : after) {
                    if (std::find(before.begin(), before.end(), n) == before.end()) {
                        newPath = targetDir + L"\\" + n;
                        break;
                    }
                }
                if (!newPath.empty()) {
                    if (IsMapped()) {
                        // The watcher re-enumerates on its own; refresh
                        // synchronously so the new file is visible and
                        // can enter rename mode right away.
                        ClearSearch();
                        RefreshFromSource();
                    } else {
                        AddIcon(newPath.substr(newPath.find_last_of(L'\\') + 1),
                                      newPath, (float)cli.x, (float)cli.y);
                    }
                    for (int i = 0; i < (int)m_icons.size(); i++) {
                        if (lstrcmpiW(m_icons[i].path.c_str(), newPath.c_str()) == 0) {
                            SelectOnly(i);
                            EnterIconRename(i);
                            break;
                        }
                    }
                }
            }
            break;
        }
    }
    // Destroy only after dispatch: the shell 新建 handler keeps its
    // cached submenu alive until InvokeCommand runs — destroying the
    // menu first makes it fail with E_FAIL.
    DestroyMenu(menu);
    ShellMenu::SetActiveNewMenu(nullptr);
    if (pidlNewFolder) CoTaskMemFree(pidlNewFolder);
    return 0;
}
