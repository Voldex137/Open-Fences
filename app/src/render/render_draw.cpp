// One frame of a fence: DrawFence() builds a Frame and calls the Draw* steps
// below in painting order (background, title bar, search box, icons, scrollbar,
// marquee, border).
#include "render/render.h"
#include "core/layout_metrics.h"
#include "render/list_scrollbar.h"
#include "core/localization.h"

#include <algorithm>
#include <cmath>
#include <cwchar>

namespace {

// D2D1_BITMAP_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC. Declared in d2d1_1.h;
// spelled out numerically (its stable ABI value) so we don't require the
// D2D 1.1 headers.
const D2D1_BITMAP_INTERPOLATION_MODE kInterpHighQualityCubic =
    (D2D1_BITMAP_INTERPOLATION_MODE)1;

constexpr float kTitleTextPad = 14.0f;
constexpr D2D1_COLOR_F kRenamingTitleColor{ 0.10f, 0.22f, 0.42f, 0.92f };
constexpr D2D1_DRAW_TEXT_OPTIONS kLabelOptions =
    D2D1_DRAW_TEXT_OPTIONS_CLIP | D2D1_DRAW_TEXT_OPTIONS_NO_SNAP;

ComPtr<ID2D1SolidColorBrush> MakeBrush(ID2D1RenderTarget& ctx, const D2D1_COLOR_F& color) {
    ComPtr<ID2D1SolidColorBrush> brush;
    ctx.CreateSolidColorBrush(color, &brush);
    return brush;
}

// Width/height of `text` laid out with `format`; false when DirectWrite fails.
bool MeasureText(IDWriteFactory* dw, const wchar_t* text, size_t length, IDWriteTextFormat* format,
                 float maxW, float maxH, DWRITE_TEXT_METRICS& out) {
    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(dw->CreateTextLayout(text, (UINT32)length, format, maxW, maxH, &layout)) || !layout)
        return false;
    layout->GetMetrics(&out);
    return true;
}

// The Explorer-style translucent plate behind a hovered or selected icon.
void FillStatePlate(ID2D1RenderTarget& ctx, const FenceAppearance& app, const D2D1_RECT_F& rect,
                    float radius, bool hovered, bool selected) {
    float fillA, edgeA, cr, cg, cb;
    if (selected) {
        cr = app.accent[0]; cg = app.accent[1]; cb = app.accent[2];
        fillA = hovered ? 0.34f : 0.26f;
        edgeA = hovered ? 0.95f : 0.75f;
    } else {
        cr = cg = cb = 1.0f;
        fillA = 0.12f;
        edgeA = 0.28f;
    }
    D2D1_ROUNDED_RECT plate{ rect, radius, radius };
    auto fill = MakeBrush(ctx, D2D1::ColorF(cr, cg, cb, fillA));
    auto edge = MakeBrush(ctx, D2D1::ColorF(cr, cg, cb, edgeA));
    if (fill) ctx.FillRoundedRectangle(plate, fill.Get());
    if (edge) ctx.DrawRoundedRectangle(plate, edge.Get(), 1.0f);
}

}  // namespace

// Everything one frame needs: the render target, the fence's size and the
// arguments of DrawFence().
struct RenderContext::Frame {
    ID2D1RenderTarget& ctx;
    float w, h, radius, titleH;
    const std::wstring& title;
    const std::vector<IconEntry>& icons;
    bool renaming, dragOver, collapsed;
    int hoverIcon;
    const std::vector<std::wstring>* selectedPaths;
    bool dragActive;
    const RECT* marquee;
    bool chevronHover, chevronDown;
    float scrollY;
    const FenceViewState* view;

    bool Loading() const { return view && view->loading; }
    int VisibleCount() const {
        return view && view->filter ? (int)view->filter->size() : (int)icons.size();
    }
};

// Geometry shared by the icons of one frame.
struct RenderContext::IconMetrics {
    bool listMode;
    float pad, plateRadius;
    float listIconSize, listRowH;
    float cellW, cellH, glyphSize;
    size_t selectedCount;
};

