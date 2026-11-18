# Playing Dotfront

A short setup is in the [README quick start](../README.md#quick-start). This document collects the
full controls, rules and mode details.

## Controls

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

## Touch controls (Android)

**Tap** selects units/cities and presses buttons, **long-press** gives a move order (drag after the
long press to draw a path), one-finger drag box-selects, and **two fingers** pan and pinch-zoom.
City production and fortify are the four on-screen buttons above the status strip.
