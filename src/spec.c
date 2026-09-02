#include "spec.h"
#include "layout.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ─────────────────────────── widget types ─────────────────────────── */

static const char* kTypeNames[WIDGET__COUNT] = { "clock", "notes", "image", "gauge", "calendar" };

int Spec_ParseType(const char* text) {
    if (!text || !text[0]) return -1;
    for (int i = 0; i < WIDGET__COUNT; i++)
        if (_stricmp(kTypeNames[i], text) == 0) return i;
    return -1;
}

const char* Spec_TypeName(int type) {
    if (type < 0 || type >= WIDGET__COUNT) return "unknown";
    return kTypeNames[type];
}

/* ─────────────────────────── defaults ─────────────────────────── */

static void SetW(WCHAR* dst, size_t cap, const char* utf8) {
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, dst, (int)cap);
    dst[cap - 1] = L'\0';
}

void Spec_Defaults(WidgetSpec* spec) {
    if (!spec) return;
    memset(spec, 0, sizeof(*spec));

    spec->type          = WIDGET_CLOCK;
    spec->enabled       = true;
    spec->x             = 0;
    spec->y             = 0;
    spec->width         = 320;
    spec->height        = 140;
    spec->anchor        = ANCHOR_TOP_LEFT;
    spec->monitor       = -1;
    spec->z_order       = ZORDER_DESKTOP;
    spec->click_through = true;
    spec->opacity       = 1.0f;
    spec->show_seconds  = false;
    spec->reload_seconds = 0;
    spec->image_fit     = FIT_CONTAIN;
    spec->style         = Style_Default();

    ClockOptions* c = &spec->clock;
    c->mode            = CLOCK_DIGITAL;
    c->use_24h         = false;
    c->time_format[0]  = L'\0';       /* derived from `format` in Spec_Finalize */
    c->date_format[0]  = L'\0';
    c->show_date       = true;
    c->blink_separator = false;
    c->date_font_family[0] = L'\0';
    c->date_font_size  = 0.0f;        /* derived from font_size */
    c->date_font_style = FontStyleRegular;
    c->date_color      = 0x00000000;  /* derived from text_color */
    c->date_letter_spacing = 0.0f;
    c->date_transform  = TEXT_AS_IS;
    c->date_gap        = 2.0f;
    c->time_ratio      = 0.62f;

    c->face_color        = 0x00000000;
    c->face_color2       = 0x00000000;
    c->face_gradient     = GRAD_NONE;
    c->ring_color        = 0x00000000;
    c->ring_width        = 2.0f;
    c->tick_color        = 0x00000000;
    c->tick_count        = 12;
    c->tick_length       = 8.0f;
    c->tick_width        = 2.0f;
    c->minute_ticks      = false;
    c->hour_hand_color   = 0x00000000;
    c->minute_hand_color = 0x00000000;
    c->second_hand_color = 0x00000000;
    c->hour_hand_width   = 5.0f;
    c->minute_hand_width = 3.5f;
    c->second_hand_width = 1.5f;
    c->hand_scale        = 1.0f;
    c->hub_color         = 0x00000000;
    c->smooth_seconds    = false;

    GaugeOptions* g = &spec->gauge;
    g->count = 1;
    for (int i = 0; i < LW_GAUGE_MAX; i++) {
        g->items[i].source = GAUGE_CPU;
        strcpy(g->items[i].drive, "C:");
        g->items[i].label[0] = L'\0';   /* derived from the source */
        g->items[i].warn_above = 0.0f;
        g->items[i].warn_below = 0.0f;
    }
    g->style         = GAUGE_BAR;
    g->layout        = GAUGE_LAYOUT_AUTO;
    g->columns       = 0;            /* derived from the reading count */
    g->spacing       = 10.0f;
    g->show_label    = true;
    g->show_value    = true;
    g->show_detail   = false;
    g->track_color   = 0x00000000;   /* derived from text_color */
    g->fill_color    = 0x00000000;
    g->fill_color2   = 0x00000000;
    g->fill_gradient = GRAD_NONE;
    g->thickness     = 8.0f;
    g->warn_color    = 0x00000000;

    CalendarOptions* cal = &spec->calendar;
    cal->week_start        = WEEK_LOCALE;
    cal->show_header       = true;
    cal->show_weekdays     = true;
    cal->show_week_numbers = false;
    cal->show_outside_days = true;
    cal->header_format[0]  = L'\0';  /* derived in Spec_Finalize */
    cal->header_color      = 0x00000000;
    cal->weekday_color     = 0x00000000;
    cal->weekend_color     = 0x00000000;
    cal->outside_color     = 0x00000000;
    cal->today_color       = 0x00000000;
    cal->today_text_color  = 0x00000000;
    cal->day_scale         = 1.0f;
}

/* ─────────────────────────── key dispatch ─────────────────────────── */

static int ParseTransformValue(const char* text) {
    if (!text || !text[0]) return TEXT_AS_IS;
    if (_stricmp(text, "upper") == 0 || _stricmp(text, "uppercase") == 0) return TEXT_UPPER;
    if (_stricmp(text, "lower") == 0 || _stricmp(text, "lowercase") == 0) return TEXT_LOWER;
    return TEXT_AS_IS;
}

#define KEY(k) (_stricmp(key, k) == 0)

static int ParseGaugeSource(const char* text) {
    if (_stricmp(text, "memory") == 0 || _stricmp(text, "ram") == 0) return GAUGE_MEMORY;
    if (_stricmp(text, "disk") == 0)                                 return GAUGE_DISK;
    if (_stricmp(text, "battery") == 0)                              return GAUGE_BATTERY;
    return GAUGE_CPU;
}

static int ParseGaugeStyle(const char* text) {
    if (_stricmp(text, "ring") == 0)   return GAUGE_RING;
    if (_stricmp(text, "number") == 0 || _stricmp(text, "text") == 0) return GAUGE_NUMBER;
    return GAUGE_BAR;
}

static int ParseGaugeLayout(const char* text) {
    if (_stricmp(text, "vertical") == 0 || _stricmp(text, "column") == 0)
        return GAUGE_LAYOUT_VERTICAL;
    if (_stricmp(text, "horizontal") == 0 || _stricmp(text, "row") == 0)
        return GAUGE_LAYOUT_HORIZONTAL;
    if (_stricmp(text, "grid") == 0) return GAUGE_LAYOUT_GRID;
    return GAUGE_LAYOUT_AUTO;
}

