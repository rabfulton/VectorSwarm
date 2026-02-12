#ifndef VG_POINTER_H
#define VG_POINTER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "vg.h"

typedef enum vg_pointer_style {
    VG_POINTER_NONE = 0,
    VG_POINTER_ASTEROIDS = 1,
    VG_POINTER_CROSSHAIR = 2
} vg_pointer_style;

typedef struct vg_pointer_desc {
    vg_vec2 position;
    float size_px;
    float angle_rad;
    float phase;
    vg_stroke_style stroke;
    vg_fill_style fill;
    int use_fill;
} vg_pointer_desc;

vg_result vg_draw_pointer(vg_context* ctx, vg_pointer_style style, const vg_pointer_desc* desc);

#ifdef __cplusplus
}
#endif

#endif
