# v-type

Prototype side-scrolling vector shooter in C using DefconDraw.

## Inspirations

- Defender
- R-Type
- Darius

## Current Features

- Side-scrolling movement and enemy flow
- Physics-based movement with velocity + acceleration integration
- Ship heading flips with horizontal thrust direction (supports reverse flight)
- Modular side-view ship silhouette with bolt-on gun pods at higher weapon levels
- Vector-drawn player ship, enemies, and projectiles
- Starfield and lane guides for motion readability
- Elastic-band camera follow (spring + damping) with look-ahead
- Multi-layer parallax depth: background starfield + foreground vector landscape bands
- Demo-style vector meters for combat state
- Top telemetry meters (`VITALITY`, `QUOTA`) via `vg_ui_meter_linear`
- Native Vulkan render path (no CPU debug raster fallback in game loop)

Weapon upgrades auto-unlock by score:
- `WPN 1`: center cannon
- `WPN 2`: dual pod cannons
- `WPN 3`: triple-shot (center + dual pods)

## Source Layout

- `src/main.c`: SDL + frame loop + DefconDraw frame lifecycle
- `src/game.c` / `src/game.h`: gameplay state, spawning, physics, collisions
- `src/render.c` / `src/render.h`: vector rendering + HUD/meters
- Ship rendering in `src/render.c` is componentized (`hull`, `canopy`, `hardpoints`, `pod`, `thruster`) for fast iteration

## Controls

- `W/A/S/D` or arrow keys: move
- `Space` or `Left Ctrl`: fire
- `R`: restart after game over
- `TAB`: toggle CRT debug panel
- `UP/DOWN`: select CRT parameter (while panel open)
- `LEFT/RIGHT`: adjust selected CRT parameter
- `Esc`: quit

## Build

```sh
cmake -S . -B build
cmake --build build -j
```

## Run

```sh
./build/v_type
```
