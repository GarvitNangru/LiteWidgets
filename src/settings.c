#include "settings.h"

#include "config.h"
#include "drawing.h"
#include "resource.h"
#include "spec.h"
#include "style.h"
#include "widget.h"

#include <commctrl.h>
#include <commdlg.h>
#include <windowsx.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * The settings editor.
 *
 * Every control here is drawn by hand. That is not decoration: the common
 * controls are themed by the system in a style that has not changed since
 * Windows 7, and mixing them with a preview of a widget that has gradients,
 * glow and rounded corners looked like two different programs stapled
 * together. Owner drawing is also what makes a colour swatch, an alpha
 * slider and a font list that shows each face in its own typeface possible
 * at all.
 *
 * The layout has exactly one rule worth knowing: property rows live inside a
 * scrolling pane of their own rather than floating over the window. Sibling
 * controls that overlap a parent-drawn surface get painted over whenever
 * that surface repaints, which is why options used to vanish until something
 * forced a redraw.
 */

/* ─────────────────────────── constants ─────────────────────────── */

#define MAX_PROPS      96
#define VALUE_LEN      160
#define MAX_FONTS      512

#define IDC_LIST       100
#define IDC_ADD        102
#define IDC_DUPLICATE  103
#define IDC_REMOVE     104
#define IDC_APPLY      105
#define IDC_CLOSE      106
#define IDC_ARRANGE    107
#define IDC_NAME       108
#define IDC_PREVIEW    109
#define IDC_PANE       110

#define PAGE_ID_BASE   700              /* one id per category pill */

/* Every property owns four consecutive ids: control, swatch, slider, browse. */
#define PROP_ID_BASE   1000
#define PROP_ID(i, s)  (PROP_ID_BASE + (i) * 4 + (s))
#define PROP_INDEX(id) (((id) - PROP_ID_BASE) / 4)
#define PROP_SUB(id)   (((id) - PROP_ID_BASE) % 4)

enum { SUB_CTRL = 0, SUB_SWATCH, SUB_SLIDER, SUB_BROWSE };

#define IDT_PREVIEW    1

/* ─────────────────────────── theme ─────────────────────────── */

typedef struct {
    COLORREF window;      /* the window ground */
    COLORREF surface;     /* cards: sidebar, pane, preview */
    COLORREF field;       /* editable areas */
    COLORREF field_hot;
    COLORREF line;
    COLORREF text;
    COLORREF dim;
    COLORREF accent;
    COLORREF on_accent;
    bool     dark;
} Theme;

static Theme g_theme;