/*
 * Every key that describes a reading takes one value per reading:
 * "warn_above = 85, 90, 0, 20". A single value covers all of them, so the
 * one-reading spelling of each key is exactly what it always was, and an
 * empty slot ("85, , 90") keeps that reading's default.
 *
 * Returns how many values were supplied, which is what tells Spec_Finalize
 * whether it is looking at a broadcast or a positional list.
 */
#define LIST_TOKEN 48

static int SplitList(const char* value, char out[][LIST_TOKEN], int max) {
    int count = 0;
    const char* p = value;

    while (count < max) {
        const char* comma = strchr(p, ',');
        const char* end = comma ? comma : p + strlen(p);

        while (p < end && (*p == ' ' || *p == '\t')) p++;
        while (end > p && (end[-1] == ' ' || end[-1] == '\t')) end--;

        size_t len = (size_t)(end - p);
        if (len >= LIST_TOKEN) len = LIST_TOKEN - 1;
        memcpy(out[count], p, len);
        out[count][len] = '\0';
        count++;

        if (!comma) break;
        p = comma + 1;
    }
    return count;
}

static int ParseWeekStart(const char* text) {
    if (_stricmp(text, "monday") == 0 || _stricmp(text, "mon") == 0) return WEEK_MONDAY;
    if (_stricmp(text, "sunday") == 0 || _stricmp(text, "sun") == 0) return WEEK_SUNDAY;
    return WEEK_LOCALE;
}

