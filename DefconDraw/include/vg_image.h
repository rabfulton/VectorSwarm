#ifndef VG_IMAGE_H
#define VG_IMAGE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "vg.h"

typedef enum vg_image_style_kind {
    VG_IMAGE_STYLE_MONO_SCANLINE = 0,
    VG_IMAGE_STYLE_BLOCK_GRAPHICS = 1
} vg_image_style_kind;

typedef struct vg_image_desc {
    const uint8_t* pixels_rgba8;
    uint32_t width;
    uint32_t height;
    uint32_t stride_bytes;
} vg_image_desc;

typedef struct vg_image_style {
    vg_image_style_kind kind;
    float threshold;
    float contrast;
    float scanline_pitch_px;
    float min_line_width_px;
    float max_line_width_px;
    float line_jitter_px;
    float cell_width_px;
    float cell_height_px;
    int block_levels;
    float intensity;
    vg_color tint_color;
    vg_blend_mode blend;
    int use_crt_palette;
    int use_context_palette;
    int palette_index;
    int invert;
    int use_boxed_glyphs;
} vg_image_style;

vg_result vg_draw_image_stylized(
    vg_context* ctx,
    const vg_image_desc* src,
    vg_rect dst,
    const vg_image_style* style
);

#ifdef __cplusplus
}
#endif

#endif
