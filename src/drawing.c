#include "drawing.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define ALPHA_OF(c) (((c) >> 24) & 0xFFu)
#define VISIBLE(c)  (ALPHA_OF(c) != 0)

/* ─────────────────────────── geometry ─────────────────────────── */

GpPath* Drawing_RoundedPath(float x, float y, float w, float h, float radius) {
    if (w <= 0.0f || h <= 0.0f) return NULL;

    float maxR = (w < h ? w : h) * 0.5f;
    if (radius > maxR) radius = maxR;
    if (radius < 0.0f) radius = 0.0f;

    GpPath* path = NULL;
    if (GdipCreatePath(FillModeWinding, &path) != 0 || !path) return NULL;

    if (radius <= 0.5f) {
        GdipAddPathRectangle(path, x, y, w, h);
        return path;
    }

    float d = radius * 2.0f;
    GdipAddPathArc(path, x,         y,         d, d, 180.0f, 90.0f);
    GdipAddPathArc(path, x + w - d, y,         d, d, 270.0f, 90.0f);
    GdipAddPathArc(path, x + w - d, y + h - d, d, d,   0.0f, 90.0f);
    GdipAddPathArc(path, x,         y + h - d, d, d,  90.0f, 90.0f);
    GdipClosePathFigure(path);
    return path;
}

GpBrush* Drawing_GradientBrush(const GpRectF* rect, ARGB c1, ARGB c2, int gradient) {
    if (!rect || gradient == GRAD_NONE || !VISIBLE(c2)) return NULL;
    if (rect->Width <= 0.0f || rect->Height <= 0.0f) return NULL;

    GpLinearGradientMode mode;
    switch (gradient) {
        case GRAD_HORIZONTAL:    mode = LinearGradientModeHorizontal;       break;
        case GRAD_DIAGONAL:      mode = LinearGradientModeForwardDiagonal;  break;
        case GRAD_DIAGONAL_BACK: mode = LinearGradientModeBackwardDiagonal; break;
        default:                 mode = LinearGradientModeVertical;         break;
    }

    /* Inflate by a pixel so the final row/column does not sample the wrap. */
    GpRectF r = { rect->X - 1.0f, rect->Y - 1.0f, rect->Width + 2.0f, rect->Height + 2.0f };

    GpLineGradient* brush = NULL;
    if (GdipCreateLineBrushFromRect(&r, c1, c2, mode, WrapModeTileFlipXY, &brush) != 0)
        return NULL;
    GdipSetLineGammaCorrection(brush, TRUE);
    return (GpBrush*)brush;
}

void Drawing_Panel(GpGraphics* gfx, ARGB fill, ARGB fill2, int gradient,
                   ARGB borderColor, float borderWidth,
                   float x, float y, float w, float h, float radius) {
    if (!gfx) return;
    if (!VISIBLE(fill) && !(borderWidth > 0.0f && VISIBLE(borderColor))) return;

    GpPath* path = Drawing_RoundedPath(x, y, w, h, radius);
    if (!path) return;

    if (VISIBLE(fill)) {
        GpRectF rect = { x, y, w, h };
        GpBrush* brush = Drawing_GradientBrush(&rect, fill, fill2, gradient);
        if (brush) {
            GdipFillPath(gfx, brush, path);
            GdipDeleteBrush(brush);
        } else {
            GpSolidFill* solid = NULL;
            if (GdipCreateSolidFill(fill, &solid) == 0) {
                GdipFillPath(gfx, (GpBrush*)solid, path);
                GdipDeleteBrush((GpBrush*)solid);
            }
        }
    }

    if (borderWidth > 0.0f && VISIBLE(borderColor)) {
        GpPen* pen = NULL;
        if (GdipCreatePen1(borderColor, borderWidth, UnitPixel, &pen) == 0) {
            /* Inset keeps the stroke inside the layered-window bitmap. */
            GdipSetPenMode(pen, PenAlignmentInset);
            GdipDrawPath(gfx, pen, path);
            GdipDeletePen(pen);
        }
    }

    GdipDeletePath(path);
}

void Drawing_Surface(GpGraphics* gfx, const WidgetStyle* style, float w, float h) {
    if (!gfx || !style) return;
    Drawing_Panel(gfx, style->bg_color, style->bg_color2, style->bg_gradient,
                  style->border_color, style->border_width,
                  0.0f, 0.0f, w, h, style->corner_radius);
}

