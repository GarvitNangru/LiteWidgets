#include "notes.h"

#include "../widget.h"
#include "../drawing.h"
#include "../config.h"

#include <shellapi.h>
#include <windowsx.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wctype.h>

/*
 * A note you cannot type into is just a file viewer, so this widget carries a
 * small text editor: click it and write. The editing model is deliberately
 * plain -- one buffer, a caret, a selection -- but it does the things muscle
 * memory expects, including shift-selection, the clipboard and undo.
 *
 * Text is laid out here rather than handed to GDI+ with a wrapping string
 * format, because the caret has to land exactly where the glyphs did. Wrapping
 * ourselves means one set of line breaks shared by drawing, hit testing and
 * caret movement.
 */

#define MAX_LINES   1024
#define MAX_CHARS   (1 << 20)
#define UNDO_DEPTH  32
#define UNDO_COALESCE_MS 700
#define AUTOSAVE_MS 1500

typedef struct {
    int   start[MAX_LINES + 1];   /* start[count] is the end of the text */
    int   count;
    float line_height;
    float x, y, width;            /* origin of the first line */
} NotesLayout;

typedef struct {
    Widget     base;
    WidgetSpec spec;
    char       ini[MAX_PATH];
    WCHAR      path[MAX_PATH];
    FILETIME   stamp;

    WCHAR* text;
    int    length;
    int    capacity;

    bool   editing;
    bool   selecting;
    bool   caret_on;
    bool   dirty;
    int    caret;
    int    anchor;                /* selection anchor; == caret means none */
    float  goal_x;                /* remembered column for up/down */

    WCHAR* undo[UNDO_DEPTH];
    int    undo_caret[UNDO_DEPTH];
    int    undo_count;
    DWORD  undo_stamp;
    int    undo_op;

    /* A 1x1 surface, only so there is something to measure text against. */
    GpBitmap*   scratch;
    GpGraphics* scratch_gfx;
} NotesWidget;

enum { OP_NONE = 0, OP_INSERT, OP_DELETE };

/* ─────────────────────────── file i/o ─────────────────────────── */

static bool FileStamp(const WCHAR* path, FILETIME* out) {
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExW(path, GetFileExInfoStandard, &data)) return false;
    *out = data.ftLastWriteTime;
    return true;
}

/* CRLF collapses to LF on the way in; the editor only ever sees '\n'. */
static void Normalise(WCHAR* text) {
    WCHAR* out = text;
    for (const WCHAR* in = text; *in; in++) {
        if (*in == L'\r' && in[1] == L'\n') continue;
        *out++ = (*in == L'\r') ? L'\n' : *in;
    }
    *out = L'\0';
}

WCHAR* Notes_LoadText(const WCHAR* path) {
    if (!path || !path[0]) return NULL;

    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return NULL;

    LARGE_INTEGER size;
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 || size.QuadPart > (8 << 20)) {
        CloseHandle(file);
        return NULL;
    }

    DWORD bytes = (DWORD)size.QuadPart;
    char* raw = (char*)malloc((size_t)bytes + 2);
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
    if (text) Normalise(text);
    return text;
}

/* Written back as UTF-8 with CRLF, so Notepad and git both behave. */
static bool SaveText(const WCHAR* path, const WCHAR* text) {
    if (!path || !path[0]) return false;

    int length = text ? (int)wcslen(text) : 0;
    WCHAR* crlf = (WCHAR*)malloc(((size_t)length * 2 + 1) * sizeof(WCHAR));
    if (!crlf) return false;

    int n = 0;
    for (int i = 0; i < length; i++) {
        if (text[i] == L'\n') crlf[n++] = L'\r';
        crlf[n++] = text[i];
    }
    crlf[n] = L'\0';

    int bytes = WideCharToMultiByte(CP_UTF8, 0, crlf, n, NULL, 0, NULL, NULL);
    char* utf8 = (bytes > 0) ? (char*)malloc((size_t)bytes) : NULL;
    if (bytes > 0 && !utf8) { free(crlf); return false; }
    if (bytes > 0) WideCharToMultiByte(CP_UTF8, 0, crlf, n, utf8, bytes, NULL, NULL);
    free(crlf);

    HANDLE file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) { free(utf8); return false; }

    DWORD written = 0;
    if (bytes > 0) WriteFile(file, utf8, (DWORD)bytes, &written, NULL);
    CloseHandle(file);
    free(utf8);
    return true;
}