static bool SystemPrefersDark(void) {
    HKEY key;
    if (RegOpenKeyExA(HKEY_CURRENT_USER,
                      "Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                      0, KEY_READ, &key) != ERROR_SUCCESS)
        return true;

    DWORD light = 1, size = sizeof(light), type = 0;
    LONG status = RegQueryValueExA(key, "AppsUseLightTheme", NULL, &type,
                                   (BYTE*)&light, &size);
    RegCloseKey(key);
    return (status == ERROR_SUCCESS && type == REG_DWORD) ? (light == 0) : true;
}

static void LoadTheme(void) {
    if (SystemPrefersDark()) {
        g_theme.dark      = true;
        g_theme.window    = RGB(0x1B, 0x1B, 0x1F);
        g_theme.surface   = RGB(0x24, 0x24, 0x2A);
        g_theme.field     = RGB(0x2E, 0x2E, 0x36);
        g_theme.field_hot = RGB(0x38, 0x38, 0x42);
        g_theme.line      = RGB(0x38, 0x38, 0x42);
        g_theme.text      = RGB(0xEA, 0xEA, 0xEF);
        g_theme.dim       = RGB(0x97, 0x97, 0xA4);
        g_theme.accent    = RGB(0x4C, 0xC2, 0xFF);
        g_theme.on_accent = RGB(0x06, 0x28, 0x3A);
    } else {
        g_theme.dark      = false;
        g_theme.window    = RGB(0xF2, 0xF2, 0xF6);
        g_theme.surface   = RGB(0xFF, 0xFF, 0xFF);
        g_theme.field     = RGB(0xEE, 0xEE, 0xF3);
        g_theme.field_hot = RGB(0xE3, 0xE3, 0xEB);
        g_theme.line      = RGB(0xDA, 0xDA, 0xE2);
        g_theme.text      = RGB(0x1B, 0x1B, 0x1F);
        g_theme.dim       = RGB(0x60, 0x60, 0x6C);
        g_theme.accent    = RGB(0x0A, 0x7A, 0xCC);
        g_theme.on_accent = RGB(0xFF, 0xFF, 0xFF);
    }
}

/* ─────────────────────────── state ─────────────────────────── */

typedef struct {
    char section[LW_SECTION_LEN];
    char values[MAX_PROPS][VALUE_LEN];
} Entry;

typedef struct {
    HWND ctrl;
    HWND label;
    HWND swatch;
    HWND slider;
    HWND browse;
} PropControls;

static HINSTANCE g_instance = NULL;
static HWND      g_window   = NULL;
static HWND      g_list     = NULL;
static HWND      g_pane     = NULL;
static HWND      g_name     = NULL;
static HWND      g_preview  = NULL;
static HWND      g_arrange  = NULL;
static HWND      g_tips     = NULL;

static HFONT g_font     = NULL;
static HFONT g_fontBold = NULL;
static HFONT g_fontSmall = NULL;

static HBRUSH g_brushWindow  = NULL;
static HBRUSH g_brushSurface = NULL;
static HBRUSH g_brushField   = NULL;

static char  g_iniPath[MAX_PATH];
static Entry g_entries[LW_MAX_WIDGETS];
static int   g_count = 0;
static int   g_selected = -1;
static bool  g_suppressSync = false;
static bool  g_dirty = false;

static const PropDef* g_props = NULL;
static int            g_propCount = 0;
static PropControls   g_controls[MAX_PROPS];

/* Category pills currently on screen, as group ids. */
static int  g_pages[PG__COUNT];
static RECT g_pageRects[PG__COUNT];
static int  g_pageCount = 0;
static int  g_activePage = 0;
static int  g_hotPage = -1;
static int  g_pillRows = 1;

static int g_dpi = 96;
static int g_scrollY = 0;
static int g_paneHeight = 0;
static int g_thumbGrab = 0;

static COLORREF g_customColors[16];

/* Installed font families, gathered once and drawn in their own face. */
static char g_fonts[MAX_FONTS][LF_FACESIZE];
static int  g_fontCount = 0;

/* ─────────────────────────── small helpers ─────────────────────────── */

static int Scale(int value) { return MulDiv(value, g_dpi, 96); }

static void DetectDpi(HWND hWnd) {
    typedef UINT (WINAPI *GetDpiForWindowFn)(HWND);
    HMODULE user32 = GetModuleHandleA("user32.dll");
    GetDpiForWindowFn fn = user32
        ? (GetDpiForWindowFn)GetProcAddress(user32, "GetDpiForWindow") : NULL;

    if (fn && hWnd) {
        UINT dpi = fn(hWnd);
        if (dpi >= 72 && dpi <= 600) { g_dpi = (int)dpi; return; }
    }
    HDC hdc = GetDC(NULL);
    if (hdc) {
        g_dpi = GetDeviceCaps(hdc, LOGPIXELSX);
        ReleaseDC(NULL, hdc);
    }
    if (g_dpi < 72) g_dpi = 96;
}

static HFONT MakeFont(int points, int weight) {
    return CreateFontA(-MulDiv(points, g_dpi, 72), 0, 0, 0, weight, 0, 0, 0,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH, "Segoe UI");
}

/* Windows 10 1809 and later will paint the title bar to match. */
static void ApplyDarkTitleBar(HWND hWnd) {
    typedef HRESULT (WINAPI *SetAttrFn)(HWND, DWORD, LPCVOID, DWORD);
    HMODULE dwm = LoadLibraryA("dwmapi.dll");
    if (!dwm) return;

    SetAttrFn setAttr = (SetAttrFn)GetProcAddress(dwm, "DwmSetWindowAttribute");
    if (setAttr) {
        BOOL dark = g_theme.dark ? TRUE : FALSE;
        if (setAttr(hWnd, 20, &dark, sizeof(dark)) != S_OK)
            setAttr(hWnd, 19, &dark, sizeof(dark));   /* pre-20H1 spelling */
    }
    FreeLibrary(dwm);
}

static HWND Make(const char* cls, const char* text, DWORD style, DWORD exStyle,
                 HWND parent, int id) {
    HWND hWnd = CreateWindowExA(exStyle, cls, text, style | WS_CHILD | WS_VISIBLE,
                                0, 0, 10, 10, parent, (HMENU)(INT_PTR)id, g_instance, NULL);
    if (hWnd) SendMessageA(hWnd, WM_SETFONT, (WPARAM)g_font, TRUE);
    return hWnd;
}

static void Place(HWND hWnd, int x, int y, int w, int h) {
    if (hWnd) SetWindowPos(hWnd, NULL, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
}

static void ShowRow(const PropControls* row, bool visible) {
    int cmd = visible ? SW_SHOW : SW_HIDE;
    if (row->label)  ShowWindow(row->label,  cmd);
    if (row->ctrl)   ShowWindow(row->ctrl,   cmd);
    if (row->swatch) ShowWindow(row->swatch, cmd);
    if (row->slider) ShowWindow(row->slider, cmd);
    if (row->browse) ShowWindow(row->browse, cmd);
}

static int FindProp(const char* key) {
    for (int i = 0; i < g_propCount; i++)
        if (_stricmp(g_props[i].key, key) == 0) return i;
    return -1;
}

/* ─────────────────────────── painting primitives ─────────────────── */

static ARGB ToArgb(COLORREF c, BYTE alpha) {
    return ((ARGB)alpha << 24) | ((ARGB)GetRValue(c) << 16)
         | ((ARGB)GetGValue(c) << 8) | (ARGB)GetBValue(c);
}

/*
 * Rounded rectangles go through GDI+ purely for the antialiasing; GDI's
 * RoundRect leaves stair-stepped corners that undo the point of the shape.
 */
static void FillRounded(HDC hdc, const RECT* rc, int radius, COLORREF fill,
                        COLORREF border, bool hasBorder) {
    GpGraphics* gfx = NULL;
    if (GdipCreateFromHDC(hdc, &gfx) != 0 || !gfx) return;
    GdipSetSmoothingMode(gfx, SmoothingModeAntiAlias);

    float x = (float)rc->left, y = (float)rc->top;
    float w = (float)(rc->right - rc->left), h = (float)(rc->bottom - rc->top);
    GpPath* path = Drawing_RoundedPath(x + 0.5f, y + 0.5f, w - 1.0f, h - 1.0f, (float)radius);
    if (path) {
        GpSolidFill* brush = NULL;
        if (GdipCreateSolidFill(ToArgb(fill, 255), &brush) == 0) {
            GdipFillPath(gfx, (GpBrush*)brush, path);
            GdipDeleteBrush((GpBrush*)brush);
        }
        if (hasBorder) {
            GpPen* pen = NULL;
            if (GdipCreatePen1(ToArgb(border, 255), 1.0f, UnitPixel, &pen) == 0) {
                GdipDrawPath(gfx, pen, path);
                GdipDeletePen(pen);
            }
        }
        GdipDeletePath(path);
    }
    GdipDeleteGraphics(gfx);
}

static void DrawLabel(HDC hdc, const RECT* rc, const char* text, COLORREF color,
                      UINT format, HFONT font) {
    HFONT old = (HFONT)SelectObject(hdc, font ? font : g_font);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, color);
    RECT box = *rc;
    DrawTextA(hdc, text, -1, &box, format);
    SelectObject(hdc, old);
}

/* ─────────────────────────── fonts on this machine ───────────────── */

static int CALLBACK CollectFont(const LOGFONTA* lf, const TEXTMETRICA* tm,
                                DWORD type, LPARAM param) {
    (void)tm; (void)type; (void)param;
    if (g_fontCount >= MAX_FONTS) return 0;
    if (lf->lfFaceName[0] == '@') return 1;    /* vertical writing variants */

    for (int i = 0; i < g_fontCount; i++)
        if (_stricmp(g_fonts[i], lf->lfFaceName) == 0) return 1;

    strncpy(g_fonts[g_fontCount], lf->lfFaceName, LF_FACESIZE - 1);
    g_fonts[g_fontCount][LF_FACESIZE - 1] = '\0';
    g_fontCount++;
    return 1;
}

static int CompareNames(const void* a, const void* b) {
    return _stricmp((const char*)a, (const char*)b);
}

static void LoadInstalledFonts(void) {
    if (g_fontCount > 0) return;

    HDC hdc = GetDC(NULL);
    if (!hdc) return;

    LOGFONTA lf;
    memset(&lf, 0, sizeof(lf));
    lf.lfCharSet = DEFAULT_CHARSET;
    EnumFontFamiliesExA(hdc, &lf, CollectFont, 0, 0);
    ReleaseDC(NULL, hdc);

    qsort(g_fonts, (size_t)g_fontCount, LF_FACESIZE, CompareNames);
}

/* ─────────────────────────── INI <-> entries ─────────────────────────── */

static void LoadEntries(void) {
    g_count = 0;

    char names[8192];
    memset(names, 0, sizeof(names));
    GetPrivateProfileSectionNamesA(names, sizeof(names), g_iniPath);

    for (char* p = names; *p && g_count < LW_MAX_WIDGETS; p += strlen(p) + 1) {
        char type[32] = { 0 };
        GetPrivateProfileStringA(p, "type", "", type, sizeof(type), g_iniPath);
        if (!type[0]) continue;

        Entry* entry = &g_entries[g_count];
        memset(entry, 0, sizeof(*entry));
        strncpy(entry->section, p, LW_SECTION_LEN - 1);

        for (int i = 0; i < g_propCount; i++)
            GetPrivateProfileStringA(p, g_props[i].key, "", entry->values[i],
                                     VALUE_LEN, g_iniPath);
        g_count++;
    }
    g_dirty = false;
}

/*
 * A field with no explicit value in the INI still has an effective value: the
 * one its preset supplies, or the built-in default for its widget type.
 * Showing that -- rather than the bare default -- is what makes the preset
 * visible in the editor, and comparing against it on save is what keeps the
 * INI free of redundant keys.
 */
static const char* PresetValueFor(const char* preset, const char* key) {
    if (!preset || !preset[0]) return NULL;

    int count = 0;
    const StylePreset* presets = Style_Presets(&count);
    for (int i = 0; i < count; i++) {
        if (_stricmp(presets[i].name, preset) != 0) continue;
        for (const StyleKV* kv = presets[i].pairs; kv->key; kv++)
            if (_stricmp(kv->key, key) == 0) return kv->value;
        return NULL;
    }
    return NULL;
}

static int EntryType(const Entry* entry) {
    int index = FindProp("type");
    if (index < 0) return WIDGET_CLOCK;
    int type = Spec_ParseType(entry->values[index]);
    return type < 0 ? WIDGET_CLOCK : type;
}

static int SelectedType(void) {
    if (g_selected < 0 || g_selected >= g_count) return WIDGET_CLOCK;
    return EntryType(&g_entries[g_selected]);
}

static const char* EffectiveDefault(const char* preset, int index) {
    const char* value = PresetValueFor(preset, g_props[index].key);
    return value ? value : Spec_DefaultFor(&g_props[index], SelectedType());
}

static void CurrentPresetName(const Entry* entry, char* out, int cap);

static void SaveEntries(void) {
    /* Drop every existing widget section so removals actually take effect. */
    char names[8192];
    memset(names, 0, sizeof(names));
    GetPrivateProfileSectionNamesA(names, sizeof(names), g_iniPath);
    for (char* p = names; *p; p += strlen(p) + 1) {
        char type[32] = { 0 };
        GetPrivateProfileStringA(p, "type", "", type, sizeof(type), g_iniPath);
        if (type[0]) WritePrivateProfileStringA(p, NULL, NULL, g_iniPath);
    }

    for (int e = 0; e < g_count; e++) {
        const Entry* entry = &g_entries[e];
        int type = EntryType(entry);

        for (int i = 0; i < g_propCount; i++) {
            if (!entry->values[i][0]) continue;
            if (!Spec_PropAppliesTo(&g_props[i], type)) continue;
            WritePrivateProfileStringA(entry->section, g_props[i].key,
                                       entry->values[i], g_iniPath);
        }
    }
    WritePrivateProfileStringA(NULL, NULL, NULL, g_iniPath);   /* flush */
    g_dirty = false;
}

/* Build a renderable spec straight from the on-screen values. */
static void BuildSpec(const Entry* entry, WidgetSpec* out) {
    Spec_Defaults(out);
    strncpy(out->section, entry->section, LW_SECTION_LEN - 1);

    int presetIndex = FindProp("preset");
    if (presetIndex >= 0 && entry->values[presetIndex][0])
        Spec_Set(out, "preset", entry->values[presetIndex]);

    for (int i = 0; i < g_propCount; i++) {
        if (i == presetIndex || !entry->values[i][0]) continue;
        Spec_Set(out, g_props[i].key, entry->values[i]);
    }
    Spec_Finalize(out);
}

/* ─────────────────────────── choice fields ─────────────────────────── */

/*
 * Enum and font fields are a button plus a popup list we draw ourselves.
 *
 * A real COMBOBOX would be less code, but its frame and drop arrow are drawn
 * by the system in the system's colours, which on a dark surface reads as a
 * light control someone forgot to style. Owning the popup also means the font
 * list can render every family in its own face, which is the only preview
 * that actually helps you choose one.
 */
typedef struct {
    char** items;          /* NULL-terminated strings */
    int    count;
    int    capacity;
    int    selected;
    bool   owns_items;     /* font lists point at g_fonts and are not owned */
    bool   font_preview;
} Choice;

static Choice g_choice[MAX_PROPS];

static void ChoiceAdd(Choice* choice, const char* text) {
    if (choice->count == choice->capacity) {
        int capacity = choice->capacity ? choice->capacity * 2 : 8;
        char** grown = (char**)realloc(choice->items, (size_t)capacity * sizeof(char*));
        if (!grown) return;
        choice->items = grown;
        choice->capacity = capacity;
    }
    choice->items[choice->count++] = choice->owns_items ? _strdup(text) : (char*)text;
}

static void ChoiceFill(Choice* choice, const char* options) {
    choice->owns_items = true;
    if (!options) return;

    /* "@presets" expands to the built-in theme list. */
    if (strcmp(options, "@presets") == 0) {
        ChoiceAdd(choice, "(none)");
        int count = 0;
        const StylePreset* presets = Style_Presets(&count);
        for (int i = 0; i < count; i++) ChoiceAdd(choice, presets[i].name);
        return;
    }

    char buffer[256];
    strncpy(buffer, options, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';

    char* context = NULL;
    for (char* token = strtok_s(buffer, "|", &context); token;
         token = strtok_s(NULL, "|", &context))
        ChoiceAdd(choice, token);
}

static const char* ChoiceValue(const Choice* choice) {
    if (choice->selected < 0 || choice->selected >= choice->count) return "";
    const char* text = choice->items[choice->selected];
    return strcmp(text, "(none)") == 0 ? "" : text;
}

static void ChoiceSelect(Choice* choice, const char* value) {
    const char* wanted = (value && value[0]) ? value : "(none)";
    for (int i = 0; i < choice->count; i++)
        if (_stricmp(choice->items[i], wanted) == 0) { choice->selected = i; return; }
    choice->selected = choice->count > 0 ? 0 : -1;
}

/* The preset as it currently reads on screen, falling back to the entry. */
static void CurrentPresetName(const Entry* entry, char* out, int cap) {
    out[0] = '\0';
    int index = FindProp("preset");
    if (index < 0) return;

    if (g_choice[index].count > 0) strncpy(out, ChoiceValue(&g_choice[index]), (size_t)cap - 1);
    else if (entry) strncpy(out, entry->values[index], (size_t)cap - 1);
    out[cap - 1] = '\0';
}

/* ─────────────────────────── the dropdown popup ───────────────────── */

#define MENU_CLASS "LiteWidgetsMenu"

static HWND g_menu = NULL;
static int  g_menuProp = -1;
static int  g_menuHot = -1;
static int  g_menuTop = 0;       /* first visible item */
static int  g_menuRows = 0;      /* items that fit */

static void RefreshPreview(void);
static void OnChoiceChanged(int index);

static int MenuRowHeight(void) { return Scale(26); }

static void CloseMenu(bool commit, int pick) {
    if (!g_menu) return;

    HWND menu = g_menu;
    int prop = g_menuProp;
    g_menu = NULL;
    g_menuProp = -1;

    if (GetCapture() == menu) ReleaseCapture();
    DestroyWindow(menu);

    if (commit && prop >= 0 && pick >= 0 && pick < g_choice[prop].count) {
        g_choice[prop].selected = pick;
        InvalidateRect(g_controls[prop].ctrl, NULL, TRUE);
        OnChoiceChanged(prop);
    }
}

static void MenuPaint(HWND hWnd, HDC hdc) {
    RECT client;
    GetClientRect(hWnd, &client);

    FillRounded(hdc, &client, Scale(8), g_theme.field, g_theme.line, true);
    if (g_menuProp < 0) return;

    const Choice* choice = &g_choice[g_menuProp];
    int rowH = MenuRowHeight();
    int y = Scale(4);

    for (int i = g_menuTop; i < choice->count && i < g_menuTop + g_menuRows; i++) {
        RECT row = { client.left + Scale(4), y, client.right - Scale(4), y + rowH };
        bool hot = (i == g_menuHot);
        bool current = (i == choice->selected);

        if (hot) FillRounded(hdc, &row, Scale(5), g_theme.accent, 0, false);
        else if (current) FillRounded(hdc, &row, Scale(5), g_theme.field_hot, 0, false);

        RECT label = row;
        label.left += Scale(10);
        label.right -= Scale(8);

        HFONT font = g_font;
        HFONT face = NULL;
        if (choice->font_preview && i > 0) {
            face = CreateFontA(-MulDiv(11, g_dpi, 72), 0, 0, 0, FW_NORMAL, 0, 0, 0,
                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                               CLEARTYPE_QUALITY, DEFAULT_PITCH, choice->items[i]);
            if (face) font = face;
        }
        DrawLabel(hdc, &label, choice->items[i], hot ? g_theme.on_accent : g_theme.text,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, font);
        if (face) DeleteObject(face);

        y += rowH;
    }
}

static int MenuHit(int y) {
    int index = g_menuTop + (y - Scale(4)) / MenuRowHeight();
    if (g_menuProp < 0) return -1;
    if (index < 0 || index >= g_choice[g_menuProp].count) return -1;
    if (index >= g_menuTop + g_menuRows) return -1;
    return index;
}

static void MenuScrollTo(int top) {
    if (g_menuProp < 0) return;
    int maximum = g_choice[g_menuProp].count - g_menuRows;
    if (maximum < 0) maximum = 0;
    if (top < 0) top = 0;
    if (top > maximum) top = maximum;
    if (top == g_menuTop) return;
    g_menuTop = top;
    InvalidateRect(g_menu, NULL, FALSE);
}

static void MenuMoveHot(int delta) {
    if (g_menuProp < 0) return;
    int count = g_choice[g_menuProp].count;
    int next = g_menuHot + delta;
    if (next < 0) next = 0;
    if (next >= count) next = count - 1;
    g_menuHot = next;

    if (next < g_menuTop) MenuScrollTo(next);
    else if (next >= g_menuTop + g_menuRows) MenuScrollTo(next - g_menuRows + 1);
    InvalidateRect(g_menu, NULL, FALSE);
}

static LRESULT CALLBACK MenuProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);

            RECT client;
            GetClientRect(hWnd, &client);
            HDC buffer = CreateCompatibleDC(hdc);
            HBITMAP surface = CreateCompatibleBitmap(hdc, client.right, client.bottom);
            HBITMAP old = (HBITMAP)SelectObject(buffer, surface);

            MenuPaint(hWnd, buffer);
            BitBlt(hdc, 0, 0, client.right, client.bottom, buffer, 0, 0, SRCCOPY);

            SelectObject(buffer, old);
            DeleteObject(surface);
            DeleteDC(buffer);
            EndPaint(hWnd, &ps);
            return 0;
        }

        case WM_MOUSEMOVE: {
            int hot = MenuHit(GET_Y_LPARAM(lParam));
            /* Capture means we see moves outside too; ignore those. */
            RECT client;
            GetClientRect(hWnd, &client);
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            if (!PtInRect(&client, pt)) hot = -1;
            if (hot != g_menuHot) {
                g_menuHot = hot;
                InvalidateRect(hWnd, NULL, FALSE);
            }
            return 0;
        }

        case WM_MOUSEWHEEL:
            MenuScrollTo(g_menuTop - GET_WHEEL_DELTA_WPARAM(wParam) * 3 / WHEEL_DELTA);
            return 0;

        case WM_LBUTTONDOWN: {
            RECT client;
            GetClientRect(hWnd, &client);
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            if (!PtInRect(&client, pt)) { CloseMenu(false, -1); return 0; }
            return 0;
        }

        case WM_LBUTTONUP: {
            int pick = MenuHit(GET_Y_LPARAM(lParam));
            if (pick >= 0) CloseMenu(true, pick);
            return 0;
        }

        case WM_KEYDOWN:
            switch (wParam) {
                case VK_ESCAPE: CloseMenu(false, -1); return 0;
                case VK_RETURN: CloseMenu(true, g_menuHot); return 0;
                case VK_UP:     MenuMoveHot(-1); return 0;
                case VK_DOWN:   MenuMoveHot(+1); return 0;
                case VK_PRIOR:  MenuMoveHot(-g_menuRows); return 0;
                case VK_NEXT:   MenuMoveHot(+g_menuRows); return 0;
                default: return 0;
            }

        case WM_KILLFOCUS:
            CloseMenu(false, -1);
            return 0;
    }
    return DefWindowProcA(hWnd, msg, wParam, lParam);
}