/* ─────────────────────────── text plumbing ─────────────────────────── */

TextRun Drawing_Run(const WidgetStyle* s, const WCHAR* text, GpRectF bounds) {
    TextRun run;
    memset(&run, 0, sizeof(run));
    run.text           = text;
    run.bounds         = bounds;
    run.font_family    = s->font_family;
    run.font_size      = s->font_size;
    run.font_style     = s->font_style;
    run.align_h        = s->align_h;
    run.align_v        = s->align_v;
    run.color          = s->text_color;
    run.color2         = s->text_color2;
    run.gradient       = s->text_gradient;
    run.letter_spacing = s->letter_spacing;
    run.line_spacing   = s->line_spacing;
    run.shadow_color   = s->shadow_color;
    run.shadow_dx      = s->shadow_offset_x;
    run.shadow_dy      = s->shadow_offset_y;
    run.glow_color     = s->glow_color;
    run.glow_radius    = s->glow_radius;
    run.outline_color  = s->outline_color;
    run.outline_width  = s->outline_width;
    return run;
}

WCHAR* Drawing_Transform(WCHAR* dst, size_t cap, const WCHAR* src, int transform) {
    if (!dst || cap == 0) return dst;
    if (!src) { dst[0] = L'\0'; return dst; }

    wcsncpy(dst, src, cap - 1);
    dst[cap - 1] = L'\0';
    if (transform == TEXT_UPPER)      CharUpperW(dst);
    else if (transform == TEXT_LOWER) CharLowerW(dst);
    return dst;
}

static GpFontFamily* MakeFamily(const WCHAR* name) {
    GpFontFamily* family = NULL;
    if (name && name[0])
        GdipCreateFontFamilyFromName(name, NULL, &family);
    if (!family) GdipCreateFontFamilyFromName(L"Segoe UI", NULL, &family);
    if (!family) GdipCreateFontFamilyFromName(L"Arial", NULL, &family);
    return family;
}

/*
 * Built on the typographic format rather than the default one.
 *
 * GDI+'s default string format pads every string by about a sixth of an em on
 * each side. That padding is invisible until something has to know where a
 * glyph actually landed — the notes editor placing a caret, say — at which
 * point measured and drawn positions disagree by a couple of pixels. Sharing
 * one convention between drawing and measuring is worth the hair's-breadth
 * shift it causes elsewhere.
 */
static GpStringFormat* MakeFormat(int alignH, int alignV, bool noWrap) {
    GpStringFormat* generic = NULL;
    GpStringFormat* fmt = NULL;
    if (GdipStringFormatGetGenericTypographic(&generic) == 0 && generic)
        GdipCloneStringFormat(generic, &fmt);
    if (!fmt && GdipCreateStringFormat(0, LANG_NEUTRAL, &fmt) != 0) return NULL;

    GdipSetStringFormatAlign(fmt, (GpStringAlignment)alignH);
    GdipSetStringFormatLineAlign(fmt, (GpStringAlignment)alignV);
    GdipSetStringFormatFlags(fmt, StringFormatFlagsNoClip | StringFormatFlagsNoFitBlackBox |
                                  (noWrap ? StringFormatFlagsNoWrap : 0));
    GdipSetStringFormatTrimming(fmt, StringTrimmingNone);
    return fmt;
}

/* Tight measurement format — no trailing pad, no wrapping. */
static GpStringFormat* MakeMeasureFormat(void) {
    GpStringFormat* generic = NULL;
    GpStringFormat* fmt = NULL;
    if (GdipStringFormatGetGenericTypographic(&generic) == 0 && generic)
        GdipCloneStringFormat(generic, &fmt);
    if (!fmt && GdipCreateStringFormat(0, LANG_NEUTRAL, &fmt) != 0)
        return NULL;
    GdipSetStringFormatAlign(fmt, StringAlignmentNear);
    GdipSetStringFormatLineAlign(fmt, StringAlignmentNear);
    GdipSetStringFormatFlags(fmt, StringFormatFlagsNoWrap | StringFormatFlagsNoClip |
                                  StringFormatFlagsMeasureTrailingSpaces);
    return fmt;
}

