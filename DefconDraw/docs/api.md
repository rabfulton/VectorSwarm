# API Reference

This document describes the current public API in `include/vg.h`, `include/vg_palette.h`, `include/vg_pointer.h`, `include/vg_ui.h`, `include/vg_ui_ext.h`, `include/vg_image.h`, `include/vg_text_layout.h`, and `include/vg_text_fx.h`.

## Versioning

- `VG_VERSION_MAJOR`, `VG_VERSION_MINOR`, `VG_VERSION_PATCH`
- Current version in tree: `0.1.0`
- The API is pre-1.0 and may change.

## Core Concepts

- `vg_context`: top-level renderer state and backend binding.
- `vg_path`: retained path command buffer (`move`, `line`, `cubic`, `close`).
- Frame lifecycle: all draw calls must happen between `vg_begin_frame` and `vg_end_frame`.
- Backends: currently `VG_BACKEND_VULKAN`.

## Result Codes

- `VG_OK`: success.
- `VG_ERROR_INVALID_ARGUMENT`: invalid pointer/state/value.
- `VG_ERROR_OUT_OF_MEMORY`: allocation failure.
- `VG_ERROR_BACKEND`: backend/Vulkan-specific failure.
- `VG_ERROR_UNSUPPORTED`: feature not available in current backend/config.

Use `vg_result_string(vg_result)` for diagnostics.

## Data Types

### `vg_vec2`

2D point in pixel space.

### `vg_color`

RGBA color in normalized floats `[0, 1]`.

### `vg_rect`

Axis-aligned rectangle in pixel space.

- `x`, `y`: top-left corner.
- `w`, `h`: width/height in pixels.

### `vg_mat2x3`

2D affine transform matrix:
- row0: `m00 m01 m02`
- row1: `m10 m11 m12`

Point transform:
- `x' = m00*x + m01*y + m02`
- `y' = m10*x + m11*y + m12`

### `vg_text_align`

- `VG_TEXT_ALIGN_LEFT`
- `VG_TEXT_ALIGN_CENTER`
- `VG_TEXT_ALIGN_RIGHT`

### `vg_stroke_style`

- `width_px`: stroke width in pixels; must be `> 0`.
- `intensity`: emission multiplier; must be `>= 0`.
- `color`: base stroke color.
- `cap`: `VG_LINE_CAP_BUTT | VG_LINE_CAP_ROUND | VG_LINE_CAP_SQUARE`.
- `join`: `VG_LINE_JOIN_MITER | VG_LINE_JOIN_ROUND | VG_LINE_JOIN_BEVEL`.
- `miter_limit`: must be `> 0`.
- `blend`: `VG_BLEND_ALPHA | VG_BLEND_ADDITIVE`.
- `stencil`: optional per-draw stencil state (`enabled == 0` disables stencil).

### `vg_stencil_state`

- `enabled`: non-zero enables stencil test/write for the draw.
- `compare_op`: `VG_COMPARE_*` compare function.
- `fail_op`, `pass_op`, `depth_fail_op`: `VG_STENCIL_OP_*` operations.
- `reference`: compare reference value.
- `compare_mask`: compare mask.
- `write_mask`: stencil write mask.

### `vg_frame_desc`

- `width`, `height`: render target size in pixels.
- `delta_time_s`: frame delta, optional for effects/time-based systems.
- `command_buffer`: backend command stream hook.

For Vulkan backend:
- Pass a valid recording `VkCommandBuffer` cast to `void*` when using GPU submission path.
- Leave `NULL` to skip Vulkan draw recording and use CPU debug rasterization APIs only.

### `vg_retro_params`

- `bloom_strength`
- `bloom_radius_px`
- `persistence_decay`
- `jitter_amount`
- `flicker_amount`

These currently drive preview-path retro effects and are also stored in context/backend state.

### `vg_crt_profile`

Unified display-look profile for retro tuning.

- Beam: `beam_core_width_px`, `beam_halo_width_px`, `beam_intensity`
- Glow: `bloom_strength`, `bloom_radius_px`
- Temporal: `persistence_decay`, `jitter_amount`, `flicker_amount`
- Screen: `vignette_strength`, `barrel_distortion`, `scanline_strength`, `noise_strength`

`vg_retro_params` remains supported as a compatibility subset.

### `vg_crt_preset`

