# Changelog

All notable changes to this project are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and versions follow
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

**Styling**

- Linear gradients for panel fills and for text, in four directions.
- Text effects: glow, drop shadow, and glyph outlines.
- Letter spacing, line spacing, and `upper` / `lower` case transforms.
- 15 built-in style presets, applied with a single `preset` key and
  overridable field by field.
- Per-widget `opacity`, applied through the layered-window blend so it costs
  nothing to render.

**Clock**

- Analog mode: dial face, outer ring, hour and minute ticks, configurable hand
  widths and lengths, centre hub, and an optional sweeping second hand.
- Custom `time_format` and `date_format` patterns using .NET-style tokens,
  with weekday, month and AM/PM names taken from the Windows locale.
- Independent styling for the date line: its own font, size, colour, tracking
  and case, with effects scaled to match its size.
- `blink_separator`, which dims the colon on odd seconds without the rest of
  the line shifting.

**Layout**

- Anchors: `x` and `y` are offsets from one of nine anchor points, so configs
  survive resolution changes.
- `monitor` selects a display by index.
- Widgets reposition themselves on `WM_DISPLAYCHANGE`.

**Settings editor**

- Rebuilt around a tabbed property editor generated from the config schema, so
  the UI cannot fall behind the file format.
- Live preview that calls the same painter the desktop uses, over a
  checkerboard so transparency reads correctly, ticking once a second.
- Colour rows with a swatch, a hex field, and an alpha slider; system colour
  and font pickers.
- Add, duplicate, delete and rename widgets.
- **Arrange on desktop**: drag widgets in place with 8px grid snapping, then
  save their positions as anchor-relative offsets.
- Only non-default keys are written back, so hand-edited configs stay short.

**Application**

- Live reload — applying settings no longer restarts the process.
- Single-instance guard; a second launch tells the running copy to reload.
- "Start with Windows" toggle in the tray menu.
- Tray menu additions: arrange widgets, reload, open the config folder.
- Tray icon drawn at runtime, so the repository ships no binary assets.
- `--config <path>` and `--settings` command-line options.
- A starter config is written on first run if none exists.

**Widgets**

- `notes` and `image` can re-read their file with `reload_seconds`, comparing
  the last-write time rather than re-reading the contents.
- `image` gained `fit` (`contain` / `cover` / `stretch`) and is clipped to the
  panel's rounded corners.
- `enabled=false` keeps a widget's config without showing it.

**Project**

- `bin\render.exe`: renders any config to PNG entirely offscreen, for
  reviewing themes and for CI.
- `bin\docgen.exe`: generates `docs/CONFIGURATION.md` from the property
  registry, with `--check` to fail a stale reference.
- GitHub Actions CI building release and debug with MSVC and with CMake,
  rendering every example config, and validating the reference.
- Example configs, an architecture document, a contributing guide, and issue
  and pull request templates.

### Changed

- Clocks now sleep until the next boundary that would change what they show:
  one wake-up a minute instead of sixty when seconds are not displayed.
- Text takes a hinted `DrawString` path unless an effect actually requires the
  glyph-path pipeline.
- `build.bat` locates Visual Studio reliably, supports `debug` and `tools`
  targets, and builds at `/W4` with no warnings.
- `CMakeLists.txt` now lists every source file; the CMake build was broken.
- Colours accept `#RGB`, `#ARGB`, `RRGGBB` and `AARRGGBB`.

### Fixed

- Widget positions saved from edit mode had nowhere to go: the INI section
  name was never recorded on the widget.
- The tray icon is restored when Explorer restarts.
- Borders are drawn inset so they are not clipped by the layered-window
  bitmap.
- Notes files encoded as UTF-16LE are read correctly.
- Fonts fall back to Segoe UI and then Arial instead of failing to draw.

## [2.0.0]

### Added

- Styling system with per-widget colours, fonts, alignment and padding.
- Rounded corners.
- Clock date line.
- Settings GUI reachable from the system tray.

### Fixed

- Wallpaper Engine reloading its scene when LiteWidgets started.
- Widgets disappearing on Win+D.
- `UpdateLayeredWindow` alpha handling and relative path resolution.

## [1.0.0]

- Initial desktop widget system: clock, notes and image widgets rendered onto
  the desktop layer.
