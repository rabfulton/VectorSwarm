# Forest Level Ambience Report

## Goal

Design a new side-scrolling biome that evokes the toxic-beautiful, spore-laden, insect-haunted forest mood you want, while fitting the current `v-type` rendering model:

- one coherent background system, not a parallel special case
- config-driven level tuning through `level_*.cfg`
- shader-heavy ambience with deterministic procedural structure
- strong silhouette readability for gameplay under the existing CRT/post stack

This should be treated as a homage in mood, scale, and atmosphere, not a direct reproduction of any specific shot or asset.

## What The Engine Already Gives Us

The current codebase already supports the right architectural shape for this:

- data-driven background selection in [`src/leveldef.c`](/home/rab/code/v-type/src/leveldef.c)
- per-theme tuning values in [`src/leveldef.h`](/home/rab/code/v-type/src/leveldef.h)
- dedicated GPU environment passes in [`src/main.c`](/home/rab/code/v-type/src/main.c)
- custom fragment shaders for `underwater`, `fire`, and `ice` in [`src/shaders/`](/home/rab/code/v-type/src/shaders)

That matters because the forest should be implemented as one more environment family in the same system:

- add `background=forest`
- add `forest.*` tuning keys
- add one main forest shader pass
- optionally add one low-res flora prepass, similar in spirit to the underwater kelp path

That is the cleanest route and respects the repo rule to extend existing systems rather than inventing a second environment framework.

## Ambience Targets

The level should feel like a dead world that has overgrown into something sacred and hostile.

Primary visual traits:

- towering fungal trunks and shell-like stalks
- dense airborne spores that read like glowing snow, ash, and pollen at once
- translucent membranes, caps, and fronds that catch backlight
- insect movement that feels ritualistic rather than merely aggressive
- muted earth below, luminous poison above
- cathedral-scale negative space between giant growths

The strongest reference cues are not literal objects. They are:

- layered depth from foreground silhouettes against glowing haze
- irregular organic repetition
- drifting particulate density
- color contrast between rot and bioluminescence
- the sense that the forest is alive and digesting the ruins

## Visual Pillars

### 1. Megaflora Silhouette

The screen needs a few large, readable forms before fine detail matters:

- giant trunks with widened bases
- umbrella caps and ribbed fungal fans
- root arches bridging across lanes
- hanging tendrils and veil-like membranes

If these silhouettes work in pure black against a bright mist, the biome will already feel convincing.

### 2. Spore Volume

Spores are the unifying motion layer:

- far field: fine dust haze
- mid field: drifting soft disks and filaments
- near field: large focus-pulling motes and streaks

This should not read like generic smoke. It needs softer edges, slower inertia, and subtle luminescent response.

### 3. Bioluminescent Veins

Use light sparingly:

- cap rims
- underside membranes
- spore pockets
- vein structures in bark/fungus
- occasional alien pollen bursts

The dark mass should dominate. Glow is the accent that gives the forest its spiritual/unnatural quality.

### 4. Layered Occlusion

Foreground darkness, midground structure, and backlit haze are what produce the filmic look.

The player should move through:

- near black silhouettes in front
- readable combat space in the middle
- luminous fog and giant forms behind

### 5. Slow Organic Motion

Almost everything should move, but very little should move quickly:

- spore drift
- tendril sway
- membrane breathing
- cap pulsing
- insect nest quiver

Fast motion should be reserved for enemies, attacks, and occasional set-piece reactions.

## Recommended Technical Direction

## A. Add A New Background Family, Not A One-Off Level Hack

Follow the same pattern used by `underwater`, `fire`, and `ice`.

Probable implementation touch points:

- [`src/leveldef.c`](/home/rab/code/v-type/src/leveldef.c)
  - parse `background=forest`
  - parse `forest.*` tuning keys
- [`src/leveldef.h`](/home/rab/code/v-type/src/leveldef.h)
  - add forest tuning fields to `leveldef_level`
- [`src/main.c`](/home/rab/code/v-type/src/main.c)
  - add `create_forest_resources(...)`
  - add `record_gpu_forest(...)`
  - optionally add `record_gpu_forest_flora(...)`
- [`src/shaders/forest.frag`](/home/rab/code/v-type/src/shaders/forest.frag)
  - main composite shader
- [`src/shaders/forest_flora.frag`](/home/rab/code/v-type/src/shaders/forest_flora.frag)
  - optional low-res flora prepass