static void OpenMenu(int index) {
    if (g_menu) { CloseMenu(false, -1); return; }

    Choice* choice = &g_choice[index];
    if (choice->count == 0) return;

    RECT field;
    GetWindowRect(g_controls[index].ctrl, &field);

    int rowH = MenuRowHeight();
    int wanted = choice->count;
    if (wanted > 12) wanted = 12;
    int height = wanted * rowH + Scale(8);

    /* Flip above the field when there is no room below it. */
    RECT screen;
    SystemParametersInfoA(SPI_GETWORKAREA, 0, &screen, 0);
    int top = field.bottom + Scale(2);
    if (top + height > screen.bottom) {
        top = field.top - height - Scale(2);
        if (top < screen.top) {
            top = screen.top;
            height = field.top - screen.top - Scale(4);
            wanted = (height - Scale(8)) / rowH;
            if (wanted < 1) wanted = 1;
            height = wanted * rowH + Scale(8);
        }
    }

    g_menuProp = index;
    g_menuRows = wanted;
    g_menuHot  = choice->selected;
    g_menuTop  = choice->selected - wanted / 2;
    if (g_menuTop > choice->count - wanted) g_menuTop = choice->count - wanted;
    if (g_menuTop < 0) g_menuTop = 0;

    g_menu = CreateWindowExA(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, MENU_CLASS, "",
                             WS_POPUP, field.left, top,
                             field.right - field.left, height,
                             g_window, NULL, g_instance, NULL);
    if (!g_menu) { g_menuProp = -1; return; }

    /* Clip the window to the shape we paint, so the corners are not stray. */
    RECT shape;
    GetClientRect(g_menu, &shape);
    HRGN region = CreateRoundRectRgn(0, 0, shape.right + 1, shape.bottom + 1,
                                     Scale(16), Scale(16));
    if (region) SetWindowRgn(g_menu, region, FALSE);

    ShowWindow(g_menu, SW_SHOWNA);
    SetForegroundWindow(g_menu);
    SetFocus(g_menu);
    SetCapture(g_menu);
}

/* The closed state of a choice field: the value, and a chevron we draw. */
static void DrawChoiceField(const DRAWITEMSTRUCT* item, int index) {
    const Choice* choice = &g_choice[index];
    bool open = (g_menu && g_menuProp == index);

    FillRounded(item->hDC, &item->rcItem, Scale(6),
                open ? g_theme.field_hot : g_theme.field,
                open ? g_theme.accent : g_theme.line, true);

    const char* text = (choice->selected >= 0 && choice->selected < choice->count)
                     ? choice->items[choice->selected] : "";

    RECT label = item->rcItem;
    label.left += Scale(10);
    label.right -= Scale(26);

    HFONT font = g_font;
    HFONT face = NULL;
    if (choice->font_preview && choice->selected > 0) {
        face = CreateFontA(-MulDiv(11, g_dpi, 72), 0, 0, 0, FW_NORMAL, 0, 0, 0,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           CLEARTYPE_QUALITY, DEFAULT_PITCH, text);
        if (face) font = face;
    }
    DrawLabel(item->hDC, &label, text, g_theme.text,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, font);
    if (face) DeleteObject(face);

    /* A chevron, drawn rather than typed: the glyph fonts disagree on it. */
    int cx = item->rcItem.right - Scale(15);
    int cy = (item->rcItem.top + item->rcItem.bottom) / 2 - Scale(1);
    int arm = Scale(4);
    HPEN pen = CreatePen(PS_SOLID, Scale(2) > 2 ? Scale(2) : 2, g_theme.dim);
    HPEN oldPen = (HPEN)SelectObject(item->hDC, pen);
    MoveToEx(item->hDC, cx - arm, cy - arm / 2, NULL);
    LineTo(item->hDC, cx, cy + arm / 2);
    LineTo(item->hDC, cx + arm + 1, cy - arm / 2 - 1);
    SelectObject(item->hDC, oldPen);
    DeleteObject(pen);
}

/* ─────────────────────────── colour rows ─────────────────────────── */

static ARGB RowColor(int index) {
    char text[VALUE_LEN] = { 0 };
    GetWindowTextA(g_controls[index].ctrl, text, VALUE_LEN);
    if (!text[0]) return Style_ParseColor(g_props[index].def, 0);
    return Style_ParseColor(text, 0);
}

static void SyncColorRow(int index) {
    ARGB color = RowColor(index);
    if (g_controls[index].slider) {
        SetWindowLongPtrA(g_controls[index].slider, GWLP_USERDATA, (LONG_PTR)((color >> 24) & 0xFF));
        InvalidateRect(g_controls[index].slider, NULL, TRUE);
    }
    if (g_controls[index].swatch) InvalidateRect(g_controls[index].swatch, NULL, TRUE);
}

static void SetRowColor(int index, ARGB color) {
    char text[16];
    Style_FormatColor(color, text, sizeof(text));
    g_suppressSync = true;
    SetWindowTextA(g_controls[index].ctrl, text);
    g_suppressSync = false;
    SyncColorRow(index);
}

static void PickColor(int index) {
    ARGB current = RowColor(index);

    CHOOSECOLORA cc;
    memset(&cc, 0, sizeof(cc));
    cc.lStructSize  = sizeof(cc);
    cc.hwndOwner    = g_window;
    cc.rgbResult    = RGB((current >> 16) & 0xFF, (current >> 8) & 0xFF, current & 0xFF);
    cc.lpCustColors = g_customColors;
    cc.Flags        = CC_FULLOPEN | CC_RGBINIT | CC_ANYCOLOR;

    if (!ChooseColorA(&cc)) return;

    /* The common dialog has no alpha channel, so keep the one we had. */
    BYTE alpha = (BYTE)((current >> 24) & 0xFF);
    if (alpha == 0) alpha = 0xFF;
    ARGB picked = ((ARGB)alpha << 24)
                | ((ARGB)GetRValue(cc.rgbResult) << 16)
                | ((ARGB)GetGValue(cc.rgbResult) << 8)
                |  (ARGB)GetBValue(cc.rgbResult);
    SetRowColor(index, picked);
}

/* ─────────────────────────── the alpha slider ─────────────────────── */

/*
 * A trackbar cannot show what it is actually setting. This one paints the
 * colour it belongs to fading to nothing behind the handle, so the alpha
 * channel is legible at a glance instead of being a number between 0 and 255.
 */
#define SLIDER_CLASS "LiteWidgetsSlider"

static void SliderPaint(HWND hWnd, HDC hdc) {
    RECT rc;
    GetClientRect(hWnd, &rc);

    HBRUSH back = CreateSolidBrush(g_theme.surface);
    FillRect(hdc, &rc, back);
    DeleteObject(back);

    int value = (int)GetWindowLongPtrA(hWnd, GWLP_USERDATA);
    if (value < 0) value = 0;
    if (value > 255) value = 255;

    int trackH = Scale(4);
    int knob = Scale(7);
    RECT track = { rc.left + knob, (rc.bottom - trackH) / 2,
                   rc.right - knob, (rc.bottom + trackH) / 2 };

    FillRounded(hdc, &track, trackH / 2, g_theme.field, 0, false);

    int span = track.right - track.left;
    RECT filled = track;
    filled.right = track.left + MulDiv(span, value, 255);
    if (filled.right > filled.left + 1)
        FillRounded(hdc, &filled, trackH / 2, g_theme.accent, 0, false);

    int cx = track.left + MulDiv(span, value, 255);
    RECT handle = { cx - knob, rc.top + Scale(3), cx + knob, rc.bottom - Scale(3) };
    FillRounded(hdc, &handle, knob, g_theme.dark ? RGB(0xF0, 0xF0, 0xF5) : RGB(0xFF, 0xFF, 0xFF),
                g_theme.line, true);
}

