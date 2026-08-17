# LiteWidgets

A lightweight, highly performant widget system for the Windows desktop.

LiteWidgets renders widgets directly on the Windows desktop layer using native Win32 APIs and GDI+. It prioritizes simplicity, low resource usage, and reliability over feature count.

## Features

- **Desktop Layer Pinning**: Widgets live in the Windows `WorkerW` hierarchy, meaning they sit behind normal windows and are completely immune to `Win+D` (Show Desktop).
- **Wallpaper Engine Compatibility**: Widgets sit naturally above live wallpapers but behind desktop icons.
- **Click-Through**: Widgets are input-transparent, allowing you to click desktop icons right through them.
- **Zero Dependencies**: Written in pure C (C17) using raw Win32 APIs. No external libraries, no bloated runtimes.
- **DPI Aware**: Fully supports Per-Monitor V2 DPI scaling.

## Included Widgets

1. **Clock**: A digital clock with configurable format (12h/24h), font, and colors.
2. **Image**: A static image viewer (PNG/JPEG) loaded once into memory with zero disk polling.
3. **Notes**: A lightweight text display that reads a text file once at startup and renders it on the desktop.

## Configuration

Settings are stored in an INI file at `config/widgets.ini`. (See the provided example file for syntax).

## Building

Requires CMake and MSVC.

```bash
cmake -B build
cmake --build build --config Release
```

The executable will be located in `build/bin/Release/LiteWidgets.exe`.

## License

MIT License