/* ─────────────────────────── layout ─────────────────────────── */

static void ApplyTransform(const WidgetSpec* spec, WCHAR* text) {
    /* Case transforms preserve length, so caret indices still line up. */
    if (spec->style.text_transform == TEXT_UPPER)      CharUpperW(text);
    else if (spec->style.text_transform == TEXT_LOWER) CharLowerW(text);
}

/*
 * Break `text` into visual lines.
 *
 * Hard breaks win; inside a paragraph the longest prefix that fits is found by
 * bisection and then walked back to the last space, so words stay whole unless
 * a single word is wider than the widget.
 */
static void BuildLayout(GpGraphics* gfx, const WidgetSpec* spec, const WCHAR* text,
                        int width, int height, NotesLayout* out) {
    memset(out, 0, sizeof(*out));

    const WidgetStyle* s = &spec->style;
    float pad = s->padding;
    out->x     = pad;
    out->width = (float)width - pad * 2.0f;
    if (out->width <= 1.0f) out->width = 1.0f;

    GpRectF box = { pad, pad, out->width, (float)height - pad * 2.0f };
    TextRun run = Drawing_Run(s, text, box);
    DrawingMetrics* m = Drawing_OpenMetrics(gfx, &run);
    if (!m) {
        out->line_height = s->font_size * 1.25f;
        out->start[0] = 0;
        out->start[1] = 0;
        out->count = 1;
        out->y = pad;
        return;
    }
    out->line_height = Drawing_LineHeight(m);

    int length = text ? (int)wcslen(text) : 0;
    int i = 0;

    while (out->count < MAX_LINES) {
        out->start[out->count++] = i;
        if (i >= length) break;      /* the empty line after a final newline */

        int hard = i;
        while (hard < length && text[hard] != L'\n') hard++;

        int fit = hard - i;
        if (fit > 0 && Drawing_Extent(m, text + i, fit) > out->width) {
            int lo = 1, hi = fit;
            while (lo < hi) {
                int mid = (lo + hi + 1) / 2;
                if (Drawing_Extent(m, text + i, mid) <= out->width) lo = mid;
                else hi = mid - 1;
            }
            fit = lo;

            /* Prefer a word boundary, but never produce an empty line. */
            for (int k = fit; k > 0; k--)
                if (text[i + k - 1] == L' ') { fit = k; break; }
        }

        i += fit;
        if (i == hard) {
            if (hard >= length) break;    /* that was the last line */
            i++;                          /* step over the newline */
        }
    }
    out->start[out->count] = length;

    Drawing_CloseMetrics(m);

    /*
     * Vertical alignment applies to the block as a whole, but a note taller
     * than its widget always starts at the top so the beginning is readable.
     */
    float total = out->line_height * (float)out->count;
    out->y = box.Y;
    if (total < box.Height) {
        if (s->align_v == ALIGN_CENTER)   out->y = box.Y + (box.Height - total) * 0.5f;
        else if (s->align_v == ALIGN_FAR) out->y = box.Y + box.Height - total;
    }
}

static int LineAt(const NotesLayout* layout, int index) {
    for (int i = 0; i < layout->count; i++)
        if (index < layout->start[i + 1] ||
            (i == layout->count - 1 && index <= layout->start[i + 1]))
            return i;
    return layout->count > 0 ? layout->count - 1 : 0;
}

/* Left edge of a line once horizontal alignment has been applied. */
static float LineOriginX(const NotesLayout* layout, const WidgetSpec* spec,
                         DrawingMetrics* m, const WCHAR* text, int line) {
    int from = layout->start[line];
    int len  = layout->start[line + 1] - from;
    if (len > 0 && text[from + len - 1] == L'\n') len--;

    if (spec->style.align_h == ALIGN_NEAR) return layout->x;
    float w = Drawing_Extent(m, text + from, len);
    if (spec->style.align_h == ALIGN_CENTER) return layout->x + (layout->width - w) * 0.5f;
    return layout->x + layout->width - w;
}

/* ─────────────────────────── painting ─────────────────────────── */

#define CARET_COLOR   0xFF4CC2FF
#define SELECT_COLOR  0x554CC2FF

