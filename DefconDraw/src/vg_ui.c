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

    vg_result r = vg_draw_rect(ctx, desc->rect, &desc->border_style);
    if (r != VG_OK) {
        return r;
    }

    float left = desc->rect.x + 16.0f;
    float top = desc->rect.y + 14.0f;
    if (desc->title_line_0 && desc->title_line_0[0] != '\0') {
        r = vg_draw_text(ctx, desc->title_line_0, (vg_vec2){left, top}, desc->label_size_px, 0.8f, &desc->text_style, NULL);
        if (r != VG_OK) {
            return r;
        }
    }
    if (desc->title_line_1 && desc->title_line_1[0] != '\0') {
        r = vg_draw_text(ctx, desc->title_line_1, (vg_vec2){left, top + 17.0f}, desc->label_size_px - 1.0f, 0.8f, &desc->text_style, NULL);
        if (r != VG_OK) {
            return r;
        }
    }

    float row_y = desc->rect.y + 70.0f;
    float label_w = desc->rect.w * 0.34f;
    float slider_x = left + label_w + 16.0f;
    float slider_w = desc->rect.w - (slider_x - desc->rect.x) - 104.0f;
    for (size_t i = 0; i < desc->item_count; ++i) {
        vg_rect label_rect = {left, row_y, label_w, desc->row_height_px - 10.0f};
        r = vg_draw_button(
            ctx,
            label_rect,
            desc->items[i].label ? desc->items[i].label : "",
            desc->label_size_px + 2.0f,
            &desc->border_style,
            &desc->text_style,
            desc->items[i].selected
        );
        if (r != VG_OK) {
            return r;
        }

        vg_rect slider_rect = {slider_x, row_y + 2.0f, slider_w, desc->row_height_px - 14.0f};
        r = vg_draw_slider(
            ctx,
            slider_rect,
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
            (vg_vec2){desc->rect.x + desc->rect.w - 78.0f, row_y + 8.0f},
            desc->value_size_px,
            0.8f,
            &desc->text_style,
            NULL
        );
        if (r != VG_OK) {
            return r;
        }

        row_y += desc->row_height_px;
    }

    if (desc->footer_line && desc->footer_line[0] != '\0') {
        r = vg_draw_text(
            ctx,
            desc->footer_line,
            (vg_vec2){left, desc->rect.y + desc->rect.h - 26.0f},
            desc->value_size_px + 4.0f,
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
