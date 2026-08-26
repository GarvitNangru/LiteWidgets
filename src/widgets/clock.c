#include "clock.h"
#include "../widget.h"
#include "../drawing.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    Widget base;
    WidgetStyle style;
    bool is_24h;
    bool show_seconds;
    bool show_date;
    float date_font_size;
    INT   date_font_style;
    ARGB  date_color;
    int   last_minute;
    int   last_second;
} ClockWidgetData;

static void Clock_Render(Widget* base, GpGraphics* gfx, int width, int height) {
    ClockWidgetData* w = (ClockWidgetData*)base;
    const WidgetStyle* s = &w->style;

    /* Background */
    Drawing_WidgetBackground(gfx, s, width, height);

    /* Get current time */
    SYSTEMTIME st;
    GetLocalTime(&st);
    w->last_minute = st.wMinute;
    w->last_second = st.wSecond;

    /* Format time string */
    WCHAR timeBuf[64];
    if (w->is_24h) {
        if (w->show_seconds) {
            swprintf(timeBuf, 64, L"%02d:%02d:%02d", st.wHour, st.wMinute, st.wSecond);
        } else {
            swprintf(timeBuf, 64, L"%02d:%02d", st.wHour, st.wMinute);
        }
    } else {
        int hr = st.wHour % 12;
        if (hr == 0) hr = 12;
        const WCHAR* ampm = (st.wHour >= 12) ? L"PM" : L"AM";
        if (w->show_seconds) {
            swprintf(timeBuf, 64, L"%d:%02d:%02d %s", hr, st.wMinute, st.wSecond, ampm);
        } else {
            swprintf(timeBuf, 64, L"%d:%02d %s", hr, st.wMinute, ampm);
        }
    }

    float pad = s->padding;

    if (w->show_date) {
        /* Format date string */
        WCHAR dateBuf[128];
        /* Use GetDateFormatEx for locale-aware date */
        GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_AUTOLAYOUT,
                        &st, L"dddd, MMMM d", dateBuf, 128, NULL);

        /*
         * Layout: split the padded area into two zones.
         * Time gets ~60% of height (larger font), date gets ~40% (smaller font).
         */
        float innerW = (float)width - pad * 2.0f;
        float innerH = (float)height - pad * 2.0f;
        float timeH  = innerH * 0.60f;
        float dateH  = innerH * 0.40f;

        GpRectF timeRect = { pad, pad, innerW, timeH };
        Drawing_Text(gfx, timeBuf, &timeRect,
                     s->font_family, s->font_size, s->font_style,
                     s->text_color, s->align_h, 2 /* Bottom — push time down toward center */);

        GpRectF dateRect = { pad, pad + timeH, innerW, dateH };
        Drawing_Text(gfx, dateBuf, &dateRect,
                     s->font_family, w->date_font_size, w->date_font_style,
                     w->date_color, s->align_h, 0 /* Top — push date up toward center */);
    } else {
        /* Single line: time centered in full widget area */
        GpRectF layoutRect = { pad, pad, (float)width - pad * 2, (float)height - pad * 2 };
        Drawing_Text(gfx, timeBuf, &layoutRect,
                     s->font_family, s->font_size, s->font_style,
                     s->text_color, s->align_h, s->align_v);
    }
}

static void Clock_OnTimer(Widget* base) {
    ClockWidgetData* w = (ClockWidgetData*)base;
    SYSTEMTIME st;
    GetLocalTime(&st);

    if (w->show_seconds) {
        if (st.wSecond != w->last_second) {
            base->needs_render = true;
        }
    } else {
        if (st.wMinute != w->last_minute) {
            base->needs_render = true;
        }
    }
}

static void Clock_Destroy(Widget* base) {
    free(base);
}

static const WidgetVtable clock_vtable = {
    Clock_Render,
    Clock_OnTimer,
    Clock_Destroy
};

bool ClockWidget_Create(HINSTANCE hInstance, int x, int y, int width, int height,
                        bool click_through, const char* format, bool show_seconds,
                        bool show_date, const WidgetStyle* style,
                        float date_font_size, INT date_font_style, ARGB date_color) {
    ClockWidgetData* w = (ClockWidgetData*)calloc(1, sizeof(ClockWidgetData));
    if (!w) return false;

    w->style = *style;
    w->is_24h = (_stricmp(format, "24h") == 0);
    w->show_seconds = show_seconds;
    w->show_date = show_date;
    w->date_font_size = date_font_size;
    w->date_font_style = date_font_style;
    w->date_color = date_color;
    w->last_minute = -1;
    w->last_second = -1;

    if (!Widget_Init(&w->base, hInstance, &clock_vtable, x, y, width, height, 1000, click_through)) {
        free(w);
        return false;
    }

    /* CRITICAL: Initial render for layered window */
    Widget_Render(&w->base);
    return true;
}
