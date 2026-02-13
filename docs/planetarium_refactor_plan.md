# Planetarium Refactor Plan

## Goal

Make Planetarium content fully data-driven so adding a new system requires content-only changes (new header + registry entry), not renderer or input logic edits.

## Current Coupling To Remove

- `src/render.c` currently embeds system names/lore (`k_system_names`, `k_system_lore`) and contract text logic.
- `src/main.c` currently uses fixed `PLANETARIUM_NODE_COUNT` and generates generic teletype copy (`SYSTEM %02d CONTRACT`).
- Orbit/node layout logic exists in both `render.c` and `main.c` and should operate from shared data.

## Target Architecture

- Planetarium UI and flow consume `planetary_system_def` data instead of hardcoded text tables.
- Reusable definitions and API stay in core files; each content system lives in one dedicated header.
- Runtime state (selection, quelled status, progression) is separated from static content.

## Proposed File Layout

- `src/planetarium/planetarium_types.h`
- `src/planetarium/planetarium_registry.h`
- `src/planetarium/planetarium_registry.c`
- `src/planetarium/planetarium_validate.c`
- `src/planetarium/systems/system_solace.h`
- `src/planetarium/systems/system_<name>.h`

## Data Model (v1)

- `planet_lore_block`
- Fields: `contract_title`, `status_pending`, `status_quelled`, `briefing_lines[3]`.

- `planet_def`
- Fields: `id`, `display_name`, `orbit_lane`, `lore`.

- `planetary_system_def`
- Fields: `id`, `display_name`, `commander_name`, `commander_callsign`, `planets`, `planet_count`, `boss_gate_label`, `boss_gate_locked_text`, `boss_gate_ready_text`, `marquee_text`.

- `planetary_registry`
- Accessors only: `planetarium_get_system_count()`, `planetarium_get_system(int idx)`.

Notes:
- All static content strings are `static const char*`.
- Runtime state never stores mutable pointers to temporary strings.

## Runtime State Changes

Update app state to support data-driven systems:

- Replace fixed node count assumptions with values from selected `planetary_system_def`.
- Add `current_system_index`.
- Track completion as `[system_index][planet_index]` (bounded by existing `PLANETARIUM_MAX_SYSTEMS` initially).
- Keep boss-node selection as `planet_count` sentinel index for current system.

## Rendering/Logic Contract

- `render.c` receives current system pointer, current selected index, and quelled status array for the current system.
- `render.c` no longer owns content strings.
- `main.c` owns selection/progression state transitions and teletype enqueue decisions.
- Teletype copy uses selected planet `contract_title` when available; numeric fallback is only for invalid data paths.

## Migration Map (Old -> New)

- `k_system_names` -> `planetary_system_def.planets[i].display_name`
- `k_system_lore` -> `planetary_system_def.planets[i].lore.*`
- `PLANETARIUM_NODE_COUNT` -> `planetary_system_def.planet_count`
- `"SYSTEM %02d CONTRACT"` -> `planetary_system_def.planets[selected].lore.contract_title`
- `k_planetarium_propaganda_marquee` default -> `planetary_system_def.marquee_text` (with fallback)

## Phased Implementation

1. Introduce types and registry
- Add `planetarium_types.h`, `planetarium_registry.h/.c`.
- Port current Solace content into `system_solace.h`.
- Acceptance: build succeeds with no behavior change.

2. Refactor render content sources
- Remove hardcoded name/lore tables from `render.c`.
- Read all labels/status/briefing text from current system data.
- Acceptance: Planetarium UI displays identical text for Solace.

3. Refactor input/progression logic
- Remove `PLANETARIUM_NODE_COUNT` dependency in `main.c`.
- Update selection wrapping and boss-gate checks to use `planet_count`.
- Teletype selection announcement reads contract title.
- Acceptance: keyboard and mouse selection still function; quell toggles still work.

4. Introduce validation
- Add startup validation for null pointers, zero counts, and count bounds.
- Log clear failures and fall back to minimal safe content when invalid.
- Acceptance: intentional bad test entry fails validation deterministically.

5. Prove extensibility
- Add `system_<name>.h` with at least 2 planets and distinct text.
- Register it after Solace.
- Acceptance: switching `current_system_index` changes all content without code edits in `main.c`/`render.c`.

## Validation Checklist

- Build with no warnings introduced by new planetarium files.
- Planet selection wraps correctly at both ends for any `planet_count >= 1`.
- Boss node unlock rule still respects "all planets quelled in current system".
- No out-of-bounds read/write when `planet_count < PLANETARIUM_MAX_SYSTEMS`.
- Marquee text has valid fallback when system field is null/empty.
- All lore strings render with supported glyphs (uppercase-safe policy).

## Risks and Mitigations

- Risk: duplicated node-center math diverges between `main.c` and `render.c`.
- Mitigation: move node-center helper to one shared function after refactor settles.

- Risk: static bounds (`PLANETARIUM_MAX_SYSTEMS`) may hide future scale issues.
- Mitigation: keep v1 bounded, then schedule dynamic allocation pass only when needed.

- Risk: invalid content data crashes UI.
- Mitigation: mandatory startup validation + defensive fallbacks in rendering paths.

## Out Of Scope (This Pass)

- Dynamic memory-backed content loading from external files.
- Localization or runtime string table loading.
- Reworking orbit visuals beyond what is needed to consume data-driven counts.