// What is going on with one icon.
struct RenderContext::IconState {
    bool hovered, selected, dragged, renaming;
};

bool RenderContext::BeginDraw() { m_d2dTarget->BeginDraw(); return true; }
bool RenderContext::EndDraw() { return SUCCEEDED(m_d2dTarget->EndDraw()); }

bool RenderContext::DrawFence(const std::wstring& title,
                              const std::vector<IconEntry>& icons,
                              bool renaming, bool dragOver, bool collapsed,
                              int hoverIcon,
                              const std::vector<std::wstring>* selectedPaths,
                              bool dragActive,
                              const RECT* marquee,
                              bool chevronHover, bool chevronDown,
                              float scrollY,
                              const FenceViewState* view) {
    Frame f{ *m_d2dTarget.Get(), (float)m_width, (float)m_height, m_app.r, m_app.titleH,
             title, icons, renaming, dragOver, collapsed, hoverIcon, selectedPaths,
             dragActive, marquee, chevronHover, chevronDown, scrollY, view };
    f.ctx.Clear(D2D1::ColorF(0, 0, 0, 0));

    // Collapsed: the whole window is the title bar (one rounded pill).
    if (f.collapsed) {
        DrawCollapsedFence(f);
        return true;
    }

    D2D1_ROUNDED_RECT shape{ { 0, 0, f.w, f.h }, f.radius, f.radius };
    // Frosted glass: blurred desktop snapshot first, translucent tint on top.
    if (m_backdropBrush) f.ctx.FillRoundedRectangle(shape, m_backdropBrush.Get());
    f.ctx.FillRoundedRectangle(shape, m_bgBrush.Get());

    DrawTitleBar(f);
    float bodyTop = DrawSearchBox(f);

    if (f.Loading()) {
        DrawBodyNotice(f, bodyTop, L10n::Tr("fence.loading"));
    } else if (!f.icons.empty()) {
        DrawIcons(f, bodyTop);
    }

    // No-results placeholder (filter active, zero matches)
    if (view && view->filter && view->filter->empty() && view->searchText &&
        !view->searchText->empty() && !f.icons.empty())
        DrawBodyNotice(f, bodyTop, L10n::Tr("fence.no_results"));

    DrawListScrollbar(f, bodyTop);
    DrawMarquee(f);

    // Border (drawn last, on top): vertical gradient, bright top edge.
    if (m_borderGradBrush) f.ctx.DrawRoundedRectangle(shape, m_borderGradBrush.Get(), 1.0f);

    if (f.dragOver) DrawDragOverAccent(f);
    return true;
}

// ── Title area ──

