#include "config.h"
#include "widgets/clock.h"
#include "widgets/image.h"
#include "widgets/notes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

DWORD Config_ParseColor(const char* hexStr) {
    if (!hexStr || !hexStr[0]) return 0xFF000000;
    /* Skip leading # if present */
    if (hexStr[0] == '#') hexStr++;
    size_t len = strlen(hexStr);
    DWORD val = strtoul(hexStr, NULL, 16);
    /* If 6 chars (RRGGBB), assume fully opaque */
    if (len <= 6) {
        val |= 0xFF000000;
    }
    return val;
}

static INT ParseFontStyle(const char* str) {
    if (!str || !str[0]) return FontStyleRegular;
    if (_stricmp(str, "bold") == 0) return FontStyleBold;
    if (_stricmp(str, "italic") == 0) return FontStyleItalic;
    if (_stricmp(str, "bold_italic") == 0) return FontStyleBoldItalic;
    return FontStyleRegular;
}

static int ParseAlignment(const char* str) {
    if (!str || !str[0]) return -1; /* Use default */
    if (_stricmp(str, "left") == 0 || _stricmp(str, "top") == 0) return 0;
    if (_stricmp(str, "center") == 0) return 1;
    if (_stricmp(str, "right") == 0 || _stricmp(str, "bottom") == 0) return 2;
    return -1;
}

void Config_ParseStyle(const char* iniPath, const char* section, WidgetStyle* out) {
    *out = Style_Default();
    char buf[128];

    /* Background */
    GetPrivateProfileStringA(section, "bg_color", "", buf, sizeof(buf), iniPath);
    if (buf[0]) out->bg_color = Config_ParseColor(buf);

    /* Text */
    GetPrivateProfileStringA(section, "text_color", "", buf, sizeof(buf), iniPath);
    if (buf[0]) out->text_color = Config_ParseColor(buf);

    /* Border */
    GetPrivateProfileStringA(section, "border_color", "", buf, sizeof(buf), iniPath);
    if (buf[0]) out->border_color = Config_ParseColor(buf);

    GetPrivateProfileStringA(section, "border_width", "", buf, sizeof(buf), iniPath);
    if (buf[0]) out->border_width = (float)atof(buf);

    /* Corner radius */
    GetPrivateProfileStringA(section, "corner_radius", "", buf, sizeof(buf), iniPath);
    if (buf[0]) out->corner_radius = (float)atof(buf);

    /* Font */
    GetPrivateProfileStringA(section, "font_family", "", buf, sizeof(buf), iniPath);
    if (buf[0]) MultiByteToWideChar(CP_UTF8, 0, buf, -1, out->font_family, 64);

    GetPrivateProfileStringA(section, "font_size", "", buf, sizeof(buf), iniPath);
    if (buf[0]) out->font_size = (float)atof(buf);

    GetPrivateProfileStringA(section, "font_style", "", buf, sizeof(buf), iniPath);
    if (buf[0]) out->font_style = ParseFontStyle(buf);

    /* Alignment */
    GetPrivateProfileStringA(section, "align_h", "", buf, sizeof(buf), iniPath);
    int ah = ParseAlignment(buf);
    if (ah >= 0) out->align_h = ah;

    GetPrivateProfileStringA(section, "align_v", "", buf, sizeof(buf), iniPath);
    int av = ParseAlignment(buf);
    if (av >= 0) out->align_v = av;

    /* Padding */
    GetPrivateProfileStringA(section, "padding", "", buf, sizeof(buf), iniPath);
    if (buf[0]) out->padding = (float)atof(buf);
}

static void CreateWidgetFromSection(const char* iniPath, const char* section, HINSTANCE hInstance) {
    char type[32] = {0};
    GetPrivateProfileStringA(section, "type", "", type, sizeof(type), iniPath);
    if (!type[0]) return;

    int x      = GetPrivateProfileIntA(section, "x", 0, iniPath);
    int y      = GetPrivateProfileIntA(section, "y", 0, iniPath);
    int width  = GetPrivateProfileIntA(section, "width", 200, iniPath);
    int height = GetPrivateProfileIntA(section, "height", 100, iniPath);

    char clickStr[16] = {0};
    GetPrivateProfileStringA(section, "click_through", "true", clickStr, sizeof(clickStr), iniPath);
    bool click_through = (_stricmp(clickStr, "true") == 0 || _stricmp(clickStr, "1") == 0);

    /* Parse shared style */
    WidgetStyle style;
    Config_ParseStyle(iniPath, section, &style);

    if (_stricmp(type, "clock") == 0) {
        char format[16] = {0};
        GetPrivateProfileStringA(section, "format", "12h", format, sizeof(format), iniPath);

        char secStr[16] = {0};
        GetPrivateProfileStringA(section, "show_seconds", "false", secStr, sizeof(secStr), iniPath);
        bool show_seconds = (_stricmp(secStr, "true") == 0 || _stricmp(secStr, "1") == 0);

        char dateStr[16] = {0};
        GetPrivateProfileStringA(section, "show_date", "true", dateStr, sizeof(dateStr), iniPath);
        bool show_date = (_stricmp(dateStr, "true") == 0 || _stricmp(dateStr, "1") == 0);

        /* Date-specific styling (falls back to main style if not set) */
        char buf[128];
        float date_font_size = style.font_size * 0.4f; /* Default: 40% of time font */
        GetPrivateProfileStringA(section, "date_font_size", "", buf, sizeof(buf), iniPath);
        if (buf[0]) date_font_size = (float)atof(buf);

        INT date_font_style = FontStyleRegular;
        GetPrivateProfileStringA(section, "date_font_style", "", buf, sizeof(buf), iniPath);
        if (buf[0]) date_font_style = ParseFontStyle(buf);

        ARGB date_color = (style.text_color & 0x00FFFFFF) | 0x99000000; /* Default: 60% opacity of text */
        GetPrivateProfileStringA(section, "date_color", "", buf, sizeof(buf), iniPath);
        if (buf[0]) date_color = Config_ParseColor(buf);

        ClockWidget_Create(hInstance, x, y, width, height, click_through,
                           format, show_seconds, show_date, &style,
                           date_font_size, date_font_style, date_color);
    }
    else if (_stricmp(type, "image") == 0) {
        char pathA[MAX_PATH] = {0};
        GetPrivateProfileStringA(section, "path", "", pathA, sizeof(pathA), iniPath);
        ImageWidget_Create(hInstance, iniPath, x, y, width, height, click_through, pathA);
    }
    else if (_stricmp(type, "notes") == 0) {
        char pathA[MAX_PATH] = {0};
        GetPrivateProfileStringA(section, "path", "", pathA, sizeof(pathA), iniPath);
        NotesWidget_Create(hInstance, iniPath, x, y, width, height, click_through, pathA, &style);
    }
}

void Config_Load(const char* iniPath, HINSTANCE hInstance) {
    char sectionNames[4096] = {0};
    DWORD chars = GetPrivateProfileSectionNamesA(sectionNames, sizeof(sectionNames), iniPath);

    if (chars > 0) {
        char* p = sectionNames;
        while (*p != '\0') {
            CreateWidgetFromSection(iniPath, p, hInstance);
            p += strlen(p) + 1;
        }
    }
}
