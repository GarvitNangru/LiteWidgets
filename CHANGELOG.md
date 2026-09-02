# Changelog

All notable changes to this project are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and versions follow
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- A `gauge` widget: CPU, memory, disk or battery, drawn as a bar, a ring or
  just the number. Optional detail line -- `17.9 / 31.8 GB`, `1h 20m left` --
  and a warning colour past a threshold in either direction, because a disk
  filling up and a battery draining are trouble in opposite directions. Every
  reading is one cheap system call on the widget's own schedule; a reading
  that rounds to the same number it already drew does not repaint. The CPU
  sample is shared across gauges, since load is a difference between two
  readings and two widgets sampling independently would disagree.
- A `calendar` widget: a month view with today marked. Locale, Monday or
  Sunday weeks, optional ISO week numbers, and the adjacent months dimmed in
  the margins. It checks the date four times an hour and repaints once a day.
- `examples/dashboard.ini`, which arranges the new widgets as a status panel.
- Widget types can carry their own default size. A gauge starts at 280x80
  and a calendar at 300x290; the three older types keep 320x140, since
  changing what an existing config resolves to would move widgets people
  have already placed.

- The `notes` widget is editable in place: click it and type. Mouse and
  keyboard selection, double-click for a word, clipboard, undo that folds a
  burst of typing into one step, and an autosave a second and a half after
  you stop. A note with no file yet is given one on the first keystroke.
- The `image` widget takes a click to open a picker, or a dropped file, and
  writes the path back to the config relative to the config folder.
- Both file-backed widgets say what to do with them when they are empty.
- An application icon, drawn from `tools/mkicon.c` at build time into a
  nine-size .ico. The repository still ships no binary assets.

### Changed

- The settings editor is rebuilt: a scrolling property pane, a resizable
  window, owner-drawn controls throughout, a font list that renders every
  installed family in its own face, colour rows with a swatch and an alpha
  slider, and a light or dark theme taken from the system setting.
- Arranging drags widgets directly rather than through DefWindowProc's move
  loop, which is what made it feel behind the cursor. Hold Shift to leave
  the snap grid.
- `click_through` now defaults per widget type: off for `notes` and `image`,
  on for `clock`.
- Text is laid out with GDI+'s typographic string format rather than the
  default one, so measured and drawn glyph positions agree. Notes wrap their
  own lines, which is what lets the caret land where the glyphs did.

### Fixed

- Starting to arrange widgets never reached a widget that was mid-interaction.
  `WM_LW_CANCEL_EDIT` was routed below the edit-mode switch, so a note being
  typed into only heard about arranging on the way out -- and stayed in its
  editing state, focusable and raised, while it was dragged around.
- Widget positions were written to the INI without flushing the profile cache,
  so a reload straight after arranging could read back the old ones.
- The settings editor silently dropped any property past its 96th. The cap now
  comes from `LW_MAX_PROPERTIES`, which `spec.c` asserts against the registry
  at compile time, so outgrowing it is a build error rather than a key that
  quietly stops appearing.
- The settings editor flickered and speckled. Owner-drawn buttons erase
  themselves with the system face colour before asking us to draw, so every
  rounded control was ringed with pale corners; the window painted the whole
  of itself under its own children on every repaint; hovering the category
  row invalidated the entire window; and each rounded rectangle built and
  threw away its own GDI+ surface. A full repaint went from 33 ms to 7 ms,
  and hovering the pills from a whole-window redraw to two pills.
- Colour rows ran off the edge of a narrow pane -- the value field was
  clipped to six of its eight hex digits and the alpha slider hung outside
  the panel. The field now keeps the room it needs and the slider stands
  down when there is none.
- Settings options disappeared as you used the editor: property rows were
  siblings of the tab control they overlapped, so a tab repaint painted over
  them, and a page with more rows than fitted ran off the bottom of a window
  that could not be resized.
- Clicks never reached a widget that wanted them. Widgets are owned by the
  desktop, and Windows redirects a click on an owned no-activate window into
  activating the owner, which took the keyboard straight back off the widget
  that had just asked for it.
- Widgets vanished behind the wallpaper when the desktop was shown, and never
  came back. They were unowned top-level windows parked at `HWND_BOTTOM`, so
  "show desktop" raised the desktop band over them permanently. They are now
  owned by the desktop window, which carries them up with it while still
  leaving them behind every ordinary window. Nothing is sent to Progman, so
  Wallpaper Engine still does not reload.
- The main window was message-only, which silently discarded every broadcast
  message the app relies on: the tray icon was never restored after Explorer
  restarted, widgets did not reposition on a resolution change, and the clock
  did not pick up a locale change. It is now a hidden top-level window.
- Widgets buried behind a live wallpaper that raises its own render window
  now recover on the next focus change, rather than staying hidden until the
  app is restarted.

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
- `z_order` chooses the layer a widget lives on: `desktop` tracks the
  wallpaper, `bottom` pins to the very back, `top` floats above all windows.

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
