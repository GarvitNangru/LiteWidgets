@echo off
setlocal

set "VS_PATH=C:\Program Files\Microsoft Visual Studio\2022\Community"
if exist "%VS_PATH%\VC\Auxiliary\Build\vcvars64.bat" (
    call "%VS_PATH%\VC\Auxiliary\Build\vcvars64.bat"
) else (
    echo Could not find vcvars64.bat
    exit /b 1
)

if not exist bin mkdir bin

rc.exe /nologo /fo bin\app.res app.manifest.rc

cl.exe /nologo /O2 /W3 /MT /I src /Fe"bin\LiteWidgets.exe" src\main.c src\desktop.c src\widget.c src\config.c src\widgets\clock.c src\widgets\image.c src\widgets\notes.c bin\app.res /link user32.lib gdi32.lib gdiplus.lib shell32.lib shcore.lib advapi32.lib

if %ERRORLEVEL% equ 0 (
    echo Build successful.
) else (
    echo Build failed.
)
endlocal
