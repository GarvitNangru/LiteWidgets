#pragma once
#ifndef CONFIG_H
#define CONFIG_H

#include <windows.h>
#include "style.h"

void Config_Load(const char* iniPath, HINSTANCE hInstance);
DWORD Config_ParseColor(const char* hexStr);
void Config_ParseStyle(const char* iniPath, const char* section, WidgetStyle* out);

#endif /* CONFIG_H */
