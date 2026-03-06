# Forest Shader Lessons Learned

This document captures the useful findings from the forest-biome rendering pass, including the bugs we hit, what actually caused them, which fixes worked, and which approaches repeatedly failed.

The goal is to preserve the technical lessons even if the repository is reset to an earlier visual state.

## Executive Summary

The forest work became unstable because too many visual responsibilities were packed into too few passes, and the same shapes were being interpreted differently in different stages.

The biggest recurring problems were:
- body coverage, glow, occlusion, and species color being mixed together too early
- screen-space shading leaking into plant-local shading
- fine tendril detail being authored in a half-resolution prepass
- multiple systems trying to own the same visual feature
- repeated parameter tweaking without isolating the owning pass first

The strongest lessons are:
- treat the flora prepass as the source of truth for plant body coverage
- keep the main forest composite dumb; do not recolor flora there
- isolate debug views per pass before tuning visuals
- do not author thin dark lines in the half-res flora prepass
- resolve overlap from coverage priority, not from generic alpha accumulation

## Current Forest Pass Structure

The active forest rendering is split into four main stages:

1. `bg forest cache`
   File: [`forest_cache.frag`](/home/rab/code/v-type/src/shaders/forest_cache.frag)

   Owns:
   - megatrunk mass
   - trunk rim accents
   - trunk vein/highlight field
   - canopy coverage

   Does not own:
   - flora body color
   - tendrils
   - spores

2. `bg forest flora`
   File: [`forest_flora.frag`](/home/rab/code/v-type/src/shaders/forest_flora.frag)

   Owns:
   - plant bodies
   - stems
   - pods/discs/umbrella caps
   - per-layer plant coverage
   - per-plant body color

   Should not own:
   - fine tendril curtains
   - scene-level darkening
   - recoloring in screen space

3. `bg forest main`
   File: [`forest.frag`](/home/rab/code/v-type/src/shaders/forest.frag)

   Owns:
   - background scene assembly
   - cache sampling
   - flora prepass sampling
   - godrays
   - near occlusion
   - full-resolution tendril overlay
   - final composite

   Should not own:
   - species recoloring of flora
   - second body-coverage system fighting the flora pass

4. Forest spores
   CPU path in [`main.c`](/home/rab/code/v-type/src/main.c)
   GPU draw through the shared particle pipeline

   Owns:
   - drifting spore flocks
   - world/camera-relative motion logic
   - spore color/size at the particle level

## What We Learned About The Major Bugs

### 1. The camouflage / blotchy flora bug

Observed behavior:
- individual plants appeared mottled or patterned
- color changed across the same plant in a way that looked like camouflage

Actual cause:
- flora color decisions were being driven from screen-space or per-fragment values rather than stable per-plant seeds
- the main composite was also bleaching/reinterpreting the flora texture after the prepass

Lesson:
- species color must be chosen per plant descriptor, not per screen pixel
- if the prepass already authored the plant color, the main composite should not reinterpret it

Useful rule:
- if a single plant changes color across its surface in a way that is not intentional material shading, check whether color is tied to `uv` or current-pixel noise instead of plant seed/state

### 2. Far layers showing through near layers

Observed behavior:
- bright or pale far/mid flora remained visible inside near flora silhouettes
- overlap looked ghostly or additive

Actual cause:
- the flora pass was using generic alpha blending/compositing semantics rather than real depth/coverage priority
- multiple overlapping plants within one layer could also brighten each other

What helped:
- winner-take-most/strongest-coverage selection within a layer
- explicit cross-layer masking: near suppresses mid/far, mid suppresses far/root, etc.

Lesson:
- body overlap must resolve from coverage priority, not accumulated translucent color
- if front organic forms are supposed to read as mass, they cannot be treated like stacked smoke cards

### 3. The “reflective / glassy” overlap bug

Observed behavior:
- overlapping flora looked glossy or reflective instead of solid

Actual cause:
- back layers were being dimmed or multiplied after composition instead of being suppressed before composition
- glow and pale body color remained visible through the overlap

Lesson:
- if overlap looks reflective, check whether the pipeline is darkening already-composited RGB instead of removing covered layers before blending

### 4. Clipping of tall/skinny flora

Observed behavior:
- tall thin plants clipped at the top of the screen
- naive height clamping did not reliably fix it

Actual causes:
- some paths were still using rough height estimates instead of final plant bounds
- at one point the flora prepass had an explicit top-band clip, which guaranteed failure for tall species
- cap placement and species proportions mattered more than the raw height scalar

Lesson:
- clipping must be solved from final plant extents, not guessed height alone
- compute the actual plant top from stem + cap geometry, then place/shift the plant
- do not hard-clip the flora prepass unless the generated descriptors already guarantee safety

### 5. The black tendril bug

Observed behavior:
- umbrella tendrils looked black or nearly invisible
- many parameter changes appeared to have no effect

There were multiple real causes over time:

1. Tendrils were being authored in the half-resolution flora prepass.
   Result:
   - thin strands collapsed to a few texels
   - dark strands quantized away during upsampling
   - length/brightness changes often had little visible effect

