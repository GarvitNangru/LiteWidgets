#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdbool.h>

#include "desktop.h"
#include "widget.h"
#include "config.h"

// For DPI Awareness (Win 10+)
typedef BOOL (WINAPI *pfnSetProcessDpiAwarenessContext)(DPI_AWARENESS_CONTEXT);

#define WM_TRAYICON (WM_USER + 1)
#define ID_TRAY_RELOAD 2001
#define ID_TRAY_EXIT 2002

static ULONG_PTR g_gdiplusToken = 0;
static UINT g_TaskbarRestartMsg = 0;
static HWND g_hMainWnd = NULL;

static void InitDpi(void) {
    HMODULE hUser32 = GetModuleHandleA("user32.dll");
    if (hUser32) {
        pfnSetProcessDpiAwarenessContext setDpi = (pfnSetProcessDpiAwarenessContext)GetProcAddress(hUser32, "SetProcessDpiAwarenessContext");
        if (setDpi) {
            // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
            setDpi((DPI_AWARENESS_CONTEXT)-4);
        }
    }
}

static void AddTrayIcon(HWND hWnd) {
    NOTIFYICONDATAA nid = {0};
    nid.cbSize = sizeof(NOTIFYICONDATAA);
    nid.hWnd = hWnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = LoadIcon(NULL, IDI_APPLICATION); // Default icon
    strcpy_s(nid.szTip, sizeof(nid.szTip), "LiteWidgets");
    Shell_NotifyIconA(NIM_ADD, &nid);
}

static void RemoveTrayIcon(HWND hWnd) {
    NOTIFYICONDATAA nid = {0};
    nid.cbSize = sizeof(NOTIFYICONDATAA);
    nid.hWnd = hWnd;
    nid.uID = 1;
    Shell_NotifyIconA(NIM_DELETE, &nid);
}

static void ShowTrayMenu(HWND hWnd, POINT pt) {
    HMENU hMenu = CreatePopupMenu();
    InsertMenuA(hMenu, -1, MF_BYPOSITION | MF_STRING, ID_TRAY_RELOAD, "Reload Widgets (Currently restarts app entirely)");
    InsertMenuA(hMenu, -1, MF_BYPOSITION | MF_SEPARATOR, 0, NULL);
    InsertMenuA(hMenu, -1, MF_BYPOSITION | MF_STRING, ID_TRAY_EXIT, "Exit");
    
    SetForegroundWindow(hWnd);
    TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, hWnd, NULL);
    PostMessage(hWnd, WM_NULL, 0, 0); // Fix menu dismissal
    DestroyMenu(hMenu);
}

static LRESULT CALLBACK MainWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == g_TaskbarRestartMsg) {
        // Explorer crashed and restarted, reconnect widgets to WorkerW
        DesktopHost_Reattach();
        return 0;
    }

    switch (msg) {
        case WM_TRAYICON:
            if (lParam == WM_RBUTTONUP || lParam == WM_LBUTTONUP) {
                POINT pt;
                GetCursorPos(&pt);
                ShowTrayMenu(hWnd, pt);
            }
            break;
        case WM_COMMAND:
            if (LOWORD(wParam) == ID_TRAY_EXIT) {
                PostQuitMessage(0);
            } else if (LOWORD(wParam) == ID_TRAY_RELOAD) {
                // Since this app has no complex state, the easiest way to fully reload configs is to restart the process.
                char exePath[MAX_PATH];
                GetModuleFileNameA(NULL, exePath, MAX_PATH);
                ShellExecuteA(NULL, "open", exePath, NULL, NULL, SW_SHOWDEFAULT);
                PostQuitMessage(0);
            }
            break;
        case WM_DESTROY:
            RemoveTrayIcon(hWnd);
            PostQuitMessage(0);
            break;
    }
    return DefWindowProcA(hWnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // 1. App Init
    InitDpi();
    
    GpStartupInput gdiInput = { 1, NULL, FALSE, FALSE };
    GdiplusStartup(&g_gdiplusToken, &gdiInput, NULL);

    // 2. Main Window for message pumping & tray icon
    WNDCLASSA wc = {0};
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "LiteWidgetsMainClass";
    RegisterClassA(&wc);

    g_hMainWnd = CreateWindowExA(0, "LiteWidgetsMainClass", "LiteWidgets", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, hInstance, NULL);
    AddTrayIcon(g_hMainWnd);

    // 3. Desktop Shell Integration
    DesktopHost_Init();
    g_TaskbarRestartMsg = RegisterWindowMessageA("TaskbarCreated");

    // 4. Load Config
    char currentDir[MAX_PATH];
    GetCurrentDirectoryA(MAX_PATH, currentDir);
    char iniPath[MAX_PATH];
    snprintf(iniPath, MAX_PATH, "%s\\config\\widgets.ini", currentDir);
    
    Config_Load(iniPath, hInstance);

    // 5. Message Loop
    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    // 6. Cleanup
    // We intentionally leak widget memory here on exit because the OS cleans it up instantly.
    // In a long-running system where widgets are created/destroyed dynamically, we'd loop and destroy them.
    GdiplusShutdown(g_gdiplusToken);
    return (int)msg.wParam;
}
