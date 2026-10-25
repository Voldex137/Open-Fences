#include "menu/shell_context_menu.h"
#include "menu/context_menu.h"
#include "core/localization.h"
#include "core/com_ptr.h"

#include <shlobj.h>
#include <shobjidl.h>
#include <shlwapi.h>

namespace ShellMenu {

// ── Shell context menu (the Explorer right-click menu for a file) ──
//
// Resolving one: parse the full path into an absolute PIDL, bind to the parent
// folder's IShellFolder, and ask it for the child's IContextMenu. Displaying:
// let the shell fill a popup (command ids in [1, 0x7FFF]), append our single
// fence-specific verb above that range, then dispatch the chosen id.

HRESULT CreateForPaths(const std::vector<std::wstring>& paths, IContextMenu** ppCM) {
    *ppCM = nullptr;
    if (paths.empty()) return E_INVALIDARG;

    std::vector<PIDLIST_ABSOLUTE> full;      // owned; the child ids point into these
    std::vector<PCUITEMID_CHILD> children;
    HRESULT hr = S_OK;
    for (const auto& path : paths) {
        PIDLIST_ABSOLUTE pidl = nullptr;
        if (FAILED(SHParseDisplayName(path.c_str(), nullptr, &pidl, 0, nullptr)) || !pidl) {
            hr = E_FAIL;
            break;
        }
        full.push_back(pidl);
        children.push_back(ILFindLastID(pidl));
    }

    ComPtr<IShellFolder> spParent;
    if (SUCCEEDED(hr)) {
        // Parent = the folder of the first item (the caller keeps all items in one folder).
        PIDLIST_ABSOLUTE pidlParent = ILCloneFull(full[0]);
        ComPtr<IShellFolder> spDesktop;
        hr = SHGetDesktopFolder(&spDesktop);
        if (SUCCEEDED(hr) && pidlParent) {
            ILRemoveLastID(pidlParent);
            if (ILIsEmpty(pidlParent)) spParent = spDesktop;   // file lives on the desktop
            else hr = spDesktop->BindToObject(pidlParent, nullptr, IID_PPV_ARGS(&spParent));
        }
        if (pidlParent) CoTaskMemFree(pidlParent);
    }

    if (SUCCEEDED(hr) && spParent) {
        hr = spParent->GetUIObjectOf(nullptr, (UINT)children.size(), children.data(),
                                     IID_IContextMenu, nullptr, (void**)ppCM);
    }
    for (auto* pidl : full) CoTaskMemFree(pidl);
    return hr;
}

HRESULT CreateForPath(const std::wstring& path, IContextMenu** ppCM) {
    return CreateForPaths(std::vector<std::wstring>{ path }, ppCM);
}

// ── "New" submenu (the shell's own NewMenuHandler) ──
//
// Explorer's 新建 submenu is produced by the handler registered under
// HKCR\Directory\Background\shellex\ContextMenuHandlers\New (stable CLSID
// since Win2000). Instantiating it directly yields the exact system menu —
// localized labels, third-party ShellNew templates — and InvokeCommand
// creates the file in the folder the handler was initialized with. Passing
// the desktop PIDL (not a plain folder path) makes the handler recognize the
// desktop, so it adds the desktop-only entries like 快捷方式.
static const CLSID kCLSID_NewMenu = { 0xD969A300, 0xE7FF, 0x11D0,
                                      { 0xA9, 0x3B, 0x00, 0xA0, 0xC9, 0x0F, 0x27, 0x19 } };

// Command-id base the handler owns inside the fence menu (above our own ids).

// Set while a 新建 menu session is live so the fence wndproc can forward the
// menu messages the handler needs (Explorer does the same forwarding).
static IContextMenu* s_activeNewCM = nullptr;

// The shell handler creates the file but doesn't tell us its final name
// ("新建 文本文档 (2).txt" on collisions), so diff the folder's names
// around the invocation to find it.
void SnapshotFolderNames(const std::wstring& dir, std::vector<std::wstring>& out) {
    if (dir.empty()) return;
    std::wstring sp = dir + L"\\*";
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(sp.c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do { out.push_back(fd.cFileName); } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
}

HRESULT CreateNewMenuHandler(PCIDLIST_ABSOLUTE pidlFolder, IContextMenu** ppCM) {
    *ppCM = nullptr;

    ComPtr<IContextMenu> spCM;
    HRESULT hr = CoCreateInstance(kCLSID_NewMenu, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&spCM));
    if (SUCCEEDED(hr)) {
        ComPtr<IShellExtInit> spInit;
        hr = spCM.As(&spInit);
        if (SUCCEEDED(hr)) {
            hr = spInit->Initialize(pidlFolder, nullptr, nullptr);
        }
    }
    if (SUCCEEDED(hr)) *ppCM = spCM.Detach();
    return hr;
}

// The fence-only verb is placed far above the shell's command-id range.
static const int kCmdRemoveFromFence = 0x8001;

// Shows the Explorer menu for `path` at `ptScreen` (screen coords) with a
// "remove from fence" entry appended. Returns false only when no system menu
// could be produced (caller falls back to the plain fence menu). Sets
// `invoked` when a shell command ran, `removeChosen` for our own verb, and
// `renameChosen` when the shell's 重命名 verb was picked (the caller starts
// the fence's in-place rename instead of handing it to Explorer).
bool RunForPath(HWND hwnd, const std::wstring& path,
                                const std::wstring& displayName, POINT ptScreen,
                                bool& invoked, bool& removeChosen,
                                bool& renameChosen) {
    return RunForPaths(hwnd, std::vector<std::wstring>{ path }, displayName, ptScreen,
                       invoked, removeChosen, renameChosen);
}

bool RunForPaths(HWND hwnd, const std::vector<std::wstring>& paths,
                 const std::wstring& displayName, POINT ptScreen,
                 bool& invoked, bool& removeChosen, bool& renameChosen) {
    invoked = removeChosen = renameChosen = false;
    if (paths.empty()) return false;
    const std::wstring& path = paths[0];

    ComPtr<IContextMenu> spCM;
    if (FAILED(CreateForPaths(paths, &spCM)) || !spCM) return false;

    HMENU menu = CreatePopupMenu();
    if (!menu) return false;

    if (FAILED(spCM->QueryContextMenu(menu, 0, 1, 0x7FFF,
                                      CMF_EXPLORE | (paths.size() == 1 ? CMF_CANRENAME : 0)))) {
        DestroyMenu(menu);
        return false;
    }

    // The shell's rename verb would start Explorer's rename in ITS view
    // (invisible when desktop icons are hidden); intercept its command id so
    // the fence renames in place instead.
    int renameCmdId = 0;
    int itemCount = GetMenuItemCount(menu);
    for (int i = 0; i < itemCount; i++) {
        MENUITEMINFOW mii = { sizeof(mii) };
        mii.fMask = MIIM_ID | MIIM_SUBMENU;
        if (!GetMenuItemInfoW(menu, i, TRUE, &mii)) continue;
        if (mii.hSubMenu || !mii.wID || mii.wID > 0x7FFF) continue;
        char verb[64] = {};
        if (SUCCEEDED(spCM->GetCommandString(mii.wID - 1, GCS_VERBA, nullptr,
                                             verb, sizeof(verb))) &&
            lstrcmpiA(verb, "rename") == 0) {
            renameCmdId = (int)mii.wID;
            break;
        }
    }

    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    std::wstring rm;
    if (paths.size() > 1) {
        rm = std::to_wstring(paths.size()) + L" " + L10n::Tr("fence.remove_n_from_fence");
    } else {
        rm = std::wstring(L10n::Tr("fence.remove_from_fence")) + L"  \"" + displayName + L"\"";
    }
    AppendMenuW(menu, MF_STRING, kCmdRemoveFromFence, rm.c_str());

    // Win11 look first: we repaint the (identical) HMENU contents ourselves,
    // because Explorer's XAML menu cannot be obtained cross-process. If the
    // Fluent layer fails to start, fall back to the classic system menu.
    SetForegroundWindow(hwnd);
    int idCmd = FluentMenu::Run(hwnd, spCM.Get(), menu, path, ptScreen);
    if (idCmd < 0)
        idCmd = TrackPopupMenu(menu, TPM_LEFTALIGN | TPM_RETURNCMD | TPM_RIGHTBUTTON,
                               ptScreen.x, ptScreen.y, 0, hwnd, nullptr);
    DestroyMenu(menu);

    if (idCmd <= 0) return true;                 // dismissed — nothing to run

    if (idCmd == kCmdRemoveFromFence) {
        removeChosen = true;
        return true;
    }
    if (renameCmdId && idCmd == renameCmdId) {
        renameChosen = true;
        return true;
    }
    if (idCmd >= 1 && idCmd <= 0x7FFF) {
        CMINVOKECOMMANDINFO ici = {};
        ici.cbSize = sizeof(ici);
        ici.hwnd = hwnd;
        ici.lpVerb = MAKEINTRESOURCEA(idCmd - 1);
        ici.nShow = SW_SHOWNORMAL;
        if (SUCCEEDED(spCM->InvokeCommand(&ici))) invoked = true;
    }
    return true;
}

void SetActiveNewMenu(IContextMenu* menu) { s_activeNewCM = menu; }

IContextMenu* ActiveNewMenu() { return s_activeNewCM; }

void ForwardMenuMessage(IContextMenu* menu, UINT msg, WPARAM wp, LPARAM lp) {
    ComPtr<IContextMenu3> cm3;
    ComPtr<IContextMenu2> cm2;
    menu->QueryInterface(IID_PPV_ARGS(&cm3));
    menu->QueryInterface(IID_PPV_ARGS(&cm2));
    LRESULT lr = 0;
    if (cm3)      cm3->HandleMenuMsg2(msg, wp, lp, &lr);
    else if (cm2) cm2->HandleMenuMsg(msg, wp, lp);
}

}  // namespace ShellMenu
