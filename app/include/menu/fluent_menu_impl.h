#pragma once

// Internal types of the Fluent context menu (menu/*.cpp). Not part of the public
// interface: see context_menu.h for FluentMenu::Run.

#include <windows.h>
#include <shobjidl.h>
#include <d2d1.h>
#include <dwrite.h>
#include <memory>
#include <string>
#include <vector>
#include "core/com_ptr.h"
#include "render/render.h"

namespace FluentMenuImpl {

inline int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Design metrics at 96 DPI; every value is multiplied by the DPI scale at use.
struct Metrics {
    float rowH   = 30;    // normal item row height
    float sepH   = 9;     // separator band height
    float padV   = 5;     // vertical padding inside the surface
    float padH   = 5;     // horizontal padding inside the surface
    float iconCol= 26;    // reserved icon column width
    float iconSz = 16;    // drawn icon size
    float gap    = 10;    // gap between icon column and text
    float chevr  = 18;    // width reserved for the submenu chevron
    float radius = 8;     // surface corner radius
    float rowRad = 4;     // hover highlight corner radius
    float minW   = 170;
    float maxW   = 340;
    float maxTextW = 240; // longest label before clipping
    float fontPx = 13;
};

// One menu entry (a row) plus its optional children (a flyout submenu).
struct CtxItem {
    std::wstring text;
    UINT id = 0;
    bool sep = false;
    bool disabled = false;
    bool checked = false;
    bool isDefault = false;   // the bold "Open" verb
    bool hasSub = false;
    int iconW = 0, iconH = 0;
    std::vector<BYTE> iconPx; // premultiplied BGRA (iconW*iconH*4), empty = none
    std::vector<CtxItem> children;
};


struct MenuPopup;   // fwd

struct MenuSession {
    HWND owner = nullptr;
    IContextMenu* cm = nullptr;
    std::wstring filePath;
    float s = 1.0f;
    Metrics mt;

    std::vector<CtxItem> model;          // root items
    std::vector<MenuPopup*> popups;      // open levels, index 0 = root
    bool closed = false;
    int result = 0;

    ComPtr<IDWriteFactory> dw;
    ComPtr<IDWriteTextFormat> fmt;
    ComPtr<IDWriteTextFormat> fmtSemi;   // semibold, for the default verb

    bool Build(HMENU menu);
    void CloseAll() { closed = true; }
    void Select(UINT id) { result = (int)id; closed = true; }
    MenuPopup* PopupAt(HWND h) const;
    void CloseDeeperThan(int depth);
    int RunLoop();
};

struct MenuPopup {
    MenuSession* sess = nullptr;
    HWND hwnd = nullptr;
    const std::vector<CtxItem>* items = nullptr;
    int depth = 0;
    MenuPopup* parent = nullptr;
    int parentItem = -1;

    int x = 0, y = 0, w = 0, h = 0;
    int shadow = 0;                    // soft drop-shadow margin around the
                                       // surface; the layered window spans
                                       // surface + this margin on every side
    int hover = -1;
    int scroll = 0;
    int contentH = 0;
    std::vector<float> rowY;             // top y of each item (content space)
    std::vector<float> rowH;             // height of each item
    UINT_PTR openTimer = 0;              // delayed flyout-open
    int pendingSub = -1;
    bool anySub = false;                 // some row carries a chevron
    bool pressed = false;                // left button held (drag-select)
    bool tracking = false;               // TME_LEAVE tracking active

    std::unique_ptr<RenderContext> render;
    std::vector<ComPtr<ID2D1Bitmap>> icons;
    std::vector<ComPtr<IDWriteTextLayout>> texts;   // one per row (null = separator)
    ComPtr<ID2D1SolidColorBrush> brTint, brBorder, brHi, brText, brDim,
                                  brHover, brPressed, brSep, brChev, brCheck,
                                  brShadow;
    ComPtr<ID2D1BitmapBrush> backBrush;   // fallback if no backdrop

    bool Create(MenuSession* se, const std::vector<CtxItem>* it, int dep,
                MenuPopup* par, int parItem, POINT at);
    void Layout();
    void BuildIcons();
    void MakeBrushes();
    void CaptureBackdrop();
    void Repaint();
    void DrawMenu();
    int  HitTest(int mx, int my) const;
    void SetHover(int idx);
    void TryOpenSub(int idx, bool immediate);
    void OpenSub(int idx);
    void CancelPendingSub();
    MenuPopup* RoutePoint(int& mx, int& my);
    void OnMouse(int mx, int my);
    void OnLeave();
    void OnClick(int mx, int my);
    void OnWheel(int delta);
    void OnKey(UINT vk);
    void EnsureVisible(int idx);

    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
};

constexpr UINT_PTR kSubTimerId      = 1;
constexpr UINT     kSubOpenDelayMs  = 220;   // hover delay before a flyout opens
constexpr UINT     WM_MENUDISMISS   = WM_USER + 1;

// Messages that start a click somewhere (client + non-client area).
inline bool IsButtonDownMsg(UINT m) {
    switch (m) {
    case WM_LBUTTONDOWN: case WM_RBUTTONDOWN: case WM_MBUTTONDOWN:
    case WM_XBUTTONDOWN: case WM_LBUTTONDBLCLK: case WM_RBUTTONDBLCLK:
    case WM_MBUTTONDBLCLK: case WM_XBUTTONDBLCLK:
    case WM_NCLBUTTONDOWN: case WM_NCRBUTTONDOWN: case WM_NCMBUTTONDOWN:
    case WM_NCXBUTTONDOWN: case WM_NCLBUTTONDBLCLK: case WM_NCRBUTTONDBLCLK:
    case WM_NCMBUTTONDBLCLK: case WM_NCXBUTTONDBLCLK:
        return true;
    }
    return false;
}

// One WH_MOUSE_LL hook shared by every menu session (menu_hook.cpp): installed
// while any menu can be open, removed when the last one closes.
void AcquireMenuHook();
void ReleaseMenuHook();

// Extract pixels for a file's icon (premultiplied BGRA); false on failure.
bool ExtractFileIconPixels(const std::wstring& path, int size, std::vector<BYTE>& out, int& w, int& h);

}  // namespace FluentMenuImpl
