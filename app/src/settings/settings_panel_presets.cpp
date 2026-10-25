#include "settings/settings_panel.h"
#include "settings/settings_internal.h"
#include "core/appearance_json.h"
#include "core/config.h"
#include "core/localization.h"
#include "core/presets.h"
#include "fence/fence_manager.h"
#include "fence/fence_window.h"
#include "menu/context_menu.h"

#include <shobjidl.h>
#include <algorithm>
#include <string>
#include <vector>

// The "Presets" menu of the settings panel and the icon chooser. Everything here
// moves a look from one place to another: a preset, the defaults for new fences,
// or another fence. The panel's own appearance (App()) is always one end of it.

namespace {

enum Cmd : int {
    kSavePreset      = 1,
    kApplyDefault    = 2,
    kCopyToDefault   = 3,
    kApplyPreset     = 100,   // + preset index
    kCopyToPreset    = 200,   // + preset index (overwrite)
    kDeletePreset    = 300,   // + preset index
    kCopyFromFence   = 400,   // + index into the list of other fences
    kCopyToFence     = 500,   // + index into the list of other fences
};

void AddDisabled(HMENU menu, const wchar_t* text) {
    AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, text);
}

}  // namespace

void SettingsPanel::SyncFontSelection() {
    m_selFont = -1;
    const FenceAppearance& app = App();
    for (int i = 0; i < (int)m_fonts.size(); i++)
        if (lstrcmpiW(m_fonts[i].c_str(), app.fontName) == 0) { m_selFont = i; break; }
    if (m_selFont >= 0)
        m_fontScrollTop = clampi(m_selFont, 0, (std::max)(0, (int)m_fonts.size() - m_fontRows));
}

void SettingsPanel::ShowPresetsMenu() {
    auto& presets = Presets::All();

    // The other fences, in creation order (this panel's own fence is left out).
    std::vector<FenceWindow*> others;
    for (auto& f : FenceManager::All())
        if (f.get() != m_fence) others.push_back(f.get());
    auto titleOf = [](FenceWindow* f) { return f->GetData().title; };

    HMENU menu = CreatePopupMenu();
    HMENU applyMenu = CreatePopupMenu();
    HMENU fromMenu = CreatePopupMenu();
    HMENU toMenu = CreatePopupMenu();
    HMENU deleteMenu = CreatePopupMenu();

    AppendMenuW(applyMenu, MF_STRING, kApplyDefault, L10n::Tr("presets.defaults"));
    if (!presets.empty()) AppendMenuW(applyMenu, MF_SEPARATOR, 0, nullptr);
    for (size_t i = 0; i < presets.size(); i++)
        AppendMenuW(applyMenu, MF_STRING, kApplyPreset + (UINT)i, presets[i].name.c_str());

    if (others.empty()) AddDisabled(fromMenu, L10n::Tr("presets.none"));
    for (size_t i = 0; i < others.size(); i++)
        AppendMenuW(fromMenu, MF_STRING, kCopyFromFence + (UINT)i, titleOf(others[i]).c_str());

    if (m_fence) AppendMenuW(toMenu, MF_STRING, kCopyToDefault, L10n::Tr("presets.defaults"));
    for (size_t i = 0; i < others.size(); i++)
        AppendMenuW(toMenu, MF_STRING, kCopyToFence + (UINT)i, titleOf(others[i]).c_str());
    if (!presets.empty()) {
        if (GetMenuItemCount(toMenu) > 0) AppendMenuW(toMenu, MF_SEPARATOR, 0, nullptr);
        for (size_t i = 0; i < presets.size(); i++)
            AppendMenuW(toMenu, MF_STRING, kCopyToPreset + (UINT)i,
                        (L"→ " + presets[i].name).c_str());
    }
    if (GetMenuItemCount(toMenu) == 0) AddDisabled(toMenu, L10n::Tr("presets.none"));

    if (presets.empty()) AddDisabled(deleteMenu, L10n::Tr("presets.none"));
    for (size_t i = 0; i < presets.size(); i++)
        AppendMenuW(deleteMenu, MF_STRING, kDeletePreset + (UINT)i, presets[i].name.c_str());

    AppendMenuW(menu, MF_STRING, kSavePreset, L10n::Tr("presets.save"));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)applyMenu, L10n::Tr("presets.apply"));
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)fromMenu, L10n::Tr("presets.copy_from"));
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)toMenu, L10n::Tr("presets.copy_to"));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)deleteMenu, L10n::Tr("presets.delete"));

    POINT pt{ m_presetsBtn.left, m_presetsBtn.bottom + (LONG)(4 * m_s) };
    ClientToScreen(m_hwnd, &pt);
    int cmd = FluentMenu::Run(m_hwnd, menu, pt);
    if (cmd < 0) cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, m_hwnd, nullptr);
    DestroyMenu(menu);   // also destroys the submenus
    if (cmd <= 0) return;

    // Loads `src` into whatever this panel edits (a fence, or the defaults).
    auto adopt = [&](const FenceAppearance& src) {
        if (m_fence) {
            m_fence->ApplyStyle(src);
        } else {
            AppearanceJson::CopyStyle(m_defaults, src);
            ApplyToFence();
        }
        SyncFontSelection();
    };

    if (cmd == kSavePreset) {
        std::wstring name = m_fence ? m_fence->GetData().title : std::wstring();
        if (name.empty()) name = Presets::NextFreeName();
        Presets::Save(name, App());
    } else if (cmd == kApplyDefault) {
        adopt(Presets::Default());
    } else if (cmd == kCopyToDefault) {
        Presets::SetDefault(App());
    } else if (cmd >= kApplyPreset && cmd < kApplyPreset + 100) {
        size_t i = (size_t)(cmd - kApplyPreset);
        if (i < presets.size()) adopt(presets[i].appearance);
    } else if (cmd >= kCopyToPreset && cmd < kCopyToPreset + 100) {
        Presets::Overwrite((size_t)(cmd - kCopyToPreset), App());
    } else if (cmd >= kDeletePreset && cmd < kDeletePreset + 100) {
        Presets::Remove((size_t)(cmd - kDeletePreset));
    } else if (cmd >= kCopyFromFence && cmd < kCopyFromFence + 100) {
        size_t i = (size_t)(cmd - kCopyFromFence);
        if (i < others.size()) adopt(others[i]->GetRender().Appearance());
    } else if (cmd >= kCopyToFence && cmd < kCopyToFence + 100) {
        size_t i = (size_t)(cmd - kCopyToFence);
        if (i < others.size()) others[i]->ApplyStyle(App());
    }
    Config::MarkDirty();
    Repaint();
}

// "From file..." / "From folder...": the icon of any file or folder becomes the
// fence's icon (collapsed tile, title). Cancel keeps the current one.
void SettingsPanel::ChooseIconFile(bool folder) {
    ComPtr<IFileOpenDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&dialog))))
        return;
    DWORD options = FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR | FOS_FORCEFILESYSTEM;
    if (folder) options |= FOS_PICKFOLDERS;
    dialog->SetOptions(options);
    if (FAILED(dialog->Show(m_hwnd))) return;
    ComPtr<IShellItem> picked;
    if (FAILED(dialog->GetResult(&picked))) return;
    LPWSTR path = nullptr;
    if (SUCCEEDED(picked->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
        wcscpy_s(App().iconPath, path);
        ApplyToFence();
        Repaint();
    }
    CoTaskMemFree(path);
}
