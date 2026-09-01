#pragma once
#ifndef GDIP_HELPERS_H
#define GDIP_HELPERS_H

#include <windows.h>

/*
 * C-style GDI+ Flat API declarations.
 *
 * The GDI+ headers shipped with the Windows SDK are C++-only wrappers around
 * this flat API. Declaring the entry points ourselves keeps LiteWidgets in
 * pure C with no C++ runtime and no extra link-time dependencies.
 */

typedef int   GpStatus;
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
    InterpolationModeDefault = 0,
    InterpolationModeLowQuality = 1,
    InterpolationModeHighQuality = 2,
    InterpolationModeBilinear = 3,
    InterpolationModeBicubic = 4,
    InterpolationModeNearestNeighbor = 5,
    InterpolationModeHighQualityBilinear = 6,
    InterpolationModeHighQualityBicubic = 7
} GpInterpolationMode;

typedef enum {
    PixelOffsetModeDefault = 0, PixelOffsetModeHighSpeed = 1,
    PixelOffsetModeHighQuality = 2, PixelOffsetModeNone = 3,
    PixelOffsetModeHalf = 4
} GpPixelOffsetMode;

typedef enum {
    FillModeAlternate = 0,
    FillModeWinding = 1
} GpFillMode;

typedef enum {
    PenAlignmentCenter = 0,
    PenAlignmentInset  = 1
} GpPenAlignment;

typedef enum {
    LineCapFlat = 0, LineCapSquare = 1, LineCapRound = 2, LineCapTriangle = 3
} GpLineCap;

typedef enum {
    LineJoinMiter = 0, LineJoinBevel = 1, LineJoinRound = 2
} GpLineJoin;

typedef enum {
    DashStyleSolid = 0, DashStyleDash = 1, DashStyleDot = 2,
    DashStyleDashDot = 3, DashStyleDashDotDot = 4, DashStyleCustom = 5
} GpDashStyle;

typedef enum {
    StringAlignmentNear   = 0,
    StringAlignmentCenter = 1,
    StringAlignmentFar    = 2
} GpStringAlignment;

typedef enum {
    StringTrimmingNone              = 0,
    StringTrimmingCharacter         = 1,
    StringTrimmingWord              = 2,
    StringTrimmingEllipsisCharacter = 3,
    StringTrimmingEllipsisWord      = 4
} GpStringTrimming;

typedef enum {
    LinearGradientModeHorizontal      = 0,
    LinearGradientModeVertical        = 1,
    LinearGradientModeForwardDiagonal = 2,
    LinearGradientModeBackwardDiagonal= 3
} GpLinearGradientMode;

typedef enum {
    MatrixOrderPrepend = 0,
    MatrixOrderAppend  = 1
} GpMatrixOrder;

typedef enum {
    CombineModeReplace = 0, CombineModeIntersect = 1, CombineModeUnion = 2,
    CombineModeXor = 3, CombineModeExclude = 4, CombineModeComplement = 5
} GpCombineMode;

typedef enum {
    WrapModeTile = 0, WrapModeTileFlipX = 1, WrapModeTileFlipY = 2,
    WrapModeTileFlipXY = 3, WrapModeClamp = 4
} GpWrapMode;

/* StringFormat flags */
#define StringFormatFlagsDirectionRightToLeft  0x00000001
#define StringFormatFlagsNoWrap                0x00001000
#define StringFormatFlagsLineLimit             0x00002000
#define StringFormatFlagsNoClip                0x00004000
#define StringFormatFlagsNoFitBlackBox         0x00000004
#define StringFormatFlagsMeasureTrailingSpaces 0x00000800

/* Pixel formats */
#define PixelFormat32bppARGB  0x0026200A
#define PixelFormat32bppPARGB 0x000E200B

/* ========== Structs ========== */

typedef struct { float X, Y; }                 GpPointF;
typedef struct { INT   X, Y; }                 GpPoint;
typedef struct { float X, Y, Width, Height; }  GpRectF;
typedef struct { INT   X, Y, Width, Height; }  GpRect;

typedef struct {
    UINT32 GdiplusVersion;
    void*  DebugEventCallback;
    BOOL   SuppressBackgroundThread;
    BOOL   SuppressExternalCodecs;
} GpStartupInput;

typedef UINT GraphicsState;