- `VG_CRT_PRESET_CLEAN_VECTOR`
- `VG_CRT_PRESET_WOPR`
- `VG_CRT_PRESET_HEAVY_CRT`

## Palette API (`vg_palette.h`)

### Types

- `VG_PALETTE_MAX_ENTRIES` (`64`)
- `VG_PALETTE_NAME_MAX` (`24`)
- `vg_palette_entry`: `{ color, name }`
- `vg_palette`: fixed-capacity palette table with `count`

### Palette Utility Functions

- `vg_palette_init(vg_palette* palette)`
- `vg_palette_make_wopr(vg_palette* palette)`
- `vg_palette_set_entry(vg_palette* palette, uint32_t index, vg_color color, const char* name)`
- `vg_palette_set_color(vg_palette* palette, uint32_t index, vg_color color)`
- `vg_palette_set_name(vg_palette* palette, uint32_t index, const char* name)`
- `vg_palette_get_color(const vg_palette* palette, uint32_t index, vg_color* out_color)`
- `vg_palette_get_name(const vg_palette* palette, uint32_t index)`
- `vg_palette_find(const vg_palette* palette, const char* name, uint32_t* out_index)`

Notes:
- `set_*` auto-grows `count` up to the highest written index.
- `find` is case-insensitive on entry names.

### Context Palette Functions

- `vg_set_palette(vg_context* ctx, const vg_palette* palette)`
- `vg_get_palette(vg_context* ctx, vg_palette* out_palette)`

New contexts default to a `WOPR`-style green ramp palette.

## Pointer API (`vg_pointer.h`)

### `vg_pointer_style`

- `VG_POINTER_NONE`
- `VG_POINTER_ASTEROIDS`
- `VG_POINTER_CROSSHAIR`

### `vg_pointer_desc`

- `position`: screen position in pixels.
- `size_px`: pointer size.
- `angle_rad`: style rotation (used by `ASTEROIDS`, also ring phase basis for `CROSSHAIR`).
- `phase`: optional animation phase/time value.
- `stroke`: stroke style.
- `fill`: fill style for optional center fill.
- `use_fill`: when non-zero, style may render a center fill element.

### `vg_draw_pointer(vg_context* ctx, vg_pointer_style style, const vg_pointer_desc* desc)`

Draws a built-in vector pointer style at `desc->position`.

### `vg_backend_vulkan_desc`

All Vulkan handles are passed as opaque `void*` in the public header and interpreted internally as Vulkan handles.

- `instance`: `VkInstance`
- `physical_device`: `VkPhysicalDevice`
- `device`: `VkDevice`
- `graphics_queue`: `VkQueue`
- `graphics_queue_family`: queue family index used for command pool/binding assumptions.
- `render_pass`: `VkRenderPass` used by internal pipeline path.
- `vertex_binding`: vertex binding index for internally bound vertex buffer.
- `max_frames_in_flight`: currently stored and defaulted; future frame resource sizing hook.
- `raster_samples`: Vulkan sample count for internal pipeline rasterization (`1/2/4/8/16/32/64`). Defaults to `1` if unset/invalid.
- `has_stencil_attachment`: non-zero when the active subpass has a stencil-capable depth/stencil attachment.

## Context API

### `vg_context_create(const vg_context_desc* desc, vg_context** out_ctx)`

Creates a context.

Requirements:
- `desc != NULL`
- `out_ctx != NULL`
- `desc->backend` supported (`VG_BACKEND_VULKAN`)

Returns:
- `VG_OK` on success.
- `VG_ERROR_BACKEND` if backend init fails.
- `VG_ERROR_UNSUPPORTED` for unsupported backend.

### `vg_context_destroy(vg_context* ctx)`

Destroys context and backend resources.

Behavior:
- Accepts `NULL`.
- For Vulkan backend, tears down internal GPU resources/pipelines if initialized.

## Frame API

### `vg_begin_frame(vg_context* ctx, const vg_frame_desc* frame)`

Begins a frame.

Requirements:
- `ctx != NULL`, `frame != NULL`
- `frame->width > 0`, `frame->height > 0`
- no active frame already

### `vg_end_frame(vg_context* ctx)`

Ends a frame and submits backend work.

