# Bosses (Kitbash + Phases) Implementation Plan

This document specifies how to add Darius/R-Type inspired bosses to `v-type` while keeping the existing visual language (vector strokes + CRT shader) and reusing the current combat/render pipelines.

Primary constraint (project rule): do not introduce parallel rendering/combat systems when existing ones can be extended cleanly. Bosses should reuse:
- `enemy` slots (`src/game.h`) for hit/hp/death flow
- existing bullet/missile/arc systems (`src/game.h`, `src/enemy.c`, `src/game.c`)
- existing enemy glyph render path (`src/render.c`)
- existing particles/debris/explosion path (`src/enemy.c`)
- existing LevelDef discovery + strict validation (`src/leveldef.c`, `docs/leveldef_schema.md`)

## Goals

- Bosses are visually stunning relative to current enemies, but still read as "same universe":
  - same stroke palette + halo/core layering
  - same CRT persistence/bloom model
  - same motion vocabulary (organic sine, mechanical jitter, electrical arcs)
- Boss fights are mechanically readable:
  - clear telegraphs
  - destructible parts that change patterns
  - deliberate “final destruction” sequence (not instant pop)
- Boss content is data-driven and reproducible:
  - spawned through `data/levels/level_*.cfg` (curated mode)
  - generation is deterministic given seed + blueprint
  - strict validation; no hidden fallbacks

## Current Baseline (for consistency)

- Enemy rendering: glyph switch in `src/render.c` (`draw_enemy_glyph(...)` around `ENEMY_VISUAL_*`).
- Damage model: circle hit + integer HP; bullet collision decrements `hp`, emits `enemy_debris` + `emit_explosion` on death (`src/enemy.c`).
- Strong existing VFX to reuse:
  - lightning arcs: `eel_arc_effect` update + render (`src/enemy.c`, `src/render.c`)
  - homing missiles: `game_spawn_enemy_missile(...)`
  - particles/explosions: `emit_explosion(...)`
- Level spawning: curated entries trigger by camera X (`src/game.c` curated path).

## Boss Representation (Unified With Existing Systems)

Boss = 1 controller + N parts, where parts are *real* `enemy` slots.

- Controller:
  - owns phase state machine, pattern scheduling, anchor motion, and scripted destruction.
  - never directly collides with player bullets (optional), or can be a large “hull” collider depending on design.
- Parts:
  - are `enemy` entries, so they reuse:
    - bullet collision + HP + EMP interactions
    - render path (new visuals are still glyphs)
    - death pipeline (debris + explosion + score hooks)
  - are attached to controller via local transform. Each frame their world position is overwritten from controller.

Why this shape:
- Avoids building a second hitbox system.
- Avoids building a second renderable object graph.
- Keeps performance characteristics consistent with the engine's existing loops (just more enemies/lines during boss).

### Attachment Metadata (recommended shape)

Do not bloat `struct enemy` with many boss-only fields unless needed.

Use a sidecar array in `game_state` or `enemy` module keyed by enemy index:
- `owner_index` (controller enemy slot index, or `-1`)
- `local_x`, `local_y` (offset in boss-local space)
- `local_rot` (optional; angle for part orientation)
- `role` (turret/armor/joint/core/decor)
- `hp_max` (for phase-based visuals, optional)
- `flags` (invulnerable, decorative-only, phase-mask)

This keeps the base `enemy` struct stable and the boss-specific state localized.

## LevelDef Integration (Strict, Data-Driven)

Bosses should be spawned via curated level entries (`wave_mode=curated`) in `data/levels/level_*.cfg`.

Add a new curated kind (example):
- `kind=20`: `boss_controller`

Curated CSV fields are already `kind,x01,y01,a,b,c[,d,e]` (see `docs/leveldef_schema.md` and `src/leveldef.h`).