static void PaintLines(GpGraphics* gfx, const WidgetSpec* spec, const WCHAR* text,
                       const NotesLayout* layout, DrawingMetrics* m,
                       int selStart, int selEnd, int caret, bool caretOn) {
    /* A caret index on a wrap boundary belongs to exactly one line. */
    int caretLine = caretOn ? LineAt(layout, caret) : -1;
    const WidgetStyle* s = &spec->style;

    WCHAR* line = (WCHAR*)malloc(((size_t)wcslen(text) + 2) * sizeof(WCHAR));
    if (!line) return;

    for (int i = 0; i < layout->count; i++) {
        int from = layout->start[i];
        int len  = layout->start[i + 1] - from;
        if (len > 0 && text[from + len - 1] == L'\n') len--;

        float y = layout->y + layout->line_height * (float)i;
        float x = LineOriginX(layout, spec, m, text, i);

        /* Selection sits behind the glyphs it covers. */
        if (selEnd > selStart) {
            int a = selStart > from ? selStart : from;
            int b = selEnd < from + len ? selEnd : from + len;
            if (b > a) {
                float ax = x + Drawing_Extent(m, text + from, a - from);
                float bx = x + Drawing_Extent(m, text + from, b - from);
                GpSolidFill* brush = NULL;
                if (GdipCreateSolidFill(SELECT_COLOR, &brush) == 0) {
                    GdipFillRectangle(gfx, (GpBrush*)brush, ax, y,
                                      bx - ax, layout->line_height);
                    GdipDeleteBrush((GpBrush*)brush);
                }
            }
        }

        if (len > 0) {
            memcpy(line, text + from, (size_t)len * sizeof(WCHAR));
            line[len] = L'\0';

            GpRectF bounds = { x, y, layout->width + 2.0f, layout->line_height };
            TextRun run = Drawing_Run(s, line, bounds);
            run.align_h = ALIGN_NEAR;   /* the origin already carries alignment */
            run.align_v = ALIGN_NEAR;
            run.no_wrap = true;
            Drawing_Text(gfx, &run);
        }

        if (i == caretLine) {
            float cx = x + Drawing_Extent(m, text + from, caret - from);
            GpSolidFill* brush = NULL;
            if (GdipCreateSolidFill(CARET_COLOR, &brush) == 0) {
                GdipFillRectangle(gfx, (GpBrush*)brush, cx, y + 1.0f,
                                  1.5f, layout->line_height - 2.0f);
                GdipDeleteBrush((GpBrush*)brush);
            }
        }
    }
    free(line);
}

/* Shown instead of an empty note, so the widget explains what to do with it. */
static void PaintHint(GpGraphics* gfx, const WidgetSpec* spec, int width, int height,
                      const WCHAR* message) {
    const WidgetStyle* s = &spec->style;
    float pad = s->padding;
    GpRectF box = { pad, pad, (float)width - pad * 2.0f, (float)height - pad * 2.0f };
    if (box.Width <= 0.0f || box.Height <= 0.0f) return;

    TextRun run = Drawing_Run(s, message, box);
    run.color    = Style_ScaleAlpha(s->text_color, 0.45f);
    run.color2   = 0;
    run.gradient = GRAD_NONE;
    run.align_h  = ALIGN_CENTER;
    run.align_v  = ALIGN_CENTER;
    Drawing_Text(gfx, &run);
}

/* Full painter. `caret` < 0 means "not being edited". */
static void PaintNote(const WidgetSpec* spec, const WCHAR* text, int caret, int anchor,
                      bool caretOn, GpGraphics* gfx, int width, int height) {
    if (!spec || !gfx) return;

    const WidgetStyle* s = &spec->style;
    Drawing_Surface(gfx, s, (float)width, (float)height);

    bool editing = caret >= 0;
    if ((!text || !text[0]) && !editing) {
        PaintHint(gfx, spec, width, height, L"Click to write a note");
        return;
    }
    if (!text) return;

    size_t length = wcslen(text);
    WCHAR* display = (WCHAR*)malloc((length + 1) * sizeof(WCHAR));
    if (!display) return;
    memcpy(display, text, (length + 1) * sizeof(WCHAR));
    ApplyTransform(spec, display);

    NotesLayout layout;
    BuildLayout(gfx, spec, display, width, height, &layout);

    GpRectF box = { s->padding, s->padding, layout.width, (float)height - s->padding * 2.0f };
    TextRun probe = Drawing_Run(s, display, box);
    DrawingMetrics* m = Drawing_OpenMetrics(gfx, &probe);
    if (m) {
        int selStart = caret < anchor ? caret : anchor;
        int selEnd   = caret < anchor ? anchor : caret;
        if (!editing) { selStart = 0; selEnd = 0; }
        PaintLines(gfx, spec, display, &layout, m, selStart, selEnd, caret, editing && caretOn);
        Drawing_CloseMetrics(m);
    }
    free(display);

    /* A thin accent frame is the only chrome that says "you are typing here". */
    if (editing) {
        Drawing_Panel(gfx, 0, 0, GRAD_NONE, CARET_COLOR, 1.0f,
                      0.5f, 0.5f, (float)width - 1.0f, (float)height - 1.0f,
                      s->corner_radius);
    }
}