void RenderContext::DrawFenceIcon(const Frame& f, float x, float y, float size) {
    // An empty path means "a folder": a name with no extension that does not exist
    // makes LoadFileIcon fall back to the generic folder icon.
    const wchar_t* path = m_app.iconPath[0] ? m_app.iconPath : L"openFences-folder";
    ID2D1Bitmap* bmp = LoadFileIcon(path);
    if (!bmp) return;
    f.ctx.DrawBitmap(bmp, D2D1::RectF(x, y, x + size, y + size), 1.0f,
                     D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
}

// Collapsed into a tile: the chosen icon with the title underneath, like a desktop icon.
void RenderContext::DrawCollapsedTile(const Frame& f) {
    D2D1_ROUNDED_RECT shape{ { 0, 0, f.w, f.h }, f.radius, f.radius };
    if (m_backdropBrush) f.ctx.FillRoundedRectangle(shape, m_backdropBrush.Get());
    if (f.renaming) {
        auto renaming = MakeBrush(f.ctx, kRenamingTitleColor);
        f.ctx.FillRoundedRectangle(shape, renaming.Get());
    } else {
        f.ctx.FillRoundedRectangle(shape, m_titleBrush.Get());
    }
    DrawTopHighlight(f);
    float glyph = (std::min)(m_app.imgSize, f.w - 8.0f);
    DrawFenceIcon(f, (f.w - glyph) * 0.5f, 6.0f, glyph);
    D2D1_RECT_F label{ 3.0f, 6.0f + glyph + 2.0f, f.w - 3.0f, f.h - 2.0f };
    f.ctx.DrawText(f.title.c_str(), (UINT32)f.title.size(), m_iconFormat.Get(), label,
                   m_textBrush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
    if (m_borderGradBrush) f.ctx.DrawRoundedRectangle(shape, m_borderGradBrush.Get(), 1.0f);
}

void RenderContext::DrawCollapsedFence(const Frame& f) {
    if (m_app.collapseStyle == 1) { DrawCollapsedTile(f); return; }
    D2D1_ROUNDED_RECT shape{ { 0, 0, f.w, f.h }, f.radius, f.radius };
    if (m_backdropBrush) f.ctx.FillRoundedRectangle(shape, m_backdropBrush.Get());
    if (f.renaming) {
        auto renaming = MakeBrush(f.ctx, kRenamingTitleColor);
        f.ctx.FillRoundedRectangle(shape, renaming.Get());
    } else {
        f.ctx.FillRoundedRectangle(shape, m_titleBrush.Get());
    }
    DrawTopHighlight(f);
    DrawTitleText(f);
    DrawChevron(f, true);
    if (m_borderGradBrush) f.ctx.DrawRoundedRectangle(shape, m_borderGradBrush.Get(), 1.0f);
}

void RenderContext::DrawTitleBar(const Frame& f) {
    // A flat FillRectangle would paint square corners over the window's
    // rounded top shape; the cached path carries the same top arcs.
    D2D1_RECT_F titleRect{ 0, 0, f.w, f.titleH };
    ComPtr<ID2D1SolidColorBrush> renaming;
    ID2D1Brush* titleFill = m_titleBrush.Get();
    if (f.renaming) {
        renaming = MakeBrush(f.ctx, kRenamingTitleColor);
        if (renaming) titleFill = renaming.Get();
    }
    if (m_titleBarPath) f.ctx.FillGeometry(m_titleBarPath.Get(), titleFill);
    else f.ctx.FillRectangle(titleRect, titleFill);

    DrawTopHighlight(f);

    D2D1_RECT_F sepRect{ 0, f.titleH - 0.5f, f.w, f.titleH + 0.5f };
    f.ctx.FillRectangle(sepRect, m_sepBrush.Get());

    DrawTitleText(f);
    DrawChevron(f, false);
}

// Subtle 3D edge: cached brush, redraws are hot.
void RenderContext::DrawTopHighlight(const Frame& f) {
    if (!m_topHiBrush) return;
    if (m_topHiPath) f.ctx.FillGeometry(m_topHiPath.Get(), m_topHiBrush.Get());
    else f.ctx.FillRectangle(D2D1_RECT_F{ 0, 0, f.w, 1.5f }, m_topHiBrush.Get());
}

// Left-aligned titles get a left inset only; center/right-aligned ones get
// symmetric insets so the alignment reads correctly within the visible title
// region (the right edge stops before the chevron box).
void RenderContext::DrawTitleText(const Frame& f) {
    float right = (m_app.titleAlign == 0) ? (f.w - f.titleH) : (f.w - f.titleH - kTitleTextPad);
    float left = kTitleTextPad;
    if (m_app.titleIcon) {
        float icon = f.titleH * 0.62f;
        DrawFenceIcon(f, kTitleTextPad, (f.titleH - icon) * 0.5f, icon);
        left += icon + kTitleTextPad * 0.6f;
    }
    D2D1_RECT_F textRect{ left, 0, right, f.titleH };
    f.ctx.DrawText(f.title.c_str(), (UINT32)f.title.size(), m_titleFormat.Get(), textRect,
                   m_textBrush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

// Collapse/expand chevron in the title bar's top-right corner. Points the way
// the body will move when clicked: up = fold away, down = unfold. The geometry
// is the cached m_chevronPath (one open figure, a true round join at the apex).
// Idle it is dimmer than the title text; hover brightens it and shows a
// caption-button plate, press dims the plate. State changes go through
// SetColor on the cached brushes, so the redraw path allocates nothing.
void RenderContext::DrawChevron(const Frame& f, bool collapsedLook) {
    if (!m_chevronPath || !m_chevronBrush) return;
    const float th = f.titleH;
    float cy = th * 0.5f;
    // Caption-button style plate under hover/press.
    if ((f.chevronHover || f.chevronDown) && m_chevronPlate) {
        const float inset = th * 0.14f;
        D2D1_ROUNDED_RECT plate{
            { f.w - th + inset, inset, f.w - inset, th - inset },
            th * 0.16f, th * 0.16f };
        m_chevronPlate->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f, f.chevronDown ? 0.05f : 0.08f));
        f.ctx.FillRoundedRectangle(plate, m_chevronPlate.Get());
    }
    const float alpha = f.chevronDown ? 0.75f : (f.chevronHover ? 0.93f : 0.62f);
    m_chevronBrush->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f, alpha));
    const float strokeW = (std::max)(1.6f, th * 0.055f);
    // Collapsed: mirror the (upward) path about the title-bar midline.
    // Row-vector matrices: A * B applies A first.
    f.ctx.SetTransform(collapsedLook
        ? D2D1::Matrix3x2F::Scale(1.0f, -1.0f) * D2D1::Matrix3x2F::Translation(0.0f, 2.0f * cy)
        : D2D1::Matrix3x2F::Identity());
    f.ctx.DrawGeometry(m_chevronPath.Get(), m_chevronBrush.Get(), strokeW, m_chevronStroke.Get());
    f.ctx.SetTransform(D2D1::Matrix3x2F::Identity());
}

