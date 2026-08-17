#include "image.h"
#include "../widget.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    Widget base;
    GpImage* image; // Cached in memory
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
    NULL, // No timer
    Image_Destroy
};

bool ImageWidget_Create(HINSTANCE hInstance, int x, int y, int width, int height, bool click_through, const WCHAR* path) {
    ImageWidgetData* w = (ImageWidgetData*)malloc(sizeof(ImageWidgetData));
    if (!w) return false;
    memset(w, 0, sizeof(ImageWidgetData));

    // Load image once at startup
    if (GdipCreateBitmapFromFile(path, (GpBitmap**)&w->image) != 0) { // 0 is Ok
        // Failed to load
        w->image = NULL; 
    }

    // Static widget, no timer (0 ms)
    if (!Widget_Init(&w->base, hInstance, &image_vtable, x, y, width, height, 0, click_through)) {
        if (w->image) GdipDisposeImage(w->image);
        free(w);
        return false;
    }
    return true;
}