/*
 * Shared glyph-run layout.
 *
 * Walks `text` one character at a time, optionally appending each glyph to
 * `path`, and returns the total advance. Passing a NULL path turns this into
 * a measuring pass, which is how the aligned start position is worked out.
 */
static float LayoutGlyphs(GpGraphics* gfx, GpPath* path,
                          GpFontFamily* family, GpFont* font, GpStringFormat* measure,
                          const WCHAR* text, int len, float emSize, INT style,
                          float x, float y, float lineHeight, float tracking) {
    static const GpRectF kWide = { 0.0f, 0.0f, 8192.0f, 8192.0f };
    float advance = 0.0f;

    for (int i = 0; i < len; i++) {
        GpRectF box = { 0, 0, 0, 0 };
        GdipMeasureString(gfx, text + i, 1, font, &kWide, measure, &box, NULL, NULL);

        if (path && text[i] != L' ') {
            GpRectF cell = { x + advance, y, box.Width + emSize, lineHeight + emSize };
            GdipAddPathString(path, text + i, 1, family, style, emSize, &cell, measure);
        }
        advance += box.Width + tracking;
    }

    if (len > 0) advance -= tracking;   /* no trailing gap */
    return advance;
}

static bool NeedsPathPipeline(const TextRun* r) {
    return (r->gradient != GRAD_NONE && VISIBLE(r->color2))
        || VISIBLE(r->shadow_color)
        || (r->glow_radius > 0.0f && VISIBLE(r->glow_color))
        || (r->outline_width > 0.0f && VISIBLE(r->outline_color))
        || fabsf(r->letter_spacing) > 0.01f;
}

/* Plain, grid-fitted text. Cheapest path and the sharpest at small sizes. */
static void DrawPlain(GpGraphics* gfx, const TextRun* r, GpFontFamily* family) {
    GpFont* font = NULL;
    if (GdipCreateFont(family, r->font_size, r->font_style, UnitPixel, &font) != 0 || !font)
        return;

    GpStringFormat* fmt = MakeFormat(r->align_h, r->align_v, r->no_wrap);
    GpSolidFill* brush = NULL;
    if (fmt && GdipCreateSolidFill(r->color, &brush) == 0) {
        GdipDrawString(gfx, r->text, -1, font, &r->bounds, fmt, (GpBrush*)brush);
        GdipDeleteBrush((GpBrush*)brush);
    }
    if (fmt) GdipDeleteStringFormat(fmt);
    GdipDeleteFont(font);
}

static float LineHeight(const TextRun* r, GpFont* font, GpGraphics* gfx) {
    REAL h = r->font_size * 1.2f;
    GdipGetFontHeight(font, gfx, &h);
    if (r->line_spacing > 0.01f) h *= r->line_spacing;
    return h;
}

/*
 * Build a glyph path for the run.
 *
 * With zero tracking GDI+ lays the string out for us — including wrapping.
 * With tracking we place each character ourselves, which also means the run
 * is treated as a single line.
 */
static GpPath* BuildGlyphPath(GpGraphics* gfx, const TextRun* r, GpFontFamily* family) {
    GpPath* path = NULL;
    if (GdipCreatePath(FillModeWinding, &path) != 0 || !path) return NULL;

    if (fabsf(r->letter_spacing) <= 0.01f) {
        GpStringFormat* fmt = MakeFormat(r->align_h, r->align_v, r->no_wrap);
        if (!fmt) { GdipDeletePath(path); return NULL; }
        GdipAddPathString(path, r->text, -1, family, r->font_style, r->font_size,
                          &r->bounds, fmt);
        GdipDeleteStringFormat(fmt);
        return path;
    }

    GpStringFormat* measure = MakeMeasureFormat();
    GpFont* font = NULL;
    if (!measure || GdipCreateFont(family, r->font_size, r->font_style, UnitPixel, &font) != 0) {
        if (measure) GdipDeleteStringFormat(measure);
        GdipDeletePath(path);
        return NULL;
    }

    int len = (int)wcslen(r->text);
    float lineHeight = LineHeight(r, font, gfx);
    float total = LayoutGlyphs(gfx, NULL, family, font, measure, r->text, len,
                               r->font_size, r->font_style, 0.0f, 0.0f,
                               lineHeight, r->letter_spacing);

    float x = r->bounds.X;
    if (r->align_h == ALIGN_CENTER)   x += (r->bounds.Width - total) * 0.5f;
    else if (r->align_h == ALIGN_FAR) x += (r->bounds.Width - total);

    float y = r->bounds.Y;
    if (r->align_v == ALIGN_CENTER)   y += (r->bounds.Height - lineHeight) * 0.5f;
    else if (r->align_v == ALIGN_FAR) y += (r->bounds.Height - lineHeight);

    LayoutGlyphs(gfx, path, family, font, measure, r->text, len,
                 r->font_size, r->font_style, x, y, lineHeight, r->letter_spacing);

    GdipDeleteFont(font);
    GdipDeleteStringFormat(measure);
    return path;
}

