#include "widget.h"

#include "desktop.h"
#include "drawing.h"
#include "layout.h"

#include <stdio.h>
#include <string.h>

#define IDT_WIDGET_TIMER 1001
#define EDIT_ACCENT      0xCC4CC2FF

/* ─────────────────────────── registry ─────────────────────────── */

static Widget* g_tracked[LW_MAX_WIDGETS];
static int     g_trackedCount = 0;
static bool    g_editMode = false;
static int     g_snapGrid = 8;

static void Track(Widget* w) {
    if (g_trackedCount < LW_MAX_WIDGETS) g_tracked[g_trackedCount++] = w;
}

static void Untrack(Widget* w) {
    for (int i = 0; i < g_trackedCount; i++) {
        if (g_tracked[i] != w) continue;
        for (int j = i; j < g_trackedCount - 1; j++) g_tracked[j] = g_tracked[j + 1];
        g_trackedCount--;
        return;
    }
}

int  Widget_Count(void)    { return g_trackedCount; }
bool Widget_EditMode(void) { return g_editMode; }
int  Widget_SnapGrid(void) { return g_snapGrid; }

void Widget_SetSnapGrid(int pixels) {
    g_snapGrid = (pixels < 1) ? 1 : pixels;
}

/* ─────────────────────────── z-order ─────────────────────────── */

#define ZFLAGS (SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE)

/*
 * Park a widget where its z_order asks for.
 *
 * The interesting case is ZORDER_DESKTOP. HWND_BOTTOM is not good enough:
 * a live wallpaper renders into its own window, which does not necessarily
 * stay at the bottom, so a widget pinned to the very bottom disappears
 * behind the wallpaper. Sitting immediately above whatever is painting the
 * desktop keeps the widget visible while still leaving it behind every
 * normal application window.
 */
static void PlaceWidget(Widget* w) {
    if (!w || !w->hwnd) return;

    if (w->z_order == ZORDER_TOP) {
        SetWindowPos(w->hwnd, HWND_TOPMOST, 0, 0, 0, 0, ZFLAGS);
        return;
    }
    if (w->z_order == ZORDER_BOTTOM) {
        SetWindowPos(w->hwnd, HWND_BOTTOM, 0, 0, 0, 0, ZFLAGS);
        return;
    }

    HWND wallpaper = DesktopHost_FindWallpaper();
    if (!wallpaper) {
        SetWindowPos(w->hwnd, HWND_BOTTOM, 0, 0, 0, 0, ZFLAGS);
        return;
    }

    /* Insert after the wallpaper's neighbour, which lands us just above it. */
    HWND above = GetWindow(wallpaper, GW_HWNDPREV);
    if (above == w->hwnd) return;               /* already in place */
    SetWindowPos(w->hwnd, above ? above : HWND_TOP, 0, 0, 0, 0, ZFLAGS);
}

void Widget_ReassertZOrder(void) {
    if (g_editMode) return;   /* edit mode deliberately floats the widgets */

    HWND wallpaper = DesktopHost_FindWallpaper();
    if (!wallpaper) return;

    for (int i = 0; i < g_trackedCount; i++) {
        Widget* w = g_tracked[i];
        if (!w || !w->hwnd || w->z_order != ZORDER_DESKTOP) continue;
        /* Only rescue the ones that actually fell behind. */
        if (DesktopHost_IsBehind(w->hwnd, wallpaper)) PlaceWidget(w);
    }
}

/* ─────────────────────────── timers ─────────────────────────── */

static void ArmTimer(Widget* w) {
    if (!w->hwnd || w->timer_interval_ms == 0) return;

    UINT delay = w->timer_interval_ms;
    if (w->vt->next_interval) {
        UINT suggested = w->vt->next_interval(w);
        if (suggested > 0) delay = suggested;
    }
    if (delay < 10) delay = 10;
    SetTimer(w->hwnd, IDT_WIDGET_TIMER, delay, NULL);
}

