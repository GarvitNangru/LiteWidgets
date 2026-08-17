#pragma once

#ifndef WIDGET_IMAGE_H
#define WIDGET_IMAGE_H

#include <windows.h>
#include <stdbool.h>

bool ImageWidget_Create(HINSTANCE hInstance, int x, int y, int width, int height, bool click_through, const WCHAR* path);

#endif
