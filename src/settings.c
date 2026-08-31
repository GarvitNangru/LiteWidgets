#include "settings.h"

#include "config.h"
#include "spec.h"
#include "style.h"
#include "widget.h"

#include <commctrl.h>
#include <commdlg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ─────────────────────────── constants ─────────────────────────── */

#define MAX_PROPS      96
#define VALUE_LEN      160

#define IDC_LIST       100
#define IDC_TAB        101
#define IDC_ADD        102
#define IDC_DUPLICATE  103
#define IDC_REMOVE     104
#define IDC_APPLY      105
#define IDC_CLOSE      106
#define IDC_ARRANGE    107
#define IDC_NAME       108
#define IDC_PREVIEW    109

/* Every property owns four consecutive ids: control, swatch, slider, browse. */
#define PROP_ID_BASE   1000
#define PROP_ID(i, s)  (PROP_ID_BASE + (i) * 4 + (s))
#define PROP_INDEX(id) (((id) - PROP_ID_BASE) / 4)
#define PROP_SUB(id)   (((id) - PROP_ID_BASE) % 4)

enum { SUB_CTRL = 0, SUB_SWATCH, SUB_SLIDER, SUB_BROWSE };

#define IDT_PREVIEW    1

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

static HINSTANCE     g_instance = NULL;
static HWND          g_window   = NULL;
static HWND          g_list     = NULL;
static HWND          g_tab      = NULL;
static HWND          g_name     = NULL;
static HWND          g_preview  = NULL;
static HWND          g_arrange  = NULL;
static HFONT         g_font     = NULL;
static HBRUSH        g_backBrush = NULL;

static char          g_iniPath[MAX_PATH];
static Entry         g_entries[LW_MAX_WIDGETS];
static int           g_count = 0;
static int           g_selected = -1;
static bool          g_suppressSync = false;

static const PropDef* g_props = NULL;
static int            g_propCount = 0;
static PropControls   g_controls[MAX_PROPS];

/* Tab pages currently on screen, as group ids. */
static int g_pages[PG__COUNT];
static int g_pageCount = 0;
static int g_activePage = 0;

static int g_dpi = 96;

static COLORREF g_customColors[16];

/* ─────────────────────────── helpers ─────────────────────────── */

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

static HFONT CreateUiFont(void) {
    NONCLIENTMETRICSA metrics;
    metrics.cbSize = sizeof(metrics);
    if (SystemParametersInfoA(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0)) {
        metrics.lfMessageFont.lfHeight = -MulDiv(9, g_dpi, 72);
        return CreateFontIndirectA(&metrics.lfMessageFont);
    }
    return CreateFontA(-MulDiv(9, g_dpi, 72), 0, 0, 0, FW_NORMAL, 0, 0, 0,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH, "Segoe UI");
}

static HWND Make(const char* cls, const char* text, DWORD style, DWORD exStyle,
                 int x, int y, int w, int h, HWND parent, int id) {
    HWND hWnd = CreateWindowExA(exStyle, cls, text, style | WS_CHILD | WS_VISIBLE,
                                x, y, w, h, parent, (HMENU)(INT_PTR)id, g_instance, NULL);
    if (hWnd) SendMessageA(hWnd, WM_SETFONT, (WPARAM)g_font, TRUE);
    return hWnd;
}

/* Property rows start hidden; ShowActivePage reveals the ones that apply. */
static void HideRow(const PropControls* row) {
    if (row->label)  ShowWindow(row->label,  SW_HIDE);
    if (row->ctrl)   ShowWindow(row->ctrl,   SW_HIDE);
    if (row->swatch) ShowWindow(row->swatch, SW_HIDE);
    if (row->slider) ShowWindow(row->slider, SW_HIDE);
    if (row->browse) ShowWindow(row->browse, SW_HIDE);
}

