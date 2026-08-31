#pragma once
#ifndef DESKTOP_H
#define DESKTOP_H

#include <windows.h>
#include <stdbool.h>

/*
 * Desktop-shell integration.
 *
 * Widgets are ordinary top-level windows, so the only thing keeping them
 * visible on the desktop is where they sit in the z-order. That is not a
 * fixed place: a live wallpaper renders into its own window, and "show
 * desktop" reshuffles the whole desktop band. This module answers the one
 * question the widgets need -- what is currently drawing the wallpaper --
 * so they can park themselves directly above it.
 */

bool DesktopHost_Init(void);

/*
 * The window to own widgets to.
 *
 * "Show desktop" raises the desktop band and pushes everything else down.
 * A widget owned by the desktop rides up with it and stays visible; an
 * unowned one is left behind the wallpaper and never comes back. Progman
 * always exists, so this costs nothing -- in particular it does not send
 * Progman the undocumented message that spawns a WorkerW, which is what
 * makes Wallpaper Engine reload.
 *
 * Returns NULL if Progman cannot be found, in which case widgets are
 * created unowned and behave as before.
 */
HWND DesktopHost_GetOwner(void);

/* Called when Explorer restarts and the desktop windows are recreated. */
void DesktopHost_Reattach(void);

/*
 * The window currently drawing the wallpaper: the desktop itself, or a live
 * wallpaper renderer sitting above it. Returns NULL when nothing matches.
 */
HWND DesktopHost_FindWallpaper(void);

/* True when `window` sits behind `reference` in the z-order. */
bool DesktopHost_IsBehind(HWND window, HWND reference);

/* True when the window belongs to the desktop band rather than an app. */
bool DesktopHost_IsDesktopWindow(HWND window);

#endif /* DESKTOP_H */
