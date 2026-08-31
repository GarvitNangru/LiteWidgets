#pragma once
#ifndef LAYOUT_H
#define LAYOUT_H

#include <windows.h>
#include <stdbool.h>

#define LW_MAX_MONITORS 16

/*
 * Anchors let a config survive a resolution change or a move to another
 * machine: `x`/`y` are an offset from the anchor point on the target monitor
 * rather than a raw desktop coordinate.
 */
typedef enum {
    ANCHOR_TOP_LEFT = 0, ANCHOR_TOP_CENTER,    ANCHOR_TOP_RIGHT,
    ANCHOR_LEFT,         ANCHOR_CENTER,        ANCHOR_RIGHT,
    ANCHOR_BOTTOM_LEFT,  ANCHOR_BOTTOM_CENTER, ANCHOR_BOTTOM_RIGHT,
    ANCHOR__COUNT
} Anchor;

int         Layout_ParseAnchor(const char* text);
const char* Layout_AnchorName(int anchor);

/* Number of attached monitors, refreshed on every call. */
int  Layout_MonitorCount(void);

/* Monitor bounds. `index` < 0 or out of range resolves to the primary. */
bool Layout_MonitorRect(int index, RECT* out);

/* Index of the monitor containing a point, or 0 when nothing matches. */
int  Layout_MonitorFromPoint(POINT pt);

/* anchor + offset -> absolute desktop position of the widget's top-left. */
POINT Layout_Resolve(int anchor, int monitor, int offsetX, int offsetY, int width, int height);

/* Inverse of Layout_Resolve: absolute position -> offset from the anchor. */
POINT Layout_ToOffset(int anchor, int monitor, POINT position, int width, int height);

#endif /* LAYOUT_H */
