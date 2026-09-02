#include "gauge.h"

#include "../widget.h"
#include "../drawing.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ALPHA_OF(c) (((c) >> 24) & 0xFFu)
#define VISIBLE(c)  (ALPHA_OF(c) != 0)

typedef struct {
    Widget       base;
    WidgetSpec   spec;
    GaugeReading readings[LW_GAUGE_MAX];
    DWORD        due[LW_GAUGE_MAX];  /* tick at which each reading goes stale */
    int          count;
    unsigned     last_signature;     /* changes only when the drawn text would */
    bool         drawn;              /* last_signature means nothing until then */
} GaugeWidget;

/* ─────────────────────────── sampling ─────────────────────────── */

static unsigned long long Ticks(FILETIME ft) {
    return ((unsigned long long)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
}

/*
 * CPU load is a difference between two samples of the same counters, so the
 * sample is process-wide: two gauges taking their own would each measure a
 * fraction of the interval and report different numbers for the same load.
 * The guard also means a second gauge ticking alongside the first reuses the
 * reading instead of resetting the window to nothing.
 */
static struct {
    unsigned long long idle, kernel, user;
    DWORD lastTick;
    float percent;
    bool  primed;
} g_cpu;

static float ReadCpu(void) {
    DWORD now = GetTickCount();
    if (g_cpu.primed && (DWORD)(now - g_cpu.lastTick) < 250u) return g_cpu.percent;

    FILETIME idleFt, kernelFt, userFt;
    if (!GetSystemTimes(&idleFt, &kernelFt, &userFt)) return g_cpu.percent;

    unsigned long long idle   = Ticks(idleFt);
    unsigned long long kernel = Ticks(kernelFt);
    unsigned long long user   = Ticks(userFt);

    if (g_cpu.primed) {
        /* Kernel time already contains idle, so it is the whole of the slice. */
        unsigned long long total = (kernel - g_cpu.kernel) + (user - g_cpu.user);
        unsigned long long busy  = total - (idle - g_cpu.idle);
        if (total > 0) {
            float percent = (float)((double)busy * 100.0 / (double)total);
            g_cpu.percent = percent < 0.0f ? 0.0f : (percent > 100.0f ? 100.0f : percent);
        }
    }

    g_cpu.idle = idle;
    g_cpu.kernel = kernel;
    g_cpu.user = user;
    g_cpu.lastTick = now;
    g_cpu.primed = true;
    return g_cpu.percent;
}

/* "6.1 / 16.0 GB" — one unit for both halves reads better than two. */
static void FormatPair(unsigned long long used, unsigned long long total,
                       WCHAR* out, size_t cap) {
    double scale = 1.0;
    const WCHAR* unit = L"B";

    if (total >= (1ull << 30))      { scale = (double)(1ull << 30); unit = L"GB"; }
    else if (total >= (1ull << 20)) { scale = (double)(1ull << 20); unit = L"MB"; }
    else if (total >= 1024ull)      { scale = 1024.0;               unit = L"KB"; }

    _snwprintf(out, cap, L"%.1f / %.1f %s", (double)used / scale, (double)total / scale, unit);
    out[cap - 1] = L'\0';
}

static void ReadMemory(GaugeReading* out) {
    MEMORYSTATUSEX status;
    status.dwLength = sizeof(status);
    if (!GlobalMemoryStatusEx(&status)) return;

    out->available = true;
    out->percent = (float)status.dwMemoryLoad;
    FormatPair(status.ullTotalPhys - status.ullAvailPhys, status.ullTotalPhys,
               out->detail, 64);
}

static void ReadDisk(const char* drive, GaugeReading* out) {
    WCHAR root[16];
    int n = MultiByteToWideChar(CP_UTF8, 0, drive, -1, root, 12);
    if (n <= 1) return;

    /* Accept "C", "C:" and "C:\" alike; the API wants the trailing separator. */
    size_t len = wcslen(root);
    if (len > 0 && root[len - 1] != L'\\') {
        if (root[len - 1] != L':') root[len++] = L':';
        root[len++] = L'\\';
        root[len] = L'\0';
    }

    ULARGE_INTEGER freeToCaller, total, totalFree;
    if (!GetDiskFreeSpaceExW(root, &freeToCaller, &total, &totalFree)) return;
    if (total.QuadPart == 0) return;

    unsigned long long used = total.QuadPart - totalFree.QuadPart;
    out->available = true;
    out->percent = (float)((double)used * 100.0 / (double)total.QuadPart);
    FormatPair(used, total.QuadPart, out->detail, 64);
}

static void ReadBattery(GaugeReading* out) {
    SYSTEM_POWER_STATUS power;
    if (!GetSystemPowerStatus(&power)) return;
    if (power.BatteryFlag & 128) return;        /* no system battery */
    if (power.BatteryLifePercent > 100) return; /* 255 means unknown */

    out->available = true;
    out->percent = (float)power.BatteryLifePercent;

    if (power.ACLineStatus == 1) {
        wcscpy(out->detail, (power.BatteryLifePercent >= 100) ? L"Full" : L"Charging");
    } else if (power.BatteryLifeTime != (DWORD)-1) {
        DWORD minutes = power.BatteryLifeTime / 60;
        _snwprintf(out->detail, 64, L"%luh %02lum left", minutes / 60, minutes % 60);
        out->detail[63] = L'\0';
    }
}

static void ReadItem(const GaugeItem* item, GaugeReading* out) {
    memset(out, 0, sizeof(*out));

    switch (item->source) {
        case GAUGE_MEMORY:  ReadMemory(out); break;
        case GAUGE_DISK:    ReadDisk(item->drive, out); break;
        case GAUGE_BATTERY: ReadBattery(out); break;
        default:
            out->available = true;
            out->percent = ReadCpu();
            break;
    }

    if (out->percent < 0.0f)   out->percent = 0.0f;
    if (out->percent > 100.0f) out->percent = 100.0f;
}

int Gauge_Read(const WidgetSpec* spec, GaugeReading* out, int cap) {
    if (!spec || !out || cap <= 0) return 0;

    int count = spec->gauge.count;
    if (count > cap) count = cap;
    for (int i = 0; i < count; i++) ReadItem(&spec->gauge.items[i], &out[i]);
    return count;
}

/* ─────────────────────────── painting ─────────────────────────── */

static const WCHAR* DefaultLabel(int source) {
    switch (source) {
        case GAUGE_MEMORY:  return L"MEMORY";
        case GAUGE_DISK:    return L"DISK";
        case GAUGE_BATTERY: return L"BATTERY";
        default:            return L"CPU";
    }
}

/* The fill colour a reading earns: past either threshold it changes to warn. */
static bool IsWarning(const GaugeItem* item, float percent) {
    if (item->warn_above > 0.0f && percent >= item->warn_above) return true;
    if (item->warn_below > 0.0f && percent <= item->warn_below) return true;
    return false;
}

static ARGB FillFor(const GaugeOptions* g, const GaugeItem* item, float percent) {
    return IsWarning(item, percent) ? g->warn_color : g->fill_color;
}

static void FillRounded(GpGraphics* gfx, ARGB c1, ARGB c2, int gradient,
                        float x, float y, float w, float h, float radius) {
    Drawing_Panel(gfx, c1, c2, gradient, 0, 0.0f, x, y, w, h, radius);
}

/* A run that carries the widget's text styling but its own box and size. */
static TextRun Run(const WidgetSpec* spec, const WCHAR* text, GpRectF box,
                   float size, ARGB color, int alignH, int alignV) {
    TextRun run = Drawing_Run(&spec->style, text, box);
    run.font_size = size;
    run.color     = color;
    run.color2    = 0;
    run.gradient  = GRAD_NONE;
    run.align_h   = alignH;
    run.align_v   = alignV;
    run.no_wrap   = true;

    /*
     * Effects are authored against the main font size; scaling them keeps a
     * 3px outline from swallowing a 10px detail line.
     */
    float ratio = (spec->style.font_size > 0.0f) ? size / spec->style.font_size : 1.0f;
    run.outline_width *= ratio;
    run.shadow_dx     *= ratio;
    run.shadow_dy     *= ratio;
    run.glow_radius   *= ratio;
    return run;
}

static void ValueText(const GaugeReading* r, WCHAR* out, size_t cap) {
    if (!r->available) { wcsncpy(out, L"n/a", cap - 1); out[cap - 1] = L'\0'; return; }
    _snwprintf(out, cap, L"%.0f%%", r->percent);
    out[cap - 1] = L'\0';
}

static void PaintBar(const WidgetSpec* spec, const GaugeItem* item, const GaugeReading* r,
                     bool detailRow, GpGraphics* gfx, float x, float y, float w, float h) {
    const WidgetStyle* s = &spec->style;
    const GaugeOptions* g = &spec->gauge;

    const WCHAR* label = item->label[0] ? item->label : DefaultLabel(item->source);
    WCHAR value[32];
    ValueText(r, value, 32);

    bool topRow  = g->show_label || g->show_value;
    bool bottom  = detailRow;

    float gap      = s->font_size * 0.35f;
    float rowH     = s->font_size * 1.3f;
    float detailH  = s->font_size * 1.0f;
    float thickness = g->thickness > h ? h : g->thickness;

    float stack = thickness
                + (topRow ? rowH + gap : 0.0f)
                + (bottom ? detailH + gap * 0.6f : 0.0f);
    float top = y + (h - stack) * 0.5f;
    if (top < y) top = y;

    if (topRow) {
        GpRectF row = { x, top, w, rowH };
        if (g->show_label) {
            TextRun run = Run(spec, label, row, s->font_size, s->text_color,
                              ALIGN_NEAR, ALIGN_CENTER);
            run.color2   = s->text_color2;
            run.gradient = s->text_gradient;
            Drawing_Text(gfx, &run);
        }
        if (g->show_value) {
            TextRun run = Run(spec, value, row, s->font_size, s->text_color,
                              g->show_label ? ALIGN_FAR : ALIGN_CENTER, ALIGN_CENTER);
            run.color2   = s->text_color2;
            run.gradient = s->text_gradient;
            Drawing_Text(gfx, &run);
        }
        top += rowH + gap;
    }

    float radius = thickness * 0.5f;
    FillRounded(gfx, g->track_color, 0, GRAD_NONE, x, top, w, thickness, radius);

    float filled = w * (r->available ? r->percent / 100.0f : 0.0f);
    /* Below one full cap the rounded ends collapse into a smear; skip it. */
    if (filled > thickness * 0.6f)
        FillRounded(gfx, FillFor(g, item, r->percent), g->fill_color2, g->fill_gradient,
                    x, top, filled, thickness, radius);

    if (!bottom || !r->detail[0]) return;
    top += thickness + gap * 0.6f;

    GpRectF row = { x, top, w, detailH };
    TextRun run = Run(spec, r->detail, row, s->font_size * 0.72f,
                      Style_ScaleAlpha(s->text_color, 0.6f), ALIGN_NEAR, ALIGN_CENTER);
    Drawing_Text(gfx, &run);
}

static void PaintRing(const WidgetSpec* spec, const GaugeItem* item, const GaugeReading* r,
                      bool detailRow, GpGraphics* gfx, float x, float y, float w, float h) {
    const WidgetStyle* s = &spec->style;
    const GaugeOptions* g = &spec->gauge;

    const WCHAR* label = item->label[0] ? item->label : DefaultLabel(item->source);
    bool caption = g->show_label || detailRow;

    /* The caption sits under the dial, so the dial gets what is left. */
    float captionH = caption ? s->font_size * 1.2f : 0.0f;
    float dialH = h - captionH;
    float diameter = (w < dialH ? w : dialH);
    if (diameter < 8.0f) return;

    float thickness = g->thickness;
    if (thickness > diameter * 0.4f) thickness = diameter * 0.4f;

    float cx = x + w * 0.5f;
    float cy = y + dialH * 0.5f;
    float radius = (diameter - thickness) * 0.5f;
    GpRectF box = { cx - radius, cy - radius, radius * 2.0f, radius * 2.0f };

    GpPen* pen = NULL;
    if (VISIBLE(g->track_color) && GdipCreatePen1(g->track_color, thickness, UnitPixel, &pen) == 0) {
        GdipDrawArc(gfx, pen, box.X, box.Y, box.Width, box.Height, 0.0f, 360.0f);
        GdipDeletePen(pen);
    }

    float sweep = (r->available ? r->percent : 0.0f) * 3.6f;
    ARGB fill = FillFor(g, item, r->percent);
    if (sweep > 0.5f && VISIBLE(fill)) {
        GpBrush* brush = Drawing_GradientBrush(&box, fill, g->fill_color2, g->fill_gradient);
        GpPen* arc = NULL;
        if (brush) {
            if (GdipCreatePen2(brush, thickness, UnitPixel, &arc) != 0) arc = NULL;
        } else if (GdipCreatePen1(fill, thickness, UnitPixel, &arc) != 0) {
            arc = NULL;
        }
        if (arc) {
            GdipSetPenStartCap(arc, LineCapRound);
            GdipSetPenEndCap(arc, LineCapRound);
            /* -90 degrees puts zero at the top, where a dial is read from. */
            GdipDrawArc(gfx, arc, box.X, box.Y, box.Width, box.Height, -90.0f, sweep);
            GdipDeletePen(arc);
        }
        if (brush) GdipDeleteBrush(brush);
    }

    if (g->show_value) {
        WCHAR value[32];
        ValueText(r, value, 32);
        float inner = (radius - thickness) * 1.4f;
        GpRectF box2 = { cx - inner, cy - inner * 0.5f, inner * 2.0f, inner };
        TextRun run = Run(spec, value, box2, s->font_size, s->text_color,
                          ALIGN_CENTER, ALIGN_CENTER);
        run.color2   = s->text_color2;
        run.gradient = s->text_gradient;
        Drawing_Text(gfx, &run);
    }

    if (!caption) return;

    const WCHAR* text = g->show_label ? label : r->detail;
    if (!text[0]) return;
    GpRectF row = { x, y + dialH, w, captionH };
    TextRun run = Run(spec, text, row, s->font_size * 0.62f,
                      Style_ScaleAlpha(s->text_color, 0.65f), ALIGN_CENTER, ALIGN_CENTER);
    Drawing_Text(gfx, &run);
}

static void PaintNumber(const WidgetSpec* spec, const GaugeItem* item, const GaugeReading* r,
                        bool detailRow, GpGraphics* gfx, float x, float y, float w, float h) {
    const WidgetStyle* s = &spec->style;
    const GaugeOptions* g = &spec->gauge;

    const WCHAR* label = item->label[0] ? item->label : DefaultLabel(item->source);
    bool top    = g->show_label;
    bool bottom = detailRow;

    float sideH = s->font_size * 0.85f;
    float valueY = y + (top ? sideH : 0.0f);
    float valueH = h - (top ? sideH : 0.0f) - (bottom ? sideH : 0.0f);
    if (valueH < 4.0f) { valueY = y; valueH = h; top = bottom = false; }

    if (top) {
        GpRectF row = { x, y, w, sideH };
        TextRun run = Run(spec, label, row, s->font_size * 0.5f,
                          Style_ScaleAlpha(s->text_color, 0.65f), s->align_h, ALIGN_CENTER);
        Drawing_Text(gfx, &run);
    }

    WCHAR value[32];
    ValueText(r, value, 32);
    GpRectF box = { x, valueY, w, valueH };
    TextRun run = Drawing_Run(s, value, box);
    run.no_wrap = true;
    run.align_v = ALIGN_CENTER;
    if (r->available && IsWarning(item, r->percent)) {
        run.color = g->warn_color;
        run.gradient = GRAD_NONE;
    }
    Drawing_Text(gfx, &run);

    if (!bottom || !r->detail[0]) return;
    GpRectF row = { x, valueY + valueH, w, sideH };
    TextRun detail = Run(spec, r->detail, row, s->font_size * 0.5f,
                         Style_ScaleAlpha(s->text_color, 0.6f), s->align_h, ALIGN_CENTER);
    Drawing_Text(gfx, &detail);
}

void Gauge_Paint(const WidgetSpec* spec, const GaugeReading* readings, int count,
                 GpGraphics* gfx, int width, int height) {
    if (!spec || !readings || count <= 0 || !gfx || width <= 0 || height <= 0) return;

    const GaugeOptions* g = &spec->gauge;
    Drawing_Surface(gfx, &spec->style, (float)width, (float)height);

    float pad = spec->style.padding;
    float w = (float)width - pad * 2.0f;
    float h = (float)height - pad * 2.0f;
    if (w <= 0.0f || h <= 0.0f) return;

    if (count > g->count) count = g->count;

    /*
     * The detail row is reserved for the whole widget or for none of it: a
     * reading with nothing to say under it would otherwise centre its bar
     * higher than the one beside it.
     */
    bool detailRow = false;
    for (int i = 0; i < count && g->show_detail; i++)
        if (readings[i].detail[0]) detailRow = true;

    int cols = 1, rows = 1;
    Spec_GaugeGrid(g, &cols, &rows);

    /* One panel, one cell per reading -- so a machine panel is a widget. */
    float cellW = (w - g->spacing * (float)(cols - 1)) / (float)cols;
    float cellH = (h - g->spacing * (float)(rows - 1)) / (float)rows;
    if (cellW <= 0.0f || cellH <= 0.0f) return;

    for (int i = 0; i < count; i++) {
        const GaugeItem* item = &g->items[i];
        float x = pad + (float)(i % cols) * (cellW + g->spacing);
        float y = pad + (float)(i / cols) * (cellH + g->spacing);

        switch (g->style) {
            case GAUGE_RING:
                PaintRing(spec, item, &readings[i], detailRow, gfx, x, y, cellW, cellH);
                break;
            case GAUGE_NUMBER:
                PaintNumber(spec, item, &readings[i], detailRow, gfx, x, y, cellW, cellH);
                break;
            default:
                PaintBar(spec, item, &readings[i], detailRow, gfx, x, y, cellW, cellH);
                break;
        }
    }
}

/* ─────────────────────────── widget plumbing ─────────────────────────── */

/*
 * How often a reading is worth taking. CPU is the only one that moves fast
 * enough to justify a second; a disk that is 61% full will still be 61% full
 * in half a minute. Each reading keeps its own schedule, so putting a disk
 * beside a CPU does not cost a volume query every second.
 */
static UINT SourceInterval(int source) {
    switch (source) {
        case GAUGE_DISK:    return 30000;
        case GAUGE_BATTERY: return 10000;
        case GAUGE_MEMORY:  return 2000;
        default:            return 1000;
    }
}

static UINT Gauge_NextInterval(Widget* base) {
    GaugeWidget* w = (GaugeWidget*)base;
    UINT soonest = 60000;

    for (int i = 0; i < w->count; i++) {
        UINT interval = SourceInterval(w->spec.gauge.items[i].source);
        if (interval < soonest) soonest = interval;
    }
    return soonest;
}

/*
 * A stamp of what is actually on screen. The percentage is drawn rounded, so
 * a load wandering between 12.1 and 12.4 must not cost a repaint; the detail
 * line is folded in because it changes on its own schedule.
 */
static unsigned Signature(const GaugeWidget* w) {
    unsigned stamp = 0;
    for (int i = 0; i < w->count; i++) {
        const GaugeReading* r = &w->readings[i];
        stamp = stamp * 131u + (unsigned)(int)(r->percent + 0.5f) * 8u;
        if (!r->available) stamp |= 1u;
        for (const WCHAR* p = r->detail; *p; p++)
            stamp += (unsigned)*p;
    }
    return stamp;
}

/* Refresh only the readings whose own interval has come round. */
static void ReadDue(GaugeWidget* w) {
    DWORD now = GetTickCount();
    for (int i = 0; i < w->count; i++) {
        if ((int)(now - w->due[i]) < 0) continue;
        ReadItem(&w->spec.gauge.items[i], &w->readings[i]);
        w->due[i] = now + SourceInterval(w->spec.gauge.items[i].source);
    }
}

static void Gauge_Render(Widget* base, GpGraphics* gfx, int width, int height) {
    GaugeWidget* w = (GaugeWidget*)base;
    ReadDue(w);
    w->last_signature = Signature(w);
    w->drawn = true;
    Gauge_Paint(&w->spec, w->readings, w->count, gfx, width, height);
}

static void Gauge_OnTimer(Widget* base) {
    GaugeWidget* w = (GaugeWidget*)base;
    ReadDue(w);
    if (!w->drawn || Signature(w) != w->last_signature) base->needs_render = true;
}

static void Gauge_Destroy(Widget* base) {
    free(base);
}

static const WidgetVtable kGaugeVtable = {
    Gauge_Render,
    Gauge_OnTimer,
    Gauge_NextInterval,
    Gauge_Destroy,
    NULL
};

bool GaugeWidget_Create(HINSTANCE hInstance, const WidgetSpec* spec) {
    GaugeWidget* w = (GaugeWidget*)calloc(1, sizeof(GaugeWidget));
    if (!w) return false;

    w->spec = *spec;
    w->count = spec->gauge.count;
    if (w->count < 1) w->count = 1;
    if (w->count > LW_GAUGE_MAX) w->count = LW_GAUGE_MAX;

    /* Due now, rather than at tick zero, so the schedule survives a wrap. */
    DWORD now = GetTickCount();
    for (int i = 0; i < w->count; i++) w->due[i] = now;

    if (!Widget_Init(&w->base, hInstance, &kGaugeVtable, spec, 1000)) {
        free(w);
        return false;
    }
    Widget_Render(&w->base);
    return true;
}
