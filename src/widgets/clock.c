#include "clock.h"

#include "../widget.h"
#include "../drawing.h"
#include "../timefmt.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define MAX_BLINK_SEGMENTS 8

typedef struct {
    Widget     base;
    WidgetSpec spec;
    int        last_signature;   /* changes only when the visible text changes */
} ClockWidget;

/* ─────────────────────────── digital ─────────────────────────── */

/*
 * Split a time string so every separator becomes its own segment. That lets
 * the colon be drawn in a dimmer colour on odd seconds without the rest of
 * the line shifting, which is what happens if you blank the character out.
 */
static int BuildSeparatorSegments(const WCHAR* text, ARGB dim,
                                  WCHAR store[MAX_BLINK_SEGMENTS][LW_FORMAT_LEN],
                                  TextSegment* segments) {
    int count = 0;
    size_t start = 0, i = 0;

    for (; text[i] && count < MAX_BLINK_SEGMENTS - 2; i++) {
        if (text[i] != L':') continue;

        if (i > start) {
            size_t len = i - start;
            if (len >= LW_FORMAT_LEN) len = LW_FORMAT_LEN - 1;
            wcsncpy(store[count], text + start, len);
            store[count][len] = L'\0';
            segments[count].text  = store[count];
            segments[count].color = 0;           /* inherit */
            count++;
        }
        wcscpy(store[count], L":");
        segments[count].text  = store[count];
        segments[count].color = dim;
        count++;
        start = i + 1;
    }

    if (text[start]) {
        wcsncpy(store[count], text + start, LW_FORMAT_LEN - 1);
        store[count][LW_FORMAT_LEN - 1] = L'\0';
        segments[count].text  = store[count];
        segments[count].color = 0;
        count++;
    }
    return count;
}

static void PaintDigital(const WidgetSpec* spec, const SYSTEMTIME* now,
                         GpGraphics* gfx, int width, int height) {
    const WidgetStyle* s = &spec->style;
    const ClockOptions* c = &spec->clock;

    float pad = s->padding;
    float innerW = (float)width  - pad * 2.0f;
    float innerH = (float)height - pad * 2.0f;
    if (innerW <= 0.0f || innerH <= 0.0f) return;

    WCHAR raw[LW_FORMAT_LEN * 2];
    WCHAR timeText[LW_FORMAT_LEN * 2];
    TimeFmt_Format(now, c->time_format, raw, LW_FORMAT_LEN * 2);
    Drawing_Transform(timeText, LW_FORMAT_LEN * 2, raw, s->text_transform);

    float timeH = c->show_date ? innerH * c->time_ratio : innerH;
    GpRectF timeRect = { pad, pad, innerW, timeH };

    TextRun timeRun = Drawing_Run(s, timeText, timeRect);
    timeRun.no_wrap = true;
    /* With a date below, hug the two rows toward the middle of the panel. */
    if (c->show_date) timeRun.align_v = ALIGN_FAR;

    bool blinking = c->blink_separator && (now->wSecond % 2) == 1;
    if (blinking) {
        WCHAR store[MAX_BLINK_SEGMENTS][LW_FORMAT_LEN];
        TextSegment segments[MAX_BLINK_SEGMENTS];
        int count = BuildSeparatorSegments(timeText, Style_ScaleAlpha(s->text_color, 0.25f),
                                           store, segments);
        if (count > 0) Drawing_TextSegments(gfx, &timeRun, segments, count);
    } else {
        Drawing_Text(gfx, &timeRun);
    }

    if (!c->show_date) return;

    float dateY = pad + timeH + c->date_gap;
    float dateH = innerH - timeH - c->date_gap;
    if (dateH <= 1.0f) return;

    WCHAR dateRaw[LW_FORMAT_LEN * 2];
    WCHAR dateText[LW_FORMAT_LEN * 2];
    TimeFmt_Format(now, c->date_format, dateRaw, LW_FORMAT_LEN * 2);
    Drawing_Transform(dateText, LW_FORMAT_LEN * 2, dateRaw, c->date_transform);

    GpRectF dateRect = { pad, dateY, innerW, dateH };
    TextRun dateRun = Drawing_Run(s, dateText, dateRect);
    dateRun.font_family    = c->date_font_family[0] ? c->date_font_family : s->font_family;
    dateRun.font_size      = c->date_font_size;
    dateRun.font_style     = c->date_font_style;
    dateRun.color          = c->date_color;
    dateRun.color2         = 0;              /* the date keeps a flat colour */
    dateRun.gradient       = GRAD_NONE;
    dateRun.letter_spacing = c->date_letter_spacing;
    dateRun.align_v        = ALIGN_NEAR;
    dateRun.no_wrap        = true;

    /*
     * Effects are authored against the time's font size. Scaling them by the
     * size ratio keeps a 3px outline from swallowing a 13px date line, while
     * still letting the two rows read as one design.
     */
    float ratio = (s->font_size > 0.0f) ? (c->date_font_size / s->font_size) : 1.0f;
    dateRun.outline_width *= ratio;
    dateRun.shadow_dx     *= ratio;
    dateRun.shadow_dy     *= ratio;
    dateRun.glow_radius   *= ratio;

    Drawing_Text(gfx, &dateRun);
}

