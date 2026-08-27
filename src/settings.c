#include "settings.h"
#include <windows.h>
#include <commdlg.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

/* ── Layout ── */
#define WIN_W      720
#define WIN_H      560
#define LIST_W     180
#define ROW_H      27
#define CTRL_H     22
#define LABEL_W    78
#define HALF_W     ((WIN_W - LIST_W - 40) / 2)

/* ── Control IDs ── */
#define IDC_LIST    100
#define IDC_ADD     101
#define IDC_REMOVE  102
#define IDC_SAVE    103
#define IDC_BROWSE  104
#define IDC_PROP    200  /* Props use 200..299 */

/* ── Property table ── */
typedef struct {
    const char* label;
    const char* key;
    int   type;      /* 0=edit, 1=combo, 2=check */
    int   group;     /* 0=common, 1=clock, 2=file */
    int   wide;      /* 1=full row width */
    const char* opts;  /* pipe-separated combo options */
    const char* def;   /* default value */
    HWND  hLabel;
    HWND  hCtrl;
} Prop;

static Prop g_props[] = {
    /* ── Common ── */
    {"Type",          "type",           1,0,0, "clock|notes|image",                "clock",    0,0},
    {"X",             "x",              0,0,0, NULL,                                "100",      0,0},
    {"Y",             "y",              0,0,0, NULL,                                "100",      0,0},
    {"Width",         "width",          0,0,0, NULL,                                "280",      0,0},
    {"Height",        "height",         0,0,0, NULL,                                "120",      0,0},
    {"Click-through", "click_through",  2,0,0, NULL,                                "true",     0,0},
    {"Bg Color",      "bg_color",       0,0,0, NULL,                                "D9181825", 0,0},
    {"Text Color",    "text_color",     0,0,0, NULL,                                "FFFFFFFF", 0,0},
    {"Border Color",  "border_color",   0,0,0, NULL,                                "33FFFFFF", 0,0},
    {"Border Width",  "border_width",   0,0,0, NULL,                                "1",        0,0},
    {"Radius",        "corner_radius",  0,0,0, NULL,                                "12",       0,0},
    {"Font",          "font_family",    0,0,1, NULL,                                "Segoe UI", 0,0},
    {"Font Size",     "font_size",      0,0,0, NULL,                                "14",       0,0},
    {"Font Style",    "font_style",     1,0,0, "regular|bold|italic|bold_italic",   "regular",  0,0},
    {"H-Align",       "align_h",        1,0,0, "left|center|right",                 "center",   0,0},
    {"V-Align",       "align_v",        1,0,0, "top|center|bottom",                 "center",   0,0},
    {"Padding",       "padding",        0,0,0, NULL,                                "10",       0,0},
    /* ── Clock ── */
    {"Format",        "format",         1,1,0, "12h|24h",                           "12h",      0,0},
    {"Show Date",     "show_date",      2,1,0, NULL,                                "true",     0,0},
    {"Show Seconds",  "show_seconds",   2,1,0, NULL,                                "false",    0,0},
    {"Date Size",     "date_font_size", 0,1,0, NULL,                                "14",       0,0},
    {"Date Style",    "date_font_style",1,1,0, "regular|bold|italic",               "regular",  0,0},
    {"Date Color",    "date_color",     0,1,0, NULL,                                "99FFFFFF", 0,0},
    /* ── File ── */
    {"File Path",     "path",           0,2,1, NULL,                                "",         0,0},
};
#define PROP_COUNT (sizeof(g_props)/sizeof(g_props[0]))

/* ── Widget entry ── */
typedef struct {
    char section[64];
    char vals[24][256];
} Entry;

#define MAX_W 32
static Entry   g_entries[MAX_W];
static int     g_cnt    = 0;
static int     g_sel    = -1;
static char    g_ini[MAX_PATH];
static HWND    g_hWnd   = NULL;
static HWND    g_hList  = NULL;
static HWND    g_hBrowse= NULL;
static HFONT   g_hFont  = NULL;
static HFONT   g_hBold  = NULL;
static HWND    g_hSecClock = NULL;
static HWND    g_hSecFile  = NULL;
static HINSTANCE g_hInst = NULL;