Proposed boss field mapping:
- `kind`: `20` (boss controller)
- `x01`: boss spawn X (in screens, same unit as exit_x01)
- `y01`: boss anchor Y (0..1)
- `a`: `boss_id` (integer; selects boss blueprint definition)
- `b`: `seed` (integer-ish float; converted to u32 in loader/spawner)
- `c`: `difficulty_scale` (float, default 1.0; validated to sane range)
- `d` (optional): `variant_id` (float->int; chooses kitbash variant)

Hard rules:
- Loader must validate boss entries strictly (unknown boss_id fails the level load).
- Do not add fallback behavior (no "if missing, spawn manta"). Fail fast with diagnostics.

### Level Editor Integration

Bosses are curated content, so they should be placeable in the level editor as spatial markers when `wave_mode=curated`.

Implementation intent (editor -> leveldef):
- Add new marker kinds:
  - `LEVEL_EDITOR_MARKER_MIDBOSS` (mid-sized boss controller)
  - `LEVEL_EDITOR_MARKER_BOSS` (full boss controller)
- These serialize to `curated_enemy=...` rows exactly like existing curated wave markers (single source of truth).
- The marker property panel should expose the boss fields as named properties, not anonymous `a,b,c`:
  - `BOSS ID` (int)
  - `SEED` (int)
  - `DIFFICULTY` (float)
  - `VARIANT` (int)
  - `GATES EXIT` (bool; see Exit Portal gating below)

Notes:
- The existing editor already treats curated spatial markers as the source of truth for `curated_enemy` rows and enforces that spatial enemy markers imply `wave_mode=curated`. Boss markers should follow the same rule.

### Spawn Gating

Boss fights should control pacing:
- While a live boss controller exists, suppress normal wave spawning.
- Gate exit portal activation until boss death (if level uses an exit portal).

This should be implemented by checking for active boss controllers in the wave spawner and portal logic (single source of truth).

### Exit Portal Upgrade: Requires Boss Defeated

Upgrade exit portals so levels can require a boss to be defeated before the player can exit.

Recommended level key:
- `exit_requires_boss_defeated` int (`0`/`1`)

Recommended behavior:
- If `exit_requires_boss_defeated=0`: existing behavior (portal works if player overlaps it).
- If `exit_requires_boss_defeated=1`: portal overlap only succeeds when all "exit-gating" bosses for the level are defeated.

"Exit-gating boss" definition (strict and explicit):
- Boss/midboss markers include `GATES EXIT` (bool) that serializes into one of the curated_enemy extra params.
- Runtime tracks “gating bosses remaining” as the count of alive boss controllers spawned from markers with `GATES EXIT=1`.

Validation guidance:
- If `exit_requires_boss_defeated=1` and the level defines no gating boss markers, treat that as invalid configuration (fail-fast) rather than silently making the portal always-available.

## Kitbash Generator Spec

Kitbash = assemble boss from reusable parts (“modules”) connected by sockets, with symmetry + clearance + budget constraints.

### Blueprint Data Model

Boss blueprint should be a small, deterministic struct (code-side table or data file later):
- `boss_id`
- `name` (for teletype/announce)
- `anchor_radius` (approx hull size for camera framing + collisions)
- `base_hp` (controller phase HP budget, if controller is damageable)
- `modules[]` (module library selection)
- `build_rules`:
  - symmetry mode (mirror by default)
  - max parts (hard cap)
  - clearance constraints (keep dodge lanes)
  - performance budgets (lines/parts)
- `phases[]`:
  - which parts are vulnerable
  - which attacks are enabled
  - trigger conditions (hp thresholds, part destroyed counts)

### Module Definition (example template)

Each module defines visuals + gameplay + attachment points:

Fields:
- `module_id`
- `role`: `CORE|ARMOR|JOINT|TURRET|VENT|DECOR`
- `visual_kind`: new `ENEMY_VISUAL_BOSS_*` kind (rendered via `draw_enemy_glyph(...)`)
- `radius`: hit radius (world units)
- `hp`: integer HP (scaled by `difficulty_scale`)
- `socket_count`
- sockets: `name`, `x`, `y`, `dir_x`, `dir_y`, `type`
  - socket `type`: `HARD` (structural) or `WEAPON` (fires from here)
