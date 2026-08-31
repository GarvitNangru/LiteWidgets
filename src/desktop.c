#include "desktop.h"

#include <string.h>

/*
 * Windows draws the wallpaper into Progman (or a WorkerW that Explorer
 * spawns beside it). Live-wallpaper tools do it differently: rather than
 * painting into that window they put their own full-screen window on the
 * desktop, and it does not always stay at the bottom of the z-order --
 * "show desktop" in particular raises it.
 *
 * A widget parked at HWND_BOTTOM therefore ends up behind the wallpaper and
 * stays there. Recognising these renderers lets the widgets sit directly
 * above whichever window is actually painting the desktop.
 *
 * Adding a renderer here is a one-line change; unknown ones simply fall back
 * to the Progman/WorkerW behaviour, which is correct for a static wallpaper.
 */
static const char* kWallpaperClasses[] = {
    "Progman",                  /* the desktop itself */
    "WorkerW",                  /* Explorer's wallpaper host */
    "UnrealWindow",             /* Wallpaper Engine, scene wallpapers */
    "CEF-OSC-WIDGET",           /* Wallpaper Engine, web wallpapers */
    "WallpaperEngine",
    "LivelyWallpaperPlayer",    /* Lively */
    "Lively.PlayerWebView",
};

static bool IsWallpaperClass(const char* name) {
    for (size_t i = 0; i < sizeof(kWallpaperClasses) / sizeof(kWallpaperClasses[0]); i++)
        if (_stricmp(kWallpaperClasses[i], name) == 0) return true;
    return false;
}

bool DesktopHost_IsDesktopWindow(HWND window) {
    if (!window) return false;
    char cls[64];
    if (!GetClassNameA(window, cls, sizeof(cls))) return false;
    return IsWallpaperClass(cls);
}

typedef struct {
    HWND found;
    int  minWidth;
    int  minHeight;
} WallpaperSearch;

static BOOL CALLBACK FindWallpaperProc(HWND window, LPARAM param) {
    WallpaperSearch* search = (WallpaperSearch*)param;

    if (!IsWindowVisible(window)) return TRUE;
    if (!DesktopHost_IsDesktopWindow(window)) return TRUE;

    /*
     * Guard against small helper windows that share a renderer's class:
     * a wallpaper covers a display, so anything much smaller is not one.
     */
    RECT rc;
    if (!GetWindowRect(window, &rc)) return TRUE;
    if (rc.right - rc.left < search->minWidth) return TRUE;
    if (rc.bottom - rc.top < search->minHeight) return TRUE;

    /* EnumWindows runs front to back, so the first hit is the highest. */
    search->found = window;
    return FALSE;
}

HWND DesktopHost_FindWallpaper(void) {
    WallpaperSearch search;
    search.found = NULL;
    /* Half a display is generous enough for odd multi-monitor arrangements. */
    search.minWidth  = GetSystemMetrics(SM_CXSCREEN) / 2;
    search.minHeight = GetSystemMetrics(SM_CYSCREEN) / 2;

    EnumWindows(FindWallpaperProc, (LPARAM)&search);
    if (search.found) return search.found;

    return FindWindowA("Progman", NULL);
}

typedef struct {
    HWND window;
    HWND reference;
    int  windowIndex;
    int  referenceIndex;
    int  index;
} OrderSearch;

static BOOL CALLBACK OrderProc(HWND window, LPARAM param) {
    OrderSearch* search = (OrderSearch*)param;
    search->index++;

    if (window == search->window)    search->windowIndex    = search->index;
    if (window == search->reference) search->referenceIndex = search->index;

    return (search->windowIndex < 0 || search->referenceIndex < 0);
}

bool DesktopHost_IsBehind(HWND window, HWND reference) {
    if (!window || !reference || window == reference) return false;

    OrderSearch search;
    search.window = window;
    search.reference = reference;
    search.windowIndex = -1;
    search.referenceIndex = -1;
    search.index = 0;
    EnumWindows(OrderProc, (LPARAM)&search);

    if (search.windowIndex < 0 || search.referenceIndex < 0) return false;
    return search.windowIndex > search.referenceIndex;   /* larger index = further back */
}

HWND DesktopHost_GetOwner(void) {
    return FindWindowA("Progman", NULL);
}

bool DesktopHost_Init(void) {
    /*
     * Deliberately does not poke Progman with the undocumented 0x052C
     * message: that spawns a WorkerW and makes Wallpaper Engine reload its
     * scene on every launch. We only ever read the window list.
     */
    return true;
}

void DesktopHost_Reattach(void) {
    /* The desktop windows are found on demand, so there is nothing to rebind. */
}
