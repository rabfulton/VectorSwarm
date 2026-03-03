# Fire Level Graphics Plan

## Goal

Design a fire-themed level that feels volatile and alive using math/shader techniques, while keeping:

- Current gameplay and collision systems unchanged in first pass.
- One coherent render path per effect class (terrain, hazards, atmosphere).
- Stable performance with existing post/CRT stack.

Primary mood target: volcanic battlefield with flowing magma channels, active flame jets, ember storms, and layered smoke.

## Visual Pillars

### 1) Magma Rivers

Role:

- Main world identity and navigation pressure.
- High contrast flow lines that suggest danger zones.

Look:

- Cracked crust with glowing seams.
- Brighter core lanes that pulse and drift.

### 2) Flame Columns and Ground Jets

Role:

- Local hazard telegraphs and arena punctuation.
- Rhythmic bursts that sync with combat pacing.

Look:

- Tapered plume shapes with noisy edges.
- Fast luminous core, softer outer tongues.

### 3) Smoke Canopy and Ash

Role:

- Atmospheric depth and silhouette framing.
- Soft obscuration without hiding bullets.

Look:

- Multi-layer scrolling smoke sheets.
- Fine ash particles with mild parallax.

### 4) Heat Distortion

Role:

- Sell temperature and motion even in static geometry.
- Local emphasis above magma and active jets.

Look:

- Refractive shimmer with vertical drift.
- Distortion strongest near hottest regions.

## Core Algorithms

## A) Magma Shader

Technique:

- Domain-warped FBM noise + crack mask + emissive ramp.

Math sketch:

- `uv0 = world_uv * magma_scale`
- `warp = vec2(fbm(uv0 + t*flow_a), fbm(uv0*1.7 - t*flow_b))`
- `uv1 = uv0 + warp_amp * (warp * 2.0 - 1.0)`
- `n = fbm(uv1)`
- `cracks = smoothstep(crack_lo, crack_hi, voronoi_edge(uv1 * crack_scale))`
- `lava = smoothstep(lava_lo, lava_hi, n) * (1.0 - cracks)`
- `pulse = 0.75 + 0.25 * sin(t * pulse_freq + n * pulse_phase)`
- `emissive = lava * pulse`

Color ramp:

- Dark crust: `#1B0C07`
- Mid lava: `#7A1E10`
- Hot lava: `#E5541A`
- White-hot accents: `#FFD38A`

Implementation notes:

- Keep it in existing terrain/world fragment pass if possible.
- Use 4-5 FBM octaves max.
- Quantize emissive slightly to avoid muddy gradients under CRT.

## B) Flame Plume Shader

Technique:

- Signed-distance flame body with noise-eroded contour.

Base SDF:

- Local coords `p` centered at jet origin.
- `body = length(vec2(p.x * width_scale, p.y)) - radius(p.y)`
- `radius(y) = mix(base_r, tip_r, saturate(y / plume_h))`

Edge breakup:

- `noise = fbm(vec2(p.x * edge_freq, p.y * rise_freq - t * rise_speed))`
- `dist = body - edge_amp * (noise - 0.5)`
- `alpha = 1.0 - smoothstep(-soft_in, soft_out, dist)`

Inner/outer temperature:

- `core = smoothstep(core_w, 0.0, abs(p.x)) * smoothstep(plume_h, 0.0, p.y)`
- `heat = clamp(core + noise * 0.2, 0.0, 1.0)`
- Color from black-red-orange-yellow ramp by `heat`.

Behavior modulation:

- Idle: lower `rise_speed`, lower `edge_amp`.
- Pre-burst: increase brightness and core height.
- Burst: increase plume_h and alpha for short window.

## C) Smoke Volume Illusion (2D Layer Stack)

Technique:

- Three scrolling alpha layers with different frequency and speed.

Per-layer:

- `s = fbm(uv * scale_i + wind_i * t)`
- `density_i = smoothstep(th_i, 1.0, s) * opacity_i`

Composite:

- Front-to-back alpha blend capped by visibility budget.
- Darken background slightly with smoke coverage.

Recommended layer settings:

- Layer 0 (far): low freq, slow drift, low alpha.
- Layer 1 (mid): medium freq, diagonal drift.
- Layer 2 (near): higher freq, intermittent thicker clumps.

