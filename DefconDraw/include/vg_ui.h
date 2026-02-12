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
    vg_stroke_style border_style;
    vg_stroke_style text_style;
    vg_stroke_style track_style;
    vg_stroke_style knob_style;
} vg_ui_slider_panel_desc;

vg_result vg_ui_draw_slider_panel(vg_context* ctx, const vg_ui_slider_panel_desc* desc);

#ifdef __cplusplus
}
#endif

#endif
