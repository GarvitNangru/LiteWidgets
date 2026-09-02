#pragma once
#ifndef GAUGE_H
#define GAUGE_H

#include <windows.h>
#include <stdbool.h>

#include "../spec.h"
#include "../gdiplus_helpers.h"

/* Create a live system gauge from a fully finalized spec. */
bool GaugeWidget_Create(HINSTANCE hInstance, const WidgetSpec* spec);

/*
 * One reading, already turned into the two things a gauge draws: a
 * percentage and a line of supporting detail.
 */
typedef struct {
    float percent;         /* 0..100 */
    bool  available;       /* false when the machine has no such reading */
    WCHAR detail[64];      /* "6.1 / 16.0 GB", "1h 20m left", or empty */
} GaugeReading;

/*
 * Take a reading for the gauge described by `spec`.
 *
 * CPU load is the difference between two samples, so the sample is shared
 * across every gauge rather than taken per widget: two of them measuring
 * different windows would disagree about the same number.
 */
void Gauge_Read(const WidgetSpec* spec, GaugeReading* out);

/*
 * Stateless painter, shared by the live widget and the settings preview, so
 * a preview is guaranteed to look exactly like the real thing.
 */
void Gauge_Paint(const WidgetSpec* spec, const GaugeReading* reading,
                 GpGraphics* gfx, int width, int height);

#endif /* GAUGE_H */
