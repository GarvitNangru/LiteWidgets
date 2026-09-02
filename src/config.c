#include "config.h"

#include "widget.h"
#include "widgets/calendar.h"
#include "widgets/clock.h"
#include "widgets/gauge.h"
#include "widgets/image.h"
#include "widgets/notes.h"

#include <shlwapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Sections can hold a lot of keys; one buffer is reused for each. */
#define SECTION_BUF 8192

void Config_ResolvePath(const char* iniPath, const char* relative, WCHAR* out, DWORD cap) {
    if (!out || cap == 0) return;
    out[0] = L'\0';
    if (!relative || !relative[0]) return;

    WCHAR wide[MAX_PATH];
    MultiByteToWideChar(CP_UTF8, 0, relative, -1, wide, MAX_PATH);

    if (!PathIsRelativeW(wide)) {
        wcsncpy(out, wide, cap - 1);
        out[cap - 1] = L'\0';
        return;
    }

    WCHAR folder[MAX_PATH];
    MultiByteToWideChar(CP_UTF8, 0, iniPath ? iniPath : "", -1, folder, MAX_PATH);
    PathRemoveFileSpecW(folder);
    PathCombineW(out, folder, wide);
}

/* ─────────────────────────── reading ─────────────────────────── */

/*
 * Walk a `key=value\0key=value\0\0` block, handing every pair to `apply`.
 * `presetPass` runs first so an explicit key always beats the preset it came
 * bundled with, regardless of where it sits in the file.
 */
static void ApplyPairs(WidgetSpec* spec, const char* block, bool presetPass) {
    for (const char* entry = block; *entry; entry += strlen(entry) + 1) {
        const char* eq = strchr(entry, '=');
        if (!eq || eq == entry) continue;

        char key[64];
        size_t keyLen = (size_t)(eq - entry);
        if (keyLen >= sizeof(key)) keyLen = sizeof(key) - 1;
        memcpy(key, entry, keyLen);
        key[keyLen] = '\0';

        /* Trim trailing spaces left by hand-edited files. */
        while (keyLen > 0 && (key[keyLen - 1] == ' ' || key[keyLen - 1] == '\t'))
            key[--keyLen] = '\0';

        const char* value = eq + 1;
        while (*value == ' ' || *value == '\t') value++;

        bool isPreset = (_stricmp(key, "preset") == 0);
        if (isPreset != presetPass) continue;

        Spec_Set(spec, key, value);
    }
}

bool Config_ReadSpec(const char* iniPath, const char* section, WidgetSpec* out) {
    if (!iniPath || !section || !out) return false;

    char type[32] = { 0 };
    GetPrivateProfileStringA(section, "type", "", type, sizeof(type), iniPath);
    if (!type[0]) return false;

    char* block = (char*)calloc(SECTION_BUF, 1);
    if (!block) return false;
    GetPrivateProfileSectionA(section, block, SECTION_BUF, iniPath);

    Spec_Defaults(out);
    strncpy(out->section, section, LW_SECTION_LEN - 1);
    out->section[LW_SECTION_LEN - 1] = '\0';

    ApplyPairs(out, block, true);    /* presets first */
    ApplyPairs(out, block, false);   /* then explicit keys */
    Spec_Finalize(out);

    free(block);
    return true;
}

int Config_ReadAll(const char* iniPath, WidgetSpec* out, int maxCount) {
    if (!iniPath || !out || maxCount <= 0) return 0;

    char* names = (char*)calloc(SECTION_BUF, 1);
    if (!names) return 0;
    GetPrivateProfileSectionNamesA(names, SECTION_BUF, iniPath);

    int count = 0;
    for (char* p = names; *p && count < maxCount; p += strlen(p) + 1)
        if (Config_ReadSpec(iniPath, p, &out[count])) count++;

    free(names);
    return count;
}

/* ─────────────────────────── creating ─────────────────────────── */

static bool CreateWidget(const char* iniPath, const WidgetSpec* spec, HINSTANCE hInstance) {
    switch (spec->type) {
        case WIDGET_CLOCK:    return ClockWidget_Create(hInstance, spec);
        case WIDGET_NOTES:    return NotesWidget_Create(hInstance, iniPath, spec);
        case WIDGET_IMAGE:    return ImageWidget_Create(hInstance, iniPath, spec);
        case WIDGET_GAUGE:    return GaugeWidget_Create(hInstance, spec);
        case WIDGET_CALENDAR: return CalendarWidget_Create(hInstance, spec);
        default:              return false;
    }
}

int Config_Load(const char* iniPath, HINSTANCE hInstance) {
    WidgetSpec* specs = (WidgetSpec*)calloc(LW_MAX_WIDGETS, sizeof(WidgetSpec));
    if (!specs) return 0;

    int found = Config_ReadAll(iniPath, specs, LW_MAX_WIDGETS);
    int created = 0;
    for (int i = 0; i < found; i++) {
        if (!specs[i].enabled) continue;
        if (CreateWidget(iniPath, &specs[i], hInstance)) created++;
    }

    free(specs);
    return created;
}