For Vulkan backend:
- validates recorded draw ranges
- if Vulkan path is configured and `frame.command_buffer` is non-null:
  - uploads staged vertex data to a host-visible `VkBuffer`
  - binds vertex buffer
  - sets viewport/scissor
  - records `vkCmdDraw` for each recorded draw
  - uses internal pipeline path when available

### `vg_stencil_clear(vg_context* ctx, uint32_t value)`

Clears stencil to `value` in the active frame.

Requirements:
- active frame
- backend stencil support enabled

### Stencil Helper Functions

- `vg_stencil_state_init(vg_stencil_state* out_state)`: initialize to disabled/default-safe state.
- `vg_stencil_state_disabled(void)`: returns disabled/default-safe stencil state.
- `vg_stencil_state_make_write_replace(uint32_t reference, uint32_t write_mask)`: helper for writing a stencil mask.
- `vg_stencil_state_make_test_equal(uint32_t reference, uint32_t compare_mask)`: helper for masked draw reads.

### `vg_clip_push_rect(vg_context* ctx, vg_rect rect)`

Pushes an axis-aligned clip rectangle onto the clip stack.

Requirements:
- active frame
- finite `rect` values
- `rect.w > 0`, `rect.h > 0`

Notes:
- The pushed clip is transformed by the current transform and stored as screen-space AABB.
- Nested clips are intersected with the current top-of-stack clip.

### `vg_clip_pop(vg_context* ctx)`

Pops one clip rectangle from the clip stack.

Requirements:
- active frame
- non-empty clip stack

### `vg_clip_reset(vg_context* ctx)`

Clears all active clips. Called automatically by `vg_begin_frame`.

## Retro Parameters API

### `vg_set_retro_params(vg_context* ctx, const vg_retro_params* params)`

Stores and forwards retro params to backend state.

### `vg_get_retro_params(vg_context* ctx, vg_retro_params* out_params)`

Reads current retro params.

### `vg_make_crt_profile(vg_crt_preset preset, vg_crt_profile* out_profile)`

Builds a profile from a preset.

### `vg_set_crt_profile(vg_context* ctx, const vg_crt_profile* profile)`

Stores and forwards full CRT profile to backend state.

### `vg_get_crt_profile(vg_context* ctx, vg_crt_profile* out_profile)`

Reads current CRT profile.

## Transform API

### `vg_transform_reset(vg_context* ctx)`

Resets current transform to identity and clears push/pop stack.

### `vg_transform_push(vg_context* ctx)`
### `vg_transform_pop(vg_context* ctx)`

Saves/restores current transform.

Notes:
- Stack depth is currently fixed (`32`).
- `vg_transform_push` returns `VG_ERROR_OUT_OF_MEMORY` on overflow.
- `vg_transform_pop` returns `VG_ERROR_INVALID_ARGUMENT` on underflow.

### `vg_transform_set(vg_context* ctx, vg_mat2x3 m)`
### `vg_transform_get(vg_context* ctx)`

Sets/gets current affine transform.

### `vg_transform_translate(vg_context* ctx, float tx, float ty)`
### `vg_transform_scale(vg_context* ctx, float sx, float sy)`
### `vg_transform_rotate(vg_context* ctx, float radians)`

Post-multiplies current transform with translate/scale/rotate operations.

Behavior:
- Applied to all geometry draw calls (`path/polyline/fill/text/ui`) in current frame.
- Transform resets to identity at each `vg_begin_frame`.

## Path API

### `vg_path_create(vg_context* ctx, vg_path** out_path)`

Creates a path object owned by `ctx`.

### `vg_path_destroy(vg_path* path)`

Destroys a path and its internal command buffer.

### `vg_path_clear(vg_path* path)`

Clears path commands.

### `vg_path_move_to(vg_path* path, vg_vec2 p)`
### `vg_path_line_to(vg_path* path, vg_vec2 p)`
### `vg_path_cubic_to(vg_path* path, vg_vec2 c0, vg_vec2 c1, vg_vec2 p1)`
### `vg_path_close(vg_path* path)`

Appends commands.

Current backend behavior notes:
- `move_to` starts a new subpath.
- `close` flushes a closed subpath for stroke generation.
- cubic curves are flattened with fixed subdivision.

## Draw API

### `vg_draw_polyline(vg_context* ctx, const vg_vec2* points, size_t count, const vg_stroke_style* style, int closed)`

Records a stroked polyline into backend draw buffers.

Requirements:
- active frame
- `points != NULL`, `count >= 2`
- valid `style`

