#pragma once
#ifndef WIDGET_H
#define WIDGET_H

#include <windows.h>
#include <stdbool.h>

#include "gdiplus_helpers.h"
#include "spec.h"

struct Widget;

typedef struct WidgetVtable {
    void (*render)(struct Widget* w, GpGraphics* gfx, int width, int height);
    void (*on_timer)(struct Widget* w);
    /*
     * Optional. Returns the delay until this widget next needs to wake up,
     * letting a clock sleep for a whole minute instead of polling every
     * second. Return 0 to keep the current interval.
     */
    UINT (*next_interval)(struct Widget* w);
    void (*destroy)(struct Widget* w);
    /*
     * Optional. Sees window messages before the default handling, which is
     * how a widget becomes interactive: the notes editor takes keystrokes
     * this way, and both notes and image accept dropped files. Return true
     * to consume the message, optionally setting *result.
     *
     * Implementing this also opts the widget into file drops.
     */
    bool (*on_message)(struct Widget* w, UINT msg, WPARAM wParam, LPARAM lParam,
                       LRESULT* result);
} WidgetVtable;

typedef struct Widget {
    HWND                hwnd;
    const WidgetVtable* vt;

    int   x, y;                  /* absolute desktop position */
    int   width, height;
    int   anchor;
    int   monitor;
    int   z_order;               /* ZOrder */
    float radius;                /* corner radius, for the edit-mode overlay */

    UINT  timer_interval_ms;     /* 0 = no timer */
    bool  click_through;
    bool  click_through_saved;
    BYTE  opacity;               /* 0..255, applied by the layered blend */
    bool  needs_render;

    char  section[LW_SECTION_LEN];
    void* data;
} Widget;

/*
 * Create the window and register the widget. Geometry comes from `spec`;
 * `timerIntervalMs` of 0 means the widget never wakes up on its own.
 */
bool Widget_Init(Widget* w, HINSTANCE hInstance, const WidgetVtable* vt,
                 const WidgetSpec* spec, UINT timerIntervalMs);

void Widget_Render(Widget* w);
void Widget_SetClickThrough(Widget* w, bool enable);
void Widget_Destroy(Widget* w);

/*
 * Let the widget take the keyboard.
 *
 * Widgets are WS_EX_NOACTIVATE so that clicking one never steals focus from
 * whatever the user was doing. A widget that wants to be typed into has to
 * drop that for as long as it is being edited, and gets raised while it is,
 * so it is not being edited underneath another window.
 */
void Widget_SetFocusable(Widget* w, bool enable);

/* Put one widget back on the layer its z_order asks for. */
void Widget_Restack(Widget* w);

/* Timer ids a widget may use for itself; the engine owns everything else. */
#define IDT_WIDGET_USER0 1010
#define IDT_WIDGET_USER1 1011

/*
 * Sent to every widget when arranging starts or stops. A widget that is in
 * the middle of an interaction should commit whatever it has and stop.
 */
#define WM_LW_CANCEL_EDIT (WM_USER + 10)

/* Tear every live widget down — used by reload, so the process keeps running. */
void Widget_DestroyAll(void);
int  Widget_Count(void);

/* Force a repaint of everything, e.g. after a display change. */
void Widget_RenderAll(void);

/*
 * Put every widget back where it belongs in the z-order.
 *
 * Only widgets that have actually fallen behind the wallpaper are touched,
 * so this is safe to call on every shell activation: showing the desktop
 * reshuffles the desktop band, and without this a widget stays buried behind
 * a live wallpaper until the app is restarted.
 */
void Widget_ReassertZOrder(void);

/*
 * Edit mode: widgets become draggable, gain a visible outline and float above
 * other windows. Leaving edit mode restores click-through and z-order.
 */
void Widget_SetEditMode(bool enable);
bool Widget_EditMode(void);
void Widget_SetSnapGrid(int pixels);
int  Widget_SnapGrid(void);

/* Write the current positions back to the INI as anchor-relative offsets. */
void Widget_SavePositions(const char* iniPath);

#endif /* WIDGET_H */
