#include "timefmt.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/*
 * Locale strings are read once and cached: a clock re-formats itself every
 * second, and hitting the locale database that often would be wasteful.
 */
typedef struct {
    bool  loaded;
    WCHAR day[7][48];        /* Monday .. Sunday */
    WCHAR dayShort[7][24];
    WCHAR dayTiny[7][8];     /* one or two letters, for a calendar header */
    int   firstDay;          /* 0 = Monday .. 6 = Sunday */
    WCHAR month[12][48];
    WCHAR monthShort[12][24];
    WCHAR am[16];
    WCHAR pm[16];
} LocaleNames;

static LocaleNames g_locale;

static void ReadLocale(LCTYPE type, WCHAR* out, int cap, const WCHAR* fallback) {
    if (GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, type, out, cap) > 0) return;
    wcsncpy(out, fallback, (size_t)cap - 1);
    out[cap - 1] = L'\0';
}

static void EnsureLocale(void) {
    if (g_locale.loaded) return;

    static const WCHAR* fbDay[7]   = { L"Monday", L"Tuesday", L"Wednesday", L"Thursday",
                                       L"Friday", L"Saturday", L"Sunday" };
    static const WCHAR* fbMonth[12] = { L"January", L"February", L"March", L"April",
                                        L"May", L"June", L"July", L"August",
                                        L"September", L"October", L"November", L"December" };

    for (int i = 0; i < 7; i++) {
        ReadLocale(LOCALE_SDAYNAME1 + i,        g_locale.day[i],      48, fbDay[i]);
        ReadLocale(LOCALE_SABBREVDAYNAME1 + i,  g_locale.dayShort[i], 24, fbDay[i]);
        ReadLocale(LOCALE_SSHORTESTDAYNAME1 + i, g_locale.dayTiny[i],  8, fbDay[i]);
    }

    /*
     * LOCALE_IFIRSTDAYOFWEEK is already Monday-based, which is the convention
     * this module uses everywhere. It comes back as text, not a number.
     */
    WCHAR first[8];
    ReadLocale(LOCALE_IFIRSTDAYOFWEEK, first, 8, L"0");
    g_locale.firstDay = (first[0] >= L'0' && first[0] <= L'6') ? (first[0] - L'0') : 0;
    for (int i = 0; i < 12; i++) {
        ReadLocale(LOCALE_SMONTHNAME1 + i,       g_locale.month[i],      48, fbMonth[i]);
        ReadLocale(LOCALE_SABBREVMONTHNAME1 + i, g_locale.monthShort[i], 24, fbMonth[i]);
    }
    ReadLocale(LOCALE_S1159, g_locale.am, 16, L"AM");
    ReadLocale(LOCALE_S2359, g_locale.pm, 16, L"PM");
    g_locale.loaded = true;
}

void TimeFmt_InvalidateLocale(void) {
    g_locale.loaded = false;
}

const WCHAR* TimeFmt_ShortestDayName(int mondayIndex) {
    EnsureLocale();
    if (mondayIndex < 0 || mondayIndex > 6) mondayIndex = 0;
    return g_locale.dayTiny[mondayIndex];
}

int TimeFmt_FirstDayOfWeek(void) {
    EnsureLocale();
    return g_locale.firstDay;
}

/* Number of consecutive copies of `c` starting at `p`. */
static int RunLength(const WCHAR* p, WCHAR c) {
    int n = 0;
    while (p[n] == c) n++;
    return n;
}

typedef struct { WCHAR* p; WCHAR* end; } Sink;

static void PutStr(Sink* s, const WCHAR* text) {
    while (*text && s->p < s->end) *s->p++ = *text++;
}

static void PutNum(Sink* s, int value, int width) {
    WCHAR buf[16];
    _snwprintf(buf, 16, width >= 2 ? L"%02d" : L"%d", value);
    buf[15] = L'\0';
    PutStr(s, buf);
}

void TimeFmt_Format(const SYSTEMTIME* st, const WCHAR* pattern, WCHAR* out, size_t cap) {
    if (!out || cap == 0) return;
    out[0] = L'\0';
    if (!st || !pattern) return;

    EnsureLocale();

    Sink sink = { out, out + (cap - 1) };
    int dayIndex   = (st->wDayOfWeek == 0) ? 6 : st->wDayOfWeek - 1;  /* Monday-based */
    int monthIndex = (st->wMonth >= 1 && st->wMonth <= 12) ? st->wMonth - 1 : 0;
    int hour12     = st->wHour % 12;
    if (hour12 == 0) hour12 = 12;

    const WCHAR* p = pattern;
    while (*p && sink.p < sink.end) {
        WCHAR c = *p;
        int n;

        switch (c) {
            case L'\\':
                if (p[1]) { if (sink.p < sink.end) *sink.p++ = p[1]; p += 2; }
                else p++;
                continue;

            case L'\'':
                p++;
                while (*p && *p != L'\'' && sink.p < sink.end) *sink.p++ = *p++;
                if (*p == L'\'') p++;
                continue;

            case L'H':
                n = RunLength(p, L'H'); PutNum(&sink, st->wHour, n); p += n; continue;

            case L'h':
                n = RunLength(p, L'h'); PutNum(&sink, hour12, n); p += n; continue;

            case L'm':
                n = RunLength(p, L'm'); PutNum(&sink, st->wMinute, n); p += n; continue;

            case L's':
                n = RunLength(p, L's'); PutNum(&sink, st->wSecond, n); p += n; continue;

            case L'f':
                n = RunLength(p, L'f');
                PutNum(&sink, st->wMilliseconds / 100, 1);
                p += n; continue;

            case L't': {
                n = RunLength(p, L't');
                const WCHAR* designator = (st->wHour >= 12) ? g_locale.pm : g_locale.am;
                if (n == 1) { if (sink.p < sink.end && designator[0]) *sink.p++ = designator[0]; }
                else PutStr(&sink, designator);
                p += n; continue;
            }

            case L'd':
                n = RunLength(p, L'd');
                if (n >= 4)      PutStr(&sink, g_locale.day[dayIndex]);
                else if (n == 3) PutStr(&sink, g_locale.dayShort[dayIndex]);
                else             PutNum(&sink, st->wDay, n);
                p += n; continue;

            case L'M':
                n = RunLength(p, L'M');
                if (n >= 4)      PutStr(&sink, g_locale.month[monthIndex]);
                else if (n == 3) PutStr(&sink, g_locale.monthShort[monthIndex]);
                else             PutNum(&sink, st->wMonth, n);
                p += n; continue;

            case L'y':
                n = RunLength(p, L'y');
                if (n <= 2) PutNum(&sink, st->wYear % 100, 2);
                else        PutNum(&sink, st->wYear, 4);
                p += n; continue;

            default:
                *sink.p++ = c;
                p++;
                continue;
        }
    }

    *sink.p = L'\0';
}