- [`CMakeLists.txt`](/home/rab/code/v-type/CMakeLists.txt)
  - add shader build rules

This keeps the environment system unified and lets the level be authored exactly like current themed levels.

## B. Use A Hybrid Art Strategy

Pure hand-authored art will be slow and hard to iterate. Pure procedural generation will look muddy if not tightly art-directed. The best result here is hybrid:

- authored palette direction
- procedural large-form generation
- shader-based atmospheric detail
- deterministic seeded variation

Recommended split:

- offline or startup-generated silhouettes for major flora
- shader-generated haze, glow, and spores
- optional hand-authored masks or seed maps later, only if needed

## C. Treat L-Systems As Structure Generators, Not Final Art

L-systems are useful here, but mostly for generating believable growth skeletons:

- trunk branching
- hanging roots
- fungal rib structures
- vein networks

Do not expect raw L-system lines alone to look beautiful. The trick is:

1. generate a branching scaffold
2. convert it into widened ribbons, caps, ribs, and membranes
3. shade it with backlight, thickness, translucency, and depth fog

That is where the "GPU shader magic" matters.

## Core Rendering Architecture

## 1. Three-Layer Composition

The forest should be composed as three logical layers.

### Layer A: Far Atmosphere

Purpose:

- establish depth
- carry global color mood
- hold god rays and distant giant forms

Content:

- low-frequency fog fields
- distant canopy shapes
- faint bioluminescent clouds
- slow drifting spores

Implementation:

- full-screen fragment shader
- mostly noise-driven and cheap

### Layer B: Midground Forest Mass

Purpose:

- sell scale and identity
- provide large readable organic shapes

Content:

- trunks
- fungal caps
- arching roots
- membrane sheets
- clustered nest shapes

Implementation:

- ideally a low-res prepass texture or batched procedural silhouette field
- then composited in main forest pass

This is where an L-system or space-colonization-generated flora field pays off.

### Layer C: Near Occluders And Foreground Motes

Purpose:

- push depth
- create cinematic framing
- make the player feel inside the biome

Content:

- dark foreground fronds
- near spore motes
- hanging strands crossing the view
- occasional bright membrane flashes

Implementation:

- a small number of high-contrast overlay shapes
- near-field particle layer

## 2. One Main Shader Plus One Optional Flora Prepass

The underwater system already demonstrates a useful pattern:

- a low-res auxiliary render target for structured content
- a main full-screen shader that composites atmosphere and detail

That pattern maps well to a forest.

Recommended approach:

- `forest_flora.frag`
  - renders large trunks, caps, tendrils, and membranes into a lower-res texture
  - deterministic, camera-aware, tiled in world space
- `forest.frag`
  - samples the flora texture
  - adds haze, spores, bloom accents, backlight, and atmospheric grading

This gives:

- stable large forms
- cheap screen coverage
- good art direction
- controllable performance

## Procedural Structure Generation

## A. L-System Library For Flora Archetypes

Use multiple grammars, each specialized to a silhouette family. One grammar for everything will produce repetition.

### Archetype 1: Tower Fungus

Use for:

- main trunk forms
- giant umbrella caps
- layered shelf fungus

Example grammar sketch:

```text
axiom: F
rules:
F -> FF-[+F+F+M]+[--F-M]
M -> [ccc]
```

Interpretation:

- `F` grows branch segments
- `+` and `-` rotate
- `[` and `]` push/pop transform
- `M` spawns a membrane or cap cluster
- `c` emits cap rib arcs rather than branch lines

Suggested generation controls:

- branch angle: `12..26 deg`
- length decay: `0.72..0.88`
- width decay: `0.76..0.92`
- depth: `3..6`

### Archetype 2: Hanging Veil

Use for:

- hanging tendrils
- draping moss-like membranes
- cocoon curtains

Better than a strict tree grammar:

- generate anchored spline strands
- apply gravity bias
- modulate with curl noise
- add membrane bridges between neighboring strands

### Archetype 3: Root Arch

Use for:

- lane framing
- giant rib-like roots
- foreground silhouettes

Good method:

- two growth points seeded far apart
- connect them with biased branching
- fit a smoothed centerline spline
- widen into ribbon geometry

### Archetype 4: Vein Fan

Use for:

- glowing translucent cap undersides
- radial shell structures
- leaf-like alien fans

Method:

- radial skeleton with uneven angular spacing
- secondary branches from each rib
- SDF thickness for translucency and rim lighting

## B. Space Colonization For More Natural Growth

