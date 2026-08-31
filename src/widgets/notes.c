#include "notes.h"

#include "../widget.h"
#include "../drawing.h"
#include "../config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    Widget     base;
    WidgetSpec spec;
    WCHAR*     text;
    WCHAR      path[MAX_PATH];
    FILETIME   stamp;
} NotesWidget;

/* ─────────────────────────── loading ─────────────────────────── */

static bool FileStamp(const WCHAR* path, FILETIME* out) {
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExW(path, GetFileExInfoStandard, &data)) return false;
    *out = data.ftLastWriteTime;
    return true;
}

WCHAR* Notes_LoadText(const WCHAR* path) {
    if (!path || !path[0]) return NULL;

    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return NULL;

    LARGE_INTEGER size;
    if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 || size.QuadPart > (8 << 20)) {
        CloseHandle(file);
        return NULL;
    }

    DWORD bytes = (DWORD)size.QuadPart;
    char* raw = (char*)malloc(bytes + 2);
    if (!raw) { CloseHandle(file); return NULL; }

    DWORD read = 0;
    BOOL ok = ReadFile(file, raw, bytes, &read, NULL);
    CloseHandle(file);
    if (!ok) { free(raw); return NULL; }
    raw[read] = '\0';
    raw[read + 1] = '\0';

    WCHAR* text = NULL;

    /* UTF-16LE with a BOM: already wide, just copy past the marker. */
    if (read >= 2 && (unsigned char)raw[0] == 0xFF && (unsigned char)raw[1] == 0xFE) {
        size_t chars = (read - 2) / sizeof(WCHAR);
        text = (WCHAR*)malloc((chars + 1) * sizeof(WCHAR));
        if (text) {
            memcpy(text, raw + 2, chars * sizeof(WCHAR));
            text[chars] = L'\0';
        }
    } else {
        const char* start = raw;
        if (read >= 3 && (unsigned char)raw[0] == 0xEF &&
            (unsigned char)raw[1] == 0xBB && (unsigned char)raw[2] == 0xBF)
            start = raw + 3;

        int chars = MultiByteToWideChar(CP_UTF8, 0, start, -1, NULL, 0);
        if (chars > 0) {
            text = (WCHAR*)malloc((size_t)chars * sizeof(WCHAR));
            if (text) MultiByteToWideChar(CP_UTF8, 0, start, -1, text, chars);
        }
    }

    free(raw);
    return text;
}

/* ─────────────────────────── painting ─────────────────────────── */

void Notes_Paint(const WidgetSpec* spec, const WCHAR* text,
                 GpGraphics* gfx, int width, int height) {
    if (!spec || !gfx) return;

    const WidgetStyle* s = &spec->style;
    Drawing_Surface(gfx, s, (float)width, (float)height);
    if (!text || !text[0]) return;

    float pad = s->padding;
    GpRectF box = { pad, pad, (float)width - pad * 2.0f, (float)height - pad * 2.0f };
    if (box.Width <= 0.0f || box.Height <= 0.0f) return;

    WCHAR* shaped = NULL;
    const WCHAR* display = text;
    if (s->text_transform != TEXT_AS_IS) {
        size_t len = wcslen(text) + 1;
        shaped = (WCHAR*)malloc(len * sizeof(WCHAR));
        if (shaped) display = Drawing_Transform(shaped, len, text, s->text_transform);
    }

    TextRun run = Drawing_Run(s, display, box);
    Drawing_Text(gfx, &run);
    free(shaped);
}

/* ─────────────────────────── widget plumbing ─────────────────────────── */

static void Notes_Render(Widget* base, GpGraphics* gfx, int width, int height) {
    NotesWidget* w = (NotesWidget*)base;
    Notes_Paint(&w->spec, w->text, gfx, width, height);
}

/* Poll the file stamp rather than the contents: one stat call, no parsing. */
static void Notes_OnTimer(Widget* base) {
    NotesWidget* w = (NotesWidget*)base;

    FILETIME stamp;
    if (!FileStamp(w->path, &stamp)) return;
    if (CompareFileTime(&stamp, &w->stamp) == 0) return;

    w->stamp = stamp;
    WCHAR* fresh = Notes_LoadText(w->path);
    if (!fresh) return;

    free(w->text);
    w->text = fresh;
    base->needs_render = true;
}

static void Notes_Destroy(Widget* base) {
    NotesWidget* w = (NotesWidget*)base;
    free(w->text);
    free(w);
}

static const WidgetVtable kNotesVtable = {
    Notes_Render,
    Notes_OnTimer,
    NULL,
    Notes_Destroy
};

bool NotesWidget_Create(HINSTANCE hInstance, const char* iniPath, const WidgetSpec* spec) {
    NotesWidget* w = (NotesWidget*)calloc(1, sizeof(NotesWidget));
    if (!w) return false;

    w->spec = *spec;
    Config_ResolvePath(iniPath, spec->path, w->path, MAX_PATH);
    w->text = Notes_LoadText(w->path);
    FileStamp(w->path, &w->stamp);

    /* Only widgets that opt into reloading get a timer at all. */
    UINT interval = (spec->reload_seconds > 0) ? (UINT)spec->reload_seconds * 1000u : 0u;
    if (!Widget_Init(&w->base, hInstance, &kNotesVtable, spec, interval)) {
        free(w->text);
        free(w);
        return false;
    }

    Widget_Render(&w->base);
    return true;
}
