#pragma once

#ifndef WIDGET_CLOCK_H
#define WIDGET_CLOCK_H

#include <windows.h>
#include <stdbool.h>

bool ClockWidget_Create(HINSTANCE hInstance, int x, int y, int width, int height, bool click_through, const char* format, int font_size, DWORD bg_color, DWORD text_color);

#endif