/*
 * Approximate a gaussian glow by stroking the glyph path with progressively
 * wider, fainter round pens. Four rings read as a soft halo and cost far less
 * than a real blur — which matters when this runs once a second, forever.
 */
static void StrokeGlow(GpGraphics* gfx, GpPath* path, ARGB color, float radius) {
    const int rings = 4;
    for (int i = rings; i >= 1; i--) {
        float width = radius * 2.0f * ((float)i / (float)rings);
        float falloff = 1.0f - ((float)i / (float)(rings + 1));
        GpPen* pen = NULL;
        if (GdipCreatePen1(Style_ScaleAlpha(color, falloff * 0.6f), width, UnitPixel, &pen) != 0)
            continue;
        GdipSetPenLineJoin(pen, LineJoinRound);
        GdipSetPenStartCap(pen, LineCapRound);
        GdipSetPenEndCap(pen, LineCapRound);
        GdipDrawPath(gfx, pen, path);
        GdipDeletePen(pen);
    }
}

static void FillGlyphs(GpGraphics* gfx, GpPath* path, const TextRun* r, ARGB color) {
    GpRectF bounds;
    if (r->gradient_rect.Width > 0.0f) {
        bounds = r->gradient_rect;
    } else if (r->gradient != GRAD_NONE && VISIBLE(r->color2)) {
        if (GdipGetPathWorldBounds(path, &bounds, NULL, NULL) != 0) bounds = r->bounds;
    } else {
        bounds = r->bounds;
    }

    GpBrush* brush = Drawing_GradientBrush(&bounds, color, r->color2, r->gradient);
    if (brush) {
        GdipFillPath(gfx, brush, path);
        GdipDeleteBrush(brush);
        return;
    }
    GpSolidFill* solid = NULL;
    if (GdipCreateSolidFill(color, &solid) == 0) {
        GdipFillPath(gfx, (GpBrush*)solid, path);
        GdipDeleteBrush((GpBrush*)solid);
    }
}

/* Glow -> shadow -> fill -> outline, in the order they stack visually. */
static void PaintPath(GpGraphics* gfx, GpPath* path, const TextRun* r, ARGB color) {
    if (r->glow_radius > 0.0f && VISIBLE(r->glow_color))
        StrokeGlow(gfx, path, r->glow_color, r->glow_radius);

    if (VISIBLE(r->shadow_color) && (r->shadow_dx != 0.0f || r->shadow_dy != 0.0f)) {
        GraphicsState state = 0;
        GdipSaveGraphics(gfx, &state);
        GdipTranslateWorldTransform(gfx, r->shadow_dx, r->shadow_dy, MatrixOrderPrepend);
        GpSolidFill* shadow = NULL;
        if (GdipCreateSolidFill(r->shadow_color, &shadow) == 0) {
            GdipFillPath(gfx, (GpBrush*)shadow, path);
            GdipDeleteBrush((GpBrush*)shadow);
        }
        GdipRestoreGraphics(gfx, state);
    }

    FillGlyphs(gfx, path, r, color);

    if (r->outline_width > 0.0f && VISIBLE(r->outline_color)) {
        GpPen* pen = NULL;
        if (GdipCreatePen1(r->outline_color, r->outline_width, UnitPixel, &pen) == 0) {
            GdipSetPenLineJoin(pen, LineJoinRound);
            GdipDrawPath(gfx, pen, path);
            GdipDeletePen(pen);
        }
    }
}

