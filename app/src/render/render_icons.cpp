#include "render/render.h"
#include "render/icon_extract.h"
#include <shellapi.h>
#include <shlobj.h>
#include <commoncontrols.h>
#include <algorithm>
#include <cstring>

ComPtr<ID2D1Bitmap> RenderContext::MakeBitmapFromPixels(const BYTE* bgra, int w, int h) {
    if (w <= 0 || h <= 0) return nullptr;
    ComPtr<IWICBitmap> wicBmp;
    HRESULT hr = m_wicFactory->CreateBitmapFromMemory(
        (UINT)w, (UINT)h, GUID_WICPixelFormat32bppPBGRA,
        (UINT)w * 4, (UINT)((size_t)w * h * 4), const_cast<BYTE*>(bgra), &wicBmp);
    if (FAILED(hr) || !wicBmp) return nullptr;
    // Render target runs at 96 DPI → WIC bitmap DIP size equals its pixel
    // size, so layout math in pixels stays exact.
    wicBmp->SetResolution(96.0f, 96.0f);
    ComPtr<ID2D1Bitmap> d2dIcon;
    if (FAILED(m_d2dTarget->CreateBitmapFromWicBitmap(wicBmp.Get(), nullptr, &d2dIcon)))
        return nullptr;
    return d2dIcon;
}

ComPtr<ID2D1Bitmap> RenderContext::TryLoadIcon(int iconIndex, int imgList,
                                                bool cropPadded) {
    ComPtr<IImageList> spImageList;
    if (FAILED(SHGetImageList(imgList, IID_PPV_ARGS(&spImageList))) || !spImageList)
        return nullptr;
    HICON hIcon = nullptr;
    if (FAILED(spImageList->GetIcon(iconIndex, ILD_TRANSPARENT, &hIcon)) || !hIcon)
        return nullptr;

    std::vector<BYTE> px;
    int w = 0, h = 0;
    bool ok = ExtractIconPixels(hIcon, px, w, h);
    DestroyIcon(hIcon);
    if (!ok || px.empty()) return nullptr;

    // Normally the canvas is kept intact: its art-to-canvas ratio is the
    // shell's own composition, and scaling the whole bitmap reproduces the
    // exact proportions Explorer shows. The jumbo list is the exception — it
    // pads some entries with enormous margins (tiny art on a huge canvas,
    // seen for types without real high-res art), so when it is the source
    // the glyph is cropped down to its opaque bounds.
    if (cropPadded) {
        int bx0, by0, bx1, by1;
        if (TightBounds(px.data(), w, h, bx0, by0, bx1, by1)) {
            int bw = bx1 - bx0 + 1, bh = by1 - by0 + 1;
            if (bw * 10 < w * 9 || bh * 10 < h * 9) {
                std::vector<BYTE> crop((size_t)bw * bh * 4);
                for (int y = 0; y < bh; y++)
                    memcpy(crop.data() + (size_t)y * bw * 4,
                           px.data() + ((size_t)(by0 + y) * w + bx0) * 4,
                           (size_t)bw * 4);
                px = std::move(crop);
                w = bw; h = bh;
            }
        }
    }

    return MakeBitmapFromPixels(px.data(), w, h);
}

ID2D1Bitmap* RenderContext::LoadFileIcon(const std::wstring& filePath) {
    auto it = m_iconCache.find(filePath);
    if (it != m_iconCache.end()) return it->second.Get();

    ComPtr<ID2D1Bitmap> d2dIcon;

    SHFILEINFOW shfi = {};
    // Namespace paths (::{CLSID}) need PIDL-based resolution;
    // SHGetFileInfoW on a raw parse string does not always resolve the icon.
    bool isNamespacePath = filePath.size() > 2 && filePath[0] == L':' && filePath[1] == L':';
    bool gotIcon = false;
    if (isNamespacePath) {
        PIDLIST_ABSOLUTE pidl = nullptr;
        if (SUCCEEDED(SHParseDisplayName(filePath.c_str(), nullptr, &pidl, 0, nullptr)) && pidl) {
            gotIcon = SHGetFileInfoW((LPCWSTR)pidl, 0, &shfi, sizeof(shfi),
                                     SHGFI_PIDL | SHGFI_SYSICONINDEX) && shfi.iIcon != -1;
            CoTaskMemFree(pidl);
        }
    } else {
        gotIcon = SHGetFileInfoW(filePath.c_str(), 0, &shfi, sizeof(shfi),
                                 SHGFI_SYSICONINDEX) && shfi.iIcon != -1;
        if (!gotIcon) {
            // The file is gone (deleted or moved): fall back to the generic icon
            // of its type, derived from the name alone, so the icon does not vanish.
            DWORD attrs = filePath.find_last_of(L'.') == std::wstring::npos
                              ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
            gotIcon = SHGetFileInfoW(filePath.c_str(), attrs, &shfi, sizeof(shfi),
                                     SHGFI_SYSICONINDEX | SHGFI_USEFILEATTRIBUTES) && shfi.iIcon != -1;
        }
    }
    if (gotIcon) {
        // Pick the smallest image list whose canvas still covers the glyph
        // box, then draw the whole bitmap into the box: the shell keeps the
        // art-to-canvas ratio consistent across the LARGE/EXTRALARGE lists,
        // so an uncropped bitmap scaled to the desktop icon size reproduces
        // the desktop's own rendering (minimal scaling keeps it crisp). The
        // jumbo list is only reached for very large glyphs and is cropped by
        // TryLoadIcon, guarding its disproportionately padded entries.
        static const int lists[3] = { SHIL_LARGE, SHIL_EXTRALARGE, SHIL_JUMBO };
        float need = m_app.imgSize;
        for (int L : lists) {
            ComPtr<ID2D1Bitmap> bmp = TryLoadIcon(shfi.iIcon, L, L == SHIL_JUMBO);
            if (!bmp) continue;
            d2dIcon = bmp;
            D2D1_SIZE_F sz = bmp->GetSize();
            if ((std::max)(sz.width, sz.height) + 0.5f >= need) break;
        }
    }

    // Cache failures too: a path that cannot resolve an icon must not
    // re-query the shell on every redraw (redraws run at drag frequency).
    m_iconCache[filePath] = d2dIcon;
    return d2dIcon.Get();
}
