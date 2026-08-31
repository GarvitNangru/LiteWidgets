#pragma once
#ifndef SETTINGS_H
#define SETTINGS_H

#include <windows.h>

/* Open the editor, or focus it when it is already open. */
void Settings_Open(HINSTANCE hInstance, const char* iniPath);

/*
 * Tell an open editor that the INI changed underneath it — after widgets
 * were dragged on the desktop, for instance — so it can re-read the file.
 */
void Settings_NotifyConfigChanged(void);

#endif /* SETTINGS_H */
