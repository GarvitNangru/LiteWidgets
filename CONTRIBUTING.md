# Contributing

Thanks for looking. LiteWidgets is small on purpose, and the goal is to keep
it that way while making it genuinely nice to use.

## What the project is trying to be

- **Cheap.** A widget that is not changing should not be waking the process
  up. Features that need a timer, a network call, or an animation have to earn
  it, and should be opt-in.
- **Dependency-free.** Pure C17 against the Win32 and GDI+ flat APIs. No
  vendored libraries, no C++ runtime, no package manager.
- **Configurable without code.** If you find yourself hardcoding a colour or a
  size, it probably wants to be a config key.

## Building

You need Visual Studio with the "Desktop development with C++" workload.

```bat
build.bat              :: release
build.bat debug        :: with symbols
build.bat tools        :: also builds bin\render.exe and bin\docgen.exe
```

CMake works too, if you prefer it:

```bat
cmake -B build
cmake --build build --config Release
```

Builds must stay clean at `/W4`. If a warning is genuinely wrong, say so in
the pull request rather than suppressing it quietly.

## Seeing your changes

`bin\render.exe` draws a config to PNG entirely offscreen — no desktop, no
running app, no restart loop:

```bat
bin\render.exe examples\showcase.ini out --sheet
```

`--sheet` also renders one tile per built-in preset, which is the fastest way
to check that a change to the drawing code did not quietly break a theme.

For the real thing, `LiteWidgets.exe --config examples\showcase.ini --settings`
starts with a specific config and opens the editor immediately.

`config/widgets.ini` is created on first run from
`config/widgets.example.ini` and is not tracked, so your own layout never
turns up in `git status`. Change the example when you mean to change what
ships.

## Adding a config key

Config lives in one place. To add a key:

1. Add the field to `WidgetSpec` or `WidgetStyle` (`src/spec.h`, `src/style.h`).
2. Add one case to `Spec_Set()` or `Style_Set()`.
3. Add a row to the property table in `src/spec.c`.
4. If the value is derived from another when unset, handle it in
   `Spec_Finalize()`.
5. Regenerate the reference:

```bat
bin\docgen.exe docs\CONFIGURATION.md
```

The settings editor builds itself from that table, so step 3 is all the UI
work there is. CI fails if `docs/CONFIGURATION.md` is out of date.

## Adding a widget type

Look at `src/widgets/clock.c` for the shape. A widget provides:

- a **stateless painter** — `Thing_Paint(spec, ..., gfx, w, h)` — that draws
  into any graphics context and touches no window state,
- a `WidgetVtable` with `render` / `on_timer` / `next_interval` / `destroy`,
- a `ThingWidget_Create(...)` that allocates, fills its spec, and calls
  `Widget_Init`.

Then register it in `WidgetType`, `kTypeNames`, `CreateWidget()` and
`Config_Paint()`. Keeping the painter separate from the window is what lets
the settings preview and the offscreen renderer show exactly what the desktop
will.

If the widget needs to update on a schedule, implement `next_interval` and
return the time until the next moment its output would actually change.

## Style

Match what is already there:

- 4 spaces, no tabs, ~95 column soft limit.
- `PascalCase` for functions, prefixed by their module (`Drawing_Text`).
- `snake_case` for struct fields, `g_` for file-scope statics.
- Comments explain *why*. The code already says what.
- Check every allocation and every GDI+ status you depend on; free on every
  path, including the failure ones.

## Commits and pull requests

- One logical change per commit, with a message that says what changed and
  why. `feat(clock): ...`, `fix(drawing): ...`, `docs: ...`.
- Rebase rather than merge; the history is meant to stay readable.
- Include a screenshot or a rendered PNG for anything visual.
- Say what you tested on: Windows version, monitor count, display scaling, and
  whether any wallpaper software was running.

## Things worth reporting

Desktop integration is the fragile part of this project. Bug reports that
mention your wallpaper software, DPI scaling, and monitor layout are far more
useful than ones that do not — most of the hard bugs live exactly there.
