#include "style.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ─────────────────────────── colour helpers ─────────────────────────── */

static int HexNibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

ARGB Style_ParseColor(const char* text, ARGB fallback) {
    if (!text) return fallback;
    while (*text == ' ' || *text == '\t' || *text == '#') text++;

    char digits[9];
    size_t n = 0;
    for (const char* p = text; *p && n < 8; p++) {
        if (HexNibble(*p) < 0) break;
        digits[n++] = *p;
    }
    digits[n] = '\0';
    if (n != 3 && n != 4 && n != 6 && n != 8) return fallback;

    unsigned long v = strtoul(digits, NULL, 16);
    switch (n) {
        case 3: /* RGB   -> opaque */
            return 0xFF000000u
                 | ((v & 0xF00u) << 12) | ((v & 0xF00u) << 8)
                 | ((v & 0x0F0u) << 8)  | ((v & 0x0F0u) << 4)
                 | ((v & 0x00Fu) << 4)  |  (v & 0x00Fu);
        case 4: /* ARGB shorthand */
            return ((v & 0xF000u) << 16) | ((v & 0xF000u) << 12)
                 | ((v & 0x0F00u) << 12) | ((v & 0x0F00u) << 8)
                 | ((v & 0x00F0u) << 8)  | ((v & 0x00F0u) << 4)
                 | ((v & 0x000Fu) << 4)  |  (v & 0x000Fu);
        case 6: /* RRGGBB -> opaque */
            return 0xFF000000u | (ARGB)v;
        default:
            return (ARGB)v;
    }
}

void Style_FormatColor(ARGB color, char* out, size_t cap) {
    if (!out || cap == 0) return;
    _snprintf(out, cap, "%08X", (unsigned)color);
    out[cap - 1] = '\0';
}

ARGB Style_WithAlpha(ARGB color, BYTE alpha) {
    return (color & 0x00FFFFFFu) | ((ARGB)alpha << 24);
}

ARGB Style_ScaleAlpha(ARGB color, float factor) {
    float a = (float)((color >> 24) & 0xFFu) * factor;
    if (a < 0.0f) a = 0.0f;
    if (a > 255.0f) a = 255.0f;
    return Style_WithAlpha(color, (BYTE)(a + 0.5f));
}

/* ─────────────────────────── enum parsing ─────────────────────────── */

bool Style_ParseBool(const char* text, bool fallback) {
    if (!text || !text[0]) return fallback;
    if (_stricmp(text, "true") == 0 || _stricmp(text, "1") == 0 ||
        _stricmp(text, "yes")  == 0 || _stricmp(text, "on") == 0) return true;
    if (_stricmp(text, "false") == 0 || _stricmp(text, "0") == 0 ||
        _stricmp(text, "no")   == 0 || _stricmp(text, "off") == 0) return false;
    return fallback;
}

int Style_ParseFontStyle(const char* text) {
    if (!text || !text[0]) return FontStyleRegular;
    int style = FontStyleRegular;
    /* Accept combinations such as "bold italic underline". */
    if (strstr(text, "bold")      || strstr(text, "Bold"))      style |= FontStyleBold;
    if (strstr(text, "italic")    || strstr(text, "Italic"))    style |= FontStyleItalic;
    if (strstr(text, "underline") || strstr(text, "Underline")) style |= FontStyleUnderline;
    if (strstr(text, "strikeout") || strstr(text, "Strikeout")) style |= FontStyleStrikeout;
    return style;
}

const char* Style_FontStyleName(int style) {
    int base = style & (FontStyleBold | FontStyleItalic);
    if (base == FontStyleBoldItalic) return "bold_italic";
    if (base == FontStyleBold)       return "bold";
    if (base == FontStyleItalic)     return "italic";
    return "regular";
}

int Style_ParseAlign(const char* text, int fallback) {
    if (!text || !text[0]) return fallback;
    if (_stricmp(text, "left")   == 0 || _stricmp(text, "top")    == 0 ||
        _stricmp(text, "start")  == 0) return ALIGN_NEAR;
    if (_stricmp(text, "center") == 0 || _stricmp(text, "middle") == 0) return ALIGN_CENTER;
    if (_stricmp(text, "right")  == 0 || _stricmp(text, "bottom") == 0 ||
        _stricmp(text, "end")    == 0) return ALIGN_FAR;
    return fallback;
}

const char* Style_AlignName(int align) {
    switch (align) {
        case ALIGN_NEAR:   return "left";
        case ALIGN_FAR:    return "right";
        default:           return "center";
    }
}