static void FillCombo(HWND combo, const char* options) {
    if (!options) return;

    /* "@presets" expands to the built-in theme list. */
    if (strcmp(options, "@presets") == 0) {
        SendMessageA(combo, CB_ADDSTRING, 0, (LPARAM)"");
        int count = 0;
        const StylePreset* presets = Style_Presets(&count);
        for (int i = 0; i < count; i++)
            SendMessageA(combo, CB_ADDSTRING, 0, (LPARAM)presets[i].name);
        return;
    }

    char buffer[256];
    strncpy(buffer, options, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';

    char* context = NULL;
    for (char* token = strtok_s(buffer, "|", &context); token;
         token = strtok_s(NULL, "|", &context))
        SendMessageA(combo, CB_ADDSTRING, 0, (LPARAM)token);
}

static void ComboSelect(HWND combo, const char* value) {
    int index = (int)SendMessageA(combo, CB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)value);
    SendMessageA(combo, CB_SETCURSEL, index >= 0 ? index : 0, 0);
}

static void ComboText(HWND combo, char* out, int cap) {
    out[0] = '\0';
    int index = (int)SendMessageA(combo, CB_GETCURSEL, 0, 0);
    if (index < 0) return;
    int length = (int)SendMessageA(combo, CB_GETLBTEXTLEN, index, 0);
    if (length < 0 || length >= cap) return;
    SendMessageA(combo, CB_GETLBTEXT, index, (LPARAM)out);
}

static int FindProp(const char* key) {
    for (int i = 0; i < g_propCount; i++)
        if (_stricmp(g_props[i].key, key) == 0) return i;
    return -1;
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
            GetPrivateProfileStringA(p, g_props[i].key, "", entry->values[i], VALUE_LEN, g_iniPath);

        g_count++;
    }
}

/*
 * A field with no explicit value in the INI still has an effective value: the
 * one its preset supplies, or the built-in default. Showing that -- rather
 * than the bare default -- is what makes the preset visible in the editor,
 * and comparing against it on save is what keeps the INI free of redundant
 * keys.
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

static const char* EffectiveDefault(const char* preset, int index) {
    const char* value = PresetValueFor(preset, g_props[index].key);
    return value ? value : g_props[index].def;
}

/* The preset as it currently reads on screen, falling back to the entry. */
static void CurrentPreset(const Entry* entry, char* out, int cap) {
    out[0] = '\0';
    int index = FindProp("preset");
    if (index < 0) return;

    if (g_controls[index].ctrl) ComboText(g_controls[index].ctrl, out, cap);
    else if (entry) strncpy(out, entry->values[index], (size_t)cap - 1);
}

static int EntryType(const Entry* entry) {
    int index = FindProp("type");
    if (index < 0) return WIDGET_CLOCK;
    int type = Spec_ParseType(entry->values[index]);
    return type < 0 ? WIDGET_CLOCK : type;
}

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
            WritePrivateProfileStringA(entry->section, g_props[i].key, entry->values[i], g_iniPath);
        }
    }
    WritePrivateProfileStringA(NULL, NULL, NULL, g_iniPath);   /* flush */
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

/* ─────────────────────────── colour rows ─────────────────────────── */

static ARGB RowColor(int index) {
    char text[VALUE_LEN] = { 0 };
    GetWindowTextA(g_controls[index].ctrl, text, VALUE_LEN);
    if (!text[0]) return Style_ParseColor(g_props[index].def, 0);
    return Style_ParseColor(text, 0);
}

