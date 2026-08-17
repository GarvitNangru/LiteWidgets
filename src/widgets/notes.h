#pragma once

#ifndef WIDGET_NOTES_H
#define WIDGET_NOTES_H

#include <windows.h>
#include <stdbool.h>

bool NotesWidget_Create(HINSTANCE hInstance, int x, int y, int width, int height, bool click_through, const char* path, int font_size, DWORD bg_color, DWORD text_color);

#endif