Gameplay guardrail:

- Clamp max final smoke alpha in combat lanes so projectiles remain readable.

## D) Heat Haze Distortion

Technique:

- Screen-space UV offset driven by animated noise and height falloff.

Math sketch:

- Sample mask `m` from heat sources (magma/jets).
- `n1 = noise(screen_uv * f1 + t * v1)`
- `n2 = noise(screen_uv * f2 - t * v2)`
- `offset = (vec2(n1, n2) * 2.0 - 1.0) * distortion_amp * m`
- Distorted sample: `scene(screen_uv + offset)`

Falloff:

- Strongest near source, decays with distance and altitude.
- Optional temporal easing to avoid jitter.

Budget:

- Single full-screen pass preferred.
- Skip effect when low graphics preset is active.

## E) Ember and Ash Particles

Technique:

- CPU or GPU point sprites with procedural drift.

Motion:

- Embers: upward buoyancy + curl noise lateral wobble.
- Ash: slower drift, slight downward settling.

Equations:

- `vel += vec2(curl_xz(pos,t), buoyancy - drag * speed) * dt`
- `life01 = age / lifetime`
- Size over life: small -> medium -> fade.
- Brightness over life: high initially for embers, low steady for ash.

Spawn zones:

- Along magma seams.
- Around active jets and explosions.

## Terrain Material Split

Use mask-driven blending for cohesive ground:

- `ground_mask` -> cooled basalt.
- `magma_mask` -> emissive lava flow.
- `slag_mask` -> transitional hot rock.

Blend strategy:

- Height/curvature-aware blend to avoid flat texture transitions.
- Add subtle triplanar or rotated UV sampling to reduce tiling.

## Event Set Pieces

### 1) Magma Surge Wave

- Temporary brightening and flow-speed increase in select channels.
- Distortion and ember emission spike during surge.

### 2) Vent Chain Reaction

- Sequential jet plumes triggered left-to-right or spiral order.
- Telegraph with ground crack glow before ignition.

### 3) Smoke Front Roll-In

- Mid-fight atmospheric phase where near smoke layer thickens briefly.
- Countered by reducing particle density elsewhere to preserve clarity.

## Implementation Sequence

1. Build base lava ground material (noise + crack mask + emissive ramp).
2. Add flame plume system with one reusable shader/material.
3. Add smoke layer stack and tune alpha limits for readability.
4. Add heat haze pass tied to magma/flame masks.
5. Add ember/ash particles with capped spawn budgets.
6. Integrate timed set-piece events (surge, vent chain, smoke front).
7. Profile and add LOD scaling by screen coverage.

## Performance and LOD Guardrails

- Cap total active jet plumes.
- Reduce FBM octaves and plume edge detail at distance.
- Disable near smoke layer on low preset.
- Reduce distortion sample strength or update rate on low preset.
- Particle cap with priority to embers near player focus area.

Hard fail-safe:

- If frame budget is exceeded, degrade effects in this order:
  1) Heat haze strength
  2) Near smoke layer
  3) Particle count
  4) Flame edge detail

## Readability Rules

- Keep hostile entities brighter and sharper than background fire.
- Reserve near-white tones for short-lived hotspots only.
- Avoid persistent full-screen orange wash.
- Maintain clear projectile contrast against magma and smoke.

## Suggested Tunables

- `magma_scale = 0.8..1.6`
- `warp_amp = 0.08..0.20`
- `pulse_freq = 0.8..1.7`
- `plume_h = 0.6..1.8` (normalized local units)
- `rise_speed = 0.7..2.4`
- `distortion_amp = 0.001..0.006` (screen UV units)
- `smoke_alpha_cap = 0.35..0.55`
- `ember_spawn_rate = 40..180` per second per hotspot cluster

## Validation Checklist

- Build passes: `cmake --build build -j4`.
- Level remains readable at 1280x720 in heavy combat.
- Enemy/projectile contrast remains clear over brightest magma zones.
- Distortion adds heat feel without nausea/jitter.
- Smoke adds depth without obscuring critical gameplay.
- Frame pacing remains stable in worst-case event overlap.