static void SliderSet(HWND hWnd, int x) {
    RECT rc;
    GetClientRect(hWnd, &rc);
    int knob = Scale(7);
    int span = (rc.right - knob) - (rc.left + knob);
    if (span <= 0) return;

    int value = MulDiv(x - (rc.left + knob), 255, span);
    if (value < 0) value = 0;
    if (value > 255) value = 255;

    SetWindowLongPtrA(hWnd, GWLP_USERDATA, value);
    InvalidateRect(hWnd, NULL, FALSE);
    SendMessageA(GetParent(hWnd), WM_HSCROLL, 0, (LPARAM)hWnd);
}

static LRESULT CALLBACK SliderProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);
            SliderPaint(hWnd, hdc);
            EndPaint(hWnd, &ps);
            return 0;
        }

        case WM_LBUTTONDOWN:
            SetCapture(hWnd);
            SliderSet(hWnd, (short)LOWORD(lParam));
            return 0;

        case WM_MOUSEMOVE:
            if (GetCapture() == hWnd) SliderSet(hWnd, (short)LOWORD(lParam));
            return 0;

        case WM_LBUTTONUP:
            if (GetCapture() == hWnd) ReleaseCapture();
            return 0;
    }
    return DefWindowProcA(hWnd, msg, wParam, lParam);
}

/* ─────────────────────────── font & file pickers ─────────────────── */

static void PickFile(int index) {
    char path[MAX_PATH] = { 0 };
    GetWindowTextA(g_controls[index].ctrl, path, MAX_PATH);

    int type = SelectedType();
    OPENFILENAMEA ofn;
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = g_window;
    ofn.lpstrFilter = (type == WIDGET_IMAGE)
        ? "Images\0*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tif;*.tiff\0All files\0*.*\0"
        : "Text files\0*.txt;*.md;*.log\0All files\0*.*\0";
    ofn.lpstrFile   = path;
    ofn.nMaxFile    = MAX_PATH;
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;

    if (GetOpenFileNameA(&ofn)) SetWindowTextA(g_controls[index].ctrl, path);
}

/* ─────────────────────────── control values ─────────────────────────── */

/*
 * BS_OWNERDRAW replaces a button's style bits rather than adding to them, so
 * an owner-drawn checkbox keeps no check state of its own. The switch tracks
 * its own, which is also what lets it read as a switch rather than a tick.
 */
static bool ToggleState(HWND button) {
    return GetWindowLongPtrA(button, GWLP_USERDATA) != 0;
}

static void SetToggle(HWND button, bool on) {
    SetWindowLongPtrA(button, GWLP_USERDATA, on ? 1 : 0);
    InvalidateRect(button, NULL, TRUE);
}

static void ReadControl(int index, char* out, int cap) {
    out[0] = '\0';
    HWND ctrl = g_controls[index].ctrl;
    if (!ctrl) return;

    switch (g_props[index].kind) {
        case PK_ENUM:
        case PK_FONT:
            strncpy(out, ChoiceValue(&g_choice[index]), (size_t)cap - 1);
            out[cap - 1] = '\0';
            break;
        case PK_BOOL:
            strncpy(out, ToggleState(ctrl) ? "true" : "false", (size_t)cap - 1);
            break;
        default:
            GetWindowTextA(ctrl, out, cap);
            break;
    }
}

static void WriteControl(int index, const char* value, const char* preset) {
    HWND ctrl = g_controls[index].ctrl;
    if (!ctrl) return;

    const char* effective = value[0] ? value : EffectiveDefault(preset, index);

    switch (g_props[index].kind) {
        case PK_ENUM:
        case PK_FONT:
            ChoiceSelect(&g_choice[index], effective);
            InvalidateRect(ctrl, NULL, TRUE);
            break;
        case PK_BOOL:
            SetToggle(ctrl, Style_ParseBool(effective, false));
            break;
        default:
            SetWindowTextA(ctrl, effective);
            break;
    }
    if (g_props[index].kind == PK_COLOR) SyncColorRow(index);
}

static void CollectSelection(void) {
    if (g_selected < 0 || g_selected >= g_count) return;
    Entry* entry = &g_entries[g_selected];

    char preset[VALUE_LEN];
    CurrentPresetName(entry, preset, VALUE_LEN);

    for (int i = 0; i < g_propCount; i++) {
        char value[VALUE_LEN];
        ReadControl(i, value, VALUE_LEN);

        /*
         * `type` anchors the section -- without it the loader skips the whole
         * widget -- and `preset` is what the other defaults are measured
         * against, so both are always kept.
         */
        bool required = _stricmp(g_props[i].key, "type") == 0
                     || _stricmp(g_props[i].key, "preset") == 0;
        if (!required && strcmp(value, EffectiveDefault(preset, i)) == 0)
            value[0] = '\0';

        if (strcmp(entry->values[i], value) != 0) g_dirty = true;
        strncpy(entry->values[i], value, VALUE_LEN - 1);
        entry->values[i][VALUE_LEN - 1] = '\0';
    }

    char name[LW_SECTION_LEN] = { 0 };
    GetWindowTextA(g_name, name, sizeof(name));
    if (name[0] && strcmp(name, entry->section) != 0) {
        strncpy(entry->section, name, LW_SECTION_LEN - 1);
        entry->section[LW_SECTION_LEN - 1] = '\0';
        g_dirty = true;
    }
}

/* ─────────────────────────── preview ─────────────────────────── */

static void RefreshPreview(void) {
    if (g_preview) InvalidateRect(g_preview, NULL, FALSE);
}

static void PaintChecker(HDC hdc, const RECT* rc) {
    COLORREF a = g_theme.dark ? RGB(0x2A, 0x2A, 0x31) : RGB(0xE4, 0xE4, 0xEA);
    COLORREF b = g_theme.dark ? RGB(0x33, 0x33, 0x3B) : RGB(0xF4, 0xF4, 0xF8);
    HBRUSH dark  = CreateSolidBrush(a);
    HBRUSH light = CreateSolidBrush(b);
    int cell = Scale(9);

    FillRect(hdc, rc, dark);
    for (int y = rc->top; y < rc->bottom; y += cell) {
        for (int x = rc->left; x < rc->right; x += cell) {
            if (((x / cell) + (y / cell)) % 2) continue;
            RECT tile = { x, y, x + cell, y + cell };
            if (tile.right  > rc->right)  tile.right  = rc->right;
            if (tile.bottom > rc->bottom) tile.bottom = rc->bottom;
            FillRect(hdc, &tile, light);
        }
    }
    DeleteObject(dark);
    DeleteObject(light);
}

/*
 * Render the widget exactly the way the desktop does -- same painter, same
 * premultiplied bitmap, same alpha blend -- then scale it down to fit.
 */
static void PaintPreview(HDC hdc, const RECT* client) {
    PaintChecker(hdc, client);
    if (g_selected < 0 || g_selected >= g_count) return;

    Entry snapshot = g_entries[g_selected];
    for (int i = 0; i < g_propCount; i++)
        ReadControl(i, snapshot.values[i], VALUE_LEN);

    WidgetSpec spec;
    BuildSpec(&snapshot, &spec);

    int width  = spec.width  > 0 ? spec.width  : 1;
    int height = spec.height > 0 ? spec.height : 1;
    if (width > 2000) width = 2000;
    if (height > 2000) height = 2000;

    GpBitmap* bitmap = NULL;
    if (GdipCreateBitmapFromScan0(width, height, 0, PixelFormat32bppPARGB, NULL, &bitmap) != 0)
        return;

    GpGraphics* gfx = NULL;
    if (GdipGetImageGraphicsContext((GpImage*)bitmap, &gfx) == 0 && gfx) {
        GdipSetSmoothingMode(gfx, SmoothingModeAntiAlias);
        GdipSetTextRenderingHint(gfx, TextRenderingHintAntiAliasGridFit);
        GdipSetInterpolationMode(gfx, InterpolationModeHighQualityBicubic);
        GdipSetPixelOffsetMode(gfx, PixelOffsetModeHalf);
        Config_Paint(&spec, g_iniPath, gfx, width, height);
        GdipDeleteGraphics(gfx);
    }

    HBITMAP hBitmap = NULL;
    if (GdipCreateHBITMAPFromBitmap(bitmap, &hBitmap, 0) == 0 && hBitmap) {
        int boxW = client->right - client->left - Scale(16);
        int boxH = client->bottom - client->top - Scale(16);

        double scale = 1.0;
        if (width > boxW || height > boxH) {
            double sx = (double)boxW / width;
            double sy = (double)boxH / height;
            scale = sx < sy ? sx : sy;
        }
        int drawW = (int)(width * scale);
        int drawH = (int)(height * scale);
        int drawX = client->left + (client->right - client->left - drawW) / 2;
        int drawY = client->top + (client->bottom - client->top - drawH) / 2;

        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP old = (HBITMAP)SelectObject(memDC, hBitmap);
        SetStretchBltMode(hdc, HALFTONE);

        BLENDFUNCTION blend;
        blend.BlendOp = AC_SRC_OVER;
        blend.BlendFlags = 0;
        blend.SourceConstantAlpha = (BYTE)(spec.opacity * 255.0f + 0.5f);
        blend.AlphaFormat = AC_SRC_ALPHA;
        AlphaBlend(hdc, drawX, drawY, drawW, drawH, memDC, 0, 0, width, height, blend);

        SelectObject(memDC, old);
        DeleteDC(memDC);
        DeleteObject(hBitmap);
    }

    GdipDisposeImage((GpImage*)bitmap);
}

static LRESULT CALLBACK PreviewProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);

            RECT client;
            GetClientRect(hWnd, &client);

            /* Double-buffered: the checkerboard would flicker otherwise. */
            HDC buffer = CreateCompatibleDC(hdc);
            HBITMAP surface = CreateCompatibleBitmap(hdc, client.right, client.bottom);
            HBITMAP old = (HBITMAP)SelectObject(buffer, surface);

            PaintPreview(buffer, &client);
            BitBlt(hdc, 0, 0, client.right, client.bottom, buffer, 0, 0, SRCCOPY);

            SelectObject(buffer, old);
            DeleteObject(surface);
            DeleteDC(buffer);
            EndPaint(hWnd, &ps);
            return 0;
        }
    }
    return DefWindowProcA(hWnd, msg, wParam, lParam);
}

/* ─────────────────────────── widget list ─────────────────────────── */

static void LayoutWindow(void);

static void RefreshList(void) {
    int top = (int)SendMessageA(g_list, LB_GETTOPINDEX, 0, 0);
    SendMessageA(g_list, LB_RESETCONTENT, 0, 0);
    for (int i = 0; i < g_count; i++)
        SendMessageA(g_list, LB_ADDSTRING, 0, (LPARAM)g_entries[i].section);
    if (g_selected >= 0 && g_selected < g_count)
        SendMessageA(g_list, LB_SETCURSEL, g_selected, 0);
    SendMessageA(g_list, LB_SETTOPINDEX, top, 0);
}

/* ─────────────────────────── pages and layout ─────────────────────── */

static bool GroupHasVisibleProps(int group, int type) {
    for (int i = 0; i < g_propCount; i++)
        if (g_props[i].group == group && Spec_PropAppliesTo(&g_props[i], type)) return true;
    return false;
}