### `vg_draw_path_stroke(vg_context* ctx, const vg_path* path, const vg_stroke_style* style)`

Strokes a `vg_path` by flattening commands into one or more polylines.

Requirements:
- active frame
- `path` belongs to `ctx`
- valid `style`

### `vg_fill_style`

Fill style for solid geometry:
- `intensity`
- `color`
- `blend`
- `stencil`

### `vg_fill_convex(vg_context* ctx, const vg_vec2* points, size_t count, const vg_fill_style* style)`

Fills a convex polygon (triangle fan triangulation).

Requirements:
- active frame
- `points != NULL`, `count >= 3`
- valid `style`

### `vg_fill_rect(vg_context* ctx, vg_rect rect, const vg_fill_style* style)`

Fills an axis-aligned rectangle.

### `vg_fill_circle(vg_context* ctx, vg_vec2 center, float radius_px, const vg_fill_style* style, int segments)`

Fills a circle via convex polygon approximation.

Requirements:
- `radius_px > 0`
- `segments` in `[8, 512]`

### `vg_measure_text(const char* text, float size_px, float letter_spacing_px)`

Returns text width in pixels for the built-in stroke font.

Notes:
- Supports ASCII-ish UI text (uppercase letters, digits, punctuation used by demo UI).
- Lowercase input is normalized to uppercase.
- Supports `\n` for multi-line width calculation (returns max line width).

### `vg_measure_text_boxed(const char* text, float size_px, float letter_spacing_px)`

Width measurement for the boxed title variant of the built-in stroke font.

### `vg_measure_text_wrapped(const char* text, float size_px, float letter_spacing_px, float wrap_width_px, size_t* out_line_count)`

Measures wrapped width and optional line count for fixed-width wrapping.

### `vg_draw_text(vg_context* ctx, const char* text, vg_vec2 origin, float size_px, float letter_spacing_px, const vg_stroke_style* style, float* out_width_px)`

Renders stroke text using the built-in line font.

Requirements:
- active frame
- valid `style`
- `text != NULL`
- `size_px > 0`

Notes:
- `origin` is text top-left anchor.
- `out_width_px` is optional and reports rendered width.

### `vg_draw_text_boxed(vg_context* ctx, const char* text, vg_vec2 origin, float size_px, float letter_spacing_px, const vg_stroke_style* style, float* out_width_px)`

Draws the boxed title variant of the built-in font.

Notes:
- Intended for headings and status lines.
- Renders an outlined/block-like pass of each glyph (outer contour + inner cutout + crisp edge).

### `vg_draw_text_boxed_weighted(vg_context* ctx, const char* text, vg_vec2 origin, float size_px, float letter_spacing_px, const vg_stroke_style* style, float weight, float* out_width_px)`

Weighted boxed-outline variant.

Notes:
- `weight` controls chunkiness of the outline/cutout passes.
- Values are clamped internally to `[0.25, 3.0]`.

### `vg_draw_text_vector_fill(vg_context* ctx, const char* text, vg_vec2 origin, float size_px, float letter_spacing_px, const vg_stroke_style* style, float* out_width_px)`

Draws stroke-font text with a filled/vector-phosphor look using layered body/fill/edge passes.

### `vg_draw_text_stencil_cutout(vg_context* ctx, const char* text, vg_vec2 origin, float size_px, float letter_spacing_px, const vg_fill_style* panel_fill, const vg_stroke_style* panel_border, const vg_stroke_style* text_style, float* out_width_px)`

Draws text as a cutout from a filled panel:
- fills and outlines a panel behind the text
- cuts the glyph interior with alpha-black text
- adds a crisp edge pass for readability

### `vg_draw_text_wrapped(vg_context* ctx, const char* text, vg_rect bounds, float size_px, float letter_spacing_px, vg_text_align align, const vg_stroke_style* style, float* out_height_px)`

Draws wrapped text constrained to a rectangle.

Behavior:
- Wraps at character boundaries when line width exceeds `bounds.w`.
- Supports explicit newline breaks.
- Aligns each line using `VG_TEXT_ALIGN_LEFT|CENTER|RIGHT`.
- Stops drawing once lines exceed `bounds.h`.

### `vg_draw_rect(vg_context* ctx, vg_rect rect, const vg_stroke_style* style)`

Draws a stroked rectangle primitive.

