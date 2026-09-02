#include "desktop.h"

#include "widget.h"

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
 * Guessing wrong is expensive, though -- a class that turns out to belong to
 * a game or an overlay drags every widget up in front of it -- so a match
 * here only counts while the search is still inside the desktop band.
 */
static const char* kWallpaperClasses[] = {
    "Progman",                  /* the desktop itself */
    "WorkerW",                  /* Explorer's wallpaper host */
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

static bool IsWidgetWindow(HWND window) {
    char cls[64];
    if (!GetClassNameA(window, cls, sizeof(cls))) return false;
    return strcmp(cls, LW_WIDGET_CLASS) == 0;
}

/*
 * A window that would be in front of the widgets if they were sitting where
 * they belong: anything the user can see that is not the desktop and not one
 * of our own widgets. Its presence is what proves a candidate above it is
 * not part of the desktop band.
 */
static bool IsOrdinaryWindow(HWND window) {
    if (!IsWindowVisible(window) || IsIconic(window)) return false;
    if (DesktopHost_IsDesktopWindow(window) || IsWidgetWindow(window)) return false;

    RECT rc;
    if (!GetWindowRect(window, &rc)) return false;
    return !IsRectEmpty(&rc);
}

typedef struct {
    HWND desktop;      /* Progman: the bottom of the band, and the stop mark */
    HWND found;
    int  minWidth;
    int  minHeight;
} WallpaperSearch;

static BOOL CALLBACK FindWallpaperProc(HWND window, LPARAM param) {
    WallpaperSearch* search = (WallpaperSearch*)param;

    if (window == search->desktop) return FALSE;   /* the band ends here */

    if (DesktopHost_IsDesktopWindow(window) && IsWindowVisible(window)) {
        /*
         * Guard against small helper windows that share a renderer's class:
         * a wallpaper covers a display, so anything much smaller is not one.
         */
        RECT rc;
        if (GetWindowRect(window, &rc)
            && rc.right - rc.left >= search->minWidth
            && rc.bottom - rc.top >= search->minHeight) {
            search->found = window;
        }
        return TRUE;
    }

    /*
     * EnumWindows runs front to back, so an ordinary window means everything
     * seen so far is in front of it -- and so is not the wallpaper, whatever
     * class it claims. A fullscreen game and the GeForce overlay both land
     * here; without this they were mistaken for a live wallpaper and every
     * widget was hoisted up in front of the game.
     */
    if (IsOrdinaryWindow(window)) search->found = NULL;
    return TRUE;
}

HWND DesktopHost_FindWallpaper(void) {
    WallpaperSearch search;
    search.desktop = FindWindowA("Progman", NULL);
    search.found = NULL;
    /* Half a display is generous enough for odd multi-monitor arrangements. */
    search.minWidth  = GetSystemMetrics(SM_CXSCREEN) / 2;
    search.minHeight = GetSystemMetrics(SM_CYSCREEN) / 2;

    EnumWindows(FindWallpaperProc, (LPARAM)&search);
    return search.found ? search.found : search.desktop;
}

bool DesktopHost_IsMisplaced(HWND window, HWND wallpaper) {
    if (!window || !wallpaper || window == wallpaper) return false;

    /*
     * Walk down the z-order. Reaching the wallpaper without passing an
     * ordinary window means nothing the user cares about is behind the
     * widget, which is the whole of the invariant; running out of windows
     * means the widget fell behind the wallpaper instead.
     */
    for (HWND below = GetWindow(window, GW_HWNDNEXT); below;
         below = GetWindow(below, GW_HWNDNEXT)) {
        if (below == wallpaper) return false;
        if (IsOrdinaryWindow(below)) return true;
    }
    return true;
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