/* ── Helpers ── */
static void FillCombo(HWND cb, const char* opts) {
    if (!opts) return;
    char buf[256]; strncpy(buf, opts, 255); buf[255]=0;
    char* t = strtok(buf, "|");
    while (t) { SendMessageA(cb, CB_ADDSTRING, 0, (LPARAM)t); t = strtok(NULL, "|"); }
}
static void SetCombo(HWND cb, const char* val) {
    int i = (int)SendMessageA(cb, CB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)val);
    SendMessageA(cb, CB_SETCURSEL, i >= 0 ? i : 0, 0);
}
static void GetCombo(HWND cb, char* out, int mx) {
    int i = (int)SendMessageA(cb, CB_GETCURSEL, 0, 0);
    if (i >= 0) SendMessageA(cb, CB_GETLBTEXT, i, (LPARAM)out); else out[0]=0;
}

static HWND Lbl(HWND p, const char* t, int x, int y, int w, int h) {
    HWND h2 = CreateWindowExA(0,"STATIC",t,WS_CHILD|WS_VISIBLE|SS_LEFT,x,y,w,h,p,NULL,g_hInst,NULL);
    SendMessageA(h2, WM_SETFONT, (WPARAM)g_hFont, TRUE); return h2;
}
static HWND Edt(HWND p, int id, int x, int y, int w) {
    HWND h2 = CreateWindowExA(WS_EX_CLIENTEDGE,"EDIT","",WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL,x,y,w,CTRL_H,p,(HMENU)(INT_PTR)id,g_hInst,NULL);
    SendMessageA(h2, WM_SETFONT, (WPARAM)g_hFont, TRUE); return h2;
}
static HWND Cmb(HWND p, int id, int x, int y, int w, const char* opts) {
    HWND h2 = CreateWindowExA(0,"COMBOBOX","",WS_CHILD|WS_VISIBLE|CBS_DROPDOWNLIST|WS_VSCROLL,x,y,w,200,p,(HMENU)(INT_PTR)id,g_hInst,NULL);
    SendMessageA(h2, WM_SETFONT, (WPARAM)g_hFont, TRUE);
    FillCombo(h2, opts); return h2;
}
static HWND Chk(HWND p, int id, const char* t, int x, int y, int w) {
    HWND h2 = CreateWindowExA(0,"BUTTON",t,WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX,x,y,w,CTRL_H,p,(HMENU)(INT_PTR)id,g_hInst,NULL);
    SendMessageA(h2, WM_SETFONT, (WPARAM)g_hFont, TRUE); return h2;
}
static HWND Btn(HWND p, int id, const char* t, int x, int y, int w, int h) {
    HWND h2 = CreateWindowExA(0,"BUTTON",t,WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,x,y,w,h,p,(HMENU)(INT_PTR)id,g_hInst,NULL);
    SendMessageA(h2, WM_SETFONT, (WPARAM)g_hFont, TRUE); return h2;
}
static HWND SectionLabel(HWND p, const char* t, int x, int y, int w) {
    HWND h2 = CreateWindowExA(0,"STATIC",t,WS_CHILD|WS_VISIBLE|SS_LEFT,x,y,w,18,p,NULL,g_hInst,NULL);
    SendMessageA(h2, WM_SETFONT, (WPARAM)g_hBold, TRUE); return h2;
}

/* ── Load entries from INI ── */
static void LoadEntries(void) {
    g_cnt = 0;
    char secs[4096];
    DWORD len = GetPrivateProfileSectionNamesA(secs, sizeof(secs), g_ini);
    if (!len) return;
    char* p = secs;
    while (*p && g_cnt < MAX_W) {
        char tp[32];
        GetPrivateProfileStringA(p, "type", "", tp, sizeof(tp), g_ini);
        if (tp[0]) {
            Entry* e = &g_entries[g_cnt];
            memset(e, 0, sizeof(Entry));
            strncpy(e->section, p, sizeof(e->section)-1);
            for (int i = 0; i < (int)PROP_COUNT; i++)
                GetPrivateProfileStringA(p, g_props[i].key, "", e->vals[i], sizeof(e->vals[i]), g_ini);
            g_cnt++;
        }
        p += strlen(p) + 1;
    }
}

/* ── Save entries to INI ── */
static void SaveEntries(void) {
    /* Delete all existing sections */
    char secs[4096];
    DWORD len = GetPrivateProfileSectionNamesA(secs, sizeof(secs), g_ini);
    if (len) {
        char* p = secs;
        while (*p) { WritePrivateProfileStringA(p, NULL, NULL, g_ini); p += strlen(p)+1; }
    }
    /* Write entries */
    for (int e = 0; e < g_cnt; e++) {
        const char* tp = g_entries[e].vals[0]; /* type */
        for (int i = 0; i < (int)PROP_COUNT; i++) {
            if (!g_entries[e].vals[i][0]) continue;
            int g = g_props[i].group;
            if (g == 0 ||
                (g == 1 && _stricmp(tp, "clock") == 0) ||
                (g == 2 && (_stricmp(tp, "notes") == 0 || _stricmp(tp, "image") == 0)))
                WritePrivateProfileStringA(g_entries[e].section, g_props[i].key, g_entries[e].vals[i], g_ini);
        }
    }
}

