# Sphere Mode Plan

## Goal

Add a new gameplay and render mode based on a spring-web sphere that fills most of the screen. The player remains visually near the center of the screen on both axes, movement rotates the sphere rather than translating through a flat world, and event-based enemy waves spawn on the reverse side of the sphere relative to the player.

This mode is intentionally closer to the current cylinder levels than to defender:

- player remains screen-centered
- firing remains left/right only
- movement can continue indefinitely in the vertical direction
- wave pacing uses the event-lane timer model already used by cylinder/event levels
- planar-only hazards like asteroid storms are unsupported

The main difference from cylinder is that the non-planar space now wraps on two axes rather than one.

## High-Level Viability

This is viable if implemented as a dedicated gameplay/render mode with its own spatial math. It should not be bolted onto defender or cylinder with scattered conditionals.

The key reason it is viable:

- existing cylinder work already proves the game can support non-planar projection, player-centering, and special-case spawn/audio/runtime logic
- the requested scope avoids the hardest open-world problems by keeping the player centered and keeping weapons constrained to the current left/right model
- event-based spawning is a much better fit than curated waves for this mode

The largest engineering risk is not the sphere rendering. It is maintaining clean gameplay behavior in a second non-planar mode without duplicating too much cylinder logic.

## Proposed Mode Shape

### New Enums / Mode Identity

Add a new gameplay/render identity, likely alongside:

- `LEVEL_STYLE_*`
- `LEVEL_RENDER_*`

Recommended names:

- `LEVEL_STYLE_SPHERE_WEB`
- `LEVEL_RENDER_SPHERE`

This keeps sphere behavior discoverable and lets mode checks stay explicit instead of inferring behavior from background style.

### Supported Features in First Version

Support:

- event-based wave spawning
- event delay / event advance timeout
- boid-style waves as the primary enemy family
- standard player weapons with left/right firing
- spring camera feel
- spring/web sphere background
- sphere-local projectiles and enemy motion
- mine support

Do not support initially:

- asteroid storms
- missile launchers
- arc nodes
- searchlights
- structure collision
- planar spatial hazards
- curated waves

This should be documented as a first-pass capability restriction, not a hidden fallback.

## World Model

### Core Representation

Use sphere-local coordinates as the source of truth, not planar screen coordinates with post-bending.

Recommended runtime basis:

- player orientation on sphere:
  - longitude `lambda`
  - latitude `phi`
- optional sphere angular velocity:
  - `lambda_vel`
  - `phi_vel`
- object surface position:
  - `lambda`
  - `phi`
- object motion in tangent space:
  - `u` velocity along longitude
  - `v` velocity along latitude

Alternative representation:

- unit surface normal / position vector `n`
- tangent basis vectors `t_lon`, `t_lat`

For gameplay code, latitude/longitude is probably easier to debug and serialize. For rendering, convert to 3D unit vectors every frame.

There is no need to clamp away from the poles in the design. The proposed inflated-cube sphere construction avoids the bad vertex distribution of a UV sphere, and orientation math can avoid gimbal lock in the usual way.

### Player Behavior

The player does not physically roam across the screen in the usual sense.

Instead:

- the player remains near screen center
- input modifies sphere orientation
- horizontal input changes longitude / facing bias
- vertical input changes latitude continuously
- spring camera influences visual lag and tilt, not ownership of world-space

The player ship can still have a small screen-space offset and spring response, but the gameplay origin should remain centered.

### Sphere Radius / Scale

The sphere does not need to be large in world-travel terms. It only needs to:

- visually fill most of the viewport
- leave some readable negative space at the screen edges
- permit the front hemisphere to act as the gameplay window

Recommended first-pass target:

- sphere projected radius large enough to cover the full width of a 16:9 frame

That keeps the sphere visually dominant and ensures the mode reads as a full-screen world surface rather than a small orb in the center of the screen.

## Rendering Plan

### Sphere Web Surface

The spring/web part should reuse the existing grid spring simulation and tuning path where practical.

Recommended generation strategy:

1. Build a cube grid or subdivided cube.
2. Normalize each vertex onto a sphere.
3. Connect local grid neighbors with springs or spring-like line segments.
4. Simulate only visible/front-side vertices if performance becomes an issue.

This gives:

- regular controllable topology
- easy spring neighbor relationships
- no polar singularity problems of a UV sphere

Important implementation note:

- the codebase already has a GPU grid spring simulation path and a spring tuning debug system
- sphere mode should reuse those spring tuning parameters and controls rather than introducing a second unrelated tuning system
- the sphere mesh should use the same level-driven mesh/tuning inputs where practical, only changing the projection/topology from flat grid to inflated-cube sphere
- if performance requires it, the reverse hemisphere spring simulation can be reduced or skipped, as long as the visible/front hemisphere remains stable and coherent