int Style_ParseGradient(const char* text) {
    if (!text || !text[0]) return GRAD_NONE;
    if (_stricmp(text, "vertical")   == 0 || _stricmp(text, "v") == 0) return GRAD_VERTICAL;
    if (_stricmp(text, "horizontal") == 0 || _stricmp(text, "h") == 0) return GRAD_HORIZONTAL;
    if (_stricmp(text, "diagonal")   == 0 || _stricmp(text, "d") == 0) return GRAD_DIAGONAL;
    if (_stricmp(text, "diagonal_back") == 0 || _stricmp(text, "db") == 0) return GRAD_DIAGONAL_BACK;
    return GRAD_NONE;
}

const char* Style_GradientName(int dir) {
    switch (dir) {
        case GRAD_VERTICAL:      return "vertical";
        case GRAD_HORIZONTAL:    return "horizontal";
        case GRAD_DIAGONAL:      return "diagonal";
        case GRAD_DIAGONAL_BACK: return "diagonal_back";
        default:                 return "none";
    }
}

static int ParseTransform(const char* text) {
    if (!text || !text[0]) return TEXT_AS_IS;
    if (_stricmp(text, "upper") == 0 || _stricmp(text, "uppercase") == 0) return TEXT_UPPER;
    if (_stricmp(text, "lower") == 0 || _stricmp(text, "lowercase") == 0) return TEXT_LOWER;
    return TEXT_AS_IS;
}

/* ─────────────────────────── defaults ─────────────────────────── */

WidgetStyle Style_Default(void) {
    WidgetStyle s;
    memset(&s, 0, sizeof(s));

    s.bg_color       = 0xD9181825;
    s.bg_color2      = 0x00000000;
    s.bg_gradient    = GRAD_NONE;
    s.border_color   = 0x33FFFFFF;
    s.border_width   = 0.0f;
    s.corner_radius  = 12.0f;

    wcscpy(s.font_family, L"Segoe UI");
    s.font_size      = 14.0f;
    s.font_style     = FontStyleRegular;
    s.text_color     = 0xFFFFFFFF;
    s.text_color2    = 0x00000000;
    s.text_gradient  = GRAD_NONE;
    s.letter_spacing = 0.0f;
    s.line_spacing   = 1.0f;
    s.text_transform = TEXT_AS_IS;

    s.shadow_color   = 0x00000000;
    s.shadow_offset_x = 0.0f;
    s.shadow_offset_y = 2.0f;
    s.glow_color     = 0x00000000;
    s.glow_radius    = 0.0f;
    s.outline_color  = 0x00000000;
    s.outline_width  = 0.0f;

    s.align_h        = ALIGN_CENTER;
    s.align_v        = ALIGN_CENTER;
    s.padding        = 10.0f;
    return s;
}

/* ─────────────────────────── key dispatch ─────────────────────────── */

#define KEY(k) (_stricmp(key, k) == 0)

bool Style_Set(WidgetStyle* s, const char* key, const char* value) {
    if (!s || !key || !value) return false;

    /* Surface */
    if (KEY("bg_color"))      { s->bg_color     = Style_ParseColor(value, s->bg_color);     return true; }
    if (KEY("bg_color2"))     { s->bg_color2    = Style_ParseColor(value, s->bg_color2);    return true; }
    if (KEY("bg_gradient"))   { s->bg_gradient  = Style_ParseGradient(value);               return true; }
    if (KEY("border_color"))  { s->border_color = Style_ParseColor(value, s->border_color); return true; }
    if (KEY("border_width"))  { s->border_width = (float)atof(value);                       return true; }
    if (KEY("corner_radius")) { s->corner_radius = (float)atof(value);                      return true; }

    /* Text */
    if (KEY("font_family")) {
        MultiByteToWideChar(CP_UTF8, 0, value, -1, s->font_family, LW_FONT_LEN);
        s->font_family[LW_FONT_LEN - 1] = L'\0';
        return true;
    }
    if (KEY("font_size"))      { s->font_size      = (float)atof(value);              return true; }
    if (KEY("font_style"))     { s->font_style     = Style_ParseFontStyle(value);     return true; }
    if (KEY("text_color"))     { s->text_color     = Style_ParseColor(value, s->text_color);  return true; }
    if (KEY("text_color2"))    { s->text_color2    = Style_ParseColor(value, s->text_color2); return true; }
    if (KEY("text_gradient"))  { s->text_gradient  = Style_ParseGradient(value);      return true; }
    if (KEY("letter_spacing")) { s->letter_spacing = (float)atof(value);              return true; }
    if (KEY("line_spacing"))   { s->line_spacing   = (float)atof(value);              return true; }
    if (KEY("text_transform")) { s->text_transform = ParseTransform(value);           return true; }

    /* Effects */
    if (KEY("shadow_color"))    { s->shadow_color    = Style_ParseColor(value, s->shadow_color); return true; }
    if (KEY("shadow_offset_x")) { s->shadow_offset_x = (float)atof(value);                       return true; }
    if (KEY("shadow_offset_y")) { s->shadow_offset_y = (float)atof(value);                       return true; }
    if (KEY("shadow_offset"))   { s->shadow_offset_x = s->shadow_offset_y = (float)atof(value);  return true; }
    if (KEY("glow_color"))      { s->glow_color      = Style_ParseColor(value, s->glow_color);   return true; }
    if (KEY("glow_radius"))     { s->glow_radius     = (float)atof(value);                       return true; }
    if (KEY("outline_color"))   { s->outline_color   = Style_ParseColor(value, s->outline_color);return true; }
    if (KEY("outline_width"))   { s->outline_width   = (float)atof(value);                       return true; }

    /* Layout */
    if (KEY("align_h")) { s->align_h = Style_ParseAlign(value, s->align_h); return true; }
    if (KEY("align_v")) { s->align_v = Style_ParseAlign(value, s->align_v); return true; }
    if (KEY("padding")) { s->padding = (float)atof(value);                  return true; }

    return false;
}