static void RebuildPages(int type) {
    int previous = (g_activePage < g_pageCount) ? g_pages[g_activePage] : PG_GENERAL;

    g_pageCount = 0;
    for (int group = 0; group < PG__COUNT; group++)
        if (GroupHasVisibleProps(group, type)) g_pages[g_pageCount++] = group;

    g_activePage = 0;
    for (int i = 0; i < g_pageCount; i++)
        if (g_pages[i] == previous) { g_activePage = i; break; }
}

/* Flow the category pills across the top, wrapping when they run out of room. */
static void LayoutPills(int left, int right, int top) {
    int gap = Scale(6);
    int height = Scale(28);
    int x = left, y = top, rows = 1;

    HDC hdc = GetDC(g_window);
    HFONT old = (HFONT)SelectObject(hdc, g_fontBold);

    for (int i = 0; i < g_pageCount; i++) {
        const char* name = Spec_GroupName(g_pages[i]);
        SIZE size = { 0, 0 };
        GetTextExtentPoint32A(hdc, name, (int)strlen(name), &size);
        int w = size.cx + Scale(26);

        if (x + w > right && x > left) {
            x = left;
            y += height + gap;
            rows++;
        }
        SetRect(&g_pageRects[i], x, y, x + w, y + height);
        x += w + gap;
    }

    SelectObject(hdc, old);
    ReleaseDC(g_window, hdc);
    g_pillRows = rows;
}

static int PillsHeight(void) {
    return g_pillRows * Scale(28) + (g_pillRows - 1) * Scale(6);
}

/*
 * Position the rows of the active category inside the scrolling pane and
 * report the total height, which is what the scroll range is built from.
 */
static int LayoutRows(void) {
    RECT pane;
    GetClientRect(g_pane, &pane);

    int pad     = Scale(16);
    int rowH    = Scale(34);
    int ctrlH   = Scale(26);
    int labelW  = Scale(120);
    int fieldX  = pad + labelW;
    int fieldW  = pane.right - fieldX - pad - Scale(4);
    if (fieldW < Scale(120)) fieldW = Scale(120);

    int activeGroup = (g_activePage < g_pageCount) ? g_pages[g_activePage] : -1;
    int type = SelectedType();
    int y = Scale(8) - g_scrollY;
    int total = Scale(16);

    for (int i = 0; i < g_propCount; i++) {
        bool visible = (g_props[i].group == activeGroup)
                    && Spec_PropAppliesTo(&g_props[i], type);
        ShowRow(&g_controls[i], visible);
        if (!visible) continue;

        int top = y + (rowH - ctrlH) / 2;
        Place(g_controls[i].label, pad, y + (rowH - Scale(18)) / 2, labelW - Scale(10), Scale(18));

        switch (g_props[i].kind) {
            case PK_COLOR: {
                int swatchW = Scale(30);
                int sliderW = Scale(96);
                int editW = fieldW - swatchW - sliderW - Scale(16);
                if (editW < Scale(60)) editW = Scale(60);
                Place(g_controls[i].swatch, fieldX, top, swatchW, ctrlH);
                Place(g_controls[i].ctrl, fieldX + swatchW + Scale(8) + Scale(9), top + Scale(4),
                      editW - Scale(18), ctrlH - Scale(8));
                Place(g_controls[i].slider, fieldX + swatchW + editW + Scale(16), top,
                      sliderW, ctrlH);
                break;
            }
            case PK_FILE: {
                int buttonW = Scale(84);
                Place(g_controls[i].ctrl, fieldX + Scale(9), top + Scale(4),
                      fieldW - buttonW - Scale(26), ctrlH - Scale(8));
                Place(g_controls[i].browse, fieldX + fieldW - buttonW, top, buttonW, ctrlH);
                break;
            }
            case PK_BOOL:
                Place(g_controls[i].ctrl, fieldX, top, Scale(44), ctrlH);
                break;
            case PK_ENUM:
            case PK_FONT:
                Place(g_controls[i].ctrl, fieldX, top, fieldW, ctrlH);
                break;
            default:
                Place(g_controls[i].ctrl, fieldX + Scale(9), top + Scale(4),
                      fieldW - Scale(18), ctrlH - Scale(8));
                break;
        }

        y += rowH;
        total += rowH;
    }
    return total;
}

/*
 * The pane scrolls with a bar drawn in the theme rather than WS_VSCROLL.
 * A system scrollbar on a dark surface is a light grey slab that no amount of
 * owner drawing elsewhere can make look intentional.
 */
#define SCROLLBAR_W 10

static bool PaneScrolls(void) {
    RECT pane;
    GetClientRect(g_pane, &pane);
    return g_paneHeight > pane.bottom - pane.top;
}

static RECT ScrollThumb(void) {
    RECT pane, thumb = { 0, 0, 0, 0 };
    GetClientRect(g_pane, &pane);

    int visible = pane.bottom - pane.top;
    if (g_paneHeight <= visible) return thumb;

    int track = visible - Scale(8);
    int height = MulDiv(track, visible, g_paneHeight);
    if (height < Scale(28)) height = Scale(28);

    int travel = track - height;
    int offset = (g_paneHeight - visible) > 0
               ? MulDiv(travel, g_scrollY, g_paneHeight - visible) : 0;

    thumb.right  = pane.right - Scale(4);
    thumb.left   = thumb.right - Scale(SCROLLBAR_W);
    thumb.top    = pane.top + Scale(4) + offset;
    thumb.bottom = thumb.top + height;
    return thumb;
}

static void UpdateScroll(void) {
    RECT pane;
    GetClientRect(g_pane, &pane);
    int visible = pane.bottom - pane.top;

    if (g_scrollY > g_paneHeight - visible) g_scrollY = g_paneHeight - visible;
    if (g_scrollY < 0) g_scrollY = 0;
}

static void RelayoutPane(void) {
    g_paneHeight = LayoutRows();
    UpdateScroll();
    g_paneHeight = LayoutRows();
    InvalidateRect(g_pane, NULL, TRUE);
}

static void ScrollPaneTo(int position) {
    RECT pane;
    GetClientRect(g_pane, &pane);
    int visible = pane.bottom - pane.top;
    int maximum = g_paneHeight - visible;
    if (maximum < 0) maximum = 0;

    if (position < 0) position = 0;
    if (position > maximum) position = maximum;
    if (position == g_scrollY) return;

    g_scrollY = position;
    LayoutRows();
    UpdateScroll();
    InvalidateRect(g_pane, NULL, TRUE);
}

/* ─────────────────────────── selection ─────────────────────────── */

static void PopulateSelection(void) {
    if (g_selected < 0 || g_selected >= g_count) return;
    const Entry* entry = &g_entries[g_selected];
    int type = EntryType(entry);

    int presetIndex = FindProp("preset");
    const char* preset = (presetIndex >= 0) ? entry->values[presetIndex] : "";

    g_suppressSync = true;
    SetWindowTextA(g_name, entry->section);
    if (presetIndex >= 0) WriteControl(presetIndex, entry->values[presetIndex], "");
    for (int i = 0; i < g_propCount; i++) {
        if (i == presetIndex) continue;
        WriteControl(i, entry->values[i], preset);
    }
    g_suppressSync = false;

    RebuildPages(type);
    g_scrollY = 0;
    LayoutWindow();     /* the pill row can change height with the type */
    RefreshPreview();
}

static void SelectEntry(int index) {
    CollectSelection();
    g_selected = index;
    PopulateSelection();
    RefreshList();
}

static void MakeUniqueSection(char* out, size_t cap, const char* base) {
    for (int suffix = 1; suffix < 1000; suffix++) {
        char candidate[LW_SECTION_LEN];
        if (suffix == 1) _snprintf(candidate, sizeof(candidate), "%s", base);
        else             _snprintf(candidate, sizeof(candidate), "%s_%d", base, suffix);

        bool taken = false;
        for (int i = 0; i < g_count; i++)
            if (_stricmp(g_entries[i].section, candidate) == 0) { taken = true; break; }

        if (!taken) {
            strncpy(out, candidate, cap - 1);
            out[cap - 1] = '\0';
            return;
        }
    }
    strncpy(out, base, cap - 1);
}

static void AddEntry(bool duplicate) {
    if (g_count >= LW_MAX_WIDGETS) {
        MessageBoxA(g_window, "Widget limit reached.", "LiteWidgets", MB_ICONINFORMATION);
        return;
    }
    CollectSelection();

    Entry* entry = &g_entries[g_count];
    if (duplicate && g_selected >= 0 && g_selected < g_count) {
        *entry = g_entries[g_selected];
        MakeUniqueSection(entry->section, LW_SECTION_LEN, g_entries[g_selected].section);
    } else {
        /* Only `type` is stated; everything else stays on its default. */
        memset(entry, 0, sizeof(*entry));
        MakeUniqueSection(entry->section, LW_SECTION_LEN, "widget");
        int typeIndex = FindProp("type");
        if (typeIndex >= 0) strncpy(entry->values[typeIndex], "clock", VALUE_LEN - 1);
    }

    g_count++;
    g_selected = g_count - 1;
    g_dirty = true;
    PopulateSelection();
    RefreshList();
}

static void RemoveEntry(void) {
    if (g_selected < 0 || g_selected >= g_count) return;

    char prompt[160];
    _snprintf(prompt, sizeof(prompt), "Delete \"%s\"?", g_entries[g_selected].section);
    if (MessageBoxA(g_window, prompt, "LiteWidgets", MB_OKCANCEL | MB_ICONQUESTION) != IDOK)
        return;

    for (int i = g_selected; i < g_count - 1; i++) g_entries[i] = g_entries[i + 1];
    g_count--;
    g_dirty = true;

    if (g_selected >= g_count) g_selected = g_count - 1;
    RefreshList();
    if (g_selected >= 0) PopulateSelection();
    else RefreshPreview();
}

/*
 * Choosing a preset writes its colours straight into the fields so the change
 * is visible and still fully editable afterwards.
 */
static void ApplyPresetToControls(const char* name) {
    if (!name || !name[0]) return;

    int count = 0;
    const StylePreset* presets = Style_Presets(&count);
    for (int i = 0; i < count; i++) {
        if (_stricmp(presets[i].name, name) != 0) continue;
        for (const StyleKV* kv = presets[i].pairs; kv->key; kv++) {
            int index = FindProp(kv->key);
            if (index >= 0) WriteControl(index, kv->value, name);
        }
        return;
    }
}

/* ─────────────────────────── owner drawing ─────────────────────────── */

static void DrawPushButton(const DRAWITEMSTRUCT* item, bool primary) {
    char text[64] = { 0 };
    GetWindowTextA(item->hwndItem, text, sizeof(text));

    bool pressed = (item->itemState & ODS_SELECTED) != 0;
    COLORREF fill, ink;

    if (primary) {
        fill = g_theme.accent;
        ink  = g_theme.on_accent;
        if (pressed) fill = g_theme.dark ? RGB(0x3A, 0xA8, 0xE2) : RGB(0x08, 0x66, 0xAC);
    } else {
        fill = pressed ? g_theme.field_hot : g_theme.field;
        ink  = g_theme.text;
    }

    FillRounded(item->hDC, &item->rcItem, Scale(6), fill, g_theme.line, !primary);
    DrawLabel(item->hDC, &item->rcItem, text, ink,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, g_font);
}