### Voronoi Sphere Grid

True spherical Voronoi is possible, but the project probably does not need exact math for the first version.

Recommended practical approach:

1. Generate seed points directly on the sphere surface.
2. Assign each mesh vertex to its nearest seed using angular distance or dot-product distance.
3. Treat edges between vertices of differing seed ownership as Voronoi borders.
4. Render only the front hemisphere borders.

This is effectively a mesh-approximated spherical Voronoi, which should be good enough visually.

Why this approach is preferred:

- it reuses the same front-half mesh already needed for the spring sphere
- avoids implementing exact spherical Voronoi clipping/polygon construction up front
- lets the Voronoi look deform with the spring lattice if desired

### Back-Half Culling

The back half does not need to be rendered.

Recommended culling rule:

- compute camera-facing dot product for each vertex / edge
- only render edges whose midpoint or both endpoints are on the visible hemisphere

Optional:

- fade near the silhouette to soften edge popping

This is simpler than full hidden-line removal and should be adequate for the intended web/grid look.

### Spring Camera

Keep a spring camera feel, but redefine what it acts on.

Instead of a flat camera chasing `camera_x/camera_y`, the spring system should operate on:

- displayed sphere orientation
- displayed ship offset/tilt
- displayed aim/facing bias

This preserves the responsive feel without turning the player into a free-moving viewport anchor.

## Spawning Plan

### Event-Based Only

This mode should use the existing auto-event lane model in `game_update_wave_spawning(...)` as the basis.

That means:

- use `lvl->events[]`
- honor per-event `delay_s`
- honor `event_wave_spawn_timeout_factor`
- use the same event progression logic as current cylinder/event levels

Curated waves are intentionally out of scope.

### Reverse-Side Spawn Rule

Waves should spawn on the far side of the sphere relative to the player.

Recommended definition:

- compute player forward surface position/orientation
- derive an antipodal spawn center
- spawn wave members within a band around that reverse-side region

For first-pass simplicity:

- choose a reverse hemisphere cap centered on the antipode
- spawn enemies with small angular spread around that cap

This is enough to preserve the intended feeling that enemies emerge from the unseen side and curve into view.

### Wave Shape Adaption

Event-wave kinds can stay the same at the level-data layer, but their spawn interpretation must become sphere-aware.

Recommended first-pass support mapping:

- `WAVE_SWARM`, `WAVE_SWARM_FISH`, `WAVE_SWARM_FIREFLY`, `WAVE_SWARM_BIRD`: supported
- `WAVE_SINE`, `WAVE_V`: optional only after sphere-path equivalents exist
- `KAMIKAZE`: optional later
- `ASTEROID_STORM`: unsupported

Boids are the best foundation because they already tolerate looser formation logic and can be made to look natural on curved paths.

## Enemy Motion Plan

### Boids First

Boids should be the primary supported enemy family for sphere mode.

Reason:

- they already support steer/goal dynamics
- they do not depend on rigid left-to-right planar formations
- they can be adapted to a tangent-space steering model without changing their high-level identity

### Sphere-Local Steering

For sphere mode, boid motion should be computed in sphere-local tangent space:

1. Represent each enemy by surface position and tangent velocity.
2. Compute local steering forces in tangent space.
3. Advance angular/surface position.
4. Renormalize back onto the sphere.
5. Rebuild tangent basis.

This avoids the common failure mode where planar velocity produces visible sliding off the surface.

### Front-Hemisphere Lifecycle

Enemies need explicit policy for what happens when they pass behind the sphere:

- remain simulated even when not rendered
- continue moving on the sphere
- despawn only via normal gameplay rules or explicit sphere-mode culling rules

Confirmed first-pass policy:

- enemies continue to exist and simulate normally on the full sphere
- only front-hemisphere objects are rendered
- no special enemy culling just because an enemy is on the back side

This keeps spawning and re-entry behavior coherent while ensuring nothing pops into existence visibly on the front side.

## Projectile Plan

### Player Bullets

Player bullets should remain conceptually left/right weapons, but their travel path must curve around the sphere.

Recommended model:

- bullet starts at ship tangent basis
- initial direction comes from left/right firing orientation in local tangent space
- each tick advances along a very shallow offset shell above the sphere
- bullet pose is reprojected every frame

The important rule is that bullets should follow the local geometry, not move as straight screen-space lines.

Decision:

- use a very shallow shell above the surface rather than strict surface-only bullets
- the shell should still visually inherit spring distortion from the underlying sphere/web

### Enemy Bullets

