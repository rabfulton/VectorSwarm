# Underwater Background Plan

## Goal

Add a new level background style with an underwater atmosphere that:

- Fits the current retro/vector visual language.
- Is data-driven via level config (`background=underwater`).
- Supports per-level tuning from the level editor.
- Avoids per-level hardcoded behavior.

## Scope

This plan covers:

- New background render path.
- Runtime tunables and defaults.
- LevelDef/level-editor integration.
- Performance constraints and update culling.

This plan does not include:

- New enemy AI.
- Collision changes.
- Audio synthesis details beyond optional hooks.

## Visual Design Targets

The underwater background should combine:

1. Caustic light field:
- Moving light bands with low-frequency warping.
- Subtle brightness modulation, not full-screen flicker.

2. Bubble field:
- Multiple bubble emitters producing small/medium bubbles.
- Randomized spawn cadence per emitter.
- Mild lateral drift/wobble while rising.

3. Kelp silhouettes:
- 2-3 parallax layers.
- Slow sinusoidal sway.
- Dimmer than gameplay entities.

4. Particulate haze:
- Fine drifting particles.
- Depth/opacity fade to avoid visual clutter.

5. Optional overlays:
- Vent-like plume zones or bio-lum nodes as future add-ons.

## Data Model

### LevelDef additions

Add underwater settings to `leveldef_level`:

- `underwater_enabled` (derived from `background=underwater`).
- `underwater_density` (global scalar).
- `underwater_caustic_strength`.
- `underwater_caustic_scale`.
- `underwater_bubble_rate`.
- `underwater_haze_alpha`.
- `underwater_current_speed`.
- `underwater_palette_shift`.

Defaults should live in LevelDef defaults path, not in ad-hoc runtime fallbacks.

### Config keys

In level files:

- `background=underwater`
- `underwater.density=...`
- `underwater.caustic_strength=...`
- `underwater.caustic_scale=...`
- `underwater.bubble_rate=...`
- `underwater.haze_alpha=...`
- `underwater.current_speed=...`
- `underwater.palette_shift=...`

Unknown/missing keys should resolve through standard LevelDef defaulting.

## Rendering Architecture

## Pass order (gameplay)

Use this order for underwater levels:

1. Base clear.
2. Underwater background (caustics + haze + kelp + bubbles).
3. Gameplay world entities.
4. Post/CRT pass.

Background must never overdraw gameplay entities.

## GPU strategy

Implement underwater as a dedicated shader path (single fullscreen draw + instanced bubbles):

- Fullscreen frag for caustics/haze.
- Optional instanced bubble draw for crisp vector-like bubble rings.
- Kelp can be generated in shader (distance fields/noise bands) or via prebuilt line strips.

Avoid CPU-heavy per-bubble draw loops if bubble count grows.

## World anchoring

All underwater motion/noise sampling should be world-anchored:

- Camera movement should fly through the field.
- No screen-locked artifacts.

Use camera position in shader uniforms similarly to existing fog/grid anchoring.

## Level Editor Integration

### Level properties

When `background=underwater`, show editable underwater fields in level properties:

- Density
- Caustic strength
- Caustic scale
- Bubble rate
- Haze alpha
- Current speed
- Palette shift

Use sensible step sizes per field (coarse/fine with modifier keys).

### Optional placed emitters (Phase 2)

After base underwater mode is stable, add spatial emitter markers:

- Bubble emitter marker kind.
- Position + rate + size range + drift.

These should behave like other spatial editor markers:

- Visible in preview.
- Selectable/editable.
- Saved in level config.

## Performance Plan

Targets:

- No significant FPS regression relative to current non-underwater backgrounds.
- Stable frame pacing under heavy gameplay.

Controls:

- Cap bubble instance count.
- Frustum/screen cull bubble instances.
- Fixed-cost fullscreen shading.
- Keep noise layers minimal and branch-light.

## Debug/Tuning Workflow

Follow existing `docs/debug.md` pattern:

- `VTYPE_UNDERWATER_TUNING=1`
- Numpad adjustments for major underwater parameters.
- `KP Enter` dumps current values for hardcoding.
- `KP*` toggles tuning HUD.
- `KP.` resets defaults.

Keep tuning gated by env var and active only on underwater background levels.

## Implementation Stages

### Stage 1: Core underwater background

1. Add LevelDef fields + parser/serializer support.
2. Add editor property exposure.
3. Add shader path for caustics/haze.
4. Add basic bubble instances.
5. Wire render order and world anchoring.

Exit criteria:

- `background=underwater` renders consistently.
- Parameters save/load correctly.
- Editor and runtime show same values.

### Stage 2: Kelp + depth polish

1. Add parallax kelp silhouettes.
2. Add depth-driven attenuation.
3. Tune color/palette behavior with theme compatibility.

Exit criteria:

- Visual depth without gameplay readability loss.

### Stage 3: Advanced emitters and polish

1. Add spatial bubble emitters (editor markers).
2. Optional biolum pulse hooks.
3. Add debug tuning env path/docs updates.

Exit criteria:

- Authorable underwater levels with reproducible look.

## Risks and Mitigations

1. Overbright/washed visuals:
- Clamp caustic intensity and haze alpha.
- Maintain contrast budget for bullets/enemies/UI.

2. Performance spikes:
- Hard cap bubble instances.
- Prefer instancing and fullscreen shader math over many CPU draw calls.

3. State drift between editor/runtime:
- Keep LevelDef file as single source of truth.
- Ensure save/reload path applies same data to editor and gameplay.

4. Excess visual noise:
- Default to restrained values.
- Use tuning env mode for aggressive experimentation only.

## Recommended First Test Level

Start with `level_blank.cfg` using:

- `background=underwater`
- No terrain overlays
- Sparse geometry

This isolates background behavior before mixing with complex level effects.