void Notes_Paint(const WidgetSpec* spec, const WCHAR* text,
                 GpGraphics* gfx, int width, int height) {
    PaintNote(spec, text, -1, -1, false, gfx, width, height);
}

/* ─────────────────────────── buffer ─────────────────────────── */

static bool Reserve(NotesWidget* w, int chars) {
    if (chars + 1 <= w->capacity) return true;
    int capacity = w->capacity ? w->capacity : 256;
    while (capacity < chars + 1) capacity *= 2;
    if (capacity > MAX_CHARS) return false;

    WCHAR* grown = (WCHAR*)realloc(w->text, (size_t)capacity * sizeof(WCHAR));
    if (!grown) return false;
    if (!w->text) grown[0] = L'\0';
    w->text = grown;
    w->capacity = capacity;
    return true;
}

static void AdoptText(NotesWidget* w, WCHAR* text) {
    free(w->text);
    w->text = text;
    w->length = text ? (int)wcslen(text) : 0;
    w->capacity = w->length + 1;
    if (w->caret > w->length)  w->caret = w->length;
    if (w->anchor > w->length) w->anchor = w->length;
}

static void ClearUndo(NotesWidget* w) {
    for (int i = 0; i < w->undo_count; i++) free(w->undo[i]);
    w->undo_count = 0;
    w->undo_op = OP_NONE;
}

/*
 * One snapshot per burst of typing rather than per keystroke: a run of inserts
 * inside the coalescing window undoes as a unit, which is what an editor is
 * expected to do.
 */
static void PushUndo(NotesWidget* w, int op) {
    DWORD now = GetTickCount();
    if (w->undo_count > 0 && op == w->undo_op && now - w->undo_stamp < UNDO_COALESCE_MS) {
        w->undo_stamp = now;
        return;
    }
    w->undo_stamp = now;
    w->undo_op = op;

    WCHAR* copy = _wcsdup(w->text ? w->text : L"");
    if (!copy) return;

    if (w->undo_count == UNDO_DEPTH) {
        free(w->undo[0]);
        memmove(w->undo, w->undo + 1, (UNDO_DEPTH - 1) * sizeof(w->undo[0]));
        memmove(w->undo_caret, w->undo_caret + 1, (UNDO_DEPTH - 1) * sizeof(w->undo_caret[0]));
        w->undo_count--;
    }
    w->undo_caret[w->undo_count] = w->caret;
    w->undo[w->undo_count++] = copy;
}

static void Undo(NotesWidget* w) {
    if (w->undo_count == 0) return;
    int slot = --w->undo_count;
    AdoptText(w, w->undo[slot]);
    w->caret = w->anchor = w->undo_caret[slot];
    if (w->caret > w->length) w->caret = w->anchor = w->length;
    w->undo[slot] = NULL;
    w->undo_op = OP_NONE;
    w->dirty = true;
}

static void DeleteRange(NotesWidget* w, int from, int to) {
    if (from < 0) from = 0;
    if (to > w->length) to = w->length;
    if (to <= from) return;

    memmove(w->text + from, w->text + to, (size_t)(w->length - to + 1) * sizeof(WCHAR));
    w->length -= (to - from);
    w->caret = w->anchor = from;
    w->dirty = true;
}

static bool DeleteSelection(NotesWidget* w) {
    if (w->caret == w->anchor) return false;
    int from = w->caret < w->anchor ? w->caret : w->anchor;
    int to   = w->caret < w->anchor ? w->anchor : w->caret;
    PushUndo(w, OP_DELETE);
    DeleteRange(w, from, to);
    return true;
}