bool Spec_Set(WidgetSpec* spec, const char* key, const char* value) {
    if (!spec || !key || !value) return false;
    ClockOptions* c = &spec->clock;
    GaugeOptions* g = &spec->gauge;
    CalendarOptions* cal = &spec->calendar;

    /* ── general ── */
    if (KEY("type")) {
        int t = Spec_ParseType(value);
        if (t >= 0) spec->type = t;
        return true;
    }
    if (KEY("enabled")) { spec->enabled = Style_ParseBool(value, spec->enabled); return true; }
    if (KEY("preset"))  { Style_ApplyPreset(&spec->style, value);                return true; }
    if (KEY("path")) {
        strncpy(spec->path, value, MAX_PATH - 1);
        spec->path[MAX_PATH - 1] = '\0';
        return true;
    }

    /* ── layout ── */
    if (KEY("x"))       { spec->x = atoi(value);      return true; }
    if (KEY("y"))       { spec->y = atoi(value);      return true; }
    if (KEY("width"))   { spec->width  = atoi(value); spec->width_set  = true; return true; }
    if (KEY("height"))  { spec->height = atoi(value); spec->height_set = true; return true; }
    if (KEY("anchor"))  { spec->anchor = Layout_ParseAnchor(value); return true; }
    if (KEY("monitor")) { spec->monitor = atoi(value); return true; }
    if (KEY("z_order")) {
        if      (_stricmp(value, "top")    == 0) spec->z_order = ZORDER_TOP;
        else if (_stricmp(value, "bottom") == 0) spec->z_order = ZORDER_BOTTOM;
        else                                     spec->z_order = ZORDER_DESKTOP;
        return true;
    }
    if (KEY("click_through")) {
        spec->click_through = Style_ParseBool(value, true);
        spec->click_through_set = true;
        return true;
    }
    if (KEY("opacity")) {
        float v = (float)atof(value);
        spec->opacity = (v > 1.0f) ? v / 100.0f : v;   /* accept 0..1 or 0..100 */
        if (spec->opacity < 0.0f) spec->opacity = 0.0f;
        if (spec->opacity > 1.0f) spec->opacity = 1.0f;
        return true;
    }

    /* ── clock, digital ── */
    if (KEY("mode")) {
        c->mode = (_stricmp(value, "analog") == 0) ? CLOCK_ANALOG : CLOCK_DIGITAL;
        return true;
    }
    if (KEY("format"))       { c->use_24h = (_stricmp(value, "24h") == 0); return true; }
    if (KEY("show_seconds")) { spec->show_seconds = Style_ParseBool(value, false); return true; }
    if (KEY("time_format"))  { SetW(c->time_format, LW_FORMAT_LEN, value); return true; }
    if (KEY("date_format"))  { SetW(c->date_format, LW_FORMAT_LEN, value); return true; }
    if (KEY("show_date"))    { c->show_date = Style_ParseBool(value, true); return true; }
    if (KEY("blink_separator")) { c->blink_separator = Style_ParseBool(value, false); return true; }
    if (KEY("date_font_family")) { SetW(c->date_font_family, LW_FONT_LEN, value); return true; }
    if (KEY("date_font_size"))   { c->date_font_size = (float)atof(value); return true; }
    if (KEY("date_font_style"))  { c->date_font_style = Style_ParseFontStyle(value); return true; }
    if (KEY("date_color"))       { c->date_color = Style_ParseColor(value, c->date_color); return true; }
    if (KEY("date_letter_spacing")) { c->date_letter_spacing = (float)atof(value); return true; }
    if (KEY("date_transform"))   { c->date_transform = ParseTransformValue(value); return true; }
    if (KEY("date_gap"))         { c->date_gap = (float)atof(value); return true; }
    if (KEY("time_ratio")) {
        float v = (float)atof(value);
        if (v > 1.0f) v /= 100.0f;
        if (v < 0.1f) v = 0.1f;
        if (v > 0.95f) v = 0.95f;
        c->time_ratio = v;
        return true;
    }

    /* ── clock, analog ── */
    if (KEY("face_color"))    { c->face_color  = Style_ParseColor(value, c->face_color);  return true; }
    if (KEY("face_color2"))   { c->face_color2 = Style_ParseColor(value, c->face_color2); return true; }
    if (KEY("face_gradient")) { c->face_gradient = Style_ParseGradient(value);            return true; }
    if (KEY("ring_color"))    { c->ring_color = Style_ParseColor(value, c->ring_color);   return true; }
    if (KEY("ring_width"))    { c->ring_width = (float)atof(value);                       return true; }
    if (KEY("tick_color"))    { c->tick_color = Style_ParseColor(value, c->tick_color);   return true; }
    if (KEY("tick_count"))    { c->tick_count = atoi(value);                              return true; }
    if (KEY("tick_length"))   { c->tick_length = (float)atof(value);                      return true; }
    if (KEY("tick_width"))    { c->tick_width = (float)atof(value);                       return true; }
    if (KEY("minute_ticks"))  { c->minute_ticks = Style_ParseBool(value, false);          return true; }
    if (KEY("hour_hand_color"))   { c->hour_hand_color   = Style_ParseColor(value, c->hour_hand_color);   return true; }
    if (KEY("minute_hand_color")) { c->minute_hand_color = Style_ParseColor(value, c->minute_hand_color); return true; }
    if (KEY("second_hand_color")) { c->second_hand_color = Style_ParseColor(value, c->second_hand_color); return true; }
    if (KEY("hour_hand_width"))   { c->hour_hand_width   = (float)atof(value); return true; }
    if (KEY("minute_hand_width")) { c->minute_hand_width = (float)atof(value); return true; }
    if (KEY("second_hand_width")) { c->second_hand_width = (float)atof(value); return true; }
    if (KEY("hand_scale"))        { c->hand_scale = (float)atof(value);        return true; }
    if (KEY("hub_color"))         { c->hub_color = Style_ParseColor(value, c->hub_color); return true; }
    if (KEY("smooth_seconds"))    { c->smooth_seconds = Style_ParseBool(value, false);    return true; }

    /* ── gauge ── */
    if (KEY("source")) {
        char tokens[LW_GAUGE_MAX][LIST_TOKEN];
        int supplied = SplitList(value, tokens, LW_GAUGE_MAX);
        int count = 0;
        for (int i = 0; i < supplied; i++)
            if (tokens[i][0]) g->items[count++].source = ParseGaugeSource(tokens[i]);
        if (count > 0) g->count = count;
        return true;
    }
    if (KEY("drive")) {
        char tokens[LW_GAUGE_MAX][LIST_TOKEN];
        g->stated_drives = SplitList(value, tokens, LW_GAUGE_MAX);
        for (int i = 0; i < g->stated_drives; i++) {
            if (!tokens[i][0]) continue;
            strncpy(g->items[i].drive, tokens[i], sizeof(g->items[i].drive) - 1);
            g->items[i].drive[sizeof(g->items[i].drive) - 1] = '\0';
        }
        return true;
    }
    if (KEY("label")) {
        char tokens[LW_GAUGE_MAX][LIST_TOKEN];
        g->stated_labels = SplitList(value, tokens, LW_GAUGE_MAX);
        for (int i = 0; i < g->stated_labels; i++)
            if (tokens[i][0]) SetW(g->items[i].label, 32, tokens[i]);
        return true;
    }
    if (KEY("warn_above")) {
        char tokens[LW_GAUGE_MAX][LIST_TOKEN];
        g->stated_above = SplitList(value, tokens, LW_GAUGE_MAX);
        for (int i = 0; i < g->stated_above; i++)
            if (tokens[i][0]) g->items[i].warn_above = (float)atof(tokens[i]);
        return true;
    }
    if (KEY("warn_below")) {
        char tokens[LW_GAUGE_MAX][LIST_TOKEN];
        g->stated_below = SplitList(value, tokens, LW_GAUGE_MAX);
        for (int i = 0; i < g->stated_below; i++)
            if (tokens[i][0]) g->items[i].warn_below = (float)atof(tokens[i]);
        return true;
    }
    if (KEY("gauge_style"))    { g->style  = ParseGaugeStyle(value);   return true; }
    if (KEY("gauge_layout"))   { g->layout = ParseGaugeLayout(value);  return true; }
    if (KEY("gauge_columns"))  { g->columns = atoi(value);             return true; }
    if (KEY("gauge_spacing"))  { g->spacing = (float)atof(value);      return true; }
    if (KEY("show_label"))     { g->show_label  = Style_ParseBool(value, true);  return true; }
    if (KEY("show_value"))     { g->show_value  = Style_ParseBool(value, true);  return true; }
    if (KEY("show_detail"))    { g->show_detail = Style_ParseBool(value, false); return true; }
    if (KEY("track_color"))    { g->track_color = Style_ParseColor(value, g->track_color); return true; }
    if (KEY("fill_color"))     { g->fill_color  = Style_ParseColor(value, g->fill_color);  return true; }
    if (KEY("fill_color2"))    { g->fill_color2 = Style_ParseColor(value, g->fill_color2); return true; }
    if (KEY("fill_gradient"))  { g->fill_gradient = Style_ParseGradient(value); return true; }
    if (KEY("thickness"))      { g->thickness  = (float)atof(value);            return true; }
    if (KEY("warn_color"))     { g->warn_color = Style_ParseColor(value, g->warn_color); return true; }

    /* ── calendar ── */
    if (KEY("week_start"))        { cal->week_start = ParseWeekStart(value);              return true; }
    if (KEY("show_header"))       { cal->show_header = Style_ParseBool(value, true);      return true; }
    if (KEY("show_weekdays"))     { cal->show_weekdays = Style_ParseBool(value, true);    return true; }
    if (KEY("show_week_numbers")) { cal->show_week_numbers = Style_ParseBool(value, false); return true; }
    if (KEY("show_outside_days")) { cal->show_outside_days = Style_ParseBool(value, true); return true; }
    if (KEY("header_format"))     { SetW(cal->header_format, LW_FORMAT_LEN, value);       return true; }
    if (KEY("header_color"))      { cal->header_color = Style_ParseColor(value, cal->header_color);   return true; }
    if (KEY("weekday_color"))     { cal->weekday_color = Style_ParseColor(value, cal->weekday_color); return true; }
    if (KEY("weekend_color"))     { cal->weekend_color = Style_ParseColor(value, cal->weekend_color); return true; }
    if (KEY("outside_color"))     { cal->outside_color = Style_ParseColor(value, cal->outside_color); return true; }
    if (KEY("today_color"))       { cal->today_color = Style_ParseColor(value, cal->today_color);     return true; }
    if (KEY("today_text_color"))  { cal->today_text_color = Style_ParseColor(value, cal->today_text_color); return true; }
    if (KEY("day_scale"))         { cal->day_scale = (float)atof(value);                  return true; }

    /* ── everything visual ── */
    return Style_Set(&spec->style, key, value);
}

#undef KEY

/* ─────────────────────────── derivation ─────────────────────────── */