static void SyncColorRow(int index) {
    ARGB color = RowColor(index);
    if (g_controls[index].slider)
        SendMessageA(g_controls[index].slider, TBM_SETPOS, TRUE, (LPARAM)((color >> 24) & 0xFF));
    if (g_controls[index].swatch)
        InvalidateRect(g_controls[index].swatch, NULL, TRUE);
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

static void ApplySliderAlpha(int index) {
    int alpha = (int)SendMessageA(g_controls[index].slider, TBM_GETPOS, 0, 0);
    SetRowColor(index, Style_WithAlpha(RowColor(index), (BYTE)alpha));
}

/* ─────────────────────────── font & file pickers ─────────────────────────── */

static const char* CompanionSizeKey(const char* fontKey) {
    if (_stricmp(fontKey, "font_family") == 0)      return "font_size";
    if (_stricmp(fontKey, "date_font_family") == 0) return "date_font_size";
    return NULL;
}

static void PickFont(int index) {
    char family[LW_FONT_LEN] = { 0 };
    GetWindowTextA(g_controls[index].ctrl, family, sizeof(family));

    LOGFONTA lf;
    memset(&lf, 0, sizeof(lf));
    strncpy(lf.lfFaceName, family[0] ? family : "Segoe UI", LF_FACESIZE - 1);
    lf.lfHeight = -24;
    lf.lfCharSet = DEFAULT_CHARSET;

    CHOOSEFONTA cf;
    memset(&cf, 0, sizeof(cf));
    cf.lStructSize = sizeof(cf);
    cf.hwndOwner   = g_window;
    cf.lpLogFont   = &lf;
    cf.Flags       = CF_SCREENFONTS | CF_INITTOLOGFONTSTRUCT | CF_NOSCRIPTSEL;

    if (!ChooseFontA(&cf)) return;

    SetWindowTextA(g_controls[index].ctrl, lf.lfFaceName);

    /* Carry the chosen size across to the matching *_font_size field. */
    const char* sizeKey = CompanionSizeKey(g_props[index].key);
    int sizeIndex = sizeKey ? FindProp(sizeKey) : -1;
    if (sizeIndex >= 0 && g_controls[sizeIndex].ctrl) {
        int px = lf.lfHeight < 0 ? -lf.lfHeight : lf.lfHeight;
        if (px > 0) {
            char text[16];
            _snprintf(text, sizeof(text), "%d", px);
            SetWindowTextA(g_controls[sizeIndex].ctrl, text);
        }
    }
}

static void PickFile(int index) {
    char path[MAX_PATH] = { 0 };
    GetWindowTextA(g_controls[index].ctrl, path, MAX_PATH);

    OPENFILENAMEA ofn;
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = g_window;
    ofn.lpstrFilter = "Supported files\0*.txt;*.md;*.png;*.jpg;*.jpeg;*.bmp;*.gif\0"
                      "Text files\0*.txt;*.md\0Images\0*.png;*.jpg;*.jpeg;*.bmp;*.gif\0"
                      "All files\0*.*\0";
    ofn.lpstrFile   = path;
    ofn.nMaxFile    = MAX_PATH;
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileNameA(&ofn)) SetWindowTextA(g_controls[index].ctrl, path);
}

/* ─────────────────────────── control values ─────────────────────────── */

static void ReadControl(int index, char* out, int cap) {
    out[0] = '\0';
    HWND ctrl = g_controls[index].ctrl;
    if (!ctrl) return;

    switch (g_props[index].kind) {
        case PK_ENUM:
            ComboText(ctrl, out, cap);
            break;
        case PK_BOOL:
            strncpy(out, SendMessageA(ctrl, BM_GETCHECK, 0, 0) == BST_CHECKED ? "true" : "false",
                    (size_t)cap - 1);
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
            ComboSelect(ctrl, effective);
            break;
        case PK_BOOL:
            SendMessageA(ctrl, BM_SETCHECK,
                         Style_ParseBool(effective, false) ? BST_CHECKED : BST_UNCHECKED, 0);
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
    CurrentPreset(entry, preset, VALUE_LEN);

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

        strncpy(entry->values[i], value, VALUE_LEN - 1);
        entry->values[i][VALUE_LEN - 1] = '\0';
    }

    char name[LW_SECTION_LEN] = { 0 };
    GetWindowTextA(g_name, name, sizeof(name));
    if (name[0]) {
        strncpy(entry->section, name, LW_SECTION_LEN - 1);
        entry->section[LW_SECTION_LEN - 1] = '\0';
    }
}

/* ─────────────────────────── tabs & visibility ─────────────────────────── */