For the largest plants, space colonization is often better than classic L-systems because it grows toward target regions and creates less obviously recursive shapes.

Use it for:

- canopy trunks reaching open pockets
- root systems seeking around combat lanes
- membrane ribs stretching toward attractor clouds

Algorithm sketch:

1. Scatter attractor points inside a target volume.
2. Start from one or more trunk seeds.
3. For each attractor, find the nearest growth node within influence radius.
4. Accumulate growth directions per node.
5. Spawn new nodes in averaged directions.
6. Remove attractors that are reached.
7. Repeat until attractors are exhausted.

Useful properties:

- naturally avoids uniform branching
- can preserve gameplay corridors by leaving no-attractor regions
- easy to shape around authored safe lanes

This is likely the best generator for the most iconic giant flora silhouettes.

## C. Convert Skeletons Into Renderable Shapes

The generated structure should not stay as thin lines.

Convert skeletons into:

- widened spline ribbons for trunks and roots
- variable-radius capsules for stems
- fan SDFs for caps
- ribbon sheets between nearby tendrils for membranes

Important rendering trick:

- thickness should vary with branch age, depth layer, and local curvature

Example:

```text
radius(node) = base_radius * age01^0.8 * depth_scale * curvature_relax
```

Where:

- `age01` is normalized distance from tip to root
- `depth_scale` makes far layers thinner
- `curvature_relax` prevents tight bends from ballooning

## Shader Methods

## A. Spore Fog Field

This is the main ambience effect.

Technique:

- layered FBM plus Worley noise plus advected particle highlights

Math sketch:

```text
uv0 = world_uv * spore_scale
warp = vec2(fbm(uv0 + t*w0), fbm(uv0*1.7 - t*w1))
uv1 = uv0 + warp_amp * (warp*2 - 1)
mist = fbm(uv1)
cells = worley_edge(uv1 * cell_scale)
clusters = smoothstep(cluster_lo, cluster_hi, 1 - cells)
spore_density = mist * 0.65 + clusters * 0.35
```

Use cases:

- mist controls broad coverage
- Worley clusters create colony-like pockets
- animated warp keeps the air alive

Rendering note:

- keep near and far spore frequencies separate
- near spores should drift faster across screen space
- far spores should move with world space and camera parallax

## B. Bioluminescent Membrane Lighting

This effect sells the alien beauty.

For each cap or membrane SDF:

```text
d = sdf_membrane(p)
edge = 1 - smoothstep(0, rim_w, abs(d))
thickness = thickness_map(p)
backlight = saturate(dot(light_dir, membrane_normal)*0.5 + 0.5)
translucency = edge * thickness * backlight
```

Then color mix:

```text
membrane_color = mix(dead_ochre, toxic_teal, translucency)
glow = translucency * pulse
```

This should be subtle. Too much emissive turns the scene into neon fantasy instead of poisonous beauty.

## C. Depth Fog With Occlusion Bias

The fog should not be uniform. It should build around plant masses and underneath the canopy.

Method:

- derive an occlusion proxy from flora alpha or signed distance
- increase fog density near large forms
- darken the upper canopy and brighten shafts behind gaps

Math sketch:

```text
fog_base = fbm(fog_uv)
fog_occ = smoothstep(occ_lo, occ_hi, flora_mass)
fog = fog_base * mix(0.7, 1.3, fog_occ)
```

This makes the world feel thick and inhabited.

## D. God Rays Through Canopy Gaps

This is a strong effect if kept restrained.

Cheap 2D method:

1. Build a canopy mask from far flora silhouettes.
2. Pick a directional light vector.
3. Sample along that vector a few times.
4. Accumulate openness.
5. Multiply by haze density.

Sketch:

```text
ray = 0
for i in 0..N-1:
    suv = uv + light_dir * step_len * i
    ray += 1 - canopy_mask(suv)
ray /= N
godray = ray * haze * ray_strength
```

Recommended:

- low sample count: `6..10`
- animate light angle very slightly over long intervals
- fade rays in foreground-heavy areas so gameplay stays legible

## E. Soft Spore Particle Layer

Use a dedicated particle effect for hero motes rather than trying to do everything in the background shader.

Behavior:

- large spores drift with buoyancy and lateral curl
- some rotate and pulse
- some stretch into short streaks when camera speed changes

Motion model:

```text
vel += curl_noise(pos * curl_scale, t * curl_speed) * curl_amp * dt
vel += wind * dt
vel *= 1 - drag * dt
pos += vel * dt
```

