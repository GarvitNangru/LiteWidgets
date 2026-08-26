#include "drawing.h"

void Drawing_RoundedRect(GpGraphics* gfx, ARGB fillColor, ARGB borderColor,
                         float borderWidth, float x, float y, float w, float h,
                         float radius) {
    if (!gfx || w <= 0 || h <= 0) return;

    /* Clamp radius */
    float maxR = (w < h ? w : h) / 2.0f;
    if (radius > maxR) radius = maxR;

    /* Sharp rectangle fallback */
    if (radius <= 0.5f) {
        if (fillColor & 0xFF000000) {
            GpSolidFill* brush = NULL;
            GdipCreateSolidFill(fillColor, &brush);
            GdipFillRectangle(gfx, (GpBrush*)brush, x, y, w, h);
            GdipDeleteBrush((GpBrush*)brush);
        }
        if (borderWidth > 0.0f && (borderColor & 0xFF000000)) {
            GpPen* pen = NULL;
            GdipCreatePen1(borderColor, borderWidth, UnitPixel, &pen);
            GdipDrawRectangle(gfx, pen, x, y, w, h);
            GdipDeletePen(pen);
        }
        return;
    }

    /* Build rounded rect path with 4 corner arcs */
    GpPath* path = NULL;
    GdipCreatePath(FillModeAlternate, &path);

    float d = radius * 2.0f;
    GdipAddPathArc(path, x,         y,         d, d, 180.0f, 90.0f);  /* Top-left */
    GdipAddPathArc(path, x + w - d, y,         d, d, 270.0f, 90.0f);  /* Top-right */
    GdipAddPathArc(path, x + w - d, y + h - d, d, d,   0.0f, 90.0f);  /* Bottom-right */
    GdipAddPathArc(path, x,         y + h - d, d, d,  90.0f, 90.0f);  /* Bottom-left */
    GdipClosePathFigure(path);

    /* Fill */
    if (fillColor & 0xFF000000) {
        GpSolidFill* brush = NULL;
        GdipCreateSolidFill(fillColor, &brush);
        GdipFillPath(gfx, (GpBrush*)brush, path);
        GdipDeleteBrush((GpBrush*)brush);
    }

    /* Border */
    if (borderWidth > 0.0f && (borderColor & 0xFF000000)) {
        GpPen* pen = NULL;
        GdipCreatePen1(borderColor, borderWidth, UnitPixel, &pen);
        GdipDrawPath(gfx, pen, path);
        GdipDeletePen(pen);
    }

    GdipDeletePath(path);
}

void Drawing_Text(GpGraphics* gfx, const WCHAR* text, const GpRectF* layoutRect,
                  const WCHAR* fontFamily, float fontSize, INT fontStyle,
                  ARGB color, int alignH, int alignV) {
    if (!gfx || !text || !layoutRect) return;

    /* Font */
    GpFontFamily* family = NULL;
    GdipCreateFontFamilyFromName(fontFamily, NULL, &family);
    if (!family) {
        GdipCreateFontFamilyFromName(L"Segoe UI", NULL, &family);
    }
    if (!family) return;

    GpFont* font = NULL;
    GdipCreateFont(family, fontSize, fontStyle, UnitPixel, &font);
    if (!font) {
        GdipDeleteFontFamily(family);
        return;
    }

    /* String format with alignment */
    GpStringFormat* fmt = NULL;
    GdipCreateStringFormat(0, LANG_NEUTRAL, &fmt);
    GdipSetStringFormatAlign(fmt, (GpStringAlignment)alignH);
    GdipSetStringFormatLineAlign(fmt, (GpStringAlignment)alignV);

    /* Brush */
    GpSolidFill* brush = NULL;
    GdipCreateSolidFill(color, &brush);

    /* Draw */
    GdipDrawString(gfx, text, -1, font, layoutRect, fmt, (GpBrush*)brush);

    /* Cleanup */
    GdipDeleteBrush((GpBrush*)brush);
    GdipDeleteStringFormat(fmt);
    GdipDeleteFont(font);
    GdipDeleteFontFamily(family);
}

void Drawing_WidgetBackground(GpGraphics* gfx, const WidgetStyle* style, int width, int height) {
    Drawing_RoundedRect(gfx, style->bg_color, style->border_color,
                        style->border_width, 0.0f, 0.0f,
                        (float)width, (float)height, style->corner_radius);
}
