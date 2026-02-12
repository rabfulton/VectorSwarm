#ifndef VG_TEXT_LAYOUT_H
#define VG_TEXT_LAYOUT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "vg.h"

typedef enum vg_text_draw_mode {
    VG_TEXT_DRAW_MODE_STROKE = 0,
    VG_TEXT_DRAW_MODE_BOXED = 1,
    VG_TEXT_DRAW_MODE_BOXED_WEIGHTED = 2,
    VG_TEXT_DRAW_MODE_VECTOR_FILL = 3,
    VG_TEXT_DRAW_MODE_STENCIL_CUTOUT = 4
} vg_text_draw_mode;

typedef struct vg_text_layout_params {
    vg_rect bounds;
    float size_px;
    float letter_spacing_px;
    float line_height_px;
    vg_text_align align;
} vg_text_layout_params;

typedef struct vg_text_layout_line {
    size_t text_offset;
    size_t text_length;
    float x;
    float y;
    float width_px;
} vg_text_layout_line;

typedef struct vg_text_layout {
    char* text;
    vg_text_layout_params params;
    vg_text_layout_line* lines;
    size_t line_count;
    float content_width_px;
    float content_height_px;
} vg_text_layout;

void vg_text_layout_reset(vg_text_layout* layout);
vg_result vg_text_layout_build(const char* text, const vg_text_layout_params* params, vg_text_layout* out_layout);
vg_result vg_text_layout_draw(
    vg_context* ctx,
    const vg_text_layout* layout,
    vg_text_draw_mode mode,
    const vg_stroke_style* text_style,
    float boxed_weight,
    const vg_fill_style* panel_fill,
    const vg_stroke_style* panel_border
);

#ifdef __cplusplus
}
#endif

#endif
