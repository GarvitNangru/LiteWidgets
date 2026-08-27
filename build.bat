@echo off
setlocal

:: Find Visual Studio installation
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath 2^>nul`) do set "VS_PATH=%%i"

if not defined VS_PATH (
    set "VS_PATH=C:\Program Files\Microsoft Visual Studio\2022\Community"
)

if exist "%VS_PATH%\VC\Auxiliary\Build\vcvars64.bat" (
    call "%VS_PATH%\VC\Auxiliary\Build\vcvars64.bat"
) else (
    echo Could not find vcvars64.bat
    exit /b 1
)

if not exist bin mkdir bin

rc.exe /nologo /fo bin\app.res app.manifest.rc

cl.exe /nologo /O2 /W3 /MT /D_CRT_SECURE_NO_WARNINGS /I src ^
    /Fe"bin\LiteWidgets.exe" /Fo"bin\\" ^
    src\main.c src\desktop.c src\widget.c src\config.c src\drawing.c src\settings.c ^
    src\widgets\clock.c src\widgets\image.c src\widgets\notes.c ^
    bin\app.res ^
    /link user32.lib gdi32.lib gdiplus.lib shell32.lib shcore.lib advapi32.lib shlwapi.lib comdlg32.lib

if %ERRORLEVEL% equ 0 (
    echo Build successful.
) else (
    echo Build failed.
)
endlocal