/* A switch rather than a tick box: the state reads from across the window. */
static void DrawToggle(const DRAWITEMSTRUCT* item) {
    bool on = ToggleState(item->hwndItem);

    RECT rc = item->rcItem;
    int height = Scale(20);
    int width  = Scale(38);
    RECT track = { rc.left, rc.top + (rc.bottom - rc.top - height) / 2,
                   rc.left + width, 0 };
    track.bottom = track.top + height;

    HBRUSH back = CreateSolidBrush(g_theme.surface);
    FillRect(item->hDC, &rc, back);
    DeleteObject(back);

    FillRounded(item->hDC, &track, height / 2,
                on ? g_theme.accent : g_theme.field, g_theme.line, !on);

    int knob = height - Scale(6);
    int kx = on ? track.right - knob - Scale(3) : track.left + Scale(3);
    RECT dot = { kx, track.top + Scale(3), kx + knob, track.top + Scale(3) + knob };
    FillRounded(item->hDC, &dot, knob / 2,
                on ? g_theme.on_accent : (g_theme.dark ? RGB(0xC8, 0xC8, 0xD2)
                                                       : RGB(0xFF, 0xFF, 0xFF)),
                g_theme.line, false);
}

/* The swatch shows alpha honestly, over a checkerboard of its own. */
static void DrawSwatch(const DRAWITEMSTRUCT* item, int index) {
    ARGB color = RowColor(index);
    RECT rc = item->rcItem;

    HBRUSH back = CreateSolidBrush(g_theme.surface);
    FillRect(item->hDC, &rc, back);
    DeleteObject(back);

    /* Low contrast on purpose: at 85% alpha a bold checker reads as a bug. */
    int cell = Scale(4);
    HBRUSH pale = CreateSolidBrush(RGB(0xCF, 0xCF, 0xD6));
    HBRUSH white = CreateSolidBrush(RGB(0xE9, 0xE9, 0xEF));
    FillRect(item->hDC, &rc, white);
    for (int y = rc.top; y < rc.bottom; y += cell)
        for (int x = rc.left; x < rc.right; x += cell) {
            if (((x / cell) + (y / cell)) % 2) continue;
            RECT tile = { x, y, x + cell < rc.right ? x + cell : rc.right,
                          y + cell < rc.bottom ? y + cell : rc.bottom };
            FillRect(item->hDC, &tile, pale);
        }
    DeleteObject(pale);
    DeleteObject(white);

    GpGraphics* gfx = NULL;
    if (GdipCreateFromHDC(item->hDC, &gfx) == 0 && gfx) {
        GdipSetSmoothingMode(gfx, SmoothingModeAntiAlias);
        GpPath* path = Drawing_RoundedPath((float)rc.left + 0.5f, (float)rc.top + 0.5f,
                                           (float)(rc.right - rc.left) - 1.0f,
                                           (float)(rc.bottom - rc.top) - 1.0f, (float)Scale(5));
        if (path) {
            GpSolidFill* brush = NULL;
            if (GdipCreateSolidFill(color, &brush) == 0) {
                GdipFillPath(gfx, (GpBrush*)brush, path);
                GdipDeleteBrush((GpBrush*)brush);
            }
            GpPen* pen = NULL;
            if (GdipCreatePen1(ToArgb(g_theme.line, 255), 1.0f, UnitPixel, &pen) == 0) {
                GdipDrawPath(gfx, pen, path);
                GdipDeletePen(pen);
            }
            GdipDeletePath(path);
        }
        GdipDeleteGraphics(gfx);
    }
}

static void DrawListItem(const DRAWITEMSTRUCT* item) {
    if ((int)item->itemID < 0) return;

    char text[LW_SECTION_LEN] = { 0 };
    SendMessageA(item->hwndItem, LB_GETTEXT, item->itemID, (LPARAM)text);

    bool selected = (item->itemState & ODS_SELECTED) != 0;
    RECT rc = item->rcItem;

    HBRUSH back = CreateSolidBrush(g_theme.surface);
    FillRect(item->hDC, &rc, back);
    DeleteObject(back);

    RECT pill = rc;
    InflateRect(&pill, -Scale(4), -Scale(1));
    if (selected) FillRounded(item->hDC, &pill, Scale(6), g_theme.field, 0, false);

    /* A bar in the accent colour marks the selection without shouting. */
    if (selected) {
        RECT mark = { pill.left + Scale(2), pill.top + Scale(5),
                      pill.left + Scale(5), pill.bottom - Scale(5) };
        FillRounded(item->hDC, &mark, Scale(2), g_theme.accent, 0, false);
    }

    RECT label = pill;
    label.left += Scale(14);
    int type = ((int)item->itemID < g_count) ? EntryType(&g_entries[item->itemID]) : 0;
    DrawLabel(item->hDC, &label, text, selected ? g_theme.text : g_theme.dim,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS,
              selected ? g_fontBold : g_font);

    RECT kind = pill;
    kind.right -= Scale(10);
    DrawLabel(item->hDC, &kind, Spec_TypeName(type), g_theme.dim,
              DT_RIGHT | DT_VCENTER | DT_SINGLELINE, g_fontSmall);
}

/* ─────────────────────────── the scrolling pane ───────────────────── */

#define PANE_CLASS "LiteWidgetsPane"

/*
 * The pane draws the rounded field behind every text control it hosts. The
 * controls themselves are ordinary EDITs painted with the field colour, so
 * they sit inside the shape without needing to be owner drawn.
 */
static void PaintPane(HWND hWnd, HDC hdc) {
    RECT client;
    GetClientRect(hWnd, &client);

    HBRUSH back = CreateSolidBrush(g_theme.surface);
    FillRect(hdc, &client, back);
    DeleteObject(back);

    for (int i = 0; i < g_propCount; i++) {
        HWND ctrl = g_controls[i].ctrl;
        if (!ctrl || !IsWindowVisible(ctrl)) continue;

        int kind = g_props[i].kind;
        if (kind == PK_BOOL || kind == PK_ENUM || kind == PK_FONT) continue;

        RECT rc;
        GetWindowRect(ctrl, &rc);
        MapWindowPoints(NULL, hWnd, (POINT*)&rc, 2);
        InflateRect(&rc, Scale(9), Scale(4));
        FillRounded(hdc, &rc, Scale(6), g_theme.field, g_theme.line, true);
    }

    if (PaneScrolls()) {
        RECT thumb = ScrollThumb();
        FillRounded(hdc, &thumb, Scale(SCROLLBAR_W) / 2, g_theme.line, 0, false);
    }
}

static LRESULT CALLBACK PaneProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);

            RECT client;
            GetClientRect(hWnd, &client);
            HDC buffer = CreateCompatibleDC(hdc);
            HBITMAP surface = CreateCompatibleBitmap(hdc, client.right, client.bottom);
            HBITMAP old = (HBITMAP)SelectObject(buffer, surface);

            PaintPane(hWnd, buffer);
            BitBlt(hdc, 0, 0, client.right, client.bottom, buffer, 0, 0, SRCCOPY);

            SelectObject(buffer, old);
            DeleteObject(surface);
            DeleteDC(buffer);
            EndPaint(hWnd, &ps);
            return 0;
        }

        case WM_MOUSEWHEEL: {
            int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            ScrollPaneTo(g_scrollY - delta * Scale(40) / WHEEL_DELTA);
            return 0;
        }

        case WM_LBUTTONDOWN: {
            RECT thumb = ScrollThumb();
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            if (!PaneScrolls()) return 0;
            if (PtInRect(&thumb, pt)) {
                g_thumbGrab = pt.y - thumb.top;
                SetCapture(hWnd);
            }
            return 0;
        }

        case WM_MOUSEMOVE: {
            if (GetCapture() != hWnd) return 0;
            RECT pane;
            GetClientRect(hWnd, &pane);
            RECT thumb = ScrollThumb();

            int visible = pane.bottom - pane.top;
            int track = visible - Scale(8);
            int height = thumb.bottom - thumb.top;
            int travel = track - height;
            if (travel <= 0) return 0;

            int offset = GET_Y_LPARAM(lParam) - g_thumbGrab - (pane.top + Scale(4));
            ScrollPaneTo(MulDiv(offset, g_paneHeight - visible, travel));
            return 0;
        }

        case WM_LBUTTONUP:
            if (GetCapture() == hWnd) ReleaseCapture();
            return 0;

        /* Commands from the hosted controls belong to the main window. */
        case WM_COMMAND:
        case WM_HSCROLL:
        case WM_DRAWITEM:
        case WM_MEASUREITEM:
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORLISTBOX:
            return SendMessageA(g_window, msg, wParam, lParam);
    }
    return DefWindowProcA(hWnd, msg, wParam, lParam);
}

/* ─────────────────────────── control creation ─────────────────────── */

static void AddTip(HWND control, const char* text) {
    if (!g_tips || !control || !text || !text[0]) return;

    TOOLINFOA info;
    memset(&info, 0, sizeof(info));
    info.cbSize   = sizeof(info);
    info.uFlags   = TTF_IDISHWND | TTF_SUBCLASS;
    info.hwnd     = GetParent(control);
    info.uId      = (UINT_PTR)control;
    info.lpszText = (char*)text;
    SendMessageA(g_tips, TTM_ADDTOOLA, 0, (LPARAM)&info);
}

static void CreatePropControls(void) {
    for (int i = 0; i < g_propCount; i++) {
        const PropDef* prop = &g_props[i];
        PropControls* row = &g_controls[i];
        memset(row, 0, sizeof(*row));

        row->label = Make("STATIC", prop->label, SS_LEFT | SS_CENTERIMAGE, 0, g_pane, 0);

        switch (prop->kind) {
            case PK_ENUM:
                ChoiceFill(&g_choice[i], prop->options);
                row->ctrl = Make("BUTTON", "", BS_OWNERDRAW | WS_TABSTOP, 0,
                                 g_pane, PROP_ID(i, SUB_CTRL));
                break;

            case PK_FONT:
                /* A blank family means "inherit", so it has to be offerable. */
                g_choice[i].font_preview = true;
                g_choice[i].owns_items = false;
                ChoiceAdd(&g_choice[i], "(none)");
                for (int f = 0; f < g_fontCount; f++) ChoiceAdd(&g_choice[i], g_fonts[f]);
                row->ctrl = Make("BUTTON", "", BS_OWNERDRAW | WS_TABSTOP, 0,
                                 g_pane, PROP_ID(i, SUB_CTRL));
                break;

            case PK_BOOL:
                row->ctrl = Make("BUTTON", "", BS_OWNERDRAW | WS_TABSTOP,
                                 0, g_pane, PROP_ID(i, SUB_CTRL));
                break;

            case PK_COLOR:
                row->swatch = Make("BUTTON", "", BS_OWNERDRAW | WS_TABSTOP, 0,
                                   g_pane, PROP_ID(i, SUB_SWATCH));
                row->ctrl = Make("EDIT", "", ES_AUTOHSCROLL | WS_TABSTOP, 0,
                                 g_pane, PROP_ID(i, SUB_CTRL));
                row->slider = Make(SLIDER_CLASS, "", WS_TABSTOP, 0,
                                   g_pane, PROP_ID(i, SUB_SLIDER));
                break;

            case PK_FILE:
                row->ctrl = Make("EDIT", "", ES_AUTOHSCROLL | WS_TABSTOP, 0,
                                 g_pane, PROP_ID(i, SUB_CTRL));
                row->browse = Make("BUTTON", "Browse", BS_OWNERDRAW | WS_TABSTOP, 0,
                                   g_pane, PROP_ID(i, SUB_BROWSE));
                break;

            default:
                row->ctrl = Make("EDIT", "", ES_AUTOHSCROLL | WS_TABSTOP, 0,
                                 g_pane, PROP_ID(i, SUB_CTRL));
                break;
        }

        AddTip(row->ctrl, prop->help);
        AddTip(row->label, prop->help);
        ShowRow(row, false);
    }
}

