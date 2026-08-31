#pragma once
#ifndef CLOCK_H
#define CLOCK_H

#include <windows.h>
#include <stdbool.h>

#include "../spec.h"
#include "../gdiplus_helpers.h"

/* Create a live clock window from a fully finalized spec. */
bool ClockWidget_Create(HINSTANCE hInstance, const WidgetSpec* spec);

/*
 * Stateless painter, shared by the live widget and the settings preview.
 * Everything it needs comes from the spec and the supplied time, so a preview
 * is guaranteed to look exactly like the real thing.
 */
void Clock_Paint(const WidgetSpec* spec, const SYSTEMTIME* now,
                 GpGraphics* gfx, int width, int height);

#endif /* CLOCK_H */