- optional:
  - `invulnerable_mask` by phase
  - `weakpoint` (if true, increases damage taken or is required to progress)

### Example Module Library (initial set)

These are intended to be implemented as new glyphs in `src/render.c` alongside existing enemy glyphs. Keep the drawing style consistent:
- 2-4 layered strokes: core + halo + optional additive accent
- no filled meshes required; prefer polylines and small circles

1. `CORE_HEART`
- role: `CORE`
- visual: rotating iris + inner “heartbeat” lines (sin-modulated intensity)
- sockets:
  - `spine_top` (HARD) at (0, +R*0.9) dir (0, +1)
  - `spine_bottom` (HARD) at (0, -R*0.9) dir (0, -1)
  - `jaw_left` (HARD) at (-R*0.9, 0) dir (-1, 0)
  - `jaw_right` (HARD) at (+R*0.9, 0) dir (+1, 0)

2. `ARMOR_PLATE`
- role: `ARMOR`
- visual: angled chevrons + crack lines that appear as HP decreases
- sockets:
  - `next` (HARD) at (+R*0.9, 0) dir (+1, 0)

3. `JOINT_BALL`
- role: `JOINT`
- visual: ring + oscillating “servo” ticks
- sockets:
  - `left` (HARD) at (-R*0.9, 0) dir (-1, 0)
  - `right` (HARD) at (+R*0.9, 0) dir (+1, 0)
  - `top` (HARD) at (0, +R*0.9) dir (0, +1)
  - `bottom` (HARD) at (0, -R*0.9) dir (0, -1)

4. `TURRET_NEEDLE`
- role: `TURRET`
- visual: long needle barrel with recoil tick + glow tip
- sockets:
  - `mount` (HARD) at (0, 0) dir (-1, 0) (mount on hull)
  - `muzzle` (WEAPON) at (+R*1.1, 0) dir (+1, 0)

5. `TURRET_SWEEPER`
- role: `TURRET`
- visual: semicircle “dish” + scanning line (phase-based)
- sockets:
  - `muzzle0` (WEAPON) at (+R*0.9, +R*0.2)
  - `muzzle1` (WEAPON) at (+R*0.9, -R*0.2)

6. `VENT_ARC`
- role: `VENT`
- visual: vent grille + periodic electrical flare
- sockets:
  - `arc_source` (WEAPON) at (+R*0.6, 0) dir (+1, 0)
- behavior: spawns lightning arcs (reuse `eel_arc_effect` rendering pipeline) from arbitrary source points.

7. `DECOR_SPINE_SEG`
- role: `DECOR`
- visual: small rib segment with low alpha
- sockets:
  - `next` (HARD)
- behavior: no weapons; low hp or invulnerable depending on phase (used to build silhouettes).

### Connection Rules (how modules fit together)

The generator builds a graph by connecting compatible sockets:

Rules:
- `HARD` sockets connect to `HARD` sockets only.
- `WEAPON` sockets are not connected; they become weapon emitters.
- Prefer mirror symmetry:
  - when placing a module on left side, place a mirrored partner on right if blueprint demands symmetry
  - mirrored placement flips `local_x` and socket directions
- Clearance constraints:
  - reserve at least one horizontal dodge lane (Y band) that remains sparse
  - never place collision radii such that the boss blocks the full vertical span
- Budget constraints:
  - cap total parts (start: 18-28 parts)
  - cap total weapon emitters (start: 6-12)
  - cap "line density": new glyphs must be mindful of `vg_draw_polyline` count

Implementation hint:
- Generate a candidate graph, then run a prune pass:
  - remove decor branches first
  - then remove secondary turrets
  - never remove required weakpoints or joints

## Combat Design: Parts, Phases, and Telemetry

### Damage and Progression

Boss fights should progress by:
- destroying armor plates to expose weakpoints
- then destroying a subset of weakpoints to advance phases
- finally destroying the core to trigger scripted death

Avoid “boss HP sponge” feel by:
- making early phases about survival/positioning
- making later phases shorter but denser

### Phase Model (recommended)