Visual model:

- soft disc
- double-ring shell
- faint inner nucleus
- optional chromatic fringe only at high glow moments

Use only a small count of near spores. The background shader can handle the dense fine particulate field.

## F. Living Bark / Cap Surface Trick

To avoid flat silhouettes, add internal texture modulation.

For trunks:

- ridge noise aligned along branch tangent
- slow pulse in internal vein mask
- edge brightening from backlight

For caps:

- radial banding
- rib SDFs
- local translucency
- occasional darker bruise zones

This can all be done without actual textured sprites if the shapes are derived from distance fields or widened spline masks.

## Rendering Trickery That Will Matter Most

## 1. Backlight Dominance

The most important trick is not detail generation. It is controlled backlighting.

If the scene is lit as:

- dark foreground
- luminous haze behind forms
- thin glowing rims on translucent structures

then even simple geometry will feel rich.

## 2. Controlled Palette Compression

Do not use too many saturated colors. Limit the palette to a few anchored zones:

- bark/rot: deep umber, smoke brown, cold charcoal
- toxic life: dusty teal, mold green, pale cyan
- spore glow: bone white, faint gold, soft turquoise

Suggested palette anchors:

- `#120D0A`
- `#2D2018`
- `#5A4B32`
- `#6E8660`
- `#7AB8A6`
- `#C7E6D7`

Keep the brightest values rare.

## 3. Foreground Framing

Near-black flora in front of the player sells scale better than adding more detail to the background.

Use:

- root arches entering from corners
- cap silhouettes crossing the top edge
- thin tendrils swaying in front of open space

These can be simple masks and still be extremely effective.

## 4. Temporal Phase Offsets Everywhere

Static procedural scenes die immediately. Every subsystem should have a different slow phase:

- haze drift
- membrane pulse
- cap breathing
- tendril sway
- spore cluster migration

Use deterministic seeds per layer or tile so repetition is broken without randomness flicker.

## 5. Topological Variation, Not Texture Variation

The level will feel artificial if variation comes only from noise values. Variation should come from changes in shape:

- wide cap clusters versus thin reeds
- root ribs versus umbrella crowns
- open glades versus dense curtains
- dead trunks versus glowing young growth

That is why structure generation matters more than piling on more noise octaves.

## Proposed Forest Tuning Keys

Add a compact set of level-driven controls, following the current theme-key model.

Suggested fields for `leveldef_level`:

- `forest_spore_density`
- `forest_spore_scale`
- `forest_spore_drift_speed`
- `forest_haze_alpha`
- `forest_canopy_density`
- `forest_parallax_strength`
- `forest_flora_density`
- `forest_branch_wobble_amp`
- `forest_branch_wobble_speed`
- `forest_membrane_glow`
- `forest_biolume_pulse_freq`
- `forest_godray_strength`
- `forest_root_arch_density`
- `forest_foreground_occluder_alpha`

Suggested config names:

```ini
background=forest
forest.spore_density=1.150
forest.spore_scale=1.000
forest.spore_drift_speed=0.850
forest.haze_alpha=0.580
forest.canopy_density=0.900
forest.parallax_strength=1.100
forest.flora_density=1.050
forest.branch_wobble_amp=0.700
forest.branch_wobble_speed=0.650
forest.membrane_glow=0.520
forest.biolume_pulse_freq=0.440
forest.godray_strength=0.300
forest.root_arch_density=0.650
forest.foreground_occluder_alpha=0.520
```

This is enough to make multiple variants:

- dim poisonous forest
- pale luminous spore grove
- root-choked tunnel biome
- flooded fungal basin

## Offline Art / Bake Opportunities

## A. Bake Forest Skeletons Into Generated Headers

The `industry` plan already points toward a useful pattern: generate runtime assets from source data rather than hand-maintaining giant arrays.

For forest content, a generator could emit:

- `src/generated/forest_flora_lut.h`
- stem seeds
- cap descriptors
- membrane links
- tile-local spline control points
- per-layer seed tables

Advantages:

- deterministic results
- easy visual iteration
- no runtime heavy generation cost
- keeps runtime code simple

## B. Optional SVG-Assisted Authoring

If fully procedural shapes do not give enough art direction, author a few source silhouettes as SVG and deform them procedurally at runtime:

- giant cap
- shell fan
- ribbed trunk
- hanging membrane

Then:

- pick variant by tile seed
- bend and scale in shader or CPU placement
- layer with procedural fog/spores

