# Forest Render Pipeline

This document describes the active forest background pipeline and the debug keybinds used to isolate each stage during gameplay.

## Pass Order

1. `bg forest cache`
   File: [`src/shaders/forest_cache.frag`](/home/rab/code/v-type/src/shaders/forest_cache.frag)
   Entry point: [`record_gpu_forest_cache(...)`](/home/rab/code/v-type/src/main.c#L10240)
   Output target: `forest_cache_view`

   What it does:
   - Builds the large structural field for the biome.
   - Writes megatrunk mass/rim/vein information into `RGB`.
   - Writes canopy coverage into `A`.

   Expected result:
   - Large dark implied trunks and cell-like organic background structures.
   - Brighter rim/vein accents where trunk edges should catch light.
   - A soft canopy coverage mask that later drives background depth and godrays.

2. `bg forest flora`
   File: [`src/shaders/forest_flora.frag`](/home/rab/code/v-type/src/shaders/forest_flora.frag)
   Entry point: [`record_gpu_forest_flora(...)`](/home/rab/code/v-type/src/main.c#L10169)
   Output target: shared `u_kelp` flora prepass

   What it does:
   - Generates the plant body field from explicit per-plant descriptors.
   - Resolves layer ownership per pixel inside the flora pass.
   - Writes plant body color into `RGB`.
   - Writes plant body coverage into `A`.

   Expected result:
   - The actual stem/cap/pod silhouettes for far, mid, and near flora.
   - Stable plant colors chosen per plant rather than screen-space blotching.
   - Near plants should occlude mid/far plants inside the prepass.

   Important note:
   - Tendrils are no longer authored here. The flora prepass owns plant bodies only.

3. `bg forest main`
   File: [`src/shaders/forest.frag`](/home/rab/code/v-type/src/shaders/forest.frag)
   Entry point: [`record_gpu_forest(...)`](/home/rab/code/v-type/src/main.c#L10296)
   Output target: main scene render target

   What it does:
   - Builds the base atmosphere/background color.
   - Samples the cache prepass and flora prepass.
   - Applies godrays and near-occlusion shaping.
   - Adds the full-resolution tendril overlay.
   - Produces the final forest composite.

   Expected result:
   - Cache and flora should read as a single coherent scene.
   - Tendrils should remain visible because they are composited after the main scene tonemapping.
   - Occlusion should darken selected near bands without flattening all flora.

4. Forest spores
   CPU update: [`append_gpu_forest_spores(...)`](/home/rab/code/v-type/src/main.c#L9145)
   Draw path: shared GPU particle instance renderer

   What it does:
   - Updates world-anchored spore flock clusters on the CPU.
   - Uploads analytic spore particles into the GPU particle instance buffer.
   - Draws spores independently from the fullscreen forest shaders.

   Expected result:
   - Bright drifting spore swarms in the upper band of the scene.
   - Camera-relative movement that feels world-anchored rather than screen-anchored.

## Debug Views

Forest debug views are only active while playing a forest-background level.

Keybinds:
- `F6`: previous forest debug view
- `F7`: next forest debug view
- `F8`: reset to full forest render

TTY feedback will report the active mode.

Modes:
- `forest debug: full`
  Normal forest render.

- `forest debug: cache`
  Shows only the sampled `u_forest_cache` result.
  Use this to inspect trunk mass, rim, veins, and canopy coverage.

- `forest debug: flora`
  Shows only the sampled flora prepass from `u_kelp`.
  Use this to inspect plant body colors, layer ordering, and coverage without the main composite.

- `forest debug: tendrils`
  Shows only the full-resolution tendril overlay.
  Use this to verify curtain length, brightness, and alignment with umbrella plants.

- `forest debug: occlusion`
  Shows the near-occlusion/canopy masking contribution.
  Use this to inspect where foreground darkening is coming from.

- `forest debug: main`
  Shows the main forest composite before the tendril overlay is added.
  Use this to separate tendril problems from base-scene problems.

- `forest debug: spores`
  Shows only the spore particle field.
  The fullscreen forest background is forced black in this mode and non-forest particles are suppressed.

## How To Use The Debug Views

When a visual bug appears, isolate it by pass instead of tuning blind:

- If a shape is wrong in `flora`, the bug is in [`forest_flora.frag`](/home/rab/code/v-type/src/shaders/forest_flora.frag).
- If a shape is correct in `flora` but wrong in `main`, the bug is in [`forest.frag`](/home/rab/code/v-type/src/shaders/forest.frag).
- If a tendril is missing in `tendrils`, the bug is in the full-resolution tendril overlay path in [`forest.frag`](/home/rab/code/v-type/src/shaders/forest.frag).
- If a tendril is correct in `tendrils` but wrong in `full`, the bug is in how the main pass composites that overlay.
- If spores are missing in `spores`, the bug is in the CPU spore particle path in [`src/main.c`](/home/rab/code/v-type/src/main.c).

This is the intended workflow for future forest work: inspect the owning pass first, then change only that pass.
