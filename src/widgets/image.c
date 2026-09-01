#include "image.h"

#include "../widget.h"
#include "../drawing.h"
#include "../config.h"

#include <commdlg.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    Widget     base;
    WidgetSpec spec;
    GpImage*   image;
    char       ini[MAX_PATH];
    WCHAR      path[MAX_PATH];
    FILETIME   stamp;
} ImageWidget;

GpImage* Image_Load(const WCHAR* path) {
    if (!path || !path[0]) return NULL;
    GpBitmap* bitmap = NULL;
    if (GdipCreateBitmapFromFile(path, &bitmap) != 0) return NULL;
    return (GpImage*)bitmap;
}

static bool FileStamp(const WCHAR* path, FILETIME* out) {
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExW(path, GetFileExInfoStandard, &data)) return false;
    *out = data.ftLastWriteTime;
    return true;
}

/*
 * Work out the source rectangle that, drawn into the whole widget box,
 * produces the requested fit. `contain` letterboxes, `cover` crops, and
 * `stretch` ignores the aspect ratio entirely.
 */
static void ComputeFit(int fit, UINT srcW, UINT srcH, int boxW, int boxH,
                       GpRect* src, GpRect* dst) {
    src->X = 0; src->Y = 0; src->Width = (INT)srcW; src->Height = (INT)srcH;
    dst->X = 0; dst->Y = 0; dst->Width = boxW;      dst->Height = boxH;
    if (srcW == 0 || srcH == 0 || fit == FIT_STRETCH) return;

    float scaleX = (float)boxW / (float)srcW;
    float scaleY = (float)boxH / (float)srcH;

    if (fit == FIT_COVER) {
        /* Crop the source so the box is filled edge to edge. */
        float scale = scaleX > scaleY ? scaleX : scaleY;
        int visibleW = (int)((float)boxW / scale + 0.5f);
        int visibleH = (int)((float)boxH / scale + 0.5f);
        if (visibleW > (int)srcW) visibleW = (int)srcW;
        if (visibleH > (int)srcH) visibleH = (int)srcH;
        src->X = ((int)srcW - visibleW) / 2;
        src->Y = ((int)srcH - visibleH) / 2;
        src->Width  = visibleW;
        src->Height = visibleH;
        return;
    }

    /* FIT_CONTAIN — shrink the destination to keep the aspect ratio. */
    float scale = scaleX < scaleY ? scaleX : scaleY;
    dst->Width  = (int)((float)srcW * scale + 0.5f);
    dst->Height = (int)((float)srcH * scale + 0.5f);
    dst->X = (boxW - dst->Width) / 2;
    dst->Y = (boxH - dst->Height) / 2;
}

/* An empty frame has to say what to do with it, or it just looks broken. */
static void PaintHint(const WidgetSpec* spec, GpGraphics* gfx, int width, int height,
                      const WCHAR* message) {
    const WidgetStyle* s = &spec->style;
    float pad = s->padding + 6.0f;
    GpRectF box = { pad, pad, (float)width - pad * 2.0f, (float)height - pad * 2.0f };
    if (box.Width <= 0.0f || box.Height <= 0.0f) return;

    GpPen* pen = NULL;
    if (GdipCreatePen1(Style_ScaleAlpha(s->text_color, 0.30f), 1.0f, UnitPixel, &pen) == 0) {
        GpPath* path = Drawing_RoundedPath(box.X, box.Y, box.Width, box.Height,
                                           s->corner_radius * 0.6f);
        if (path) {
            GdipSetPenDashStyle(pen, DashStyleDash);
            GdipDrawPath(gfx, pen, path);
            GdipDeletePath(path);
        }
        GdipDeletePen(pen);
    }

    TextRun run = Drawing_Run(s, message, box);
    run.color    = Style_ScaleAlpha(s->text_color, 0.55f);
    run.color2   = 0;
    run.gradient = GRAD_NONE;
    run.align_h  = ALIGN_CENTER;
    run.align_v  = ALIGN_CENTER;
    Drawing_Text(gfx, &run);
}

void Image_Paint(const WidgetSpec* spec, GpImage* image,
                 GpGraphics* gfx, int width, int height) {
    if (!spec || !gfx) return;

    const WidgetStyle* s = &spec->style;
    Drawing_Surface(gfx, s, (float)width, (float)height);
    if (!image) {
        PaintHint(spec, gfx, width, height,
                  spec->click_through ? L"No image"
                                      : L"Click to choose an image,\nor drop one here");
        return;
    }

    UINT srcW = 0, srcH = 0;
    GdipGetImageWidth(image, &srcW);
    GdipGetImageHeight(image, &srcH);
    if (srcW == 0 || srcH == 0) return;

    float pad = s->padding;
    int boxW = width  - (int)(pad * 2.0f);
    int boxH = height - (int)(pad * 2.0f);
    if (boxW <= 0 || boxH <= 0) return;

    GpRect src, dst;
    ComputeFit(spec->image_fit, srcW, srcH, boxW, boxH, &src, &dst);

    /* Clip to the panel shape so rounded corners actually round the image. */
    GpPath* clip = NULL;
    GraphicsState state = 0;
    if (s->corner_radius > 0.5f) {
        clip = Drawing_RoundedPath(pad, pad, (float)boxW, (float)boxH, s->corner_radius);
        if (clip) {
            GdipSaveGraphics(gfx, &state);
            GdipSetClipPath(gfx, clip, CombineModeIntersect);
        }
    }

    GdipDrawImageRectRectI(gfx, image,
                           (int)pad + dst.X, (int)pad + dst.Y, dst.Width, dst.Height,
                           src.X, src.Y, src.Width, src.Height,
                           UnitPixel, NULL, NULL, NULL);

    if (clip) {
        GdipRestoreGraphics(gfx, state);
        GdipDeletePath(clip);
    }
}