3 phases are enough for a first boss:

Phase 1: "Approach / Pattern Teaching"
- Vulnerable: outer armor plates only
- Attacks: slow needle turrets + occasional missiles
- Telegraph: long warmups (flash/pulse on turret glyph)

Phase 2: "Expose / Control Space"
- Trigger: N armor plates destroyed OR time threshold
- Vulnerable: joints + vents
- Attacks: sweepers + lightning arcs (area denial)
- Telegraph: arcs pulse rhythmically; give “safe windows”

Phase 3: "Core / Final"
- Trigger: core exposed (all required plates destroyed)
- Vulnerable: core only
- Attacks: denser aimed fire + short arc bursts + add spawns (optional)
- Telegraph: core “heartbeat” accelerates; screen intensity spikes

### Attack Toolkit (reuse existing systems)

Do not add new projectile renderers unless absolutely required. Use:
- `enemy_bullets` for most patterns (multi-emitter fire)
- `game_spawn_enemy_missile` for salvos
- `eel_arc_effect` for beams/lightning-like patterns
- existing particle/explosion system for hit feedback

Patterns to implement first (agent-friendly):
- `AIMED_BURST`: 2-4 emitters on cooldown; slight aim error
- `SPREAD_FAN`: fixed angles from a turret (no aim)
- `SWEEP`: change emitter direction smoothly over time; fire small shots along sweep
- `ARC_BURST`: spawn 2-6 arcs with shared rhythm (reuse arc pulse logic)
- `MISSILE_SALVO`: 1-3 missiles from vents with charge-up window

## Final Destruction Sequence (Make It Epic)

Boss death should be a scripted sequence owned by the controller:
- disable new attacks
- sequentially “detonate” remaining parts:
  - emit heavy debris (extend existing debris emitter, but keep `enemy_debris` type)
  - emit explosions along the hull with rising intensity
  - spawn arcs that crawl across the silhouette (power surge)
- end with:
  - large final explosion + lingering particles
  - score award + announcer text
  - enable exit portal / mark level clear

Duration target: 3 to 6 seconds.

## Staged Implementation Plan (Agents)

This is intended to be implemented in small, reviewable steps. Each stage should build successfully (`cmake -S . -B build && cmake --build build -j4`).

### Stage 0: Ground Rules + Budgets
- Define performance budgets for bosses (parts, emitters, arcs, particles).
- Decide whether controller is damageable or parts-only.
- Add debug overlay lines/text to show:
  - boss phase
  - part count + active emitters
  - controller anchor + bounds

### Stage 1: New Enemy Archetypes (Controller + Part)
- Add `ENEMY_ARCH_BOSS_CONTROLLER` and `ENEMY_ARCH_BOSS_PART` behavior in enemy update loop (`src/enemy.c`).
- Implement attachment update: part position = controller position + rotated local offset.
- Ensure parts do not run formation/swarm/kamikaze logic.

Verification:
- Spawn a dummy boss (controller + 4 parts) from code and confirm parts track controller.

### Stage 2: LevelDef Spawn Hook (Curated Boss Entry)
- Extend `parse_curated_enemy` / validation to accept `kind=20` with strict field rules.
- Extend `enemy_spawn_curated_enemy(...)` to spawn a boss by `boss_id`.
- Add gating: suppress normal wave spawns while a boss controller is alive.

Verification:
- Add a boss entry to one curated level file and confirm it spawns at the right X and blocks additional waves.

### Stage 2b: Level Editor Boss Markers + Exit Requirement
- Add boss markers to the editor entity palette (midboss + boss).
- Add named boss properties to the property panel (boss_id/seed/difficulty/variant/gates_exit).
- Add `exit_requires_boss_defeated` to LevelDef schema and expose it as an exit-marker property.
- Runtime: prevent portal exit when exit requires boss and gating bosses remain.

Verification:
- In editor, place a boss marker and set `GATES EXIT=1`.
- Set portal `REQUIRES BOSS DEFEATED=1`.
- Confirm portal is visually present but does not transition until the boss is defeated.