// ── Search box (mapped fences). Returns the top of the body below it. ──

float RenderContext::DrawSearchBox(const Frame& f) {
    const FenceViewState* view = f.view;
    if (!view || !view->searchText) return f.titleH;

    float sh = m_app.searchH;
    float sx = 8.0f, sy = f.titleH + 4.0f, sw = f.w - 16.0f, sbh = sh - 8.0f;

    // Field background
    D2D1_ROUNDED_RECT box{ { sx, sy, sx + sw, sy + sbh }, 4.0f, 4.0f };
    auto fill = MakeBrush(f.ctx, D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.07f));
    if (fill) f.ctx.FillRoundedRectangle(box, fill.Get());
    auto edge = MakeBrush(f.ctx, D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.10f));
    if (edge) f.ctx.DrawRoundedRectangle(box, edge.Get(), 1.0f);

    // Magnifier icon (circle + handle)
    float mgCx = sx + 14.0f, mgCy = sy + sbh * 0.5f, mgR = 5.0f;
    auto glass = MakeBrush(f.ctx, D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.35f));
    if (glass) {
        D2D1_ELLIPSE ring{ { mgCx, mgCy }, mgR, mgR };
        f.ctx.DrawEllipse(ring, glass.Get(), 1.5f);
        float a45 = 3.14159f / 4.0f;
        float hx = mgCx + mgR * cosf(a45), hy = mgCy + mgR * sinf(a45);
        float hx2 = mgCx + (mgR + 3.5f) * cosf(a45);
        float hy2 = mgCy + (mgR + 3.5f) * sinf(a45);
        f.ctx.DrawLine({ hx, hy }, { hx2, hy2 }, glass.Get(), 1.5f);
    }

    // Text or placeholder
    float tx = sx + 28.0f;
    D2D1_RECT_F textRect{ tx, sy, sx + sw - 8.0f, sy + sbh };
    if (view->searchText->empty()) {
        auto hint = MakeBrush(f.ctx, D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.25f));
        const wchar_t* hintText = L10n::Tr("fence.search_hint");
        if (hint) f.ctx.DrawText(hintText, (UINT32)wcslen(hintText), m_listFormat.Get(), textRect,
                                 hint.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
        m_searchTextW = 0.0f;   // caret at start of field
    } else {
        f.ctx.DrawText(view->searchText->c_str(), (UINT32)view->searchText->size(),
                       m_listFormat.Get(), textRect, m_iconTextBrush.Get(),
                       D2D1_DRAW_TEXT_OPTIONS_CLIP);
        if (*view->searchText != m_searchTextCache) {
            m_searchTextCache = *view->searchText;
            DWRITE_TEXT_METRICS tm = {};
            if (MeasureText(m_dwFactory.Get(), view->searchText->c_str(), view->searchText->size(),
                            m_listFormat.Get(), 2000.0f, sbh, tm))
                m_searchTextW = tm.width;
        }
    }

    // Caret (blinks regardless of whether text is present)
    if (view->caretOn) {
        float cx = tx + m_searchTextW + 1.0f;
        auto caret = MakeBrush(f.ctx, D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.7f));
        if (caret) f.ctx.FillRectangle(D2D1_RECT_F{ cx, sy + 4.0f, cx + 1.5f, sy + sbh - 4.0f },
                                       caret.Get());
    }
    return f.titleH + sh;
}

