#include "shell/desktop_selection.h"
#include "fence/fence_manager.h"
#include "fence/fence_window.h"
#include "shell/desktop_host.h"

#include <commctrl.h>
#include <algorithm>
#include <vector>

namespace DesktopSelection {

namespace {

bool g_wasDown = false;
bool g_sweeping = false;        // a press on the empty desktop is in progress
POINT g_anchor = {};
bool g_moved = false;           // the cursor has travelled far enough to count as a sweep
std::vector<HWND> g_baseSelected;   // tiles already selected when the sweep began (Ctrl keeps them)

bool IsTile(FenceWindow& f) {
    return f.IsCollapsed() && f.GetRender().Appearance().collapseStyle == 1;
}

// True when a desktop item (icon or its label) is under the screen point.
bool ItemUnder(HWND list, POINT screen) {
    POINT client = screen;
    ScreenToClient(list, &client);
    DWORD pid = 0;
    GetWindowThreadProcessId(list, &pid);
    HANDLE proc = OpenProcess(PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE, FALSE, pid);
    if (!proc) return false;
    bool hit = false;
    void* mem = VirtualAllocEx(proc, nullptr, sizeof(LVHITTESTINFO), MEM_COMMIT, PAGE_READWRITE);
    if (mem) {
        LVHITTESTINFO info = {};
        info.pt = client;
        DWORD_PTR res = 0;
        if (WriteProcessMemory(proc, mem, &info, sizeof(info), nullptr) &&
            SendMessageTimeoutW(list, LVM_HITTEST, 0, (LPARAM)mem, SMTO_ABORTIFHUNG, 100, &res) &&
            ReadProcessMemory(proc, mem, &info, sizeof(info), nullptr))
            hit = info.iItem >= 0 && (info.flags & LVHT_ONITEM);
        VirtualFreeEx(proc, mem, 0, MEM_RELEASE);
    }
    CloseHandle(proc);
    return hit;
}

void ClearAll() {
    for (auto& f : FenceManager::All()) f->SetTileSelected(false);
}

}  // namespace

void Poll() {
    const bool down = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    const bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;

    if (down && !g_wasDown) {                       // press
        g_sweeping = false;
        POINT p;
        GetCursorPos(&p);
        HWND list = Desktop::FindIconListHost();
        if (list && WindowFromPoint(p) == list) {   // on the desktop itself (not a fence, not another app)
            if (!ctrl) ClearAll();                  // any click on the desktop drops the old selection
            if (!ItemUnder(list, p)) {              // empty ground: this press starts a rubber band
                g_sweeping = true;
                g_moved = false;
                g_anchor = p;
                g_baseSelected.clear();
                for (auto& f : FenceManager::All())
                    if (f->IsTileSelected()) g_baseSelected.push_back(f->Hwnd());
            }
        }
    } else if (down && g_sweeping) {                // sweeping
        POINT c;
        GetCursorPos(&c);
        if (!g_moved &&
            abs(c.x - g_anchor.x) < GetSystemMetrics(SM_CXDRAG) &&
            abs(c.y - g_anchor.y) < GetSystemMetrics(SM_CYDRAG))
            { g_wasDown = down; return; }
        g_moved = true;
        RECT band{ (std::min)(g_anchor.x, c.x), (std::min)(g_anchor.y, c.y),
                   (std::max)(g_anchor.x, c.x), (std::max)(g_anchor.y, c.y) };
        for (auto& f : FenceManager::All()) {
            if (!IsTile(*f)) { f->SetTileSelected(false); continue; }
            RECT r, hit;
            bool inside = GetWindowRect(f->Hwnd(), &r) && IntersectRect(&hit, &band, &r);
            bool before = std::find(g_baseSelected.begin(), g_baseSelected.end(), f->Hwnd()) != g_baseSelected.end();
            f->SetTileSelected(inside || (ctrl && before));
        }
    } else if (!down) {
        g_sweeping = false;
    }
    g_wasDown = down;
}

}  // namespace DesktopSelection
