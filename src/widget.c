#include "widget.h"
#include "desktop.h"

#define IDT_WIDGET_TIMER 1001

static LRESULT CALLBACK WidgetWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    Widget* w = (Widget*)GetWindowLongPtrA(hWnd, GWLP_USERDATA);

    switch (msg) {
        case WM_NCCREATE: {
            CREATESTRUCTA* cs = (CREATESTRUCTA*)lParam;
            SetWindowLongPtrA(hWnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
            return DefWindowProcA(hWnd, msg, wParam, lParam);
        }
        case WM_CREATE: {
            w = (Widget*)((CREATESTRUCTA*)lParam)->lpCreateParams;
            if (w && w->timer_interval_ms > 0) {
                // Sync clock timers to second boundary
                SYSTEMTIME st;
                GetLocalTime(&st);
                UINT delay = w->timer_interval_ms;
                if (delay == 1000) {
                    delay = 1000 - st.wMilliseconds;
                }
                SetTimer(hWnd, IDT_WIDGET_TIMER, delay, NULL);
            }
            return 0;
        }
        case WM_TIMER: {
            if (w && wParam == IDT_WIDGET_TIMER) {
                // If it was the first synchronized tick, reset to regular interval
                if (w->timer_interval_ms == 1000) {
                    KillTimer(hWnd, IDT_WIDGET_TIMER);
                    SetTimer(hWnd, IDT_WIDGET_TIMER, w->timer_interval_ms, NULL);
                }
                
                if (w->vt->on_timer) {
                    w->vt->on_timer(w);
                }
                if (w->needs_render) {
                    Widget_Render(w);
                    w->needs_render = false;
                }
            }
            return 0;
        }
        case WM_WINDOWPOSCHANGING: {
            // Prevent Win+D from hiding/minimizing the widget
            WINDOWPOS* wp = (WINDOWPOS*)lParam;
            if (wp->flags & SWP_HIDEWINDOW) {
                wp->flags &= ~SWP_HIDEWINDOW;
            }
            // Always keep at bottom
            wp->hwndInsertAfter = HWND_BOTTOM;
            return 0;
        }
        case WM_NCHITTEST: {
            if (w && w->click_through) {
                return HTTRANSPARENT;
            }
            // Allow dragging the widget if not click-through
            return HTCAPTION;
        }
        case WM_DPICHANGED: {
            // Future enhancement: rescale fonts and sizes
            return 0;
        }
        case WM_DESTROY: {
            if (w && w->timer_interval_ms > 0) {
                KillTimer(hWnd, IDT_WIDGET_TIMER);
            }
            return 0;
        }
    }
    return DefWindowProcA(hWnd, msg, wParam, lParam);
}

static bool RegisterWidgetClass(HINSTANCE hInstance) {
    static bool registered = false;
    if (registered) return true;

    WNDCLASSA wc = {0};
    wc.lpfnWndProc = WidgetWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "LiteWidgetClass";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);

    if (RegisterClassA(&wc)) {
        registered = true;
        return true;
    }
    return false;
}

bool Widget_Init(Widget* w, HINSTANCE hInstance, const WidgetVtable* vt, int x, int y, int width, int height, UINT timer_interval_ms, bool click_through) {
    if (!w || !vt) return false;

    if (!RegisterWidgetClass(hInstance)) return false;

    w->vt = vt;
    w->x = x;
    w->y = y;
    w->width = width;
    w->height = height;
    w->timer_interval_ms = timer_interval_ms;
    w->click_through = click_through;
    w->needs_render = true;
    w->data = NULL;

    DWORD exStyle = WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
    if (click_through) {
        exStyle |= WS_EX_TRANSPARENT;
    }

    w->hwnd = CreateWindowExA(
        exStyle,
        "LiteWidgetClass",
        "LiteWidget",
        WS_POPUP | WS_VISIBLE,
        x, y, width, height,
        NULL, NULL, hInstance, w
    );

    if (!w->hwnd) return false;

    // Attach to WorkerW
    HWND hDesktop = DesktopHost_GetParent();
    if (hDesktop) {
        // Adjust coordinates relative to the virtual screen top-left (for multi-monitor)
        int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
        int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
        
        SetParent(w->hwnd, hDesktop);
        SetWindowPos(w->hwnd, HWND_TOP, x - vx, y - vy, width, height, SWP_NOACTIVATE);
    } else {
        SetWindowPos(w->hwnd, HWND_BOTTOM, x, y, width, height, SWP_NOACTIVATE);
    }

    return true;
}

void Widget_Render(Widget* w) {
    if (!w || !w->hwnd || !w->vt->render) return;

    // PixelFormat32bppPARGB is 0x000E200B
    GpBitmap* pBitmap = NULL;
    GdipCreateBitmapFromScan0(w->width, w->height, 0, 0x000E200B, NULL, &pBitmap);
    GpGraphics* pGraphics = NULL;
    GdipGetImageGraphicsContext((GpImage*)pBitmap, &pGraphics);
    
    GdipSetSmoothingMode(pGraphics, SmoothingModeAntiAlias);

    // Call actual widget render
    w->vt->render(w, pGraphics, w->width, w->height);

    HBITMAP hBmp = NULL;
    GdipCreateHBITMAPFromBitmap(pBitmap, &hBmp, 0);

    HDC hdcScreen = GetDC(NULL);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    HBITMAP hOldBmp = (HBITMAP)SelectObject(hdcMem, hBmp);

    POINT ptSrc = { 0, 0 };
    SIZE sizeDst = { w->width, w->height };
    BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };

    UpdateLayeredWindow(w->hwnd, hdcScreen, NULL, &sizeDst, hdcMem, &ptSrc, 0, &blend, ULW_ALPHA);

    SelectObject(hdcMem, hOldBmp);
    DeleteObject(hBmp);
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdcScreen);

    GdipDeleteGraphics(pGraphics);
    GdipDisposeImage((GpImage*)pBitmap);
}

void Widget_SetClickThrough(Widget* w, bool enable) {
    if (!w || !w->hwnd) return;
    w->click_through = enable;
    LONG_PTR exStyle = GetWindowLongPtrA(w->hwnd, GWL_EXSTYLE);
    if (enable) {
        exStyle |= WS_EX_TRANSPARENT;
    } else {
        exStyle &= ~WS_EX_TRANSPARENT;
    }
    SetWindowLongPtrA(w->hwnd, GWL_EXSTYLE, exStyle);
}

void Widget_Destroy(Widget* w) {
    if (w) {
        if (w->vt->destroy) {
            w->vt->destroy(w);
        }
        if (w->hwnd) {
            DestroyWindow(w->hwnd);
            w->hwnd = NULL;
        }
    }
}
