#include "desktop.h"

/*
 * Desktop integration — simplified.
 *
 * We no longer use WorkerW as owner for widget windows (it caused
 * Wallpaper Engine to flicker/reload). Widgets are now standalone
 * popups that survive Win+D via WM_WINDOWPOSCHANGING hooks.
 *
 * This module is kept minimal for future use / Explorer restart handling.
 */

bool DesktopHost_Init(void) {
    return true;
}

HWND DesktopHost_GetParent(void) {
    return NULL;
}

void DesktopHost_Reattach(void) {
    /* Nothing to reattach — widgets don't have a desktop parent */
}
