#pragma once
#ifndef SPEC_H
#define SPEC_H

#include <windows.h>
#include <stdbool.h>

#include "style.h"

#define LW_SECTION_LEN 64
#define LW_FORMAT_LEN  96
#define LW_MAX_WIDGETS 64

/* ─────────────────────────── widget types ─────────────────────────── */

typedef enum {
    WIDGET_CLOCK = 0,
    WIDGET_NOTES,
    WIDGET_IMAGE,
    WIDGET__COUNT
} WidgetType;

#define TYPE_BIT(t)  (1u << (t))
#define TYPE_ANY     0xFFFFFFFFu
#define TYPE_FILE    (TYPE_BIT(WIDGET_NOTES) | TYPE_BIT(WIDGET_IMAGE))

int         Spec_ParseType(const char* text);
const char* Spec_TypeName(int type);

/* ─────────────────────────── clock options ─────────────────────────── */

typedef enum { CLOCK_DIGITAL = 0, CLOCK_ANALOG } ClockMode;

/* How an image fills its box. */
typedef enum { FIT_CONTAIN = 0, FIT_COVER, FIT_STRETCH } ImageFit;

/*
 * Where a widget sits in the window stack.
 *
 * ZORDER_DESKTOP tracks the wallpaper rather than pinning to the very
 * bottom, because a live wallpaper draws into its own window and would
 * otherwise cover the widget.
 */
typedef enum { ZORDER_DESKTOP = 0, ZORDER_BOTTOM, ZORDER_TOP } ZOrder;

typedef struct {
    int   mode;                 /* ClockMode */

    /* Digital */
    bool  use_24h;              /* `format` shorthand, used when time_format is blank */
    WCHAR time_format[LW_FORMAT_LEN];
    WCHAR date_format[LW_FORMAT_LEN];
    bool  show_date;
    bool  blink_separator;      /* dim the ':' on odd seconds */

    WCHAR date_font_family[LW_FONT_LEN];   /* empty inherits the main font */
    float date_font_size;       /* <= 0 derives from the time size */
    INT   date_font_style;
    ARGB  date_color;           /* alpha 0 derives from text_color */
    float date_letter_spacing;
    int   date_transform;       /* TextTransform */
    float date_gap;             /* px between the time and date rows */
    float time_ratio;           /* share of the box the time row occupies */

    /* Analog */
    ARGB  face_color;
    ARGB  face_color2;
    int   face_gradient;
    ARGB  ring_color;
    float ring_width;
    ARGB  tick_color;
    int   tick_count;           /* 0 = none, else major ticks around the dial */
    float tick_length;
    float tick_width;
    bool  minute_ticks;
    ARGB  hour_hand_color;
    ARGB  minute_hand_color;
    ARGB  second_hand_color;
    float hour_hand_width;
    float minute_hand_width;
    float second_hand_width;
    float hand_scale;           /* multiplier on all hand lengths */
    ARGB  hub_color;
    bool  smooth_seconds;       /* sweep instead of tick (costs a 50ms timer) */
} ClockOptions;

/* ─────────────────────────── widget spec ─────────────────────────── */

typedef struct {
    char        section[LW_SECTION_LEN];
    int         type;           /* WidgetType */
    bool        enabled;

    int         x, y;           /* offset from the anchor point */
    int         width, height;
    int         anchor;         /* Anchor */
    int         monitor;        /* -1 = primary */
    int         z_order;        /* ZOrder */
    bool        click_through;
    float       opacity;        /* 0..1, multiplies every alpha channel */

    bool        show_seconds;   /* drives the tick rate for time-based widgets */

    /* File-backed widgets */
    char        path[MAX_PATH];
    int         reload_seconds;  /* 0 = load once at startup */
    int         image_fit;       /* ImageFit */

    WidgetStyle  style;
    ClockOptions clock;
} WidgetSpec;

void Spec_Defaults(WidgetSpec* spec);

/*
 * Apply one `key = value` pair from any source: the INI file, a preset, or
 * the settings editor. Returns false when the key is not recognised, which
 * is how unknown keys get reported instead of silently ignored.
 */
bool Spec_Set(WidgetSpec* spec, const char* key, const char* value);

/*
 * Resolve everything that is derived rather than stated: the time pattern
 * implied by `format`, date colours that inherit from the text colour, analog
 * hand colours, and so on. Call once after the last Spec_Set.
 */
void Spec_Finalize(WidgetSpec* spec);

/* ─────────────────────────── property registry ─────────────────────────── */

typedef enum {
    PK_TEXT = 0, PK_INT, PK_FLOAT, PK_COLOR, PK_BOOL, PK_ENUM, PK_FONT, PK_FILE
} PropKind;

typedef enum {
    PG_GENERAL = 0, PG_LAYOUT, PG_SURFACE, PG_TEXT, PG_EFFECTS,
    PG_CLOCK, PG_ANALOG, PG_SOURCE, PG__COUNT
} PropGroup;

typedef struct {
    const char*   key;
    const char*   label;
    unsigned char kind;      /* PropKind */
    unsigned char group;     /* PropGroup */
    unsigned int  types;     /* bitmask of TYPE_BIT(WidgetType) */
    const char*   options;   /* '|' separated choices for PK_ENUM */
    const char*   def;       /* default, as it would appear in the INI */
    const char*   help;
} PropDef;

const PropDef* Spec_Properties(int* count);
const PropDef* Spec_FindProperty(const char* key);
const char*    Spec_GroupName(int group);

/* True when a property applies to the given widget type. */
bool Spec_PropAppliesTo(const PropDef* prop, int type);

#endif /* SPEC_H */
