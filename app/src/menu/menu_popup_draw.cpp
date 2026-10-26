#include "menu/fluent_menu_impl.h"
#include "menu/context_menu.h"
#include "shell/desktop_host.h"
#include <windowsx.h>
#include <algorithm>
#include <cmath>

namespace FluentMenuImpl {

void MenuPopup::DrawMenu() {
    auto* t = render ? render->Target() : nullptr;
    if (!t) return;
    render->BeginDraw();

    const Metrics& mt = sess->mt;
    float s = sess->s;
    float W = (float)w, H = (float)h;
    const float S = (float)shadow;

    t->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));

    // Soft drop shadow: concentric rounded strokes fading outward, drawn
    // first at identity transform; the surface fill below covers their
    // inner overlap. brShadow is cached — only its alpha changes per band.
    if (brShadow && shadow > 0) {
        const int kBandCount = 10;
        const float band = S / kBandCount;
        for (int k = 0; k < kBandCount; k++) {
            float f = 1.0f - (float)k / kBandCount;
            brShadow->SetColor(D2D1::ColorF(0.0f, 0.0f, 0.0f, f * f * 0.12f));
            float off = (float)(k + 1) * band;
            D2D1_ROUNDED_RECT ring = { {S - off, S - off, S + W + off, S + H + off},
                                       mt.radius * s + off, mt.radius * s + off };
            t->DrawRoundedRectangle(ring, brShadow.Get(), band * 2.0f);
        }
    }

    // Surface: frosted backdrop + translucent tint, rounded, bordered. The
    // surface sits at +S inside the enlarged window, and the backdrop brush
    // is anchored to the capture origin (the window's top-left) — so the
    // fill MUST run under the identity transform; shifting the transform
    // would shift the sampling by S pixels too.
    D2D1_ROUNDED_RECT surface = { {S, S, S + W, S + H}, mt.radius * s, mt.radius * s };
    if (render->HasBackdrop())     t->FillRoundedRectangle(surface, render->BackdropBrush());
    else if (backBrush)            t->FillRoundedRectangle(surface, backBrush.Get());

    // Everything below draws in surface coordinates.
    t->SetTransform(D2D1::Matrix3x2F::Translation(S, S));

    t->FillRoundedRectangle({ {0.0f, 0.0f, W, H}, mt.radius * s, mt.radius * s }, brTint.Get());
    t->FillRectangle(D2D1_RECT_F{ 1.0f, 1.0f, W - 1.0f, 1.0f + 1.0f * s }, brHi.Get());
    t->DrawRoundedRectangle({ {0.0f, 0.0f, W, H}, mt.radius * s, mt.radius * s }, brBorder.Get(), 1.0f);

    // Rows (clipped to the surface; scrolled rows slide under the padding).
    t->PushAxisAlignedClip(D2D1_RECT_F{ 1.0f, 1.0f, W - 1.0f, H - 1.0f },
                           D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    float padH  = mt.padH * s;
    float textX = padH + mt.iconCol * s + mt.gap * s;
    float iconSz = mt.iconSz * s;

    for (size_t i = 0; i < items->size(); i++) {
        float top = rowY[i] - (float)scroll;
        float rh  = rowH[i];
        if (top + rh <= 0.0f || top >= H) continue;
        const CtxItem& it = (*items)[i];

        if (it.sep) {
            float cy = top + rh * 0.5f;
            t->FillRectangle(D2D1_RECT_F{ padH + 2.0f * s, cy - 0.5f,
                                          W - padH - 2.0f * s, cy + 0.5f }, brSep.Get());
            continue;
        }

        // Hover plate; while the button is held it deepens to the pressed
        // plate so the click is acknowledged before the item fires.
        if (hover == (int)i && !it.disabled) {
            t->FillRoundedRectangle(
                D2D1_ROUNDED_RECT{ {padH, top + 1.5f * s, W - padH, top + rh - 1.5f * s},
                                   mt.rowRad * s, mt.rowRad * s },
                pressed ? brPressed.Get() : brHover.Get());
        }

        // Icon gutter: real bitmap when present, check mark for check verbs.
        float ix = padH + (mt.iconCol * s - iconSz) * 0.5f;
        float iy = top + (rh - iconSz) * 0.5f;
        if (i < icons.size() && icons[i]) {
            t->DrawBitmap(icons[i].Get(), D2D1_RECT_F{ ix, iy, ix + iconSz, iy + iconSz },
                          it.disabled ? 0.4f : 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        } else if (it.checked) {
            float cx = padH + mt.iconCol * s * 0.5f, cy = top + rh * 0.5f;
            t->DrawLine({ cx - 4.0f * s, cy },        { cx - 1.2f * s, cy + 2.8f * s },
                        brCheck.Get(), 1.5f * s);
            t->DrawLine({ cx - 1.2f * s, cy + 2.8f * s }, { cx + 4.2f * s, cy - 2.8f * s },
                        brCheck.Get(), 1.5f * s);
        }

        // Checked items that already show a glyph (the tray's toggles) get their check
        // mark at the right edge, so "on" is visible; without a glyph the gutter holds it.
        if (it.checked && i < icons.size() && icons[i]) {
            float cx = W - padH - 14.0f * s, cy = top + rh * 0.5f;
            t->DrawLine({ cx - 4.0f * s, cy },        { cx - 1.2f * s, cy + 2.8f * s },
                        brCheck.Get(), 1.8f * s);
            t->DrawLine({ cx - 1.2f * s, cy + 2.8f * s }, { cx + 4.2f * s, cy - 2.8f * s },
                        brCheck.Get(), 1.8f * s);
        }

        // Label (pre-measured layout; over-long text trims at the column edge)
        if (texts[i]) {
            DWRITE_TEXT_METRICS m = {};
            texts[i]->GetMetrics(&m);
            float ty = top + (rh - m.height) * 0.5f;
            t->DrawTextLayout(D2D1_POINT_2F{ textX, ty }, texts[i].Get(),
                              it.disabled ? brDim.Get() : brText.Get());
        }

        // Submenu chevron
        if (it.hasSub) {
            float cy  = top + rh * 0.5f;
            float cx0 = W - padH - mt.chevr * s * 0.5f - 2.0f * s;
            ID2D1Brush* cb = it.disabled ? brDim.Get() : brChev.Get();
            t->DrawLine({ cx0, cy - 3.5f * s }, { cx0 + 3.5f * s, cy }, cb, 1.3f * s);
            t->DrawLine({ cx0 + 3.5f * s, cy }, { cx0, cy + 3.5f * s }, cb, 1.3f * s);
        }
    }
    t->PopAxisAlignedClip();

    t->SetTransform(D2D1::Matrix3x2F::Identity());
    render->EndDraw();
}

}  // namespace FluentMenuImpl
