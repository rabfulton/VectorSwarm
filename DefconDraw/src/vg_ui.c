#include "vg_ui.h"

#include <math.h>
#include <stdio.h>

static float vg_ui_clampf(float v, float lo, float hi) {
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

void vg_ui_slider_panel_default_metrics(vg_ui_slider_panel_metrics* out_metrics) {
    if (!out_metrics) {
        return;
    }
    out_metrics->pad_left_px = 16.0f;
    out_metrics->pad_top_px = 14.0f;
    out_metrics->pad_right_px = 14.0f;
    out_metrics->pad_bottom_px = 14.0f;
    out_metrics->title_line_gap_px = 17.0f;
    out_metrics->rows_top_offset_px = 70.0f;
    out_metrics->label_col_frac = 0.40f;
    out_metrics->col_gap_px = 16.0f;
    out_metrics->value_col_width_px = 62.0f;
    out_metrics->row_label_height_sub_px = 10.0f;
    out_metrics->row_slider_y_offset_px = 2.0f;
    out_metrics->row_slider_height_sub_px = 14.0f;
    out_metrics->value_y_offset_px = 8.0f;
    out_metrics->footer_y_from_bottom_px = 26.0f;
    out_metrics->title_sub_size_delta_px = 1.0f;
    out_metrics->label_size_bias_px = 2.0f;
    out_metrics->footer_size_bias_px = 4.0f;
}

static vg_result vg_ui_slider_panel_sanitize_metrics(
    const vg_ui_slider_panel_desc* desc,
    vg_ui_slider_panel_metrics* out_metrics
) {
    if (!desc || !out_metrics) {
        return VG_ERROR_INVALID_ARGUMENT;
    }
    vg_ui_slider_panel_default_metrics(out_metrics);
    if (desc->metrics) {
        *out_metrics = *desc->metrics;
    }
    if (!(isfinite(out_metrics->label_col_frac) && out_metrics->label_col_frac > 0.05f && out_metrics->label_col_frac < 0.85f)) {
        out_metrics->label_col_frac = 0.40f;
    }
    if (!isfinite(out_metrics->value_col_width_px) || out_metrics->value_col_width_px < 16.0f) {
        out_metrics->value_col_width_px = 62.0f;
    }
    return VG_OK;
}

vg_result vg_ui_slider_panel_compute_layout(
    const vg_ui_slider_panel_desc* desc,
    vg_ui_slider_panel_layout* out_layout
) {
    if (!desc || !out_layout || !desc->items || desc->item_count == 0u) {
        return VG_ERROR_INVALID_ARGUMENT;
    }
    if (desc->rect.w <= 0.0f || desc->rect.h <= 0.0f) {
        return VG_ERROR_INVALID_ARGUMENT;
    }
    if (!isfinite(desc->row_height_px) || desc->row_height_px <= 0.0f) {
        return VG_ERROR_INVALID_ARGUMENT;
    }

    vg_ui_slider_panel_metrics m;
    vg_result mr = vg_ui_slider_panel_sanitize_metrics(desc, &m);
    if (mr != VG_OK) {
        return mr;
    }

    out_layout->left_x = desc->rect.x + m.pad_left_px;
    out_layout->title_line_0_pos = (vg_vec2){out_layout->left_x, desc->rect.y + m.pad_top_px};
    out_layout->title_line_1_pos = (vg_vec2){out_layout->left_x, desc->rect.y + m.pad_top_px + m.title_line_gap_px};
    out_layout->row_start_y = desc->rect.y + m.rows_top_offset_px;
    out_layout->label_w = desc->rect.w * m.label_col_frac;
    out_layout->slider_x = out_layout->left_x + out_layout->label_w + m.col_gap_px;
    out_layout->slider_w = desc->rect.w - (out_layout->slider_x - desc->rect.x) - m.value_col_width_px - m.pad_right_px;
    if (out_layout->slider_w < 4.0f) {
        out_layout->slider_w = 4.0f;
    }
    out_layout->value_x = desc->rect.x + desc->rect.w - m.value_col_width_px + desc->value_text_x_offset_px;
    out_layout->footer_pos = (vg_vec2){
        out_layout->left_x,
        desc->rect.y + desc->rect.h - m.footer_y_from_bottom_px
    };
    return VG_OK;
}

vg_result vg_ui_slider_panel_compute_row_layout(
    const vg_ui_slider_panel_desc* desc,
    const vg_ui_slider_panel_layout* layout,
    size_t row_index,
    vg_ui_slider_panel_row_layout* out_row
) {
    if (!desc || !layout || !out_row || row_index >= desc->item_count) {
        return VG_ERROR_INVALID_ARGUMENT;
    }
    vg_ui_slider_panel_metrics m;
    vg_result mr = vg_ui_slider_panel_sanitize_metrics(desc, &m);
    if (mr != VG_OK) {
        return mr;
    }
    float row_y = layout->row_start_y + desc->row_height_px * (float)row_index;
    out_row->label_rect = (vg_rect){
        layout->left_x,
        row_y,
        layout->label_w,
        desc->row_height_px - m.row_label_height_sub_px
    };
    out_row->slider_rect = (vg_rect){
        layout->slider_x,
        row_y + m.row_slider_y_offset_px,
        layout->slider_w,
        desc->row_height_px - m.row_slider_height_sub_px
    };
    out_row->value_pos = (vg_vec2){
        layout->value_x,
        row_y + m.value_y_offset_px
    };
    return VG_OK;
}

vg_result vg_ui_draw_slider_panel(vg_context* ctx, const vg_ui_slider_panel_desc* desc) {
    if (!ctx || !desc || !desc->items || desc->item_count == 0u) {
        return VG_ERROR_INVALID_ARGUMENT;
    }
    if (!isfinite(desc->row_height_px) || desc->row_height_px <= 0.0f) {
        return VG_ERROR_INVALID_ARGUMENT;
    }
    if (!isfinite(desc->label_size_px) || desc->label_size_px <= 0.0f) {
        return VG_ERROR_INVALID_ARGUMENT;
    }
    if (!isfinite(desc->value_size_px) || desc->value_size_px <= 0.0f) {
        return VG_ERROR_INVALID_ARGUMENT;
    }
    vg_ui_slider_panel_metrics m;
    vg_result mr = vg_ui_slider_panel_sanitize_metrics(desc, &m);
    if (mr != VG_OK) {
        return mr;
    }
    vg_ui_slider_panel_layout layout;
    vg_result lr = vg_ui_slider_panel_compute_layout(desc, &layout);
    if (lr != VG_OK) {
        return lr;
    }

    vg_result r = vg_draw_rect(ctx, desc->rect, &desc->border_style);
    if (r != VG_OK) {
        return r;
    }
    if (desc->title_line_0 && desc->title_line_0[0] != '\0') {
        r = vg_draw_text(ctx, desc->title_line_0, layout.title_line_0_pos, desc->label_size_px, 0.8f, &desc->text_style, NULL);
        if (r != VG_OK) {
            return r;
        }
    }
    if (desc->title_line_1 && desc->title_line_1[0] != '\0') {
        r = vg_draw_text(
            ctx,
            desc->title_line_1,
            layout.title_line_1_pos,
            desc->label_size_px - m.title_sub_size_delta_px,
            0.8f,
            &desc->text_style,
            NULL
        );
        if (r != VG_OK) {
            return r;
        }
    }

    for (size_t i = 0; i < desc->item_count; ++i) {
        vg_ui_slider_panel_row_layout row;
        vg_result rr = vg_ui_slider_panel_compute_row_layout(desc, &layout, i, &row);
        if (rr != VG_OK) {
            return rr;
        }
        r = vg_draw_button(
            ctx,
            row.label_rect,
            desc->items[i].label ? desc->items[i].label : "",
            desc->label_size_px + m.label_size_bias_px,
            &desc->border_style,
            &desc->text_style,
            desc->items[i].selected
        );
        if (r != VG_OK) {
            return r;
        }

        r = vg_draw_slider(
            ctx,
            row.slider_rect,
            vg_ui_clampf(desc->items[i].value_01, 0.0f, 1.0f),
            &desc->border_style,
            &desc->track_style,
            &desc->knob_style
        );
        if (r != VG_OK) {
            return r;
        }

        char val_text[32];
        snprintf(val_text, sizeof(val_text), "%.3f", desc->items[i].value_display);
        r = vg_draw_text(
            ctx,
            val_text,
            row.value_pos,
            desc->value_size_px,
            0.8f,
            &desc->text_style,
            NULL
        );
        if (r != VG_OK) {
            return r;
        }
    }

    if (desc->footer_line && desc->footer_line[0] != '\0') {
        r = vg_draw_text(
            ctx,
            desc->footer_line,
            layout.footer_pos,
            desc->value_size_px + m.footer_size_bias_px,
            1.0f,
            &desc->text_style,
            NULL
        );
        if (r != VG_OK) {
            return r;
        }
    }
    return VG_OK;
}