void Drawing_Text(GpGraphics* gfx, const TextRun* r) {
    if (!gfx || !r || !r->text || !r->text[0]) return;
    if (r->bounds.Width <= 0.0f || r->bounds.Height <= 0.0f) return;

    GpFontFamily* family = MakeFamily(r->font_family);
    if (!family) return;

    if (!NeedsPathPipeline(r)) {
        DrawPlain(gfx, r, family);
        GdipDeleteFontFamily(family);
        return;
    }

    GpPath* path = BuildGlyphPath(gfx, r, family);
    if (!path) {
        DrawPlain(gfx, r, family);
        GdipDeleteFontFamily(family);
        return;
    }

    PaintPath(gfx, path, r, r->color);

    GdipDeletePath(path);
    GdipDeleteFontFamily(family);
}

float Drawing_MeasureWidth(GpGraphics* gfx, const TextRun* r) {
    if (!gfx || !r || !r->text || !r->text[0]) return 0.0f;

    GpFontFamily* family = MakeFamily(r->font_family);
    if (!family) return 0.0f;

    GpStringFormat* measure = MakeMeasureFormat();
    GpFont* font = NULL;
    float width = 0.0f;

    if (measure && GdipCreateFont(family, r->font_size, r->font_style, UnitPixel, &font) == 0) {
        width = LayoutGlyphs(gfx, NULL, family, font, measure, r->text, (int)wcslen(r->text),
                             r->font_size, r->font_style, 0.0f, 0.0f,
                             r->font_size, r->letter_spacing);
        GdipDeleteFont(font);
    }
    if (measure) GdipDeleteStringFormat(measure);
    GdipDeleteFontFamily(family);
    return width;
}

/* ─────────────────────────── plain cells ─────────────────────────── */

struct DrawingFont {
    GpFontFamily*   family;
    GpFont*         font;
    GpStringFormat* format;
};

DrawingFont* Drawing_OpenFont(const WCHAR* family, float size, INT style) {
    if (size <= 0.0f) return NULL;

    DrawingFont* f = (DrawingFont*)calloc(1, sizeof(DrawingFont));
    if (!f) return NULL;

    f->family = MakeFamily(family);
    f->format = MakeFormat(ALIGN_CENTER, ALIGN_CENTER, true);
    if (!f->family || !f->format ||
        GdipCreateFont(f->family, size, style, UnitPixel, &f->font) != 0) {
        Drawing_CloseFont(f);
        return NULL;
    }
    return f;
}

void Drawing_CloseFont(DrawingFont* f) {
    if (!f) return;
    if (f->font)   GdipDeleteFont(f->font);
    if (f->format) GdipDeleteStringFormat(f->format);
    if (f->family) GdipDeleteFontFamily(f->family);
    free(f);
}

void Drawing_Cell(GpGraphics* gfx, DrawingFont* f, const WCHAR* text,
                  GpRectF box, ARGB color, int alignH, int alignV) {
    if (!gfx || !f || !text || !text[0] || !VISIBLE(color)) return;

    GdipSetStringFormatAlign(f->format, (GpStringAlignment)alignH);
    GdipSetStringFormatLineAlign(f->format, (GpStringAlignment)alignV);

    GpSolidFill* brush = NULL;
    if (GdipCreateSolidFill(color, &brush) != 0) return;
    GdipDrawString(gfx, text, -1, f->font, &box, f->format, (GpBrush*)brush);
    GdipDeleteBrush((GpBrush*)brush);
}

/* ─────────────────────────── metrics ─────────────────────────── */

struct DrawingMetrics {
    GpGraphics*     gfx;
    GpFontFamily*   family;
    GpFont*         font;
    GpStringFormat* measure;
    float           tracking;
    float           line_height;
};

DrawingMetrics* Drawing_OpenMetrics(GpGraphics* gfx, const TextRun* run) {
    if (!gfx || !run) return NULL;

    DrawingMetrics* m = (DrawingMetrics*)calloc(1, sizeof(DrawingMetrics));
    if (!m) return NULL;

    m->gfx      = gfx;
    m->tracking = run->letter_spacing;
    m->family   = MakeFamily(run->font_family);
    m->measure  = MakeMeasureFormat();

    if (!m->family || !m->measure ||
        GdipCreateFont(m->family, run->font_size, run->font_style, UnitPixel, &m->font) != 0) {
        Drawing_CloseMetrics(m);
        return NULL;
    }

    REAL height = run->font_size * 1.2f;
    GdipGetFontHeight(m->font, gfx, &height);
    if (run->line_spacing > 0.01f) height *= run->line_spacing;
    m->line_height = height;
    return m;
}

