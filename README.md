# War of Dots — offline C edition

[![build](https://github.com/Azizfois2/war-of-dots-c/actions/workflows/build.yml/badge.svg)](https://github.com/Azizfois2/war-of-dots-c/actions/workflows/build.yml)

A small single-player RTS in C, inspired by the Python [War of Dots](https://github.com/gamepycoder/War-of-dots-) in this repository.
You command blue dots, the computer commands red dots, and the whole match runs offline on one machine.

No game engine, no assets, no image libraries: the game draws into a 32-bit framebuffer and a
small platform layer presents it. Linux uses Xlib/MIT-SHM, Windows uses GDI, macOS uses native Cocoa,
and the game core is shared. Build dependencies are a C compiler, `make`, and the native
platform SDK/headers.

![screenshot](screenshot.png)

## Features

- Procedurally generated island every match (value-noise elevation + forest layer, sea border)
- Five terrain types with real gameplay effects: plains, forest, hill, water, impassable mountain
- Selection (click, shift-add, box select), hand-drawn movement paths, and formation offsets
- Fog of war with explored-terrain memory, terrain-scaled vision (hills see far, forests short),
  enemy units and city ownership hidden outside your sight, and a matching fogged minimap
- Mountain-aware routing: clear path segments are kept exactly as drawn, blocked ones are
  replaced by a BFS grid route so units never wedge themselves against a cliff
- Three unit roles (scout, soldier, heavy) and per-city production choice
- Orders: attack-move, hold position, stop; rally points for new units
- City fortification: spend 10 s to wall a city, slowing enemy capture and widening its vision
- Save/load (`F5` / `F9`) with a versioned binary snapshot, plus a start menu and continue option
- Match summary with units built/lost, cities captured, and peak army size
- Minimal synthesized sound events (`M` toggles), no audio assets
- Cities: capture by presence/majority, unit production, rally points for new units
- Supply system: units heal near friendly cities, starve far away or behind enemy lines
- Fog-respecting AI: it scouts unknown ground, remembers city owners, and defends only attacks
  it can actually see; it builds mixed armies and recovers units stuck in traffic jams
- Three AI difficulty levels, selectable from the command line or with `1` / `2` / `3`
- Camera pan/zoom, minimap, pause, restart, start menu, win screen
- Headless AI-vs-AI mode for testing, balance work, and regression checks

## Build

```sh
cd c_rts
make            # Linux/X11, or native macOS/Cocoa
make windows    # Windows .exe (cross-compile with mingw-w64)
make sdl        # optional SDL2 build on Linux -> wod_c_sdl
```

Requirements:

- Linux: `gcc` (or clang), `make`, X11 headers
  (Debian/Ubuntu: `sudo apt install build-essential libx11-dev libxext-dev`).
- Windows: `make windows` from Linux with `x86_64-w64-mingw32-gcc` installed
  (`sudo apt install mingw-w64`). On Windows itself, use MSYS2/MinGW and
  `make windows MINGW=gcc`. No SDL or other runtime DLLs are needed.
- macOS: Xcode command-line tools only, then plain `make`. The window and input
  use AppKit/Cocoa, with no SDL or Homebrew dependency. If the native backend
  gives you trouble, `brew install sdl2 && make sdl` builds the SDL2 fallback
  as `wod_c_sdl`.

The Windows executable is `wod_c.exe`; everything else is identical.

## Play

```sh
./wod_c                 # Linux
wod_c.exe               # Windows
./wod_c --seed 42       # reproducible map
./wod_c --difficulty hard
```

The start menu shows the seed and map size. Press `SPACE` to start, `1` / `2` / `3` to pick the AI difficulty, `F9` to continue a saved match, `R` for a new map, or `Q` to quit.

| Input | Action |
| --- | --- |
| Left click | Select a unit, shift-click to add, click a city to select it |
| Left drag | Box-select your units |
| Right drag | Draw a path for the selection (or a rally path for a selected city) |
| `Z` | Select all your units |
| `G` | Attack-move the selection to the cursor (stops to fight) |
| `H` | Hold position (selected units stop and do not chase) |
| `X` | Stop / cancel current orders |
| `1` `2` `3` | With a city selected: produce scout / soldier / heavy. Otherwise: switch the red AI difficulty |
| `F` | Fortify the selected city (10 s, pauses its production) |
| `F5` / `F9` | Save the match / load `wod_save.bin` |
| `M` | Toggle sound |
| `A` `D` / `W` `S`, arrows | Pan the camera |
| Middle drag | Pan the camera |
| Mouse wheel | Zoom around the cursor |
| `SPACE` | Pause / resume |
| `ESC` | Clear selection (or leave the menu) |
| `R` | Restart with a new random map |
| `Q` | Quit |

## Rules in one screen

- **Terrain**: plains 100 % speed / 100 % damage, forest 80 % / 75 %, hills 70 % / 150 %,
  water 60 % / 50 %. Mountains cannot be crossed.
- **Fog of war**: you only see what your dots and cities can see. Vision starts at the role
  radius (scout 190, soldier 105, heavy 90) and is scaled by the terrain a unit stands on
  (hills ×1.50, forest ×0.76, water ×0.86), plus 200 px around your cities and 270 px around
  fortified ones. Areas you have explored stay
  dimly visible as terrain memory, but enemy dots disappear into the fog. A city's flag and the
  minimap keep showing the last owner you scouted; the red HUD counts only scouted cities and
  currently visible red dots. The AI plays under the same fog: it must scout to find cities and
  can only react to attacks it can see.
- **Units**: scouts are fast and see far but are fragile; soldiers are the balanced line unit;
  heavies are slow, tough, and hit hard. Each city's production choice is independent, and
  build time scales with the role.
- **Combat** is automatic: every dot attacks the closest enemy within range, once per frame,
  scaled by the terrain it stands on. Orders only control movement and target approach.
- **Cities** flip after about 2 seconds when one side has more units inside the capture radius
  (a full uncontested capture is faster than a contested one; fortification slows it by 32 %). Ownership resets the city's
  production timer. Units inside an owned city heal; far from any owned city they lose health.
- **Production**: each owned city spawns its chosen role every
  `(7 + 0.22 × army_size) × role_build_time` seconds, up to 60 dots per player. New dots follow
  the city's rally path if one was drawn.
- **Fortification** costs 10 s of production. A fortified city flips 32 % slower, sees farther,
  and is drawn with a gold segmented ring. It cannot be fortified twice.
- **Save/load** stores the complete deterministic match state in `wod_save.bin`; the menu only
  offers Continue when that file is present.
- **Win**: a player with no cities and no units loses. The match timer stops on the win screen.

## AI difficulty

The red AI has three profiles, chosen with `--difficulty easy|normal|hard` or switched live
with `1`, `2`, `3`:

- **Easy** decides every 1.8 s, defends with two units over a short radius, and attacks with
  larger but less frequent waves.
- **Normal** is the original opponent: a decision every 0.75 s, four defenders, waves of
  `3 + cities`.
- **Hard** reacts every 0.45 s, defends with six units over a wider radius, and commits up to
  ten units per wave. All profiles send limited waves (not the whole army at once) and recover
  units from stalled paths, which keeps long matches from freezing at a chokepoint.

## Headless mode

```sh
./wod_c --headless 180 --seed 3
./wod_c --headless 180 --seed 3 --difficulty hard
```

Runs both sides as AI for up to 180 simulated seconds, printing map stats, city positions,
periodic army summaries, and the result. `--difficulty` applies to both sides in headless mode. This is how the simulation was smoke-tested
(`RED WINS ... blue 0 cities/0 units ...`) and is useful when tuning constants.

## Code layout

```
src/common.h   tuning constants, Vec2 helpers, Unit/City types, unit table, terrain enums
src/world.h/c  noise, island generation, terrain queries, BFS router
src/game.h/c   match setup, city capture/production/fortification, save/load, selection, orders
src/units.c    movement, orders, separation, supply, combat, stall recovery
src/ai.h/c     fog-aware opponent: scout, remember, defend, mass waves, attack
src/render.h/c terrain pre-render, terrain blit, units, cities, paths, selection box
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
in `src/common.h` (terrain modifiers, speeds, damage, capture time, production, supply).
Rendering runs at 1280×720 — change `WIN_W`/`WIN_H` in `src/platform.h`.

## Known limitations / next steps

- Windows support is compile-tested via mingw-w64 but was not run on a Windows
  machine in this environment.
- The macOS Cocoa build is compiled, launched and screenshotted on a real macOS
  runner by CI, but it has not been played on a Mac by a human yet.
- No network play.
- The AI is tuned by headless simulation, not by long human playtesting.
- Sound is limited to short platform beeps, not a music/audio system.
- The renderer only needs a pixel buffer, so new platforms implement the small
  `src/platform.h` interface rather than touching the game or HUD.
