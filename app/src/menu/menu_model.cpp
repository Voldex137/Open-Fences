#include "menu/fluent_menu_impl.h"
#include "render/icon_extract.h"
#include <shlobj.h>
#include <shellapi.h>
#include <algorithm>
#include <functional>

namespace FluentMenuImpl {

// ── Icon extraction ──

// Extract pixels for a file's icon by drawing the HICON over black and white
// and recovering alpha from the difference (robust for all icon formats).
bool ExtractFileIconPixels(const std::wstring& path, int size,
                                  std::vector<BYTE>& out, int& w, int& h) {
    SHFILEINFOW sfi = {};
    if (!SHGetFileInfoW(path.c_str(), 0, &sfi, sizeof(sfi),
                        SHGFI_ICON | SHGFI_SMALLICON))
        return false;
    HICON icon = sfi.hIcon;
    if (!icon) return false;

    HDC screen = GetDC(nullptr);
    auto grab = [&](BYTE fill, std::vector<BYTE>& px) {
        BITMAPINFO bi = {};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = size;
        bi.bmiHeader.biHeight = -size;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        px.assign((size_t)size * size * 4, 0);
        void* bits = nullptr;
        HBITMAP dib = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (!dib) return false;
        HDC mem = CreateCompatibleDC(screen);
        HGDIOBJ old = SelectObject(mem, dib);
        RECT rc = { 0, 0, size, size };
        HBRUSH br = CreateSolidBrush(RGB(fill, fill, fill));
        FillRect(mem, &rc, br);
        DeleteObject(br);
        DrawIconEx(mem, 0, 0, icon, size, size, 0, nullptr, DI_NORMAL);
        memcpy(px.data(), bits, px.size());
        SelectObject(mem, old);
        DeleteDC(mem);
        DeleteObject(dib);
        return true;
    };

    std::vector<BYTE> onBlack, onWhite;
    bool ok = grab(0x00, onBlack) && grab(0xFF, onWhite);
    ReleaseDC(nullptr, screen);
    DestroyIcon(icon);
    if (!ok) return false;

    out.resize(onBlack.size());
    for (size_t i = 0; i < out.size(); i += 4) {
        // alpha = white - black (per channel); average for stability
        int a = ((onWhite[i] - onBlack[i]) + (onWhite[i+1] - onBlack[i+1]) +
                 (onWhite[i+2] - onBlack[i+2])) / 3;
        a = clampi(a, 0, 255);
        // color is what we drew on black (already = C*a); premultiply is C*a.
        out[i + 0] = onBlack[i + 0];
        out[i + 1] = onBlack[i + 1];
        out[i + 2] = onBlack[i + 2];
        out[i + 3] = (BYTE)a;
    }
    w = h = size;
    return true;
}

// ── Walk the HMENU into the model ──

static void ForwardInitPopup(IContextMenu* cm, HMENU m) {
    // Handlers populate lazy text/icons on WM_INITMENUPOPUP (Explorer does the
    // same). Forward for this popup and all nested submenus.
    ComPtr<IContextMenu3> cm3;
    ComPtr<IContextMenu2> cm2;
    cm->QueryInterface(IID_PPV_ARGS(&cm3));
    cm->QueryInterface(IID_PPV_ARGS(&cm2));
    std::function<void(HMENU)> walk = [&](HMENU pop) {
        LRESULT lr = 0;
        if (cm3)      cm3->HandleMenuMsg2(WM_INITMENUPOPUP, (WPARAM)pop, 0, &lr);
        else if (cm2) cm2->HandleMenuMsg(WM_INITMENUPOPUP, (WPARAM)pop, 0);
        int n = GetMenuItemCount(pop);
        for (int i = 0; i < n; i++) {
            MENUITEMINFOW mii = { sizeof(mii) };
            mii.fMask = MIIM_SUBMENU;
            if (GetMenuItemInfoW(pop, i, TRUE, &mii) && mii.hSubMenu)
                walk(mii.hSubMenu);
        }
    };
    walk(m);
}

// "&Open" / "Create &shortcut": the menu is drawn without underlined mnemonics,
// so the marker is dropped ("&&" is a literal ampersand).
static std::wstring StripMnemonic(const wchar_t* s) {
    std::wstring out;
    for (; *s; ++s) {
        if (*s == L'&') {
            if (s[1] == L'&') { out.push_back(L'&'); ++s; }
            continue;
        }
        out.push_back(*s);
    }
    return out;
}

static void WalkMenu(HMENU m, std::vector<CtxItem>& out,
                     const std::wstring& filePath, int depth) {
    int n = GetMenuItemCount(m);
    for (int i = 0; i < n; i++) {
        MENUITEMINFOW mii = { sizeof(mii) };
        mii.fMask = MIIM_ID | MIIM_FTYPE | MIIM_STATE | MIIM_BITMAP |
                    MIIM_SUBMENU | MIIM_STRING;
        wchar_t text[256] = {};
        mii.dwTypeData = text; mii.cch = 256;
        if (!GetMenuItemInfoW(m, i, TRUE, &mii)) continue;

        CtxItem it;
        if (mii.fType & MFT_SEPARATOR) { it.sep = true; out.push_back(std::move(it)); continue; }

        it.text = StripMnemonic(text);
        it.id = mii.wID;
        it.disabled = (mii.fState & (MFS_DISABLED | MFS_GRAYED)) != 0;
        it.checked = (mii.fState & MFS_CHECKED) != 0;
        it.isDefault = (mii.fState & MFS_DEFAULT) != 0;

        // Icon: prefer the item bitmap; fall back to the file-type icon for
        // the default (Open) verb so the top item looks like Win11.
        if (mii.hbmpItem && mii.hbmpItem != HBMMENU_CALLBACK) {
            ExtractBitmapPixels(mii.hbmpItem, it.iconPx, it.iconW, it.iconH);
        }
        if (it.iconPx.empty() && it.isDefault && !filePath.empty()) {
            int isz = GetSystemMetrics(SM_CXSMICON);
            ExtractFileIconPixels(filePath, isz ? isz : 16, it.iconPx, it.iconW, it.iconH);
        }

        if (mii.hSubMenu) {
            it.hasSub = true;
            if (depth < 4) WalkMenu(mii.hSubMenu, it.children, filePath, depth + 1);
            if (it.children.empty()) it.hasSub = false;
        }

        out.push_back(std::move(it));
    }
}

bool MenuSession::Build(HMENU menu) {
    // Only shell verb menus answer WM_INITMENUPOPUP; plain HMENUs (tray,
    // fence title bar) arrive fully populated.
    if (cm) ForwardInitPopup(cm, menu);
    WalkMenu(menu, model, filePath, 0);
    // Collapse duplicate separators and trim leading/trailing ones.
    std::vector<CtxItem> clean;
    clean.reserve(model.size());
    for (auto& it : model) {
        if (it.sep && (clean.empty() || clean.back().sep)) continue;
        clean.push_back(std::move(it));
    }
    while (!clean.empty() && clean.back().sep) clean.pop_back();
    model = std::move(clean);
    return !model.empty();
}

}  // namespace FluentMenuImpl
