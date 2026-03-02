# Underwater Enemy Plan

## Goal

Add underwater-themed enemies that are visually richer than the current diamond glyph while keeping:

- One enemy render path (`draw_enemy_glyph(...)`) as source of truth.
- Existing collision and gameplay behavior unchanged for first pass.
- Stable performance at `MAX_ENEMIES` with current post/CRT stack.

The primary target is a 2D jellyfish with convincing pulsation and tentacle flow.

## Current Integration Points

Use these existing systems instead of creating parallel ones:

- Runtime enemy data in `src/game.h` (`enemy` struct).
- Spawning and wave selection in `src/enemy.c`.
- Enemy drawing in `src/render.c` (`draw_enemy_glyph(...)`).
- Curated marker kinds in `src/leveldef.c` and editor marker labels in `src/render.c`.

## Entity Proposals

### 1) Jellyfish Swarm (Priority)

Role:

- Slow drift with soft vertical phase motion.
- Mid threat via spread or burst fire.
- High readability silhouette with biolum pulse.

Visual model:

- Bell: dome polyline (top half ellipse + soft lower skirt).
- Tentacles: 6-14 short segmented trails from bell rim.
- Optional inner arcs: 2-3 animated lines inside bell.

Movement flavor:

- Reuse existing swarm AI (`ENEMY_ARCH_SWARM`) to avoid new steering system.
- Add mild bobbing in render only for first pass.

### 2) Manta Glider

Role:

- Formation or boid variant with broad wings.
- Medium speed lateral sweeps.

Visual model:

- Kite-like center with left/right wing curves.
- Wing tips flap in opposite phase.

### 3) Anemone Turret (Static-ish Ambush)

Role:

- Slow or stationary curated hazard in choke points.

Visual model:

- Circular core + short radial tendrils.
- Pre-fire contraction animation before burst shot.

### 4) Eel Striker

Role:

- Fast strike enemy using kamikaze state machine.

Visual model:

- Spine chain with traveling sine wave.
- Tail brightness ramps during strike state.

## Jellyfish: Concrete Math

Use per-enemy phase and seed:

- `phase = g->t * pulse_freq + pulse_phase`
- `p = sinf(phase)`
- `pulse01 = 0.5f + 0.5f * p`

Bell deformation:

- `scale_y = 1.0f + 0.18f * p`
- `scale_x = 1.0f - 0.10f * p`
- `skirt_drop = rr * (0.20f + 0.10f * pulse01)`

Render-space bob:

- `bob = rr * 0.20f * sinf(phase * 0.5f + seed * 2.3f)`
- Apply to drawn `y` only in Phase 1.

Tentacle lateral offset for segment `u` in `[0,1]`:

- `amp_u = rr * tentacle_amp * powf(u, 1.25f)`
- `offset = amp_u * sinf(g->t * wave_freq - u * wave_phase + tentacle_phase)`

Recommended defaults:

- `pulse_freq`: `1.6..2.4`
- `tentacle_amp`: `0.08..0.18`
- `wave_freq`: `2.2..3.4`
- `wave_phase`: `5.0..8.0`
- `tentacle_count`: `8..12`
- `segments_per_tentacle`: `4..6`

## Data Model Changes

Add a compact visual variant block to `enemy` in `src/game.h`:

- `int visual_kind;` (`0=default, 1=jelly, 2=manta, 3=anemone, 4=eel, 5=cuttlefish_mine`)
- `uint32_t visual_seed;`
- `float visual_phase;`
- `float visual_param_a;`
- `float visual_param_b;`

Notes:

- Keep this generic so all future variants reuse the same fields.
- Do not add separate per-variant structs.

## Spawn/Authoring Changes

### Curated kind extension

Introduce curated enemy kind `15` = `jelly_swarm`:

- `src/leveldef.c`: extend `curated_kind_from_name(...)`.
- `src/leveldef.h`: update `leveldef_curated_enemy.kind` comment.
- `src/render.c`: update `level_editor_marker_name(...)`, `editor_wave_type_name(...)`, and enemy-marker checks that currently include `2/3/4/5/10/11/12`.

Additional curated kinds:

- `16` = `manta_wing`
- `17` = `eel_striker`
- `18` = `cuttlefish_mine`

### Spawn mapping

In `src/enemy.c`, treat kind `15` as swarm-based spawn path:

- Include `15` in the boid/swarm curated condition.
- Set `e->visual_kind = 1` (jelly) during spawn.
- Initialize per-instance phase/params from deterministic hash of `wave_id` + `slot_index`.

Recommended initialization:

