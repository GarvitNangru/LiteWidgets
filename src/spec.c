#include "spec.h"
#include "layout.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ─────────────────────────── widget types ─────────────────────────── */

static const char* kTypeNames[WIDGET__COUNT] = { "clock", "notes", "image" };

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
}

/* ─────────────────────────── key dispatch ─────────────────────────── */

static int ParseTransformValue(const char* text) {
    if (!text || !text[0]) return TEXT_AS_IS;
    if (_stricmp(text, "upper") == 0 || _stricmp(text, "uppercase") == 0) return TEXT_UPPER;
    if (_stricmp(text, "lower") == 0 || _stricmp(text, "lowercase") == 0) return TEXT_LOWER;
    return TEXT_AS_IS;
}

#define KEY(k) (_stricmp(key, k) == 0)

bool Spec_Set(WidgetSpec* spec, const char* key, const char* value) {
    if (!spec || !key || !value) return false;
    ClockOptions* c = &spec->clock;

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
    if (KEY("width"))   { spec->width  = atoi(value); return true; }
    if (KEY("height"))  { spec->height = atoi(value); return true; }
    if (KEY("anchor"))  { spec->anchor = Layout_ParseAnchor(value); return true; }
    if (KEY("monitor")) { spec->monitor = atoi(value); return true; }
    if (KEY("click_through")) { spec->click_through = Style_ParseBool(value, true); return true; }
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

    /* ── everything visual ── */
    return Style_Set(&spec->style, key, value);
}

#undef KEY

/* ─────────────────────────── derivation ─────────────────────────── */

void Spec_Finalize(WidgetSpec* spec) {
    if (!spec) return;
    ClockOptions* c = &spec->clock;
    const WidgetStyle* s = &spec->style;

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
}

/* ─────────────────────────── property registry ─────────────────────────── */

#define CLOCK_ONLY TYPE_BIT(WIDGET_CLOCK)
#define TEXTUAL    (TYPE_BIT(WIDGET_CLOCK) | TYPE_BIT(WIDGET_NOTES))

static const PropDef g_props[] = {
/*  key                    label               kind      group       types       options                                     default        help */
{"type",               "Type",              PK_ENUM,  PG_GENERAL, TYPE_ANY,   "clock|notes|image",                        "clock",       "Which widget to render"},
{"enabled",            "Enabled",           PK_BOOL,  PG_GENERAL, TYPE_ANY,   NULL,                                       "true",        "Set to false to keep the config but hide the widget"},
{"preset",             "Style preset",      PK_ENUM,  PG_GENERAL, TYPE_ANY,   "@presets",                                 "",            "Applies a themed set of colours; individual keys still win"},

{"anchor",             "Anchor",            PK_ENUM,  PG_LAYOUT,  TYPE_ANY,   "top_left|top_center|top_right|left|center|right|bottom_left|bottom_center|bottom_right", "top_left", "Corner or edge that X/Y are measured from"},
{"monitor",            "Monitor",           PK_INT,   PG_LAYOUT,  TYPE_ANY,   NULL,                                       "-1",          "Monitor index, or -1 for the primary display"},
{"x",                  "X offset",          PK_INT,   PG_LAYOUT,  TYPE_ANY,   NULL,                                       "0",           "Horizontal offset from the anchor, in pixels"},
{"y",                  "Y offset",          PK_INT,   PG_LAYOUT,  TYPE_ANY,   NULL,                                       "0",           "Vertical offset from the anchor, in pixels"},
{"width",              "Width",             PK_INT,   PG_LAYOUT,  TYPE_ANY,   NULL,                                       "320",         "Widget width in pixels"},
{"height",             "Height",            PK_INT,   PG_LAYOUT,  TYPE_ANY,   NULL,                                       "140",         "Widget height in pixels"},
{"opacity",            "Opacity",           PK_FLOAT, PG_LAYOUT,  TYPE_ANY,   NULL,                                       "1.0",         "Master transparency, 0.0 to 1.0"},
{"click_through",      "Click-through",     PK_BOOL,  PG_LAYOUT,  TYPE_ANY,   NULL,                                       "true",        "Let clicks pass to the desktop underneath"},

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

{"path",               "File",              PK_FILE,  PG_SOURCE,  TYPE_FILE,  NULL,                                       "",            "Source file, relative to the config folder"},
{"reload_seconds",     "Reload every",      PK_INT,   PG_SOURCE,  TYPE_FILE,  NULL,                                       "0",           "Seconds between file checks; 0 loads once at startup"},
{"fit",                "Fit",               PK_ENUM,  PG_SOURCE,  TYPE_BIT(WIDGET_IMAGE), "contain|cover|stretch",        "contain",     "How the image fills its box"},
};

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
        case PG_SOURCE:  return "Source";
        default:         return "Other";
    }
}
