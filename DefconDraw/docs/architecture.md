# Architecture Notes

## Layers

- `vg_core` (`src/vg.c`): public API, handles, command validation, frame sequencing.
- `vg_ui` (`src/vg_ui.c`): reusable immediate-mode style debug UI helpers built from core primitives.
- `vg_ui_ext` (`src/vg_ui_ext.c`): higher-level instrument primitives (meters, graphs, history helpers) for dashboards/HUDs.
- `vg_vk` (`src/backends/vulkan`): GPU resource management and drawing pipelines.
- `vg_fx` (`src/fx`): retro display model passes (bloom, persistence, flicker/jitter).
- CPU debug preview: backend can rasterize staged triangles to RGBA8 for fast iteration.

## API design goals

- Opaque handles in public headers for ABI stability.
- Explicit frame boundaries (`vg_begin_frame` / `vg_end_frame`).
- Style-driven strokes with width/intensity for retro display tuning.
- Lightweight 2D affine transform stack (`push/pop/translate/scale/rotate`) applied in core before backend submission.
- Backend abstraction in `vg_context_desc` for future non-Vulkan targets.

## Client-side profile model

- The library accepts display state through `vg_set_crt_profile`.
- Profile persistence remains client-owned (example app saves/loads profile text files and reapplies through API).
- Demo profile currently stores scene selection, UI visibility, line width, boxed-font weight, and CRT parameters.

## Next technical steps

- Improve curve flattening with adaptive subdivision (currently fixed-step CPU flattening).
- Implement `vg_fx` passes (HDR bloom + persistence history texture).
- Move Vulkan uploads from host-visible memory to staging + device-local buffers.
