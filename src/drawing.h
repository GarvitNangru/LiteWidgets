#pragma once
#ifndef DRAWING_H
#define DRAWING_H

#include "gdiplus_helpers.h"
#include "style.h"

/*
 * Draw a filled rounded rectangle with optional border.
 * If corner_radius is 0, draws a sharp rectangle.
 */
void Drawing_RoundedRect(GpGraphics* gfx, ARGB fillColor, ARGB borderColor,
                         float borderWidth, float x, float y, float w, float h,
                         float radius);

/*
 * Draw text within a rectangle using WidgetStyle for font, color, and alignment.
 * The layoutRect defines the area where text is drawn (apply padding before calling).
 */
void Drawing_Text(GpGraphics* gfx, const WCHAR* text, const GpRectF* layoutRect,
                  const WCHAR* fontFamily, float fontSize, INT fontStyle,
                  ARGB color, int alignH, int alignV);

/*
 * Convenience: draw the full styled background for a widget.
 */
void Drawing_WidgetBackground(GpGraphics* gfx, const WidgetStyle* style, int width, int height);

#endif /* DRAWING_H */