bool Spec_IsInteractive(int type) {
    return type == WIDGET_NOTES || type == WIDGET_IMAGE;
}

/* How the readings of one gauge are tiled, once the layout is resolved. */
void Spec_GaugeGrid(const GaugeOptions* g, int* cols, int* rows) {
    int columns = 1;
    switch (g->layout) {
        case GAUGE_LAYOUT_HORIZONTAL: columns = g->count; break;
        case GAUGE_LAYOUT_GRID:       columns = g->columns; break;
        default:                      columns = 1; break;
    }
    if (columns < 1) columns = 1;
    if (columns > g->count) columns = g->count;

    *cols = columns;
    *rows = (g->count + columns - 1) / columns;
}

/*
 * Everything about a gauge that is derived rather than stated: how many
 * readings there are, how they are tiled, and the colours a preset should
 * have dressed but does not know about.
 */
static void ResolveGauge(GaugeOptions* g, const WidgetStyle* s) {
    if (g->count < 1) g->count = 1;
    if (g->count > LW_GAUGE_MAX) g->count = LW_GAUGE_MAX;

    /* One value covers every reading; several are positional. */
    for (int i = 1; i < g->count; i++) {
        if (g->stated_drives == 1) strcpy(g->items[i].drive, g->items[0].drive);
        if (g->stated_labels == 1) wcscpy(g->items[i].label, g->items[0].label);
        if (g->stated_above  == 1) g->items[i].warn_above = g->items[0].warn_above;
        if (g->stated_below  == 1) g->items[i].warn_below = g->items[0].warn_below;
    }

    for (int i = 0; i < g->count; i++) {
        GaugeItem* item = &g->items[i];
        if (item->warn_above < 0.0f)   item->warn_above = 0.0f;
        if (item->warn_above > 100.0f) item->warn_above = 100.0f;
        if (item->warn_below < 0.0f)   item->warn_below = 0.0f;
        if (item->warn_below > 100.0f) item->warn_below = 100.0f;
        if (!item->drive[0]) strcpy(item->drive, "C:");
    }

    /*
     * Bars are wide and short, so they stack; a row of rings reads better
     * than a column of them. Stating a layout always wins.
     */
    if (g->layout == GAUGE_LAYOUT_AUTO)
        g->layout = (g->style == GAUGE_BAR) ? GAUGE_LAYOUT_VERTICAL
                                            : GAUGE_LAYOUT_HORIZONTAL;
    if (g->columns < 1) {
        int columns = 1;
        while (columns * columns < g->count) columns++;   /* the squarest grid */
        g->columns = columns;
    }
    if (g->spacing < 0.0f) g->spacing = 0.0f;

    if (((g->fill_color >> 24) & 0xFFu) == 0)
        g->fill_color = s->text_color;
    if (((g->track_color >> 24) & 0xFFu) == 0)
        g->track_color = Style_ScaleAlpha(s->text_color, 0.18f);
    if (((g->warn_color >> 24) & 0xFFu) == 0)
        g->warn_color = 0xFFE06C75;
    if (g->thickness <= 0.0f) g->thickness = 8.0f;
}

/*
 * A widget nobody has sized yet should be born the right shape for what it
 * is: a month grid at 320x140 is unreadable, a ring in a 280x80 box is a
 * dial with nothing around it, and four stacked bars need four bars' worth
 * of height. Only the types added after 320x140 became the default differ
 * from it -- changing what an existing config resolves to would move widgets
 * people have already placed. Stated sizes always win.
 */
static void DefaultSize(const WidgetSpec* spec, int* width, int* height) {
    switch (spec->type) {
        case WIDGET_GAUGE: {
            const GaugeOptions* g = &spec->gauge;
            bool round = (g->style != GAUGE_BAR);
            int cols = 1, rows = 1;
            Spec_GaugeGrid(g, &cols, &rows);

            int cellW = round ? 170 : 280;
            int cellH = round ? 170 :  80;
            int gap   = (int)(g->spacing + 0.5f);
            *width  = cellW * cols + gap * (cols - 1);
            *height = cellH * rows + gap * (rows - 1);
            break;
        }
        case WIDGET_CALENDAR: *width = 300; *height = 290; break;
        default:              *width = 320; *height = 140; break;
    }
}

