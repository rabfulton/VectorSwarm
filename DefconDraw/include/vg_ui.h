#ifndef VG_UI_H
#define VG_UI_H

#ifdef __cplusplus
extern "C" {
#endif

#include "vg.h"

typedef struct vg_ui_slider_item {
    const char* label;
    float value_01;
    float value_display;
    int selected;
} vg_ui_slider_item;

typedef struct vg_ui_slider_panel_metrics {
    float pad_left_px;
    float pad_top_px;
    float pad_right_px;
    float pad_bottom_px;
    float title_line_gap_px;
    float rows_top_offset_px;
    float label_col_frac;
    float col_gap_px;
    float value_col_width_px;
    float row_label_height_sub_px;
    float row_slider_y_offset_px;
    float row_slider_height_sub_px;
    float value_y_offset_px;
    float footer_y_from_bottom_px;
    float title_sub_size_delta_px;
    float label_size_bias_px;
    float footer_size_bias_px;
} vg_ui_slider_panel_metrics;

typedef struct vg_ui_slider_panel_desc {
    vg_rect rect;
    const char* title_line_0;
    const char* title_line_1;
    const char* footer_line;
    const vg_ui_slider_item* items;
    size_t item_count;
    float row_height_px;
    float label_size_px;
    float value_size_px;
    float value_text_x_offset_px;
    vg_stroke_style border_style;
    vg_stroke_style text_style;
    vg_stroke_style track_style;
    vg_stroke_style knob_style;
    const vg_ui_slider_panel_metrics* metrics;
} vg_ui_slider_panel_desc;

typedef struct vg_ui_slider_panel_layout {
    vg_vec2 title_line_0_pos;
    vg_vec2 title_line_1_pos;
    vg_vec2 footer_pos;
    float row_start_y;
    float left_x;
    float label_w;
    float slider_x;
    float slider_w;
    float value_x;
} vg_ui_slider_panel_layout;

typedef struct vg_ui_slider_panel_row_layout {
    vg_rect label_rect;
    vg_rect slider_rect;
    vg_vec2 value_pos;
} vg_ui_slider_panel_row_layout;

void vg_ui_slider_panel_default_metrics(vg_ui_slider_panel_metrics* out_metrics);
vg_result vg_ui_slider_panel_compute_layout(
    const vg_ui_slider_panel_desc* desc,
    vg_ui_slider_panel_layout* out_layout
);
vg_result vg_ui_slider_panel_compute_row_layout(
    const vg_ui_slider_panel_desc* desc,
    const vg_ui_slider_panel_layout* layout,
    size_t row_index,
    vg_ui_slider_panel_row_layout* out_row
);
vg_result vg_ui_draw_slider_panel(vg_context* ctx, const vg_ui_slider_panel_desc* desc);

#ifdef __cplusplus
}
#endif

#endif
