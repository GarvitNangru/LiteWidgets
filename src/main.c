#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "autostart.h"
#include "config.h"
#include "desktop.h"
#include "settings.h"
#include "timefmt.h"
#include "widget.h"

typedef BOOL (WINAPI *SetProcessDpiAwarenessContextFn)(DPI_AWARENESS_CONTEXT);

#define WM_TRAYICON        (WM_USER + 1)
#define ID_TRAY_SETTINGS   2000
#define ID_TRAY_EDIT       2001
#define ID_TRAY_RELOAD     2002
#define ID_TRAY_FOLDER     2003
#define ID_TRAY_AUTOSTART  2004
#define ID_TRAY_EXIT       2005

#define APP_MUTEX   "LiteWidgets.SingleInstance"
#define APP_WAKEUP  "LiteWidgets.Reload"

static char      g_iniPath[MAX_PATH];
static ULONG_PTR g_gdiplusToken = 0;
static UINT      g_taskbarCreatedMsg = 0;
static UINT      g_reloadMsg = 0;
static HWND      g_mainWnd = NULL;
static HICON     g_trayIcon = NULL;

/* ─────────────────────────── startup plumbing ─────────────────────────── */

static void InitDpi(void) {
    HMODULE user32 = GetModuleHandleA("user32.dll");
    if (!user32) return;

    SetProcessDpiAwarenessContextFn setContext =
        (SetProcessDpiAwarenessContextFn)GetProcAddress(user32, "SetProcessDpiAwarenessContext");
    if (setContext) setContext((DPI_AWARENESS_CONTEXT)-4);   /* PER_MONITOR_AWARE_V2 */
}

/*
 * Resolve the config next to the executable, then one level up so a build in
 * bin/ still finds the repo's config folder. Overridable with --config.
 */
static void ResolveIniPath(char* out, DWORD cap) {
    char dir[MAX_PATH];
    GetModuleFileNameA(NULL, dir, MAX_PATH);

    char* slash = strrchr(dir, '\\');
    if (slash) *slash = '\0';

    _snprintf(out, cap, "%s\\config\\widgets.ini", dir);
    if (GetFileAttributesA(out) != INVALID_FILE_ATTRIBUTES) return;

    char parent[MAX_PATH];
    strncpy(parent, dir, MAX_PATH - 1);
    parent[MAX_PATH - 1] = '\0';
    slash = strrchr(parent, '\\');
    if (slash) {
        *slash = '\0';
        char candidate[MAX_PATH];
        _snprintf(candidate, MAX_PATH, "%s\\config\\widgets.ini", parent);
        if (GetFileAttributesA(candidate) != INVALID_FILE_ATTRIBUTES) {
            strncpy(out, candidate, cap - 1);
            out[cap - 1] = '\0';
            return;
        }
    }
    /* Nothing on disk yet — keep the exe-relative path so we can create it. */
}

/*
 *   --config <path>   use a specific INI instead of the default lookup
 *   --settings        open the editor as soon as the app starts
 */
static void ParseCommandLine(char* iniPath, DWORD cap, bool* openSettings) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return;

    for (int i = 1; i < argc; i++) {
        bool wantsConfig = lstrcmpiW(argv[i], L"--config") == 0 || lstrcmpiW(argv[i], L"-c") == 0;
        if (wantsConfig && i + 1 < argc) {
            WideCharToMultiByte(CP_UTF8, 0, argv[++i], -1, iniPath, (int)cap, NULL, NULL);
        } else if (lstrcmpiW(argv[i], L"--settings") == 0 || lstrcmpiW(argv[i], L"-s") == 0) {
            *openSettings = true;
        }
    }
    LocalFree(argv);
}

/*
 * Build the tray icon at runtime instead of shipping a binary .ico: a rounded
 * badge with two clock hands, drawn at the system's small-icon size.
 */