static void InsertText(NotesWidget* w, const WCHAR* insert, int count) {
    if (count <= 0) return;
    if (w->caret != w->anchor) DeleteSelection(w);
    else PushUndo(w, OP_INSERT);

    if (!Reserve(w, w->length + count)) return;
    memmove(w->text + w->caret + count, w->text + w->caret,
            (size_t)(w->length - w->caret + 1) * sizeof(WCHAR));
    memcpy(w->text + w->caret, insert, (size_t)count * sizeof(WCHAR));
    w->length += count;
    w->caret += count;
    w->anchor = w->caret;
    w->dirty = true;
}

/* ─────────────────────────── measuring outside a paint ─────────────────── */

static GpGraphics* Scratch(NotesWidget* w) {
    if (w->scratch_gfx) return w->scratch_gfx;
    if (GdipCreateBitmapFromScan0(1, 1, 0, PixelFormat32bppPARGB, NULL, &w->scratch) != 0)
        return NULL;
    if (GdipGetImageGraphicsContext((GpImage*)w->scratch, &w->scratch_gfx) != 0)
        w->scratch_gfx = NULL;
    return w->scratch_gfx;
}

/* Layout of the current buffer, for hit testing and caret movement. */
static bool CurrentLayout(NotesWidget* w, NotesLayout* layout, WCHAR** displayOut) {
    GpGraphics* gfx = Scratch(w);
    if (!gfx) return false;

    const WCHAR* text = w->text ? w->text : L"";
    size_t length = wcslen(text);
    WCHAR* display = (WCHAR*)malloc((length + 1) * sizeof(WCHAR));
    if (!display) return false;
    memcpy(display, text, (length + 1) * sizeof(WCHAR));
    ApplyTransform(&w->spec, display);

    BuildLayout(gfx, &w->spec, display, w->base.width, w->base.height, layout);
    *displayOut = display;
    return true;
}

static int IndexFromPoint(NotesWidget* w, int px, int py) {
    NotesLayout layout;
    WCHAR* display = NULL;
    if (!CurrentLayout(w, &layout, &display)) return w->caret;

    int index = w->caret;
    GpRectF box = { 0, 0, layout.width, 1.0f };
    TextRun probe = Drawing_Run(&w->spec.style, display, box);
    DrawingMetrics* m = Drawing_OpenMetrics(Scratch(w), &probe);
    if (m) {
        int line = (int)(((float)py - layout.y) / layout.line_height);
        if (line < 0) line = 0;
        if (line >= layout.count) line = layout.count - 1;

        int from = layout.start[line];
        int len  = layout.start[line + 1] - from;
        if (len > 0 && display[from + len - 1] == L'\n') len--;

        float x = LineOriginX(&layout, &w->spec, m, display, line);
        index = from;
        for (int k = 1; k <= len; k++) {
            float before = x + Drawing_Extent(m, display + from, k - 1);
            float after  = x + Drawing_Extent(m, display + from, k);
            if ((float)px < (before + after) * 0.5f) break;
            index = from + k;
        }
        Drawing_CloseMetrics(m);
    }
    free(display);
    return index;
}

/* Move the caret one visual line, keeping the column the user started from. */
static void MoveLine(NotesWidget* w, int delta, bool extend) {
    NotesLayout layout;
    WCHAR* display = NULL;
    if (!CurrentLayout(w, &layout, &display)) return;

    GpRectF box = { 0, 0, layout.width, 1.0f };
    TextRun probe = Drawing_Run(&w->spec.style, display, box);
    DrawingMetrics* m = Drawing_OpenMetrics(Scratch(w), &probe);
    if (m) {
        int line = LineAt(&layout, w->caret);
        float originX = LineOriginX(&layout, &w->spec, m, display, line);
        if (w->goal_x < 0.0f)
            w->goal_x = originX + Drawing_Extent(m, display + layout.start[line],
                                                 w->caret - layout.start[line]);

        int target = line + delta;
        if (target >= 0 && target < layout.count) {
            int from = layout.start[target];
            int len  = layout.start[target + 1] - from;
            if (len > 0 && display[from + len - 1] == L'\n') len--;

            float x = LineOriginX(&layout, &w->spec, m, display, target);
            int index = from;
            for (int k = 1; k <= len; k++) {
                float before = x + Drawing_Extent(m, display + from, k - 1);
                float after  = x + Drawing_Extent(m, display + from, k);
                if (w->goal_x < (before + after) * 0.5f) break;
                index = from + k;
            }
            w->caret = index;
        } else {
            w->caret = (delta < 0) ? 0 : w->length;
        }
        Drawing_CloseMetrics(m);
    }
    free(display);
    if (!extend) w->anchor = w->caret;
}