Enemy bullets should follow the same sphere-local projectile rules.

Simplest first pass:

- all bullets travel along a tangent vector and remain on a common shell around the sphere

Later, if needed:

- allow enemy bullets to arc slightly inward/outward for readability

## Collision / Gameplay Space

Use sphere-local nearest-distance logic, not planar screen distance, for gameplay interactions.

Required systems:

- enemy-player distance
- bullet-enemy
- bullet-player
- pickup/player if pickups are later added
- audio spatialization if this mode ends up with strong directional sound needs

For most of these, angular distance on the sphere plus optional shell-radius offset should be the basis.

## UI / Camera Rules

### Player Lock

Player should be mostly locked to center on both axes.

Recommended first pass:

- player screen anchor fixed at center
- small visual spring offset only
- no large camera pans

### Facing / Shooting

Weapons remain left/right only, same as current defender/cylinder expectations.

That avoids needing:

- omnidirectional aim UI
- radial fire logic
- target reticle redesign

## Audio Considerations

Sphere mode will eventually need its own spatialization logic, but this is not a blocker for a first visual/gameplay prototype.

Recommended first pass:

- keep the existing front-facing screen-space audio approximation
- do not attempt full spherical audio until gameplay is proven

If needed later:

- derive pan from projected screen x
- derive gain from angular distance from the player-facing center

## Suggested Runtime Refactor

To avoid repeating the cylinder mistake of spreading mode conditionals everywhere, introduce explicit helpers for non-planar gameplay.

Recommended helpers:

- `level_uses_sphere(...)`
- `spawn_space_for_level(...)`
- `project_world_to_screen_*`
- `enemy_spawn_next_wave_*`
- `update_projectiles_*`
- `update_boids_*`

Shared abstractions should be introduced only where behavior is actually shared. Cylinder and sphere are similar, but not identical enough to fake one as the other.

## Implementation Phases

### Phase 1: Render Prototype

Deliverables:

- new `LEVEL_RENDER_SPHERE`
- front-half spring sphere render
- optional mesh-approximated Voronoi borders
- player centered over sphere
- spring camera lag on sphere orientation

No enemies required yet.

### Phase 2: Sphere Navigation

Deliverables:

- sphere orientation state
- up/down continuous movement
- left/right facing/orientation behavior
- sphere-local projection helpers
- player bullets curving correctly

Still no full wave gameplay required.

### Phase 3: Event Waves

Deliverables:

- event-lane support in sphere mode
- reverse-side spawn cap
- event timer / timeout behavior
- boid-only first enemy support

This is the point where the mode becomes playable.

### Phase 4: Sphere Boids

Deliverables:

- sphere-local boid update path
- front-hemisphere rendering and full-sphere simulation
- hit detection and death flow

Optional after that:

- additional enemy archetypes

### Phase 5: Polish / Secondary Systems

Deliverables:

- improved Voronoi deformation response
- sphere-specific audio spatialization
- extra supported wave families if worthwhile
- editor exposure and level config authoring support

## Editor / Data Plan

For first pass, sphere mode probably needs only:

- new render style
- maybe new background style if the sphere background is considered distinct from grid/web
- normal event lane support
- no curated support
- reuse existing spring mesh/tuning parameters from the current grid/web system where possible

Potential future config keys:

- sphere radius scale
- spring stiffness / damping
- Voronoi seed count
- reverse-side spawn spread
- shell depth for enemies/projectiles

These should be added only if the defaults are not sufficient.

## Recommended First Level Content

Initial sphere prototype level should use:

- event lane only
- boid or fish/firefly/bird swarm variants only
- no asteroid storms
- no structure terrain
- mines allowed
- no missiles

This keeps the feature set narrow while still giving enemy variety.

## Remaining Questions

These are the main questions still open before implementation starts:

1. Should sphere mode be a completely new background style as well as a new render style, or should it reuse the existing grid/web family with sphere-specific rendering rules?

2. Sphere motion/orientation should be quaternion-first.
   This is the preferred approach because enemies and projectiles need to remain tangent/parallel to the surface cleanly.

3. Voronoi borders should be bound to the deformed mesh so they wobble with the spring sphere.
   They should not be fully recomputed every frame from a new Voronoi solve.

4. The existing cylinder event timer UI/logic can be reused as-is.

## Recommendation

Proceed with a prototype in this order:

1. front-half spring sphere render
2. player-centered sphere orientation and movement
3. sphere-local projectile path
4. event-lane reverse-side spawns
5. boid-only sphere enemy motion

If those five steps feel good, the mode is worth continuing. If they do not, the failure will be clear early and confined to a prototype branch of work rather than a broad engine rewrite.
