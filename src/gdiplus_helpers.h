#pragma once
#ifndef GDIP_HELPERS_H
#define GDIP_HELPERS_H

#include <windows.h>

/*
 * C-style GDI+ Flat API Declarations
 * These are necessary when using GDI+ from pure C, as the standard
 * GDI+ headers are C++-only wrappers.
 */

typedef int GpStatus;
typedef DWORD ARGB;
typedef float REAL;
#define WINGDIPAPI __stdcall

/* ========== Enums ========== */

typedef enum {
    UnitWorld = 0, UnitDisplay = 1, UnitPixel = 2,
    UnitPoint = 3, UnitInch = 4, UnitDocument = 5, UnitMillimeter = 6
} GpUnit;

typedef enum {
    FontStyleRegular = 0, FontStyleBold = 1, FontStyleItalic = 2,
    FontStyleBoldItalic = 3, FontStyleUnderline = 4, FontStyleStrikeout = 8
} GpFontStyle;

typedef enum {
    SmoothingModeDefault = 0, SmoothingModeHighSpeed = 1,
    SmoothingModeHighQuality = 2, SmoothingModeNone = 3,
    SmoothingModeAntiAlias = 4
} GpSmoothingMode;

typedef enum {
    TextRenderingHintSystemDefault = 0,
    TextRenderingHintSingleBitPerPixelGridFit,
    TextRenderingHintSingleBitPerPixel,
    TextRenderingHintAntiAliasGridFit,
    TextRenderingHintAntiAlias,
    TextRenderingHintClearTypeGridFit
} GpTextRenderingHint;

typedef enum {
    FillModeAlternate = 0,
    FillModeWinding = 1
} GpFillMode;

typedef enum {
    PenAlignmentCenter = 0,
    PenAlignmentInset  = 1
} GpPenAlignment;

typedef enum {
    StringAlignmentNear   = 0,
    StringAlignmentCenter = 1,
    StringAlignmentFar    = 2
} GpStringAlignment;

typedef enum {
    StringTrimmingNone              = 0,
    StringTrimmingEllipsisCharacter = 3,
    StringTrimmingEllipsisWord      = 4
} GpStringTrimming;

/* StringFormat flags */
#define StringFormatFlagsNoWrap              0x00001000
#define StringFormatFlagsNoClip              0x00004000
#define StringFormatFlagsLineLimit           0x00002000
#define StringFormatFlagsMeasureTrailingSpaces 0x00000800

/* Pixel formats */
#define PixelFormat32bppPARGB 0x000E200B

/* ========== Structs ========== */

typedef struct {
    float X, Y, Width, Height;
} GpRectF;

typedef struct {
    UINT32 GdiplusVersion;
    void*  DebugEventCallback;
    BOOL   SuppressBackgroundThread;
    BOOL   SuppressExternalCodecs;
} GpStartupInput;

/* Opaque GDI+ types */
typedef struct GpGraphics     GpGraphics;
typedef struct GpBrush        GpBrush;
typedef struct GpSolidFill    GpSolidFill;
typedef struct GpFontFamily   GpFontFamily;
typedef struct GpFont         GpFont;
typedef struct GpImage        GpImage;
typedef struct GpBitmap       GpBitmap;
typedef struct GpPath         GpPath;
typedef struct GpPen          GpPen;
typedef struct GpStringFormat GpStringFormat;

/* ========== Core ========== */
GpStatus WINGDIPAPI GdiplusStartup(ULONG_PTR *token, const GpStartupInput *input, void *output);
void     WINGDIPAPI GdiplusShutdown(ULONG_PTR token);

/* ========== Graphics ========== */
GpStatus WINGDIPAPI GdipCreateFromHDC(HDC hdc, GpGraphics **graphics);
GpStatus WINGDIPAPI GdipDeleteGraphics(GpGraphics *graphics);
GpStatus WINGDIPAPI GdipSetSmoothingMode(GpGraphics *graphics, GpSmoothingMode smoothingMode);
GpStatus WINGDIPAPI GdipSetTextRenderingHint(GpGraphics *graphics, GpTextRenderingHint mode);

/* ========== Brushes ========== */
GpStatus WINGDIPAPI GdipCreateSolidFill(ARGB color, GpSolidFill **brush);
GpStatus WINGDIPAPI GdipDeleteBrush(GpBrush *brush);

