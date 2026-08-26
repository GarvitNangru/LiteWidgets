#include "widget.h"
#include "desktop.h"

#define IDT_WIDGET_TIMER 1001

static LRESULT CALLBACK WidgetWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    Widget* w = (Widget*)GetWindowLongPtrA(hWnd, GWLP_USERDATA);

    switch (msg) {
        case WM_WINDOWPOSCHANGING: {
            WINDOWPOS* wp = (WINDOWPOS*)lParam;
            if (wp->flags & SWP_HIDEWINDOW) {
                wp->flags &= ~SWP_HIDEWINDOW;
            }
            break;
        }
        case WM_SYSCOMMAND: {
            if ((wParam & 0xFFF0) == SC_MINIMIZE) {
                return 0;
            }
            break;
        }
        case WM_NCCREATE: {
            CREATESTRUCTA* cs = (CREATESTRUCTA*)lParam;
            SetWindowLongPtrA(hWnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
            return DefWindowProcA(hWnd, msg, wParam, lParam);
        }
        case WM_CREATE: {
            w = (Widget*)((CREATESTRUCTA*)lParam)->lpCreateParams;
            if (w && w->timer_interval_ms > 0) {
                SYSTEMTIME st;
                GetLocalTime(&st);
                UINT delay = w->timer_interval_ms;
                if (delay == 1000) {
                    delay = 1000 - st.wMilliseconds;
                    if (delay == 0) delay = 1000;
                }
                SetTimer(hWnd, IDT_WIDGET_TIMER, delay, NULL);
            }
            return 0;
        }
        case WM_TIMER: {
            if (w && wParam == IDT_WIDGET_TIMER) {
                /* Reset to regular interval after first sync tick */
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
        case WM_NCHITTEST: {
            if (w && w->click_through) {
                return HTTRANSPARENT;
            }
            return HTCAPTION;
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

bool Widget_Init(Widget* w, HINSTANCE hInstance, const WidgetVtable* vt,
                 int x, int y, int width, int height,
                 UINT timer_interval_ms, bool click_through) {
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
        DesktopHost_GetParent(),
        NULL, hInstance, w
    );

    if (!w->hwnd) return false;

    SetWindowPos(w->hwnd, HWND_BOTTOM, x, y, width, height, SWP_NOACTIVATE);
    return true;
}

void Widget_Render(Widget* w) {
    if (!w || !w->hwnd || !w->vt->render) return;

    GpBitmap* pBitmap = NULL;
    GdipCreateBitmapFromScan0(w->width, w->height, 0, PixelFormat32bppPARGB, NULL, &pBitmap);
    if (!pBitmap) return;

    GpGraphics* pGraphics = NULL;
    GdipGetImageGraphicsContext((GpImage*)pBitmap, &pGraphics);
    if (!pGraphics) {
        GdipDisposeImage((GpImage*)pBitmap);
        return;
    }

    GdipSetSmoothingMode(pGraphics, SmoothingModeAntiAlias);
    GdipSetTextRenderingHint(pGraphics, TextRenderingHintAntiAliasGridFit);

    w->vt->render(w, pGraphics, w->width, w->height);

    HBITMAP hBmp = NULL;
    GdipCreateHBITMAPFromBitmap(pBitmap, &hBmp, 0);

    if (hBmp) {
        HDC hdcScreen = GetDC(NULL);
        HDC hdcMem = CreateCompatibleDC(hdcScreen);
        HBITMAP hOldBmp = (HBITMAP)SelectObject(hdcMem, hBmp);

        POINT ptDst = { w->x, w->y };
        SIZE sizeDst = { w->width, w->height };
        POINT ptSrc = { 0, 0 };
        BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };

        UpdateLayeredWindow(w->hwnd, hdcScreen, &ptDst, &sizeDst, hdcMem, &ptSrc, 0, &blend, ULW_ALPHA);

        SelectObject(hdcMem, hOldBmp);
        DeleteObject(hBmp);
        DeleteDC(hdcMem);
        ReleaseDC(NULL, hdcScreen);
    }

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
    SetWindowPos(w->hwnd, NULL, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
}

void Widget_Destroy(Widget* w) {
    if (!w) return;
    /* Destroy window FIRST to stop messages, then free widget data */
    HWND hwnd = w->hwnd;
    w->hwnd = NULL;
    if (hwnd) {
        SetWindowLongPtrA(hwnd, GWLP_USERDATA, 0);
        DestroyWindow(hwnd);
    }
    if (w->vt->destroy) {
        w->vt->destroy(w);
    }
}
