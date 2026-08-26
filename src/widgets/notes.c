#include "notes.h"
#include "../widget.h"
#include "../drawing.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <shlwapi.h>

typedef struct {
    Widget base;
    WidgetStyle style;
    WCHAR* text_buffer;
} NotesWidgetData;

static void Notes_Render(Widget* base, GpGraphics* gfx, int width, int height) {
    NotesWidgetData* w = (NotesWidgetData*)base;
    const WidgetStyle* s = &w->style;

    /* Background */
    Drawing_WidgetBackground(gfx, s, width, height);

    /* Text */
    if (w->text_buffer) {
        float pad = s->padding;
        GpRectF layoutRect = { pad, pad, (float)width - pad * 2, (float)height - pad * 2 };
        Drawing_Text(gfx, w->text_buffer, &layoutRect,
                     s->font_family, s->font_size, s->font_style,
                     s->text_color, s->align_h, s->align_v);
    }
}

static void Notes_Destroy(Widget* base) {
    NotesWidgetData* w = (NotesWidgetData*)base;
    if (w->text_buffer) {
        free(w->text_buffer);
    }
    free(w);
}

static const WidgetVtable notes_vtable = {
    Notes_Render,
    NULL,
    Notes_Destroy
};

/*
 * Resolve a potentially relative path against the INI file's directory.
 * If pathA is already absolute, use it directly.
 * Otherwise, combine it with the directory containing the INI file.
 */
static void ResolvePathRelativeToIni(const char* iniPathA, const char* pathA,
                                     WCHAR* outW, DWORD maxLen) {
    WCHAR inputW[MAX_PATH];
    MultiByteToWideChar(CP_UTF8, 0, pathA, -1, inputW, MAX_PATH);

    /* If already absolute, use directly */
    if (!PathIsRelativeW(inputW)) {
        wcsncpy(outW, inputW, maxLen);
        return;
    }

    /* Get INI file's directory */
    WCHAR iniDirW[MAX_PATH];
    MultiByteToWideChar(CP_UTF8, 0, iniPathA, -1, iniDirW, MAX_PATH);
    PathRemoveFileSpecW(iniDirW);

    /* Combine: iniDir + relative path */
    PathCombineW(outW, iniDirW, inputW);
}

static WCHAR* LoadFileToWideString(const WCHAR* path) {
    FILE* f = _wfopen(path, L"rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    if (fsize <= 0) { fclose(f); return NULL; }
    fseek(f, 0, SEEK_SET);

    char* buf = (char*)malloc(fsize + 1);
    if (!buf) { fclose(f); return NULL; }
    fread(buf, 1, fsize, f);
    buf[fsize] = '\0';
    fclose(f);

    /* Skip UTF-8 BOM if present */
    char* start = buf;
    if (fsize >= 3 && (unsigned char)buf[0] == 0xEF &&
        (unsigned char)buf[1] == 0xBB && (unsigned char)buf[2] == 0xBF) {
        start = buf + 3;
    }

    int wchars = MultiByteToWideChar(CP_UTF8, 0, start, -1, NULL, 0);
    WCHAR* wbuf = (WCHAR*)malloc(wchars * sizeof(WCHAR));
    if (wbuf) {
        MultiByteToWideChar(CP_UTF8, 0, start, -1, wbuf, wchars);
    }
    free(buf);
    return wbuf;
}

bool NotesWidget_Create(HINSTANCE hInstance, const char* iniPath,
                        int x, int y, int width, int height,
                        bool click_through, const char* path,
                        const WidgetStyle* style) {
    NotesWidgetData* w = (NotesWidgetData*)calloc(1, sizeof(NotesWidgetData));
    if (!w) return false;

    w->style = *style;

    /* Resolve file path relative to INI directory */
    WCHAR resolvedPath[MAX_PATH];
    ResolvePathRelativeToIni(iniPath, path, resolvedPath, MAX_PATH);
    w->text_buffer = LoadFileToWideString(resolvedPath);

    if (!Widget_Init(&w->base, hInstance, &notes_vtable, x, y, width, height, 0, click_through)) {
        if (w->text_buffer) free(w->text_buffer);
        free(w);
        return false;
    }

    /* CRITICAL: Initial render for layered window */
    Widget_Render(&w->base);
    return true;
}