static HICON CreateTrayIcon(void) {
    int size = GetSystemMetrics(SM_CXSMICON);
    if (size < 16) size = 16;

    GpBitmap* bitmap = NULL;
    if (GdipCreateBitmapFromScan0(size, size, 0, PixelFormat32bppPARGB, NULL, &bitmap) != 0)
        return NULL;

    GpGraphics* gfx = NULL;
    if (GdipGetImageGraphicsContext((GpImage*)bitmap, &gfx) == 0 && gfx) {
        GdipSetSmoothingMode(gfx, SmoothingModeAntiAlias);

        float s = (float)size;
        GpSolidFill* brush = NULL;
        if (GdipCreateSolidFill(0xFF4CC2FF, &brush) == 0) {
            GdipFillEllipse(gfx, (GpBrush*)brush, 0.5f, 0.5f, s - 1.0f, s - 1.0f);
            GdipDeleteBrush((GpBrush*)brush);
        }

        GpPen* pen = NULL;
        if (GdipCreatePen1(0xFF0A1A24, (s > 20.0f) ? 2.0f : 1.5f, UnitPixel, &pen) == 0) {
            GdipSetPenStartCap(pen, LineCapRound);
            GdipSetPenEndCap(pen, LineCapRound);
            float c = s * 0.5f;
            GdipDrawLine(gfx, pen, c, c, c, c - s * 0.28f);          /* minute hand */
            GdipDrawLine(gfx, pen, c, c, c + s * 0.20f, c);          /* hour hand */
            GdipDeletePen(pen);
        }
        GdipDeleteGraphics(gfx);
    }

    HICON icon = NULL;
    HBITMAP color = NULL;
    if (GdipCreateHBITMAPFromBitmap(bitmap, &color, 0) == 0 && color) {
        HBITMAP mask = CreateBitmap(size, size, 1, 1, NULL);
        ICONINFO info;
        info.fIcon = TRUE;
        info.xHotspot = 0;
        info.yHotspot = 0;
        info.hbmMask = mask;
        info.hbmColor = color;
        icon = CreateIconIndirect(&info);
        if (mask) DeleteObject(mask);
        DeleteObject(color);
    }
    GdipDisposeImage((GpImage*)bitmap);

    return icon ? icon : LoadIcon(NULL, IDI_APPLICATION);
}

/* ─────────────────────────── tray ─────────────────────────── */

static void AddTrayIcon(HWND hWnd) {
    NOTIFYICONDATAA nid;
    memset(&nid, 0, sizeof(nid));
    nid.cbSize = sizeof(nid);
    nid.hWnd = hWnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = g_trayIcon;
    strncpy(nid.szTip, "LiteWidgets", sizeof(nid.szTip) - 1);
    Shell_NotifyIconA(NIM_ADD, &nid);
}

static void RemoveTrayIcon(HWND hWnd) {
    NOTIFYICONDATAA nid;
    memset(&nid, 0, sizeof(nid));
    nid.cbSize = sizeof(nid);
    nid.hWnd = hWnd;
    nid.uID = 1;
    Shell_NotifyIconA(NIM_DELETE, &nid);
}

