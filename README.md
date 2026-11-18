# Dotfront — C edition

[![C11](https://img.shields.io/badge/language-C11-00599C?logo=c&logoColor=white)](https://en.wikipedia.org/wiki/C11)
[![License: Apache-2.0](https://img.shields.io/badge/license-Apache--2.0-4b8bbe)](LICENSE)
[![Platforms: Linux, Windows, macOS, Android](https://img.shields.io/badge/platforms-Linux%20%7C%20Windows%20%7C%20macOS%20%7C%20Android-7d81c9)](#download)
[![Release: v1.2.0](https://img.shields.io/badge/release-v1.2.0-2ea44f)](https://github.com/Azizfois2/dotfront/releases/latest)
[![build](https://github.com/Azizfois2/dotfront/actions/workflows/build.yml/badge.svg)](https://github.com/Azizfois2/dotfront/actions/workflows/build.yml)

An unofficial fan-made C RTS inspired by the Python [War of Dots](https://github.com/gamepycoder/War-of-dots-)
and [warofdots.net](https://warofdots.net/). Dotfront is not affiliated with or endorsed by the
original project or its rights holders; “War of Dots” remains the property of its owner.

You are blue; the other 1–5 factions are AI- or LAN-client-controlled (red, green, gold, violet,
cyan), offline or over a LAN, on one shared C game core.

No game engine, no assets, no image libraries: the game draws into a 32-bit framebuffer and a
small platform layer presents it. Linux uses Xlib/MIT-SHM, Windows uses GDI, macOS uses native Cocoa,
and the game core is shared. Build dependencies are a C compiler, `make`, and the native
platform SDK/headers.

![Dotfront gameplay: blue dots and cities on a fogged, faction-tinted procedural map](screenshot.png)

## Contents

- [Download](#download)
- [Quick start](#quick-start)
- [Features](#features)
- [Build and platform support](#build-and-platform-support)
  - [Android](#android)
- [Play](#play)
  - [LAN play](#lan-play)
- [Command-line options](#command-line-options)
- [Development](#development)
- [Known limitations](#known-limitations)
- [License](#license)

## Download

Prebuilt **Linux**, **Windows**, **macOS (universal)** and **Android** packages are attached to
the [latest release](https://github.com/Azizfois2/dotfront/releases/latest). Android is a
landscape APK for arm64 and armeabi-v7a devices; no Play Store account is needed. See
[CHANGELOG.md](CHANGELOG.md) for what changed in each tag.

## Quick start

```sh
# Linux (Debian/Ubuntu)
sudo apt install build-essential libx11-dev libxext-dev
make && ./dotfront

# Windows: open the MSYS2 UCRT64 shell
pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-make
mingw32-make windows WINDRES=windres
./dotfront.exe

# macOS
xcode-select --install
make && ./dotfront
```

Prefer not to build? Grab a prebuilt package from the [latest release](https://github.com/Azizfois2/dotfront/releases/latest).
Android is built with `make android` — see [Android](#android).

## Features

### Playing

- 2–6 factions total; three unit roles (scout, soldier, heavy), independent per-city production
  choice with queues of up to five, rally points, and attack-move/hold/stop orders
- Cities capture by presence/majority, produce units, and can be fortified in 10 s to slow enemy
  capture and widen vision
- Five terrain types with real movement and damage effects: plains, forest, hill, water and
  impassable mountain
- Fog of war with explored-terrain memory, terrain-scaled vision (hills see far, forests short),
  hidden enemy units and city ownership, and a matching fogged minimap
- Territory influence tints ground and draws borders; friendly soil heals, enemy soil drains, and
  units starve far from cities or behind enemy lines
- Three AI difficulty levels; the fog-aware AI scouts unknown ground, remembers city owners,
  defends only visible attacks, builds mixed armies and recovers units stuck in traffic jams
- Mouse-driven start menu with opponent arrows, difficulty buttons, continue, new map, sound, help
  and quit; clickable pause, winner, elimination and tutorial overlays with hover feedback
- Save/load (`F5` / `F9`) with a versioned binary snapshot, a Continue option in the menu, and a
  match summary listing units built/lost, cities captured and peak army size

### Maps and controls

- Procedurally generated maps in four styles (island, archipelago, lakes, highlands) and eight
  large-scale silhouettes (continent, twin isles, atoll, fjords, island chain, isthmus, crescent,
  rift), with warped cliffs, soft shallow-water coastlines and wavy sea borders; the map grows with
  the player count (roughly five cities and 400k px² per faction, up to 2060×1160)
- Selection (click, shift-add, box select), hand-drawn movement paths with formation offsets, and
  staged routes (hold Shift and right-drag to append segments, `SPACE` to send, `C` to clear)
- Mountain-aware routing: clear path segments are kept exactly as drawn, blocked ones replaced by a
  BFS grid route so units never wedge themselves against a cliff
- Camera pan/zoom, edge scrolling, minimap with a marker on your starting city, minimap
  click-to-jump and right-click orders, and a two-column in-game tutorial

### Modes and platforms

- LAN play: an authoritative host, up to five TCP clients, client orders, and AI takeover of every
  empty or disconnected faction slot
- Fog-free spectator mode after elimination with faction-camera presets, plus a headless AI-vs-AI
  mode for testing, balance work and regression checks
- Android touch port: tap to select, long-press to move, two fingers to pan and pinch-zoom, plus
  on-screen city production and fortify buttons
- Native window/taskbar/Dock icon embedded with no runtime files, a macOS `.app` bundle, and minimal
  synthesized sound events (`M` toggles) with no audio assets

## Build and platform support

From the repository root:

```sh
make            # Linux/X11, or native macOS/Cocoa
make windows    # Windows .exe (cross-compile with mingw-w64)
make sdl        # optional SDL2 build on Linux -> dotfront_sdl
make app        # macOS: "Dotfront.app" bundle with Finder icon
make android    # Android APK (requires Android SDK + NDK 27.2)
```

| Platform | Presentation | Build command | Runtime dependencies | CI coverage |
| --- | --- | --- | --- | --- |
| Linux | X11 + MIT-SHM | `make` | libX11, libXext | build, headless, Xvfb launch, LAN |
| Linux | SDL2 | `make sdl` | SDL2 | build, headless |
| Windows | Win32 GDI | `mingw32-make windows WINDRES=windres` | none | cross-compile only (no Windows runner yet) |
| macOS | Cocoa | `make` / `make app` | none | build, launch, screenshot |
| Android | SDL2 | `make android` | bundled in the APK | debug APK |

Requirements:

- Linux: `gcc` (or clang), `make`, X11 headers
  (Debian/Ubuntu: `sudo apt install build-essential libx11-dev libxext-dev`).
- Windows (native): install the MSYS2 UCRT64 toolchain
  (`pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-make`), open the
  **MSYS2 UCRT64** shell, and from the repository root run
  `mingw32-make windows WINDRES=windres`. The `WINDRES` override is needed
  because MSYS2 ships `windres`, not the cross-prefixed
  `x86_64-w64-mingw32-windres` the Makefile defaults to. Run the build from the
  MSYS2 shell: the Makefile recipes use `rm -rf`, `uname` and shell conditionals,
  so `cmd.exe`/PowerShell will not work.
- Windows (cross-compile from Linux): `make windows` with
  `x86_64-w64-mingw32-gcc` and `x86_64-w64-mingw32-windres` installed
  (`sudo apt install mingw-w64`). No SDL or other runtime DLLs are needed.
- macOS: Xcode command-line tools only, then plain `make` (or `make app` for the
  bundle). The window and input use AppKit/Cocoa, with no SDL or Homebrew dependency. If the native backend
  gives you trouble, `brew install sdl2 && make sdl` builds the SDL2 fallback
  as `dotfront_sdl`.

The Windows executable is `dotfront.exe`; everything else is identical. Source artwork lives in
`assets/icon.png` with generated `.ico`/`.icns` variants, and is embedded into each backend.

### Android

The Android port uses the same SDL2 backend and C game core, wrapped in a small Gradle project:

```sh
ANDROID_HOME=$HOME/android-sdk make android
# APK: android/app/build/outputs/apk/release/app-release.apk
```

It needs JDK 17+, Android SDK platform 35/build-tools 35 and NDK `27.2.12479018`. The first build
downloads SDL2 2.32.10 (`android/build-apk.sh`), then Gradle downloads its own dependencies.
The release workflow builds and signs an installable APK automatically.

Touch controls are documented in [docs/PLAY.md](docs/PLAY.md#touch-controls-android).

## Play

```sh
./dotfront      # Linux and macOS
dotfront.exe    # Windows
```

The start menu shows the seed, city count, map style and landmass silhouette. The camera starts
centred on your capital and is deliberately zoomed in enough that you have to scroll vertically
and horizontally to see the whole map. Before starting, use `LEFT` / `RIGHT`
to choose the number of opponents (1-5), shown as a faction-dot row. Press `SPACE` to start,
`1` / `2` / `3` to pick the AI difficulty, `F9` to continue a saved match, `R` for a new map,
or `Q` to quit.

The full control list, rules, AI behaviour and elimination/spectator options are in
[docs/PLAY.md](docs/PLAY.md). See [Command-line options](#command-line-options) for startup flags.

### LAN play

1. On one machine start a host: `./dotfront --host 47777 --players 4 --seed 12`
   (`dotfront.exe` on Windows).
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

## Command-line options

| Option | Effect |
| --- | --- |
| `--seed N` | Reproducible map (also shown in the start menu). |
| `--players N` | Total factions, 2–6 (default 4). |
| `--difficulty easy\|normal\|hard` | AI profile for every AI faction. |
| `--host [PORT]` | Host a LAN match on a TCP port (default 47777); the title shows `[HOST]`. |
| `--join HOST[:PORT]` | Join a LAN host; the title shows `[CLIENT]`. |
| `--port N` | Default LAN port used by `--join` and `--host`. |
| `--headless N` | Run AI vs AI for up to N simulated seconds; offline only. |
| `--spectate` | Let the AI play blue, then watch with no fog of war; offline only. |
| `--version` | Print the version and exit. |
| `--help` | Print usage and exit. |

Examples (use `dotfront.exe` on Windows):

```sh
./dotfront --seed 42 --players 6 --difficulty hard
./dotfront --headless 180 --seed 3 --players 4
./dotfront --host 47777 --players 4 --seed 12
./dotfront --join 192.168.1.20:47777
```

## Development

Dotfront is plain C11 with a small platform interface: renderers only need a 32-bit framebuffer,
so new backends implement `src/platform.h` without touching the game or HUD. Simulation is fixed
at 60 Hz (`SIM_DT`) and display syncs to the same rate.

For the code layout, tuning constants, headless simulation and testing notes, see
[docs/DEV.md](docs/DEV.md).

## Known limitations

- Windows was verified natively with the MSYS2 UCRT64 toolchain (GCC 15.2.0):
  `mingw32-make windows WINDRES=windres` builds `dotfront.exe` without warnings,
  the window opens and a match plays, headless simulation runs, and a LAN
  host/client pair connects over TCP. It has not been played through a full
  human match on Windows yet.
- The macOS Cocoa build is compiled, launched and screenshotted on a real macOS
  runner by CI, but it has not been played on a Mac by a human yet.
- The AI is tuned by headless simulation, not by long human playtesting.
- Sound is limited to short platform beeps, not a music/audio system.

## License

Dotfront is released under the [Apache License 2.0](LICENSE). The introduction above carries the
fan-project disclaimer.
