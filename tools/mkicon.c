/*
 * mkicon — renders the application icon and writes a multi-resolution .ico.
 *
 * The repository ships no binary assets: the icon is drawn from this source
 * at build time, exactly as the widgets themselves are drawn. `assets/icon.svg`
 * is the same design in a form GitHub and browsers can render; the geometry
 * below is its literal translation, so a change to one wants the same change
 * to the other.
 *
 *   mkicon <out.ico>
 */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "gdiplus_helpers.h"

/* Sizes Windows actually asks for: tray, taskbar, Alt-Tab, Explorer views. */
static const int kSizes[] = { 16, 20, 24, 32, 40, 48, 64, 128, 256 };
#define SIZE_COUNT ((int)(sizeof(kSizes) / sizeof(kSizes[0])))

/* Anything this size or larger is stored as PNG, which is what shells prefer. */
#define PNG_FROM 128

/* ─────────────────────────── the design ─────────────────────────── */

static GpPath* RoundedPath(float x, float y, float w, float h, float r) {
    GpPath* path = NULL;
    if (GdipCreatePath(FillModeAlternate, &path) != 0) return NULL;

    float d = r * 2.0f;
    GdipAddPathArc(path, x,         y,         d, d, 180.0f, 90.0f);
    GdipAddPathArc(path, x + w - d, y,         d, d, 270.0f, 90.0f);
    GdipAddPathArc(path, x + w - d, y + h - d, d, d,   0.0f, 90.0f);
    GdipAddPathArc(path, x,         y + h - d, d, d,  90.0f, 90.0f);
    GdipClosePathFigure(path);
    return path;
}

static void PaintMark(GpGraphics* gfx, float size) {
    GdipSetSmoothingMode(gfx, SmoothingModeAntiAlias);
    GdipSetPixelOffsetMode(gfx, PixelOffsetModeHalf);

    /* Normalised from the 256px master in assets/icon.svg. */
    float x = size * 0.0703f;
    float y = size * 0.0703f;
    float w = size * 0.8594f;
    float h = size * 0.8594f;
    float r = size * 0.2109f;

    GpPath* tile = RoundedPath(x, y, w, h, r);
    if (!tile) return;

    GpRectF fill = { x, y, w, h };
    GpLineGradient* brush = NULL;
    if (GdipCreateLineBrushFromRectWithAngle(&fill, 0xFF7BDCFF, 0xFF2D6BF0,
                                             45.0f, TRUE, WrapModeTileFlipXY,
                                             &brush) == 0) {
        GdipFillPath(gfx, (GpBrush*)brush, tile);
        GdipDeleteBrush((GpBrush*)brush);
    }

    /* Top sheen, so the tile reads as a surface rather than a flat swatch. */
    GpRectF sheenRect = { x, y, w, h };
    GpLineGradient* sheen = NULL;
    if (GdipCreateLineBrushFromRectWithAngle(&sheenRect, 0x40FFFFFF, 0x00FFFFFF,
                                             90.0f, TRUE, WrapModeTileFlipXY,
                                             &sheen) == 0) {
        GdipFillPath(gfx, (GpBrush*)sheen, tile);
        GdipDeleteBrush((GpBrush*)sheen);
    }

    float edge = size * 0.0117f;
    if (edge >= 0.9f) {
        GpPen* pen = NULL;
        if (GdipCreatePen1(0x73FFFFFF, edge, UnitPixel, &pen) == 0) {
            GdipSetPenMode(pen, PenAlignmentInset);
            GdipDrawPath(gfx, pen, tile);
            GdipDeletePen(pen);
        }
    }
    GdipDeletePath(tile);

    /* Two hands at 12 and 3 — an asymmetric mark that survives 16px. */
    GpPen* hands = NULL;
    float width = size * 0.078f;
    if (width < 1.0f) width = 1.0f;
    if (GdipCreatePen1(0xFF0B1A33, width, UnitPixel, &hands) == 0) {
        GdipSetPenStartCap(hands, LineCapRound);
        GdipSetPenEndCap(hands, LineCapRound);
        float c = size * 0.5f;
        GdipDrawLine(gfx, hands, c, c, c, size * 0.242f);
        GdipDrawLine(gfx, hands, c, c, size * 0.719f, c);
        GdipDeletePen(hands);
    }
}

static GpBitmap* RenderSize(int size) {
    GpBitmap* bitmap = NULL;
    if (GdipCreateBitmapFromScan0(size, size, 0, PixelFormat32bppPARGB, NULL, &bitmap) != 0)
        return NULL;

    GpGraphics* gfx = NULL;
    if (GdipGetImageGraphicsContext((GpImage*)bitmap, &gfx) != 0) {
        GdipDisposeImage((GpImage*)bitmap);
        return NULL;
    }
    PaintMark(gfx, (float)size);
    GdipDeleteGraphics(gfx);
    return bitmap;
}

/* ─────────────────────────── encoding ─────────────────────────── */

/* One .ico member: either a PNG blob or a BITMAPINFOHEADER + XOR + AND mask. */
typedef struct {
    BYTE*  data;
    DWORD  size;
    int    pixels;
} Member;

/* PNG encoder - {557CF406-1A04-11D3-9A73-0000F81EF32E} */
static const CLSID kPngEncoder =
    { 0x557cf406, 0x1a04, 0x11d3, { 0x9a, 0x73, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e } };

static BYTE* ReadWholeFile(const WCHAR* path, DWORD* outSize) {
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return NULL;

    DWORD size = GetFileSize(file, NULL);
    BYTE* data = (size != INVALID_FILE_SIZE) ? (BYTE*)malloc(size) : NULL;
    DWORD read = 0;
    if (!data || !ReadFile(file, data, size, &read, NULL) || read != size) {
        free(data);
        CloseHandle(file);
        return NULL;
    }
    CloseHandle(file);
    *outSize = size;
    return data;
}

