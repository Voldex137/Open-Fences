#pragma once

// Shared by the settings panel sources (settings/*.cpp).

// ── Hit-test ids ──
// Layout and input share one geometry pass (ComputeLayout fills m_hits), so
// what you see is exactly what you click.
enum : int {
    HIT_NONE = 0,
    HIT_CLOSE, HIT_FONT_LIST, HIT_FONT_SCROLL,
    HIT_SIZE_SLIDER, HIT_OPACITY_SLIDER,
    HIT_PANEL_SWATCH0 = 100,          // +0..7
    HIT_TITLE_SWATCH0 = 120,          // +0..7
    HIT_PANEL_MORE = 140, HIT_TITLE_MORE = 141,
    HIT_TITLEH_MINUS, HIT_TITLEH_PLUS,
    HIT_TITLEPOS0 = 160,           // +0 left, +1 center, +2 right
    HIT_DISPMODE0 = 180,           // +0 grid, +1 list
    HIT_DELETE_FENCE = 200,
};

// Dark preset tints (panel body / title bar). The first entry of each row is
// the fence default, so the current look always shows as selected on open.
inline constexpr float kPanelColors[8][3] = {
    {0.08f,0.08f,0.10f}, {0.10f,0.10f,0.12f}, {0.13f,0.13f,0.15f}, {0.05f,0.05f,0.06f},
    {0.10f,0.12f,0.18f}, {0.12f,0.10f,0.16f}, {0.09f,0.13f,0.11f}, {0.15f,0.11f,0.09f},
};
inline constexpr float kTitleColors[8][3] = {
    {0.05f,0.05f,0.07f}, {0.07f,0.07f,0.09f}, {0.10f,0.10f,0.12f}, {0.03f,0.03f,0.04f},
    {0.07f,0.10f,0.16f}, {0.10f,0.08f,0.14f}, {0.07f,0.11f,0.09f}, {0.13f,0.09f,0.07f},
};

inline int clampi(int v, int lo, int hi)   { return v < lo ? lo : (v > hi ? hi : v); }
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
