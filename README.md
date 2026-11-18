# Dotfront — C edition

[![build](https://github.com/Azizfois2/dotfront/actions/workflows/build.yml/badge.svg)](https://github.com/Azizfois2/dotfront/actions/workflows/build.yml)

An unofficial fan-made C RTS inspired by the Python [War of Dots](https://github.com/gamepycoder/War-of-dots-)
and [warofdots.net](https://warofdots.net/). Dotfront is not affiliated with or endorsed by the
original project or its rights holders; “War of Dots” remains the property of its owner.

You command blue dots against 2-6 AI factions (blue, red, green, gold, violet, cyan), offline or
over a LAN, on one shared C game core.

No game engine, no assets, no image libraries: the game draws into a 32-bit framebuffer and a
small platform layer presents it. Linux uses Xlib/MIT-SHM, Windows uses GDI, macOS uses native Cocoa,
and the game core is shared. Build dependencies are a C compiler, `make`, and the native
platform SDK/headers.

![screenshot](screenshot.png)

## Download

Prebuilt **Linux**, **Windows**, **macOS (universal)** and **Android** packages are attached to
the [latest release](https://github.com/Azizfois2/dotfront/releases/latest). Android is a
landscape APK for arm64 and armeabi-v7a devices; no Play Store account is needed. See
[CHANGELOG.md](CHANGELOG.md) for what changed in each tag.

## Features

- 2-6 factions with distinct colours, and a mouse-driven start menu with opponent arrows,
  difficulty buttons, continue, new map, sound, help and quit
- Clickable pause, winner, elimination and tutorial overlays with hover feedback
- Android touch port: tap to select, long-press to move, two fingers to pan and pinch-zoom,
  plus on-screen city production and fortify buttons
- LAN play: an authoritative host, up to five TCP clients, client orders, and AI takeover of
  every empty or disconnected faction slot
- Procedurally generated maps in four styles (island, archipelago, lakes, highlands) and five
  large-scale silhouettes (continent, twin isles, broken atoll, fjords, island chain,
  isthmus, crescent, rift), with warped
  cliffs, soft shallow-water coastlines and wavy sea borders instead of one central square
  blob; the map grows
  with the player count (roughly five cities and 400k px² per faction, up to 2060×1160)
- Territory grid derived from city influence: ground is faintly faction-tinted, borders are drawn
  on the cell grid, and standing on friendly/enemy soil modifies supply healing
- City production queues (up to five queued units), control groups, minimap click-to-jump and
  right-click orders, a two-column in-game tutorial, and faction-camera presets while spectating
- Five terrain types with real gameplay effects: plains, forest, hill, water, impassable mountain
- Selection (click, shift-add, box select), hand-drawn movement paths, and formation offsets
- Staged path drawing: hold Shift and right-drag to append route segments, then `SPACE` to send
  the whole route or `C` to clear it
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
- Native window/taskbar/Dock icon (embedded, no runtime files needed) and a macOS `.app` bundle
- Cities: capture by presence/majority, unit production, rally points for new units
- Supply system: units heal near friendly cities, starve far away or behind enemy lines
- Fog-respecting AI: it scouts unknown ground, remembers city owners, and defends only attacks
  it can actually see; it builds mixed armies and recovers units stuck in traffic jams
- Three AI difficulty levels, selectable from the command line or with `1` / `2` / `3`
- Camera pan/zoom, edge scrolling, minimap (with a marker on your starting city), pause,
  restart, start menu, win screen, and a fog-free spectator mode after elimination
- Headless AI-vs-AI mode for testing, balance work, and regression checks

## Build

```sh
cd c_rts
make            # Linux/X11, or native macOS/Cocoa
make windows    # Windows .exe (cross-compile with mingw-w64)
make sdl        # optional SDL2 build on Linux -> dotfront_sdl
make app        # macOS: "Dotfront.app" bundle with Finder icon
make android    # Android APK (requires Android SDK + NDK 27.2)
```

Requirements:

- Linux: `gcc` (or clang), `make`, X11 headers
  (Debian/Ubuntu: `sudo apt install build-essential libx11-dev libxext-dev`).
- Windows: `make windows` from Linux with `x86_64-w64-mingw32-gcc` and
  `x86_64-w64-mingw32-windres` installed (`sudo apt install mingw-w64`). On Windows itself, use MSYS2/MinGW and
  `make windows MINGW=gcc`. No SDL or other runtime DLLs are needed.
- macOS: Xcode command-line tools only, then plain `make` (or `make app` for the
  bundle). The window and input use AppKit/Cocoa, with no SDL or Homebrew dependency. If the native backend
  gives you trouble, `brew install sdl2 && make sdl` builds the SDL2 fallback
  as `dotfront_sdl`.

The Windows executable is `dotfront.exe`; everything else is identical. Source artwork lives in
`assets/icon.png` with generated `.ico`/`.icns` variants, and is embedded into each backend.

### Android

The Android port uses the same SDL2 backend and C game core, wrapped in a small Gradle project:

```sh
cd c_rts
ANDROID_HOME=$HOME/android-sdk make android
# APK: android/app/build/outputs/apk/release/app-release.apk
```

It needs JDK 17+, Android SDK platform 35/build-tools 35 and NDK `27.2.12479018`. The first build
downloads SDL2 2.32.10 (`android/build-apk.sh`), then Gradle downloads its own dependencies.
The release workflow builds and signs an installable APK automatically.

Touch controls: **tap** selects units/cities and presses buttons, **long-press** gives a move
order (drag after the long press to draw a path), one-finger drag box-selects, and **two fingers**
pan and pinch-zoom. City production and fortify are the four on-screen buttons above the status strip.

## Play

```sh
./dotfront                 # Linux
dotfront.exe               # Windows
./dotfront --seed 42       # reproducible map
./dotfront --players 6     # 1 human + 5 AI factions (2-6 total)
./dotfront --difficulty hard
./dotfront --spectate      # watch an all-AI match with no fog of war
./dotfront --host 47777 --players 4 --seed 12   # LAN host
./dotfront --join 192.168.1.20:47777            # LAN client
```

The start menu shows the seed, city count, map style and landmass silhouette. The camera starts
centred on your capital and is deliberately zoomed in enough that you have to scroll vertically
and horizontally to see the whole map. Before starting, use `LEFT` / `RIGHT`
to choose the number of opponents (1-5), shown as a faction-dot row. Press `SPACE` to start,
`1` / `2` / `3` to pick the AI difficulty, `F9` to continue a saved match, `R` for a new map,
or `Q` to quit.

### LAN play

1. On one machine start a host: `./dotfront --host 47777 --players 4 --seed 12`.
   The window title shows `[HOST]` and the lobby badge shows the port and connected clients.
2. On each other machine run the same build: `./dotfront --join 192.168.1.20:47777`
   (replace the address with the host's LAN IP; append `:PORT` only if not 47777).
   Clients are assigned the next free faction and see the host's map in the lobby.
3. The host presses `SPACE` to start. Any faction slot without a client is controlled by the AI,
   and a client that leaves mid-match is immediately taken over by the AI.
4. Clients send orders (move, attack-move, hold, stop, production, rally, fortify) to the host,
   which simulates the match and broadcasts authoritative snapshots about ten times per second.
   Client cameras, selection, control groups, tutorial, sound and fog view stay local.

The host controls pause, restart, map/player count and saving. `F9` load is disabled in LAN
matches. A client joining after the match has started is rejected. The host listens on TCP port
47777 (or `--port N` / the optional port after `--host`), so allow that port through the host
firewall. This is LAN play, not NAT traversal; both ends must run the same game version.

The start menu, pause screen, winner/elimination screens and tutorial are all clickable. Hover a
button to highlight it; the keyboard shortcuts below still work everywhere.

| Input | Action |
| --- | --- |
| Left click | Click menu/overlay buttons; select a unit, shift-click to add, click a city |
| Left drag | Box-select your units |
| Right drag | Draw a path for the selection (or a rally path for a selected city) |
| `SHIFT` + right drag | Append a stage to the staged route; markers show each stage |
| `SPACE` with a staged route | Send all staged waypoints as one route |
| `C` | Clear the staged route / path preview |
| `Z` | Select all your units |
| `G` | Attack-move the selection to the cursor (stops to fight) |
| `H` | Hold position (selected units stop and do not chase) |
| `X` | Stop / cancel current orders |
| `1` `2` `3` | With a city selected: produce scout / soldier / heavy. In the offline menu: switch AI difficulty |
| City buttons | Click SCOUT / SOLDIER / HEAVY / FORTIFY above the status strip (touch and mouse) |
| `SHIFT` + `1` `2` `3` | Queue another scout / soldier / heavy at the selected city (max 5) |
| `CTRL` + `1`–`6` | Store the current selection as a control group |
| `1`–`6` | Recall that group when no city is selected |
| Minimap left click | Jump the camera to that part of the map |
| Minimap right click | Send selected units there, or set the selected city's rally point |
| `F` | Fortify the selected city (10 s, pauses its production) |
| `F5` / `F9` | Save the match / load `wod_save.bin` |
| `M` | Toggle sound |
| `A` `D` / `W` `S`, arrows | Pan the camera |
| Middle drag | Pan the camera |
| Move cursor to a window edge | Scroll the camera in that direction |
| Mouse wheel | Zoom around the cursor |
| `SPACE` | Pause / resume (or send staged orders when a route is staged) |
| `T` | Open / close the tutorial and controls screen |
| `ESC` / Android Back | Close help, clear selection, or leave the menu |
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
  minimap keep showing the last owner you scouted; each rival HUD entry counts only the cities
  and currently visible dots you have scouted. Every AI plays under the same fog: it must scout
  to find cities and can only react to attacks it can see.
- **Territory**: every owned city projects a 230 px influence radius. The strongest nearby city
  owns each grid cell, so overlapping borders resolve by distance. Explored territory is tinted
  with the owner colour and gets a drawn border along cell edges. Units on their own territory
  gain +1.2 HP/s supply; units on enemy territory lose 1.0 HP/s.
- **Units**: scouts are fast and see far but are fragile; soldiers are the balanced line unit;
  heavies are slow, tough, and hit hard. Each city's production choice is independent, and
  build time scales with the role.
- **Combat** is automatic: every dot attacks the closest enemy within range, once per frame,
  scaled by the terrain it stands on. Orders only control movement and target approach.
- **Cities** flip after about 2 seconds when one side has more units inside the capture radius
  (a full uncontested capture is faster than a contested one; fortification slows it by 32 %). Ownership resets the city's
  production timer. Units inside an owned city heal; far from any owned city they lose health.
- **Production**: each owned city spawns its chosen role every
  `(7 + 0.22 × army_size) × role_build_time` seconds. The per-player cap scales with the match:
  60 dots for 2 players, down to roughly 30 each in a 6-faction war. New dots follow the city's
  rally path if one was drawn.
- **Fortification** costs 10 s of production. A fortified city flips 32 % slower, sees farther,
  and is drawn with a gold segmented ring. It cannot be fortified twice.
- **Save/load** stores the complete deterministic match state in `wod_save.bin`; the menu only
  offers Continue when that file is present.
- **Win**: a faction with no cities and no units is eliminated; the last faction standing wins.
  The match timer stops on the win screen, which lists every faction's captures/built/lost/peak.

## AI difficulty

Every AI faction uses the same profile, chosen with `--difficulty easy|normal|hard` or switched
live with `1`, `2`, `3`:

- **Easy** decides every 1.8 s, defends with two units over a short radius, and attacks with
  larger but less frequent waves.
- **Normal** is the original opponent: a decision every 0.75 s, four defenders, waves of
  `3 + cities`.
- **Hard** reacts every 0.45 s, defends with six units over a wider radius, and commits up to
  ten units per wave. All profiles send limited waves (not the whole army at once) and recover
  units from stalled paths, which keeps long matches from freezing at a chokepoint. Once a
  faction owns most of the map, is at its unit cap, or has an enemy down to its last known city,
  it sends every idle dot instead of trickling waves, so endgames actually close out.

## If you are eliminated

When your last city and unit are gone but other AI factions are still fighting, the game pauses
and offers three choices:

| Key | Action |
| --- | --- |
| `R` | Start a fresh match with a new random map |
| `SPACE` | Keep watching the surviving factions with **no fog of war**; `Q` quits at any time |
| `Q` | Quit immediately |

While spectating, the whole map is visible, the HUD shows every faction's live city/army counts,
and the normal pan/zoom controls still work. Press `1`–`6` to jump the camera to a surviving
faction's capital.

## Headless mode

```sh
./dotfront --headless 180 --seed 3 --players 4
./dotfront --headless 240 --seed 3 --players 6 --difficulty hard
```

Runs every faction as AI for up to the given simulated seconds, printing map style, silhouette and size,
city positions, periodic per-faction army summaries, and the result. `--difficulty` applies to
all sides in headless mode. This is how the simulation is smoke-tested and tuned; 6-faction
wars with one faction reduced to a last city can take several minutes to finish.

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