// "Loading…" / "No results" over the body.
void RenderContext::DrawBodyNotice(const Frame& f, float bodyTop, const wchar_t* text) {
    D2D1_RECT_F area{ 4.0f, bodyTop, f.w - 4.0f, f.h };
    f.ctx.DrawText(text, (UINT32)wcslen(text), m_iconFormat.Get(), area, m_iconTextBrush.Get(),
                   D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

// ── Icons (free placement: each entry carries its own position) ──

void RenderContext::DrawIcons(const Frame& f, float bodyTop) {
    IconMetrics m;
    m.listMode = (m_app.displayMode == 1);
    m.pad = kGridPad;
    // Explorer-style state plates share one rounded radius (half the fence
    // corner so the plate reads as a child of the panel).
    m.plateRadius = m_app.r * 0.5f;
    m.selectedCount = f.selectedPaths ? f.selectedPaths->size() : 0;
    // List mode: compact rows, small icon left, text right
    m.listIconSize = m_app.iconSize * 1.8f;
    m.listRowH = m_app.iconSize * 2.6f;
    m.cellW = m.listMode ? (f.w - 2 * m.pad) : m_app.cellW;
    m.cellH = m.listMode ? m.listRowH : m_app.cellH;
    m.glyphSize = m.listMode ? m.listIconSize : m_app.imgSize;

    const int total = f.VisibleCount();
    for (int vi = 0; vi < total; vi++) {
        int i = f.view && f.view->filter ? (*f.view->filter)[vi] : vi;
        IconState st;
        st.hovered  = i == f.hoverIcon;
        st.selected = f.selectedPaths &&
            std::find(f.selectedPaths->begin(), f.selectedPaths->end(), f.icons[i].path) !=
                f.selectedPaths->end();
        st.dragged  = f.dragActive && st.selected;
        st.renaming = f.view && f.view->renameIcon == i && f.view->iconRenameText != nullptr;

        if (m.listMode) DrawListRow(f, i, bodyTop, m, st);
        else            DrawGridCell(f, i, m, st);
    }
}

// List mode: small icon left, text right. Applies the scroll offset and clips
// to the body area (below the title bar).
void RenderContext::DrawListRow(const Frame& f, int i, float bodyTop, const IconMetrics& m,
                                const IconState& st) {
    const IconEntry& icon = f.icons[i];
    float cx = icon.x, cy = icon.y;
    float visY = cy - f.scrollY;
    if (visY >= f.h || visY + m.cellH <= bodyTop) return;

    float clipTop = (std::max)(visY, bodyTop);
    float clipH = m.cellH - (clipTop - visY);
    if (clipH < m.cellH * 0.5f) return;   // less than half visible, skip

    // Only rows that are actually visible need their glyph extracted.
    auto* iconBmp = LoadFileIcon(icon.path);
    const float glyphAlpha = st.dragged ? 0.55f : 1.0f;

    // Hover / selection plate (clipped to body)
    if ((st.hovered || st.selected) && !st.dragged && !st.renaming)
        FillStatePlate(f.ctx, m_app,
                       { cx + 1, clipTop + 1, cx + m.cellW - 1, clipTop + clipH - 1 },
                       m.plateRadius, st.hovered, st.selected);

    // Icon: vertically centered in the visible portion of the row
    float iconCY = clipTop + (std::max)(0.0f, clipH - m.listIconSize) * 0.5f;
    if (iconBmp && clipH > 2.0f) {
        auto sz = iconBmp->GetSize();
        float fit = (std::min)(m.listIconSize / sz.width, m.listIconSize / sz.height);
        float drawW = sz.width * fit;
        float drawH = sz.height * fit;
        float dx = cx + 2.0f + (m.listIconSize - drawW) * 0.5f;
        float dy = iconCY + (m.listIconSize - drawH) * 0.5f;
        f.ctx.DrawBitmap(iconBmp, D2D1_RECT_F{ dx, dy, dx + drawW, dy + drawH }, glyphAlpha,
                         kInterpHighQualityCubic, nullptr);
    }

    // Text: positioned in the visible row portion
    float tx = cx + m.listIconSize + 8.0f;
    D2D1_RECT_F textRect{ tx, clipTop, cx + m.cellW - 4.0f, clipTop + clipH };
    auto* listFmt = m_listFormat.Get() ? m_listFormat.Get() : m_iconFormat.Get();
    auto* textBrush = st.selected ? m_textBrush.Get() : m_iconTextBrush.Get();
    if (st.renaming) {
        const FenceViewState& view = *f.view;
        D2D1_ROUNDED_RECT box{ { tx - 2.0f, clipTop + 1.0f, cx + m.cellW - 2.0f, clipTop + clipH - 1.0f },
                               m.plateRadius * 0.5f, m.plateRadius * 0.5f };
        auto back = MakeBrush(f.ctx, D2D1::ColorF(0.11f, 0.12f, 0.15f, 0.96f));
        auto ring = MakeBrush(f.ctx, D2D1::ColorF(m_app.accent[0], m_app.accent[1], m_app.accent[2], 0.9f));
        if (back) f.ctx.FillRoundedRectangle(box, back.Get());
        if (ring) f.ctx.DrawRoundedRectangle(box, ring.Get(), 1.0f);
        f.ctx.DrawText(view.iconRenameText->c_str(), (UINT32)view.iconRenameText->size(), listFmt,
                       textRect, m_textBrush.Get(), kLabelOptions);
        if (view.renameCaret >= 0) {
            std::wstring prefix = view.iconRenameText->substr(0, (size_t)view.renameCaret);
            DWRITE_TEXT_METRICS tm = {};
            float prefixW = 0.0f;
            if (MeasureText(m_dwFactory.Get(), prefix.c_str(), prefix.size(), listFmt,
                            m.cellW - m.listIconSize - 8.0f, m.cellH, tm))
                prefixW = tm.width;
            auto caret = MakeBrush(f.ctx, D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.85f));
            if (caret) f.ctx.FillRectangle(
                D2D1_RECT_F{ tx + prefixW, clipTop + 2.0f, tx + prefixW + 1.5f, clipTop + clipH - 2.0f },
                caret.Get());
        }
    } else if (st.dragged) {
        auto faded = MakeBrush(f.ctx, D2D1::ColorF(m_app.iconText[0], m_app.iconText[1],
                                                   m_app.iconText[2], m_app.iconText[3] * glyphAlpha));
        f.ctx.DrawText(icon.name.c_str(), (UINT32)icon.name.size(), listFmt, textRect,
                       faded ? faded.Get() : m_iconTextBrush.Get(), kLabelOptions);
    } else {
        f.ctx.DrawText(icon.name.c_str(), (UINT32)icon.name.size(), listFmt, textRect, textBrush,
                       kLabelOptions);
    }
}

// Grid mode: glyph on top, label below, in a cell of the icon grid.
void RenderContext::DrawGridCell(const Frame& f, int i, const IconMetrics& m, const IconState& st) {
    const IconEntry& icon = f.icons[i];
    float cx = icon.x, cy = icon.y;
    const float glyphAlpha = st.dragged ? 0.55f : 1.0f;
    auto* iconBmp = LoadFileIcon(icon.path);

    // Hover / selection plate
    if ((st.hovered || st.selected) && !st.dragged && !st.renaming)
        FillStatePlate(f.ctx, m_app, { cx + 1, cy + 1, cx + m.cellW - 1, cy + m.cellH - 1 },
                       m.plateRadius, st.hovered, st.selected);

    if (iconBmp) {
        auto sz = iconBmp->GetSize();
        float fit = (std::min)(m.glyphSize / sz.width, m.glyphSize / sz.height);
        float drawW = sz.width * fit;
        float drawH = sz.height * fit;
        float ix = cx + (m.cellW - drawW) * 0.5f;
        float iy = cy + 2.0f + (m.glyphSize - drawH) * 0.5f;
        f.ctx.DrawBitmap(iconBmp, D2D1_RECT_F{ ix, iy, ix + drawW, iy + drawH }, glyphAlpha,
                         kInterpHighQualityCubic, nullptr);
    }

    D2D1_RECT_F textRect{ cx, cy + m.glyphSize + 4.0f, cx + m.cellW, cy + m.cellH };
    const wchar_t* name = icon.name.c_str();
    const UINT32 nameLen = (UINT32)icon.name.size();

    if (st.renaming) {
        // Explorer-style rename edit box: opaque backing + accent ring, sized
        // to the live text.
        const FenceViewState& view = *f.view;
        float textW = 0.0f, textH = m.cellH - (m.glyphSize + 4.0f);
        DWRITE_TEXT_METRICS tm = {};
        if (MeasureText(m_dwFactory.Get(), view.iconRenameText->c_str(), view.iconRenameText->size(),
                        m_iconFormat.Get(), m.cellW, m.cellH, tm)) {
            textW = tm.width; textH = tm.height;
        }
        float bx = cx + (m.cellW - textW) * 0.5f;
        D2D1_ROUNDED_RECT box{ { bx - 3.0f, textRect.top - 1.0f, bx + textW + 3.0f, textRect.top + textH + 1.5f },
                                m.plateRadius * 0.5f, m.plateRadius * 0.5f };
        auto back = MakeBrush(f.ctx, D2D1::ColorF(0.11f, 0.12f, 0.15f, 0.96f));
        auto ring = MakeBrush(f.ctx, D2D1::ColorF(m_app.accent[0], m_app.accent[1], m_app.accent[2], 0.9f));
        if (back) f.ctx.FillRoundedRectangle(box, back.Get());
        if (ring) f.ctx.DrawRoundedRectangle(box, ring.Get(), 1.0f);
        f.ctx.DrawText(view.iconRenameText->c_str(), (UINT32)view.iconRenameText->size(),
                       m_iconFormat.Get(), textRect, m_textBrush.Get(), kLabelOptions);
        if (view.renameCaret >= 0) {
            // The label format centers the text in the full cell width; the
            // caret lands right after the typed prefix.
            std::wstring prefix = view.iconRenameText->substr(0, (size_t)view.renameCaret);
            DWRITE_TEXT_METRICS pm = {};
            float prefixW = 0.0f;
            if (MeasureText(m_dwFactory.Get(), prefix.c_str(), prefix.size(), m_iconFormat.Get(),
                            m.cellW, m.cellH, pm))
                prefixW = pm.width;
            float caretX = cx + (m.cellW - textW) * 0.5f + prefixW;
            auto caret = MakeBrush(f.ctx, D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.85f));
            if (caret) f.ctx.FillRectangle(
                D2D1_RECT_F{ caretX, textRect.top + 1.0f, caretX + 1.5f, textRect.top + textH + 1.0f },
                caret.Get());
        }
    } else if (st.dragged) {
        auto faded = MakeBrush(f.ctx, D2D1::ColorF(m_app.iconText[0], m_app.iconText[1],
                                                   m_app.iconText[2], m_app.iconText[3] * glyphAlpha));
        f.ctx.DrawText(name, nameLen, m_iconFormat.Get(), textRect,
                       faded ? faded.Get() : m_iconTextBrush.Get(), kLabelOptions);
    } else if (st.selected && m.selectedCount == 1) {
        // A single selected icon gets the accent label box under its (full) name.
        if (icon.path != m_selLabelPath) {
            DWRITE_TEXT_METRICS tm = {};
            if (MeasureText(m_dwFactory.Get(), name, nameLen, m_iconFormat.Get(), m.cellW, m.cellH, tm)) {
                m_selLabelW = tm.width;
                m_selLabelH = tm.height;
                m_selLabelPath = icon.path;
            }
        }
        if (!m_selLabelPath.empty()) {
            float bx = cx + (m.cellW - m_selLabelW) * 0.5f;
            D2D1_ROUNDED_RECT box{ { bx - 3.0f, textRect.top - 1.0f,
                                     bx + m_selLabelW + 3.0f, textRect.top + m_selLabelH + 1.5f },
                                   m_app.r * 0.25f, m_app.r * 0.25f };
            auto accent = MakeBrush(f.ctx, D2D1::ColorF(m_app.accent[0], m_app.accent[1],
                                                        m_app.accent[2], 0.92f));
            if (accent) f.ctx.FillRoundedRectangle(box, accent.Get());
        }
        f.ctx.DrawText(name, nameLen, m_iconFormat.Get(), textRect, m_textBrush.Get(), kLabelOptions);
    } else if (st.selected) {
        f.ctx.DrawText(name, nameLen, m_iconFormat.Get(), textRect, m_textBrush.Get(), kLabelOptions);
    } else {
        f.ctx.DrawText(name, nameLen, m_iconFormat.Get(), textRect, m_iconTextBrush.Get(), kLabelOptions);
    }
}

