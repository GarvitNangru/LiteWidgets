#pragma once
#ifndef CONFIG_H
#define CONFIG_H

#include <windows.h>
#include <stdbool.h>

#include "spec.h"
#include "gdiplus_helpers.h"

/* Create every enabled widget in the INI. Returns how many were created. */
int Config_Load(const char* iniPath, HINSTANCE hInstance);

/* Destroy the live widgets and rebuild them from disk, without restarting. */
int Config_Reload(const char* iniPath, HINSTANCE hInstance);

/*
 * Read one `[section]` into a finalized spec. Returns false when the section
 * has no `type` key, which is how non-widget sections are skipped.
 */
bool Config_ReadSpec(const char* iniPath, const char* section, WidgetSpec* out);

/* Read every widget section, in file order. Returns the number written. */
int Config_ReadAll(const char* iniPath, WidgetSpec* out, int maxCount);

/* Resolve a path that may be relative to the INI's own folder. */
void Config_ResolvePath(const char* iniPath, const char* relative, WCHAR* out, DWORD cap);

/*
 * Paint any spec into a graphics context without creating a window. The
 * settings preview goes through here, so what it shows is what a live widget
 * would draw.
 */
void Config_Paint(const WidgetSpec* spec, const char* iniPath,
                  GpGraphics* gfx, int width, int height);

/* Write a starter config when none exists yet. */
bool Config_WriteDefault(const char* iniPath);

/*
 * Persist one key for a widget's own section.
 *
 * Interactive widgets change their own configuration -- an image is given a
 * new file, a note is given one for the first time -- and the change has to
 * outlive the process. Routing it through here also tells whoever is watching
 * the config, which is how the settings editor stays in step without the
 * widgets having to know it exists.
 */
void Config_WriteKey(const char* iniPath, const char* section,
                     const char* key, const char* value);

/* Register the observer notified by Config_WriteKey. NULL clears it. */
typedef void (*ConfigChangedFn)(void);
void Config_OnChanged(ConfigChangedFn observer);

#endif /* CONFIG_H */
