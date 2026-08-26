#pragma once
#ifndef STYLE_H
#define STYLE_H

#include <windows.h>
#include "gdiplus_helpers.h"

/*
 * WidgetStyle — shared visual styling for all widget types.
 * Parsed from INI config per-widget section.
 */
typedef struct {
    ARGB  bg_color;
    ARGB  text_color;
    ARGB  border_color;
    float border_width;
    float corner_radius;
    WCHAR font_family[64];
    float font_size;
    INT   font_style;    /* GpFontStyle enum */
    int   align_h;       /* 0=Left, 1=Center, 2=Right */
    int   align_v;       /* 0=Top, 1=Center, 2=Bottom */
    float padding;
} WidgetStyle;

/* Default style values */
static inline WidgetStyle Style_Default(void) {
    WidgetStyle s = {0};
    s.bg_color     = 0xD9181825;
    s.text_color   = 0xFFFFFFFF;
    s.border_color = 0x33FFFFFF;
    s.border_width = 0.0f;
    s.corner_radius = 12.0f;
    s.font_size    = 14.0f;
    s.font_style   = FontStyleRegular;
    s.align_h      = 1; /* Center */
    s.align_v      = 1; /* Center */
    s.padding      = 10.0f;
    wcscpy(s.font_family, L"Segoe UI");
    return s;
}

#endif /* STYLE_H */