void Spec_Finalize(WidgetSpec* spec) {
    if (!spec) return;
    ClockOptions* c = &spec->clock;
    const WidgetStyle* s = &spec->style;

    ResolveGauge(&spec->gauge, s);   /* the default size depends on the grid */

    int width = 0, height = 0;
    DefaultSize(spec, &width, &height);
    if (!spec->width_set)  spec->width  = width;
    if (!spec->height_set) spec->height = height;

    if (spec->width  < 8) spec->width  = 8;
    if (spec->height < 8) spec->height = 8;

    if (c->time_format[0] == L'\0') {
        const WCHAR* pattern;
        if (c->use_24h) pattern = spec->show_seconds ? L"HH:mm:ss" : L"HH:mm";
        else            pattern = spec->show_seconds ? L"h:mm:ss tt" : L"h:mm tt";
        wcscpy(c->time_format, pattern);
    }
    if (c->date_format[0] == L'\0')
        wcscpy(c->date_format, L"dddd, MMMM d");
    if (c->date_font_family[0] == L'\0')
        wcscpy(c->date_font_family, s->font_family);
    if (c->date_font_size <= 0.0f)
        c->date_font_size = s->font_size * 0.4f;
    if (((c->date_color >> 24) & 0xFFu) == 0)
        c->date_color = Style_ScaleAlpha(s->text_color, 0.6f);

    /* Analog defaults trail the panel style so a preset dresses the dial too. */
    if (((c->face_color >> 24) & 0xFFu) == 0 && c->face_gradient != GRAD_NONE)
        c->face_color = s->bg_color;
    if (((c->ring_color >> 24) & 0xFFu) == 0)
        c->ring_color = Style_ScaleAlpha(s->text_color, 0.25f);
    if (((c->tick_color >> 24) & 0xFFu) == 0)
        c->tick_color = Style_ScaleAlpha(s->text_color, 0.55f);
    if (((c->hour_hand_color >> 24) & 0xFFu) == 0)
        c->hour_hand_color = s->text_color;
    if (((c->minute_hand_color >> 24) & 0xFFu) == 0)
        c->minute_hand_color = s->text_color;
    if (((c->second_hand_color >> 24) & 0xFFu) == 0)
        c->second_hand_color = (((s->glow_color >> 24) & 0xFFu) != 0)
                             ? Style_WithAlpha(s->glow_color, 0xFF)
                             : 0xFFE06C75;
    if (((c->hub_color >> 24) & 0xFFu) == 0)
        c->hub_color = c->second_hand_color;

    if (c->hand_scale <= 0.0f) c->hand_scale = 1.0f;
    if (spec->opacity <= 0.0f) spec->opacity = 1.0f;

    /*
     * Calendar colours trail the text colour the way the dial's do, so a
     * preset dresses them without having to know they exist.
     */
    CalendarOptions* cal = &spec->calendar;
    if (cal->header_format[0] == 0)
        wcscpy(cal->header_format, L"MMMM yyyy");
    if (((cal->header_color >> 24) & 0xFFu) == 0)
        cal->header_color = s->text_color;
    if (((cal->weekday_color >> 24) & 0xFFu) == 0)
        cal->weekday_color = Style_ScaleAlpha(s->text_color, 0.50f);
    if (((cal->weekend_color >> 24) & 0xFFu) == 0)
        cal->weekend_color = Style_ScaleAlpha(s->text_color, 0.75f);
    if (((cal->outside_color >> 24) & 0xFFu) == 0)
        cal->outside_color = Style_ScaleAlpha(s->text_color, 0.22f);
    if (((cal->today_color >> 24) & 0xFFu) == 0)
        cal->today_color = Style_WithAlpha(s->text_color, 0xFF);

    /*
     * Today's number sits on the marker, not on the panel, so it has to
     * contrast with the marker. Taking the panel colour looked right until a
     * translucent-white preset put a white number on a white disc; asking the
     * marker how bright it is works for any preset.
     */
    if (((cal->today_text_color >> 24) & 0xFFu) == 0) {
        ARGB marker = cal->today_color;
        float luma = (0.2126f * (float)((marker >> 16) & 0xFFu)
                    + 0.7152f * (float)((marker >>  8) & 0xFFu)
                    + 0.0722f * (float)( marker        & 0xFFu)) / 255.0f;
        cal->today_text_color = (luma > 0.55f) ? 0xFF16161C : 0xFFF2F2F7;
    }
    if (cal->day_scale <= 0.0f) cal->day_scale = 1.0f;

    /*
     * Notes and images are used -- typed into, dropped onto -- so they keep
     * their clicks. Everything else is looked at, and clicks belong to the
     * desktop icons behind it. Either way the config can say otherwise.
     */
    if (!spec->click_through_set)
        spec->click_through = !Spec_IsInteractive(spec->type);
}

/* ─────────────────────────── property registry ─────────────────────────── */

#define CLOCK_ONLY    TYPE_BIT(WIDGET_CLOCK)
#define GAUGE_ONLY    TYPE_BIT(WIDGET_GAUGE)
#define CALENDAR_ONLY TYPE_BIT(WIDGET_CALENDAR)

/* Everything that puts glyphs on the screen, and so takes the text keys. */
#define TEXTUAL    (TYPE_BIT(WIDGET_CLOCK) | TYPE_BIT(WIDGET_NOTES) | \
                    TYPE_BIT(WIDGET_GAUGE) | TYPE_BIT(WIDGET_CALENDAR))

