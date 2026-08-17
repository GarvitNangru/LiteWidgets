#include "clock.h"
#include "../widget.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    Widget base;
    bool is_24h;
    int font_size;
    DWORD bg_color;
    DWORD text_color;
    int last_minute; // Used to avoid re-rendering if minute hasn't changed
} ClockWidgetData;

static void Clock_Render(Widget* base, GpGraphics* gfx, int width, int height) {
    ClockWidgetData* w = (ClockWidgetData*)base;
    
    // Draw background
    GpSolidFill* pBrush = NULL;
    GdipCreateSolidFill(w->bg_color, &pBrush);
    // Draw rounded rect (simplified to normal rect here, can be enhanced with GpPath)
    GdipFillRectangleI(gfx, (GpBrush*)pBrush, 0, 0, width, height);
    GdipDeleteBrush((GpBrush*)pBrush);

    // Get time
    SYSTEMTIME st;
    GetLocalTime(&st);
    w->last_minute = st.wMinute;

    WCHAR timeBuf[32];
    if (w->is_24h) {
        swprintf(timeBuf, 32, L"%02d:%02d", st.wHour, st.wMinute);
    } else {
        int hr = st.wHour % 12;
        if (hr == 0) hr = 12;
        swprintf(timeBuf, 32, L"%d:%02d %s", hr, st.wMinute, (st.wHour >= 12) ? L"PM" : L"AM");
    }

    // Setup font
    GpFontFamily* pFontFamily = NULL;
    GdipCreateFontFamilyFromName(L"Segoe UI", NULL, &pFontFamily);
    GpFont* pFont = NULL;
    GdipCreateFont(pFontFamily, (REAL)w->font_size, FontStyleBold, UnitPixel, &pFont);
    
    GpSolidFill* pTextBrush = NULL;
    GdipCreateSolidFill(w->text_color, &pTextBrush);
    
    GpRectF layoutRect = { 0.0f, 0.0f, (float)width, (float)height };
    // Very basic centering could be applied with StringFormat
    GdipDrawString(gfx, timeBuf, -1, pFont, &layoutRect, NULL, (GpBrush*)pTextBrush);

    GdipDeleteBrush((GpBrush*)pTextBrush);
    GdipDeleteFont(pFont);
    GdipDeleteFontFamily(pFontFamily);
}

static void Clock_OnTimer(Widget* base) {
    ClockWidgetData* w = (ClockWidgetData*)base;
    SYSTEMTIME st;
    GetLocalTime(&st);
    if (st.wMinute != w->last_minute) {
        base->needs_render = true; // Only re-render when minute changes
    }
}

static void Clock_Destroy(Widget* base) {
    ClockWidgetData* w = (ClockWidgetData*)base;
    free(w);
}

static const WidgetVtable clock_vtable = {
    Clock_Render,
    Clock_OnTimer,
    Clock_Destroy
};

bool ClockWidget_Create(HINSTANCE hInstance, int x, int y, int width, int height, bool click_through, const char* format, int font_size, DWORD bg_color, DWORD text_color) {
    ClockWidgetData* w = (ClockWidgetData*)malloc(sizeof(ClockWidgetData));
    if (!w) return false;
    memset(w, 0, sizeof(ClockWidgetData));

    w->is_24h = (_stricmp(format, "24h") == 0);
    w->font_size = font_size;
    w->bg_color = bg_color;
    w->text_color = text_color;
    w->last_minute = -1;

    // Timer is 1000ms so it accurately hits the second boundary and we don't skip minutes.
    if (!Widget_Init(&w->base, hInstance, &clock_vtable, x, y, width, height, 1000, click_through)) {
        free(w);
        return false;
    }
    return true;
}
