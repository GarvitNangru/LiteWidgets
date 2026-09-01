/*
 * render — offscreen widget renderer.
 *
 * Draws every widget in a config to a PNG using the exact painters the live
 * desktop uses. Handy for reviewing a theme without restarting the app, for
 * generating the screenshots in docs/, and for checking rendering in CI where
 * there is no desktop to look at.
 *
 *   render <config.ini> <output-dir> [--sheet]
 *
 * --sheet also writes contact-sheet.png: one tile per built-in style preset.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "spec.h"
#include "style.h"

/* PNG encoder — {557CF406-1A04-11D3-9A73-0000F81EF32E} */
static const CLSID kPngEncoder =
    { 0x557cf406, 0x1a04, 0x11d3, { 0x9a, 0x73, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e } };

static GpBitmap* NewCanvas(int width, int height, GpGraphics** gfxOut) {
    GpBitmap* bitmap = NULL;
    if (GdipCreateBitmapFromScan0(width, height, 0, PixelFormat32bppPARGB, NULL, &bitmap) != 0)
        return NULL;

    GpGraphics* gfx = NULL;
    if (GdipGetImageGraphicsContext((GpImage*)bitmap, &gfx) != 0 || !gfx) {
        GdipDisposeImage((GpImage*)bitmap);
        return NULL;
    }
    GdipSetSmoothingMode(gfx, SmoothingModeAntiAlias);
    GdipSetTextRenderingHint(gfx, TextRenderingHintAntiAliasGridFit);
    GdipSetInterpolationMode(gfx, InterpolationModeHighQualityBicubic);
    GdipSetPixelOffsetMode(gfx, PixelOffsetModeHalf);

    *gfxOut = gfx;
    return bitmap;
}

/*
 * CreateDirectory only makes the last component, so an output path like
 * `artifacts\showcase` fails whenever `artifacts` does not already exist --
 * which, in CI, is always.
 */
static bool EnsureDirectory(const char* path) {
    char build[MAX_PATH];
    strncpy(build, path, MAX_PATH - 1);
    build[MAX_PATH - 1] = '\0';

    for (char* p = build; *p; p++) {
        if (*p != '\\' && *p != '/') continue;
        char separator = *p;
        *p = '\0';
        if (build[0] && strcmp(build, ".") != 0 && strcmp(build, "..") != 0)
            CreateDirectoryA(build, NULL);
        *p = separator;
    }
    CreateDirectoryA(build, NULL);
    return GetFileAttributesA(build) != INVALID_FILE_ATTRIBUTES;
}

static bool SavePng(GpBitmap* bitmap, const char* path) {
    WCHAR wide[MAX_PATH];
    MultiByteToWideChar(CP_UTF8, 0, path, -1, wide, MAX_PATH);
    return GdipSaveImageToFile((GpImage*)bitmap, wide, &kPngEncoder, NULL) == 0;
}

static int RenderConfig(const char* iniPath, const char* outDir) {
    WidgetSpec* specs = (WidgetSpec*)calloc(LW_MAX_WIDGETS, sizeof(WidgetSpec));
    if (!specs) return 0;

    int count = Config_ReadAll(iniPath, specs, LW_MAX_WIDGETS);
    int written = 0;

    for (int i = 0; i < count; i++) {
        GpGraphics* gfx = NULL;
        GpBitmap* bitmap = NewCanvas(specs[i].width, specs[i].height, &gfx);
        if (!bitmap) continue;

        Config_Paint(&specs[i], iniPath, gfx, specs[i].width, specs[i].height);
        GdipDeleteGraphics(gfx);

        char path[MAX_PATH];
        _snprintf(path, MAX_PATH, "%s\\%s.png", outDir, specs[i].section);
        if (SavePng(bitmap, path)) {
            printf("  %s  (%dx%d, %s)\n", path, specs[i].width, specs[i].height,
                   Spec_TypeName(specs[i].type));
            written++;
        }
        GdipDisposeImage((GpImage*)bitmap);
    }

    free(specs);
    return written;
}

