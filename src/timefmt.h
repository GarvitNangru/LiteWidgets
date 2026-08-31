#pragma once
#ifndef TIMEFMT_H
#define TIMEFMT_H

#include <windows.h>

/*
 * Locale-aware date/time formatting with .NET-style tokens.
 *
 *   H HH     hour, 24-hour            h hh   hour, 12-hour
 *   m mm     minute                   s ss   second
 *   t tt     AM/PM designator         f      tenths of a second
 *   d dd     day of month             ddd    short weekday   dddd  weekday
 *   M MM     month number             MMM    short month     MMMM  month
 *   yy yyyy  year
 *
 * Anything else is copied through. Use \ to escape a single character and
 * 'single quotes' to pass a literal run, e.g. "h:mm tt 'on' dddd".
 */
void TimeFmt_Format(const SYSTEMTIME* st, const WCHAR* pattern, WCHAR* out, size_t cap);

/* Refresh cached locale strings; call after WM_SETTINGCHANGE. */
void TimeFmt_InvalidateLocale(void);

#endif /* TIMEFMT_H */
