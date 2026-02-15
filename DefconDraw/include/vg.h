#ifndef VG_H
#define VG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

#define VG_VERSION_MAJOR 0
#define VG_VERSION_MINOR 1
#define VG_VERSION_PATCH 0

typedef enum vg_result {
    VG_OK = 0,
    VG_ERROR_INVALID_ARGUMENT = -1,
    VG_ERROR_OUT_OF_MEMORY = -2,
    VG_ERROR_BACKEND = -3,
    VG_ERROR_UNSUPPORTED = -4
} vg_result;

typedef enum vg_line_cap {
    VG_LINE_CAP_BUTT = 0,
    VG_LINE_CAP_ROUND = 1,
    VG_LINE_CAP_SQUARE = 2
} vg_line_cap;

typedef enum vg_line_join {
    VG_LINE_JOIN_MITER = 0,
    VG_LINE_JOIN_ROUND = 1,
    VG_LINE_JOIN_BEVEL = 2
} vg_line_join;

typedef enum vg_blend_mode {
    VG_BLEND_ALPHA = 0,
    VG_BLEND_ADDITIVE = 1
} vg_blend_mode;

typedef enum vg_compare_op {
    VG_COMPARE_NEVER = 0,
    VG_COMPARE_LESS = 1,
    VG_COMPARE_EQUAL = 2,
    VG_COMPARE_LESS_OR_EQUAL = 3,
    VG_COMPARE_GREATER = 4,
    VG_COMPARE_NOT_EQUAL = 5,
    VG_COMPARE_GREATER_OR_EQUAL = 6,
    VG_COMPARE_ALWAYS = 7
} vg_compare_op;

typedef enum vg_stencil_op {
    VG_STENCIL_OP_KEEP = 0,
    VG_STENCIL_OP_ZERO = 1,
    VG_STENCIL_OP_REPLACE = 2,
    VG_STENCIL_OP_INCREMENT_AND_CLAMP = 3,
    VG_STENCIL_OP_DECREMENT_AND_CLAMP = 4,
    VG_STENCIL_OP_INVERT = 5,
    VG_STENCIL_OP_INCREMENT_AND_WRAP = 6,
    VG_STENCIL_OP_DECREMENT_AND_WRAP = 7
} vg_stencil_op;

typedef struct vg_stencil_state {
    int enabled;
    vg_compare_op compare_op;
    vg_stencil_op fail_op;
    vg_stencil_op pass_op;
    vg_stencil_op depth_fail_op;
    uint32_t reference;
    uint32_t compare_mask;
    uint32_t write_mask;
} vg_stencil_state;

typedef struct vg_context vg_context;
typedef struct vg_path vg_path;

typedef struct vg_vec2 {
    float x;
    float y;
} vg_vec2;

typedef struct vg_color {
    float r;
    float g;
    float b;
    float a;
} vg_color;

typedef struct vg_rect {
    float x;
    float y;
    float w;
    float h;
} vg_rect;

typedef enum vg_text_align {
    VG_TEXT_ALIGN_LEFT = 0,
    VG_TEXT_ALIGN_CENTER = 1,
    VG_TEXT_ALIGN_RIGHT = 2
} vg_text_align;

typedef struct vg_mat2x3 {
    float m00;
    float m01;
    float m02;
    float m10;
    float m11;
    float m12;
} vg_mat2x3;

typedef struct vg_stroke_style {
    float width_px;
    float intensity;
    vg_color color;
    vg_line_cap cap;
    vg_line_join join;
    float miter_limit;
    vg_blend_mode blend;
    vg_stencil_state stencil;
} vg_stroke_style;

typedef struct vg_fill_style {
    float intensity;
    vg_color color;
    vg_blend_mode blend;
    vg_stencil_state stencil;
} vg_fill_style;

typedef struct vg_frame_desc {
    uint32_t width;
    uint32_t height;
    float delta_time_s;
    void* command_buffer;
} vg_frame_desc;

typedef struct vg_retro_params {
    float bloom_strength;
    float bloom_radius_px;
    float persistence_decay;
    float jitter_amount;
    float flicker_amount;
} vg_retro_params;

typedef struct vg_crt_profile {
    float beam_core_width_px;
    float beam_halo_width_px;
    float beam_intensity;
    float bloom_strength;
    float bloom_radius_px;
    float persistence_decay;
    float jitter_amount;
    float flicker_amount;
    float vignette_strength;
    float barrel_distortion;
    float scanline_strength;
    float noise_strength;
} vg_crt_profile;

typedef enum vg_crt_preset {
    VG_CRT_PRESET_CLEAN_VECTOR = 0,
    VG_CRT_PRESET_WOPR = 1,
    VG_CRT_PRESET_HEAVY_CRT = 2
} vg_crt_preset;

typedef struct vg_backend_vulkan_desc {
    void* instance;
    void* physical_device;
    void* device;
    void* graphics_queue;
    uint32_t graphics_queue_family;
    void* render_pass;
    uint32_t vertex_binding;
    uint32_t max_frames_in_flight;
    uint32_t raster_samples;
    uint32_t has_stencil_attachment;
} vg_backend_vulkan_desc;

typedef enum vg_backend_type {
    VG_BACKEND_VULKAN = 0
} vg_backend_type;

typedef struct vg_context_desc {
    vg_backend_type backend;
    union {
        vg_backend_vulkan_desc vulkan;
    } api;
} vg_context_desc;

vg_result vg_context_create(const vg_context_desc* desc, vg_context** out_ctx);
void vg_context_destroy(vg_context* ctx);