Requirements:
- active frame
- `rect.w > 0`, `rect.h > 0`
- valid `style`

### `vg_draw_button(vg_context* ctx, vg_rect rect, const char* label, float label_size_px, const vg_stroke_style* border_style, const vg_stroke_style* text_style, int pressed)`

Draws a stroked vector button (rectangle + centered stroke text).

Requirements:
- active frame
- valid rectangle and styles
- `label != NULL`
- `label_size_px > 0`

### `vg_draw_slider(vg_context* ctx, vg_rect rect, float value_01, const vg_stroke_style* border_style, const vg_stroke_style* track_style, const vg_stroke_style* knob_style)`

Draws a horizontal slider primitive:
- outer rectangle
- horizontal track line
- rectangle knob at normalized `value_01`

Requirements:
- active frame
- valid rectangle and styles
- `value_01` is clamped to `[0, 1]`

## UI Helper API (`vg_ui.h`)

### `vg_ui_slider_item`

One row in a slider panel:
- `label`
- `value_01`
- `value_display`
- `selected`

### `vg_ui_slider_panel_desc`

Panel-level descriptor:
- panel `rect`
- optional `title_line_0`, `title_line_1`, `footer_line`
- `items` + `item_count`
- sizing: `row_height_px`, `label_size_px`, `value_size_px`, `value_text_x_offset_px`
- styles: `border_style`, `text_style`, `track_style`, `knob_style`
- optional `metrics` (`vg_ui_slider_panel_metrics`) for explicit paddings/margins/column sizing

### `vg_ui_draw_slider_panel(vg_context* ctx, const vg_ui_slider_panel_desc* desc)`

Draws a reusable immediate-mode style debug panel with labeled sliders and value readouts.

### `vg_ui_slider_panel_metrics`

Explicit panel layout controls used by draw and hit-test layout queries:
- paddings: `pad_left_px`, `pad_top_px`, `pad_right_px`, `pad_bottom_px`
- title/row spacing: `title_line_gap_px`, `rows_top_offset_px`
- columns: `label_col_frac`, `col_gap_px`, `value_col_width_px`
- row internals: `row_label_height_sub_px`, `row_slider_y_offset_px`, `row_slider_height_sub_px`, `value_y_offset_px`
- footer/text tuning: `footer_y_from_bottom_px`, `title_sub_size_delta_px`, `label_size_bias_px`, `footer_size_bias_px`

### `vg_ui_slider_panel_default_metrics(vg_ui_slider_panel_metrics* out_metrics)`

Fills default metrics matching legacy visual behavior.

### `vg_ui_slider_panel_compute_layout(...)`
### `vg_ui_slider_panel_compute_row_layout(...)`

Layout query API that returns resolved panel/row rectangles and text anchors.
Use these for host-side hit-testing to avoid geometry drift from duplicated formulas.

## Meter API (`vg_ui_ext.h`)

### `vg_ui_meter_mode`

- `VG_UI_METER_CONTINUOUS`
- `VG_UI_METER_SEGMENTED`

### `vg_ui_meter_style`

Style bundle for meter drawing:
- `frame`
- `fill`
- `bg`
- `tick`
- `text`

### `vg_ui_meter_desc`

Meter descriptor:
- placement: `rect`
- value domain: `min_value`, `max_value`, `value`
- presentation: `mode`, `segments`, `segment_gap_px`
- text: `label`, `value_fmt`, `show_value`, `show_ticks`
- scaling: `ui_scale`, `text_scale` (set `<=0` to use `1.0`)

### `vg_ui_meter_linear_layout`
### `vg_ui_meter_radial_layout`

Resolved meter geometry for host-side interaction and labels.

### `vg_ui_meter_linear_layout_compute(...)`
### `vg_ui_meter_radial_layout_compute(...)`

Layout query helpers for linear/radial meters.

### `vg_ui_meter_linear(vg_context* ctx, const vg_ui_meter_desc* desc, const vg_ui_meter_style* style)`

Draws a horizontal linear meter.

### `vg_ui_meter_radial(vg_context* ctx, vg_vec2 center, float radius_px, const vg_ui_meter_desc* desc, const vg_ui_meter_style* style)`

Draws a radial meter with arc/ticks and optional needle/value text.

### `vg_ui_graph_style`

Style bundle for graph drawing:
- `frame`
- `line`
- `bar`
- `grid`
- `text`