### Stage 3: Kitbash Builder (Deterministic)
- Implement module library + blueprint structs.
- Implement graph build:
  - place `CORE_HEART`
  - add mirrored arms with `JOINT_BALL` + `ARMOR_PLATE`
  - attach turrets to available sockets
  - attach decor spine segments optionally
- Implement prune pass for constraints/budgets.
- Bake output into spawned parts (enemy slots + sidecar attachment metadata).

Verification:
- Same seed yields same part layout every run.
- Layout respects clearance (can dodge).

### Stage 4: Boss Visual Kinds (Impressive Glyphs)
- Add new `ENEMY_VISUAL_BOSS_*` values and implement glyphs in `src/render.c` following existing style:
  - core/halo layers
  - minor animated accents
  - hit feedback (intensity spike)

Verification:
- Glyphs read at gameplay distance and under CRT persistence.

### Stage 5: Attack Patterns + Telemetry
- Implement pattern scheduler on controller:
  - timed warmups
  - per-emitter cooldowns
  - phase-driven enable/disable
- Implement emitters:
  - bullets from `WEAPON` sockets
  - missiles from vents
  - arcs from arc sources

Verification:
- Telegraphs are visible and consistent.
- Attack density respects budgets.

### Stage 6: Damage, Phase Transitions, and Weakpoints
- Track destroyed parts and/or core exposure.
- Implement phase triggers and part vulnerability masks:
  - invulnerable armor until phase start
  - core invulnerable until plates destroyed
- Add phase-change cue:
  - short teletype message
  - glow spike on key modules

Verification:
- Player can clearly understand what is damageable now.

### Stage 7: Final Destruction Script
- Implement controller death -> scripted destruction timeline:
  - timed part detonations
  - escalating explosions/arcs
  - final blast
- Ensure this sequence is deterministic and finishes cleanly (no lingering invulnerable hazards).

Verification:
- Exit portal unlocks (if present), waves resume (if applicable), and level can complete.

### Stage 8: First Boss Content Pass
- Create Boss #1 blueprint and tune 3 phases.
- Add it to one curated level file with an announce message.
- Iterate on readability and “wow” factor:
  - silhouette first
  - telegraphs second
  - density third

## Implementation Checklist (Do Not Skip)

- Keep strict leveldef behavior: unknown boss ids fail fast; no fallbacks.
- Reuse existing systems:
  - `enemy` slots for parts
  - bullets/missiles/arcs for attacks
  - `enemy_debris` + particles for destruction
- Keep controller logic as single source of truth:
  - phase state
  - scheduling
  - gating wave spawns/exit unlock
- Rebuild after each stage.

## Open Design Choices (decide before Stage 1)

- Do bosses exist only in curated levels, or can normal waves trigger bosses?
- Is the controller damageable, or only parts (recommended: parts-only for clarity)?
- How many bosses ship in the first pass (recommended: 1 boss end-to-end before expanding the library)?

## Midbosses (Mid-Sized Bosses)

Midbosses are the same technical system as full bosses (controller + parts), just a smaller blueprint and smaller budgets.

### Where Midbosses Appear

1) Curated levels:
- Place `MIDBOSS` markers just like `BOSS` markers.
- Midbosses can be optionally marked as `GATES EXIT` or not.

2) Non-curated wave sequences:
- Add a new `wave_cycle` token (example): `midboss`.
- Add a small set of level keys to configure which midboss spawns in normal mode:
  - `midboss.id` (int)
  - `midboss.seed` (int)
  - `midboss.variant` (int)
  - `midboss.difficulty_scale` (float)
  - `midboss.gates_exit` (int 0/1)

This mirrors how `asteroid_storm` works today: a wave token triggers an event lane, and the detailed parameters live in level keys rather than being embedded in the token.

### Midboss Budgets (starting targets)

- Parts: 8 to 14
- Weapon emitters: 3 to 6
- Arcs: 0 to 6 (if used)
- Phases: 2 (teach -> core)
- Destruction: 2 to 4 seconds

