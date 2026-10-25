// Scrollbar geometry of list-mode fences.
#include "render/list_scrollbar.h"
#include "test_util.h"

#include <cmath>

using test::Check;

static bool Near(float a, float b) { return std::fabs(a - b) < 0.001f; }

int main() {
    // A 200x300 fence whose list starts at y=40 (260 px visible), rows total 1040 px.
    {
        ListScrollbar bar = ComputeListScrollbar(200, 300, 40, 1040, 0);
        Check(bar.visible, "content taller than the viewport shows a scrollbar");
        Check(Near(bar.viewportHeight, 260), "viewport is the area below the body top");
        Check(Near(bar.maxScroll, 780), "max scroll = content - viewport");
        Check(Near(bar.trackX, 200 - kListScrollbarWidth - kListScrollbarPad), "track hugs the right border");
        Check(Near(bar.trackTop, 40 + kListScrollbarPad) && Near(bar.trackBottom, 300 - kListScrollbarPad),
              "track spans the body minus the padding");
        Check(Near(bar.thumbTop, bar.trackTop), "thumb starts at the top of the track at scroll 0");
        // thumb = track * viewport / content = 256 * 260 / 1040 = 64
        Check(Near(bar.thumbHeight, 64), "thumb height is proportional to the visible share");
    }
    {
        ListScrollbar bar = ComputeListScrollbar(200, 300, 40, 1040, 780);
        Check(Near(bar.thumbTop + bar.thumbHeight, bar.trackBottom), "thumb reaches the bottom at max scroll");
        ListScrollbar mid = ComputeListScrollbar(200, 300, 40, 1040, 390);
        Check(Near(mid.thumbTop, bar.trackTop + (bar.TrackHeight() - bar.thumbHeight) / 2),
              "half the scroll puts the thumb half way");
    }
    {
        ListScrollbar bar = ComputeListScrollbar(200, 300, 40, 200, 0);
        Check(!bar.visible && bar.maxScroll == 0.0f, "content that fits needs no scrollbar");
        ListScrollbar exact = ComputeListScrollbar(200, 300, 40, 260, 0);
        Check(!exact.visible, "content exactly as tall as the viewport needs none either");
    }
    {
        ListScrollbar huge = ComputeListScrollbar(200, 300, 40, 1000000, 0);
        Check(Near(huge.thumbHeight, kListScrollbarMinThumb), "the thumb never gets smaller than the minimum");
    }
    {
        ListScrollbar bar = ComputeListScrollbar(200, 300, 40, 1040, 0);
        float range = bar.TrackHeight() - bar.thumbHeight;
        Check(Near(ScrollForThumbDrag(bar, 0, 0), 0), "no movement, no scrolling");
        Check(Near(ScrollForThumbDrag(bar, 0, range), bar.maxScroll), "dragging the full range scrolls to the end");
        Check(Near(ScrollForThumbDrag(bar, 100, range / 2), 100 + bar.maxScroll / 2), "drag distance is relative to the start offset");
        Check(Near(ScrollForThumbDrag(bar, 500, -range), 500 - bar.maxScroll), "dragging up scrolls back (callers clamp)");
        ListScrollbar none = ComputeListScrollbar(200, 300, 40, 100, 0);
        Check(Near(ScrollForThumbDrag(none, 7, 50), 7), "an invisible scrollbar does not scroll");
    }
    return test::Finish();
}