/* Opaque GDI+ types */
typedef struct GpGraphics     GpGraphics;
typedef struct GpBrush        GpBrush;
typedef struct GpSolidFill    GpSolidFill;
typedef struct GpLineGradient GpLineGradient;
typedef struct GpFontFamily   GpFontFamily;
typedef struct GpFont         GpFont;
typedef struct GpImage        GpImage;
typedef struct GpBitmap       GpBitmap;
typedef struct GpPath         GpPath;
typedef struct GpPen          GpPen;
typedef struct GpStringFormat GpStringFormat;
typedef struct GpMatrix       GpMatrix;

/* ========== Core ========== */
GpStatus WINGDIPAPI GdiplusStartup(ULONG_PTR *token, const GpStartupInput *input, void *output);
void     WINGDIPAPI GdiplusShutdown(ULONG_PTR token);

/* ========== Graphics ========== */
GpStatus WINGDIPAPI GdipCreateFromHDC(HDC hdc, GpGraphics **graphics);
GpStatus WINGDIPAPI GdipDeleteGraphics(GpGraphics *graphics);
/* Deleting a Graphics flushes it; a shared one has to be flushed by hand
   before GDI draws to the same DC, or the two orders disagree. */
typedef enum { FlushIntentionFlush = 0, FlushIntentionSync = 1 } GpFlushIntention;
GpStatus WINGDIPAPI GdipFlush(GpGraphics *graphics, GpFlushIntention intention);
GpStatus WINGDIPAPI GdipGraphicsClear(GpGraphics *graphics, ARGB color);
GpStatus WINGDIPAPI GdipSetSmoothingMode(GpGraphics *graphics, GpSmoothingMode smoothingMode);
GpStatus WINGDIPAPI GdipSetTextRenderingHint(GpGraphics *graphics, GpTextRenderingHint mode);
GpStatus WINGDIPAPI GdipSetInterpolationMode(GpGraphics *graphics, GpInterpolationMode mode);
GpStatus WINGDIPAPI GdipSetPixelOffsetMode(GpGraphics *graphics, GpPixelOffsetMode mode);
GpStatus WINGDIPAPI GdipSaveGraphics(GpGraphics *graphics, GraphicsState *state);
GpStatus WINGDIPAPI GdipRestoreGraphics(GpGraphics *graphics, GraphicsState state);
GpStatus WINGDIPAPI GdipResetWorldTransform(GpGraphics *graphics);
GpStatus WINGDIPAPI GdipTranslateWorldTransform(GpGraphics *graphics, REAL dx, REAL dy, GpMatrixOrder order);
GpStatus WINGDIPAPI GdipRotateWorldTransform(GpGraphics *graphics, REAL angle, GpMatrixOrder order);
GpStatus WINGDIPAPI GdipScaleWorldTransform(GpGraphics *graphics, REAL sx, REAL sy, GpMatrixOrder order);
GpStatus WINGDIPAPI GdipSetClipPath(GpGraphics *graphics, GpPath *path, GpCombineMode combineMode);
GpStatus WINGDIPAPI GdipResetClip(GpGraphics *graphics);

/* ========== Brushes ========== */
GpStatus WINGDIPAPI GdipCreateSolidFill(ARGB color, GpSolidFill **brush);
GpStatus WINGDIPAPI GdipSetSolidFillColor(GpSolidFill *brush, ARGB color);
GpStatus WINGDIPAPI GdipDeleteBrush(GpBrush *brush);
GpStatus WINGDIPAPI GdipCreateLineBrushFromRect(const GpRectF *rect, ARGB color1, ARGB color2,
                                                GpLinearGradientMode mode, GpWrapMode wrapMode,
                                                GpLineGradient **lineGradient);
GpStatus WINGDIPAPI GdipCreateLineBrushFromRectWithAngle(const GpRectF *rect, ARGB color1, ARGB color2,
                                                         REAL angle, BOOL isAngleScalable,
                                                         GpWrapMode wrapMode, GpLineGradient **lineGradient);
GpStatus WINGDIPAPI GdipSetLineGammaCorrection(GpLineGradient *brush, BOOL useGammaCorrection);

/* ========== Pens ========== */
GpStatus WINGDIPAPI GdipCreatePen1(ARGB color, REAL width, GpUnit unit, GpPen **pen);
GpStatus WINGDIPAPI GdipCreatePen2(GpBrush *brush, REAL width, GpUnit unit, GpPen **pen);
GpStatus WINGDIPAPI GdipSetPenWidth(GpPen *pen, REAL width);
GpStatus WINGDIPAPI GdipSetPenColor(GpPen *pen, ARGB argb);
/* Exported as GdipSetPenMode; PenAlignment is the parameter's type. */
GpStatus WINGDIPAPI GdipSetPenMode(GpPen *pen, GpPenAlignment penMode);
GpStatus WINGDIPAPI GdipSetPenDashStyle(GpPen *pen, GpDashStyle dashStyle);
GpStatus WINGDIPAPI GdipSetPenStartCap(GpPen *pen, GpLineCap startCap);
GpStatus WINGDIPAPI GdipSetPenEndCap(GpPen *pen, GpLineCap endCap);
GpStatus WINGDIPAPI GdipSetPenLineJoin(GpPen *pen, GpLineJoin lineJoin);
GpStatus WINGDIPAPI GdipDeletePen(GpPen *pen);

