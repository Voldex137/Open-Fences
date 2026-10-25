#include "settings/settings_panel.h"
#include "settings/settings_internal.h"
#include "shell/desktop_host.h"
#include "core/localization.h"
#include "fence/fence_window.h"
#include "core/config.h"
#include "core/presets.h"
#include <windowsx.h>
#include <commdlg.h>
#include <algorithm>
#include <cmath>

SettingsPanel* SettingsPanel::s_active = nullptr;

// ── Font enumeration ──

// ── Lifetime ──

void SettingsPanel::Run(FenceWindow* fence) {
    if (fence) RunPanel(fence);
}

void SettingsPanel::RunDefaults() { RunPanel(nullptr); }

FenceAppearance& SettingsPanel::App() {
    return m_fence ? m_fence->GetRender().Appearance() : m_defaults;
}

void SettingsPanel::RunPanel(FenceWindow* fence) {
    if (s_active) {                             // single instance: just raise
        if (s_active->m_hwnd) SetForegroundWindow(s_active->m_hwnd);
        return;
    }
    bool doDelete = false;
    HWND fenceHwnd = nullptr;
    {
        SettingsPanel panel(fence);
        panel.RunLoop();
        doDelete = panel.m_deleteFence;
        fenceHwnd = panel.m_fenceHwnd;
    }   // panel fully destroyed here before the delete is posted
    if (doDelete && fenceHwnd)
        PostMessageW(FenceWindow::Owner(), WM_FENCE_DELETE, (WPARAM)fenceHwnd, 0);
}

void SettingsPanel::CloseActiveFor(const FenceWindow* fence) {
    if (s_active && s_active->m_fence == fence) {
        s_active->m_closing = true;
        if (s_active->m_hwnd) DestroyWindow(s_active->m_hwnd);
        s_active->m_hwnd = nullptr;
    }
}

SettingsPanel::SettingsPanel(FenceWindow* fence) : m_fence(fence) {
    m_fenceHwnd = fence ? fence->Hwnd() : nullptr;
    if (!fence) m_defaults = Presets::Default();
    m_s = Desktop::DpiScale();
    s_active = this;

    static bool clsReg = false;
    if (!clsReg) {
        WNDCLASSEXW wc = {sizeof(wc)};
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = WndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = L"OpenFencesSettings";
        RegisterClassExW(&wc);
        clsReg = true;
    }

    // Size + position. The panel tries the fence's right, falls back to the
    // left, and is clamped to the monitor work area. If it still does not
    // fit vertically, the font-list viewport shrinks (min 3 visible rows).
    POINT cursor = {};
    GetCursorPos(&cursor);
    HMONITOR mon = m_fenceHwnd ? MonitorFromWindow(m_fenceHwnd, MONITOR_DEFAULTTONEAREST)
                               : MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = {sizeof(mi)};
    GetMonitorInfoW(mon, &mi);
    int waH = mi.rcWork.bottom - mi.rcWork.top;

    m_fontRows = 5;
    auto heightFor = [&](int rows) { return (int)((std::max)(422 + rows * 26, 500) * m_s); };
    m_panelH = heightFor(m_fontRows);
    while (m_panelH > waH && m_fontRows > 3) { m_fontRows--; m_panelH = heightFor(m_fontRows); }
    m_panelW = (int)(700 * m_s);

    RECT frc;
    if (m_fenceHwnd) GetWindowRect(m_fenceHwnd, &frc);
    else frc = RECT{ cursor.x - m_panelW, cursor.y - m_panelH, cursor.x, cursor.y };   // above the tray
    int x = frc.right + 12;
    if (x + m_panelW > mi.rcWork.right) x = frc.left - 12 - m_panelW;
    x = clampi(x, mi.rcWork.left, (std::max)(mi.rcWork.left, mi.rcWork.right - m_panelW));
    int y = clampi(frc.top, mi.rcWork.top, (std::max)(mi.rcWork.top, mi.rcWork.bottom - m_panelH));
    m_x = x; m_y = y;

    m_render = std::make_unique<RenderContext>(m_panelW, m_panelH);
    CreatePanelBrushes();
    CreatePanelFormats();
    LoadFonts();

    m_hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOOLWINDOW,
        L"OpenFencesSettings", L"Settings", WS_POPUP,
        m_x, m_y, m_panelW, m_panelH,
        nullptr, nullptr, GetModuleHandleW(nullptr), this);
    SetWindowLongPtrW(m_hwnd, GWLP_USERDATA, (LONG_PTR)this);

    // Draw + Present before ShowWindow so the first visible frame is clean.
    Repaint();
    ShowWindow(m_hwnd, SW_SHOW);
    SetForegroundWindow(m_hwnd);
    SetFocus(m_hwnd);
}

SettingsPanel::~SettingsPanel() {
    if (s_active == this) s_active = nullptr;
    if (m_hwnd) { DestroyWindow(m_hwnd); m_hwnd = nullptr; }
}

// ── Nested blocking message loop ──

static bool IsOutsideButtonDown(UINT m) {
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

void SettingsPanel::RunLoop() {
    MSG msg;
    while (!m_closing) {
        BOOL got = GetMessageW(&msg, nullptr, 0, 0);
        if (got == 0) {                     // WM_QUIT: re-post and bail out
            PostQuitMessage((int)msg.wParam);
            break;
        }
        if (got == -1) continue;

        // Any button press outside the panel closes it, and the message is
        // swallowed so it cannot leak through to a fence (drag / menu).
        if (IsOutsideButtonDown(msg.message) && msg.hwnd != m_hwnd) {
            m_closing = true;
            break;
        }

        TranslateMessage(&msg);
        DispatchMessageW(&msg);
        if (m_hwnd && !IsWindow(m_hwnd)) break;
    }
}

// ── Panel palette (brushes + text formats) ──

// ── Layout (single pass → geometry + hit rects) ──

// ── Drawing ──

void SettingsPanel::Repaint() {
    if (!m_render || !m_hwnd) return;
    DrawPanel();
    m_render->Present(m_hwnd, m_x, m_y);
}

void SettingsPanel::ApplyToFence() {
    if (!m_fence) {   // defaults mode: store the edited look for new fences
        Presets::SetDefault(m_defaults);
        Config::MarkDirty();
        return;
    }
    m_fence->GetRender().RebuildStyles();
    m_fence->SyncCollapsedSize();
    m_fence->Redraw();
    Config::MarkDirty();   // appearance changed → persist (debounced)
}

// ── Input ──

// ── WndProc ──

LRESULT CALLBACK SettingsPanel::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* self = (SettingsPanel*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_CREATE: {
        auto* cs = (CREATESTRUCTW*)lp;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
        return 0;
    }
    case WM_LBUTTONDOWN:
        if (self) self->OnLButtonDown(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;
    case WM_MOUSEMOVE:
        if (self) self->OnMouseMove(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;
    case WM_LBUTTONUP:
        if (self) self->OnLButtonUp(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;
    case WM_MOUSEWHEEL:
        if (self) self->OnMouseWheel(GET_WHEEL_DELTA_WPARAM(wp),
            GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;
    case WM_KEYDOWN:
        if (self && wp == VK_ESCAPE) self->m_closing = true;
        return 0;
    case WM_DESTROY:
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        if (self) self->m_hwnd = nullptr;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