This hybrid approach often gives a better first shippable result than pure L-system output.

## C. Signed Distance Atlas For Flora Motifs

Another strong route is a tiny atlas of procedural motifs:

- cap
- pod
- rib fan
- cocoon
- veined leaf

Generate or author them once as SDFs, then:

- stamp them into the flora prepass
- warp them
- light them consistently

This can dramatically improve art direction without needing large texture assets.

## Enemy And Gameplay Harmony

The background must not fight gameplay readability.

Recommendations:

- keep combat lane brightness fairly stable in the center band
- push the strongest glows above and below the player path
- use enemy palette shifts sparingly; hostile silhouettes must remain readable
- avoid full-screen dense fog during bullet-heavy sections

Enemy types that fit this biome well:

- moth-like or beetle-like swarmers
- cocoon mines
- pollen burst hazards
- root arc traps
- glider insects with translucent wings

The atmosphere should imply an ecosystem, not just a color swap.

## Set Piece Ideas

### 1. Spore Bloom Corridor

- a low-combat section
- dense floating spores
- huge backlit caps
- slow god rays
- occasional membrane pulse

This is the "beauty shot" zone.

### 2. Root Cathedral

- giant crossing arches form the level ceiling
- negative space lanes between rib roots
- distant luminous sacs breathe in sync

### 3. Nest Wound

- the forest looks torn open
- exposed glowing interior veins
- more aggressive insect pressure
- haze turns from pale to sickly amber-green

### 4. Dead Basin

- fewer glows
- heavy dust field
- enormous fossilized fungi silhouettes
- occasional surviving luminous colonies

These can all be different parameterizations of the same base system.

## Recommended Implementation Stages

## Stage 1: Fastest High-Value Result

Goal:

- get the mood on screen quickly with minimal tech risk

Implement:

- `background=forest`
- one full-screen `forest.frag`
- layered spore haze
- distant canopy silhouettes from noise masks
- foreground occluder masks
- restrained god rays

This will already establish a strong mood, even before true generated flora exists.

## Stage 2: Flora Prepass

Goal:

- add iconic giant structures

Implement:

- low-res `forest_flora` render target
- deterministic seeded trunk/cap/tendril generator
- world-space tiling
- alpha and emissive channels for later composite

This is the stage where the biome starts feeling special rather than just well-shaded.

## Stage 3: Structured Generation

Goal:

- make flora look authored and biologically plausible

Implement:

- L-system grammar library
- space-colonization generator for giant forms
- membrane bridges and vein fans
- cap translucency lighting

This is where the environment becomes worthy of being a signature level type.

## Stage 4: Hero Particles And Set Pieces

Goal:

- add the magic and motion that players remember

Implement:

- large spore particles
- set-piece pulses
- nest breathing
- localized biolum bursts
- hazard-linked atmospheric responses

## Performance Guidance

This biome can get expensive fast if everything becomes layered transparency.

Rules to keep it under control:

- keep the large-form flora in one low-res prepass when possible
- cap FBM octaves at `3..5`
- prefer tiled world-space determinism over thousands of dynamic sprites
- reserve high-frequency detail for near field only
- use a small number of god-ray samples
- keep overdraw low in the center combat band

Most of the visual payoff should come from:

- silhouette
- compositing
- color grading
- slow motion

not from brute-force detail.

## Best Bet For "Visually Stunning" In Practice

If the goal is the strongest result for the effort, the most effective stack is:

1. giant generated fungal silhouettes in a low-res flora prepass
2. a full-screen forest shader for spore haze, depth, and grading
3. translucent membrane rim lighting on selected structures
4. a small near-field particle layer for hero spores
5. strong foreground framing

L-systems should help drive the skeletons of the megaflora, but they should not be the whole solution. The beauty will come from how those skeletons are widened, layered, backlit, fogged, and pulsed.

## Concrete Recommendation

If you want one implementation path to pursue first, pursue this:

1. Add a new `forest` background family beside `underwater`, `fire`, and `ice`.
2. Build `forest.frag` first to establish haze, palette, god rays, and distant canopy.
3. Reuse the underwater low-res auxiliary-pass pattern to create `forest_flora.frag`.
4. Generate tileable flora descriptors from either:
   - a small L-system library, or
   - a space-colonization generator for large trunks plus simpler procedural caps.
5. Composite the flora texture with spore fog and membrane rim lighting.

That route is technically aligned with the current engine, visually ambitious, and realistic to iterate without needing a huge hand-painted asset pipeline.
