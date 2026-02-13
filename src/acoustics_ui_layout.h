#ifndef V_TYPE_ACOUSTICS_UI_LAYOUT_H
#define V_TYPE_ACOUSTICS_UI_LAYOUT_H

#include "render.h"
#include "vg_ui.h"

#include <stddef.h>

typedef struct acoustics_ui_layout {
    vg_rect panel[2];
    vg_rect button[2];
    vg_rect save_button[2];
    vg_rect slot_button[2][ACOUSTICS_SLOT_COUNT];
    float row_y0[2];
    float row_h;
    float slider_x[2];
    float slider_w[2];
    int row_count[2];
    float value_col_width_px;
} acoustics_ui_layout;

float acoustics_compute_value_col_width(
    float ui,
    float value_size_px,
    const float* values,
    size_t value_count
);

vg_ui_slider_panel_metrics acoustics_scaled_slider_metrics(float ui, float value_col_width_px);

acoustics_ui_layout make_acoustics_ui_layout(float w, float h, float value_col_width_px);

#endif
