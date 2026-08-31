#include "layout.h"

#include <string.h>

typedef struct {
    RECT rects[LW_MAX_MONITORS];
    int  count;
    int  primary;
} MonitorTable;

static BOOL CALLBACK CollectMonitor(HMONITOR hMon, HDC hdc, LPRECT rc, LPARAM param) {
    (void)hdc; (void)rc;
    MonitorTable* table = (MonitorTable*)param;
    if (table->count >= LW_MAX_MONITORS) return FALSE;

    MONITORINFO info = { sizeof(MONITORINFO) };
    if (!GetMonitorInfoA(hMon, &info)) return TRUE;

    if (info.dwFlags & MONITORINFOF_PRIMARY) table->primary = table->count;
    table->rects[table->count++] = info.rcMonitor;
    return TRUE;
}

/*
 * The table is rebuilt on demand rather than cached: monitors come and go
 * (docking, projectors), and enumerating a handful of displays is cheap
 * compared with getting the placement wrong.
 */
static void BuildTable(MonitorTable* table) {
    memset(table, 0, sizeof(*table));
    EnumDisplayMonitors(NULL, NULL, CollectMonitor, (LPARAM)table);

    if (table->count == 0) {
        table->rects[0].left = 0;
        table->rects[0].top = 0;
        table->rects[0].right = GetSystemMetrics(SM_CXSCREEN);
        table->rects[0].bottom = GetSystemMetrics(SM_CYSCREEN);
        table->count = 1;
        table->primary = 0;
    }
}

int Layout_MonitorCount(void) {
    MonitorTable table;
    BuildTable(&table);
    return table.count;
}

bool Layout_MonitorRect(int index, RECT* out) {
    if (!out) return false;
    MonitorTable table;
    BuildTable(&table);

    if (index < 0 || index >= table.count) index = table.primary;
    *out = table.rects[index];
    return true;
}

int Layout_MonitorFromPoint(POINT pt) {
    MonitorTable table;
    BuildTable(&table);
    for (int i = 0; i < table.count; i++) {
        const RECT* r = &table.rects[i];
        if (pt.x >= r->left && pt.x < r->right && pt.y >= r->top && pt.y < r->bottom)
            return i;
    }
    return table.primary;
}

static const struct { const char* name; int value; } kAnchorNames[] = {
    { "top_left",      ANCHOR_TOP_LEFT      },
    { "top_center",    ANCHOR_TOP_CENTER    },
    { "top_right",     ANCHOR_TOP_RIGHT     },
    { "left",          ANCHOR_LEFT          },
    { "center",        ANCHOR_CENTER        },
    { "right",         ANCHOR_RIGHT         },
    { "bottom_left",   ANCHOR_BOTTOM_LEFT   },
    { "bottom_center", ANCHOR_BOTTOM_CENTER },
    { "bottom_right",  ANCHOR_BOTTOM_RIGHT  },
};

int Layout_ParseAnchor(const char* text) {
    if (!text || !text[0]) return ANCHOR_TOP_LEFT;
    for (int i = 0; i < ANCHOR__COUNT; i++)
        if (_stricmp(kAnchorNames[i].name, text) == 0) return kAnchorNames[i].value;
    return ANCHOR_TOP_LEFT;
}

const char* Layout_AnchorName(int anchor) {
    if (anchor < 0 || anchor >= ANCHOR__COUNT) return "top_left";
    return kAnchorNames[anchor].name;
}

/* Fractions of the free space used by each anchor column / row. */
static void AnchorFactors(int anchor, float* fx, float* fy) {
    static const float kf[3] = { 0.0f, 0.5f, 1.0f };
    if (anchor < 0 || anchor >= ANCHOR__COUNT) anchor = ANCHOR_TOP_LEFT;
    *fx = kf[anchor % 3];
    *fy = kf[anchor / 3];
}

POINT Layout_Resolve(int anchor, int monitor, int offsetX, int offsetY, int width, int height) {
    RECT mr;
    Layout_MonitorRect(monitor, &mr);

    float fx, fy;
    AnchorFactors(anchor, &fx, &fy);

    int freeW = (mr.right - mr.left) - width;
    int freeH = (mr.bottom - mr.top) - height;

    POINT pt;
    pt.x = mr.left + (int)(freeW * fx) + offsetX;
    pt.y = mr.top  + (int)(freeH * fy) + offsetY;
    return pt;
}

POINT Layout_ToOffset(int anchor, int monitor, POINT position, int width, int height) {
    POINT base = Layout_Resolve(anchor, monitor, 0, 0, width, height);
    POINT offset;
    offset.x = position.x - base.x;
    offset.y = position.y - base.y;
    return offset;
}