/* ── Populate controls from entry ── */
static void Populate(int idx) {
    if (idx < 0 || idx >= g_cnt) return;
    Entry* e = &g_entries[idx];
    for (int i = 0; i < (int)PROP_COUNT; i++) {
        const char* v = e->vals[i][0] ? e->vals[i] : g_props[i].def;
        switch (g_props[i].type) {
            case 0: SetWindowTextA(g_props[i].hCtrl, v); break;
            case 1: SetCombo(g_props[i].hCtrl, v); break;
            case 2: SendMessageA(g_props[i].hCtrl, BM_SETCHECK,
                        (_stricmp(v,"true")==0||_stricmp(v,"1")==0)?BST_CHECKED:BST_UNCHECKED, 0); break;
        }
    }
}

/* ── Collect controls into entry ── */
static void Collect(int idx) {
    if (idx < 0 || idx >= g_cnt) return;
    Entry* e = &g_entries[idx];
    for (int i = 0; i < (int)PROP_COUNT; i++) {
        switch (g_props[i].type) {
            case 0: GetWindowTextA(g_props[i].hCtrl, e->vals[i], sizeof(e->vals[i])); break;
            case 1: GetCombo(g_props[i].hCtrl, e->vals[i], sizeof(e->vals[i])); break;
            case 2: strcpy(e->vals[i],
                        SendMessageA(g_props[i].hCtrl,BM_GETCHECK,0,0)==BST_CHECKED?"true":"false"); break;
        }
    }
}

/* ── Show/hide type-specific controls ── */
static void UpdateVisibility(void) {
    char tp[32]; GetCombo(g_props[0].hCtrl, tp, sizeof(tp));
    int clk = (_stricmp(tp,"clock")==0);
    int fil = (_stricmp(tp,"notes")==0 || _stricmp(tp,"image")==0);
    for (int i = 0; i < (int)PROP_COUNT; i++) {
        if (g_props[i].group == 1) {
            ShowWindow(g_props[i].hLabel, clk?SW_SHOW:SW_HIDE);
            ShowWindow(g_props[i].hCtrl,  clk?SW_SHOW:SW_HIDE);
        } else if (g_props[i].group == 2) {
            ShowWindow(g_props[i].hLabel, fil?SW_SHOW:SW_HIDE);
            ShowWindow(g_props[i].hCtrl,  fil?SW_SHOW:SW_HIDE);
        }
    }
    if (g_hSecClock) ShowWindow(g_hSecClock, clk?SW_SHOW:SW_HIDE);
    if (g_hSecFile)  ShowWindow(g_hSecFile,  fil?SW_SHOW:SW_HIDE);
    if (g_hBrowse)   ShowWindow(g_hBrowse,   fil?SW_SHOW:SW_HIDE);
}

/* ── Refresh listbox ── */
static void RefreshList(void) {
    SendMessageA(g_hList, LB_RESETCONTENT, 0, 0);
    for (int i = 0; i < g_cnt; i++) {
        char buf[96];
        const char* tp = g_entries[i].vals[0][0] ? g_entries[i].vals[0] : "?";
        _snprintf(buf, sizeof(buf), "%s  -  %s", tp, g_entries[i].section);
        SendMessageA(g_hList, LB_ADDSTRING, 0, (LPARAM)buf);
    }
}

static void OnSelChanged(void) {
    int newSel = (int)SendMessageA(g_hList, LB_GETCURSEL, 0, 0);
    if (g_sel >= 0 && g_sel < g_cnt) Collect(g_sel);
    g_sel = newSel;
    if (g_sel >= 0) { Populate(g_sel); UpdateVisibility(); }
}