static bool GroupHasVisibleProps(int group, int type) {
    for (int i = 0; i < g_propCount; i++)
        if (g_props[i].group == group && Spec_PropAppliesTo(&g_props[i], type)) return true;
    return false;
}

static void LayoutPage(int group, int type);

static void RebuildTabs(int type) {
    int previousGroup = (g_activePage < g_pageCount) ? g_pages[g_activePage] : PG_GENERAL;

    g_pageCount = 0;
    for (int group = 0; group < PG__COUNT; group++)
        if (GroupHasVisibleProps(group, type)) g_pages[g_pageCount++] = group;

    TabCtrl_DeleteAllItems(g_tab);
    for (int i = 0; i < g_pageCount; i++) {
        TCITEMA item;
        memset(&item, 0, sizeof(item));
        item.mask = TCIF_TEXT;
        item.pszText = (char*)Spec_GroupName(g_pages[i]);
        TabCtrl_InsertItem(g_tab, i, &item);
    }

    g_activePage = 0;
    for (int i = 0; i < g_pageCount; i++)
        if (g_pages[i] == previousGroup) { g_activePage = i; break; }
    TabCtrl_SetCurSel(g_tab, g_activePage);
}

static void ShowActivePage(int type) {
    int activeGroup = (g_activePage < g_pageCount) ? g_pages[g_activePage] : -1;

    for (int i = 0; i < g_propCount; i++) {
        bool visible = Spec_PropAppliesTo(&g_props[i], type) && g_props[i].group == activeGroup;
        int cmd = visible ? SW_SHOW : SW_HIDE;
        if (g_controls[i].label)  ShowWindow(g_controls[i].label,  cmd);
        if (g_controls[i].ctrl)   ShowWindow(g_controls[i].ctrl,   cmd);
        if (g_controls[i].swatch) ShowWindow(g_controls[i].swatch, cmd);
        if (g_controls[i].slider) ShowWindow(g_controls[i].slider, cmd);
        if (g_controls[i].browse) ShowWindow(g_controls[i].browse, cmd);
    }
    if (activeGroup >= 0) LayoutPage(activeGroup, type);
}

/* ─────────────────────────── preview ─────────────────────────── */

static void RefreshPreview(void) {
    if (g_preview) InvalidateRect(g_preview, NULL, FALSE);
}

