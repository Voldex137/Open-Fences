#include "render/list_scrollbar.h"

#include <algorithm>

ListScrollbar ComputeListScrollbar(float width, float height, float bodyTop,
                                   float contentHeight, float scrollY) {
    ListScrollbar bar;
    bar.viewportHeight = height - bodyTop;
    bar.maxScroll = contentHeight - bar.viewportHeight;
    bar.visible = bar.maxScroll > 0.0f;
    if (!bar.visible) {
        bar.maxScroll = 0.0f;
        return bar;
    }
    bar.trackX = width - kListScrollbarWidth - kListScrollbarPad;
    bar.trackTop = bodyTop + kListScrollbarPad;
    bar.trackBottom = height - kListScrollbarPad;
    float trackH = bar.TrackHeight();
    bar.thumbHeight = (std::max)(kListScrollbarMinThumb, trackH * bar.viewportHeight / contentHeight);
    bar.thumbTop = bar.trackTop + (scrollY / bar.maxScroll) * (trackH - bar.thumbHeight);
    return bar;
}

float ScrollForThumbDrag(const ListScrollbar& bar, float startOffset, float dy) {
    float range = bar.TrackHeight() - bar.thumbHeight;
    if (!bar.visible || range <= 0.0f) return startOffset;
    return startOffset + (dy / range) * bar.maxScroll;
}
