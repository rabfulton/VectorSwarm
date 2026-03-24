# GPU Sphere Style Plans

## Goal

Add two new sphere render styles that reuse the existing sphere gameplay and physics:

- `sphere_hologram_planetarium`
- `sphere_ion_storm`

These styles should be visual-only additions on top of the current sphere gameplay path. They should not introduce alternate movement, spawn, or collision rules.

The implementation must be GPU-only for runtime rendering:

- no new CPU fallback renderers
- no per-frame CPU mesh rebuilding for these styles
- no expensive per-frame CPU noise synthesis
- expensive noise fields should be prebaked once, then sampled cheaply at runtime

If a build cannot support the required GPU path, the style should be unavailable rather than silently falling back to a slow CPU implementation.

## Current Relevant Paths

- Sphere gameplay and projection already live in [`src/game.c`](../src/game.c).
- Current sphere web rendering is CPU-heavy in [`draw_sphere_web(...)`](../src/render.c).
- Current GPU sphere line/fill submission is wired through [`render_build_sphere_gpu_lines(...)`](../src/render.c) and [`record_gpu_radar(...)`](../src/main.c).
- Current GPU particle sphere path is wired through [`record_gpu_sphere_particles(...)`](../src/main.c).

The new styles should build on the existing GPU submission model in `src/main.c`, not on the CPU vector path in `src/render.c`.

## Shared Foundation Plan

### 1. New Render Styles

Add two render-style enums and parser/editor labels:

- `LEVEL_RENDER_SPHERE_HOLOGRAM`
- `LEVEL_RENDER_SPHERE_ION_STORM`

Recommended file touch points:

- `src/leveldef.h`
- `src/leveldef.c`
- `src/level_editor.c`
- `src/render.c`
- `src/main.c`

`level_uses_sphere(...)` and related gameplay checks should include both styles so they inherit the same sphere mechanics as `sphere` and `sphere_particle`.

### 2. Shared GPU Sphere Resource Layer

Create a shared GPU-only sphere style resource block in `src/main.c` that owns:

- one static icosphere mesh for shell rendering
- one static great-circle/ring line mesh for hologram overlays
- one descriptor set/layout for sphere-style textures
- one small blue-noise or ordered-dither texture
- one small scanline/phosphor mask texture

These resources should be created once at renderer initialization or swapchain/pipeline rebuild time, not regenerated per frame.

### 3. Prebaked Procedural Textures

Prefer one-time baking over live procedural generation for expensive noise:

- bake at startup using a compute shader, or
- ship offline-generated textures as assets if that is simpler

Recommended prebaked resources:

- `curl_volume_64`: 64x64x64 3D texture storing curl vectors in RGB
- `band_warp_2d`: 256x64 2D texture for gas-belt warp modulation
- `aurora_mask_2d`: 256x128 2D texture for polar curtain breakup
- `blue_noise_2d`: 64x64 or 128x128
- `holo_mask_2d`: 64x64 ordered dither / phosphor mask

Rules:

- do not numerically differentiate noise in the fragment shader every frame if a prebaked curl field can be sampled instead
- do not generate these textures on the CPU each frame
- do not stream dynamic texture uploads every frame for these effects

### 4. Shared Shader Inputs

Both styles should consume a compact push-constant or uniform block with:

- viewport size
- screen center
- sphere radius in pixels
- sphere orientation quaternion
- time
- palette/tint controls
- optional style-specific tuning scalars

This keeps the gameplay-side sphere state unified while letting the GPU handle all per-pixel and per-vertex work.

### 5. Availability Rules

These styles should be explicitly GPU-only:

- available only when the relevant shader path is compiled and initialized
- disabled in menus/editor if the pipeline is unavailable
- no fallback to `draw_sphere_web(...)`

This is important both for performance and to avoid drifting visual behavior across code paths.

## Plan A: `sphere_hologram_planetarium`

### Visual Target

