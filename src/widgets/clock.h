#pragma once
#ifndef CLOCK_H
#define CLOCK_H

#include <windows.h>
#include <stdbool.h>
#include "../style.h"

bool ClockWidget_Create(HINSTANCE hInstance, int x, int y, int width, int height,
                        bool click_through, const char* format, bool show_seconds,
                        bool show_date, const WidgetStyle* style,
                        float date_font_size, INT date_font_style, ARGB date_color);

#endif /* CLOCK_H */
