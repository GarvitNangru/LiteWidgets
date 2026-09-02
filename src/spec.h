#pragma once
#ifndef SPEC_H
#define SPEC_H

#include <windows.h>
#include <stdbool.h>

#include "style.h"

#define LW_SECTION_LEN 64
#define LW_FORMAT_LEN  96
#define LW_MAX_WIDGETS 64

/*
 * Ceiling on the property registry, and the size of the arrays the settings
 * editor builds from it. spec.c asserts the table fits at compile time, so
 * adding a property past this point is a build error rather than a key that
 * quietly stops appearing in the editor.
 */
#define LW_MAX_PROPERTIES 160

/* ─────────────────────────── widget types ─────────────────────────── */

typedef enum {
    WIDGET_CLOCK = 0,
    WIDGET_NOTES,
    WIDGET_IMAGE,
    WIDGET_GAUGE,
    WIDGET_CALENDAR,
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

/* ─────────────────────────── gauge options ─────────────────────────── */

/*
 * What a gauge is measuring. Every one of these is a cheap system call the
 * widget makes on its own tick -- nothing polls, nothing runs a thread, and
 * a reading that has not changed does not cause a repaint.
 */
typedef enum {
    GAUGE_CPU = 0,
    GAUGE_MEMORY,
    GAUGE_DISK,
    GAUGE_BATTERY
} GaugeSource;

typedef enum { GAUGE_BAR = 0, GAUGE_RING, GAUGE_NUMBER } GaugeStyle;

/* How several readings share one widget. AUTO is resolved in Spec_Finalize. */
typedef enum {
    GAUGE_LAYOUT_AUTO = 0,
    GAUGE_LAYOUT_VERTICAL,
    GAUGE_LAYOUT_HORIZONTAL,
    GAUGE_LAYOUT_GRID
} GaugeLayout;

/* Readings one widget can carry. */
#define LW_GAUGE_MAX 8

/*
 * One reading inside a gauge. The keys that describe it -- source, drive,
 * label, the two thresholds -- all take a list, so a machine panel is one
 * widget rather than four windows that have to be aligned by hand.
 */
typedef struct {
    int   source;               /* GaugeSource */
    char  drive[8];             /* which volume the disk source reads */
    WCHAR label[32];            /* empty derives a name from the source */

    /*
     * A reading past either threshold repaints the fill in the warning
     * colour. Two of them because the direction that means trouble depends on
     * the reading: a disk filling up warns high, a battery draining warns low.
     */
    float warn_above;           /* percent; 0 disables */
    float warn_below;           /* percent; 0 disables */
} GaugeItem;

typedef struct {
    GaugeItem items[LW_GAUGE_MAX];
    int   count;                /* readings actually configured, at least 1 */

    /* How many values each list key supplied, so Spec_Finalize can fill in. */
    int   stated_drives, stated_labels, stated_above, stated_below;

    int   style;                /* GaugeStyle, shared by every reading */
    int   layout;               /* GaugeLayout */
    int   columns;              /* grid columns; 0 derives a square-ish grid */
    float spacing;              /* px between readings */

    bool  show_label;
    bool  show_value;
    bool  show_detail;          /* the second line: "6.1 / 16.0 GB" and friends */

    ARGB  track_color;          /* alpha 0 derives from the text colour */
    ARGB  fill_color;           /* alpha 0 derives from the text colour */
    ARGB  fill_color2;
    int   fill_gradient;
    float thickness;            /* bar height, or ring stroke width */
    ARGB  warn_color;
} GaugeOptions;

/* ─────────────────────────── calendar options ─────────────────────── */

typedef enum { WEEK_LOCALE = 0, WEEK_MONDAY, WEEK_SUNDAY } WeekStart;

typedef struct {
    int   week_start;           /* WeekStart */
    bool  show_header;
    bool  show_weekdays;
    bool  show_week_numbers;
    bool  show_outside_days;    /* the greyed days of the neighbouring months */
    WCHAR header_format[LW_FORMAT_LEN];

    ARGB  header_color;         /* alpha 0 derives from the text colour */
    ARGB  weekday_color;
    ARGB  weekend_color;
    ARGB  outside_color;
    ARGB  today_color;          /* the marker behind today */
    ARGB  today_text_color;
    float day_scale;            /* day-number size, relative to font_size */
} CalendarOptions;

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
    bool        click_through_set;  /* stated in the config, not derived */
    bool        width_set, height_set;
    float       opacity;        /* 0..1, multiplies every alpha channel */

    bool        show_seconds;   /* drives the tick rate for time-based widgets */

    /* File-backed widgets */
    char        path[MAX_PATH];
    int         reload_seconds;  /* 0 = load once at startup */
    int         image_fit;       /* ImageFit */

    WidgetStyle     style;
    ClockOptions    clock;
    GaugeOptions    gauge;
    CalendarOptions calendar;
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

/*
 * True for the widget types you use rather than look at. They keep their
 * clicks by default; everything else lets them through to the desktop.
 */
bool Spec_IsInteractive(int type);

/* How a gauge's readings are tiled. Valid once Spec_Finalize has run. */
void Spec_GaugeGrid(const GaugeOptions* gauge, int* columns, int* rows);

/* ─────────────────────────── property registry ─────────────────────────── */

/*
 * PK_LIST is PK_ENUM's plural: a typed field whose value is one or more of
 * `options`, which the editor offers from a dropdown that appends rather
 * than replaces.
 */
typedef enum {
    PK_TEXT = 0, PK_INT, PK_FLOAT, PK_COLOR, PK_BOOL, PK_ENUM, PK_FONT, PK_FILE,
    PK_LIST
} PropKind;

typedef enum {
    PG_GENERAL = 0, PG_LAYOUT, PG_SURFACE, PG_TEXT, PG_EFFECTS,
    PG_CLOCK, PG_ANALOG, PG_GAUGE, PG_CALENDAR, PG_SOURCE, PG__COUNT
} PropGroup;

typedef struct {
    const char*   key;
    const char*   label;
    unsigned char kind;      /* PropKind */
    unsigned char group;     /* PropGroup */
    unsigned int  types;     /* bitmask of TYPE_BIT(WidgetType) */
    const char*   options;   /* '|' separated choices for PK_ENUM and PK_LIST */
    const char*   def;       /* default, as it would appear in the INI */
    const char*   help;
} PropDef;

const PropDef* Spec_Properties(int* count);
const PropDef* Spec_FindProperty(const char* key);
const char*    Spec_GroupName(int group);

/* True when a property applies to the given widget type. */
bool Spec_PropAppliesTo(const PropDef* prop, int type);

/*
 * The default a property takes for one widget type.
 *
 * Almost every default is the same everywhere, but not all: a clock is a
 * decoration you click through, while notes and images are things you use.
 * The editor and the generated reference both resolve defaults through here
 * so neither can disagree with the loader.
 */
const char* Spec_DefaultFor(const PropDef* prop, int type);

#endif /* SPEC_H */