/* ========== Pens ========== */
GpStatus WINGDIPAPI GdipCreatePen1(ARGB color, REAL width, GpUnit unit, GpPen **pen);
GpStatus WINGDIPAPI GdipSetPenAlignment(GpPen *pen, GpPenAlignment alignment);
GpStatus WINGDIPAPI GdipDeletePen(GpPen *pen);

/* ========== Paths ========== */
GpStatus WINGDIPAPI GdipCreatePath(GpFillMode fillMode, GpPath **path);
GpStatus WINGDIPAPI GdipDeletePath(GpPath *path);
GpStatus WINGDIPAPI GdipAddPathArc(GpPath *path, REAL x, REAL y, REAL width, REAL height, REAL startAngle, REAL sweepAngle);
GpStatus WINGDIPAPI GdipClosePathFigure(GpPath *path);
GpStatus WINGDIPAPI GdipFillPath(GpGraphics *graphics, GpBrush *brush, GpPath *path);
GpStatus WINGDIPAPI GdipDrawPath(GpGraphics *graphics, GpPen *pen, GpPath *path);

/* ========== Text ========== */
GpStatus WINGDIPAPI GdipCreateFontFamilyFromName(const WCHAR *name, void *fontCollection, GpFontFamily **fontFamily);
GpStatus WINGDIPAPI GdipDeleteFontFamily(GpFontFamily *fontFamily);
GpStatus WINGDIPAPI GdipCreateFont(GpFontFamily *fontFamily, REAL emSize, INT style, GpUnit unit, GpFont **font);
GpStatus WINGDIPAPI GdipDeleteFont(GpFont *font);
GpStatus WINGDIPAPI GdipDrawString(GpGraphics *graphics, const WCHAR *string, INT length, const GpFont *font, const GpRectF *layoutRect, const GpStringFormat *stringFormat, const GpBrush *brush);
GpStatus WINGDIPAPI GdipMeasureString(GpGraphics *graphics, const WCHAR *string, INT length, const GpFont *font, const GpRectF *layoutRect, const GpStringFormat *stringFormat, GpRectF *boundingBox, INT *codepointsFitted, INT *linesFilled);

/* ========== StringFormat ========== */
GpStatus WINGDIPAPI GdipCreateStringFormat(INT formatAttributes, LANGID language, GpStringFormat **format);
GpStatus WINGDIPAPI GdipDeleteStringFormat(GpStringFormat *format);
GpStatus WINGDIPAPI GdipSetStringFormatAlign(GpStringFormat *format, GpStringAlignment align);
GpStatus WINGDIPAPI GdipSetStringFormatLineAlign(GpStringFormat *format, GpStringAlignment align);
GpStatus WINGDIPAPI GdipSetStringFormatFlags(GpStringFormat *format, INT flags);
GpStatus WINGDIPAPI GdipSetStringFormatTrimming(GpStringFormat *format, GpStringTrimming trimming);

/* ========== Drawing (float versions) ========== */
GpStatus WINGDIPAPI GdipFillRectangle(GpGraphics *graphics, GpBrush *brush, REAL x, REAL y, REAL width, REAL height);
GpStatus WINGDIPAPI GdipFillRectangleI(GpGraphics *graphics, GpBrush *brush, INT x, INT y, INT width, INT height);
GpStatus WINGDIPAPI GdipDrawRectangle(GpGraphics *graphics, GpPen *pen, REAL x, REAL y, REAL width, REAL height);

/* ========== Images ========== */
GpStatus WINGDIPAPI GdipCreateBitmapFromFile(const WCHAR *filename, GpBitmap **bitmap);
GpStatus WINGDIPAPI GdipCreateBitmapFromScan0(INT width, INT height, INT stride, INT format, BYTE* scan0, GpBitmap** bitmap);
GpStatus WINGDIPAPI GdipGetImageGraphicsContext(GpImage *image, GpGraphics **graphics);
GpStatus WINGDIPAPI GdipCreateHBITMAPFromBitmap(GpBitmap* bitmap, HBITMAP* hbmReturn, ARGB background);
GpStatus WINGDIPAPI GdipDrawImageRectI(GpGraphics *graphics, GpImage *image, INT x, INT y, INT width, INT height);
GpStatus WINGDIPAPI GdipDisposeImage(GpImage *image);
GpStatus WINGDIPAPI GdipGetImageWidth(GpImage *image, UINT *width);
GpStatus WINGDIPAPI GdipGetImageHeight(GpImage *image, UINT *height);

#endif /* GDIP_HELPERS_H */
