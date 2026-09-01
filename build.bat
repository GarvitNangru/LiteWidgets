@echo off
setlocal enabledelayedexpansion

:: ---------------------------------------------------------------------------
:: LiteWidgets build script (MSVC). Run it from any shell -- it locates and
:: initialises the Visual Studio environment itself.
::
::   build.bat            release build
::   build.bat debug      debug build with symbols
::   build.bat tools      also build bin\render.exe, the offscreen renderer
:: ---------------------------------------------------------------------------

set "CONFIG=release"
set "TOOLS="
for %%a in (%*) do (
    if /i "%%a"=="debug" set "CONFIG=debug"
    if /i "%%a"=="tools" set "TOOLS=1"
)

if defined VCToolsInstallDir goto :have_env

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" set "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"

set "VS_PATH="
if exist "%VSWHERE%" for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS_PATH=%%i"

if not defined VS_PATH (
    echo [build] Visual Studio with the C++ toolset was not found.
    echo [build] Install "Desktop development with C++" and try again.
    exit /b 1
)

:: vcvars is noisy and, on some installs, shells out to a vswhere it expects
:: on PATH; neither is our problem, so both streams go to nul.
call "%VS_PATH%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul
if errorlevel 1 (
    echo [build] Could not initialise the MSVC environment.
    exit /b 1
)

:have_env
if not exist bin mkdir bin
if not exist bin\obj mkdir bin\obj
if not exist bin\obj-tools mkdir bin\obj-tools

:: The application icon is drawn from source rather than checked in, so the
:: repository stays free of binary assets. It has to exist before rc runs.
cl.exe /nologo /W4 /std:c17 /O2 /MT /nologo /D_CRT_SECURE_NO_WARNINGS /I src ^
    /Fe"bin\mkicon.exe" /Fo"bin\obj-tools\\" tools\mkicon.c ^
    /link /OPT:REF gdiplus.lib user32.lib >nul
if errorlevel 1 (
    echo [build] could not build tools\mkicon.c
    exit /b 1
)
bin\mkicon.exe bin\app.ico >nul
if errorlevel 1 exit /b 1

rc.exe /nologo /fo bin\app.res app.manifest.rc
if errorlevel 1 exit /b 1

rc.exe /nologo /i bin /i src /fo bin\icon.res app.icon.rc
if errorlevel 1 exit /b 1

if /i "%CONFIG%"=="debug" (
    set "CFLAGS=/Od /Zi /MTd /DDEBUG"
    set "LDFLAGS=/DEBUG"
) else (
    set "CFLAGS=/O2 /GL /MT /DNDEBUG"
    set "LDFLAGS=/LTCG /OPT:REF /OPT:ICF"
)

set "CORE=src\config.c src\spec.c src\desktop.c src\style.c src\drawing.c src\layout.c src\timefmt.c src\widget.c src\widgets\clock.c src\widgets\image.c src\widgets\notes.c"

cl.exe /nologo /W4 /std:c17 !CFLAGS! /D_CRT_SECURE_NO_WARNINGS /I src ^
    /Fe"bin\LiteWidgets.exe" /Fo"bin\obj\\" /Fd"bin\LiteWidgets.pdb" ^
    src\main.c src\autostart.c src\settings.c %CORE% ^
    bin\app.res bin\icon.res ^
    /link !LDFLAGS! user32.lib gdi32.lib gdiplus.lib shell32.lib shcore.lib ^
    advapi32.lib shlwapi.lib comdlg32.lib comctl32.lib msimg32.lib

if errorlevel 1 (
    echo [build] FAILED
    exit /b 1
)
echo [build] bin\LiteWidgets.exe ^(%CONFIG%^)

if not defined TOOLS goto :done

cl.exe /nologo /W4 /std:c17 !CFLAGS! /D_CRT_SECURE_NO_WARNINGS /I src ^
    /Fe"bin\render.exe" /Fo"bin\obj-tools\\" /Fd"bin\render.pdb" ^
    tools\render.c %CORE% ^
    /link !LDFLAGS! user32.lib gdi32.lib gdiplus.lib shlwapi.lib shell32.lib comdlg32.lib

if errorlevel 1 (
    echo [build] tools FAILED
    exit /b 1
)
echo [build] bin\render.exe

cl.exe /nologo /W4 /std:c17 !CFLAGS! /D_CRT_SECURE_NO_WARNINGS /I src ^
    /Fe"bin\docgen.exe" /Fo"bin\obj-tools\\" /Fd"bin\docgen.pdb" ^
    tools\docgen.c src\spec.c src\style.c src\layout.c ^
    /link !LDFLAGS! user32.lib

if errorlevel 1 (
    echo [build] tools FAILED
    exit /b 1
)
echo [build] bin\docgen.exe

:done
endlocal
