#include "calendar.h"

#include "../widget.h"
#include "../drawing.h"
#include "../timefmt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GRID_ROWS 6          /* fixed, so the layout does not jump each month */
#define GRID_COLS 7

typedef struct {
    Widget     base;
    WidgetSpec spec;
    int        last_day;     /* the date currently drawn, as yyyymmdd */
} CalendarWidget;

/* ─────────────────────────── the calendar itself ─────────────────── */

static bool IsLeap(int year) {
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

static int DaysInMonth(int year, int month) {
    static const int kDays[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (month < 1 || month > 12) return 30;
    if (month == 2 && IsLeap(year)) return 29;
    return kDays[month - 1];
}

/*
 * Day of the week, 0 = Sunday. Routed through the system rather than a
 * congruence so it agrees with everything else Windows says about the date.
 */
static int WeekdayOf(int year, int month, int day) {
    SYSTEMTIME st;
    memset(&st, 0, sizeof(st));
    st.wYear = (WORD)year;
    st.wMonth = (WORD)month;
    st.wDay = (WORD)day;

    FILETIME ft;
    SYSTEMTIME back;
    if (!SystemTimeToFileTime(&st, &ft)) return 0;
    if (!FileTimeToSystemTime(&ft, &back)) return 0;
    return back.wDayOfWeek;
}

static int DayOfYear(int year, int month, int day) {
    int total = day;
    for (int m = 1; m < month; m++) total += DaysInMonth(year, m);
    return total;
}

/* A year has 53 ISO weeks when it starts on a Thursday, or a leap year on a
   Wednesday; every other year has 52. */
static int IsoWeeksInYear(int year) {
    int jan1 = WeekdayOf(year, 1, 1);
    int iso  = (jan1 == 0) ? 7 : jan1;
    return (iso == 4 || (IsLeap(year) && iso == 3)) ? 53 : 52;
}

static int IsoWeek(int year, int month, int day) {
    int dow = WeekdayOf(year, month, day);
    int iso = (dow == 0) ? 7 : dow;                  /* 1 = Monday .. 7 = Sunday */
    int week = (DayOfYear(year, month, day) - iso + 10) / 7;

    if (week < 1) return IsoWeeksInYear(year - 1);
    if (week > IsoWeeksInYear(year)) return 1;
    return week;
}

/* Which weekday the first column shows, 0 = Sunday. */
static int FirstColumnDay(int weekStart) {
    if (weekStart == WEEK_MONDAY) return 1;
    if (weekStart == WEEK_SUNDAY) return 0;
    /* The locale answers Monday-first; the grid counts from Sunday. */
    return (TimeFmt_FirstDayOfWeek() + 1) % 7;
}

/* ─────────────────────────── painting ─────────────────────────── */

typedef struct {
    int  day;            /* 1..31 */
    bool outside;        /* belongs to the month before or after */
    bool weekend;
    bool today;
} Cell;

/*
 * Fill the six-week grid. Leading and trailing cells carry the neighbouring
 * months' days so the shape of the month is legible even when they are drawn
 * dimmed -- or not drawn at all.
 */
static void BuildGrid(const SYSTEMTIME* today, int weekStart, Cell out[GRID_ROWS * GRID_COLS]) {
    int year = today->wYear, month = today->wMonth;
    int firstColumn = FirstColumnDay(weekStart);

    int leading = (WeekdayOf(year, month, 1) - firstColumn + 7) % 7;
    int inMonth = DaysInMonth(year, month);
    int prevMonth = (month == 1) ? 12 : month - 1;
    int prevDays  = DaysInMonth((month == 1) ? year - 1 : year, prevMonth);

    for (int i = 0; i < GRID_ROWS * GRID_COLS; i++) {
        Cell* cell = &out[i];
        int offset = i - leading;          /* 0-based day of the current month */

        if (offset < 0) {
            cell->day = prevDays + offset + 1;
            cell->outside = true;
        } else if (offset >= inMonth) {
            cell->day = offset - inMonth + 1;
            cell->outside = true;
        } else {
            cell->day = offset + 1;
            cell->outside = false;
        }

        int weekday = (firstColumn + (i % GRID_COLS)) % 7;
        cell->weekend = (weekday == 0 || weekday == 6);
        cell->today = !cell->outside && cell->day == (int)today->wDay;
    }
}

void Calendar_Paint(const WidgetSpec* spec, const SYSTEMTIME* today,
                    GpGraphics* gfx, int width, int height) {
    if (!spec || !today || !gfx || width <= 0 || height <= 0) return;

    const WidgetStyle* s = &spec->style;
    const CalendarOptions* c = &spec->calendar;

    Drawing_Surface(gfx, s, (float)width, (float)height);

    float pad = s->padding;
    float x = pad, y = pad;
    float w = (float)width - pad * 2.0f;
    float h = (float)height - pad * 2.0f;
    if (w <= 0.0f || h <= 0.0f) return;

    /* The header carries the widget's full styling; the grid is drawn plain. */
    if (c->show_header) {
        WCHAR text[LW_FORMAT_LEN * 2];
        TimeFmt_Format(today, c->header_format, text, LW_FORMAT_LEN * 2);

        float headerH = s->font_size * 1.6f;
        if (headerH > h * 0.4f) headerH = h * 0.4f;

        GpRectF box = { x, y, w, headerH };
        TextRun run = Drawing_Run(s, text, box);
        run.color   = c->header_color;
        run.align_v = ALIGN_CENTER;
        run.no_wrap = true;
        Drawing_Text(gfx, &run);

        y += headerH;
        h -= headerH;
    }

    /* The week column is narrower than a day column; it only holds two digits. */
    float weekW = c->show_week_numbers ? (w / (GRID_COLS + 0.8f)) * 0.8f : 0.0f;
    float gridX = x + weekW;
    float cellW = (w - weekW) / GRID_COLS;

    float weekdayH = c->show_weekdays ? s->font_size * 1.3f : 0.0f;
    if (weekdayH > h * 0.25f) weekdayH = h * 0.25f;
    float cellH = (h - weekdayH) / GRID_ROWS;
    if (cellW <= 1.0f || cellH <= 1.0f) return;

    int firstColumn = FirstColumnDay(c->week_start);

    DrawingFont* smallFont = Drawing_OpenFont(s->font_family, s->font_size * 0.72f,
                                          s->font_style);
    DrawingFont* dayFont = Drawing_OpenFont(s->font_family, s->font_size * c->day_scale,
                                        s->font_style);
    /* "small" is a macro in rpcndr.h, which windows.h drags in. */
    if (!dayFont) { Drawing_CloseFont(smallFont); return; }

    if (c->show_weekdays && smallFont) {
        for (int i = 0; i < GRID_COLS; i++) {
            /* TimeFmt indexes from Monday; the grid counts from Sunday. */
            int weekday = (firstColumn + i) % 7;
            const WCHAR* name = TimeFmt_ShortestDayName((weekday + 6) % 7);
            GpRectF box = { gridX + cellW * i, y, cellW, weekdayH };
            Drawing_Cell(gfx, smallFont, name, box, c->weekday_color, ALIGN_CENTER, ALIGN_CENTER);
        }
    }

    float gridY = y + weekdayH;

    Cell cells[GRID_ROWS * GRID_COLS];
    BuildGrid(today, c->week_start, cells);

    if (c->show_week_numbers && smallFont) {
        /*
         * Numbered from each row's own first day, which is the only date in
         * the row guaranteed to exist in the month the row belongs to.
         */
        for (int row = 0; row < GRID_ROWS; row++) {
            const Cell* cell = &cells[row * GRID_COLS];
            int year = (int)today->wYear;
            int month = (int)today->wMonth;
            if (cell->outside && row == 0)                  { month--; if (month < 1) { month = 12; year--; } }
            else if (cell->outside && row > 0)              { month++; if (month > 12) { month = 1; year++; } }

            WCHAR text[8];
            _snwprintf(text, 8, L"%d", IsoWeek(year, month, cell->day));
            text[7] = L'\0';

            GpRectF box = { x, gridY + cellH * row, weekW, cellH };
            Drawing_Cell(gfx, smallFont, text, box, c->outside_color, ALIGN_CENTER, ALIGN_CENTER);
        }
    }

    for (int i = 0; i < GRID_ROWS * GRID_COLS; i++) {
        const Cell* cell = &cells[i];
        if (cell->outside && !c->show_outside_days) continue;

        float cx = gridX + cellW * (i % GRID_COLS);
        float cy = gridY + cellH * (i / GRID_COLS);
        GpRectF box = { cx, cy, cellW, cellH };

        ARGB color = cell->outside ? c->outside_color
                   : (cell->weekend ? c->weekend_color : s->text_color);

        if (cell->today) {
            /* A disc rather than a square: it survives any cell aspect ratio. */
            float size = (cellW < cellH ? cellW : cellH) * 0.86f;
            GpSolidFill* brush = NULL;
            if (GdipCreateSolidFill(c->today_color, &brush) == 0) {
                GdipFillEllipse(gfx, (GpBrush*)brush,
                                cx + (cellW - size) * 0.5f, cy + (cellH - size) * 0.5f,
                                size, size);
                GdipDeleteBrush((GpBrush*)brush);
            }
            color = c->today_text_color;
        }

        WCHAR text[8];
        _snwprintf(text, 8, L"%d", cell->day);
        text[7] = L'\0';
        Drawing_Cell(gfx, dayFont, text, box, color, ALIGN_CENTER, ALIGN_CENTER);
    }

    Drawing_CloseFont(dayFont);
    Drawing_CloseFont(smallFont);
}

/* ─────────────────────────── widget plumbing ─────────────────────────── */

static int DayStamp(const SYSTEMTIME* st) {
    return st->wYear * 10000 + st->wMonth * 100 + st->wDay;
}

/*
 * A month view changes once a day. Waking at midnight exactly would be
 * elegant and fragile -- a sleeping machine, a clock correction or a timezone
 * change all move the boundary -- so it looks in every quarter of an hour and
 * only repaints when the date has actually turned over. The check itself is
 * a GetLocalTime and an integer compare.
 */
static UINT Calendar_NextInterval(Widget* base) {
    (void)base;
    return 15u * 60u * 1000u;
}

static void Calendar_Render(Widget* base, GpGraphics* gfx, int width, int height) {
    CalendarWidget* w = (CalendarWidget*)base;
    SYSTEMTIME now;
    GetLocalTime(&now);
    w->last_day = DayStamp(&now);
    Calendar_Paint(&w->spec, &now, gfx, width, height);
}

static void Calendar_OnTimer(Widget* base) {
    CalendarWidget* w = (CalendarWidget*)base;
    SYSTEMTIME now;
    GetLocalTime(&now);
    if (DayStamp(&now) != w->last_day) base->needs_render = true;
}

static void Calendar_Destroy(Widget* base) {
    free(base);
}

static const WidgetVtable kCalendarVtable = {
    Calendar_Render,
    Calendar_OnTimer,
    Calendar_NextInterval,
    Calendar_Destroy,
    NULL
};

bool CalendarWidget_Create(HINSTANCE hInstance, const WidgetSpec* spec) {
    CalendarWidget* w = (CalendarWidget*)calloc(1, sizeof(CalendarWidget));
    if (!w) return false;

    w->spec = *spec;
    w->last_day = -1;

    if (!Widget_Init(&w->base, hInstance, &kCalendarVtable, spec, 60000)) {
        free(w);
        return false;
    }
    Widget_Render(&w->base);
    return true;
}
