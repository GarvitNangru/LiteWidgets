#pragma once
#ifndef DRAWING_H
#define DRAWING_H

#include <stdbool.h>
#include "gdiplus_helpers.h"
#include "style.h"

/*
 * A single styled run of text.
 *
 * Drawing_Text picks the cheapest renderer that can satisfy the run: plain
 * GdipDrawString when no effects are requested (hinted, crisp, fast), and a
 * glyph-path pipeline when a gradient, glow, shadow, outline or letter
 * spacing is in play.
 */
typedef struct {
    const WCHAR* text;
    GpRectF      bounds;

    const WCHAR* font_family;
    float        font_size;
    INT          font_style;

    int          align_h;      /* ALIGN_NEAR / CENTER / FAR */
    int          align_v;

    ARGB         color;
    ARGB         color2;
    int          gradient;     /* GradientDir */
    GpRectF      gradient_rect; /* zero width = derive from the glyphs */

    float        letter_spacing;
    float        line_spacing;  /* multiplier; 0 or 1 = font default */
    bool         no_wrap;

    ARGB         shadow_color;
    float        shadow_dx, shadow_dy;
    ARGB         glow_color;
    float        glow_radius;
    ARGB         outline_color;
    float        outline_width;
} TextRun;

/* One coloured piece of a single line. */
typedef struct {
    const WCHAR* text;
    ARGB         color;        /* alpha 0 inherits the run colour */
} TextSegment;

/* Build a run that inherits every text-related field from a WidgetStyle. */
TextRun Drawing_Run(const WidgetStyle* s, const WCHAR* text, GpRectF bounds);

/* Render a run. Safe to call with an empty or NULL string. */
void Drawing_Text(GpGraphics* gfx, const TextRun* run);

/*
 * Render one line assembled from differently coloured pieces — a dimmed
 * separator, a quieter AM/PM. Always uses the glyph-path pipeline so the
 * pieces line up exactly.
 */
void Drawing_TextSegments(GpGraphics* gfx, const TextRun* base,
                          const TextSegment* segments, int count);

/* Measured width of a run's text, including letter spacing. */
float Drawing_MeasureWidth(GpGraphics* gfx, const TextRun* run);

/* Rounded-rectangle path. Caller owns the returned path. Radius is clamped. */
GpPath* Drawing_RoundedPath(float x, float y, float w, float h, float radius);

/* Filled (optionally gradient) rounded rectangle with an optional border. */
void Drawing_Surface(GpGraphics* gfx, const WidgetStyle* style, float w, float h);

/* Panel fill using explicit colours, for previews and sub-surfaces. */
void Drawing_Panel(GpGraphics* gfx, ARGB fill, ARGB fill2, int gradient,
                   ARGB borderColor, float borderWidth,
                   float x, float y, float w, float h, float radius);

/* Create a linear gradient brush over `rect`; returns NULL for solid fills. */
GpBrush* Drawing_GradientBrush(const GpRectF* rect, ARGB c1, ARGB c2, int gradient);

/* Apply a text transform in place. Returns `dst`. */
WCHAR* Drawing_Transform(WCHAR* dst, size_t cap, const WCHAR* src, int transform);

#endif /* DRAWING_H */
