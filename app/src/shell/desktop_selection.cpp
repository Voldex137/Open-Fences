#include "shell/desktop_selection.h"
#include "fence/fence_manager.h"
#include "fence/fence_window.h"
#include "shell/desktop_host.h"
#include "core/config.h"

#include <commctrl.h>
#include <algorithm>
#include <vector>

namespace DesktopSelection {

namespace {

bool g_wasDown = false;
bool g_sweeping = false;        // a press on the empty desktop is in progress: a rubber band
bool g_carrying = false;        // a press on a selected desktop icon: the tiles ride along with the drag
POINT g_anchor = {};
bool g_moved = false;           // the cursor has travelled far enough to count as a sweep / drag
std::vector<HWND> g_baseSelected;   // tiles already selected when the sweep began (Ctrl keeps them)
struct TileStart { HWND hwnd; POINT start; };
std::vector<TileStart> g_carried;

bool IsTile(FenceWindow& f) {
    return f.IsCollapsed() && f.GetRender().Appearance().collapseStyle == 1;
}

// Runs `fn(remoteMemory)` with a block of `size` bytes allocated in explorer's address space.
template <typename Fn>
bool WithRemote(HWND list, size_t size, Fn fn) {
    DWORD pid = 0;
    GetWindowThreadProcessId(list, &pid);
    HANDLE proc = OpenProcess(PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE, FALSE, pid);
    if (!proc) return false;
    bool ok = false;
    if (void* mem = VirtualAllocEx(proc, nullptr, size, MEM_COMMIT, PAGE_READWRITE)) {
        ok = fn(proc, mem);
        VirtualFreeEx(proc, mem, 0, MEM_RELEASE);
    }
    CloseHandle(proc);
    return ok;
}

HWND DesktopList() {
    HWND list = Desktop::FindIconListHost();
    wchar_t cls[64] = {};
    if (!list || !GetClassNameW(list, cls, 64) || wcscmp(cls, L"SysListView32")) return nullptr;
    return list;
}

// Index of the desktop item (icon or its label) under the screen point, or -1.
int ItemUnder(HWND list, POINT screen) {
    POINT client = screen;
    ScreenToClient(list, &client);
    int found = -1;
    WithRemote(list, sizeof(LVHITTESTINFO), [&](HANDLE proc, void* mem) {
        LVHITTESTINFO info = {};
        info.pt = client;
        DWORD_PTR res = 0;
        if (WriteProcessMemory(proc, mem, &info, sizeof(info), nullptr) &&
            SendMessageTimeoutW(list, LVM_HITTEST, 0, (LPARAM)mem, SMTO_ABORTIFHUNG, 100, &res) &&
            ReadProcessMemory(proc, mem, &info, sizeof(info), nullptr) &&
            info.iItem >= 0 && (info.flags & LVHT_ONITEM))
            found = info.iItem;
        return true;
    });
    return found;
}

bool ItemSelected(HWND list, int index) {
    DWORD_PTR state = 0;
    return SendMessageTimeoutW(list, LVM_GETITEMSTATE, (WPARAM)index, LVIS_SELECTED, SMTO_ABORTIFHUNG, 100, &state) &&
           (state & LVIS_SELECTED) != 0;
}

void ClearAll() {
    for (auto& f : FenceManager::All()) f->SetTileSelected(false);
}

bool AnyTileSelected() {
    for (auto& f : FenceManager::All())
        if (f->IsTileSelected() && IsTile(*f)) return true;
    return false;
}

void MoveCarried(int dx, int dy) {
    for (const auto& t : g_carried) {
        POINT np{ t.start.x + dx, t.start.y + dy };
        if (HWND par = GetParent(t.hwnd)) ScreenToClient(par, &np);
        SetWindowPos(t.hwnd, nullptr, np.x, np.y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

}  // namespace

std::vector<Item> SelectedItems() {
    std::vector<Item> items;
    HWND list = DesktopList();
    if (!list) return items;
    std::vector<int> indices;
    int cur = -1;
    for (;;) {
        DWORD_PTR next = 0;
        if (!SendMessageTimeoutW(list, LVM_GETNEXTITEM, (WPARAM)cur, LVNI_SELECTED, SMTO_ABORTIFHUNG, 100, &next)) break;
        cur = (int)next;
        if (cur < 0) break;
        indices.push_back(cur);
        if (indices.size() > 2000) break;
    }
    WithRemote(list, sizeof(POINT), [&](HANDLE proc, void* mem) {
        for (int idx : indices) {
            DWORD_PTR res = 0;
            POINT pos = {};
            if (SendMessageTimeoutW(list, LVM_GETITEMPOSITION, (WPARAM)idx, (LPARAM)mem, SMTO_ABORTIFHUNG, 100, &res) &&
                res && ReadProcessMemory(proc, mem, &pos, sizeof(pos), nullptr))
                items.push_back({ idx, pos });
        }
        return true;
    });
    return items;
}

void OffsetItems(const std::vector<Item>& items, int dx, int dy) {
    HWND list = DesktopList();
    if (!list) return;
    for (const auto& it : items) {
        DWORD_PTR res = 0;
        int x = it.start.x + dx, y = it.start.y + dy;
        SendMessageTimeoutW(list, LVM_SETITEMPOSITION, (WPARAM)it.index, MAKELPARAM((short)x, (short)y),
                            SMTO_ABORTIFHUNG, 100, &res);
    }
}

void SettleTiles(const std::vector<HWND>& tiles) {
    std::vector<HWND> pending = tiles;
    for (HWND h : tiles) {
        pending.erase(std::remove(pending.begin(), pending.end(), h), pending.end());
        for (auto& f : FenceManager::All())
            if (f->Hwnd() == h) { f->SnapTileToDesktopGrid(&pending); break; }
    }
}

void Poll() {
    const bool down = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    const bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;

    if (down && !g_wasDown) {                       // press
        g_sweeping = g_carrying = false;
        POINT p;
        GetCursorPos(&p);
        HWND list = DesktopList();
        if (list && WindowFromPoint(p) == list) {   // on the desktop itself (not a fence, not another app)
            int item = ItemUnder(list, p);
            if (item >= 0) {
                if (ItemSelected(list, item) && AnyTileSelected()) {
                    // Pressing a selected desktop icon: if it turns into a drag, the selected
                    // tiles go with it (Explorer moves the icons itself).
                    g_carrying = true;
                    g_moved = false;
                    g_anchor = p;
                    g_carried.clear();
                    for (auto& f : FenceManager::All()) {
                        RECT r;
                        if (f->IsTileSelected() && IsTile(*f) && GetWindowRect(f->Hwnd(), &r))
                            g_carried.push_back({ f->Hwnd(), POINT{ r.left, r.top } });
                    }
                } else if (!ctrl) {
                    ClearAll();                     // a different icon becomes the selection
                }
            } else {                                // empty ground: this press starts a rubber band
                if (!ctrl) ClearAll();
                g_sweeping = true;
                g_moved = false;
                g_anchor = p;
                g_baseSelected.clear();
                for (auto& f : FenceManager::All())
                    if (f->IsTileSelected()) g_baseSelected.push_back(f->Hwnd());
            }
        }
    } else if (down && (g_sweeping || g_carrying)) {
        POINT c;
        GetCursorPos(&c);
        if (!g_moved &&
            abs(c.x - g_anchor.x) < GetSystemMetrics(SM_CXDRAG) &&
            abs(c.y - g_anchor.y) < GetSystemMetrics(SM_CYDRAG)) {
            g_wasDown = down;
            return;
        }
        g_moved = true;
        if (g_carrying) {
            MoveCarried(c.x - g_anchor.x, c.y - g_anchor.y);
        } else {
            RECT band{ (std::min)(g_anchor.x, c.x), (std::min)(g_anchor.y, c.y),
                       (std::max)(g_anchor.x, c.x), (std::max)(g_anchor.y, c.y) };
            for (auto& f : FenceManager::All()) {
                if (!IsTile(*f)) { f->SetTileSelected(false); continue; }
                RECT r, hit;
                bool inside = GetWindowRect(f->Hwnd(), &r) && IntersectRect(&hit, &band, &r);
                bool before = std::find(g_baseSelected.begin(), g_baseSelected.end(), f->Hwnd()) != g_baseSelected.end();
                f->SetTileSelected(inside || (ctrl && before));
            }
        }
    } else if (!down && g_wasDown) {                // release
        if (g_carrying) {
            if (g_moved) {
                std::vector<HWND> tiles;
                for (const auto& t : g_carried) tiles.push_back(t.hwnd);
                SettleTiles(tiles);
                Config::MarkDirty();
            } else {
                ClearAll();                         // a plain click on an icon leaves just that icon selected
            }
        }
        g_sweeping = g_carrying = false;
        g_carried.clear();
    }
    g_wasDown = down;
}

}  // namespace DesktopSelection