A mission-control hologram globe:

- bright front-hemisphere geodesic lines
- dim dashed or attenuated backface rings
- latitude/longitude overlays
- rotating sweep arcs
- orbit-track circles
- phosphor persistence and scanline breakup
- subtle CRT-like screen-door shimmer

The look should read as synthetic and instrument-like, not as a textured planet.

### Rendering Strategy

Implement this as a dedicated GPU line/fill pipeline, not as CPU vector drawing.

Recommended structure:

1. Upload a static line mesh once:
   - geodesic sphere edges
   - latitude rings
   - longitude rings
   - a few reusable orbit arcs
2. Rotate the geometry in the vertex shader using the sphere quaternion.
3. In the fragment shader:
   - attenuate lines by view-space `z`
   - dim or dash backfacing arcs
   - apply phosphor scanline modulation
   - apply ordered dither to reduce flat digital-looking coverage
4. Add a second lightweight additive pass for:
   - sweep glow
   - rim halo
   - reticle accents

### GPU Implementation Notes

- Reuse the existing radar-style line submission model where practical.
- Do not rebuild sphere line vertices on the CPU each frame.
- Instead, keep the sphere line mesh static and do orientation and visibility work in the shader.
- Backface attenuation should happen in the shader from transformed normals or transformed positions.

### Classic Computer Graphics Techniques To Use

- hidden-line attenuation instead of full hidden-line removal
- screen-door transparency / ordered dithering
- scanline modulation
- phosphor persistence tinting
- additive sweep bloom
- front/back hemisphere line contrast

This keeps the look grounded in classic workstation/planetarium graphics rather than modern PBR.

### Suggested Pipeline Layout

- `sphere_hologram_line_pipeline`
- `sphere_hologram_glow_pipeline`

Both can share:

- one static vertex buffer
- one descriptor set for dither/mask textures
- one push-constant block

### Runtime Cost Profile

Expected runtime work:

- two very cheap draws
- no dynamic geometry rebuild
- no particle simulation
- no volumetric marching

This should be the cheaper of the two new styles.

### Implementation Steps

1. Add the new render-style enum and level parser/editor support.
2. Add static GPU buffers for hologram sphere lines/rings.
3. Add hologram shaders and pipeline creation in `src/main.c`.
4. Add a `record_gpu_sphere_hologram(...)` pass.
5. Route render-style selection so this pass runs for `LEVEL_RENDER_SPHERE_HOLOGRAM`.
6. Tune front/back line contrast, dash rules, scanline strength, and sweep timing.

### Risks

- If too many lines are rendered at uniform brightness, the globe will become unreadable.
- If scanline/dither is pushed too hard, the style will look noisy instead of precise.
- If the style reuses too much of the existing `LEVEL_RENDER_SPHERE` presentation, it will not feel distinct enough.

The fix is to bias toward sparse major rings, strong silhouette control, and clear front/back separation.

## Plan B: `sphere_ion_storm`

### Visual Target

A gas-giant atmosphere wrapped around the gameplay sphere:

- broad latitudinal belts
- curl-noise-driven storm shear and turbulence
- large oval vortices
- layered translucent shells
- bright polar aurora curtains
- deep rim glow and forward-scatter haze

It should read as a giant magnetized atmosphere, not as a generic cloud sphere.

### Core Look Rule

Curl noise should drive advection and storm structure, but the base shape must still come from latitude bands.

Without latitude bias, curl noise alone will read as smoke or nebula.

The recipe should be:

1. Start from band masks aligned to sphere latitude.
2. Warp those bands with samples from a prebaked curl field.
3. Add storm ovals from secondary low-frequency masks.
4. Add polar aurora separately, not as the same signal as the cloud belts.

### Rendering Strategy

Implement this as a dedicated GPU shell renderer.

Recommended structure:

- draw 3 to 5 concentric icosphere shells
- in the vertex shader:
  - rotate by sphere quaternion
  - push shell radius slightly outward per layer