static void ShowTrayMenu(HWND hWnd) {
    POINT pt;
    GetCursorPos(&pt);

    HMENU menu = CreatePopupMenu();
    AppendMenuA(menu, MF_STRING, ID_TRAY_SETTINGS, "&Settings\tDouble-click");
    AppendMenuA(menu, MF_STRING | (Widget_EditMode() ? MF_CHECKED : 0), ID_TRAY_EDIT,
                Widget_EditMode() ? "Finish arranging (saves positions)" : "&Arrange widgets");
    AppendMenuA(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(menu, MF_STRING, ID_TRAY_RELOAD, "&Reload config");
    AppendMenuA(menu, MF_STRING, ID_TRAY_FOLDER, "Open config &folder");
    AppendMenuA(menu, MF_STRING | (Autostart_IsEnabled() ? MF_CHECKED : 0),
                ID_TRAY_AUTOSTART, "Start with &Windows");
    AppendMenuA(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(menu, MF_STRING, ID_TRAY_EXIT, "E&xit");

    /* Required so the menu dismisses when the user clicks elsewhere. */
    SetForegroundWindow(hWnd);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_LEFTALIGN,
                   pt.x, pt.y, 0, hWnd, NULL);
    PostMessageA(hWnd, WM_NULL, 0, 0);
    DestroyMenu(menu);
}

static void OpenConfigFolder(void) {
    char folder[MAX_PATH];
    strncpy(folder, g_iniPath, MAX_PATH - 1);
    folder[MAX_PATH - 1] = '\0';
    char* slash = strrchr(folder, '\\');
    if (slash) *slash = '\0';
    ShellExecuteA(NULL, "open", folder, NULL, NULL, SW_SHOWNORMAL);
}

static void ToggleEditMode(HWND hWnd) {
    if (Widget_EditMode()) {
        Widget_SavePositions(g_iniPath);
        Widget_SetEditMode(false);
        Settings_NotifyConfigChanged();
    } else {
        Widget_SetEditMode(true);
    }
    (void)hWnd;
}

/* ─────────────────────────── window procedure ─────────────────────────── */

static LRESULT CALLBACK MainWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == g_taskbarCreatedMsg) {
        AddTrayIcon(hWnd);        /* Explorer restarted; re-add our icon */
        DesktopHost_Reattach();
        return 0;
    }
    if (msg == g_reloadMsg) {
        Config_Reload(g_iniPath, GetModuleHandle(NULL));
        return 0;
    }

    switch (msg) {
        case WM_TRAYICON:
            if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) ShowTrayMenu(hWnd);
            else if (lParam == WM_LBUTTONDBLCLK) Settings_Open(GetModuleHandle(NULL), g_iniPath);
            return 0;

        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case ID_TRAY_SETTINGS:  Settings_Open(GetModuleHandle(NULL), g_iniPath); break;
                case ID_TRAY_EDIT:      ToggleEditMode(hWnd); break;
                case ID_TRAY_RELOAD:    Config_Reload(g_iniPath, GetModuleHandle(NULL)); break;
                case ID_TRAY_FOLDER:    OpenConfigFolder(); break;
                case ID_TRAY_AUTOSTART: Autostart_SetEnabled(!Autostart_IsEnabled()); break;
                case ID_TRAY_EXIT:      DestroyWindow(hWnd); break;
                default: break;
            }
            return 0;

        /* Monitors moved or changed resolution: anchors need re-resolving. */
        case WM_DISPLAYCHANGE:
            Config_Reload(g_iniPath, GetModuleHandle(NULL));
            return 0;

        case WM_SETTINGCHANGE:
            TimeFmt_InvalidateLocale();
            Widget_RenderAll();
            return 0;

        case WM_DESTROY:
            RemoveTrayIcon(hWnd);
            Widget_DestroyAll();
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcA(hWnd, msg, wParam, lParam);
}

/* ─────────────────────────── entry point ─────────────────────────── */

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance; (void)lpCmdLine; (void)nCmdShow;

    g_reloadMsg = RegisterWindowMessageA(APP_WAKEUP);

    /*
     * Only one instance may own the widgets. A second launch — a double-click
     * on the exe, or the shortcut firing twice — tells the first to reload
     * and then bows out, instead of stacking duplicate windows.
     */
    HANDLE instanceMutex = CreateMutexA(NULL, TRUE, APP_MUTEX);
    if (instanceMutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        PostMessageA(HWND_BROADCAST, g_reloadMsg, 0, 0);
        CloseHandle(instanceMutex);
        return 0;
    }

    InitDpi();

    GpStartupInput gdiplusInput = { 1, NULL, FALSE, FALSE };
    if (GdiplusStartup(&g_gdiplusToken, &gdiplusInput, NULL) != 0) {
        MessageBoxA(NULL, "GDI+ failed to start.", "LiteWidgets", MB_ICONERROR);
        return 1;
    }

    bool openSettings = false;
    ResolveIniPath(g_iniPath, MAX_PATH);
    ParseCommandLine(g_iniPath, MAX_PATH, &openSettings);
    Config_WriteDefault(g_iniPath);   /* no-op when the file already exists */

    g_trayIcon = CreateTrayIcon();

    WNDCLASSA wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "LiteWidgetsMainClass";
    RegisterClassA(&wc);

    g_mainWnd = CreateWindowExA(0, "LiteWidgetsMainClass", "LiteWidgets", 0,
                                0, 0, 0, 0, HWND_MESSAGE, NULL, hInstance, NULL);
    if (!g_mainWnd) {
        GdiplusShutdown(g_gdiplusToken);
        return 1;
    }

    g_taskbarCreatedMsg = RegisterWindowMessageA("TaskbarCreated");
    AddTrayIcon(g_mainWnd);

    DesktopHost_Init();
    Config_Load(g_iniPath, hInstance);
    if (openSettings) Settings_Open(hInstance, g_iniPath);

    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    if (g_trayIcon) DestroyIcon(g_trayIcon);
    GdiplusShutdown(g_gdiplusToken);
    if (instanceMutex) {
        ReleaseMutex(instanceMutex);
        CloseHandle(instanceMutex);
    }
    return (int)msg.wParam;
}