/* ── Create all controls ── */
static void CreateControls(HWND parent) {
    int rx = LIST_W + 25; /* right panel x */
    int rw = WIN_W - rx - 15;

    /* Left panel: listbox + buttons */
    g_hList = CreateWindowExA(WS_EX_CLIENTEDGE, "LISTBOX", "",
        WS_CHILD|WS_VISIBLE|WS_VSCROLL|LBS_NOTIFY,
        12, 12, LIST_W, WIN_H - 80, parent, (HMENU)IDC_LIST, g_hInst, NULL);
    SendMessageA(g_hList, WM_SETFONT, (WPARAM)g_hFont, TRUE);

    Btn(parent, IDC_ADD,    "+ Add",    12, WIN_H-60, 85, 28);
    Btn(parent, IDC_REMOVE, "- Remove", 105, WIN_H-60, 85, 28);

    /* Right panel: properties */
    int y = 10;
    int col = 0;
    int lastGroup = -1;
    int cw_half = (rw - LABEL_W * 2 - 15) / 2; /* control width for half-row */
    int cw_full = rw - LABEL_W - 5;             /* control width for full-row */

    for (int i = 0; i < (int)PROP_COUNT; i++) {
        /* Section header on group change */
        if (g_props[i].group != lastGroup) {
            if (col == 1) { y += ROW_H; col = 0; }
            if (g_props[i].group == 0 && lastGroup < 0) {
                /* no header for first group */
            } else if (g_props[i].group == 1) {
                y += 6;
                g_hSecClock = SectionLabel(parent, "── Clock Options ──", rx, y, rw);
                y += 22;
            } else if (g_props[i].group == 2) {
                y += 6;
                g_hSecFile = SectionLabel(parent, "── File ──", rx, y, rw);
                y += 22;
            }
            lastGroup = g_props[i].group;
        }

        int isWide = g_props[i].wide;
        int cx, cw;

        if (isWide || g_props[i].type == 2) {
            /* Full row */
            if (col == 1) { y += ROW_H; col = 0; }
            cx = rx;
            cw = cw_full;
        } else if (col == 0) {
            cx = rx;
            cw = cw_half;
        } else {
            cx = rx + LABEL_W + cw_half + 15;
            cw = cw_half;
        }

        /* Create label + control */
        if (g_props[i].type == 2) {
            /* Checkbox: label is part of the control */
            g_props[i].hLabel = CreateWindowExA(0,"STATIC","",WS_CHILD,0,0,0,0,parent,NULL,g_hInst,NULL);
            g_props[i].hCtrl = Chk(parent, IDC_PROP+i, g_props[i].label, cx, y, cw);
        } else {
            g_props[i].hLabel = Lbl(parent, g_props[i].label, cx, y+3, LABEL_W, 18);
            int ecx = cx + LABEL_W;
            int ecw = cw;
            if (isWide && strcmp(g_props[i].key, "path") == 0) {
                ecw -= 70;
                g_hBrowse = Btn(parent, IDC_BROWSE, "Browse...", ecx+ecw+5, y, 65, CTRL_H);
            }
            if (g_props[i].type == 0)
                g_props[i].hCtrl = Edt(parent, IDC_PROP+i, ecx, y, ecw);
            else
                g_props[i].hCtrl = Cmb(parent, IDC_PROP+i, ecx, y, ecw, g_props[i].opts);
        }

        if (isWide || g_props[i].type == 2) {
            y += ROW_H;
            col = 0;
        } else {
            col++;
            if (col >= 2) { col = 0; y += ROW_H; }
        }
    }

    /* Save & Reload button */
    Btn(parent, IDC_SAVE, "Save && Reload", WIN_W - 155, WIN_H - 60, 135, 32);
}