- in the fragment shader:
  - compute sphere-local position
  - derive latitude and tangent basis
  - sample prebaked curl volume and band-warp textures
  - advect band coordinates
  - evaluate belt color, storm masks, aurora masks, and Fresnel
  - accumulate shell opacity additively or alpha-blended back-to-front

This follows the shell-texturing playbook and avoids CPU particles or CPU noise work.

### Prebaked Texture Plan

Generate once at startup or ship offline:

- `curl_volume_64`
  - source of local flow vectors
  - sampled in sphere-local space
- `band_warp_2d`
  - low-cost modulation for belt width and breakup
- `storm_shape_2d`
  - optional oval storm masks for long-lived giant vortices
- `aurora_mask_2d`
  - breakup for polar ribbons
- `blue_noise_2d`
  - dithering and shell de-banding

Do not:

- compute curl by finite-differencing layered noise per pixel every frame
- build noise textures on the CPU during gameplay
- spawn thousands of CPU-driven shell particles to fake the atmosphere

### GPU Shading Model

The fragment shader should combine:

- latitude-based color bands
- curl-warped UVs
- shell-thickness falloff
- Fresnel rim term
- polar aurora mask
- view-dependent haze
- optional bloom threshold mask

Color direction:

- warm lower belts
- cooler upper haze
- electric cyan/green aurora
- occasional magenta accents only if deliberately stylized

### Suggested Pipeline Layout

- `sphere_ion_storm_pipeline`
- `sphere_ion_storm_bloom_pipeline`

Optional later:

- one compute pass to update slow storm offsets or animate hotspot fields

That compute pass should remain lightweight and operate on a tiny parameter buffer, not on a giant per-frame texture bake.

### Runtime Cost Profile

Expected runtime work:

- 3 to 5 shell draws
- several cheap texture samples per fragment
- one bloom-capable emissive pass

This is heavier than hologram, but still predictable and GPU-friendly if the shell count stays low and the noise is prebaked.

### Implementation Steps

1. Add the new render-style enum and level parser/editor support.
2. Add one-time prebake or asset load for curl/band/aurora textures.
3. Upload a static icosphere mesh once for shell rendering.
4. Add ion-storm shader pair and pipeline creation in `src/main.c`.
5. Add `record_gpu_sphere_ion_storm(...)`.
6. Route render-style selection so this pass runs for `LEVEL_RENDER_SPHERE_ION_STORM`.
7. Tune shell count, curl intensity, belt contrast, aurora brightness, and bloom.

### Risks

- Too much curl warp will destroy the gas-giant belt read.
- Too many shell layers will cost fill rate without adding clarity.
- If aurora and cloud glow share the same color space, the image will flatten.

The fix is to keep the belts structurally dominant, use aurora only near poles, and cap shell count aggressively.

## Recommended Build Order

1. Shared GPU sphere-style infrastructure.
2. `sphere_hologram_planetarium`.
3. Shared prebaked-noise resource path.
4. `sphere_ion_storm`.

Reason:

- hologram is cheaper and lower risk
- it proves the new GPU-only sphere-style routing
- ion storm can then reuse the same style resource ownership and pipeline conventions

## Fail-Fast Rules

- If a requested style pipeline is not available, do not silently fall back to CPU sphere rendering.
- If prebaked textures fail to load or bake, surface a clear initialization error.
- If a level selects one of these styles in a build without GPU support, treat that as unsupported configuration.

This matches the project rule against hidden fallback behavior.

## File Touch Points

Expected implementation files:

- `src/leveldef.h`
- `src/leveldef.c`
- `src/level_editor.c`
- `src/render.c`
- `src/main.c`
- shader sources and generated SPIR-V headers
- `docs/leveldef_schema.md`

If any new `.c` files are introduced, they must be added explicitly to `CMakeLists.txt`.