/* ========== Paths ========== */
GpStatus WINGDIPAPI GdipCreatePath(GpFillMode fillMode, GpPath **path);
GpStatus WINGDIPAPI GdipDeletePath(GpPath *path);
GpStatus WINGDIPAPI GdipResetPath(GpPath *path);
GpStatus WINGDIPAPI GdipAddPathArc(GpPath *path, REAL x, REAL y, REAL width, REAL height, REAL startAngle, REAL sweepAngle);
GpStatus WINGDIPAPI GdipAddPathLine(GpPath *path, REAL x1, REAL y1, REAL x2, REAL y2);
GpStatus WINGDIPAPI GdipAddPathEllipse(GpPath *path, REAL x, REAL y, REAL width, REAL height);
GpStatus WINGDIPAPI GdipAddPathRectangle(GpPath *path, REAL x, REAL y, REAL width, REAL height);
GpStatus WINGDIPAPI GdipAddPathPath(GpPath *path, const GpPath *addingPath, BOOL connect);
GpStatus WINGDIPAPI GdipAddPathString(GpPath *path, const WCHAR *string, INT length,
                                      const GpFontFamily *family, INT style, REAL emSize,
                                      const GpRectF *layoutRect, const GpStringFormat *format);
GpStatus WINGDIPAPI GdipClosePathFigure(GpPath *path);
GpStatus WINGDIPAPI GdipGetPathWorldBounds(GpPath *path, GpRectF *bounds, const GpMatrix *matrix, const GpPen *pen);
GpStatus WINGDIPAPI GdipTransformPath(GpPath *path, GpMatrix *matrix);
GpStatus WINGDIPAPI GdipFillPath(GpGraphics *graphics, GpBrush *brush, GpPath *path);
GpStatus WINGDIPAPI GdipDrawPath(GpGraphics *graphics, GpPen *pen, GpPath *path);

/* ========== Matrix ========== */
GpStatus WINGDIPAPI GdipCreateMatrix(GpMatrix **matrix);
GpStatus WINGDIPAPI GdipDeleteMatrix(GpMatrix *matrix);
GpStatus WINGDIPAPI GdipTranslateMatrix(GpMatrix *matrix, REAL offsetX, REAL offsetY, GpMatrixOrder order);

/* ========== Text ========== */
GpStatus WINGDIPAPI GdipCreateFontFamilyFromName(const WCHAR *name, void *fontCollection, GpFontFamily **fontFamily);
GpStatus WINGDIPAPI GdipDeleteFontFamily(GpFontFamily *fontFamily);
GpStatus WINGDIPAPI GdipCreateFont(GpFontFamily *fontFamily, REAL emSize, INT style, GpUnit unit, GpFont **font);
GpStatus WINGDIPAPI GdipDeleteFont(GpFont *font);
GpStatus WINGDIPAPI GdipGetFontHeight(const GpFont *font, const GpGraphics *graphics, REAL *height);
GpStatus WINGDIPAPI GdipDrawString(GpGraphics *graphics, const WCHAR *string, INT length, const GpFont *font, const GpRectF *layoutRect, const GpStringFormat *stringFormat, const GpBrush *brush);
GpStatus WINGDIPAPI GdipMeasureString(GpGraphics *graphics, const WCHAR *string, INT length, const GpFont *font, const GpRectF *layoutRect, const GpStringFormat *stringFormat, GpRectF *boundingBox, INT *codepointsFitted, INT *linesFilled);

/* ========== StringFormat ========== */
GpStatus WINGDIPAPI GdipCreateStringFormat(INT formatAttributes, LANGID language, GpStringFormat **format);
GpStatus WINGDIPAPI GdipDeleteStringFormat(GpStringFormat *format);
GpStatus WINGDIPAPI GdipSetStringFormatAlign(GpStringFormat *format, GpStringAlignment align);
GpStatus WINGDIPAPI GdipSetStringFormatLineAlign(GpStringFormat *format, GpStringAlignment align);
GpStatus WINGDIPAPI GdipSetStringFormatFlags(GpStringFormat *format, INT flags);
GpStatus WINGDIPAPI GdipSetStringFormatTrimming(GpStringFormat *format, GpStringTrimming trimming);
GpStatus WINGDIPAPI GdipCloneStringFormat(const GpStringFormat *format, GpStringFormat **newFormat);
/* The generic formats are process-wide singletons — clone before mutating, never delete. */
GpStatus WINGDIPAPI GdipStringFormatGetGenericTypographic(GpStringFormat **format);
GpStatus WINGDIPAPI GdipStringFormatGetGenericDefault(GpStringFormat **format);