/* ─────────────────────────── window procedure ─────────────────────────── */

static int SnapTo(int value, int grid) {
    if (grid <= 1) return value;
    int half = grid / 2;
    return ((value + (value >= 0 ? half : -half)) / grid) * grid;
}

static LRESULT CALLBACK WidgetWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    Widget* w = (Widget*)GetWindowLongPtrA(hWnd, GWLP_USERDATA);

    switch (msg) {
        case WM_NCCREATE: {
            CREATESTRUCTA* cs = (CREATESTRUCTA*)lParam;
            SetWindowLongPtrA(hWnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
            return DefWindowProcA(hWnd, msg, wParam, lParam);
        }

        case WM_CREATE:
            /* The timer is armed from Widget_Init, once the handle is stored. */
            return 0;

        /* Win+D and "show desktop" try to hide us; refuse politely. */
        case WM_WINDOWPOSCHANGING: {
            WINDOWPOS* wp = (WINDOWPOS*)lParam;
            if (wp->flags & SWP_HIDEWINDOW) wp->flags &= ~SWP_HIDEWINDOW;
            break;
        }

        case WM_WINDOWPOSCHANGED:
            if (w) {
                RECT rc;
                GetWindowRect(hWnd, &rc);
                w->x = rc.left;
                w->y = rc.top;
            }
            break;

        /* Snap to the grid while a widget is being dragged in edit mode. */
        case WM_MOVING: {
            if (w && g_editMode && g_snapGrid > 1) {
                RECT* rc = (RECT*)lParam;
                int width  = rc->right - rc->left;
                int height = rc->bottom - rc->top;
                rc->left = SnapTo(rc->left, g_snapGrid);
                rc->top  = SnapTo(rc->top,  g_snapGrid);
                rc->right  = rc->left + width;
                rc->bottom = rc->top  + height;
                return TRUE;
            }
            break;
        }

        case WM_EXITSIZEMOVE:
            if (w) Widget_Render(w);
            return 0;

        case WM_SYSCOMMAND:
            if ((wParam & 0xFFF0) == SC_MINIMIZE) return 0;
            break;

        case WM_TIMER:
            if (w && wParam == IDT_WIDGET_TIMER) {
                if (w->vt->on_timer) w->vt->on_timer(w);
                if (w->needs_render) {
                    Widget_Render(w);
                    w->needs_render = false;
                }
                /* Re-arm every tick: the next boundary is rarely a fixed period. */
                if (w->vt->next_interval) {
                    KillTimer(hWnd, IDT_WIDGET_TIMER);
                    ArmTimer(w);
                }
            }
            return 0;

        case WM_NCHITTEST:
            if (w && w->click_through) return HTTRANSPARENT;
            return HTCAPTION;   /* the whole surface drags */

        case WM_DESTROY:
            if (w && w->timer_interval_ms > 0) KillTimer(hWnd, IDT_WIDGET_TIMER);
            return 0;
    }
    return DefWindowProcA(hWnd, msg, wParam, lParam);
}

static bool RegisterWidgetClass(HINSTANCE hInstance) {
    static bool registered = false;
    if (registered) return true;

    WNDCLASSA wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc   = WidgetWndProc;
    wc.hInstance     = hInstance;
    wc.lpszClassName = "LiteWidgetClass";
    wc.hCursor       = LoadCursor(NULL, IDC_SIZEALL);

    registered = RegisterClassA(&wc) != 0;
    return registered;
}

/* ─────────────────────────── lifecycle ─────────────────────────── */

