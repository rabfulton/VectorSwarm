#ifndef VG_INTERNAL_H
#define VG_INTERNAL_H

#include "vg.h"
#include "vg_palette.h"

typedef enum vg_cmd_type {
    VG_CMD_MOVE_TO,
    VG_CMD_LINE_TO,
    VG_CMD_CUBIC_TO,
    VG_CMD_CLOSE
} vg_cmd_type;

typedef struct vg_path_cmd {
    vg_cmd_type type;
    vg_vec2 p[3];
} vg_path_cmd;

struct vg_path {
    struct vg_context* owner;
    vg_path_cmd* cmds;
    size_t count;
    size_t cap;
};

typedef struct vg_backend_ops {
    void (*destroy)(struct vg_context* ctx);
    vg_result (*begin_frame)(struct vg_context* ctx, const vg_frame_desc* frame);
    vg_result (*end_frame)(struct vg_context* ctx);
    void (*set_retro_params)(struct vg_context* ctx, const vg_retro_params* params);
    void (*set_crt_profile)(struct vg_context* ctx, const vg_crt_profile* profile);
    vg_result (*draw_path_stroke)(struct vg_context* ctx, const struct vg_path* path, const vg_stroke_style* style);
    vg_result (*draw_polyline)(struct vg_context* ctx, const vg_vec2* points, size_t count, const vg_stroke_style* style, int closed);
    vg_result (*fill_convex)(struct vg_context* ctx, const vg_vec2* points, size_t count, const vg_fill_style* style);
    vg_result (*stencil_clear)(struct vg_context* ctx, uint32_t value);
    vg_result (*debug_rasterize_rgba8)(struct vg_context* ctx, uint8_t* pixels, uint32_t width, uint32_t height, uint32_t stride_bytes);
} vg_backend_ops;

typedef struct vg_backend_state {
    const vg_backend_ops* ops;
    void* impl;
} vg_backend_state;

struct vg_context {
    vg_context_desc desc;
    vg_frame_desc frame;
    vg_retro_params retro;
    vg_crt_profile crt;
    vg_palette palette;
    vg_mat2x3 transform;
    vg_mat2x3 transform_stack[32];
    uint32_t transform_stack_count;
    vg_rect clip_stack[32];
    uint32_t clip_stack_count;
    int in_frame;
    vg_backend_state backend;
};

vg_result vg_vk_backend_create(struct vg_context* ctx);
int vg_context_get_clip(const struct vg_context* ctx, vg_rect* out_clip);

#endif