#undef KEY

/* ─────────────────────────── presets ─────────────────────────── */

static const StyleKV kv_midnight[] = {
    {"bg_color","D9181825"}, {"bg_color2","D9232335"}, {"bg_gradient","vertical"},
    {"text_color","FFFFFFFF"}, {"border_color","33FFFFFF"}, {"border_width","1"},
    {"corner_radius","16"}, {"font_family","Segoe UI"}, {NULL,NULL}
};
static const StyleKV kv_nord[] = {
    {"bg_color","E62E3440"}, {"bg_color2","E63B4252"}, {"bg_gradient","vertical"},
    {"text_color","FFECEFF4"}, {"border_color","4488C0D0"}, {"border_width","1"},
    {"corner_radius","12"}, {"font_family","Segoe UI"}, {NULL,NULL}
};
static const StyleKV kv_mocha[] = {
    {"bg_color","E61E1E2E"}, {"bg_color2","E6302D41"}, {"bg_gradient","diagonal"},
    {"text_color","FFCDD6F4"}, {"text_color2","FFF5C2E7"}, {"text_gradient","horizontal"},
    {"border_color","44CBA6F7"}, {"border_width","1"}, {"corner_radius","16"},
    {"font_family","Segoe UI"}, {NULL,NULL}
};
static const StyleKV kv_dracula[] = {
    {"bg_color","E6282A36"}, {"bg_color2","E6343746"}, {"bg_gradient","vertical"},
    {"text_color","FFF8F8F2"}, {"border_color","55BD93F9"}, {"border_width","1"},
    {"corner_radius","12"}, {"font_family","Segoe UI"}, {NULL,NULL}
};
static const StyleKV kv_gruvbox[] = {
    {"bg_color","E6282828"}, {"bg_color2","E63C3836"}, {"bg_gradient","vertical"},
    {"text_color","FFEBDBB2"}, {"border_color","55FE8019"}, {"border_width","1"},
    {"corner_radius","8"}, {"font_family","Segoe UI"}, {NULL,NULL}
};
static const StyleKV kv_solarized[] = {
    {"bg_color","E6002B36"}, {"bg_color2","E6073642"}, {"bg_gradient","vertical"},
    {"text_color","FF93A1A1"}, {"border_color","55268BD2"}, {"border_width","1"},
    {"corner_radius","8"}, {"font_family","Segoe UI"}, {NULL,NULL}
};
static const StyleKV kv_terminal[] = {
    {"bg_color","E6000000"}, {"bg_gradient","none"}, {"text_color","FF33FF66"},
    {"glow_color","5533FF66"}, {"glow_radius","6"}, {"border_color","4433FF66"},
    {"border_width","1"}, {"corner_radius","4"}, {"font_family","Consolas"},
    {"letter_spacing","1"}, {NULL,NULL}
};
static const StyleKV kv_glass[] = {
    {"bg_color","2EFFFFFF"}, {"bg_color2","14FFFFFF"}, {"bg_gradient","diagonal"},
    {"text_color","FFFFFFFF"}, {"border_color","4DFFFFFF"}, {"border_width","1"},
    {"corner_radius","20"}, {"shadow_color","66000000"}, {"shadow_offset_y","2"},
    {"font_family","Segoe UI"}, {NULL,NULL}
};
static const StyleKV kv_minimal[] = {
    {"bg_color","00000000"}, {"bg_gradient","none"}, {"border_width","0"},
    {"corner_radius","0"}, {"text_color","FFFFFFFF"}, {"shadow_color","B3000000"},
    {"shadow_offset_x","0"}, {"shadow_offset_y","2"}, {"font_family","Segoe UI"},
    {NULL,NULL}
};
static const StyleKV kv_neon[] = {
    {"bg_color","CC05050A"}, {"bg_color2","CC0B0B18"}, {"bg_gradient","vertical"},
    {"text_color","FF00E5FF"}, {"glow_color","6600E5FF"}, {"glow_radius","10"},
    {"border_color","5500E5FF"}, {"border_width","1"}, {"corner_radius","14"},
    {"letter_spacing","2"}, {"font_family","Segoe UI"}, {NULL,NULL}
};
static const StyleKV kv_sunset[] = {
    {"bg_color","E62B1B3D"}, {"bg_color2","E6141026"}, {"bg_gradient","diagonal"},
    {"text_color","FFFFB86C"}, {"text_color2","FFFF6E9C"}, {"text_gradient","horizontal"},
    {"border_color","44FF6E9C"}, {"border_width","1"}, {"corner_radius","18"},
    {"font_family","Segoe UI"}, {NULL,NULL}
};
static const StyleKV kv_aurora[] = {
    {"bg_color","E60B1B2B"}, {"bg_color2","E60F3A3A"}, {"bg_gradient","diagonal"},
    {"text_color","FF7DF9C6"}, {"text_color2","FF66C7F4"}, {"text_gradient","horizontal"},
    {"border_color","4466C7F4"}, {"border_width","1"}, {"corner_radius","18"},
    {"font_family","Segoe UI"}, {NULL,NULL}
};
static const StyleKV kv_paper[] = {
    {"bg_color","F2F5F1E8"}, {"bg_color2","F2EDE7D9"}, {"bg_gradient","vertical"},
    {"text_color","FF2B2B2B"}, {"border_color","33000000"}, {"border_width","1"},
    {"corner_radius","10"}, {"font_family","Georgia"}, {NULL,NULL}
};
static const StyleKV kv_carbon[] = {
    {"bg_color","F0161616"}, {"bg_gradient","none"}, {"text_color","FFF4F4F4"},
    {"border_color","33FFFFFF"}, {"border_width","1"}, {"corner_radius","6"},
    {"font_family","Segoe UI"}, {NULL,NULL}
};
static const StyleKV kv_blueprint[] = {
    {"bg_color","E60A1F44"}, {"bg_color2","E6103058"}, {"bg_gradient","vertical"},
    {"text_color","FFB3D4FF"}, {"border_color","6666A3FF"}, {"border_width","1"},
    {"corner_radius","4"}, {"font_family","Consolas"}, {"letter_spacing","1"},
    {NULL,NULL}
};