bool Widget_Init(Widget* w, HINSTANCE hInstance, const WidgetVtable* vt,
                 const WidgetSpec* spec, UINT timerIntervalMs) {
    if (!w || !vt || !spec) return false;
    if (!RegisterWidgetClass(hInstance)) return false;

    POINT pos = Layout_Resolve(spec->anchor, spec->monitor, spec->x, spec->y,
                               spec->width, spec->height);

    w->vt            = vt;
    w->x             = pos.x;
    w->y             = pos.y;
    w->width         = spec->width;
    w->height        = spec->height;
    w->anchor        = spec->anchor;
    w->monitor       = spec->monitor;
    w->z_order       = spec->z_order;
    w->radius        = spec->style.corner_radius;
    w->click_through = spec->click_through;
    w->click_through_saved = spec->click_through;
    w->needs_render  = true;
    w->data          = NULL;
    w->timer_interval_ms = timerIntervalMs;
    w->opacity       = (BYTE)(spec->opacity * 255.0f + 0.5f);
    strncpy(w->section, spec->section, LW_SECTION_LEN - 1);
    w->section[LW_SECTION_LEN - 1] = '\0';

    DWORD exStyle = WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
    if (spec->click_through) exStyle |= WS_EX_TRANSPARENT;

    /*
     * Owned by the desktop, so "show desktop" carries the widget up with the
     * desktop band instead of stranding it behind the wallpaper. A widget
     * asking to float above everything gets no owner, since ownership would
     * pin it to the desktop's band.
     */
    HWND owner = (spec->z_order == ZORDER_TOP) ? NULL : DesktopHost_GetOwner();

    w->hwnd = CreateWindowExA(exStyle, "LiteWidgetClass", "LiteWidget",
                              WS_POPUP | WS_VISIBLE,
                              pos.x, pos.y, spec->width, spec->height,
                              owner, NULL, hInstance, w);
    if (!w->hwnd) return false;

    PlaceWidget(w);
    ArmTimer(w);
    Track(w);
    return true;
}

/* Accent outline so widgets are findable while they are being arranged. */
static void DrawEditOverlay(GpGraphics* gfx, const Widget* w) {
    float radius = w->radius > 1.0f ? w->radius : 6.0f;
    Drawing_Panel(gfx, 0x14FFFFFF, 0, GRAD_NONE, EDIT_ACCENT, 2.0f,
                  1.0f, 1.0f, (float)w->width - 2.0f, (float)w->height - 2.0f, radius);
}

void Widget_Render(Widget* w) {
    if (!w || !w->hwnd || !w->vt->render) return;

    GpBitmap* bitmap = NULL;
    if (GdipCreateBitmapFromScan0(w->width, w->height, 0, PixelFormat32bppPARGB, NULL, &bitmap) != 0
        || !bitmap) return;

    GpGraphics* gfx = NULL;
    if (GdipGetImageGraphicsContext((GpImage*)bitmap, &gfx) != 0 || !gfx) {
        GdipDisposeImage((GpImage*)bitmap);
        return;
    }

    GdipSetSmoothingMode(gfx, SmoothingModeAntiAlias);
    GdipSetTextRenderingHint(gfx, TextRenderingHintAntiAliasGridFit);
    GdipSetInterpolationMode(gfx, InterpolationModeHighQualityBicubic);
    GdipSetPixelOffsetMode(gfx, PixelOffsetModeHalf);

    w->vt->render(w, gfx, w->width, w->height);
    if (g_editMode) DrawEditOverlay(gfx, w);

    HBITMAP hBmp = NULL;
    GdipCreateHBITMAPFromBitmap(bitmap, &hBmp, 0);

    if (hBmp) {
        HDC hdcScreen = GetDC(NULL);
        HDC hdcMem = CreateCompatibleDC(hdcScreen);
        HBITMAP hOld = (HBITMAP)SelectObject(hdcMem, hBmp);

        POINT ptDst = { w->x, w->y };
        SIZE  size  = { w->width, w->height };
        POINT ptSrc = { 0, 0 };
        BLENDFUNCTION blend;
        blend.BlendOp = AC_SRC_OVER;
        blend.BlendFlags = 0;
        blend.SourceConstantAlpha = w->opacity;   /* master opacity, for free */
        blend.AlphaFormat = AC_SRC_ALPHA;

        UpdateLayeredWindow(w->hwnd, hdcScreen, &ptDst, &size, hdcMem, &ptSrc,
                            0, &blend, ULW_ALPHA);

        SelectObject(hdcMem, hOld);
        DeleteObject(hBmp);
        DeleteDC(hdcMem);
        ReleaseDC(NULL, hdcScreen);
    }

    GdipDeleteGraphics(gfx);
    GdipDisposeImage((GpImage*)bitmap);
}

