#pragma once
#ifndef IMAGE_H
#define IMAGE_H

#include <windows.h>
#include <stdbool.h>

bool ImageWidget_Create(HINSTANCE hInstance, const char* iniPath,
                        int x, int y, int width, int height,
                        bool click_through, const char* pathA);

#endif /* IMAGE_H */