/* ─────────────────────────── widget plumbing ─────────────────────────── */

static void Image_Render(Widget* base, GpGraphics* gfx, int width, int height) {
    ImageWidget* w = (ImageWidget*)base;
    Image_Paint(&w->spec, w->image, gfx, width, height);
}

static void Image_OnTimer(Widget* base) {
    ImageWidget* w = (ImageWidget*)base;

    FILETIME stamp;
    if (!FileStamp(w->path, &stamp)) return;
    if (CompareFileTime(&stamp, &w->stamp) == 0) return;

    GpImage* fresh = Image_Load(w->path);
    if (!fresh) return;

    w->stamp = stamp;
    if (w->image) GdipDisposeImage(w->image);
    w->image = fresh;
    base->needs_render = true;
}

/* ─────────────────────────── choosing a picture ─────────────────────── */

/*
 * Store the path the way the config prefers it: relative to the INI's own
 * folder when the file lives nearby, so a config folder stays portable, and
 * absolute otherwise.
 */
static void RememberPath(ImageWidget* w, const WCHAR* picked) {
    wcsncpy(w->path, picked, MAX_PATH - 1);
    w->path[MAX_PATH - 1] = L'\0';

    WCHAR folder[MAX_PATH];
    MultiByteToWideChar(CP_UTF8, 0, w->ini, -1, folder, MAX_PATH);
    PathRemoveFileSpecW(folder);

    WCHAR relative[MAX_PATH];
    const WCHAR* store = picked;
    if (folder[0] && PathRelativePathToW(relative, folder, FILE_ATTRIBUTE_DIRECTORY,
                                         picked, 0) &&
        wcsncmp(relative, L"..", 2) != 0) {
        /* Drop the leading ".\" that PathRelativePathTo insists on. */
        store = (relative[0] == L'.' && relative[1] == L'\\') ? relative + 2 : relative;
    }

    char utf8[MAX_PATH];
    WideCharToMultiByte(CP_UTF8, 0, store, -1, utf8, MAX_PATH, NULL, NULL);
    strncpy(w->spec.path, utf8, MAX_PATH - 1);
    w->spec.path[MAX_PATH - 1] = '\0';

    Config_WriteKey(w->ini, w->base.section, "path", utf8);
}

static void UseImage(ImageWidget* w, const WCHAR* picked) {
    GpImage* fresh = Image_Load(picked);
    if (!fresh) return;

    if (w->image) GdipDisposeImage(w->image);
    w->image = fresh;
    RememberPath(w, picked);
    FileStamp(w->path, &w->stamp);
    w->base.needs_render = true;
}

static void ChooseImage(ImageWidget* w) {
    WCHAR path[MAX_PATH];
    wcsncpy(path, w->path, MAX_PATH - 1);
    path[MAX_PATH - 1] = L'\0';

    OPENFILENAMEW ofn;
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = w->base.hwnd;
    ofn.lpstrFilter = L"Images\0*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tif;*.tiff\0"
                      L"All files\0*.*\0";
    ofn.lpstrFile   = path;
    ofn.nMaxFile    = MAX_PATH;
    ofn.lpstrTitle  = L"Choose an image";
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR |
                      OFN_EXPLORER;

    if (GetOpenFileNameW(&ofn)) UseImage(w, path);
}

static bool Image_OnMessage(Widget* base, UINT msg, WPARAM wParam, LPARAM lParam,
                            LRESULT* result) {
    ImageWidget* w = (ImageWidget*)base;
    (void)wParam; (void)lParam;
    *result = 0;

    switch (msg) {
        case WM_SETCURSOR:
            SetCursor(LoadCursor(NULL, IDC_HAND));
            *result = TRUE;
            return true;

        case WM_LBUTTONUP:
            ChooseImage(w);
            return true;

        case WM_DROPFILES: {
            WCHAR path[MAX_PATH];
            HDROP drop = (HDROP)wParam;
            bool ok = DragQueryFileW(drop, 0, path, MAX_PATH) > 0;
            DragFinish(drop);
            if (ok) UseImage(w, path);
            return true;
        }

        default:
            return false;
    }
}

static void Image_Destroy(Widget* base) {
    ImageWidget* w = (ImageWidget*)base;
    if (w->image) GdipDisposeImage(w->image);
    free(w);
}

static const WidgetVtable kImageVtable = {
    Image_Render,
    Image_OnTimer,
    NULL,
    Image_Destroy,
    Image_OnMessage
};

bool ImageWidget_Create(HINSTANCE hInstance, const char* iniPath, const WidgetSpec* spec) {
    ImageWidget* w = (ImageWidget*)calloc(1, sizeof(ImageWidget));
    if (!w) return false;

    w->spec = *spec;
    strncpy(w->ini, iniPath ? iniPath : "", MAX_PATH - 1);
    Config_ResolvePath(iniPath, spec->path, w->path, MAX_PATH);
    w->image = Image_Load(w->path);
    FileStamp(w->path, &w->stamp);

    UINT interval = (spec->reload_seconds > 0) ? (UINT)spec->reload_seconds * 1000u : 0u;
    if (!Widget_Init(&w->base, hInstance, &kImageVtable, spec, interval)) {
        if (w->image) GdipDisposeImage(w->image);
        free(w);
        return false;
    }

    Widget_Render(&w->base);
    return true;
}