void Widget_RenderAll(void) {
    for (int i = 0; i < g_trackedCount; i++)
        if (g_tracked[i]) Widget_Render(g_tracked[i]);
}

void Widget_SetClickThrough(Widget* w, bool enable) {
    if (!w || !w->hwnd) return;
    w->click_through = enable;

    LONG_PTR exStyle = GetWindowLongPtrA(w->hwnd, GWL_EXSTYLE);
    if (enable) exStyle |=  WS_EX_TRANSPARENT;
    else        exStyle &= ~WS_EX_TRANSPARENT;
    SetWindowLongPtrA(w->hwnd, GWL_EXSTYLE, exStyle);

    SetWindowPos(w->hwnd, NULL, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED | SWP_NOACTIVATE);
}

void Widget_Destroy(Widget* w) {
    if (!w) return;
    Untrack(w);

    HWND hwnd = w->hwnd;
    w->hwnd = NULL;
    if (hwnd) {
        SetWindowLongPtrA(hwnd, GWLP_USERDATA, 0);
        DestroyWindow(hwnd);
    }
    if (w->vt->destroy) w->vt->destroy(w);   /* frees the allocation */
}

void Widget_DestroyAll(void) {
    while (g_trackedCount > 0)
        Widget_Destroy(g_tracked[g_trackedCount - 1]);
}

/* ─────────────────────────── edit mode ─────────────────────────── */

void Widget_SetEditMode(bool enable) {
    if (g_editMode == enable) return;
    g_editMode = enable;

    for (int i = 0; i < g_trackedCount; i++) {
        Widget* w = g_tracked[i];
        if (!w || !w->hwnd) continue;

        if (enable) {
            w->click_through_saved = w->click_through;
            Widget_SetClickThrough(w, false);
            SetWindowPos(w->hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        } else {
            Widget_SetClickThrough(w, w->click_through_saved);
            SetWindowPos(w->hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, ZFLAGS);
            PlaceWidget(w);
        }
        Widget_Render(w);
    }
}

void Widget_SavePositions(const char* iniPath) {
    if (!iniPath) return;

    for (int i = 0; i < g_trackedCount; i++) {
        Widget* w = g_tracked[i];
        if (!w || !w->hwnd || !w->section[0]) continue;

        RECT rc;
        GetWindowRect(w->hwnd, &rc);
        w->x = rc.left;
        w->y = rc.top;

        /*
         * A widget pinned to an explicit monitor should follow the user when
         * they drag it onto a different one; -1 stays "wherever primary is".
         */
        int monitor = w->monitor;
        if (monitor >= 0) {
            POINT centre = { w->x + w->width / 2, w->y + w->height / 2 };
            monitor = Layout_MonitorFromPoint(centre);
            w->monitor = monitor;
        }

        POINT position = { w->x, w->y };
        POINT offset = Layout_ToOffset(w->anchor, monitor, position, w->width, w->height);

        char buf[32];
        _snprintf(buf, sizeof(buf), "%d", offset.x);
        WritePrivateProfileStringA(w->section, "x", buf, iniPath);
        _snprintf(buf, sizeof(buf), "%d", offset.y);
        WritePrivateProfileStringA(w->section, "y", buf, iniPath);
        if (w->monitor >= 0) {
            _snprintf(buf, sizeof(buf), "%d", w->monitor);
            WritePrivateProfileStringA(w->section, "monitor", buf, iniPath);
        }
    }
}