static void CreateChrome(void) {
    g_list = Make("LISTBOX", "",
                  LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | WS_VSCROLL | WS_TABSTOP,
                  0, g_window, IDC_LIST);

    Make("BUTTON", "Add",    BS_OWNERDRAW | WS_TABSTOP, 0, g_window, IDC_ADD);
    Make("BUTTON", "Copy",   BS_OWNERDRAW | WS_TABSTOP, 0, g_window, IDC_DUPLICATE);
    Make("BUTTON", "Delete", BS_OWNERDRAW | WS_TABSTOP, 0, g_window, IDC_REMOVE);

    g_name = Make("EDIT", "", ES_AUTOHSCROLL | WS_TABSTOP, 0, g_window, IDC_NAME);

    g_pane = CreateWindowExA(0, PANE_CLASS, "", WS_CHILD | WS_VISIBLE,
                             0, 0, 10, 10, g_window, (HMENU)IDC_PANE, g_instance, NULL);

    g_preview = CreateWindowExA(0, "LiteWidgetsPreview", "", WS_CHILD | WS_VISIBLE,
                                0, 0, 10, 10, g_window, (HMENU)IDC_PREVIEW, g_instance, NULL);

    g_arrange = Make("BUTTON", "Arrange on desktop", BS_OWNERDRAW | WS_TABSTOP, 0,
                     g_window, IDC_ARRANGE);
    Make("BUTTON", "Close", BS_OWNERDRAW | WS_TABSTOP, 0, g_window, IDC_CLOSE);
    Make("BUTTON", "Apply", BS_OWNERDRAW | WS_TABSTOP, 0, g_window, IDC_APPLY);
}

/* ─────────────────────────── window layout ─────────────────────────── */

typedef struct { RECT sidebar, header, pane, preview, footer; } Frame;

static Frame ComputeFrame(int width, int height) {
    Frame f;
    int pad     = Scale(14);
    int sidebar = Scale(210);
    int preview = Scale(290);
    int footer  = Scale(58);

    SetRect(&f.sidebar, pad, pad, pad + sidebar, height - footer);
    SetRect(&f.preview, width - pad - preview, pad, width - pad, height - footer);

    int contentLeft  = f.sidebar.right + pad;
    int contentRight = f.preview.left - pad;
    if (contentRight - contentLeft < Scale(300)) contentRight = contentLeft + Scale(300);

    SetRect(&f.header, contentLeft, pad, contentRight, pad + Scale(56));

    /*
     * The pills wrap, so how much room they need is only known after they are
     * laid out. Everything below them follows from that.
     */
    LayoutPills(contentLeft, contentRight, f.header.bottom + Scale(12));
    SetRect(&f.pane, contentLeft, f.header.bottom + Scale(12) + PillsHeight() + Scale(12),
            contentRight, height - footer);
    SetRect(&f.footer, pad, height - footer, width - pad, height - pad);
    return f;
}

static void LayoutWindow(void) {
    RECT client;
    GetClientRect(g_window, &client);
    Frame f = ComputeFrame(client.right, client.bottom);

    int pad = Scale(10);
    int buttonH = Scale(30);

    int listBottom = f.sidebar.bottom - buttonH - pad;
    Place(g_list, f.sidebar.left + Scale(6), f.sidebar.top + Scale(38),
          f.sidebar.right - f.sidebar.left - Scale(12), listBottom - f.sidebar.top - Scale(38));

    int span = f.sidebar.right - f.sidebar.left - Scale(12);
    int buttonW = (span - Scale(12)) / 3;
    int by = listBottom + Scale(4);
    Place(GetDlgItem(g_window, IDC_ADD), f.sidebar.left + Scale(6), by, buttonW, buttonH);
    Place(GetDlgItem(g_window, IDC_DUPLICATE), f.sidebar.left + Scale(6) + buttonW + Scale(6),
          by, buttonW, buttonH);
    Place(GetDlgItem(g_window, IDC_REMOVE),
          f.sidebar.left + Scale(6) + (buttonW + Scale(6)) * 2, by, buttonW, buttonH);

    Place(g_name, f.header.left + Scale(23), f.header.top + Scale(26),
          f.header.right - f.header.left - Scale(46), Scale(20));

    Place(g_pane, f.pane.left, f.pane.top,
          f.pane.right - f.pane.left, f.pane.bottom - f.pane.top);

    Place(g_preview, f.preview.left + Scale(10), f.preview.top + Scale(38),
          f.preview.right - f.preview.left - Scale(20),
          f.preview.bottom - f.preview.top - Scale(152));

    int fy = f.footer.top + Scale(12);
    Place(g_arrange, f.footer.left, fy, Scale(170), Scale(32));
    Place(GetDlgItem(g_window, IDC_APPLY), f.footer.right - Scale(210), fy, Scale(96), Scale(32));
    Place(GetDlgItem(g_window, IDC_CLOSE), f.footer.right - Scale(104), fy, Scale(96), Scale(32));

    RelayoutPane();
    InvalidateRect(g_window, NULL, TRUE);
}

/* Pills are drawn straight onto the window; only their rects are stored. */
static void PaintPills(HDC hdc) {
    for (int i = 0; i < g_pageCount; i++) {
        bool active = (i == g_activePage);
        bool hot = (i == g_hotPage);
        COLORREF fill = active ? g_theme.accent : (hot ? g_theme.field_hot : g_theme.field);
        COLORREF ink  = active ? g_theme.on_accent : g_theme.text;

        FillRounded(hdc, &g_pageRects[i], (g_pageRects[i].bottom - g_pageRects[i].top) / 2,
                    fill, g_theme.line, !active);
        DrawLabel(hdc, &g_pageRects[i], Spec_GroupName(g_pages[i]), ink,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE, active ? g_fontBold : g_font);
    }
}

static void PaintWindow(HDC hdc) {
    RECT client;
    GetClientRect(g_window, &client);
    Frame f = ComputeFrame(client.right, client.bottom);

    HBRUSH back = CreateSolidBrush(g_theme.window);
    FillRect(hdc, &client, back);
    DeleteObject(back);

    FillRounded(hdc, &f.sidebar, Scale(10), g_theme.surface, g_theme.line, true);
    FillRounded(hdc, &f.header,  Scale(10), g_theme.surface, g_theme.line, true);
    FillRounded(hdc, &f.preview, Scale(10), g_theme.surface, g_theme.line, true);

    FillRounded(hdc, &f.pane, Scale(10), g_theme.surface, g_theme.line, true);

    RECT title = { f.sidebar.left + Scale(14), f.sidebar.top + Scale(12),
                   f.sidebar.right - Scale(10), f.sidebar.top + Scale(30) };
    DrawLabel(hdc, &title, "WIDGETS", g_theme.dim, DT_LEFT | DT_VCENTER | DT_SINGLELINE,
              g_fontSmall);

    RECT nameLabel = { f.header.left + Scale(16), f.header.top + Scale(8),
                       f.header.right - Scale(10), f.header.top + Scale(22) };
    DrawLabel(hdc, &nameLabel, "SECTION NAME", g_theme.dim,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE, g_fontSmall);

    /* The name box shares the pane's field styling. */
    RECT nameField;
    GetWindowRect(g_name, &nameField);
    MapWindowPoints(NULL, g_window, (POINT*)&nameField, 2);
    InflateRect(&nameField, Scale(9), Scale(4));
    FillRounded(hdc, &nameField, Scale(6), g_theme.field, g_theme.line, true);

    RECT previewTitle = { f.preview.left + Scale(14), f.preview.top + Scale(12),
                          f.preview.right - Scale(10), f.preview.top + Scale(30) };
    DrawLabel(hdc, &previewTitle, "LIVE PREVIEW", g_theme.dim,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE, g_fontSmall);

    RECT note = { f.preview.left + Scale(14), f.preview.bottom - Scale(96),
                  f.preview.right - Scale(14), f.preview.bottom - Scale(10) };
    DrawLabel(hdc, &note,
              "Colours are AARRGGBB, and the slider beside one sets its alpha.\r\n"
              "Arrange on desktop drags widgets where they will live, snapping to "
              "an 8px grid; hold Shift for finer control.",
              g_theme.dim, DT_LEFT | DT_WORDBREAK, g_fontSmall);

    PaintPills(hdc);
}

/* ─────────────────────────── commands ─────────────────────────── */

static void ApplyChanges(void) {
    CollectSelection();
    SaveEntries();
    Config_Reload(g_iniPath, g_instance);
    RefreshList();
}

static void UpdateArrangeButton(void) {
    SetWindowTextA(g_arrange, Widget_EditMode() ? "Finish arranging" : "Arrange on desktop");
    InvalidateRect(g_arrange, NULL, TRUE);
}

static void ToggleArrange(void) {
    if (Widget_EditMode()) {
        Widget_SavePositions(g_iniPath);
        Widget_SetEditMode(false);
        Settings_NotifyConfigChanged();
    } else {
        ApplyChanges();      /* arrange the widgets the user is actually editing */
        Widget_SetEditMode(true);
    }
    UpdateArrangeButton();
}

static void OnPropCommand(int id, int code) {
    int index = PROP_INDEX(id);
    if (index < 0 || index >= g_propCount) return;
    int sub = PROP_SUB(id);

    if (sub == SUB_SWATCH) { PickColor(index); RefreshPreview(); return; }
    if (sub == SUB_BROWSE) { PickFile(index);  RefreshPreview(); return; }

    if (code == EN_CHANGE) {
        if (g_suppressSync) return;
        if (g_props[index].kind == PK_COLOR) SyncColorRow(index);
        RefreshPreview();
        return;
    }

    if (code == BN_CLICKED) {
        int kind = g_props[index].kind;
        if (kind == PK_ENUM || kind == PK_FONT) {
            OpenMenu(index);
            return;
        }
        if (kind == PK_BOOL) SetToggle(g_controls[index].ctrl,
                                       !ToggleState(g_controls[index].ctrl));
        RefreshPreview();
    }
}

/* Picking from a dropdown can change what the whole page is showing. */
static void OnChoiceChanged(int index) {
    if (_stricmp(g_props[index].key, "type") == 0) {
        CollectSelection();
        PopulateSelection();
        RefreshList();
        return;
    }
    if (_stricmp(g_props[index].key, "preset") == 0)
        ApplyPresetToControls(ChoiceValue(&g_choice[index]));
    RefreshPreview();
}

static bool ConfirmDiscard(void) {
    CollectSelection();
    if (!g_dirty) return true;

    int answer = MessageBoxA(g_window, "Apply your changes before closing?",
                             "LiteWidgets", MB_YESNOCANCEL | MB_ICONQUESTION);
    if (answer == IDCANCEL) return false;
    if (answer == IDYES) ApplyChanges();
    return true;
}

/* ─────────────────────────── window procedure ─────────────────────── */

