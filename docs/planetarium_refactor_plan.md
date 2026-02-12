# Planetarium Refactor Plan

## Target Architecture

- Make planetarium fully data-driven: render logic consumes a `planetary_system_def` object, never hardcoded strings in `render.c`.
- Keep all lore/name text as `static const` string literals in headers, grouped per system.
- Put reusable types and interfaces in one core header, and each system in its own header file.

## Proposed File Layout

- `src/planetarium/planetarium_types.h`: shared structs/enums (`planet_def`, `planet_lore_block`, `planetary_system_def`).
- `src/planetarium/planetarium_registry.h`: `planetarium_get_system_count()`, `planetarium_get_system(int idx)`.
- `src/planetarium/planetarium_registry.c`: array of pointers to all system defs.
- `src/planetarium/systems/system_solace.h`: one complete system (names, lore, contracts, orbit params, boss text).
- `src/planetarium/systems/system_<name>.h`: one file per future system.
- `src/planetarium/planetarium_text_sanitize.h`: optional helper for uppercase/safe glyph policy.

## Data Model

- `planet_def`: display name, orbit lane index, color/style hints, unlock state defaults.
- `planet_lore_block`: `contract_title`, `status_pending`, `status_quelled`, `briefing_lines[3]`.
- `planetary_system_def`: system display name, commander strings, array of `planet_def`, boss-gate copy, propaganda marquee source pointer.
- All string fields are `const char*` backed by compile-time string constants.

## Rendering/Logic Split

- `render.c` gets current `planetary_system_def*` from app state and only draws from data fields.
- Selection logic in `main.c` uses counts/IDs from system definition, not hardcoded `PLANETARIUM_NODE_COUNT`.
- Teletype messages are generated from `contract_title` in selected planet data (no inline format strings except numeric fallback).

## Scalability for Multiple Systems

- Add `current_system_index` to app state.
- On mission progression, switch to next `planetary_system_def` from registry.
- Keep quelled/completion state in runtime arrays keyed by `[system_index][planet_index]`.

## String Policy

- Keep almost all text as header-defined constants.
- Build dynamic strings only where needed (`SYSTEM %02d` style), and always copy into persistent buffers before typewriter use.
- Enforce uppercase-safe glyph set in content headers to avoid `?` rendering.

## Recommended Implementation Order

1. Introduce `planetarium_types.h` + registry API.
2. Move current hardcoded planet names/lore from `render.c` into `system_solace.h`.
3. Refactor renderer to consume `planetary_system_def`.
4. Refactor selection/teletype/state code in `main.c` to use system data.
5. Add second system header as proof of reuse.
6. Add lightweight validation helper that checks null pointers/count bounds at startup.