- `visual_seed = (uint32_t)(wave_id * 73856093u) ^ (uint32_t)(slot_index * 19349663u);`
- `visual_phase = hash01_u32(visual_seed) * 6.2831853f;`
- `visual_param_a = lerpf(1.6f, 2.4f, hash01_u32(visual_seed ^ 0xA53u));`
- `visual_param_b = lerpf(0.08f, 0.18f, hash01_u32(visual_seed ^ 0xB71u));`

Variant mapping for new kinds:

- Kind `16` (`manta_wing`): formation path (`sine` or `v`) with `visual_kind=2`.
- Kind `17` (`eel_striker`): kamikaze path with `visual_kind=4`.
- Kind `18` (`cuttlefish_mine`): minefield spawn path with mine visual marker/flag (see dedicated section below).

## Render Path Changes

Keep `draw_enemy_glyph(...)` as single dispatcher.

Add helpers in `src/render.c`:

- `draw_enemy_glyph_default(...)` (current diamond/tail logic moved as-is).
- `draw_enemy_glyph_jelly(...)`.
- `draw_enemy_glyph_manta(...)`.
- `draw_enemy_glyph_anemone(...)` (stub for later).
- `draw_enemy_glyph_eel(...)`.
- `draw_mine_glyph_cuttlefish(...)` (mine rendering path).

Dispatch:

- `switch (e->visual_kind)` inside `draw_enemy_glyph(...)`.
- Fall through to default glyph if unknown kind.

### Jelly renderer detail

1. Compute normalized facing and side vectors (`fx/fy`, `nx/ny`).
2. Build bell polyline (10-14 points) in local coordinates.
3. Apply pulse deformation (`scale_x/scale_y`) and bob.
4. Draw bell outer line with existing `enemy_style`.
5. Draw inner 2-3 arcs with reduced alpha/intensity.
6. Draw tentacles as short polylines with phase-lag wave.

Blend and readability:

- Keep same red family as enemy palette for now.
- Add one low-intensity additive pass only on bell rim.
- Clamp total jelly draw calls to avoid doubling frame cost.

## Manta Ray: Concrete Plan

AI/behavior:

- Reuse `ENEMY_ARCH_FORMATION`.
- Prefer `V` wave and wide spacing for "gliding school" look.
- Weapon profile: pulse or spread, lower fire cadence than jelly.

Shape math:

- Local wing span scales with `rr`.
- Left/right wing tips flap in opposite phase:
  - `flap = sinf(g->t * flap_freq + visual_phase)`
  - Left tip y offset: `+flap_amp * flap`
  - Right tip y offset: `-flap_amp * flap`
- Body uses a short center rhombus for readability.

Polyline layout (example points in local facing space):

- Nose `( +1.10*rr, 0 )`
- Left tip `( -0.20*rr, +1.45*rr + flap_delta )`
- Tail `( -1.05*rr, 0 )`
- Right tip `( -0.20*rr, -1.45*rr - flap_delta )`
- Back to nose

Recommended params:

- `flap_freq`: `1.2..1.9`
- `flap_amp`: `0.10..0.22 * rr`
- `visual_param_a = flap_freq`
- `visual_param_b = flap_amp_normalized`

Spawn mapping:

- Kind `16` should spawn as formation (`kind 3`-like defaults) with `visual_kind=2`.
- Keep collision radius unchanged in phase 1.

Performance:

- 1 main body polyline + optional 1 center spine line.
- No tentacle-like subsegments.
- Cost should be close to current default glyph.

## Electric Eel: Concrete Plan

AI/behavior:

- Reuse `ENEMY_ARCH_KAMIKAZE` state machine.
- Visual should communicate winding coil before thrust.
- During `THRUST/STRIKE`, amplify wave speed and brightness.

Spine-chain math:

- Build eel as `N` sample points (`N=7..11`) from head to tail.
- Along-body parameter `u = i/(N-1)`.
- Centerline length `L = rr * eel_len_scale`.
- Traveling wave:
  - `wave = sinf(g->t * wave_freq + visual_phase - u * wave_phase)`
  - lateral offset `lat = wave_amp * (0.2 + 0.8*u) * wave`
- Head anchored at enemy position/facing.

State modulation:

- Coil (`ENEMY_STATE_KAMIKAZE_COIL`):
  - lower `L`, higher curvature (`wave_amp` up).
- Thrust/Strike:
  - increase `L` by `20-35%`.
  - increase `wave_freq` by `30-50%`.
  - render a short additive "charge trace" near head.

Recommended params:

- `eel_len_scale`: `2.2..3.0`
- `wave_amp`: `0.10..0.22 * rr`
- `wave_freq`: `3.0..5.0`
- `wave_phase`: `6.0..10.0`

Spawn mapping:

- Kind `17` routes through kamikaze spawn with `visual_kind=4`.
- Keep `kamikaze_tail` behavior; eel line replaces diamond body while retaining tail as optional additive flare.