/* ========== Shapes ========== */
GpStatus WINGDIPAPI GdipFillRectangle(GpGraphics *graphics, GpBrush *brush, REAL x, REAL y, REAL width, REAL height);
GpStatus WINGDIPAPI GdipFillRectangleI(GpGraphics *graphics, GpBrush *brush, INT x, INT y, INT width, INT height);
GpStatus WINGDIPAPI GdipDrawRectangle(GpGraphics *graphics, GpPen *pen, REAL x, REAL y, REAL width, REAL height);
GpStatus WINGDIPAPI GdipDrawLine(GpGraphics *graphics, GpPen *pen, REAL x1, REAL y1, REAL x2, REAL y2);
GpStatus WINGDIPAPI GdipDrawEllipse(GpGraphics *graphics, GpPen *pen, REAL x, REAL y, REAL width, REAL height);
GpStatus WINGDIPAPI GdipFillEllipse(GpGraphics *graphics, GpBrush *brush, REAL x, REAL y, REAL width, REAL height);
GpStatus WINGDIPAPI GdipDrawArc(GpGraphics *graphics, GpPen *pen, REAL x, REAL y, REAL width, REAL height, REAL startAngle, REAL sweepAngle);
GpStatus WINGDIPAPI GdipFillPie(GpGraphics *graphics, GpBrush *brush, REAL x, REAL y, REAL width, REAL height, REAL startAngle, REAL sweepAngle);

/* ========== Images ========== */
GpStatus WINGDIPAPI GdipCreateBitmapFromFile(const WCHAR *filename, GpBitmap **bitmap);
GpStatus WINGDIPAPI GdipCreateBitmapFromScan0(INT width, INT height, INT stride, INT format, BYTE* scan0, GpBitmap** bitmap);
GpStatus WINGDIPAPI GdipGetImageGraphicsContext(GpImage *image, GpGraphics **graphics);
GpStatus WINGDIPAPI GdipCreateHBITMAPFromBitmap(GpBitmap* bitmap, HBITMAP* hbmReturn, ARGB background);
GpStatus WINGDIPAPI GdipDrawImageRectI(GpGraphics *graphics, GpImage *image, INT x, INT y, INT width, INT height);
GpStatus WINGDIPAPI GdipDrawImageRectRectI(GpGraphics *graphics, GpImage *image,
                                           INT dstx, INT dsty, INT dstwidth, INT dstheight,
                                           INT srcx, INT srcy, INT srcwidth, INT srcheight,
                                           GpUnit srcUnit, const void *imageAttributes,
                                           void *callback, void *callbackData);
GpStatus WINGDIPAPI GdipDisposeImage(GpImage *image);
GpStatus WINGDIPAPI GdipSaveImageToFile(GpImage *image, const WCHAR *filename,
                                        const CLSID *clsidEncoder, const void *encoderParams);
GpStatus WINGDIPAPI GdipGetImageWidth(GpImage *image, UINT *width);
GpStatus WINGDIPAPI GdipGetImageHeight(GpImage *image, UINT *height);

/* ========== Direct pixel access ========== */

typedef struct {
    UINT   Width;
    UINT   Height;
    INT    Stride;
    INT    PixelFormat;
    void*  Scan0;
    UINT_PTR Reserved;
} GpBitmapData;

#define ImageLockModeRead      0x0001
#define ImageLockModeWrite     0x0002
#define ImageLockModeUserInput 0x0004

/*
 * Locking with PixelFormat32bppARGB converts on the way out, which is the
 * cheapest way to get straight (non-premultiplied) alpha out of a surface
 * that was composited premultiplied.
 */
GpStatus WINGDIPAPI GdipBitmapLockBits(GpBitmap *bitmap, const GpRect *rect, UINT flags,
                                       INT format, GpBitmapData *lockedBitmapData);
GpStatus WINGDIPAPI GdipBitmapUnlockBits(GpBitmap *bitmap, GpBitmapData *lockedBitmapData);

#endif /* GDIP_HELPERS_H */