/* ─────────────────────────── analog ─────────────────────────── */

/* Draw a line along a radius, measured from 12 o'clock going clockwise. */
static void DrawRadial(GpGraphics* gfx, GpPen* pen, float cx, float cy,
                       float angleDeg, float r0, float r1) {
    GraphicsState state = 0;
    GdipSaveGraphics(gfx, &state);
    GdipTranslateWorldTransform(gfx, cx, cy, MatrixOrderPrepend);
    GdipRotateWorldTransform(gfx, angleDeg, MatrixOrderPrepend);
    GdipDrawLine(gfx, pen, 0.0f, -r0, 0.0f, -r1);
    GdipRestoreGraphics(gfx, state);
}

static void DrawHand(GpGraphics* gfx, ARGB color, float widthPx,
                     float cx, float cy, float angleDeg, float length, float tail) {
    if (widthPx <= 0.0f || ((color >> 24) & 0xFFu) == 0) return;

    GpPen* pen = NULL;
    if (GdipCreatePen1(color, widthPx, UnitPixel, &pen) != 0) return;
    GdipSetPenStartCap(pen, LineCapRound);
    GdipSetPenEndCap(pen, LineCapRound);
    DrawRadial(gfx, pen, cx, cy, angleDeg, -tail, length);
    GdipDeletePen(pen);
}

static void PaintAnalog(const WidgetSpec* spec, const SYSTEMTIME* now,
                        GpGraphics* gfx, int width, int height) {
    const WidgetStyle* s = &spec->style;
    const ClockOptions* c = &spec->clock;

    float cx = width * 0.5f;
    float cy = height * 0.5f;
    float radius = (width < height ? width : height) * 0.5f - s->padding;
    if (radius < 6.0f) return;

    /* Dial face. */
    if (((c->face_color >> 24) & 0xFFu) != 0) {
        GpRectF box = { cx - radius, cy - radius, radius * 2.0f, radius * 2.0f };
        GpBrush* brush = Drawing_GradientBrush(&box, c->face_color, c->face_color2, c->face_gradient);
        if (brush) {
            GdipFillEllipse(gfx, brush, box.X, box.Y, box.Width, box.Height);
            GdipDeleteBrush(brush);
        } else {
            GpSolidFill* solid = NULL;
            if (GdipCreateSolidFill(c->face_color, &solid) == 0) {
                GdipFillEllipse(gfx, (GpBrush*)solid, box.X, box.Y, box.Width, box.Height);
                GdipDeleteBrush((GpBrush*)solid);
            }
        }
    }

    /* Outer ring. */
    if (c->ring_width > 0.0f && ((c->ring_color >> 24) & 0xFFu) != 0) {
        GpPen* pen = NULL;
        if (GdipCreatePen1(c->ring_color, c->ring_width, UnitPixel, &pen) == 0) {
            float r = radius - c->ring_width * 0.5f;
            GdipDrawEllipse(gfx, pen, cx - r, cy - r, r * 2.0f, r * 2.0f);
            GdipDeletePen(pen);
        }
    }

    float tickOuter = radius - c->ring_width - 2.0f;
    if (tickOuter < 2.0f) tickOuter = radius;

    /* Fine minute marks, skipped where a major tick already sits. */
    if (c->minute_ticks && ((c->tick_color >> 24) & 0xFFu) != 0) {
        GpPen* pen = NULL;
        if (GdipCreatePen1(Style_ScaleAlpha(c->tick_color, 0.55f), 1.0f, UnitPixel, &pen) == 0) {
            for (int i = 0; i < 60; i++) {
                if (c->tick_count > 0 && (i % (60 / (c->tick_count > 60 ? 60 : c->tick_count))) == 0)
                    continue;
                DrawRadial(gfx, pen, cx, cy, i * 6.0f, tickOuter - 3.0f, tickOuter);
            }
            GdipDeletePen(pen);
        }
    }

    /* Major hour ticks. */
    if (c->tick_count > 0 && c->tick_width > 0.0f && ((c->tick_color >> 24) & 0xFFu) != 0) {
        GpPen* pen = NULL;
        if (GdipCreatePen1(c->tick_color, c->tick_width, UnitPixel, &pen) == 0) {
            GdipSetPenStartCap(pen, LineCapRound);
            GdipSetPenEndCap(pen, LineCapRound);
            float step = 360.0f / (float)c->tick_count;
            for (int i = 0; i < c->tick_count; i++)
                DrawRadial(gfx, pen, cx, cy, i * step, tickOuter - c->tick_length, tickOuter);
            GdipDeletePen(pen);
        }
    }

    float seconds = (float)now->wSecond +
                    (c->smooth_seconds ? (float)now->wMilliseconds / 1000.0f : 0.0f);
    float minutes = (float)now->wMinute + seconds / 60.0f;
    float hours   = (float)(now->wHour % 12) + minutes / 60.0f;

    float scale = c->hand_scale;
    float tail  = radius * 0.16f;

    DrawHand(gfx, c->hour_hand_color,   c->hour_hand_width,   cx, cy,
             hours * 30.0f,   radius * 0.50f * scale, tail);
    DrawHand(gfx, c->minute_hand_color, c->minute_hand_width, cx, cy,
             minutes * 6.0f,  radius * 0.74f * scale, tail);

    if (spec->show_seconds)
        DrawHand(gfx, c->second_hand_color, c->second_hand_width, cx, cy,
                 seconds * 6.0f, radius * 0.84f * scale, tail * 1.4f);

    /* Centre cap. */
    if (((c->hub_color >> 24) & 0xFFu) != 0) {
        float r = (c->hour_hand_width > 0.0f ? c->hour_hand_width : 4.0f) * 0.9f;
        GpSolidFill* solid = NULL;
        if (GdipCreateSolidFill(c->hub_color, &solid) == 0) {
            GdipFillEllipse(gfx, (GpBrush*)solid, cx - r, cy - r, r * 2.0f, r * 2.0f);
            GdipDeleteBrush((GpBrush*)solid);
        }
    }
}