int Config_Reload(const char* iniPath, HINSTANCE hInstance) {
    bool wasEditing = Widget_EditMode();

    Widget_DestroyAll();
    int created = Config_Load(iniPath, hInstance);

    /* Rebuilt widgets start click-through; put them back into edit mode. */
    if (wasEditing) {
        Widget_SetEditMode(false);
        Widget_SetEditMode(true);
    }
    return created;
}

/* ─────────────────────────── preview ─────────────────────────── */

void Config_Paint(const WidgetSpec* spec, const char* iniPath,
                  GpGraphics* gfx, int width, int height) {
    if (!spec || !gfx || width <= 0 || height <= 0) return;

    switch (spec->type) {
        case WIDGET_CLOCK: {
            SYSTEMTIME now;
            GetLocalTime(&now);
            Clock_Paint(spec, &now, gfx, width, height);
            break;
        }
        case WIDGET_NOTES: {
            WCHAR path[MAX_PATH];
            Config_ResolvePath(iniPath, spec->path, path, MAX_PATH);
            WCHAR* text = Notes_LoadText(path);
            /* NULL, not a stand-in string: the widget draws its own hint. */
            Notes_Paint(spec, text, gfx, width, height);
            free(text);
            break;
        }
        case WIDGET_IMAGE: {
            WCHAR path[MAX_PATH];
            Config_ResolvePath(iniPath, spec->path, path, MAX_PATH);
            GpImage* image = Image_Load(path);
            Image_Paint(spec, image, gfx, width, height);
            if (image) GdipDisposeImage(image);
            break;
        }
        case WIDGET_GAUGE: {
            GaugeReading readings[LW_GAUGE_MAX];
            int count = Gauge_Read(spec, readings, LW_GAUGE_MAX);
            Gauge_Paint(spec, readings, count, gfx, width, height);
            break;
        }
        case WIDGET_CALENDAR: {
            SYSTEMTIME now;
            GetLocalTime(&now);
            Calendar_Paint(spec, &now, gfx, width, height);
            break;
        }
        default:
            break;
    }
}

/* ─────────────────────────── writing back ─────────────────────────── */

static ConfigChangedFn g_observer = NULL;

void Config_OnChanged(ConfigChangedFn observer) {
    g_observer = observer;
}

void Config_WriteKey(const char* iniPath, const char* section,
                     const char* key, const char* value) {
    if (!iniPath || !iniPath[0] || !section || !section[0] || !key) return;

    WritePrivateProfileStringA(section, key, value, iniPath);
    WritePrivateProfileStringA(NULL, NULL, NULL, iniPath);   /* flush */
    if (g_observer) g_observer();
}

/* ─────────────────────────── bootstrap ─────────────────────────── */

static const char kDefaultConfig[] =
    "; LiteWidgets configuration\r\n"
    "; Full key reference: docs/CONFIGURATION.md\r\n"
    "\r\n"
    "[clock]\r\n"
    "type=clock\r\n"
    "preset=midnight\r\n"
    "anchor=top_right\r\n"
    "x=-60\r\n"
    "y=60\r\n"
    "width=340\r\n"
    "height=150\r\n"
    "font_size=64\r\n"
    "format=12h\r\n"
    "show_date=true\r\n"
    "date_transform=upper\r\n"
    "date_letter_spacing=2\r\n";

bool Config_WriteDefault(const char* iniPath) {
    if (!iniPath || !iniPath[0]) return false;
    if (GetFileAttributesA(iniPath) != INVALID_FILE_ATTRIBUTES) return false;

    char folder[MAX_PATH];
    strncpy(folder, iniPath, MAX_PATH - 1);
    folder[MAX_PATH - 1] = '\0';
    char* slash = strrchr(folder, '\\');
    if (slash) {
        *slash = '\0';
        CreateDirectoryA(folder, NULL);
    }

    /*
     * Start from the example that ships with the project when it is there.
     * The live config is deliberately not tracked in git, so editing your
     * own layout never shows up as a change to the repository.
     */
    if (slash) {
        char example[MAX_PATH];
        _snprintf(example, MAX_PATH, "%s\\widgets.example.ini", folder);
        if (CopyFileA(example, iniPath, TRUE)) return true;
    }

    HANDLE file = CreateFileA(iniPath, GENERIC_WRITE, 0, NULL, CREATE_NEW,
                              FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return false;

    DWORD written = 0;
    WriteFile(file, kDefaultConfig, (DWORD)(sizeof(kDefaultConfig) - 1), &written, NULL);
    CloseHandle(file);
    return true;
}