static void MoveHome(NotesWidget* w, bool end, bool extend) {
    NotesLayout layout;
    WCHAR* display = NULL;
    if (!CurrentLayout(w, &layout, &display)) return;

    int line = LineAt(&layout, w->caret);
    if (end) {
        int to = layout.start[line + 1];
        if (to > layout.start[line] && display[to - 1] == L'\n') to--;
        w->caret = to;
    } else {
        w->caret = layout.start[line];
    }
    free(display);
    if (!extend) w->anchor = w->caret;
    w->goal_x = -1.0f;
}

/* ─────────────────────────── clipboard ─────────────────────────── */

static void CopySelection(NotesWidget* w, bool cut) {
    if (w->caret == w->anchor) return;
    int from = w->caret < w->anchor ? w->caret : w->anchor;
    int to   = w->caret < w->anchor ? w->anchor : w->caret;
    int count = to - from;

    if (!OpenClipboard(w->base.hwnd)) return;
    EmptyClipboard();

    HGLOBAL block = GlobalAlloc(GMEM_MOVEABLE, ((size_t)count + 1) * sizeof(WCHAR));
    if (block) {
        WCHAR* dst = (WCHAR*)GlobalLock(block);
        if (dst) {
            memcpy(dst, w->text + from, (size_t)count * sizeof(WCHAR));
            dst[count] = L'\0';
            GlobalUnlock(block);
            SetClipboardData(CF_UNICODETEXT, block);
        }
    }
    CloseClipboard();

    if (cut) DeleteSelection(w);
}

static void Paste(NotesWidget* w) {
    if (!IsClipboardFormatAvailable(CF_UNICODETEXT)) return;
    if (!OpenClipboard(w->base.hwnd)) return;

    HANDLE handle = GetClipboardData(CF_UNICODETEXT);
    const WCHAR* source = handle ? (const WCHAR*)GlobalLock(handle) : NULL;
    if (source) {
        int count = (int)wcslen(source);
        WCHAR* clean = (WCHAR*)malloc(((size_t)count + 1) * sizeof(WCHAR));
        if (clean) {
            memcpy(clean, source, ((size_t)count + 1) * sizeof(WCHAR));
            Normalise(clean);
            InsertText(w, clean, (int)wcslen(clean));
            free(clean);
        }
        GlobalUnlock(handle);
    }
    CloseClipboard();
}

/* ─────────────────────────── persistence ─────────────────────────── */

/*
 * A note with no file yet gets one on the first keystroke, next to the config
 * and named after its section, and the path is written back so the choice
 * survives a restart.
 */
static bool EnsurePath(NotesWidget* w) {
    if (w->path[0]) return true;
    if (!w->ini[0] || !w->base.section[0]) return false;

    char relative[MAX_PATH];
    _snprintf(relative, MAX_PATH, "%s.txt", w->base.section);

    Config_ResolvePath(w->ini, relative, w->path, MAX_PATH);
    if (!w->path[0]) return false;

    strncpy(w->spec.path, relative, MAX_PATH - 1);
    w->spec.path[MAX_PATH - 1] = '\0';
    Config_WriteKey(w->ini, w->base.section, "path", relative);
    return true;
}

static void Commit(NotesWidget* w) {
    if (!w->dirty) return;
    if (!EnsurePath(w)) return;
    if (!SaveText(w->path, w->text ? w->text : L"")) return;

    w->dirty = false;
    FileStamp(w->path, &w->stamp);   /* our own write must not look external */
}

static void StopEditing(NotesWidget* w) {
    if (!w->editing) return;

    KillTimer(w->base.hwnd, IDT_WIDGET_USER0);
    KillTimer(w->base.hwnd, IDT_WIDGET_USER1);
    Commit(w);

    w->editing = false;
    w->selecting = false;
    w->anchor = w->caret;
    ClearUndo(w);
    Widget_SetFocusable(&w->base, false);
    w->base.needs_render = true;
}

static void StartEditing(NotesWidget* w) {
    if (w->editing) return;

    w->editing = true;
    w->caret_on = true;
    w->goal_x = -1.0f;
    if (!w->text) Reserve(w, 0);

    Widget_SetFocusable(&w->base, true);
    UINT blink = GetCaretBlinkTime();
    if (blink == 0 || blink == INFINITE) blink = 530;
    SetTimer(w->base.hwnd, IDT_WIDGET_USER0, blink, NULL);
    w->base.needs_render = true;
}