// ── Overlays ──

void RenderContext::DrawListScrollbar(const Frame& f, float bodyTop) {
    if (m_app.displayMode != 1 || f.icons.empty()) return;
    float rowH = m_app.iconSize * 2.6f;
    ListScrollbar bar = ComputeListScrollbar(f.w, f.h, bodyTop, (float)f.VisibleCount() * rowH, f.scrollY);
    if (!bar.visible) return;

    const float sbW = kListScrollbarWidth;
    D2D1_ROUNDED_RECT track{ { bar.trackX, bar.trackTop, bar.trackX + sbW, bar.trackBottom },
                             sbW * 0.5f, sbW * 0.5f };
    auto trackBrush = MakeBrush(f.ctx, D2D1::ColorF(1, 1, 1, 0.08f));
    if (trackBrush) f.ctx.FillRoundedRectangle(track, trackBrush.Get());

    D2D1_ROUNDED_RECT thumb{ { bar.trackX, bar.thumbTop, bar.trackX + sbW, bar.thumbTop + bar.thumbHeight },
                             sbW * 0.5f, sbW * 0.5f };
    auto thumbBrush = MakeBrush(f.ctx, D2D1::ColorF(1, 1, 1, 0.25f));
    if (thumbBrush) f.ctx.FillRoundedRectangle(thumb, thumbBrush.Get());
}

