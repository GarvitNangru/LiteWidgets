#pragma once
#ifndef CALENDAR_H
#define CALENDAR_H

#include <windows.h>
#include <stdbool.h>

#include "../spec.h"
#include "../gdiplus_helpers.h"

/* Create a live month view from a fully finalized spec. */
bool CalendarWidget_Create(HINSTANCE hInstance, const WidgetSpec* spec);

/*
 * Stateless painter, shared by the live widget and the settings preview.
 * `today` is both the month drawn and the day marked, so a preview can show
 * any date without the widget having to know about it.
 */
void Calendar_Paint(const WidgetSpec* spec, const SYSTEMTIME* today,
                    GpGraphics* gfx, int width, int height);

#endif /* CALENDAR_H */
