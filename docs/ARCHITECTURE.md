# Architecture

LiteWidgets is a single Win32 process that draws widgets onto the desktop
layer. It is deliberately small: pure C17, no external dependencies, and no
framework between the config file and `UpdateLayeredWindow`.

## The shape of it

```
config/widgets.ini
        |
        v
   config.c  ──reads sections──>  spec.c  ──> WidgetSpec (the whole config, typed)
        |                                          |
        | creates                                  | consumed by
        v                                          v
   widget.c  ──owns the HWND, timer, blend──>  widgets/clock.c
        |                                      widgets/notes.c
        |                                      widgets/image.c
        |                                      widgets/gauge.c
        |                                      widgets/calendar.c
        v                                          |
  UpdateLayeredWindow                              | all draw through
                                                   v
                                          drawing.c + style.c
```

Two rules keep this from tangling:

**Painting never touches windows.** Every widget exposes a stateless painter —
`Clock_Paint(spec, now, gfx, w, h)` — that draws into any GDI+ graphics
context. The live widget calls it with a layered-window bitmap; the settings
preview calls it with an offscreen bitmap; `tools/render.c` calls it with one
it saves to PNG. There is no second code path to keep in sync, which is why
the preview is exact rather than approximate.

**Adding a widget type is five edits.** A `WidgetType` and an options struct
in `spec.h`; defaults, keys and property rows in `spec.c`; a `widgets/*.c`
with a stateless painter and a vtable; a case each in `Config_Load` and
`Config_Paint`; and the file in both build lists. The settings editor, the
generated reference and the offscreen renderer all pick it up from the
property registry without being told.

**Input goes through one hook.** A widget may implement `on_message` in its
vtable and see window messages before the default handling. That is the whole
of the interactivity story: the notes editor takes keystrokes through it, and
both notes and image accept dropped files. Arranging outranks it -- while the
user is placing widgets, every widget is a draggable block and nothing else.

**Config is one typed struct.** `WidgetSpec` (in `spec.h`) is the complete
description of a widget. `Spec_Set()` maps a single `key = value` pair onto
it, and everything that produces config — the INI loader, the style presets,
the settings editor — goes through that one function. Adding a knob means
adding a field, one line in `Spec_Set()`, and one row in the property table.

## Files

| File | Responsibility |
| --- | --- |
| `main.c` | Process lifetime, tray icon and menu, single-instance guard, display-change handling |
| `config.c` | INI to `WidgetSpec`, widget creation, live reload, preview dispatch |
| `spec.c` | The `WidgetSpec` model, `Spec_Set`, derived defaults, the property registry |
| `style.c` | `WidgetStyle`, colour parsing, the built-in presets |
| `widget.c` | Window creation, the layered-window blit, timers, edit mode, the widget registry |
| `drawing.c` | Rounded panels, gradients, the glyph-path text pipeline (glow, shadow, outline, tracking) |
| `layout.c` | Anchors and monitor geometry |
| `timefmt.c` | Locale-aware date/time patterns |
| `settings.c` | The editor window and its owner-drawn controls, built from the property registry |
| `autostart.c` | The per-user Run key |
| `desktop.c` | Desktop-shell integration hooks |
| `widgets/*.c` | One painter and one lifecycle per widget type |
| `tools/mkicon.c` | Draws the application icon and writes the .ico the build embeds |

## The property registry

`Spec_Properties()` in `spec.c` returns a table describing every config key:
its type, which widget types it applies to, its choices, its default, and a
one-line description. Three things are generated from that table:

- the settings editor's tabs and controls (`settings.c`),
- `docs/CONFIGURATION.md` (`tools/docgen.c`),
- the validation that CI runs to catch a stale reference.

So a new key needs no UI work and no documentation work. It needs a field, a
`Spec_Set` case, and a table row.

## Why it stays cheap

**Widgets sleep between changes.** A clock showing hours and minutes does not
poll once a second — `Clock_NextInterval()` computes the milliseconds until
the next minute boundary and the timer is re-armed for exactly that. One
wake-up a minute instead of sixty. Seconds, a blinking separator, or a
sweeping second hand each opt into a faster tick, and only then.

**Nothing redraws unless it would look different.** `on_timer` compares a
cheap signature of what is currently displayed and sets `needs_render` only on
a change.

**Text takes the cheap path when it can.** `Drawing_Text` uses plain
`GdipDrawString` — hinted and fast — unless the run actually asks for a
gradient, glow, shadow, outline or letter spacing. Only then does it build a
glyph path.

**Opacity is free.** The master `opacity` is passed as
`BLENDFUNCTION.SourceConstantAlpha` to `UpdateLayeredWindow` rather than being
composited into the bitmap.

**File-backed widgets stat, they do not read.** Notes and image widgets with
`reload_seconds` set compare the file's last-write time and only re-read when
it moves.

## Desktop integration

Widgets are `WS_POPUP` layered windows with no owner, pushed to `HWND_BOTTOM`.
They deliberately do *not* reparent to `WorkerW`: doing so makes Wallpaper
Engine reload its scene on every start. Instead, `WM_WINDOWPOSCHANGING` strips
`SWP_HIDEWINDOW`, which is what makes them survive Win+D and "show desktop".

`WS_EX_TRANSPARENT` gives click-through; edit mode clears it, raises the
windows to topmost, and paints an accent outline so they can be dragged.

## The tools

`build.bat tools` builds two console programs that exist to keep the project
honest:

- **`bin\render.exe`** draws a config to PNG offscreen. It needs no desktop,
  so CI runs it on every push to catch painting regressions, and it is how the
  images in this repository are produced.
- **`bin\docgen.exe`** writes `docs/CONFIGURATION.md` from the property
  registry, and with `--check` fails when the committed file is stale.
