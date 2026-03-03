# Insect Enemy Plan

## Goal

Add insect-inspired enemies that feel mathematically alive and shader-friendly while keeping:

- One enemy render path (`draw_enemy_glyph(...)`) as source of truth.
- Existing collision and gameplay behavior unchanged for first pass.
- Stable performance at `MAX_ENEMIES` with current post/CRT stack.

The primary target is a 2D wasp interceptor with readable wingbeat, abdomen flex, and a clear strike telegraph.

## Current Integration Points

Use these existing systems instead of creating parallel ones:

- Runtime enemy data in `src/game.h` (`enemy` struct).
- Spawning and wave selection in `src/enemy.c`.
- Enemy drawing in `src/render.c` (`draw_enemy_glyph(...)`).
- Curated marker kinds in `src/leveldef.c` and editor marker labels in `src/render.c`.

## Entity Proposals

### 1) Wasp Interceptor (Priority)

Role:

- Fast pursuit and lateral feints.
- Mid/high pressure with burst stinger shots.
- Strong silhouette from triangular thorax + high-frequency wings.

Visual model:

- Body split into head, thorax, abdomen poly segments.
- Two wing blades with opposite phase flapping.
- Short trailing stinger line that brightens on attack wind-up.

Movement flavor:

- Reuse `ENEMY_ARCH_KAMIKAZE` for aggressive closing behavior.
- Keep gameplay logic unchanged; wingbeat/abdomen flex is render-only first pass.

### 2) Firefly Drifter

Role:

- Soft-moving ranged annoyance that forms loose packs.
- Light pulses act as tactical distraction and telegraph.

Visual model:

- Compact capsule body with pulsing halo.
- Small dotted wing traces at pulse peaks.

### 3) Beetle Bulwark

Role:

- Heavier formation unit with slower cadence and higher durability profile.
- Occupies lanes and forces repositioning.

Visual model:

- Hard shell carapace with segmented elytra lines.
- Minimal wing motion; emphasis on armored geometry.

### 4) Locust Ripper

Role:

- Swarm pressure unit with rapid direction snaps.
- Useful for dense wave moments without boss complexity.

Visual model:

- Lean body with long rear-leg zig lines.
- Jump-like squash/stretch read in render-space phase.

## Wasp: Concrete Math

Use per-enemy phase and seed:

- `phase = g->t * wing_freq + visual_phase`
- `w = sinf(phase)`
- `wing01 = 0.5f + 0.5f * w`

Body deformation:

- `abdomen_bend = rr * 0.16f * sinf(phase * 0.5f + seed * 1.9f)`
- `thorax_scale = 1.0f + 0.05f * wing01`
- `head_nod = rr * 0.05f * sinf(phase * 0.75f)`

Wing motion (per side `s` in `{-1,+1}`):

- `wing_angle = base_angle * s + flap_amp * s * w`
- `wing_span = rr * (1.15f + 0.10f * wing01)`
- Use 3-point wing polyline anchored at thorax edge.

Stinger charge cue:

- `charge = saturate((state_t - charge_start) / charge_window)`
- `stinger_len = rr * (0.28f + 0.22f * charge)`
- `stinger_glow = charge * charge`

Recommended defaults:

- `wing_freq`: `9.0..13.0`
- `flap_amp`: `0.30..0.55` radians
- `base_angle`: `0.55..0.80` radians
- `abdomen_bend_scale`: `0.10..0.20 * rr`
- `visual_param_a = wing_freq`
- `visual_param_b = flap_amp`

## Data Model Changes

Add a compact variant block to `enemy` in `src/game.h`:

- `int visual_kind;` (`0=default, 6=wasp, 7=firefly, 8=beetle, 9=locust`)
- `uint32_t visual_seed;`
- `float visual_phase;`
- `float visual_param_a;`
- `float visual_param_b;`

Notes:

- Reuse this same generic block for all new variants.
- Do not add per-variant structs.

## Spawn/Authoring Changes

### Curated kind extension

Introduce curated enemy kinds:

- `19` = `wasp_interceptor`
- `20` = `firefly_drifter`
- `21` = `beetle_bulwark`
- `22` = `locust_ripper`

Touch points:

- `src/leveldef.c`: extend `curated_kind_from_name(...)`.
- `src/leveldef.h`: update `leveldef_curated_enemy.kind` comment.
- `src/render.c`: update `level_editor_marker_name(...)`, `editor_wave_type_name(...)`, and enemy-marker checks.

### Spawn mapping

In `src/enemy.c`:

- Kind `19` maps to kamikaze-style spawn defaults with `visual_kind=6`.
- Kind `20` maps to swarm/boid-style path with `visual_kind=7`.
- Kind `21` maps to formation path with lower speed defaults and `visual_kind=8`.
- Kind `22` maps to swarm path with higher turn response and `visual_kind=9`.

Deterministic init pattern:

- `visual_seed = (uint32_t)(wave_id * 73856093u) ^ (uint32_t)(slot_index * 19349663u);`
- `visual_phase = hash01_u32(visual_seed) * 6.2831853f;`
- `visual_param_a = lerpf(a_min, a_max, hash01_u32(visual_seed ^ 0xA53u));`
- `visual_param_b = lerpf(b_min, b_max, hash01_u32(visual_seed ^ 0xB71u));`

## Render Path Changes

Keep `draw_enemy_glyph(...)` as single dispatcher.

