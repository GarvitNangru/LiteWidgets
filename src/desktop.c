#include "desktop.h"
#include <stdio.h>

static HWND g_hWorkerW = NULL;

static BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    HWND* pWorkerW = (HWND*)lParam;
    
    // Look for the window hosting the desktop icons
    HWND hDefView = FindWindowExA(hwnd, NULL, "SHELLDLL_DefView", NULL);
    if (hDefView != NULL) {
        // The background WorkerW is the next sibling
        *pWorkerW = FindWindowExA(NULL, hwnd, "WorkerW", NULL);
        return FALSE; // Stop enumerating
    }
    return TRUE; // Continue enumerating
}

bool DesktopHost_Init(void) {
    g_hWorkerW = NULL;

    // 1. Find Progman (Program Manager)
    HWND hProgman = FindWindowA("Progman", "Program Manager");
    if (!hProgman) {
        return false;
    }

    // 2. Send undocumented message 0x052C to force Progman to spawn WorkerW
    DWORD_PTR res = 0;
    SendMessageTimeoutA(hProgman, 0x052C, 0x0000000D, 0, SMTO_NORMAL, 1000, &res);

    // 3. Find the newly spawned WorkerW
    EnumWindows(EnumWindowsProc, (LPARAM)&g_hWorkerW);

    // 4. Fallback: If not found, just use Progman itself (some older/different OS configs)
    if (!g_hWorkerW) {
        g_hWorkerW = hProgman;
    }

    return g_hWorkerW != NULL;
}

HWND DesktopHost_GetParent(void) {
    return g_hWorkerW;
}

void DesktopHost_Reattach(void) {
    // Re-run the init process to find the new WorkerW after Explorer restart
    DesktopHost_Init();
}