static void TouchAutosave(NotesWidget* w) {
    SetTimer(w->base.hwnd, IDT_WIDGET_USER1, AUTOSAVE_MS, NULL);
}

/* ─────────────────────────── message handling ─────────────────────────── */

static bool OnKeyDown(NotesWidget* w, WPARAM key) {
    bool shift = (GetKeyState(VK_SHIFT)   & 0x8000) != 0;
    bool ctrl  = (GetKeyState(VK_CONTROL) & 0x8000) != 0;

    if (ctrl) {
        switch (key) {
            case 'A': w->anchor = 0; w->caret = w->length; return true;
            case 'C': CopySelection(w, false); return true;
            case 'X': CopySelection(w, true);  return true;
            case 'V': Paste(w); return true;
            case 'Z': Undo(w);  return true;
            case 'S': Commit(w); return true;
            case VK_HOME: w->caret = 0;         if (!shift) w->anchor = 0;         return true;
            case VK_END:  w->caret = w->length; if (!shift) w->anchor = w->length; return true;
            default: return false;
        }
    }

    switch (key) {
        case VK_ESCAPE:
            StopEditing(w);
            return true;

        case VK_LEFT:
            if (!shift && w->caret != w->anchor)
                w->caret = w->anchor = (w->caret < w->anchor ? w->caret : w->anchor);
            else if (w->caret > 0) w->caret--;
            if (!shift) w->anchor = w->caret;
            w->goal_x = -1.0f;
            return true;

        case VK_RIGHT:
            if (!shift && w->caret != w->anchor)
                w->caret = w->anchor = (w->caret > w->anchor ? w->caret : w->anchor);
            else if (w->caret < w->length) w->caret++;
            if (!shift) w->anchor = w->caret;
            w->goal_x = -1.0f;
            return true;

        case VK_UP:   MoveLine(w, -1, shift); return true;
        case VK_DOWN: MoveLine(w, +1, shift); return true;
        case VK_HOME: MoveHome(w, false, shift); return true;
        case VK_END:  MoveHome(w, true,  shift); return true;

        case VK_DELETE:
            if (!DeleteSelection(w) && w->caret < w->length) {
                PushUndo(w, OP_DELETE);
                DeleteRange(w, w->caret, w->caret + 1);
            }
            TouchAutosave(w);
            return true;

        default:
            return false;
    }
}

static bool OnChar(NotesWidget* w, WPARAM code) {
    if (GetKeyState(VK_CONTROL) & 0x8000) return true;   /* handled on key down */

    WCHAR ch = (WCHAR)code;
    if (ch == L'\b') {
        if (!DeleteSelection(w) && w->caret > 0) {
            PushUndo(w, OP_DELETE);
            DeleteRange(w, w->caret - 1, w->caret);
        }
    } else if (ch == L'\r' || ch == L'\n') {
        WCHAR newline = L'\n';
        InsertText(w, &newline, 1);
    } else if (ch == L'\t') {
        InsertText(w, L"    ", 4);
    } else if (ch >= 32 || ch == 0x2028) {
        InsertText(w, &ch, 1);
    } else {
        return true;
    }

    w->goal_x = -1.0f;
    w->caret_on = true;
    TouchAutosave(w);
    return true;
}

static bool AcceptDrop(NotesWidget* w, HDROP drop) {
    WCHAR path[MAX_PATH];
    bool ok = DragQueryFileW(drop, 0, path, MAX_PATH) > 0;
    DragFinish(drop);
    if (!ok) return false;

    StopEditing(w);
    wcsncpy(w->path, path, MAX_PATH - 1);
    w->path[MAX_PATH - 1] = L'\0';

    char utf8[MAX_PATH];
    WideCharToMultiByte(CP_UTF8, 0, path, -1, utf8, MAX_PATH, NULL, NULL);
    strncpy(w->spec.path, utf8, MAX_PATH - 1);
    Config_WriteKey(w->ini, w->base.section, "path", utf8);

    AdoptText(w, Notes_LoadText(w->path));
    FileStamp(w->path, &w->stamp);
    w->dirty = false;
    return true;
}

