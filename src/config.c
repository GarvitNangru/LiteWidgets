#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Forward declarations for widget creators
extern bool ClockWidget_Create(HINSTANCE hInstance, int x, int y, int width, int height, bool click_through, const char* format, int font_size, DWORD bg_color, DWORD text_color);
extern bool ImageWidget_Create(HINSTANCE hInstance, int x, int y, int width, int height, bool click_through, const WCHAR* path);
extern bool NotesWidget_Create(HINSTANCE hInstance, int x, int y, int width, int height, bool click_through, const char* path, int font_size, DWORD bg_color, DWORD text_color);

DWORD Config_ParseColor(const char* hexStr) {
    if (!hexStr) return 0xFF000000;
    DWORD val = strtoul(hexStr, NULL, 16);
    return val;
}

static void CreateWidgetFromSection(const char* iniPath, const char* section, HINSTANCE hInstance) {
    char type[32] = {0};
    GetPrivateProfileStringA(section, "type", "", type, sizeof(type), iniPath);
    
    if (strlen(type) == 0) return;

    int x = GetPrivateProfileIntA(section, "x", 0, iniPath);
    int y = GetPrivateProfileIntA(section, "y", 0, iniPath);
    int width = GetPrivateProfileIntA(section, "width", 200, iniPath);
    int height = GetPrivateProfileIntA(section, "height", 100, iniPath);
    
    char clickStr[16] = {0};
    GetPrivateProfileStringA(section, "click_through", "true", clickStr, sizeof(clickStr), iniPath);
    bool click_through = (_stricmp(clickStr, "true") == 0 || _stricmp(clickStr, "1") == 0);

    if (_stricmp(type, "clock") == 0) {
        char format[16] = {0};
        GetPrivateProfileStringA(section, "format", "12h", format, sizeof(format), iniPath);
        int font_size = GetPrivateProfileIntA(section, "font_size", 32, iniPath);
        
        char bgStr[16] = {0};
        char fgStr[16] = {0};
        GetPrivateProfileStringA(section, "bg_color", "CC1A1A2E", bgStr, sizeof(bgStr), iniPath);
        GetPrivateProfileStringA(section, "text_color", "FFFFFFFF", fgStr, sizeof(fgStr), iniPath);
        
        ClockWidget_Create(hInstance, x, y, width, height, click_through, format, font_size, Config_ParseColor(bgStr), Config_ParseColor(fgStr));
    }
    else if (_stricmp(type, "image") == 0) {
        char pathA[MAX_PATH] = {0};
        GetPrivateProfileStringA(section, "path", "", pathA, sizeof(pathA), iniPath);
        
        WCHAR pathW[MAX_PATH] = {0};
        MultiByteToWideChar(CP_UTF8, 0, pathA, -1, pathW, MAX_PATH);
        
        ImageWidget_Create(hInstance, x, y, width, height, click_through, pathW);
    }
    else if (_stricmp(type, "notes") == 0) {
        char pathA[MAX_PATH] = {0};
        GetPrivateProfileStringA(section, "path", "", pathA, sizeof(pathA), iniPath);
        int font_size = GetPrivateProfileIntA(section, "font_size", 14, iniPath);
        
        char bgStr[16] = {0};
        char fgStr[16] = {0};
        GetPrivateProfileStringA(section, "bg_color", "CC1A1A2E", bgStr, sizeof(bgStr), iniPath);
        GetPrivateProfileStringA(section, "text_color", "FFFFFFFF", fgStr, sizeof(fgStr), iniPath);
        
        NotesWidget_Create(hInstance, x, y, width, height, click_through, pathA, font_size, Config_ParseColor(bgStr), Config_ParseColor(fgStr));
    }
}

void Config_Load(const char* iniPath, HINSTANCE hInstance) {
    char sectionNames[2048] = {0};
    DWORD chars = GetPrivateProfileSectionNamesA(sectionNames, sizeof(sectionNames), iniPath);
    
    if (chars > 0) {
        char* p = sectionNames;
        while (*p != '\0') {
            CreateWidgetFromSection(iniPath, p, hInstance);
            p += strlen(p) + 1;
        }
    }
}