static const PropDef g_props[] = {
/*  key                    label               kind      group       types       options                                     default        help */
{"type",               "Type",              PK_ENUM,  PG_GENERAL, TYPE_ANY,   "clock|notes|image|gauge|calendar",         "clock",       "Which widget to render"},
{"enabled",            "Enabled",           PK_BOOL,  PG_GENERAL, TYPE_ANY,   NULL,                                       "true",        "Set to false to keep the config but hide the widget"},
{"preset",             "Style preset",      PK_ENUM,  PG_GENERAL, TYPE_ANY,   "@presets",                                 "",            "Applies a themed set of colours; individual keys still win"},

{"anchor",             "Anchor",            PK_ENUM,  PG_LAYOUT,  TYPE_ANY,   "top_left|top_center|top_right|left|center|right|bottom_left|bottom_center|bottom_right", "top_left", "Corner or edge that X/Y are measured from"},
{"monitor",            "Monitor",           PK_INT,   PG_LAYOUT,  TYPE_ANY,   NULL,                                       "-1",          "Monitor index, or -1 for the primary display"},
{"x",                  "X offset",          PK_INT,   PG_LAYOUT,  TYPE_ANY,   NULL,                                       "0",           "Horizontal offset from the anchor, in pixels"},
{"y",                  "Y offset",          PK_INT,   PG_LAYOUT,  TYPE_ANY,   NULL,                                       "0",           "Vertical offset from the anchor, in pixels"},
{"width",              "Width",             PK_INT,   PG_LAYOUT,  TYPE_ANY,   NULL,                                       "320",         "Widget width in pixels; the default varies by type"},
{"height",             "Height",            PK_INT,   PG_LAYOUT,  TYPE_ANY,   NULL,                                       "140",         "Widget height in pixels; the default varies by type"},
{"opacity",            "Opacity",           PK_FLOAT, PG_LAYOUT,  TYPE_ANY,   NULL,                                       "1.0",         "Master transparency, 0.0 to 1.0"},
{"click_through",      "Click-through",     PK_BOOL,  PG_LAYOUT,  TYPE_ANY,   NULL,                                       "true",        "Let clicks pass to the desktop underneath; off by default for notes and image, which you interact with"},
{"z_order",            "Layer",             PK_ENUM,  PG_LAYOUT,  TYPE_ANY,   "desktop|bottom|top",                       "desktop",     "desktop follows the wallpaper, bottom pins to the very back, top floats above all windows"},

{"bg_color",           "Background",        PK_COLOR, PG_SURFACE, TYPE_ANY,   NULL,                                       "D9181825",    "Panel fill, AARRGGBB"},
{"bg_color2",          "Background 2",      PK_COLOR, PG_SURFACE, TYPE_ANY,   NULL,                                       "00000000",    "Gradient end colour for the panel"},
{"bg_gradient",        "Bg gradient",       PK_ENUM,  PG_SURFACE, TYPE_ANY,   "none|vertical|horizontal|diagonal|diagonal_back", "none", "Direction of the panel gradient"},
{"border_color",       "Border",            PK_COLOR, PG_SURFACE, TYPE_ANY,   NULL,                                       "33FFFFFF",    "Border colour"},
{"border_width",       "Border width",      PK_FLOAT, PG_SURFACE, TYPE_ANY,   NULL,                                       "0",           "Border thickness in pixels, 0 for none"},
{"corner_radius",      "Corner radius",     PK_FLOAT, PG_SURFACE, TYPE_ANY,   NULL,                                       "12",          "Rounded corner radius in pixels"},
{"padding",            "Padding",           PK_FLOAT, PG_SURFACE, TYPE_ANY,   NULL,                                       "10",          "Inner margin between the panel edge and content"},

{"font_family",        "Font",              PK_FONT,  PG_TEXT,    TEXTUAL,    NULL,                                       "Segoe UI",    "Font family name"},
{"font_size",          "Font size",         PK_FLOAT, PG_TEXT,    TEXTUAL,    NULL,                                       "14",          "Font size in pixels"},
{"font_style",         "Font style",        PK_ENUM,  PG_TEXT,    TEXTUAL,    "regular|bold|italic|bold_italic",          "regular",     "Weight and slant"},
{"text_color",         "Text colour",       PK_COLOR, PG_TEXT,    TEXTUAL,    NULL,                                       "FFFFFFFF",    "Primary text colour"},
{"text_color2",        "Text colour 2",     PK_COLOR, PG_TEXT,    TEXTUAL,    NULL,                                       "00000000",    "Gradient end colour for text"},
{"text_gradient",      "Text gradient",     PK_ENUM,  PG_TEXT,    TEXTUAL,    "none|vertical|horizontal|diagonal|diagonal_back", "none", "Direction of the text gradient"},
{"letter_spacing",     "Letter spacing",    PK_FLOAT, PG_TEXT,    TEXTUAL,    NULL,                                       "0",           "Extra pixels between glyphs; may be negative"},
{"line_spacing",       "Line spacing",      PK_FLOAT, PG_TEXT,    TEXTUAL,    NULL,                                       "1.0",         "Line height multiplier"},
{"text_transform",     "Transform",         PK_ENUM,  PG_TEXT,    TEXTUAL,    "none|upper|lower",                         "none",        "Force upper or lower case"},
{"align_h",            "Horizontal align",  PK_ENUM,  PG_TEXT,    TEXTUAL,    "left|center|right",                        "center",      "Horizontal text alignment"},
{"align_v",            "Vertical align",    PK_ENUM,  PG_TEXT,    TEXTUAL,    "top|center|bottom",                        "center",      "Vertical text alignment"},

{"shadow_color",       "Shadow",            PK_COLOR, PG_EFFECTS, TEXTUAL,    NULL,                                       "00000000",    "Drop shadow colour; alpha 0 disables it"},
{"shadow_offset_x",    "Shadow X",          PK_FLOAT, PG_EFFECTS, TEXTUAL,    NULL,                                       "0",           "Shadow offset in pixels"},
{"shadow_offset_y",    "Shadow Y",          PK_FLOAT, PG_EFFECTS, TEXTUAL,    NULL,                                       "2",           "Shadow offset in pixels"},
{"glow_color",         "Glow",              PK_COLOR, PG_EFFECTS, TEXTUAL,    NULL,                                       "00000000",    "Glow colour behind the text"},
{"glow_radius",        "Glow radius",       PK_FLOAT, PG_EFFECTS, TEXTUAL,    NULL,                                       "0",           "Glow spread in pixels, 0 for none"},
{"outline_color",      "Outline",           PK_COLOR, PG_EFFECTS, TEXTUAL,    NULL,                                       "00000000",    "Colour of the glyph outline"},
{"outline_width",      "Outline width",     PK_FLOAT, PG_EFFECTS, TEXTUAL,    NULL,                                       "0",           "Outline thickness in pixels"},

{"mode",               "Clock mode",        PK_ENUM,  PG_CLOCK,   CLOCK_ONLY, "digital|analog",                           "digital",     "Digital readout or an analog dial"},
{"format",             "Hour format",       PK_ENUM,  PG_CLOCK,   CLOCK_ONLY, "12h|24h",                                  "12h",         "Shorthand used when no explicit pattern is set"},
{"show_seconds",       "Show seconds",      PK_BOOL,  PG_CLOCK,   CLOCK_ONLY, NULL,                                       "false",       "Include seconds and tick once a second"},
{"time_format",        "Time pattern",      PK_TEXT,  PG_CLOCK,   CLOCK_ONLY, NULL,                                       "",            "Custom pattern, e.g. h:mm tt - overrides Hour format"},
{"blink_separator",    "Blink colon",       PK_BOOL,  PG_CLOCK,   CLOCK_ONLY, NULL,                                       "false",       "Fade the separator on odd seconds"},
{"show_date",          "Show date",         PK_BOOL,  PG_CLOCK,   CLOCK_ONLY, NULL,                                       "true",        "Draw a second line below the time"},
{"date_format",        "Date pattern",      PK_TEXT,  PG_CLOCK,   CLOCK_ONLY, "",                                         "dddd, MMMM d","Date pattern, e.g. ddd d MMM yyyy"},
{"date_font_family",   "Date font",         PK_FONT,  PG_CLOCK,   CLOCK_ONLY, NULL,                                       "",            "Blank inherits the main font"},
{"date_font_size",     "Date size",         PK_FLOAT, PG_CLOCK,   CLOCK_ONLY, NULL,                                       "0",           "0 means 40% of the time size"},
{"date_font_style",    "Date style",        PK_ENUM,  PG_CLOCK,   CLOCK_ONLY, "regular|bold|italic|bold_italic",          "regular",     "Weight and slant of the date line"},
{"date_color",         "Date colour",       PK_COLOR, PG_CLOCK,   CLOCK_ONLY, NULL,                                       "00000000",    "Alpha 0 derives 60% of the text colour"},
{"date_letter_spacing","Date spacing",      PK_FLOAT, PG_CLOCK,   CLOCK_ONLY, NULL,                                       "0",           "Extra pixels between date glyphs"},
{"date_transform",     "Date transform",    PK_ENUM,  PG_CLOCK,   CLOCK_ONLY, "none|upper|lower",                         "none",        "Force upper or lower case on the date"},
{"date_gap",           "Date gap",          PK_FLOAT, PG_CLOCK,   CLOCK_ONLY, NULL,                                       "2",           "Vertical gap between the time and date rows"},
{"time_ratio",         "Time row share",    PK_FLOAT, PG_CLOCK,   CLOCK_ONLY, NULL,                                       "0.62",        "Share of the height given to the time row"},

{"face_color",         "Face",              PK_COLOR, PG_ANALOG,  CLOCK_ONLY, NULL,                                       "00000000",    "Dial fill; alpha 0 leaves the panel showing"},
{"face_color2",        "Face 2",            PK_COLOR, PG_ANALOG,  CLOCK_ONLY, NULL,                                       "00000000",    "Gradient end colour for the dial"},
{"face_gradient",      "Face gradient",     PK_ENUM,  PG_ANALOG,  CLOCK_ONLY, "none|vertical|horizontal|diagonal|diagonal_back", "none", "Direction of the dial gradient"},
{"ring_color",         "Ring",              PK_COLOR, PG_ANALOG,  CLOCK_ONLY, NULL,                                       "00000000",    "Outer ring colour"},
{"ring_width",         "Ring width",        PK_FLOAT, PG_ANALOG,  CLOCK_ONLY, NULL,                                       "2",           "Outer ring thickness, 0 for none"},
{"tick_color",         "Ticks",             PK_COLOR, PG_ANALOG,  CLOCK_ONLY, NULL,                                       "00000000",    "Hour tick colour"},
{"tick_count",         "Tick count",        PK_INT,   PG_ANALOG,  CLOCK_ONLY, NULL,                                       "12",          "Number of major ticks, 0 for none"},
{"tick_length",        "Tick length",       PK_FLOAT, PG_ANALOG,  CLOCK_ONLY, NULL,                                       "8",           "Major tick length in pixels"},
{"tick_width",         "Tick width",        PK_FLOAT, PG_ANALOG,  CLOCK_ONLY, NULL,                                       "2",           "Major tick thickness in pixels"},
{"minute_ticks",       "Minute ticks",      PK_BOOL,  PG_ANALOG,  CLOCK_ONLY, NULL,                                       "false",       "Draw the 60 fine minute marks"},
{"hour_hand_color",    "Hour hand",         PK_COLOR, PG_ANALOG,  CLOCK_ONLY, NULL,                                       "00000000",    "Hour hand colour"},
{"minute_hand_color",  "Minute hand",       PK_COLOR, PG_ANALOG,  CLOCK_ONLY, NULL,                                       "00000000",    "Minute hand colour"},
{"second_hand_color",  "Second hand",       PK_COLOR, PG_ANALOG,  CLOCK_ONLY, NULL,                                       "00000000",    "Second hand colour"},
{"hour_hand_width",    "Hour width",        PK_FLOAT, PG_ANALOG,  CLOCK_ONLY, NULL,                                       "5",           "Hour hand thickness"},
{"minute_hand_width",  "Minute width",      PK_FLOAT, PG_ANALOG,  CLOCK_ONLY, NULL,                                       "3.5",         "Minute hand thickness"},
{"second_hand_width",  "Second width",      PK_FLOAT, PG_ANALOG,  CLOCK_ONLY, NULL,                                       "1.5",         "Second hand thickness"},
{"hand_scale",         "Hand scale",        PK_FLOAT, PG_ANALOG,  CLOCK_ONLY, NULL,                                       "1.0",         "Multiplier applied to every hand length"},
{"hub_color",          "Hub",               PK_COLOR, PG_ANALOG,  CLOCK_ONLY, NULL,                                       "00000000",    "Centre cap colour"},
{"smooth_seconds",     "Sweep seconds",     PK_BOOL,  PG_ANALOG,  CLOCK_ONLY, NULL,                                       "false",       "Sweep the second hand; redraws ~20x a second"},

{"source",             "Readings",          PK_LIST,  PG_GAUGE,   GAUGE_ONLY, "cpu|memory|disk|battery",                  "cpu",         "What the gauge measures; list several to put them in one widget"},
{"gauge_style",        "Gauge style",       PK_ENUM,  PG_GAUGE,   GAUGE_ONLY, "bar|ring|number",                          "bar",         "A track, an arc, or the number on its own"},
{"gauge_layout",       "Arrangement",       PK_ENUM,  PG_GAUGE,   GAUGE_ONLY, "auto|vertical|horizontal|grid",            "auto",        "How several readings are arranged; auto stacks bars and puts rings in a row"},
{"gauge_columns",      "Grid columns",      PK_INT,   PG_GAUGE,   GAUGE_ONLY, NULL,                                       "0",           "Columns when the arrangement is a grid; 0 picks the squarest one"},
{"gauge_spacing",      "Spacing",           PK_FLOAT, PG_GAUGE,   GAUGE_ONLY, NULL,                                       "10",          "Pixels between readings"},
{"drive",              "Drive",             PK_TEXT,  PG_GAUGE,   GAUGE_ONLY, NULL,                                       "C:",          "Which volume a disk reading measures; one per reading"},
{"label",              "Label",             PK_TEXT,  PG_GAUGE,   GAUGE_ONLY, NULL,                                       "",            "Blank names each reading after its source; one per reading"},
{"show_label",         "Show label",        PK_BOOL,  PG_GAUGE,   GAUGE_ONLY, NULL,                                       "true",        "Draw the label"},
{"show_value",         "Show value",        PK_BOOL,  PG_GAUGE,   GAUGE_ONLY, NULL,                                       "true",        "Draw the percentage"},
{"show_detail",        "Show detail",       PK_BOOL,  PG_GAUGE,   GAUGE_ONLY, NULL,                                       "false",       "Add the underlying figures, e.g. 6.1 / 16.0 GB"},
{"fill_color",         "Fill",              PK_COLOR, PG_GAUGE,   GAUGE_ONLY, NULL,                                       "00000000",    "Filled portion; alpha 0 uses the text colour"},
{"fill_color2",        "Fill 2",            PK_COLOR, PG_GAUGE,   GAUGE_ONLY, NULL,                                       "00000000",    "Gradient end colour for the fill"},
{"fill_gradient",      "Fill gradient",     PK_ENUM,  PG_GAUGE,   GAUGE_ONLY, "none|vertical|horizontal|diagonal|diagonal_back", "none", "Direction of the fill gradient"},
{"track_color",        "Track",             PK_COLOR, PG_GAUGE,   GAUGE_ONLY, NULL,                                       "00000000",    "Unfilled portion; alpha 0 derives from the text colour"},
{"thickness",          "Thickness",         PK_FLOAT, PG_GAUGE,   GAUGE_ONLY, NULL,                                       "8",           "Bar height, or ring stroke width, in pixels"},
{"warn_above",         "Warn above",        PK_FLOAT, PG_GAUGE,   GAUGE_ONLY, NULL,                                       "0",           "Recolour the fill past this percentage; 0 never does. One per reading"},
{"warn_below",         "Warn below",        PK_FLOAT, PG_GAUGE,   GAUGE_ONLY, NULL,                                       "0",           "Recolour the fill under this percentage, for a draining battery. One per reading"},
{"warn_color",         "Warning colour",    PK_COLOR, PG_GAUGE,   GAUGE_ONLY, NULL,                                       "00000000",    "Fill colour past the threshold"},

{"week_start",         "Week starts",       PK_ENUM,  PG_CALENDAR, CALENDAR_ONLY, "locale|monday|sunday",                  "locale",      "First column of the grid"},
{"show_header",        "Show header",       PK_BOOL,  PG_CALENDAR, CALENDAR_ONLY, NULL,                                    "true",        "Draw the month and year line"},
{"header_format",      "Header pattern",    PK_TEXT,  PG_CALENDAR, CALENDAR_ONLY, NULL,                                    "MMMM yyyy",   "Date pattern for the header line"},
{"show_weekdays",      "Show weekdays",     PK_BOOL,  PG_CALENDAR, CALENDAR_ONLY, NULL,                                    "true",        "Draw the row of weekday initials"},
{"show_week_numbers",  "Week numbers",      PK_BOOL,  PG_CALENDAR, CALENDAR_ONLY, NULL,                                    "false",       "Add an ISO week number column"},
{"show_outside_days",  "Adjacent months",   PK_BOOL,  PG_CALENDAR, CALENDAR_ONLY, NULL,                                    "true",        "Fill the empty cells with neighbouring days"},
{"day_scale",          "Day size",          PK_FLOAT, PG_CALENDAR, CALENDAR_ONLY, NULL,                                    "1.0",         "Day-number size relative to the font size"},
{"header_color",       "Header colour",     PK_COLOR, PG_CALENDAR, CALENDAR_ONLY, NULL,                                    "00000000",    "Alpha 0 uses the text colour"},
{"weekday_color",      "Weekday colour",    PK_COLOR, PG_CALENDAR, CALENDAR_ONLY, NULL,                                    "00000000",    "Alpha 0 derives 50% of the text colour"},
{"weekend_color",      "Weekend colour",    PK_COLOR, PG_CALENDAR, CALENDAR_ONLY, NULL,                                    "00000000",    "Saturday and Sunday numbers"},
{"outside_color",      "Adjacent colour",   PK_COLOR, PG_CALENDAR, CALENDAR_ONLY, NULL,                                    "00000000",    "Days belonging to the months either side"},
{"today_color",        "Today marker",      PK_COLOR, PG_CALENDAR, CALENDAR_ONLY, NULL,                                    "00000000",    "Disc drawn behind today"},
{"today_text_color",   "Today number",      PK_COLOR, PG_CALENDAR, CALENDAR_ONLY, NULL,                                    "00000000",    "Today's number, drawn on the marker"},

{"path",               "File",              PK_FILE,  PG_SOURCE,  TYPE_FILE,  NULL,                                       "",            "Source file, relative to the config folder"},
{"reload_seconds",     "Reload every",      PK_INT,   PG_SOURCE,  TYPE_FILE,  NULL,                                       "0",           "Seconds between file checks; 0 loads once at startup"},
{"fit",                "Fit",               PK_ENUM,  PG_SOURCE,  TYPE_BIT(WIDGET_IMAGE), "contain|cover|stretch",        "contain",     "How the image fills its box"},
};

