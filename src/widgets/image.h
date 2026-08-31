#pragma once
#ifndef IMAGE_H
#define IMAGE_H

#include <windows.h>
#include <stdbool.h>

#include "../spec.h"
#include "../gdiplus_helpers.h"

bool ImageWidget_Create(HINSTANCE hInstance, const char* iniPath, const WidgetSpec* spec);

/* Load a bitmap from disk. Returns NULL on failure. Caller disposes. */
GpImage* Image_Load(const WCHAR* path);

/* Stateless painter shared with the settings preview. */
void Image_Paint(const WidgetSpec* spec, GpImage* image,
                 GpGraphics* gfx, int width, int height);

#endif /* IMAGE_H */
