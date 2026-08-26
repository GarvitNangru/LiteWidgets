#include "image.h"
#include "../widget.h"
#include <stdlib.h>
#include <string.h>
#include <shlwapi.h>

typedef struct {
    Widget base;
    GpImage* image;
} ImageWidgetData;

static void Image_Render(Widget* base, GpGraphics* gfx, int width, int height) {
    ImageWidgetData* w = (ImageWidgetData*)base;
    if (w->image) {
        GdipDrawImageRectI(gfx, w->image, 0, 0, width, height);
    }
}

static void Image_Destroy(Widget* base) {
    ImageWidgetData* w = (ImageWidgetData*)base;
    if (w->image) {
        GdipDisposeImage(w->image);
    }
    free(w);
}

static const WidgetVtable image_vtable = {
    Image_Render,
    NULL,
    Image_Destroy
};

static void ResolveImagePath(const char* iniPathA, const char* pathA,
                             WCHAR* outW, DWORD maxLen) {
    WCHAR inputW[MAX_PATH];
    MultiByteToWideChar(CP_UTF8, 0, pathA, -1, inputW, MAX_PATH);

    if (!PathIsRelativeW(inputW)) {
        wcsncpy(outW, inputW, maxLen);
        return;
    }

    WCHAR iniDirW[MAX_PATH];
    MultiByteToWideChar(CP_UTF8, 0, iniPathA, -1, iniDirW, MAX_PATH);
    PathRemoveFileSpecW(iniDirW);
    PathCombineW(outW, iniDirW, inputW);
}

bool ImageWidget_Create(HINSTANCE hInstance, const char* iniPath,
                        int x, int y, int width, int height,
                        bool click_through, const char* pathA) {
    ImageWidgetData* w = (ImageWidgetData*)calloc(1, sizeof(ImageWidgetData));
    if (!w) return false;

    WCHAR resolvedPath[MAX_PATH];
    ResolveImagePath(iniPath, pathA, resolvedPath, MAX_PATH);

    if (GdipCreateBitmapFromFile(resolvedPath, (GpBitmap**)&w->image) != 0) {
        w->image = NULL;
    }

    if (!Widget_Init(&w->base, hInstance, &image_vtable, x, y, width, height, 0, click_through)) {
        if (w->image) GdipDisposeImage(w->image);
        free(w);
        return false;
    }

    /* CRITICAL: Initial render for layered window */
    Widget_Render(&w->base);
    return true;
}
