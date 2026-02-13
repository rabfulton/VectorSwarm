# AGENTS.md

This file captures project-specific quirks for future coding agents working in `v-type`.

## Quick Start

- Configure/build:
  - `cmake -S . -B build`
  - `cmake --build build -j4`
- Main binary: `build/VectorSwarm`
- There is no formal test suite in this repo right now. A successful build is the minimum verification.

## High-Risk Areas (Read Before Editing)

### 1. Planetarium geometry is duplicated in two files

`planetarium_node_center(...)` exists in:
- `src/render.c`
- `src/main.c`

These must stay in sync. If you change boss/node placement in one file only, rendering and mouse hit-testing drift apart.

### 2. Planetarium text comes from multiple pipelines

The left panel area mixes:
- Teletype line (`metrics->teletype_text`) drawn in `render.c`
- Additional status/title text drawn separately in `render.c`

It is easy to accidentally duplicate system names. Check both pipelines when changing labels.

### 3. Right-panel briefing uses custom wrap/layout glue

Long-form briefing text is rendered through `draw_wrapped_text_block_down(...)` in `src/render.c`.

Important details:
- It uses `vg_text_layout_build(...)` and `vg_measure_text_wrapped(...)`.
- It performs project-local word-aware normalization to avoid mid-word breaks and leading-space indents after wraps/newlines.
- It clips to available vertical space (`top_y`/`bottom_y`) by design.

If text appears truncated, first inspect panel geometry and spacing constants before blaming source strings.

### 4. Coordinate system is screen-space with inverted mouse Y

The renderer uses scene-space where Y increases upward. Mouse input is converted in `map_mouse_to_scene_coords(...)` (`src/main.c`) and includes Y inversion and optional CRT barrel correction.

Do not compare raw SDL mouse Y directly against render-space coordinates.

### 5. Marquee source can be accidentally overridden

The scrolling propaganda/news marquee should use:
- `k_planetarium_propaganda_marquee` from `src/planetarium_propaganda.h`

It is set via `sync_planetarium_marquee(...)` in `src/main.c`. Avoid replacing it with short per-system strings unless explicitly requested.

## Planetarium Data Model Notes

Files:
- `src/planetarium/planetarium_types.h`
- `src/planetarium/planetarium_registry.[hc]`
- `src/planetarium/planetarium_validate.[hc]`
- `src/planetarium/systems/system_*.h`
- `src/planetarium/commander_nick_dialogues.h`

Quirks:
- System/planet definitions are static compile-time initializers.
- Do **not** use function calls in global initializers for system headers (C constant initializer rules).
- `commander_message_id` is used instead of storing function-derived pointers in initializers.
- Registry validation runs at startup; if you add fields/content, update validation logic too.

## Text Rendering Constraints

The built-in vector font is uppercase-biased and compact. For readability:
- Keep long UI copy uppercase unless there is a reason not to.
- Tune `size_px`, spacing, and panel bounds together; changing one often requires changing all three.

## Build-System Gotcha

New `.c` files are **not** auto-discovered.
- Add them explicitly in `CMakeLists.txt` under `add_executable(v_type ...)`.

## External Library Rule

- Do **not** edit `DefconDraw/` in this repository as part of normal project work.
- If a fix or feature is needed in the library, stop and ask the user to make/approve the upstream/library-side change.
- Keep project-side adaptations in `src/` unless the user explicitly instructs otherwise.

## Editing Checklist for Planetarium UI Changes

1. If moving nodes/boss gate, update both `src/render.c` and `src/main.c` geometry helpers.
2. Re-check left panel for duplicate labels (teletype + status/title).
3. Re-check right panel for overlap/truncation at 1280x720.
4. Rebuild with `cmake --build build -j4`.
5. If content changed, ensure `planetarium_validate_registry(...)` still passes logically.

## Non-Obvious Current Behavior

- Boss gate label is currently `BOSS GATE`.
- Left pane top line for normal systems comes from teletype/system selection; status is a separate line.
- Right pane title currently reads `MISSION BRIEFING FROM COMMANDER NICK`.

If you intentionally change these, update this file so future agents do not “fix” them back.