### `vg_ui_graph_desc`

Graph descriptor:
- placement: `rect`
- data: `samples`, `sample_count`
- value domain: `min_value`, `max_value`
- text: `label`
- display flags: `show_grid`, `show_minmax_labels`
- scaling: `ui_scale`, `text_scale` (set `<=0` to use `1.0`)

### `vg_ui_history`

Caller-owned rolling history helper:
- `data`, `capacity`, `count`, `head`

### `vg_ui_history_reset(vg_ui_history* h)`
### `vg_ui_history_push(vg_ui_history* h, float value)`
### `vg_ui_history_linearize(const vg_ui_history* h, float* out, size_t out_cap)`

Convenience helpers for timeline widgets in immediate-mode usage.

### `vg_ui_graph_line(vg_context* ctx, const vg_ui_graph_desc* desc, const vg_ui_graph_style* style)`

Draws a line graph from sample history.

### `vg_ui_graph_bars(vg_context* ctx, const vg_ui_graph_desc* desc, const vg_ui_graph_style* style)`

Draws a bar graph/histogram from sample values.

### `vg_ui_histogram_desc`

Histogram descriptor:
- placement: `rect`
- data: `bins`, `bin_count`
- value domain: `min_value`, `max_value`
- text: `label`, `x_label`, `y_label`
- display flags: `show_grid`, `show_axes`
- scaling: `ui_scale`, `text_scale` (set `<=0` to use `1.0`)

### `vg_ui_histogram(vg_context* ctx, const vg_ui_histogram_desc* desc, const vg_ui_graph_style* style)`

Draws a histogram widget with optional axes and labels.

### `vg_ui_pie_desc`

Pie/donut descriptor:
- placement: `center`, `radius_px`
- data: `values`, `value_count`
- optional per-slice colors: `colors`
- optional per-slice labels: `labels`
- text: `label`
- display flag: `show_percent_labels`
- scaling: `ui_scale`, `text_scale` (set `<=0` to use `1.0`)

### `vg_ui_pie_chart(vg_context* ctx, const vg_ui_pie_desc* desc, const vg_stroke_style* outline_style, const vg_stroke_style* text_style)`

Draws a pie/donut chart with optional percentage labels.

## Image API (`vg_image.h`)

### `vg_image_style_kind`

- `VG_IMAGE_STYLE_MONO_SCANLINE`
- `VG_IMAGE_STYLE_BLOCK_GRAPHICS`

### `vg_image_desc`

Source image descriptor:
- `pixels_rgba8`
- `width`, `height`
- `stride_bytes`

### `vg_image_style`

Image stylization settings:
- `kind`
- tonal controls: `threshold`, `contrast`
- scanline shape: `scanline_pitch_px`, `min_line_width_px`, `max_line_width_px`, `line_jitter_px`
- block graphics: `cell_width_px`, `cell_height_px`, `block_levels` (`2..32`)
- output controls: `intensity`, `tint_color`, `blend`
- palette/polarity flags: `use_crt_palette`, `use_context_palette`, `palette_index`, `invert`

### `vg_draw_image_stylized(vg_context* ctx, const vg_image_desc* src, vg_rect dst, const vg_image_style* style)`

Draws a stylized image inside `dst`.  
Current implementation supports mono scanline rendering where line thickness varies with local luminance.

## SVG API (`vg_svg.h`)

### `vg_svg_asset`

Opaque loaded SVG geometry handle.

### `vg_svg_load_params`

Load/flatten controls:
- `curve_tolerance_px`: bezier flattening tolerance in source SVG units.
- `dpi`: parse DPI (default `96`).
- `units`: parse units (default `"px"`).

### `vg_svg_draw_params`

Draw controls:
- `dst`: output rectangle in screen pixels.
- `preserve_aspect`: when non-zero, preserves source aspect ratio and centers in `dst`.
- `flip_y`: optional Y flip in destination space.
- `fill_closed_paths`: when non-zero, attempts fill passes on closed paths.
- `use_source_colors`: when non-zero, uses SVG fill/stroke colors when available.
- `fill_intensity`, `stroke_intensity`: pass-level intensity scales.
- `use_context_palette`: when non-zero and `palette` is null/empty, quantizes against context palette.
- `palette`, `palette_count`: optional palette quantization for SVG colors.

