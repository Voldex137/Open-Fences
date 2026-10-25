#pragma once

// Geometry of the scrollbar of a list-mode fence. Pure arithmetic, shared by
// the renderer (drawing) and the mouse handling (hit testing and dragging) so
// the two cannot disagree.

constexpr float kListScrollbarWidth = 6.0f;
constexpr float kListScrollbarPad = 2.0f;      // gap to the fence border and to the title area
constexpr float kListScrollbarMinThumb = 20.0f;

struct ListScrollbar {
    bool visible = false;          // the content is taller than the viewport
    float maxScroll = 0.0f;        // largest scroll offset
    float viewportHeight = 0.0f;   // height of the visible body
    float trackX = 0.0f;           // left edge of the track
    float trackTop = 0.0f, trackBottom = 0.0f;
    float thumbTop = 0.0f, thumbHeight = 0.0f;

    float TrackHeight() const { return trackBottom - trackTop; }
};

// `width` x `height`: fence size; `bodyTop`: where the list area starts (below
// the title bar and the search box); `contentHeight`: total height of all rows;
// `scrollY`: current scroll offset (0 .. maxScroll).
ListScrollbar ComputeListScrollbar(float width, float height, float bodyTop,
                                   float contentHeight, float scrollY);

// Scroll offset after the thumb was dragged `dy` pixels away from where the
// drag started (`startOffset`); unchanged when the thumb has no room to move.
// Not clamped: callers clamp to 0 .. maxScroll.
float ScrollForThumbDrag(const ListScrollbar& bar, float startOffset, float dy);
