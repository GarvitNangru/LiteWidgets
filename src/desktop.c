#include "desktop.h"

static HWND g_hWorkerW = NULL;

static BOOL CALLBACK FindWorkerWProc(HWND hwnd, LPARAM lParam) {
    HWND* pWorkerW = (HWND*)lParam;

    /* Look for the window hosting the desktop icons */
    HWND hDefView = FindWindowExA(hwnd, NULL, "SHELLDLL_DefView", NULL);
    if (hDefView != NULL) {
        /* The background WorkerW is the next sibling after this one */
        *pWorkerW = FindWindowExA(NULL, hwnd, "WorkerW", NULL);
        return FALSE;
    }
    return TRUE;
}

bool DesktopHost_Init(void) {
    g_hWorkerW = NULL;

    /*
     * Step 1: Try to find an EXISTING WorkerW.
     * If Wallpaper Engine (or similar software) is already running,
     * the WorkerW already exists. We can just use it without sending
     * the 0x052C message, which would force WE to reload its wallpaper.
     */
    EnumWindows(FindWorkerWProc, (LPARAM)&g_hWorkerW);

    if (g_hWorkerW) {
        return true; /* Found existing WorkerW — no need to disturb anything */
    }

    /*
     * Step 2: No WorkerW found. Send the undocumented message to Progman
     * to create one. This only happens when no wallpaper software is active.
     */
    HWND hProgman = FindWindowA("Progman", "Program Manager");
    if (!hProgman) {
        return false;
    }

    DWORD_PTR res = 0;
    SendMessageTimeoutA(hProgman, 0x052C, 0x0000000D, 0, SMTO_NORMAL, 1000, &res);

    /* Now find the newly spawned WorkerW */
    EnumWindows(FindWorkerWProc, (LPARAM)&g_hWorkerW);

    /* Fallback: use Progman itself */
    if (!g_hWorkerW) {
        g_hWorkerW = hProgman;
    }

    return g_hWorkerW != NULL;
}

HWND DesktopHost_GetParent(void) {
    return g_hWorkerW;
}

void DesktopHost_Reattach(void) {
    DesktopHost_Init();
}
