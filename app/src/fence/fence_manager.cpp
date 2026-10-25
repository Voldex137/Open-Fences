#include "fence/fence_manager.h"
#include "core/config.h"
#include "core/localization.h"
#include "shell/desktop_host.h"
#include "shell/desktop_icons.h"

#include <algorithm>
#include <string>

namespace FenceManager {

namespace {

FenceList g_fences;
bool g_allHidden = false;

// "f1", "f2", ...: the smallest number not used by an existing fence (a plain
// count would repeat an id after a fence in the middle was deleted).
std::wstring NextFenceId() {
    for (int n = 1;; n++) {
        std::wstring id = L"f" + std::to_wstring(n);
        bool taken = std::any_of(g_fences.begin(), g_fences.end(),
                                 [&](const auto& f) { return f->GetData().id == id; });
        if (!taken) return id;
    }
}

}  // namespace

FenceList& All() { return g_fences; }

void Add(std::unique_ptr<FenceWindow> fence) { g_fences.push_back(std::move(fence)); }

FenceWindow* CreateNew() {
    float s = Desktop::DpiScale();
    FenceData data{ NextFenceId(), L10n::Tr("fence.new_title"),
                    (int)(100 * s), (int)(100 * s), (int)(300 * s), (int)(220 * s) };
    g_fences.push_back(std::make_unique<FenceWindow>(data));
    FenceWindow* fence = g_fences.back().get();
    DesktopIcons::ParkIfHidden(fence->Hwnd());
    Config::MarkDirty();
    return fence;
}

void Remove(HWND hwnd) {
    auto it = std::find_if(g_fences.begin(), g_fences.end(),
                           [hwnd](const auto& f) { return f->Hwnd() == hwnd; });
    if (it == g_fences.end()) return;
    (*it)->ReturnStoredItemsToDesktop();   // the fence is going away; its files return to the desktop
    g_fences.erase(it);
    Config::MarkDirty();
}

// Salvage the state of the (now dead) objects and recreate them; the constructor
// re-discovers the fresh desktop host on its own.
void Rebuild() {
    DesktopIcons::BeginRebuild();
    struct Saved { FenceData data; std::vector<IconEntry> icons; bool collapsed; };
    std::vector<Saved> saved;
    saved.reserve(g_fences.size());
    for (auto& f : g_fences)
        saved.push_back({ f->GetData(), f->Icons(), f->IsCollapsed() });
    g_fences.clear();   // dtors see the dead HWNDs and skip DestroyWindow
    for (auto& s : saved) {
        auto f = std::make_unique<FenceWindow>(s.data);
        if (!s.data.sourceFolder.empty()) {
            f->SetSortPreset(s.data.sortCol, s.data.sortAsc);
            f->MapToFolder(s.data.sourceFolder);
        } else {
            f->SetIcons(s.icons);
        }
        if (s.collapsed) f->ToggleCollapse();
        if (g_allHidden) f->Hide();
        g_fences.push_back(std::move(f));
    }
    DesktopIcons::EndRebuild(Handles());
}

bool AllHidden() { return g_allHidden; }

void SetAllHidden(bool hidden) {
    g_allHidden = hidden;
    for (auto& f : g_fences) {
        if (hidden) f->Hide(); else f->Show();
    }
}

std::vector<HWND> Handles() {
    std::vector<HWND> handles;
    handles.reserve(g_fences.size());
    for (auto& f : g_fences) handles.push_back(f->Hwnd());
    return handles;
}

}  // namespace FenceManager