// Rubber-band marquee (left-drag multi-select, desktop style). Square corners
// like Explorer's band. The brushes are cached (redraws fire on every mouse
// move while the band is up) and share the accent color with the selection
// plates; the light fill keeps swept icons visible through the band.
void RenderContext::DrawMarquee(const Frame& f) {
    const RECT* mq = f.marquee;
    if (!mq || mq->right <= mq->left || mq->bottom <= mq->top) return;
    D2D1_RECT_F rect{ (float)mq->left, (float)mq->top, (float)mq->right, (float)mq->bottom };
    if (m_marqueeFillBrush) f.ctx.FillRectangle(rect, m_marqueeFillBrush.Get());
    if (m_marqueeEdgeBrush) f.ctx.DrawRectangle(rect, m_marqueeEdgeBrush.Get(), 1.0f);
}

void RenderContext::DrawDragOverAccent(const Frame& f) {
    auto accent = MakeBrush(f.ctx, D2D1::ColorF(m_app.accent[0], m_app.accent[1], m_app.accent[2], 0.55f));
    if (!accent) return;
    D2D1_ROUNDED_RECT ring{ { 1.5f, 1.5f, f.w - 1.5f, f.h - 1.5f }, f.radius - 1, f.radius - 1 };
    f.ctx.DrawRoundedRectangle(ring, accent.Get(), 2.0f);
}
