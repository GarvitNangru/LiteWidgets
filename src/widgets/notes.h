#pragma once
#ifndef NOTES_H
#define NOTES_H

#include <windows.h>
#include <stdbool.h>

#include "../spec.h"
#include "../gdiplus_helpers.h"

bool NotesWidget_Create(HINSTANCE hInstance, const char* iniPath, const WidgetSpec* spec);

/* Read a UTF-8 (or UTF-16LE) text file into a wide string. Caller frees. */
WCHAR* Notes_LoadText(const WCHAR* path);

/* Stateless painter shared with the settings preview. */
void Notes_Paint(const WidgetSpec* spec, const WCHAR* text,
                 GpGraphics* gfx, int width, int height);

#endif /* NOTES_H */