void Clock_Paint(const WidgetSpec* spec, const SYSTEMTIME* now,
                 GpGraphics* gfx, int width, int height) {
    if (!spec || !now || !gfx || width <= 0 || height <= 0) return;

    Drawing_Surface(gfx, &spec->style, (float)width, (float)height);

    if (spec->clock.mode == CLOCK_ANALOG) PaintAnalog(spec, now, gfx, width, height);
    else                                  PaintDigital(spec, now, gfx, width, height);
}

/* ─────────────────────────── widget plumbing ─────────────────────────── */

/*
 * A clock only needs to wake up when what it displays would change. A plain
 * hour:minute readout therefore ticks once a minute rather than 60 times, and
 * only the opt-in sweeping second hand runs at animation rates.
 */
static UINT TickResolution(const ClockWidget* w) {
    const WidgetSpec* spec = &w->spec;
    if (spec->clock.mode == CLOCK_ANALOG && spec->clock.smooth_seconds && spec->show_seconds)
        return 50;
    if (spec->show_seconds || spec->clock.blink_separator)
        return 1000;
    return 60000;
}

/* Milliseconds until the next boundary we actually care about. */
static UINT Clock_NextInterval(Widget* base) {
    ClockWidget* w = (ClockWidget*)base;
    UINT resolution = TickResolution(w);
    if (resolution <= 100) return resolution;

    SYSTEMTIME st;
    GetLocalTime(&st);

    UINT elapsed = (resolution == 60000)
                 ? (UINT)st.wSecond * 1000u + st.wMilliseconds
                 : (UINT)st.wMilliseconds;

    UINT remaining = resolution - (elapsed % resolution);
    return remaining < 20u ? resolution : remaining;
}

/* A cheap stamp of everything currently visible, to skip redundant redraws. */
static int Signature(const ClockWidget* w, const SYSTEMTIME* st) {
    UINT resolution = TickResolution(w);
    if (resolution <= 100)
        return (int)(st->wMinute * 60000 + st->wSecond * 1000 + st->wMilliseconds / 50);
    if (resolution == 1000)
        return (int)(st->wHour * 3600 + st->wMinute * 60 + st->wSecond);
    return (int)(st->wHour * 60 + st->wMinute);
}

static void Clock_Render(Widget* base, GpGraphics* gfx, int width, int height) {
    ClockWidget* w = (ClockWidget*)base;
    SYSTEMTIME st;
    GetLocalTime(&st);
    w->last_signature = Signature(w, &st);
    Clock_Paint(&w->spec, &st, gfx, width, height);
}

static void Clock_OnTimer(Widget* base) {
    ClockWidget* w = (ClockWidget*)base;
    SYSTEMTIME st;
    GetLocalTime(&st);
    if (Signature(w, &st) != w->last_signature)
        base->needs_render = true;
}

static void Clock_Destroy(Widget* base) {
    free(base);
}

static const WidgetVtable kClockVtable = {
    Clock_Render,
    Clock_OnTimer,
    Clock_NextInterval,
    Clock_Destroy
};

bool ClockWidget_Create(HINSTANCE hInstance, const WidgetSpec* spec) {
    ClockWidget* w = (ClockWidget*)calloc(1, sizeof(ClockWidget));
    if (!w) return false;

    w->spec = *spec;
    w->last_signature = -1;

    if (!Widget_Init(&w->base, hInstance, &kClockVtable, spec, 1000)) {
        free(w);
        return false;
    }
    Widget_Render(&w->base);
    return true;
}
