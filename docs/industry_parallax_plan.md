# Industry Parallax Implementation Plan (SVG Bake Path)

## Goal

Use `assets/images/industry.svg` as the source of a **filled**, performant defender background with parallax depth, without runtime SVG rendering cost.

## Constraints

- Preserve current defender gameplay/render pipeline behavior.
- Avoid runtime `vg_svg_draw(...)` in gameplay.
- Keep visual style close to current defender industrial look.
- Use SVG-derived geometry for only a subset of layers (not all layers).

## High-Level Approach

1. Offline bake SVG -> generated geometry assets.
2. Load generated geometry at startup.
3. Render a few parallax layers from baked geometry.
4. Keep procedural layers for the rest.
5. Add tuning constants for quick visual iteration.

## Stage 1: Offline Bake Tool

### 1.1 Tool output

Create a generator that emits:

- `src/generated/industry_mesh.h`
  - `industry_vertex` array (2D positions in normalized local space, [0..1] SVG viewbox space).
  - Triangle index array for filled geometry.
  - Optional polyline index ranges for outline accents.
  - Source bounds (`viewbox_w`, `viewbox_h`).

### 1.2 Bake inputs

- Source: `assets/images/industry.svg`
- Parse all closed shapes that contribute to the silhouette/fill.
- Flatten curves to line segments with a controlled tolerance.
- Triangulate each closed polygon robustly (ear clipping or monotone decomposition).

### 1.3 Build integration

- Add bake command to CMake as a pre-build generated-file step.
- Re-run bake only when SVG changes.
- If bake fails: stop build with explicit error (no silent fallback).

## Stage 2: Runtime Data Integration

### 2.1 Renderer data

Add a lightweight runtime mesh handle in renderer scope:

- vertices + indices from generated header (static arrays).
- no dynamic allocation required.

### 2.2 Coordinate convention

- Keep mesh local origin at bottom-left for easy baseline alignment.
- Scale by tile height per layer.
- Compute tile width from baked aspect ratio.

## Stage 3: Defender Parallax Composition

### 3.1 Layer mix

Use 6 total background layers:

- 2 or 3 layers: baked SVG fill mesh (mid/far depth).
- remaining layers: existing procedural industrial primitives (near depth and variation).

### 3.2 Tiling

- Tile each baked layer in world X by `tile_w`.
- Compute visible tile index span from camera and viewport.
- Draw only visible tiles (+1 margin tile each side).

### 3.3 Vertical placement

- Far layers higher, near layers lower (Y-up coordinate system).
- Front-most procedural layer anchored at screen bottom with no base border.

### 3.4 Shading

- Filled mesh uses dimmed theme color + alpha by depth.
- Optional outline accents from baked polylines at reduced intensity.
- Keep overall brightness below current foreground gameplay elements.

## Stage 4: Performance Targets

### 4.1 Success criteria

- Defender background adds minimal GPU cost vs current procedural baseline.
- No obvious frame-time spikes while scrolling.
- No full-screen image post-stylization pass.

### 4.2 Draw-call budget

- Batch by layer where possible.
- Keep baked-layer tile count bounded by viewport.
- Prefer static buffers / static arrays over per-frame geometry rebuilds.

## Stage 5: Visual Tuning Parameters

Expose constants in one block (renderer):

- `k_industry_layer_count_svg`
- `k_industry_layer_count_proc`
- per-layer `parallax`, `y_base`, `scale`, `alpha`, `intensity`.
- flattening tolerance (bake-time only).

These should be the only knobs needed for style iteration.

## Stage 6: Validation Checklist

1. Defender level loads and scrolls with stable FPS.
2. Baked layers tile seamlessly in X (no popping).
3. Baked layers are visibly filled, not outline-only.
4. Near/far depth ordering is visually correct.
5. Foreground gameplay readability remains intact.

## Future Extensions (Not in first pass)

- Multiple baked industrial variants and weighted selection by tile index.
- Per-layer color themes derived from level config.
- Optional animated accents (smoke/pulses) on top of baked silhouette.
