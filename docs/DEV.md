# Developing Dotfront

Build commands and the platform support matrix are in the
[README](../README.md#build-and-platform-support); startup flags are in the
[command-line options](../README.md#command-line-options) table.

## Code layout

```
src/common.h   tuning constants, Vec2 helpers, Unit/City types, unit table, terrain enums
src/world.h/c  dynamic map sizing/styles, noise terrain, city placement, BFS router
src/game.h/c   match setup, factions, capture/production/fortification, territory, save/load, orders
src/units.c    movement, orders, separation, supply, combat, stall recovery
src/ai.h/c     fog-aware opponent: scout, remember, defend, mass waves, attack
src/render.h/c terrain pre-render, fog, territory tint/borders, units, cities, paths, selection
src/hud.c      top bar, minimap, help, selection/production info, menu, pause/win overlays
src/gfx.h/c    framebuffer primitives: AA discs, thick lines, triangles, text
src/font.h/c   5x7 bitmap font
src/platform.h      window/presentation/event/time interface shared by all builds
src/platform_x11.c  X11 backend: Xlib window + double-buffered MIT-SHM (XImage fallback)
src/platform_win32.c Win32 backend: GDI window + top-down DIB, no external runtime
src/platform_cocoa.m native macOS backend: NSWindow/NSView, Cocoa events, CoreGraphics image
src/platform_sdl.c   optional SDL2 backend (Linux `make sdl`, macOS `make macos-sdl`)
src/main.c          camera, event handling, fixed-step loop, headless mode
```

Simulation is fixed at 60 Hz (`SIM_DT`), display syncs to the same rate. All match tuning lives
in `src/common.h` (terrain modifiers, speeds, damage, capture time, production, supply,
territory radius). Runtime map storage is sized for 2400×1350; each match uses a 16:9 region
between 1600×900 and that limit, based on the player count. Rendering runs at 1280×720 —
change `WIN_W`/`WIN_H` in `src/platform.h`.

The renderer only needs a pixel buffer, so new platforms implement the small
`src/platform.h` interface rather than touching the game or HUD.

## Headless mode

```sh
./dotfront --headless 180 --seed 3 --players 4
./dotfront --headless 240 --seed 3 --players 6 --difficulty hard
```

Runs every faction as AI for up to the given simulated seconds, printing map style, silhouette and size,
city positions, periodic per-faction army summaries, and the result. `--difficulty` applies to
all sides in headless mode. This is how the simulation is smoke-tested and tuned; 6-faction
wars with one faction reduced to a last city can take several minutes to finish. Headless mode
is offline only; it cannot be combined with `--host` or `--join`.
