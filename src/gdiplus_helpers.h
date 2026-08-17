#pragma once

#ifndef GDIP_HELPERS_H
#define GDIP_HELPERS_H

#include <windows.h>


/*
 * C-style GDI+ Flat API Declarations
 * These are necessary when using GDI+ from pure C, as the standard GDI+ headers
 * are C++-only wrappers.
 */

typedef int GpStatus;
typedef DWORD ARGB;
typedef float REAL;
#define WINGDIPAPI __stdcall


typedef enum {
    UnitWorld = 0,
    UnitDisplay = 1,
    UnitPixel = 2,
    UnitPoint = 3,
    UnitInch = 4,
    UnitDocument = 5,
    UnitMillimeter = 6
} GpUnit;

typedef enum {
    FontStyleRegular = 0,
    FontStyleBold = 1,
    FontStyleItalic = 2,
    FontStyleBoldItalic = 3,
    FontStyleUnderline = 4,
    FontStyleStrikeout = 8
} GpFontStyle;

typedef enum {
    SmoothingModeInvalid = -1,
    SmoothingModeDefault = 0,
    SmoothingModeHighSpeed = 1,
    SmoothingModeHighQuality = 2,
    SmoothingModeNone = 3,
    SmoothingModeAntiAlias = 4,
    SmoothingModeAntiAlias8x4 = 5,
    SmoothingModeAntiAlias8x8 = 6
} GpSmoothingMode;

typedef enum {
    TextRenderingHintSystemDefault = 0,
    TextRenderingHintSingleBitPerPixelGridFit,
    TextRenderingHintSingleBitPerPixel,
    TextRenderingHintAntiAliasGridFit,
    TextRenderingHintAntiAlias,
    TextRenderingHintClearTypeGridFit
} GpTextRenderingHint;

typedef struct {
    float X;
    float Y;
    float Width;
    float Height;
} GpRectF;

typedef struct {
    UINT32 GdiplusVersion;
    void* DebugEventCallback;
    BOOL SuppressBackgroundThread;
    BOOL SuppressExternalCodecs;
} GpStartupInput;

typedef struct GpGraphics GpGraphics;
typedef struct GpBrush GpBrush;
typedef struct GpSolidFill GpSolidFill;
typedef struct GpFontFamily GpFontFamily;
typedef struct GpFont GpFont;
typedef struct GpImage GpImage;
typedef struct GpBitmap GpBitmap;

/* GDI+ Core */
GpStatus WINGDIPAPI GdiplusStartup(ULONG_PTR *token, const GpStartupInput *input, void *output);
void WINGDIPAPI GdiplusShutdown(ULONG_PTR token);

/* Graphics */
GpStatus WINGDIPAPI GdipCreateFromHDC(HDC hdc, GpGraphics **graphics);
GpStatus WINGDIPAPI GdipDeleteGraphics(GpGraphics *graphics);
GpStatus WINGDIPAPI GdipSetSmoothingMode(GpGraphics *graphics, GpSmoothingMode smoothingMode);
GpStatus WINGDIPAPI GdipSetTextRenderingHint(GpGraphics *graphics, GpTextRenderingHint mode);

/* Brushes */
GpStatus WINGDIPAPI GdipCreateSolidFill(ARGB color, GpSolidFill **brush);
GpStatus WINGDIPAPI GdipDeleteBrush(GpBrush *brush);

/* Text */
GpStatus WINGDIPAPI GdipCreateFontFamilyFromName(const WCHAR *name, void *fontCollection, GpFontFamily **fontFamily);
GpStatus WINGDIPAPI GdipDeleteFontFamily(GpFontFamily *fontFamily);
GpStatus WINGDIPAPI GdipCreateFont(GpFontFamily *fontFamily, REAL emSize, INT style, GpUnit unit, GpFont **font);
GpStatus WINGDIPAPI GdipDeleteFont(GpFont *font);
GpStatus WINGDIPAPI GdipDrawString(GpGraphics *graphics, const WCHAR *string, INT length, const GpFont *font, const GpRectF *layoutRect, const void *stringFormat, const GpBrush *brush);

/* Drawing */
GpStatus WINGDIPAPI GdipFillRectangleI(GpGraphics *graphics, GpBrush *brush, INT x, INT y, INT width, INT height);

/* Images */
GpStatus WINGDIPAPI GdipCreateBitmapFromFile(const WCHAR *filename, GpBitmap **bitmap);
GpStatus WINGDIPAPI GdipDrawImageRectI(GpGraphics *graphics, GpImage *image, INT x, INT y, INT width, INT height);
GpStatus WINGDIPAPI GdipDisposeImage(GpImage *image);
GpStatus WINGDIPAPI GdipGetImageWidth(GpImage *image, UINT *width);
GpStatus WINGDIPAPI GdipGetImageHeight(GpImage *image, UINT *height);

#endif // GDIP_HELPERS_H
