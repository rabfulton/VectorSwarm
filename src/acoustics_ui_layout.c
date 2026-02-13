#include "acoustics_ui_layout.h"

#include "ui_layout.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define ACOUSTICS_BUTTON_WIDTH_FRAC 0.1904f

float acoustics_compute_value_col_width(
    float ui,
    float value_size_px,
    const float* values,
    size_t value_count
) {
    float max_text_w = 0.0f;
    if (values) {
        for (size_t i = 0; i < value_count; ++i) {
            char value_text[32];
            (void)snprintf(value_text, sizeof(value_text), "%.3f", values[i]);
            const float text_w = vg_measure_text(value_text, value_size_px, 0.8f);
            if (text_w > max_text_w) {
                max_text_w = text_w;
            }
        }
    }
    const float min_col_w = 70.0f * ui;
    const float col_pad = 12.0f * ui;
    const float measured_w = ceilf(max_text_w + col_pad);
    return fmaxf(min_col_w, measured_w);
}

vg_ui_slider_panel_metrics acoustics_scaled_slider_metrics(float ui, float value_col_width_px) {
    vg_ui_slider_panel_metrics m;
    vg_ui_slider_panel_default_metrics(&m);
    m.pad_left_px *= ui;
    m.pad_top_px *= ui;
    m.pad_right_px *= ui;
    m.pad_bottom_px *= ui;
    m.title_line_gap_px *= ui;
    m.rows_top_offset_px *= ui;
    m.col_gap_px *= ui;
    m.value_col_width_px = value_col_width_px;
    m.row_label_height_sub_px *= ui;
    m.row_slider_y_offset_px *= ui;
    m.row_slider_height_sub_px *= ui;
    m.value_y_offset_px *= ui;
    m.footer_y_from_bottom_px *= ui;
    m.title_sub_size_delta_px *= ui;
    m.label_size_bias_px *= ui;
    m.footer_size_bias_px *= ui;
    return m;
}

acoustics_ui_layout make_acoustics_ui_layout(float w, float h, float value_col_width_px, int row_count_left, int row_count_right) {
    acoustics_ui_layout l;
    memset(&l, 0, sizeof(l));
    const float ui = ui_reference_scale(w, h);
    const vg_rect safe = make_ui_safe_frame(w, h);

    l.panel[0] = (vg_rect){safe.x + safe.w * 0.01f, safe.y + safe.h * 0.10f, safe.w * 0.47f, safe.h * 0.80f};
    l.panel[1] = (vg_rect){safe.x + safe.w * 0.52f, safe.y + safe.h * 0.10f, safe.w * 0.47f, safe.h * 0.80f};
    for (int p = 0; p < 2; ++p) {
        const vg_rect panel = l.panel[p];
        l.button[p] = (vg_rect){
            panel.x + panel.w * 0.03f,
            panel.y + panel.h - panel.h * 0.08f,
            panel.w * ACOUSTICS_BUTTON_WIDTH_FRAC,
            panel.h * 0.042f
        };
    }

    for (int p = 0; p < 2; ++p) {
        const vg_rect btn = l.button[p];
        const vg_rect panel = l.panel[p];
        const float save_w = panel.w * 0.15f;
        l.save_button[p] = (vg_rect){panel.x + panel.w - panel.w * 0.03f - save_w, btn.y, save_w, btn.h};
        const float sx = btn.x + btn.w + panel.w * 0.02f;
        const float sg = panel.w * 0.006f;
        const float sr = l.save_button[p].x - panel.w * 0.02f;
        const float avail = fmaxf(10.0f, sr - sx);
        const float sw = fmaxf(8.0f, (avail - sg * (float)(ACOUSTICS_SLOT_COUNT - 1)) / (float)ACOUSTICS_SLOT_COUNT);
        for (int s = 0; s < ACOUSTICS_SLOT_COUNT; ++s) {
            l.slot_button[p][s] = (vg_rect){sx + (sw + sg) * (float)s, btn.y, sw, btn.h};
        }
    }

    l.value_col_width_px = value_col_width_px;
    l.row_h = 34.0f * ui;
    l.row_count[0] = (row_count_left > 0) ? row_count_left : 8;
    l.row_count[1] = (row_count_right > 0) ? row_count_right : 6;
    vg_ui_slider_panel_metrics sm = acoustics_scaled_slider_metrics(ui, value_col_width_px);
    vg_ui_slider_item dummy_fire[8] = {0};
    vg_ui_slider_item dummy_thr[6] = {0};
    for (int p = 0; p < 2; ++p) {
        const vg_rect r = l.panel[p];
        vg_ui_slider_panel_desc desc = {
            .rect = r,
            .items = (p == 0) ? dummy_fire : dummy_thr,
            .item_count = (size_t)l.row_count[p],
            .row_height_px = l.row_h,
            .label_size_px = 11.0f * ui,
            .value_size_px = 11.5f * ui,
            .value_text_x_offset_px = 0.0f,
            .metrics = &sm
        };
        vg_ui_slider_panel_layout panel_layout;
        vg_ui_slider_panel_row_layout row_layout;
        if (vg_ui_slider_panel_compute_layout(&desc, &panel_layout) == VG_OK &&
            vg_ui_slider_panel_compute_row_layout(&desc, &panel_layout, 0u, &row_layout) == VG_OK) {
            l.row_y0[p] = panel_layout.row_start_y;
            l.slider_x[p] = row_layout.slider_rect.x;
            l.slider_w[p] = row_layout.slider_rect.w;
        } else {
            l.row_y0[p] = r.y + sm.rows_top_offset_px;
            l.slider_x[p] = r.x + sm.pad_left_px + r.w * sm.label_col_frac + sm.col_gap_px;
            l.slider_w[p] = r.w - (l.slider_x[p] - r.x) - sm.value_col_width_px - sm.pad_right_px;
        }
    }
    return l;
}

vg_rect acoustics_page_toggle_button_rect(float w, float h) {
    const vg_rect safe = make_ui_safe_frame(w, h);
    return (vg_rect){
        safe.x + safe.w * 0.79f,
        safe.y + safe.h * 0.92f,
        safe.w * 0.20f,
        safe.h * 0.042f
    };
}
