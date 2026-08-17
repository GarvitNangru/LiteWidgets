#include "notes.h"
#include "../widget.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    Widget base;
    int font_size;
    DWORD bg_color;
    DWORD text_color;
    WCHAR* text_buffer; // Entire file content cached in memory
} NotesWidgetData;

static void Notes_Render(Widget* base, GpGraphics* gfx, int width, int height) {
    NotesWidgetData* w = (NotesWidgetData*)base;
    
    // Draw background
    GpSolidFill* pBrush = NULL;
    GdipCreateSolidFill(w->bg_color, &pBrush);
    GdipFillRectangleI(gfx, (GpBrush*)pBrush, 0, 0, width, height);
    GdipDeleteBrush((GpBrush*)pBrush);

    if (w->text_buffer) {
        // Setup font
        GpFontFamily* pFontFamily = NULL;
        GdipCreateFontFamilyFromName(L"Segoe UI", NULL, &pFontFamily);
        GpFont* pFont = NULL;
        GdipCreateFont(pFontFamily, (REAL)w->font_size, FontStyleRegular, UnitPixel, &pFont);
        
        GpSolidFill* pTextBrush = NULL;
        GdipCreateSolidFill(w->text_color, &pTextBrush);
        
        // Add some padding
        GpRectF layoutRect = { 10.0f, 10.0f, (float)(width - 20), (float)(height - 20) };
        GdipDrawString(gfx, w->text_buffer, -1, pFont, &layoutRect, NULL, (GpBrush*)pTextBrush);

        GdipDeleteBrush((GpBrush*)pTextBrush);
        GdipDeleteFont(pFont);
        GdipDeleteFontFamily(pFontFamily);
    }
}

static void Notes_Destroy(Widget* base) {
    NotesWidgetData* w = (NotesWidgetData*)base;
    if (w->text_buffer) {
        free(w->text_buffer);
    }
    free(w);
}

static const WidgetVtable notes_vtable = {
    Notes_Render,
    NULL, // No timer
    Notes_Destroy
};

bool NotesWidget_Create(HINSTANCE hInstance, int x, int y, int width, int height, bool click_through, const char* path, int font_size, DWORD bg_color, DWORD text_color) {
    NotesWidgetData* w = (NotesWidgetData*)malloc(sizeof(NotesWidgetData));
    if (!w) return false;
    memset(w, 0, sizeof(NotesWidgetData));

    w->font_size = font_size;
    w->bg_color = bg_color;
    w->text_color = text_color;
    
    // Load file into memory once
    FILE* f = fopen(path, "rb");
    if (f) {
        fseek(f, 0, SEEK_END);
        long fsize = ftell(f);
        fseek(f, 0, SEEK_SET);
        
        char* ansi_buf = (char*)malloc(fsize + 1);
        if (ansi_buf) {
            fread(ansi_buf, 1, fsize, f);
            ansi_buf[fsize] = '\0';
            
            // Convert to wide string
            int wchars = MultiByteToWideChar(CP_UTF8, 0, ansi_buf, -1, NULL, 0);
            w->text_buffer = (WCHAR*)malloc(wchars * sizeof(WCHAR));
            if (w->text_buffer) {
                MultiByteToWideChar(CP_UTF8, 0, ansi_buf, -1, w->text_buffer, wchars);
            }
            free(ansi_buf);
        }
        fclose(f);
    }

    // Static widget
    if (!Widget_Init(&w->base, hInstance, &notes_vtable, x, y, width, height, 0, click_through)) {
        if (w->text_buffer) free(w->text_buffer);
        free(w);
        return false;
    }
    return true;
}
