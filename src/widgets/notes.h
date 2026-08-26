#pragma once
#ifndef NOTES_H
#define NOTES_H

#include <windows.h>
#include <stdbool.h>
#include "../style.h"

bool NotesWidget_Create(HINSTANCE hInstance, const char* iniPath,
                        int x, int y, int width, int height,
                        bool click_through, const char* path,
                        const WidgetStyle* style);

#endif /* NOTES_H */