/* One tile per preset, laid out in a grid, over a dark backdrop. */
static bool RenderContactSheet(const char* iniPath, const char* outDir) {
    const int tileW = 300, tileH = 132, gap = 16, columns = 3;

    int presetCount = 0;
    const StylePreset* presets = Style_Presets(&presetCount);
    int rows = (presetCount + columns - 1) / columns;

    int width  = columns * tileW + (columns + 1) * gap;
    int height = rows * tileH + (rows + 1) * gap;

    GpGraphics* gfx = NULL;
    GpBitmap* sheet = NewCanvas(width, height, &gfx);
    if (!sheet) return false;

    GpSolidFill* backdrop = NULL;
    if (GdipCreateSolidFill(0xFF101014, &backdrop) == 0) {
        GdipFillRectangle(gfx, (GpBrush*)backdrop, 0, 0, (float)width, (float)height);
        GdipDeleteBrush((GpBrush*)backdrop);
    }

    for (int i = 0; i < presetCount; i++) {
        WidgetSpec spec;
        Spec_Defaults(&spec);
        spec.width = tileW;
        spec.height = tileH;
        Spec_Set(&spec, "preset", presets[i].name);
        Spec_Set(&spec, "font_size", "52");
        Spec_Set(&spec, "date_transform", "upper");
        Spec_Set(&spec, "date_letter_spacing", "2");
        /* Quoted: the preset name is a label, not a time pattern. */
        char label[96];
        _snprintf(label, sizeof(label), "'%s'", presets[i].name);
        Spec_Set(&spec, "date_format", label);
        Spec_Finalize(&spec);

        GpGraphics* tileGfx = NULL;
        GpBitmap* tile = NewCanvas(tileW, tileH, &tileGfx);
        if (!tile) continue;
        Config_Paint(&spec, iniPath, tileGfx, tileW, tileH);
        GdipDeleteGraphics(tileGfx);

        int x = gap + (i % columns) * (tileW + gap);
        int y = gap + (i / columns) * (tileH + gap);
        GdipDrawImageRectI(gfx, (GpImage*)tile, x, y, tileW, tileH);
        GdipDisposeImage((GpImage*)tile);
    }

    GdipDeleteGraphics(gfx);

    char path[MAX_PATH];
    _snprintf(path, MAX_PATH, "%s\\contact-sheet.png", outDir);
    bool ok = SavePng(sheet, path);
    if (ok) printf("  %s  (%d presets)\n", path, presetCount);
    GdipDisposeImage((GpImage*)sheet);
    return ok;
}

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: render <config.ini> <output-dir> [--sheet]\n");
        return 2;
    }

    const char* iniPath = argv[1];
    const char* outDir  = argv[2];
    bool sheet = (argc > 3 && strcmp(argv[3], "--sheet") == 0);

    if (GetFileAttributesA(iniPath) == INVALID_FILE_ATTRIBUTES) {
        fprintf(stderr, "render: cannot open %s\n", iniPath);
        return 1;
    }
    if (!EnsureDirectory(outDir)) {
        fprintf(stderr, "render: cannot create %s\n", outDir);
        return 1;
    }

    ULONG_PTR token = 0;
    GpStartupInput input = { 1, NULL, FALSE, FALSE };
    if (GdiplusStartup(&token, &input, NULL) != 0) {
        fprintf(stderr, "render: GDI+ failed to start\n");
        return 1;
    }

    printf("render: %s\n", iniPath);
    int written = RenderConfig(iniPath, outDir);
    if (sheet && RenderContactSheet(iniPath, outDir)) written++;

    GdiplusShutdown(token);

    if (written == 0) {
        fprintf(stderr, "render: nothing was written\n");
        return 1;
    }
    return 0;
}
