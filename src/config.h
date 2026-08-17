#pragma once

#ifndef CONFIG_H
#define CONFIG_H

#include <windows.h>
#include <stdbool.h>

/**
 * Loads the widget configuration from an INI file and initializes 
 * the corresponding widget instances.
 */
void Config_Load(const char* iniPath, HINSTANCE hInstance);

/**
 * Parses an ARGB hex string (e.g., "CC1A1A2E") into a 32-bit ARGB value.
 */
DWORD Config_ParseColor(const char* hexStr);

#endif // CONFIG_H