### `vg_svg_load_from_file(const char* file_path, const vg_svg_load_params* params, vg_svg_asset** out_asset)`

Loads an SVG file and flattens vector paths to polyline data for fast drawing.

### `vg_svg_get_bounds(const vg_svg_asset* asset, vg_rect* out_bounds)`

Returns source-space bounds for the loaded SVG asset.

### `vg_svg_draw(vg_context* ctx, const vg_svg_asset* asset, const vg_svg_draw_params* params, const vg_stroke_style* style)`

Draws the flattened SVG asset using the provided stroke style.
When `use_source_colors` is enabled, `style` is used as a geometric fallback/base (width/caps/join/blend), while colors come from SVG path paints.
Fill rendering uses tessellated triangles, so concave shapes and hole contours are supported according to SVG fill rule (even-odd or non-zero).

### `vg_svg_destroy(vg_svg_asset* asset)`

Releases SVG asset memory.

## Text Layout API (`vg_text_layout.h`)

### `vg_text_draw_mode`

- `VG_TEXT_DRAW_MODE_STROKE`
- `VG_TEXT_DRAW_MODE_BOXED`
- `VG_TEXT_DRAW_MODE_BOXED_WEIGHTED`
- `VG_TEXT_DRAW_MODE_VECTOR_FILL`
- `VG_TEXT_DRAW_MODE_STENCIL_CUTOUT`

### `vg_text_layout_params`

Layout inputs:
- `bounds`
- `size_px`
- `letter_spacing_px`
- `line_height_px` (`<= 0` uses default `size * 1.35`)
- `align` (`LEFT|CENTER|RIGHT`)

### `vg_text_layout_line`

Per-line layout output:
- source text slice (`text_offset`, `text_length`)
- resolved position (`x`, `y`)
- measured width (`width_px`)

### `vg_text_layout`

Caller-owned layout object:
- copied source `text`
- `params`
- `lines` + `line_count`
- `content_width_px`, `content_height_px`

### `vg_text_layout_reset(vg_text_layout* layout)`

Releases layout-owned memory and resets fields.

### `vg_text_layout_build(const char* text, const vg_text_layout_params* params, vg_text_layout* out_layout)`

Builds wrapped/aligned line layout for drawing.

### `vg_text_layout_draw(vg_context* ctx, const vg_text_layout* layout, vg_text_draw_mode mode, const vg_stroke_style* text_style, float boxed_weight, const vg_fill_style* panel_fill, const vg_stroke_style* panel_border)`

Draws a prebuilt layout in any supported text mode.  
For `STENCIL_CUTOUT`, `panel_fill` and `panel_border` must be provided.

## Text FX API (`vg_text_fx.h`)

### `vg_text_fx_typewriter`

Typewriter reveal state:
- `text`
- `visible_chars`
- `timer_s`
- `char_dt_s`

### `vg_text_fx_typewriter_reset(vg_text_fx_typewriter* fx)`

Resets reveal state to zero visible characters.

### `vg_text_fx_typewriter_set_text(vg_text_fx_typewriter* fx, const char* text)`

Assigns source text and resets reveal state.

### `vg_text_fx_typewriter_set_rate(vg_text_fx_typewriter* fx, float char_dt_s)`

Sets time per revealed character (`seconds/char`).

### `vg_text_fx_typewriter_set_beep(vg_text_fx_typewriter* fx, vg_text_fx_beep_fn fn, void* user)`

Sets optional callback invoked when printable characters are revealed.

### `vg_text_fx_typewriter_set_beep_profile(vg_text_fx_typewriter* fx, float base_hz, float step_hz, float dur_s, float amp)`

Configures optional beep parameters passed to the callback.

### `vg_text_fx_typewriter_enable_beep(vg_text_fx_typewriter* fx, int enabled)`

Enables/disables callback-based teletype beep events.

### `vg_text_fx_typewriter_update(vg_text_fx_typewriter* fx, float dt_s)`

Advances reveal state by delta time and returns number of newly revealed chars.

### `vg_text_fx_typewriter_copy_visible(const vg_text_fx_typewriter* fx, char* out, size_t out_cap)`

Copies currently visible prefix into caller buffer and null-terminates.

### `vg_text_fx_marquee`

Horizontal marquee state:
- `text`
- `offset_px`
- `speed_px_s`
- `gap_px`