static void PaintChecker(HDC hdc, const RECT* rc) {
    HBRUSH dark  = CreateSolidBrush(RGB(0x3A, 0x3A, 0x3E));
    HBRUSH light = CreateSolidBrush(RGB(0x4A, 0x4A, 0x50));
    int cell = Scale(10);

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
 * Render the widget exactly the way the desktop does — same painter, same
 * premultiplied bitmap, same alpha blend — then scale it down to fit.
 */
static void PaintPreview(HWND hWnd, HDC hdc, const RECT* client) {
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
        int boxW = client->right - client->left - Scale(8);
        int boxH = client->bottom - client->top - Scale(8);

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
    (void)hWnd;
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

            PaintPreview(hWnd, buffer, &client);
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

/* ─────────────────────────── list ─────────────────────────── */

static void RefreshList(void) {
    SendMessageA(g_list, LB_RESETCONTENT, 0, 0);
    for (int i = 0; i < g_count; i++) {
        char label[128];
        _snprintf(label, sizeof(label), "%s  -  %s",
                  g_entries[i].section, Spec_TypeName(EntryType(&g_entries[i])));
        SendMessageA(g_list, LB_ADDSTRING, 0, (LPARAM)label);
    }
    if (g_selected >= 0 && g_selected < g_count)
        SendMessageA(g_list, LB_SETCURSEL, g_selected, 0);
}

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

    RebuildTabs(type);
    ShowActivePage(type);
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
    PopulateSelection();
    RefreshList();
}

static void RemoveEntry(void) {
    if (g_selected < 0 || g_selected >= g_count) return;

    for (int i = g_selected; i < g_count - 1; i++) g_entries[i] = g_entries[i + 1];
    g_count--;

    if (g_selected >= g_count) g_selected = g_count - 1;
    RefreshList();
    if (g_selected >= 0) PopulateSelection();
    else RefreshPreview();
}

/* ─────────────────────────── presets ─────────────────────────── */

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

/* ─────────────────────────── layout ─────────────────────────── */

static RECT PageRect(void) {
    RECT rc;
    GetWindowRect(g_tab, &rc);
    MapWindowPoints(NULL, g_window, (POINT*)&rc, 2);
    TabCtrl_AdjustRect(g_tab, FALSE, &rc);
    InflateRect(&rc, -Scale(6), -Scale(4));
    return rc;
}

static void LayoutPage(int group, int type) {
    RECT page = PageRect();
    int rowH   = Scale(27);
    int labelW = Scale(78);
    int colW   = (page.right - page.left - Scale(10)) / 2;

    int column = 0;
    int y = page.top;

    for (int i = 0; i < g_propCount; i++) {
        if (g_props[i].group != group) continue;
        if (!Spec_PropAppliesTo(&g_props[i], type)) continue;

        bool wide = (g_props[i].kind == PK_FILE);
        if (wide && column == 1) { column = 0; y += rowH; }

        int x = page.left + column * (colW + Scale(10));
        int fieldX = x + labelW;
        int fieldW = (wide ? (page.right - page.left) : colW) - labelW;
        int ctrlH  = Scale(21);

        if (g_controls[i].label)
            SetWindowPos(g_controls[i].label, NULL, x, y + Scale(4), labelW - Scale(4), Scale(16),
                         SWP_NOZORDER | SWP_NOACTIVATE);

        switch (g_props[i].kind) {
            case PK_COLOR: {
                int swatchW = Scale(22);
                int sliderW = Scale(72);
                int editW   = fieldW - swatchW - sliderW - Scale(8);
                SetWindowPos(g_controls[i].swatch, NULL, fieldX, y, swatchW, ctrlH,
                             SWP_NOZORDER | SWP_NOACTIVATE);
                SetWindowPos(g_controls[i].ctrl, NULL, fieldX + swatchW + Scale(4), y, editW, ctrlH,
                             SWP_NOZORDER | SWP_NOACTIVATE);
                SetWindowPos(g_controls[i].slider, NULL,
                             fieldX + swatchW + editW + Scale(8), y, sliderW, ctrlH,
                             SWP_NOZORDER | SWP_NOACTIVATE);
                break;
            }
            case PK_FONT: {
                int buttonW = Scale(28);
                SetWindowPos(g_controls[i].ctrl, NULL, fieldX, y, fieldW - buttonW - Scale(4), ctrlH,
                             SWP_NOZORDER | SWP_NOACTIVATE);
                SetWindowPos(g_controls[i].browse, NULL,
                             fieldX + fieldW - buttonW, y, buttonW, ctrlH,
                             SWP_NOZORDER | SWP_NOACTIVATE);
                break;
            }
            case PK_FILE: {
                int buttonW = Scale(72);
                SetWindowPos(g_controls[i].ctrl, NULL, fieldX, y, fieldW - buttonW - Scale(6), ctrlH,
                             SWP_NOZORDER | SWP_NOACTIVATE);
                SetWindowPos(g_controls[i].browse, NULL,
                             fieldX + fieldW - buttonW, y, buttonW, ctrlH,
                             SWP_NOZORDER | SWP_NOACTIVATE);
                break;
            }
            case PK_BOOL:
                SetWindowPos(g_controls[i].ctrl, NULL, fieldX, y, fieldW, ctrlH,
                             SWP_NOZORDER | SWP_NOACTIVATE);
                break;
            case PK_ENUM:
                /* Combos size their own list; height is the dropped height. */
                SetWindowPos(g_controls[i].ctrl, NULL, fieldX, y, fieldW, Scale(200),
                             SWP_NOZORDER | SWP_NOACTIVATE);
                break;
            default:
                SetWindowPos(g_controls[i].ctrl, NULL, fieldX, y, fieldW, ctrlH,
                             SWP_NOZORDER | SWP_NOACTIVATE);
                break;
        }

        if (wide) { y += rowH; column = 0; }
        else if (++column >= 2) { column = 0; y += rowH; }
    }
}

static void CreatePropControls(void) {
    for (int i = 0; i < g_propCount; i++) {
        const PropDef* prop = &g_props[i];
        PropControls* row = &g_controls[i];
        memset(row, 0, sizeof(*row));

        if (prop->kind != PK_BOOL)
            row->label = Make("STATIC", prop->label, SS_LEFT, 0, 0, 0, 10, 10,
                              g_window, 0);

        switch (prop->kind) {
            case PK_ENUM:
                row->ctrl = Make("COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
                                 0, 0, 0, 10, 200, g_window, PROP_ID(i, SUB_CTRL));
                FillCombo(row->ctrl, prop->options);
                break;

            case PK_BOOL:
                row->ctrl = Make("BUTTON", prop->label, BS_AUTOCHECKBOX | WS_TABSTOP,
                                 0, 0, 0, 10, 10, g_window, PROP_ID(i, SUB_CTRL));
                break;

            case PK_COLOR:
                row->swatch = Make("BUTTON", "", BS_OWNERDRAW | WS_TABSTOP, 0, 0, 0, 10, 10,
                                   g_window, PROP_ID(i, SUB_SWATCH));
                row->ctrl = Make("EDIT", "", ES_AUTOHSCROLL | WS_TABSTOP, WS_EX_CLIENTEDGE,
                                 0, 0, 10, 10, g_window, PROP_ID(i, SUB_CTRL));
                row->slider = Make(TRACKBAR_CLASSA, "", TBS_HORZ | TBS_NOTICKS | WS_TABSTOP,
                                   0, 0, 0, 10, 10, g_window, PROP_ID(i, SUB_SLIDER));
                SendMessageA(row->slider, TBM_SETRANGE, TRUE, MAKELPARAM(0, 255));
                break;

            case PK_FONT:
                row->ctrl = Make("EDIT", "", ES_AUTOHSCROLL | WS_TABSTOP, WS_EX_CLIENTEDGE,
                                 0, 0, 10, 10, g_window, PROP_ID(i, SUB_CTRL));
                row->browse = Make("BUTTON", "...", BS_PUSHBUTTON | WS_TABSTOP, 0,
                                   0, 0, 10, 10, g_window, PROP_ID(i, SUB_BROWSE));
                break;

            case PK_FILE:
                row->ctrl = Make("EDIT", "", ES_AUTOHSCROLL | WS_TABSTOP, WS_EX_CLIENTEDGE,
                                 0, 0, 10, 10, g_window, PROP_ID(i, SUB_CTRL));
                row->browse = Make("BUTTON", "Browse...", BS_PUSHBUTTON | WS_TABSTOP, 0,
                                   0, 0, 10, 10, g_window, PROP_ID(i, SUB_BROWSE));
                break;

            default:
                row->ctrl = Make("EDIT", "", ES_AUTOHSCROLL | WS_TABSTOP, WS_EX_CLIENTEDGE,
                                 0, 0, 10, 10, g_window, PROP_ID(i, SUB_CTRL));
                break;
        }

        HideRow(row);
    }
}

static void CreateChrome(int clientW, int clientH) {
    int pad     = Scale(12);
    int listW   = Scale(200);
    int barH    = Scale(46);
    int buttonH = Scale(26);
    int listH   = clientH - Scale(34) - barH - Scale(38);

    Make("STATIC", "Widgets", SS_LEFT, 0, pad, Scale(14), listW, Scale(16), g_window, 0);
    g_list = Make("LISTBOX", "", LBS_NOTIFY | WS_VSCROLL | WS_TABSTOP, WS_EX_CLIENTEDGE,
                  pad, Scale(34), listW, listH, g_window, IDC_LIST);

    int buttonW = (listW - Scale(8)) / 3;
    int buttonY = Scale(34) + listH + Scale(6);
    Make("BUTTON", "Add", BS_PUSHBUTTON | WS_TABSTOP, 0,
         pad, buttonY, buttonW, buttonH, g_window, IDC_ADD);
    Make("BUTTON", "Copy", BS_PUSHBUTTON | WS_TABSTOP, 0,
         pad + buttonW + Scale(4), buttonY, buttonW, buttonH, g_window, IDC_DUPLICATE);
    Make("BUTTON", "Delete", BS_PUSHBUTTON | WS_TABSTOP, 0,
         pad + (buttonW + Scale(4)) * 2, buttonY, buttonW, buttonH, g_window, IDC_REMOVE);

    int rightX = pad + listW + Scale(12);
    int previewW = Scale(262);
    int previewX = clientW - pad - previewW;
    int tabW = previewX - rightX - Scale(12);

    Make("STATIC", "Name", SS_LEFT, 0, rightX, Scale(16), Scale(38), Scale(16), g_window, 0);
    g_name = Make("EDIT", "", ES_AUTOHSCROLL | WS_TABSTOP, WS_EX_CLIENTEDGE,
                  rightX + Scale(40), Scale(12), tabW - Scale(40), Scale(21), g_window, IDC_NAME);

    g_tab = Make(WC_TABCONTROLA, "", WS_TABSTOP, 0,
                 rightX, Scale(42), tabW, clientH - Scale(42) - barH, g_window, IDC_TAB);

    Make("STATIC", "Live preview", SS_LEFT, 0,
         previewX, Scale(16), previewW, Scale(16), g_window, 0);
    int previewH = clientH - Scale(38) - barH - Scale(120);
    g_preview = CreateWindowExA(WS_EX_CLIENTEDGE, "LiteWidgetsPreview", "",
                                WS_CHILD | WS_VISIBLE,
                                previewX, Scale(38), previewW, previewH,
                                g_window, (HMENU)IDC_PREVIEW, g_instance, NULL);

    Make("STATIC",
         "Drag widgets straight on the desktop with Arrange, then finish to save "
         "their positions.\r\n\r\nColours are AARRGGBB. The slider on the right "
         "sets the alpha channel.",
         SS_LEFT, 0, previewX, Scale(38) + previewH + Scale(12), previewW, Scale(100),
         g_window, 0);

    int barY = clientH - barH + Scale(8);
    g_arrange = Make("BUTTON", "Arrange on desktop", BS_PUSHBUTTON | WS_TABSTOP, 0,
                     pad, barY, Scale(150), Scale(28), g_window, IDC_ARRANGE);
    Make("BUTTON", "Close", BS_PUSHBUTTON | WS_TABSTOP, 0,
         clientW - pad - Scale(90), barY, Scale(90), Scale(28), g_window, IDC_CLOSE);
    Make("BUTTON", "Apply", BS_DEFPUSHBUTTON | WS_TABSTOP, 0,
         clientW - pad - Scale(188), barY, Scale(90), Scale(28), g_window, IDC_APPLY);
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
    if (sub == SUB_BROWSE) {
        if (g_props[index].kind == PK_FONT) PickFont(index);
        else                                PickFile(index);
        RefreshPreview();
        return;
    }

    if (code == EN_CHANGE) {
        if (g_suppressSync) return;
        if (g_props[index].kind == PK_COLOR) SyncColorRow(index);
        RefreshPreview();
        return;
    }

    if (code == CBN_SELCHANGE) {
        if (_stricmp(g_props[index].key, "type") == 0) {
            CollectSelection();
            PopulateSelection();
            RefreshList();
            return;
        }
        if (_stricmp(g_props[index].key, "preset") == 0) {
            char name[VALUE_LEN];
            ComboText(g_controls[index].ctrl, name, VALUE_LEN);
            ApplyPresetToControls(name);
        }
        RefreshPreview();
        return;
    }

    if (code == BN_CLICKED) RefreshPreview();
}

/* ─────────────────────────── window procedure ─────────────────────────── */

static LRESULT CALLBACK SettingsProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CTLCOLORSTATIC:
            SetBkMode((HDC)wParam, TRANSPARENT);
            return (LRESULT)g_backBrush;

        case WM_DRAWITEM: {
            DRAWITEMSTRUCT* item = (DRAWITEMSTRUCT*)lParam;
            int index = PROP_INDEX((int)item->CtlID);
            if (index < 0 || index >= g_propCount) break;

            ARGB color = RowColor(index);
            HBRUSH brush = CreateSolidBrush(RGB((color >> 16) & 0xFF,
                                                (color >> 8) & 0xFF, color & 0xFF));
            FillRect(item->hDC, &item->rcItem, brush);
            DeleteObject(brush);
            FrameRect(item->hDC, &item->rcItem, (HBRUSH)GetStockObject(GRAY_BRUSH));
            if (item->itemState & ODS_FOCUS) DrawFocusRect(item->hDC, &item->rcItem);
            return TRUE;
        }

        case WM_HSCROLL: {
            HWND bar = (HWND)lParam;
            if (!bar) break;
            int id = GetDlgCtrlID(bar);
            if (id < PROP_ID_BASE || PROP_SUB(id) != SUB_SLIDER) break;
            ApplySliderAlpha(PROP_INDEX(id));
            RefreshPreview();
            return 0;
        }

        case WM_NOTIFY: {
            NMHDR* header = (NMHDR*)lParam;
            if (header->idFrom == IDC_TAB && header->code == TCN_SELCHANGE) {
                g_activePage = TabCtrl_GetCurSel(g_tab);
                if (g_selected >= 0) ShowActivePage(EntryType(&g_entries[g_selected]));
                return 0;
            }
            break;
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
                case IDC_CLOSE:     DestroyWindow(hWnd); return 0;
                default: break;
            }
            break;
        }

        case WM_TIMER:
            if (wParam == IDT_PREVIEW) { RefreshPreview(); return 0; }
            break;

        case WM_CLOSE:
            DestroyWindow(hWnd);
            return 0;

        case WM_DESTROY:
            KillTimer(hWnd, IDT_PREVIEW);
            if (g_font) { DeleteObject(g_font); g_font = NULL; }
            if (g_backBrush) { DeleteObject(g_backBrush); g_backBrush = NULL; }
            g_window = NULL;
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
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
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
    icc.dwICC  = ICC_TAB_CLASSES | ICC_BAR_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    RegisterClasses();
    DetectDpi(NULL);
    g_font = CreateUiFont();
    g_backBrush = CreateSolidBrush(GetSysColor(COLOR_BTNFACE));

    int clientW = Scale(1024);
    int clientH = Scale(672);

    RECT frame = { 0, 0, clientW, clientH };
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    AdjustWindowRect(&frame, style, FALSE);

    int windowW = frame.right - frame.left;
    int windowH = frame.bottom - frame.top;
    int x = (GetSystemMetrics(SM_CXSCREEN) - windowW) / 2;
    int y = (GetSystemMetrics(SM_CYSCREEN) - windowH) / 2;

    g_window = CreateWindowExA(0, "LiteWidgetsSettings", "LiteWidgets Settings",
                               style, x, y, windowW, windowH,
                               NULL, NULL, hInstance, NULL);
    if (!g_window) return;

    CreateChrome(clientW, clientH);
    CreatePropControls();

    LoadEntries();
    g_selected = g_count > 0 ? 0 : -1;
    RefreshList();
    if (g_selected >= 0) PopulateSelection();
    else RebuildTabs(WIDGET_CLOCK);
    UpdateArrangeButton();

    /* Keeps the preview clock ticking while the editor is open. */
    SetTimer(g_window, IDT_PREVIEW, 1000, NULL);

    ShowWindow(g_window, SW_SHOW);
    UpdateWindow(g_window);
}
