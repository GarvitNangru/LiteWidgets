#include "autostart.h"

#include <windows.h>
#include <stdio.h>

#define RUN_KEY   "Software\\Microsoft\\Windows\\CurrentVersion\\Run"
#define RUN_VALUE "LiteWidgets"

bool Autostart_IsEnabled(void) {
    HKEY key;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
        return false;

    DWORD type = 0, size = 0;
    LONG result = RegQueryValueExA(key, RUN_VALUE, NULL, &type, NULL, &size);
    RegCloseKey(key);
    return result == ERROR_SUCCESS && type == REG_SZ;
}

bool Autostart_SetEnabled(bool enable) {
    HKEY key;
    if (RegCreateKeyExA(HKEY_CURRENT_USER, RUN_KEY, 0, NULL, 0,
                        KEY_SET_VALUE, NULL, &key, NULL) != ERROR_SUCCESS)
        return false;

    LONG result;
    if (enable) {
        char exe[MAX_PATH];
        DWORD length = GetModuleFileNameA(NULL, exe, MAX_PATH);
        if (length == 0 || length >= MAX_PATH) { RegCloseKey(key); return false; }

        /* Quoted so a path containing spaces still launches. */
        char command[MAX_PATH + 4];
        int written = _snprintf(command, sizeof(command), "\"%s\"", exe);
        if (written < 0) { RegCloseKey(key); return false; }

        result = RegSetValueExA(key, RUN_VALUE, 0, REG_SZ,
                                (const BYTE*)command, (DWORD)written + 1);
    } else {
        result = RegDeleteValueA(key, RUN_VALUE);
        if (result == ERROR_FILE_NOT_FOUND) result = ERROR_SUCCESS;
    }

    RegCloseKey(key);
    return result == ERROR_SUCCESS;
}