static bool Notes_OnMessage(Widget* base, UINT msg, WPARAM wParam, LPARAM lParam,
                            LRESULT* result) {
    NotesWidget* w = (NotesWidget*)base;
    *result = 0;

    switch (msg) {
        case WM_LW_CANCEL_EDIT:
            StopEditing(w);
            return true;

        case WM_SETCURSOR:
            SetCursor(LoadCursor(NULL, IDC_IBEAM));
            *result = TRUE;
            return true;

        case WM_LBUTTONDOWN: {
            int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
            StartEditing(w);
            w->caret = IndexFromPoint(w, x, y);
            if (!(GetKeyState(VK_SHIFT) & 0x8000)) w->anchor = w->caret;
            w->goal_x = -1.0f;
            w->caret_on = true;
            w->selecting = true;
            SetCapture(base->hwnd);
            base->needs_render = true;
            return true;
        }

        case WM_MOUSEMOVE:
            if (!w->selecting) return false;
            w->caret = IndexFromPoint(w, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            base->needs_render = true;
            return true;

        case WM_LBUTTONUP:
            if (!w->selecting) return false;
            w->selecting = false;
            ReleaseCapture();
            return true;

        case WM_CAPTURECHANGED:
            w->selecting = false;
            return false;

        case WM_LBUTTONDBLCLK: {
            /* Double click selects the word under the caret. */
            if (!w->text) return true;
            int at = IndexFromPoint(w, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            int from = at, to = at;
            while (from > 0 && !iswspace(w->text[from - 1])) from--;
            while (to < w->length && !iswspace(w->text[to])) to++;
            w->anchor = from;
            w->caret = to;
            base->needs_render = true;
            return true;
        }

        case WM_KEYDOWN:
            if (!w->editing) return false;
            if (!OnKeyDown(w, wParam)) return false;
            base->needs_render = true;
            return true;

        case WM_CHAR:
            if (!w->editing) return false;
            OnChar(w, wParam);
            base->needs_render = true;
            return true;

        case WM_KILLFOCUS:
            StopEditing(w);
            return true;

        case WM_DROPFILES:
            if (AcceptDrop(w, (HDROP)wParam)) base->needs_render = true;
            return true;

        case WM_TIMER:
            if (wParam == IDT_WIDGET_USER0) {
                w->caret_on = !w->caret_on;
                base->needs_render = true;
                return true;
            }
            if (wParam == IDT_WIDGET_USER1) {
                KillTimer(base->hwnd, IDT_WIDGET_USER1);
                Commit(w);
                return true;
            }
            return false;

        default:
            return false;
    }
}

/* ─────────────────────────── widget plumbing ─────────────────────────── */

static void Notes_Render(Widget* base, GpGraphics* gfx, int width, int height) {
    NotesWidget* w = (NotesWidget*)base;
    PaintNote(&w->spec, w->text ? w->text : L"",
              w->editing ? w->caret : -1, w->anchor, w->caret_on,
              gfx, width, height);
}

/* Poll the file stamp rather than the contents: one stat call, no parsing. */
static void Notes_OnTimer(Widget* base) {
    NotesWidget* w = (NotesWidget*)base;
    if (w->editing || w->dirty) return;   /* never clobber unsaved edits */

    FILETIME stamp;
    if (!FileStamp(w->path, &stamp)) return;
    if (CompareFileTime(&stamp, &w->stamp) == 0) return;

    w->stamp = stamp;
    WCHAR* fresh = Notes_LoadText(w->path);
    if (!fresh) return;

    AdoptText(w, fresh);
    base->needs_render = true;
}

static void Notes_Destroy(Widget* base) {
    NotesWidget* w = (NotesWidget*)base;
    Commit(w);
    ClearUndo(w);
    if (w->scratch_gfx) GdipDeleteGraphics(w->scratch_gfx);
    if (w->scratch) GdipDisposeImage((GpImage*)w->scratch);
    free(w->text);
    free(w);
}

static const WidgetVtable kNotesVtable = {
    Notes_Render,
    Notes_OnTimer,
    NULL,
    Notes_Destroy,
    Notes_OnMessage
};

bool NotesWidget_Create(HINSTANCE hInstance, const char* iniPath, const WidgetSpec* spec) {
    NotesWidget* w = (NotesWidget*)calloc(1, sizeof(NotesWidget));
    if (!w) return false;

    w->spec = *spec;
    w->goal_x = -1.0f;
    strncpy(w->ini, iniPath ? iniPath : "", MAX_PATH - 1);
    Config_ResolvePath(iniPath, spec->path, w->path, MAX_PATH);
    AdoptText(w, Notes_LoadText(w->path));
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