/* ── Window procedure ── */
static LRESULT CALLBACK SettingsWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
            CreateControls(hWnd);
            LoadEntries();
            RefreshList();
            if (g_cnt > 0) {
                SendMessageA(g_hList, LB_SETCURSEL, 0, 0);
                g_sel = 0;
                Populate(0);
                UpdateVisibility();
            }
            return 0;

        case WM_COMMAND: {
            int id = LOWORD(wParam);
            int code = HIWORD(wParam);

            /* Listbox selection changed */
            if (id == IDC_LIST && code == LBN_SELCHANGE) {
                OnSelChanged();
                return 0;
            }

            /* Type combo changed → show/hide type-specific controls */
            if (id == IDC_PROP + 0 && code == CBN_SELCHANGE) {
                UpdateVisibility();
                return 0;
            }

            /* Add widget */
            if (id == IDC_ADD) {
                if (g_cnt >= MAX_W) break;
                char sec[64]; int n = 1; int found;
                do {
                    _snprintf(sec, sizeof(sec), "widget_%d", n++);
                    found = 0;
                    for (int i = 0; i < g_cnt; i++)
                        if (_stricmp(g_entries[i].section, sec) == 0) { found = 1; break; }
                } while (found);
                Entry* e = &g_entries[g_cnt];
                memset(e, 0, sizeof(Entry));
                strncpy(e->section, sec, sizeof(e->section)-1);
                for (int i = 0; i < (int)PROP_COUNT; i++)
                    strncpy(e->vals[i], g_props[i].def, sizeof(e->vals[i])-1);
                g_cnt++;
                RefreshList();
                SendMessageA(g_hList, LB_SETCURSEL, g_cnt-1, 0);
                OnSelChanged();
                return 0;
            }

            /* Remove widget */
            if (id == IDC_REMOVE) {
                if (g_sel < 0 || g_sel >= g_cnt) break;
                for (int i = g_sel; i < g_cnt-1; i++)
                    g_entries[i] = g_entries[i+1];
                g_cnt--;
                g_sel = -1;
                RefreshList();
                if (g_cnt > 0) {
                    SendMessageA(g_hList, LB_SETCURSEL, 0, 0);
                    OnSelChanged();
                }
                return 0;
            }

            /* Browse for file */
            if (id == IDC_BROWSE) {
                char path[MAX_PATH] = {0};
                OPENFILENAMEA ofn = {0};
                ofn.lStructSize = sizeof(ofn);
                ofn.hwndOwner = hWnd;
                ofn.lpstrFilter = "Text Files\0*.txt\0All Files\0*.*\0";
                ofn.lpstrFile = path;
                ofn.nMaxFile = MAX_PATH;
                ofn.Flags = OFN_FILEMUSTEXIST;
                if (GetOpenFileNameA(&ofn)) {
                    /* Find path control */
                    for (int i=0;i<(int)PROP_COUNT;i++) {
                        if (strcmp(g_props[i].key,"path")==0) {
                            SetWindowTextA(g_props[i].hCtrl, path);
                            break;
                        }
                    }
                }
                return 0;
            }

            /* Save & Reload */
            if (id == IDC_SAVE) {
                if (g_sel >= 0) Collect(g_sel);
                SaveEntries();
                /* Restart app */
                char exePath[MAX_PATH];
                GetModuleFileNameA(NULL, exePath, MAX_PATH);
                ShellExecuteA(NULL, "open", exePath, NULL, NULL, SW_SHOWDEFAULT);
                PostQuitMessage(0);
                return 0;
            }
            break;
        }

        case WM_CLOSE:
            DestroyWindow(hWnd);
            g_hWnd = NULL;
            return 0;

        case WM_DESTROY:
            if (g_hFont) { DeleteObject(g_hFont); g_hFont = NULL; }
            if (g_hBold) { DeleteObject(g_hBold); g_hBold = NULL; }
            g_hWnd = NULL;
            return 0;
    }
    return DefWindowProcA(hWnd, msg, wParam, lParam);
}

/* ── Public API ── */
void Settings_Open(HINSTANCE hInstance, const char* iniPath) {
    if (g_hWnd) { SetForegroundWindow(g_hWnd); return; } /* Already open */

    g_hInst = hInstance;
    strncpy(g_ini, iniPath, MAX_PATH-1);
    g_sel = -1;
    g_cnt = 0;

    /* Fonts */
    g_hFont = CreateFontA(-14, 0,0,0, FW_NORMAL, 0,0,0, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, "Segoe UI");
    g_hBold = CreateFontA(-13, 0,0,0, FW_BOLD, 0,0,0, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, "Segoe UI");

    /* Register class */
    static bool reg = false;
    if (!reg) {
        WNDCLASSA wc = {0};
        wc.lpfnWndProc = SettingsWndProc;
        wc.hInstance = hInstance;
        wc.lpszClassName = "LiteWidgetsSettings";
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        RegisterClassA(&wc);
        reg = true;
    }

    /* Center on screen */
    int sx = (GetSystemMetrics(SM_CXSCREEN) - WIN_W) / 2;
    int sy = (GetSystemMetrics(SM_CYSCREEN) - WIN_H) / 2;

    g_hWnd = CreateWindowExA(0, "LiteWidgetsSettings", "LiteWidgets Settings",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        sx, sy, WIN_W, WIN_H,
        NULL, NULL, hInstance, NULL);

    ShowWindow(g_hWnd, SW_SHOW);
    UpdateWindow(g_hWnd);
}
