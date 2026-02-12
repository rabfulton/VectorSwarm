#ifndef VG_SVG_H
#define VG_SVG_H

#ifdef __cplusplus
extern "C" {
#endif

#include "vg.h"

typedef struct vg_svg_asset vg_svg_asset;

typedef struct vg_svg_load_params {
    float curve_tolerance_px;
    float dpi;
    const char* units;
} vg_svg_load_params;

typedef struct vg_svg_draw_params {
    vg_rect dst;
    int preserve_aspect;
    int flip_y;
    int fill_closed_paths;
    int use_source_colors;
    float fill_intensity;
    float stroke_intensity;
    int use_context_palette;
    const vg_color* palette;
    uint32_t palette_count;
} vg_svg_draw_params;

vg_result vg_svg_load_from_file(
    const char* file_path,
    const vg_svg_load_params* params,
    vg_svg_asset** out_asset
);
void vg_svg_destroy(vg_svg_asset* asset);

vg_result vg_svg_get_bounds(const vg_svg_asset* asset, vg_rect* out_bounds);

vg_result vg_svg_draw(
    vg_context* ctx,
    const vg_svg_asset* asset,
    const vg_svg_draw_params* params,
    const vg_stroke_style* style
);

#ifdef __cplusplus
}
#endif

#endif