Add helpers in `src/render.c`:

- `draw_enemy_glyph_default(...)` (current logic, unchanged).
- `draw_enemy_glyph_wasp(...)`.
- `draw_enemy_glyph_firefly(...)`.
- `draw_enemy_glyph_beetle(...)`.
- `draw_enemy_glyph_locust(...)`.

Dispatch via:

- `switch (e->visual_kind)` inside `draw_enemy_glyph(...)`.
- Fall through to default glyph for unknown kind.

### Wasp renderer detail

1. Build local basis from facing (`fx/fy`) and side (`nx/ny`).
2. Draw segmented body chain (head-thorax-abdomen) with slight flex.
3. Draw two wing polylines with high-frequency opposite-phase flap.
4. Draw stinger line with state-based extension/glow.
5. Add a tiny additive pass only on wing tips and stinger in charge state.

Readability:

- Keep enemy red family for hostility consistency.
- Use slightly brighter wing/stinger accents, not a full palette shift.
- Clamp additive intensity under CRT to avoid bloom smearing.

## Firefly: Concrete Plan

AI/behavior:

- Reuse `ENEMY_ARCH_SWARM`.
- Prefer moderate speed and loose cohesion.

Shape math:

- Core radius `r = rr * (0.65f + 0.10f * sinf(phase*0.7f))`
- Glow pulse `glow = powf(0.5f + 0.5f*sinf(g->t*pulse_freq + visual_phase), 2.2f)`
- Halo radius `halo_r = r * (1.3f + 0.9f * glow)`

Rendering:

- One body polyline + one halo ring polyline.
- At `glow > 0.75`, draw 2 short wing dashes.

Recommended params:

- `pulse_freq`: `1.4..2.8`
- `halo_intensity`: `0.20..0.55`

## Beetle: Concrete Plan

AI/behavior:

- Reuse `ENEMY_ARCH_FORMATION`.
- Slower advance, steadier lane pressure.

Shape math:

- Carapace as mirrored arc shell.
- Elytra split line: center seam from head to tail.
- Leg hints: 3 short side ticks per side with tiny phase jitter.

Recommended params:

- `shell_width`: `1.1..1.4 * rr`
- `shell_height`: `0.8..1.1 * rr`
- `jitter_freq`: `2.0..3.5`

Performance:

- 2-3 body polylines + optional leg ticks at close range only.

## Locust: Concrete Plan

AI/behavior:

- Reuse `ENEMY_ARCH_SWARM`.
- Higher direction volatility than firefly.

Shape math:

- Body centerline with 5-7 points.
- Rear-leg motif: two long angled segments.
- Hop illusion (render-only):
  - `hop = max(0, sinf(g->t*hop_freq + visual_phase))`
  - `scale_x = 1.0f + 0.18f*hop`
  - `scale_y = 1.0f - 0.10f*hop`

Recommended params:

- `hop_freq`: `2.5..4.0`
- `leg_len`: `0.9..1.4 * rr`

## GPU Trick Budget (Low Risk)

Phase 1 (no new enemy-specific shader programs):

- CPU-generated vector lines only.
- Optional additive accents on wing tips/halos.

Phase 2 (optional):

- Reuse existing fullscreen post stack.
- Add tiny procedural wing shimmer in fragment stage only if profiling allows.

Do not add a separate enemy rendering pipeline.

## Performance Budget and Guardrails

Target max polyline cost per visible enemy:

- Wasp: `6-8` polylines.
- Firefly: `2-4` polylines.
- Beetle: `3-5` polylines.
- Locust: `4-6` polylines.

Adaptive LOD:

- Suppress wing/leg detail when on back-half cylinder or tiny on screen.
- Skip additive accents below size threshold.
- Enforce per-frame accent cap (stingers/halos) to stabilize frame pacing.

Hard guards:

- Per-frame point-count cap for insect variant extras.
- If cap exceeded, draw core body only for lowest-priority instances.

## Implementation Sequence

1. Add visual variant fields in `src/game.h` (if not already present from underwater work).
2. Extend curated kind parsing and editor names for `19/20/21/22`.
3. Map spawn logic in `src/enemy.c` with deterministic per-instance params.
4. Refactor `draw_enemy_glyph(...)` into dispatcher + variant helpers.
5. Implement wasp, firefly, beetle, and locust glyph functions.
6. Add LOD and per-frame cap guards.
7. Build and verify readability in mixed-wave scenarios.

## Validation Checklist

- Build passes: `cmake --build build -j4`.
- Existing enemy kinds render unchanged.
- Kinds `19/20/21/22` appear in editor, save/reload, and spawn correctly.
- Wasp wingbeat remains readable at gameplay speed.
- Firefly glow telegraphs are visible but not blinding under CRT.
- Beetle silhouette remains distinct from default diamond glyph.
- Locust hop motion reads as aggressive without misleading hitbox.
- Frame pacing remains stable in worst-case swarm scenes.

## Suggested Config Samples

- `curated_enemy=wasp_interceptor,0.80,0.46,8,0,0`
- `curated_enemy=wasp_interceptor,1.25,0.40,10,0,0`
- `curated_enemy=firefly_drifter,0.95,0.60,12,0,0`
- `curated_enemy=beetle_bulwark,1.45,0.50,6,0,0`
- `curated_enemy=locust_ripper,1.70,0.33,14,0,0`

`a` controls count. `b` and `c` remain available for future speed/accel overrides.