### `vg_text_fx_marquee_reset(vg_text_fx_marquee* fx)`
### `vg_text_fx_marquee_set_text(vg_text_fx_marquee* fx, const char* text)`
### `vg_text_fx_marquee_set_speed(vg_text_fx_marquee* fx, float speed_px_s)`
### `vg_text_fx_marquee_set_gap(vg_text_fx_marquee* fx, float gap_px)`
### `vg_text_fx_marquee_update(vg_text_fx_marquee* fx, float dt_s)`

State/configuration helpers for scrolling text.

### `vg_text_fx_marquee_draw(...)`

Draws horizontally scrolling text inside a box with optional:
- background fill (`panel_fill`)
- border stroke (`panel_border`)

Supports all text draw modes via `vg_text_draw_mode`.

Notes:
- Uses real clip/scissor through `vg_clip_push_rect`/`vg_clip_pop`.
- No side-mask parameter is required; clipping is handled by the clip stack/scissor path.

## Debug Raster API

### `vg_debug_rasterize_rgba8(vg_context* ctx, uint8_t* pixels, uint32_t width, uint32_t height, uint32_t stride_bytes)`

CPU raster fallback for preview/debug.

Requirements:
- active frame
- `pixels != NULL`
- `width > 0`, `height > 0`
- `stride_bytes >= width * 4`

Behavior:
- rasterizes staged triangles into RGBA8
- applies blend modes
- applies retro effects (bloom/flicker/jitter) based on current params

## Backend Integration: Vulkan

### Minimum setup for GPU draw recording

1. Create Vulkan instance/device/swapchain/render pass in app.
2. Create `vg_context` with `VG_BACKEND_VULKAN` and populate Vulkan handles in `vg_backend_vulkan_desc`.
3. Per frame, pass recording `VkCommandBuffer` through `vg_frame_desc.command_buffer`.
4. Call `vg_begin_frame` -> draw calls -> `vg_end_frame` while render pass is active.

### Internal pipeline path

Internal pipeline is available when all are true:
- Vulkan is found at build time.
- shader tools are available (`glslangValidator`, `xxd`) at build time.
- `render_pass` is provided in backend desc.

If this path is unavailable, app can still bind its own graphics pipeline before `vg_end_frame` draw recording.

### Vertex input contract (internal pipeline)

- binding: `vg_backend_vulkan_desc.vertex_binding`
- location `0`: `vec2` (`VK_FORMAT_R32G32_SFLOAT`)
- primitive topology: triangle list

## Known Limitations

- API/ABI not stabilized yet.
- Curve flattening is fixed-step, not adaptive.
- GPU upload currently uses host-visible memory; no staging+device-local path yet.
- No internal swapchain or render pass management; app owns frame orchestration.
- Persistence in Vulkan output is app-driven (see `examples/demo_vk_sdl.c` for a render-pass `LOAD` + fullscreen fade pattern).
- Demo bloom in Vulkan example uses a post-process composite path (offscreen scene target + bloom target + fullscreen composite).
- Built-in text uses an embedded stroke font table; loading `.ttf` line fonts is not implemented yet.
- Vulkan backend batches by blend mode when submitting recorded draws (alpha pass, then additive pass) and merges contiguous compatible draw commands.

## Quick Usage Skeleton

```c
vg_context* ctx = NULL;
vg_context_desc desc = {0};
desc.backend = VG_BACKEND_VULKAN;
desc.api.vulkan.instance = (void*)instance;
desc.api.vulkan.physical_device = (void*)physical_device;
desc.api.vulkan.device = (void*)device;
desc.api.vulkan.graphics_queue = (void*)graphics_queue;
desc.api.vulkan.graphics_queue_family = graphics_qf;
desc.api.vulkan.render_pass = (void*)render_pass;
desc.api.vulkan.vertex_binding = 0;
desc.api.vulkan.raster_samples = 1; /* use 4 for 4x MSAA render pass */
desc.api.vulkan.has_stencil_attachment = 1; /* set only if render pass subpass has stencil attachment */
vg_context_create(&desc, &ctx);

vg_frame_desc frame = {
    .width = width,
    .height = height,
    .delta_time_s = dt,
    .command_buffer = (void*)cmd
};
vg_begin_frame(ctx, &frame);
vg_draw_polyline(ctx, pts, count, &style, 0);
vg_end_frame(ctx);
```