vg_result vg_begin_frame(vg_context* ctx, const vg_frame_desc* frame);
vg_result vg_end_frame(vg_context* ctx);
vg_result vg_stencil_clear(vg_context* ctx, uint32_t value);
void vg_stencil_state_init(vg_stencil_state* out_state);
vg_stencil_state vg_stencil_state_disabled(void);
vg_stencil_state vg_stencil_state_make_write_replace(uint32_t reference, uint32_t write_mask);
vg_stencil_state vg_stencil_state_make_test_equal(uint32_t reference, uint32_t compare_mask);

void vg_set_retro_params(vg_context* ctx, const vg_retro_params* params);
void vg_get_retro_params(vg_context* ctx, vg_retro_params* out_params);
void vg_make_crt_profile(vg_crt_preset preset, vg_crt_profile* out_profile);
void vg_set_crt_profile(vg_context* ctx, const vg_crt_profile* profile);
void vg_get_crt_profile(vg_context* ctx, vg_crt_profile* out_profile);
void vg_transform_reset(vg_context* ctx);
vg_result vg_transform_push(vg_context* ctx);
vg_result vg_transform_pop(vg_context* ctx);
void vg_transform_set(vg_context* ctx, vg_mat2x3 m);
vg_mat2x3 vg_transform_get(vg_context* ctx);
void vg_transform_translate(vg_context* ctx, float tx, float ty);
void vg_transform_scale(vg_context* ctx, float sx, float sy);
void vg_transform_rotate(vg_context* ctx, float radians);
vg_result vg_clip_push_rect(vg_context* ctx, vg_rect rect);
vg_result vg_clip_pop(vg_context* ctx);
void vg_clip_reset(vg_context* ctx);

vg_result vg_path_create(vg_context* ctx, vg_path** out_path);
void vg_path_destroy(vg_path* path);

void vg_path_clear(vg_path* path);
vg_result vg_path_move_to(vg_path* path, vg_vec2 p);
vg_result vg_path_line_to(vg_path* path, vg_vec2 p);
vg_result vg_path_cubic_to(vg_path* path, vg_vec2 c0, vg_vec2 c1, vg_vec2 p1);
vg_result vg_path_close(vg_path* path);

vg_result vg_draw_path_stroke(vg_context* ctx, const vg_path* path, const vg_stroke_style* style);
vg_result vg_draw_polyline(vg_context* ctx, const vg_vec2* points, size_t count, const vg_stroke_style* style, int closed);
vg_result vg_fill_convex(vg_context* ctx, const vg_vec2* points, size_t count, const vg_fill_style* style);
vg_result vg_fill_rect(vg_context* ctx, vg_rect rect, const vg_fill_style* style);
vg_result vg_fill_circle(vg_context* ctx, vg_vec2 center, float radius_px, const vg_fill_style* style, int segments);

float vg_measure_text(const char* text, float size_px, float letter_spacing_px);
float vg_measure_text_boxed(const char* text, float size_px, float letter_spacing_px);
float vg_measure_text_wrapped(const char* text, float size_px, float letter_spacing_px, float wrap_width_px, size_t* out_line_count);
vg_result vg_draw_text(
    vg_context* ctx,
    const char* text,
    vg_vec2 origin,
    float size_px,
    float letter_spacing_px,
    const vg_stroke_style* style,
    float* out_width_px
);
vg_result vg_draw_text_boxed(
    vg_context* ctx,
    const char* text,
    vg_vec2 origin,
    float size_px,
    float letter_spacing_px,
    const vg_stroke_style* style,
    float* out_width_px
);
vg_result vg_draw_text_boxed_weighted(
    vg_context* ctx,
    const char* text,
    vg_vec2 origin,
    float size_px,
    float letter_spacing_px,
    const vg_stroke_style* style,
    float weight,
    float* out_width_px
);
vg_result vg_draw_text_vector_fill(
    vg_context* ctx,
    const char* text,
    vg_vec2 origin,
    float size_px,
    float letter_spacing_px,
    const vg_stroke_style* style,
    float* out_width_px
);
vg_result vg_draw_text_stencil_cutout(
    vg_context* ctx,
    const char* text,
    vg_vec2 origin,
    float size_px,
    float letter_spacing_px,
    const vg_fill_style* panel_fill,
    const vg_stroke_style* panel_border,
    const vg_stroke_style* text_style,
    float* out_width_px
);
vg_result vg_draw_text_wrapped(
    vg_context* ctx,
    const char* text,
    vg_rect bounds,
    float size_px,
    float letter_spacing_px,
    vg_text_align align,
    const vg_stroke_style* style,
    float* out_height_px
);

vg_result vg_draw_rect(vg_context* ctx, vg_rect rect, const vg_stroke_style* style);
vg_result vg_draw_button(
    vg_context* ctx,
    vg_rect rect,
    const char* label,
    float label_size_px,
    const vg_stroke_style* border_style,
    const vg_stroke_style* text_style,
    int pressed
);
vg_result vg_draw_slider(
    vg_context* ctx,
    vg_rect rect,
    float value_01,
    const vg_stroke_style* border_style,
    const vg_stroke_style* track_style,
    const vg_stroke_style* knob_style
);

vg_result vg_debug_rasterize_rgba8(
    vg_context* ctx,
    uint8_t* pixels,
    uint32_t width,
    uint32_t height,
    uint32_t stride_bytes
);

const char* vg_result_string(vg_result result);

#ifdef __cplusplus
}
#endif

#endif
