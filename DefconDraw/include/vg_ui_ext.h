#ifndef VG_UI_EXT_H
#define VG_UI_EXT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "vg.h"

typedef enum vg_ui_meter_mode {
    VG_UI_METER_CONTINUOUS = 0,
    VG_UI_METER_SEGMENTED = 1
} vg_ui_meter_mode;

typedef struct vg_ui_meter_style {
    vg_stroke_style frame;
    vg_stroke_style fill;
    vg_stroke_style bg;
    vg_stroke_style tick;
    vg_stroke_style text;
} vg_ui_meter_style;

typedef struct vg_ui_meter_desc {
    vg_rect rect;
    float min_value;
    float max_value;
    float value;
    vg_ui_meter_mode mode;
    int segments;
    float segment_gap_px;
    const char* label;
    const char* value_fmt;
    int show_value;
    int show_ticks;
    float ui_scale;
    float text_scale;
} vg_ui_meter_desc;

typedef struct vg_ui_meter_linear_layout {
    vg_rect outer_rect;
    vg_rect inner_rect;
    vg_rect fill_rect;
    vg_vec2 label_pos;
    vg_vec2 value_pos;
} vg_ui_meter_linear_layout;

typedef struct vg_ui_meter_radial_layout {
    vg_vec2 center;
    float radius_px;
    float a0;
    float sweep;
    float tick_inner_radius;
    float tick_outer_radius;
    float needle_radius;
    vg_vec2 value_pos;
    vg_vec2 label_pos;
} vg_ui_meter_radial_layout;

vg_result vg_ui_meter_linear_layout_compute(
    const vg_ui_meter_desc* desc,
    const vg_ui_meter_style* style,
    vg_ui_meter_linear_layout* out_layout
);
vg_result vg_ui_meter_radial_layout_compute(
    vg_vec2 center,
    float radius_px,
    const vg_ui_meter_desc* desc,
    const vg_ui_meter_style* style,
    vg_ui_meter_radial_layout* out_layout
);
vg_result vg_ui_meter_linear(vg_context* ctx, const vg_ui_meter_desc* desc, const vg_ui_meter_style* style);
vg_result vg_ui_meter_radial(vg_context* ctx, vg_vec2 center, float radius_px, const vg_ui_meter_desc* desc, const vg_ui_meter_style* style);

typedef struct vg_ui_graph_style {
    vg_stroke_style frame;
    vg_stroke_style line;
    vg_stroke_style bar;
    vg_stroke_style grid;
    vg_stroke_style text;
} vg_ui_graph_style;

typedef struct vg_ui_graph_desc {
    vg_rect rect;
    const float* samples;
    size_t sample_count;
    float min_value;
    float max_value;
    const char* label;
    int show_grid;
    int show_minmax_labels;
    float ui_scale;
    float text_scale;
} vg_ui_graph_desc;

typedef struct vg_ui_histogram_desc {
    vg_rect rect;
    const float* bins;
    size_t bin_count;
    float min_value;
    float max_value;
    const char* label;
    const char* x_label;
    const char* y_label;
    int show_grid;
    int show_axes;
    float ui_scale;
    float text_scale;
} vg_ui_histogram_desc;

typedef struct vg_ui_pie_desc {
    vg_vec2 center;
    float radius_px;
    const float* values;
    size_t value_count;
    const vg_color* colors;
    const char* const* labels;
    const char* label;
    int show_percent_labels;
    float ui_scale;
    float text_scale;
} vg_ui_pie_desc;

typedef struct vg_ui_history {
    float* data;
    size_t capacity;
    size_t count;
    size_t head;
} vg_ui_history;

void vg_ui_history_reset(vg_ui_history* h);
void vg_ui_history_push(vg_ui_history* h, float value);
size_t vg_ui_history_linearize(const vg_ui_history* h, float* out, size_t out_cap);

vg_result vg_ui_graph_line(vg_context* ctx, const vg_ui_graph_desc* desc, const vg_ui_graph_style* style);
vg_result vg_ui_graph_bars(vg_context* ctx, const vg_ui_graph_desc* desc, const vg_ui_graph_style* style);
vg_result vg_ui_histogram(vg_context* ctx, const vg_ui_histogram_desc* desc, const vg_ui_graph_style* style);
vg_result vg_ui_pie_chart(vg_context* ctx, const vg_ui_pie_desc* desc, const vg_stroke_style* outline_style, const vg_stroke_style* text_style);

#ifdef __cplusplus
}
#endif

#endif
