#pragma once
#ifndef STYLE_H
#define STYLE_H

#include <windows.h>
#include <stdbool.h>
#include "gdiplus_helpers.h"

#define LW_FONT_LEN 64

/* Gradient direction for surfaces and text fills. */
typedef enum {
    GRAD_NONE = 0,
    GRAD_VERTICAL,          /* top -> bottom */
    GRAD_HORIZONTAL,        /* left -> right */
    GRAD_DIAGONAL,          /* top-left -> bottom-right */
    GRAD_DIAGONAL_BACK      /* top-right -> bottom-left */
} GradientDir;

typedef enum {
    TEXT_AS_IS = 0,
    TEXT_UPPER,
    TEXT_LOWER
} TextTransform;

/* Horizontal / vertical alignment share the GDI+ Near/Center/Far encoding. */
enum { ALIGN_NEAR = 0, ALIGN_CENTER = 1, ALIGN_FAR = 2 };

/*
 * WidgetStyle — the complete visual description of a widget surface.
 *
 * Every field is settable from the INI through Style_Set(), which is also
 * what the built-in presets and the settings UI go through. Adding a styling
 * knob means adding a field here plus one case in Style_Set().
 */
typedef struct {
    /* Surface */
    ARGB  bg_color;
    ARGB  bg_color2;        /* gradient end colour */
    int   bg_gradient;      /* GradientDir */
    ARGB  border_color;
    float border_width;
    float corner_radius;

    /* Text */
    WCHAR font_family[LW_FONT_LEN];
    float font_size;
    INT   font_style;       /* GpFontStyle bitmask */
    ARGB  text_color;
    ARGB  text_color2;      /* gradient end colour */
    int   text_gradient;    /* GradientDir */
    float letter_spacing;   /* extra px between glyphs; may be negative */
    float line_spacing;     /* multiplier, 1.0 = font default */
    int   text_transform;   /* TextTransform */

    /* Effects */
    ARGB  shadow_color;
    float shadow_offset_x;
    float shadow_offset_y;
    ARGB  glow_color;
    float glow_radius;      /* px; 0 disables the glow */
    ARGB  outline_color;
    float outline_width;

    /* Layout */
    int   align_h;
    int   align_v;
    float padding;
} WidgetStyle;

/* A style preset is just a list of INI key/value pairs applied in order. */
typedef struct { const char* key; const char* value; } StyleKV;
typedef struct {
    const char*    name;
    const char*    description;
    const StyleKV* pairs;
} StylePreset;

WidgetStyle Style_Default(void);

/*
 * Apply one `key = value` pair. Returns false when the key is not a style
 * key, which lets callers fall through to widget-specific options.
 */
bool Style_Set(WidgetStyle* s, const char* key, const char* value);

/* Apply a named preset (case-insensitive). Returns false if unknown. */
bool Style_ApplyPreset(WidgetStyle* s, const char* name);

/* Preset registry, for the settings UI and documentation. */
const StylePreset* Style_Presets(int* count);

/* Colour helpers. Accepts RGB, ARGB, RRGGBB, AARRGGBB, with optional '#'. */
ARGB Style_ParseColor(const char* text, ARGB fallback);
void Style_FormatColor(ARGB color, char* out, size_t cap);
ARGB Style_WithAlpha(ARGB color, BYTE alpha);
ARGB Style_ScaleAlpha(ARGB color, float factor);

/* Enum parsing shared by the INI loader and the settings UI. */
int  Style_ParseFontStyle(const char* text);
int  Style_ParseAlign(const char* text, int fallback);
int  Style_ParseGradient(const char* text);
bool Style_ParseBool(const char* text, bool fallback);

const char* Style_FontStyleName(int style);
const char* Style_AlignName(int align);
const char* Style_GradientName(int dir);

#endif /* STYLE_H */
