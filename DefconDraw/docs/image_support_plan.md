# Image Support Plan

## Goal

Add stylized image support that fits Defcon Draw's vector/CRT aesthetic, starting with `nick.jpg` as the baseline test asset and a dedicated demo screen to compare effects.

## Scope (Phase 1)

- Offline conversion pipeline for raster images.
- Runtime draw path for preprocessed image assets.
- Three initial visual modes:
- `MONO_THRESHOLD`
- `HALFTONE_DOT`
- `SCANLINE_QUANT`
- New Vulkan demo scene that shows side-by-side variants from `nick.jpg`.

Out of scope for phase 1:
- Full-color photo rendering path.
- Runtime JPEG/PNG decode inside core renderer.
- Arbitrary image transforms beyond position/scale/intensity.

## Design Principles

- Keep core renderer focused on vector + draw submission.
- Put image parsing/conversion in tooling (`vg_assets`), not render core.
- Runtime should consume a compact preprocessed format.
- Style controls should match current CRT profile and blend naturally with bloom/persistence.

## Proposed Architecture

## 1) Offline Tool: `vg_imgconv`

Input:
- `nick.jpg` (and later additional JPG/PNG assets)

Output:
- `.vgi` binary asset (single image, preprocessed)
- optional `.json` sidecar for debug metadata

Pipeline steps:
1. Load source image (tool-only dependency: `stb_image`).
2. Convert to luminance.
3. Optional resize to target display resolution.
4. Apply one mode transform:
- threshold (mono)
- ordered/Bayer dither (halftone)
- scanline quantization (banded levels)
5. Pack to compact format.
6. Write asset + metadata.

## 2) Runtime Module: `vg_image`

Public API sketch:

```c
typedef enum vg_image_mode {
    VG_IMAGE_MONO_THRESHOLD = 0,
    VG_IMAGE_HALFTONE_DOT = 1,
    VG_IMAGE_SCANLINE_QUANT = 2
} vg_image_mode;

typedef struct vg_image_style {
    float intensity;
    float contrast;
    float threshold;
    float dither_amount;
    vg_blend_mode blend;
} vg_image_style;

typedef struct vg_image_asset vg_image_asset;

vg_result vg_image_load_vgi(const char* path, vg_image_asset** out_asset);
void vg_image_destroy(vg_image_asset* asset);
vg_result vg_draw_image(vg_context* ctx, const vg_image_asset* asset, vg_rect dst, const vg_image_style* style);
```

Implementation note:
- Phase 1 draw path can submit as a grid of quads/segments (CPU-generated geometry).
- Later optimization: atlas texture path or compute-based decode.

## 3) Demo Scene (new slot)

Add new scene in `examples/demo_vk_sdl.c`:
- Name: `MODE 8 IMAGE FX TEST`
- Source asset: converted `nick.jpg`
- Layout:
- Left: original luminance reference
- Center: mono threshold
- Right: halftone / scanline (toggle key)
- Bottom text: active mode + parameters

Controls:
- `8` switch to image scene
- `M` cycle image mode
- `[`/`]` threshold
- `;`/`'` contrast
- `,`/`.` dither amount

Optional:
- Include current debug UI controls for image params under a section header.

## File/Repo Plan

### New files

- `include/vg_image.h`
- `src/vg_image.c`
- `tools/vg_imgconv.c`
- `docs/image_support_plan.md` (this file)

### Build integration

- Add `vg_image.c` to `vg` target in `CMakeLists.txt`.
- Add `vg_imgconv` tool target in `CMakeLists.txt`.
- Add a sample conversion command to docs:
  - `./build/vg_imgconv nick.jpg assets/nick_mono.vgi --mode mono --width 320`

### Demo integration

- Extend scene enum (`SCENE_COUNT` + new slot 8).
- Update scene text/help lines.
- Load preprocessed `.vgi` at demo startup.

## Milestones

## Milestone 1: Converter foundation

- Implement `vg_imgconv` with mono threshold output.
- Convert `nick.jpg` -> `assets/nick_mono.vgi`.
- Add metadata dump for quick inspection.

Acceptance:
- Converter runs deterministically and outputs stable binary for same input/options.

## Milestone 2: Runtime draw path

- Implement `vg_image_load_vgi` and `vg_draw_image`.
- Draw mono image in demo scene.

Acceptance:
- Image renders correctly under CRT pipeline and window resize.

## Milestone 3: Additional effects

- Add halftone + scanline quant conversion modes.
- Add mode switching in scene 8.

Acceptance:
- Visual difference is obvious and consistent frame-to-frame.

## Milestone 4: Parameterization + docs

- Expose style params in debug UI for scene 8.
- Document API in `docs/api.md`.
- Add screenshots to `screenshots/` + README gallery entry.

Acceptance:
- User can tune image style live and save CRT profile independently.

## Risks and Mitigations

- Risk: image pass looks disconnected from vector style.
- Mitigation: keep monochrome palette, additive blending, and CRT post stack.

- Risk: CPU geometry generation too heavy for large images.
- Mitigation: cap demo asset resolution (e.g., 320x240) and profile first.

- Risk: API lock-in too early.
- Mitigation: keep `vg_image_style` minimal in phase 1; extend later.

## Recommended Next Step

Start Milestone 1 immediately:
1. Add `vg_imgconv` tool target.
2. Implement mono threshold conversion for `nick.jpg`.
3. Commit one generated asset for demo bootstrap.