2. Tendril color was falling back toward dark stem/shadow values.
   Result:
   - strands read as black under the cap

3. The full-resolution overlay was sometimes composited too early.
   Result:
   - later darkening/tonemapping crushed it back down

4. The full-resolution overlay reused the same old bug:
   the curtain attachment depended on the umbrella mask at the current pixel.
   Result:
   - once the fragment moved below the cap, the attachment went to zero
   - the debug tendril view became nearly blank

Lesson:
- fine hanging strands must be full resolution
- the attachment function for a curtain below a cap cannot depend on the current-pixel filled umbrella mask
- use a horizontal/profile-based attachment, not a current-pixel cap coverage test

### 6. The “changes have no visible effect” problem

Observed behavior:
- many edits compiled successfully but produced little or no visible change

Actual causes:
- the changed parameter was downstream of a stronger limiter
- the active shape was resolution-limited by the half-res prepass
- later composite stages were bleaching or crushing the modified signal
- the wrong pass owned the feature

Lesson:
- before tuning a parameter, identify the owning pass and the strongest limiter:
  - resolution
  - coverage
  - later compositing
  - wrong layer ownership

## Performance Findings

The key performance findings from profiling:

- `bg forest main` was the dominant fullscreen cost.
- `bg forest flora` was the second major cost.
- the cache pass mattered, but it was not the main bottleneck once the obvious work moved there.
- the CPU spore path was not the main GPU problem.

Useful reductions that actually helped:
- early reject empty pixels in the flora/main passes
- skip heavy shape/tendril work outside plausible bands
- reduce redundant godray/cache sampling
- reduce empty-space procedural evaluation instead of only shaving tiny amounts of ALU

Reductions that were risky or visually destabilizing:
- forcing looping/repeating structural caches without protecting the art path
- removing or rewriting large structural fields without first locking the visual baseline

Lesson:
- for this biome, the best performance wins came from killing empty-space work, not from cosmetic parameter trimming

## Architectural Lessons

### The flora prepass should be the source of truth for body coverage

What worked better:
- plant descriptors
- stable per-plant seeds
- explicit bounds
- winner selection by coverage

What repeatedly failed:
- one giant shader trying to derive everything per fragment with no explicit per-plant ownership

Recommendation:
- keep body coverage and body color together in the flora prepass
- keep layer ordering there too

### The main pass should not make species-color decisions

What went wrong:
- the main pass repeatedly washed out, retinted, or reinterpreted flora authored upstream

Recommendation:
- the main pass should sample the flora prepass mostly as-authored
- it can add scene-level lighting, atmosphere, and occlusion
- it should not choose what species color a plant is

### Tendrils should be their own feature

This was one of the strongest conclusions.

Recommendation:
- do not draw thin hanging curtains in the half-resolution flora prepass
- either:
  - draw them full-resolution in `forest.frag`, or
  - move them to explicit geometry / instanced quads later

### Spores should stay in the particle system

The CPU particle path ended up being the right direction for spores.

Recommendation:
- keep spores independent from the fullscreen forest shader
- debug them separately from cache/flora

## Debugging Workflow That Worked

The addition of forest debug views was one of the most useful changes.

Useful views:
- `flora`
  shows whether the body pass itself is correct

- `tendrils`
  shows whether the full-resolution overlay is generating actual curtain content

- `occlusion`
  shows whether foreground darkening is coming from the intended mask

- `cache`
  shows whether structural background fields are stable

- `main`
  shows the forest composite before the tendril overlay

- `spores`
  isolates particle behavior from the fullscreen shaders

Key lesson:
- do not tune the full render when a debug view can show which stage is actually wrong

## Recommended Plan For A Future Clean Pass

If the repository is reset and the forest pass is revisited, the safest order is:

1. Lock the background structure first.
   - cache pass only
   - megatrunks/canopy/godrays stable

2. Rebuild the flora prepass as the authoritative body system.
   - explicit plant descriptors
   - per-plant stable colors
   - real layer ownership by coverage
   - no thin tendrils yet

3. Composite the flora in the main pass with minimal reinterpretation.
   - no recoloring
   - no flora-specific bleaching

4. Add tendrils only after the plant bodies are correct.
   - full-resolution only
   - debug with `forest debug: tendrils`

5. Keep spores separate in the particle system.

6. Profile again only after the art path is stable.

## Things Worth Keeping Even If The Repo Is Reset

- [`forest_pipeline_debug.md`](/home/rab/code/v-type/docs/forest_pipeline_debug.md)
- the idea of forest-specific debug views by pass
- the GPU debug labels:
  - `bg forest cache`
  - `bg forest flora`
  - `bg forest main`
- the insight that spores belong in the particle path
- the insight that tendrils should not live in the half-res flora pass

## Final Conclusion

The biggest mistake was not any one shader formula. It was allowing multiple passes to partially own the same visual language.

When the next forest pass happens, success will depend on:
- single ownership per visual feature
- stable per-plant data for flora
- pass-isolated debugging before tuning
- resisting the urge to fix composite-stage symptoms before the owning pass is proven correct
