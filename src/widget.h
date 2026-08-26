#pragma once
#ifndef WIDGET_H
#define WIDGET_H

#include <windows.h>
#include <stdbool.h>
#include "gdiplus_helpers.h"

struct Widget;

typedef struct WidgetVtable {
    void (*render)(struct Widget* w, GpGraphics* gfx, int width, int height);
    void (*on_timer)(struct Widget* w);
    void (*destroy)(struct Widget* w);
} WidgetVtable;

typedef struct Widget {
    HWND hwnd;
    const WidgetVtable* vt;
    int x, y, width, height;
    UINT timer_interval_ms; /* 0 = no timer (static) */
    bool click_through;
    void* data; /* Widget specific data */
    bool needs_render;
} Widget;

bool Widget_Init(Widget* w, HINSTANCE hInstance, const WidgetVtable* vt,
                 int x, int y, int width, int height,
                 UINT timer_interval_ms, bool click_through);
void Widget_Render(Widget* w);
void Widget_SetClickThrough(Widget* w, bool enable);
void Widget_Destroy(Widget* w);

#endif /* WIDGET_H */
