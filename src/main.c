#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdbool.h>

#include "desktop.h"
#include "widget.h"
#include "config.h"

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
        pfnSetProcessDpiAwarenessContext setDpi =
            (pfnSetProcessDpiAwarenessContext)GetProcAddress(hUser32, "SetProcessDpiAwarenessContext");
        if (setDpi) {
            setDpi((DPI_AWARENESS_CONTEXT)-4); /* PER_MONITOR_AWARE_V2 */
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
    nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
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
    InsertMenuA(hMenu, -1, MF_BYPOSITION | MF_STRING, ID_TRAY_RELOAD, "Reload Widgets");
    InsertMenuA(hMenu, -1, MF_BYPOSITION | MF_SEPARATOR, 0, NULL);
    InsertMenuA(hMenu, -1, MF_BYPOSITION | MF_STRING, ID_TRAY_EXIT, "Exit");

    SetForegroundWindow(hWnd);
    TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, hWnd, NULL);
    PostMessage(hWnd, WM_NULL, 0, 0);
    DestroyMenu(hMenu);
}

static LRESULT CALLBACK MainWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == g_TaskbarRestartMsg) {
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

/*
 * Resolve INI path: look for config/widgets.ini relative to the executable.
 * Walks up from the exe directory to handle bin/ subfolder builds.
 */
static void ResolveIniPath(char* outPath, DWORD maxLen) {
    char exeDir[MAX_PATH];
    GetModuleFileNameA(NULL, exeDir, MAX_PATH);
    /* Strip filename to get directory */
    char* lastSlash = strrchr(exeDir, '\\');
    if (lastSlash) *lastSlash = '\0';

    /* Try: <exeDir>/config/widgets.ini */
    _snprintf(outPath, maxLen, "%s\\config\\widgets.ini", exeDir);
    if (GetFileAttributesA(outPath) != INVALID_FILE_ATTRIBUTES) return;

    /* Try: <exeDir>/../config/widgets.ini (exe is in bin/) */
    char* parentSlash = strrchr(exeDir, '\\');
    if (parentSlash) {
        *parentSlash = '\0';
        _snprintf(outPath, maxLen, "%s\\config\\widgets.ini", exeDir);
        if (GetFileAttributesA(outPath) != INVALID_FILE_ATTRIBUTES) return;
    }

    /* Fallback: just use the first attempt */
    if (lastSlash) *lastSlash = '\\'; /* Restore */
    _snprintf(outPath, maxLen, "%s\\config\\widgets.ini", exeDir);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance; (void)lpCmdLine; (void)nCmdShow;

    /* 1. DPI */
    InitDpi();

    /* 2. GDI+ */
    GpStartupInput gdiInput = { 1, NULL, FALSE, FALSE };
    GdiplusStartup(&g_gdiplusToken, &gdiInput, NULL);

    /* 3. Main message window + tray icon */
    WNDCLASSA wc = {0};
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "LiteWidgetsMainClass";
    RegisterClassA(&wc);

    g_hMainWnd = CreateWindowExA(0, "LiteWidgetsMainClass", "LiteWidgets", 0,
                                 0, 0, 0, 0, HWND_MESSAGE, NULL, hInstance, NULL);
    AddTrayIcon(g_hMainWnd);

    /* 4. Desktop shell integration */
    DesktopHost_Init();
    g_TaskbarRestartMsg = RegisterWindowMessageA("TaskbarCreated");

    /* 5. Load config and create widgets */
    char iniPath[MAX_PATH];
    ResolveIniPath(iniPath, MAX_PATH);
    Config_Load(iniPath, hInstance);

    /* 6. Message loop */
    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    /* 7. Cleanup */
    GdiplusShutdown(g_gdiplusToken);
    return (int)msg.wParam;
}