static const StylePreset g_presets[] = {
    {"midnight",  "Deep navy card, soft white text",         kv_midnight},
    {"glass",     "Frosted translucent panel",               kv_glass},
    {"minimal",   "No panel - text with a drop shadow",      kv_minimal},
    {"neon",      "Black panel, glowing cyan text",          kv_neon},
    {"sunset",    "Purple panel, orange to pink gradient",   kv_sunset},
    {"aurora",    "Teal panel, mint to sky gradient",        kv_aurora},
    {"terminal",  "Phosphor green on black, monospaced",     kv_terminal},
    {"blueprint", "Drafting blue with monospaced text",      kv_blueprint},
    {"carbon",    "Flat near-black with a hairline border",  kv_carbon},
    {"paper",     "Light theme, warm off-white",             kv_paper},
    {"nord",      "Nord palette",                            kv_nord},
    {"mocha",     "Catppuccin Mocha palette",                kv_mocha},
    {"dracula",   "Dracula palette",                         kv_dracula},
    {"gruvbox",   "Gruvbox Dark palette",                    kv_gruvbox},
    {"solarized", "Solarized Dark palette",                  kv_solarized},
};

const StylePreset* Style_Presets(int* count) {
    if (count) *count = (int)(sizeof(g_presets) / sizeof(g_presets[0]));
    return g_presets;
}

bool Style_ApplyPreset(WidgetStyle* s, const char* name) {
    if (!s || !name || !name[0]) return false;
    int count = 0;
    const StylePreset* presets = Style_Presets(&count);
    for (int i = 0; i < count; i++) {
        if (_stricmp(presets[i].name, name) != 0) continue;
        for (const StyleKV* kv = presets[i].pairs; kv->key; kv++)
            Style_Set(s, kv->key, kv->value);
        return true;
    }
    return false;
}