static bool EncodePng(GpBitmap* bitmap, Member* out) {
    WCHAR folder[MAX_PATH];
    WCHAR path[MAX_PATH];
    if (!GetTempPathW(MAX_PATH, folder)) return false;
    if (!GetTempFileNameW(folder, L"lwi", 0, path)) return false;

    if (GdipSaveImageToFile((GpImage*)bitmap, path, &kPngEncoder, NULL) != 0) {
        DeleteFileW(path);
        return false;
    }
    DWORD size = 0;
    BYTE* data = ReadWholeFile(path, &size);
    DeleteFileW(path);
    if (!data) return false;

    out->data = data;
    out->size = size;
    return true;
}

/*
 * The classic layout: a BITMAPINFOHEADER whose height covers both the colour
 * bitmap and the 1bpp mask below it, then bottom-up BGRA rows, then the mask.
 * The mask is redundant for 32bpp icons but old shells still read it.
 */
static bool EncodeBmp(GpBitmap* bitmap, int size, Member* out) {
    GpRect rect = { 0, 0, size, size };
    GpBitmapData locked;
    memset(&locked, 0, sizeof(locked));

    /* Straight alpha: .ico is not premultiplied. */
    if (GdipBitmapLockBits(bitmap, &rect, ImageLockModeRead,
                           PixelFormat32bppARGB, &locked) != 0)
        return false;

    DWORD maskStride  = (DWORD)(((size + 31) / 32) * 4);
    DWORD colorBytes  = (DWORD)size * (DWORD)size * 4;
    DWORD maskBytes   = maskStride * (DWORD)size;
    DWORD total       = sizeof(BITMAPINFOHEADER) + colorBytes + maskBytes;

    BYTE* buffer = (BYTE*)calloc(total, 1);
    if (!buffer) {
        GdipBitmapUnlockBits(bitmap, &locked);
        return false;
    }

    BITMAPINFOHEADER* header = (BITMAPINFOHEADER*)buffer;
    header->biSize        = sizeof(BITMAPINFOHEADER);
    header->biWidth       = size;
    header->biHeight      = size * 2;     /* colour + mask, as the format wants */
    header->biPlanes      = 1;
    header->biBitCount    = 32;
    header->biCompression = BI_RGB;
    header->biSizeImage   = colorBytes + maskBytes;

    BYTE* color = buffer + sizeof(BITMAPINFOHEADER);
    BYTE* mask  = color + colorBytes;

    for (int row = 0; row < size; row++) {
        const BYTE* src = (const BYTE*)locked.Scan0 + (size_t)row * (size_t)locked.Stride;
        BYTE* dst = color + (size_t)(size - 1 - row) * (size_t)size * 4;
        memcpy(dst, src, (size_t)size * 4);

        BYTE* maskRow = mask + (size_t)(size - 1 - row) * maskStride;
        for (int col = 0; col < size; col++)
            if (src[col * 4 + 3] < 128) maskRow[col / 8] |= (BYTE)(0x80 >> (col % 8));
    }

    GdipBitmapUnlockBits(bitmap, &locked);
    out->data = buffer;
    out->size = total;
    return true;
}

/* ─────────────────────────── file ─────────────────────────── */

static bool WriteIcon(const char* path, Member* members, int count) {
    HANDLE file = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return false;

    WORD head[3] = { 0, 1, (WORD)count };
    DWORD written = 0;
    WriteFile(file, head, sizeof(head), &written, NULL);

    DWORD offset = (DWORD)(sizeof(head) + (size_t)count * 16);
    for (int i = 0; i < count; i++) {
        BYTE entry[16];
        memset(entry, 0, sizeof(entry));
        entry[0] = (BYTE)(members[i].pixels >= 256 ? 0 : members[i].pixels);
        entry[1] = entry[0];
        *(WORD*)(entry + 4) = 1;     /* planes */
        *(WORD*)(entry + 6) = 32;    /* bits per pixel */
        *(DWORD*)(entry + 8)  = members[i].size;
        *(DWORD*)(entry + 12) = offset;
        WriteFile(file, entry, sizeof(entry), &written, NULL);
        offset += members[i].size;
    }

    for (int i = 0; i < count; i++)
        WriteFile(file, members[i].data, members[i].size, &written, NULL);

    CloseHandle(file);
    return true;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: mkicon <out.ico>\n");
        return 2;
    }

    ULONG_PTR token = 0;
    GpStartupInput startup = { 1, NULL, FALSE, FALSE };
    if (GdiplusStartup(&token, &startup, NULL) != 0) {
        fprintf(stderr, "mkicon: GDI+ failed to start\n");
        return 1;
    }

    Member members[SIZE_COUNT];
    int count = 0;

    for (int i = 0; i < SIZE_COUNT; i++) {
        GpBitmap* bitmap = RenderSize(kSizes[i]);
        if (!bitmap) continue;

        Member member;
        memset(&member, 0, sizeof(member));
        member.pixels = kSizes[i];

        bool ok = (kSizes[i] >= PNG_FROM) ? EncodePng(bitmap, &member)
                                          : EncodeBmp(bitmap, kSizes[i], &member);
        GdipDisposeImage((GpImage*)bitmap);
        if (ok) members[count++] = member;
    }

    int status = 0;
    if (count == 0 || !WriteIcon(argv[1], members, count)) {
        fprintf(stderr, "mkicon: could not write %s\n", argv[1]);
        status = 1;
    } else {
        printf("mkicon: %s (%d sizes)\n", argv[1], count);
    }

    for (int i = 0; i < count; i++) free(members[i].data);
    GdiplusShutdown(token);
    return status;
}