void Drawing_CloseMetrics(DrawingMetrics* m) {
    if (!m) return;
    if (m->font)    GdipDeleteFont(m->font);
    if (m->measure) GdipDeleteStringFormat(m->measure);
    if (m->family)  GdipDeleteFontFamily(m->family);
    free(m);
}

float Drawing_LineHeight(const DrawingMetrics* m) {
    return m ? m->line_height : 0.0f;
}

float Drawing_Extent(DrawingMetrics* m, const WCHAR* text, int count) {
    static const GpRectF kWide = { 0.0f, 0.0f, 16384.0f, 16384.0f };
    if (!m || !text || count <= 0) return 0.0f;

    /* Without tracking the whole span is one call, which is the common case. */
    if (fabsf(m->tracking) <= 0.01f) {
        GpRectF box = { 0, 0, 0, 0 };
        GdipMeasureString(m->gfx, text, count, m->font, &kWide, m->measure, &box, NULL, NULL);
        return box.Width;
    }

    float advance = 0.0f;
    for (int i = 0; i < count; i++) {
        GpRectF box = { 0, 0, 0, 0 };
        GdipMeasureString(m->gfx, text + i, 1, m->font, &kWide, m->measure, &box, NULL, NULL);
        advance += box.Width + m->tracking;
    }
    return advance - m->tracking;
}

void Drawing_TextSegments(GpGraphics* gfx, const TextRun* base,
                          const TextSegment* segments, int count) {
    if (!gfx || !base || !segments || count <= 0) return;
    if (base->bounds.Width <= 0.0f || base->bounds.Height <= 0.0f) return;

    GpFontFamily* family = MakeFamily(base->font_family);
    if (!family) return;

    GpStringFormat* measure = MakeMeasureFormat();
    GpFont* font = NULL;
    if (!measure || GdipCreateFont(family, base->font_size, base->font_style, UnitPixel, &font) != 0) {
        if (measure) GdipDeleteStringFormat(measure);
        GdipDeleteFontFamily(family);
        return;
    }

    float tracking = base->letter_spacing;
    float lineHeight = LineHeight(base, font, gfx);

    /* Pass 1 — total advance so the line can be aligned as a whole. */
    float total = 0.0f;
    for (int i = 0; i < count; i++) {
        if (!segments[i].text || !segments[i].text[0]) continue;
        int len = (int)wcslen(segments[i].text);
        total += LayoutGlyphs(gfx, NULL, family, font, measure, segments[i].text, len,
                              base->font_size, base->font_style, 0.0f, 0.0f,
                              lineHeight, tracking) + tracking;
    }
    if (total > 0.0f) total -= tracking;

    float x = base->bounds.X;
    if (base->align_h == ALIGN_CENTER)   x += (base->bounds.Width - total) * 0.5f;
    else if (base->align_h == ALIGN_FAR) x += (base->bounds.Width - total);

    float y = base->bounds.Y;
    if (base->align_v == ALIGN_CENTER)   y += (base->bounds.Height - lineHeight) * 0.5f;
    else if (base->align_v == ALIGN_FAR) y += (base->bounds.Height - lineHeight);

    /* One gradient across the whole line rather than per segment. */
    TextRun run = *base;
    if (run.gradient != GRAD_NONE && VISIBLE(run.color2) && run.gradient_rect.Width <= 0.0f) {
        GpRectF span = { x, y, total, lineHeight };
        run.gradient_rect = span;
    }

    /* Pass 2 — draw each piece in its own colour. */
    for (int i = 0; i < count; i++) {
        if (!segments[i].text || !segments[i].text[0]) continue;
        int len = (int)wcslen(segments[i].text);

        GpPath* path = NULL;
        if (GdipCreatePath(FillModeWinding, &path) != 0 || !path) break;

        float advance = LayoutGlyphs(gfx, path, family, font, measure, segments[i].text, len,
                                     base->font_size, base->font_style, x, y,
                                     lineHeight, tracking);

        ARGB color = VISIBLE(segments[i].color) ? segments[i].color : base->color;
        PaintPath(gfx, path, &run, color);
        GdipDeletePath(path);

        x += advance + tracking;
    }

    GdipDeleteFont(font);
    GdipDeleteStringFormat(measure);
    GdipDeleteFontFamily(family);
}