Performance:

- One polyline with `7..11` points + optional short head flare line.
- Much cheaper than jelly.

## Cuttlefish Mine: Concrete Plan

Role:

- Underwater mine variant with strong telegraph before detonation.
- Integrates with existing `mine` gameplay/collision path (do not create a second mine entity system).

Data extension (mine runtime in `src/game.h`):

- Add `int visual_kind;` to `mine` (`0=default,1=cuttlefish`).
- Add `float pulse_phase;` for deterministic telegraph animation.

Spawn/authoring:

- Add curated kind `18` to place cuttlefish mine clusters.
- In spawn handling, reuse existing minefield placement logic and set `mine.visual_kind=1`.
- For level config compatibility, default all old mines to `visual_kind=0`.

Shape and animation:

- Core body: rounded diamond/dome hybrid polyline.
- Fins: two short side arcs with slight phase offset.
- Arm filaments: 4-6 very short trailing lines.
- Pulsing telegraph:
  - `pulse = 0.5 + 0.5*sinf(g->t * pulse_freq + pulse_phase)`
  - increase line width/intensity and additive halo as pulse approaches 1.

Detonation cue:

- Last `0.35s` before trigger: fast pulse multiplier and tighter inner ring.
- This is visual-only; explosion timing remains gameplay-owned.

Recommended params:

- `pulse_freq`: `1.0..1.8` idle, up to `4.5` in pre-detonation cue.
- `fin_amp`: `0.05..0.10 * rr`.
- `filament_len`: `0.25..0.55 * rr`.

Performance:

- Mine body 2-3 polylines + small additive halo.
- Filaments reduced to 2 lines at low LOD.
- Respect existing mine count caps.

## GPU Trick Budget (Low Risk)

Phase 1 (no shader changes):

- CPU-generated vector lines only.
- One optional additive rim pass.

Phase 2 (optional, if needed):

- Tiny fullscreen underwater shimmer already exists for background.
- Add local screen-space distortion around jelly only if perf headroom remains.

Do not add a separate enemy rendering pipeline.

## Performance Budget and Guardrails

Target budget per visible jelly:

- Bell + inner arcs: `3-4` polylines.
- Tentacles: `8` tentacles x `1` polyline each.
- Total: around `11-12` polyline draws/jelly at full LOD.

Adaptive LOD:

- Far or low-depth enemies: reduce tentacle count to `4-6`.
- Skip inner arcs when tiny on screen.
- On cylinder back-half where alpha is already low, force minimal LOD.

Hard guards:

- Global jelly tentacle segment cap per frame.
- If cap exceeded, draw bell only for lowest-priority instances.

Cross-variant budgets:

- Manta: `<=2` polylines each.
- Eel: `<=2` polylines each, `N<=11` points.
- Cuttlefish mine: `<=5` polylines each at full LOD.

## Implementation Sequence

1. Add `visual_kind/seed/params` fields in `src/game.h`.
2. Initialize defaults in enemy spawn common path in `src/enemy.c`.
3. Add curated kinds `15/16/17/18` parsing + editor labels.
4. Map variants:
 - `15` jelly swarm (`visual_kind=1`)
 - `16` manta wing (`visual_kind=2`)
 - `17` eel striker (`visual_kind=4`)
 - `18` cuttlefish mine (`mine.visual_kind=1`)
5. Refactor `draw_enemy_glyph(...)` into dispatcher + default helper.
6. Implement jelly, manta, and eel glyph functions.
7. Add cuttlefish mine glyph in mine render path.
8. Add LOD clamps and per-frame cap metric.
9. Build and profile in underwater levels.

## Validation Checklist

- Build passes: `cmake --build build -j4`.
- Existing enemy kinds (2/3/4/5/10/11/12) render unchanged.
- Kinds `15/16/17/18` appear in editor, save/reload, and spawn correctly.
- Jellyfish remain readable against underwater caustics.
- Manta wing flap remains readable at low sizes.
- Eel strike state transitions remain visually distinct.
- Cuttlefish mine detonation telegraph is obvious without hiding bullets.
- Frame pacing remains stable in worst-case swarm count.

## Suggested Config Samples

For curated underwater test levels:

- `curated_enemy=jelly_swarm,0.70,0.45,10,0,0`
- `curated_enemy=jelly_swarm,1.20,0.60,14,0,0`
- `curated_enemy=jelly_swarm,1.80,0.35,8,0,0`
- `curated_enemy=manta_wing,0.90,0.52,6,0,0`
- `curated_enemy=eel_striker,1.30,0.40,5,0,0`
- `curated_enemy=cuttlefish_mine,1.60,0.50,9,0,0`

`a` controls count. `b` and `c` remain available for future speed/accel overrides.
