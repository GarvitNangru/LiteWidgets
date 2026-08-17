#pragma once

#ifndef DESKTOP_H
#define DESKTOP_H

#include <windows.h>
#include <stdbool.h>

/**
 * Initializes the desktop host environment by locating the 
 * background WorkerW window (behind desktop icons but above wallpaper).
 */
bool DesktopHost_Init(void);

/**
 * Gets the HWND of the WorkerW desktop background.
 * Returns NULL if it could not be found.
 */
HWND DesktopHost_GetParent(void);

/**
 * Intended to be called when the "TaskbarCreated" message is received,
 * indicating that Explorer.exe has crashed and restarted.
 */
void DesktopHost_Reattach(void);

#endif // DESKTOP_H