static void OnPillClick(int x, int y) {
    POINT pt = { x, y };
    for (int i = 0; i < g_pageCount; i++) {
        if (!PtInRect(&g_pageRects[i], pt)) continue;
        if (i == g_activePage) return;
        g_activePage = i;
        g_scrollY = 0;
        RelayoutPane();
        InvalidateRect(g_window, NULL, FALSE);
        return;
    }
}

static LRESULT CALLBACK SettingsProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);

            RECT client;
            GetClientRect(hWnd, &client);
            HDC buffer = CreateCompatibleDC(hdc);
            HBITMAP surface = CreateCompatibleBitmap(hdc, client.right, client.bottom);
            HBITMAP old = (HBITMAP)SelectObject(buffer, surface);

            PaintWindow(buffer);
            BitBlt(hdc, 0, 0, client.right, client.bottom, buffer, 0, 0, SRCCOPY);

            SelectObject(buffer, old);
            DeleteObject(surface);
            DeleteDC(buffer);
            EndPaint(hWnd, &ps);
            return 0;
        }

        case WM_SIZE:
            if (g_pane) LayoutWindow();
            return 0;

        case WM_GETMINMAXINFO: {
            MINMAXINFO* info = (MINMAXINFO*)lParam;
            info->ptMinTrackSize.x = Scale(860);
            info->ptMinTrackSize.y = Scale(560);
            return 0;
        }

        case WM_LBUTTONDOWN:
            OnPillClick(LOWORD(lParam), HIWORD(lParam));
            return 0;

        case WM_MOUSEMOVE: {
            POINT pt = { LOWORD(lParam), HIWORD(lParam) };
            int hot = -1;
            for (int i = 0; i < g_pageCount; i++)
                if (PtInRect(&g_pageRects[i], pt)) { hot = i; break; }
            if (hot != g_hotPage) {
                g_hotPage = hot;
                InvalidateRect(hWnd, NULL, FALSE);
            }
            return 0;
        }

        case WM_CTLCOLOREDIT: {
            HDC hdc = (HDC)wParam;
            SetBkMode(hdc, OPAQUE);
            SetBkColor(hdc, g_theme.field);
            SetTextColor(hdc, g_theme.text);
            return (LRESULT)g_brushField;
        }

        case WM_CTLCOLORLISTBOX: {
            HDC hdc = (HDC)wParam;
            SetBkColor(hdc, g_theme.field);
            SetTextColor(hdc, g_theme.text);
            return (LRESULT)g_brushField;
        }

        case WM_CTLCOLORSTATIC: {
            HDC hdc = (HDC)wParam;
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, g_theme.dim);
            return (LRESULT)((HWND)lParam && GetParent((HWND)lParam) == g_pane
                             ? g_brushSurface : g_brushWindow);
        }

        case WM_MEASUREITEM: {
            MEASUREITEMSTRUCT* item = (MEASUREITEMSTRUCT*)lParam;
            item->itemHeight = (UINT)Scale(30);
            return TRUE;
        }

        case WM_DRAWITEM: {
            DRAWITEMSTRUCT* item = (DRAWITEMSTRUCT*)lParam;
            int id = (int)item->CtlID;

            if (item->CtlType == ODT_LISTBOX) { DrawListItem(item); return TRUE; }

            if (id >= PROP_ID_BASE) {
                int index = PROP_INDEX(id);
                if (index < 0 || index >= g_propCount) return TRUE;

                switch (PROP_SUB(id)) {
                    case SUB_SWATCH: DrawSwatch(item, index); return TRUE;
                    case SUB_BROWSE: DrawPushButton(item, false); return TRUE;
                    default: break;
                }
                int kind = g_props[index].kind;
                if (kind == PK_ENUM || kind == PK_FONT) DrawChoiceField(item, index);
                else                                    DrawToggle(item);
                return TRUE;
            }
            DrawPushButton(item, id == IDC_APPLY);
            return TRUE;
        }

        case WM_HSCROLL: {
            HWND bar = (HWND)lParam;
            if (!bar) break;
            int id = GetDlgCtrlID(bar);
            if (id < PROP_ID_BASE || PROP_SUB(id) != SUB_SLIDER) break;

            int index = PROP_INDEX(id);
            int alpha = (int)GetWindowLongPtrA(bar, GWLP_USERDATA);
            SetRowColor(index, Style_WithAlpha(RowColor(index), (BYTE)alpha));
            RefreshPreview();
            return 0;
        }

        case WM_COMMAND: {
            int id = LOWORD(wParam);
            int code = HIWORD(wParam);

            if (id >= PROP_ID_BASE) { OnPropCommand(id, code); return 0; }

            switch (id) {
                case IDC_LIST:
                    if (code == LBN_SELCHANGE)
                        SelectEntry((int)SendMessageA(g_list, LB_GETCURSEL, 0, 0));
                    return 0;
                case IDC_NAME:
                    if (code == EN_CHANGE && !g_suppressSync) {
                        CollectSelection();
                        RefreshList();
                    }
                    return 0;
                case IDC_ADD:       AddEntry(false); return 0;
                case IDC_DUPLICATE: AddEntry(true);  return 0;
                case IDC_REMOVE:    RemoveEntry();   return 0;
                case IDC_APPLY:     ApplyChanges();  return 0;
                case IDC_ARRANGE:   ToggleArrange(); return 0;
                case IDC_CLOSE:     SendMessageA(hWnd, WM_CLOSE, 0, 0); return 0;
                default: break;
            }
            break;
        }

        case WM_TIMER:
            if (wParam == IDT_PREVIEW) { RefreshPreview(); return 0; }
            break;

        case WM_CLOSE:
            if (!ConfirmDiscard()) return 0;
            DestroyWindow(hWnd);
            return 0;

        case WM_DESTROY:
            KillTimer(hWnd, IDT_PREVIEW);
            if (g_font)      { DeleteObject(g_font);      g_font = NULL; }
            if (g_fontBold)  { DeleteObject(g_fontBold);  g_fontBold = NULL; }
            if (g_fontSmall) { DeleteObject(g_fontSmall); g_fontSmall = NULL; }
            if (g_brushWindow)  { DeleteObject(g_brushWindow);  g_brushWindow = NULL; }
            if (g_brushSurface) { DeleteObject(g_brushSurface); g_brushSurface = NULL; }
            if (g_brushField)   { DeleteObject(g_brushField);   g_brushField = NULL; }
            for (int i = 0; i < g_propCount; i++) {
                if (g_choice[i].owns_items)
                    for (int k = 0; k < g_choice[i].count; k++) free(g_choice[i].items[k]);
                free(g_choice[i].items);
                memset(&g_choice[i], 0, sizeof(g_choice[i]));
            }
            g_window = NULL;
            g_pane = NULL;
            g_selected = -1;
            return 0;
    }
    return DefWindowProcA(hWnd, msg, wParam, lParam);
}

/* ─────────────────────────── public API ─────────────────────────── */

void Settings_NotifyConfigChanged(void) {
    if (!g_window) return;
    int previous = g_selected;
    LoadEntries();
    g_selected = (previous >= 0 && previous < g_count) ? previous : (g_count ? 0 : -1);
    RefreshList();
    if (g_selected >= 0) PopulateSelection();
    if (g_arrange) UpdateArrangeButton();
}

static void RegisterClasses(void) {
    static bool registered = false;
    if (registered) return;

    WNDCLASSA wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc   = SettingsProc;
    wc.hInstance     = g_instance;
    wc.lpszClassName = "LiteWidgetsSettings";
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon         = LoadIconA(g_instance, MAKEINTRESOURCEA(IDI_APP));
    RegisterClassA(&wc);

    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc   = PaneProc;
    wc.hInstance     = g_instance;
    wc.lpszClassName = PANE_CLASS;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    RegisterClassA(&wc);

    memset(&wc, 0, sizeof(wc));
    wc.style         = CS_DROPSHADOW;
    wc.lpfnWndProc   = MenuProc;
    wc.hInstance     = g_instance;
    wc.lpszClassName = MENU_CLASS;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    RegisterClassA(&wc);

    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc   = SliderProc;
    wc.hInstance     = g_instance;
    wc.lpszClassName = SLIDER_CLASS;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    RegisterClassA(&wc);

    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc   = PreviewProc;
    wc.hInstance     = g_instance;
    wc.lpszClassName = "LiteWidgetsPreview";
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    RegisterClassA(&wc);

    registered = true;
}

void Settings_Open(HINSTANCE hInstance, const char* iniPath) {
    if (g_window) {
        ShowWindow(g_window, SW_RESTORE);
        SetForegroundWindow(g_window);
        return;
    }

    g_instance = hInstance;
    strncpy(g_iniPath, iniPath, MAX_PATH - 1);
    g_iniPath[MAX_PATH - 1] = '\0';

    g_props = Spec_Properties(&g_propCount);
    if (g_propCount > MAX_PROPS) g_propCount = MAX_PROPS;

    INITCOMMONCONTROLSEX icc;
    icc.dwSize = sizeof(icc);
    icc.dwICC  = ICC_STANDARD_CLASSES | ICC_BAR_CLASSES;
    InitCommonControlsEx(&icc);

    LoadTheme();
    RegisterClasses();
    DetectDpi(NULL);
    LoadInstalledFonts();

    g_font      = MakeFont(10, FW_NORMAL);
    g_fontBold  = MakeFont(10, FW_SEMIBOLD);
    g_fontSmall = MakeFont(8, FW_NORMAL);

    g_brushWindow  = CreateSolidBrush(g_theme.window);
    g_brushSurface = CreateSolidBrush(g_theme.surface);
    g_brushField   = CreateSolidBrush(g_theme.field);

    int clientW = Scale(1060);
    int clientH = Scale(700);

    RECT frame = { 0, 0, clientW, clientH };
    DWORD style = WS_OVERLAPPEDWINDOW;
    AdjustWindowRect(&frame, style, FALSE);

    int windowW = frame.right - frame.left;
    int windowH = frame.bottom - frame.top;
    int x = (GetSystemMetrics(SM_CXSCREEN) - windowW) / 2;
    int y = (GetSystemMetrics(SM_CYSCREEN) - windowH) / 2;

    g_window = CreateWindowExA(0, "LiteWidgetsSettings", "LiteWidgets Settings",
                               style, x, y, windowW, windowH,
                               NULL, NULL, hInstance, NULL);
    if (!g_window) return;

    DetectDpi(g_window);
    ApplyDarkTitleBar(g_window);

    g_tips = CreateWindowExA(WS_EX_TOPMOST, TOOLTIPS_CLASSA, NULL,
                             WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
                             0, 0, 0, 0, g_window, NULL, hInstance, NULL);
    if (g_tips) SendMessageA(g_tips, TTM_SETMAXTIPWIDTH, 0, Scale(320));

    CreateChrome();
    CreatePropControls();

    LoadEntries();
    g_selected = g_count > 0 ? 0 : -1;
    RefreshList();
    RebuildPages(SelectedType());
    LayoutWindow();
    if (g_selected >= 0) PopulateSelection();
    UpdateArrangeButton();

    /* Keeps the preview clock ticking while the editor is open. */
    SetTimer(g_window, IDT_PREVIEW, 1000, NULL);

    ShowWindow(g_window, SW_SHOW);
    UpdateWindow(g_window);
}