_Static_assert(sizeof(g_props) / sizeof(g_props[0]) <= LW_MAX_PROPERTIES,
               "the property registry outgrew LW_MAX_PROPERTIES");

const PropDef* Spec_Properties(int* count) {
    if (count) *count = (int)(sizeof(g_props) / sizeof(g_props[0]));
    return g_props;
}

const PropDef* Spec_FindProperty(const char* key) {
    if (!key) return NULL;
    int count = 0;
    const PropDef* props = Spec_Properties(&count);
    for (int i = 0; i < count; i++)
        if (_stricmp(props[i].key, key) == 0) return &props[i];
    return NULL;
}

const char* Spec_DefaultFor(const PropDef* prop, int type) {
    if (!prop) return "";
    if (_stricmp(prop->key, "click_through") == 0)
        return Spec_IsInteractive(type) ? "false" : "true";

    /* One buffer each, so asking for both in one expression still works. */
    bool wantsWidth = _stricmp(prop->key, "width") == 0;
    if (wantsWidth || _stricmp(prop->key, "height") == 0) {
        static char widthText[8], heightText[8];
        WidgetSpec probe;
        Spec_Defaults(&probe);
        probe.type = type;
        ResolveGauge(&probe.gauge, &probe.style);

        int width = 0, height = 0;
        DefaultSize(&probe, &width, &height);
        _snprintf(widthText, sizeof(widthText), "%d", width);
        _snprintf(heightText, sizeof(heightText), "%d", height);
        return wantsWidth ? widthText : heightText;
    }
    return prop->def;
}

bool Spec_PropAppliesTo(const PropDef* prop, int type) {
    if (!prop) return false;
    if (type < 0 || type >= WIDGET__COUNT) return true;
    return (prop->types & TYPE_BIT(type)) != 0;
}

const char* Spec_GroupName(int group) {
    switch (group) {
        case PG_GENERAL: return "General";
        case PG_LAYOUT:  return "Layout";
        case PG_SURFACE: return "Panel";
        case PG_TEXT:    return "Text";
        case PG_EFFECTS: return "Effects";
        case PG_CLOCK:   return "Clock";
        case PG_ANALOG:  return "Analog dial";
        case PG_GAUGE:    return "Gauge";
        case PG_CALENDAR: return "Calendar";
        case PG_SOURCE:  return "Source";
        default:         return "Other";
    }
}
