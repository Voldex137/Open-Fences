#include "fence/fence_stack.h"
#include "core/config.h"
#include "fence/fence_manager.h"
#include "fence/stack_chain.h"

#include <algorithm>

namespace FenceStack {

namespace {

bool g_laying = false;   // Layout moves windows, which re-enters Layout through WM_WINDOWPOSCHANGED

StackNodes Nodes() {
    StackNodes nodes;
    for (auto& f : FenceManager::All())
        nodes.push_back({ f->GetData().id, f->StackAbove() });
    return nodes;
}

FenceWindow* Find(const std::wstring& id) {
    for (auto& f : FenceManager::All())
        if (f->GetData().id == id) return f.get();
    return nullptr;
}

// Writes the links of `nodes` back into the windows; returns true if anything changed.
bool Store(const StackNodes& nodes) {
    bool changed = false;
    for (const auto& n : nodes) {
        FenceWindow* f = Find(n.id);
        if (f && f->StackAbove() != n.above) {
            f->SetStackAbove(n.above);
            changed = true;
        }
    }
    if (changed) Config::MarkDirty();
    return changed;
}

}  // namespace

bool Eligible(FenceWindow& f) {
    return f.GetRender().Appearance().collapseStyle == 0;
}

bool IsStacked(const FenceWindow& f) {
    if (!f.StackAbove().empty()) return true;
    return !StackChain::RowBelow(Nodes(), f.GetData().id).empty();
}

void Extract(FenceWindow& f) {
    StackNodes nodes = Nodes();
    StackChain::Extract(nodes, f.GetData().id);
    Store(nodes);
    Layout();
}

bool DropOnto(FenceWindow& f) {
    if (!f.IsCollapsed() || !Eligible(f)) return false;
    RECT mine;
    if (!GetWindowRect(f.Hwnd(), &mine)) return false;
    POINT centre{ (mine.left + mine.right) / 2, (mine.top + mine.bottom) / 2 };

    StackNodes nodes = Nodes();
    // A row of a stack that is dragged away alone stands free first.
    if (!f.StackAbove().empty()) StackChain::Extract(nodes, f.GetData().id);

    for (auto& other : FenceManager::All()) {
        if (other.get() == &f || !other->IsCollapsed() || !Eligible(*other)) continue;
        RECT r;
        if (!GetWindowRect(other->Hwnd(), &r) || !PtInRect(&r, centre)) continue;
        if (StackChain::AttachBelow(nodes, f.GetData().id, other->GetData().id)) {
            Store(nodes);
            Layout();
            return true;
        }
    }
    Store(nodes);
    return false;
}

void Layout() {
    if (g_laying) return;
    g_laying = true;

    StackNodes nodes = Nodes();
    StackNodes clean = nodes;
    StackChain::Sanitize(clean);
    // A panel switched to the icon style leaves its stack.
    for (auto& n : clean)
        if (FenceWindow* f = Find(n.id); f && !Eligible(*f) && !n.above.empty()) StackChain::Extract(clean, n.id);
    Store(clean);

    for (auto& top : FenceManager::All()) {
        if (!top->StackAbove().empty()) continue;
        std::vector<std::wstring> rows = StackChain::RowsBelow(clean, top->GetData().id);
        if (rows.empty()) continue;
        RECT r;
        if (!GetWindowRect(top->Hwnd(), &r)) continue;
        int left = r.left, bottom = r.bottom;
        for (const auto& id : rows) {
            FenceWindow* row = Find(id);
            if (!row || !row->Hwnd()) continue;
            RECT rr;
            if (!GetWindowRect(row->Hwnd(), &rr)) continue;
            if (rr.left != left || rr.top != bottom) {
                POINT p{ left, bottom };
                if (HWND parent = GetParent(row->Hwnd())) ScreenToClient(parent, &p);
                SetWindowPos(row->Hwnd(), nullptr, p.x, p.y, 0, 0,
                             SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
                GetWindowRect(row->Hwnd(), &rr);
            }
            bottom = rr.bottom;
        }
    }
    g_laying = false;
}

}  // namespace FenceStack
