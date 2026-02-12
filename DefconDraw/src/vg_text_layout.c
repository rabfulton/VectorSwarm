#include "vg_text_layout.h"

#include <stdlib.h>
#include <string.h>

static float vg_text_layout_line_height(const vg_text_layout_params* p) {
    if (p->line_height_px > 0.0f) {
        return p->line_height_px;
    }
    return p->size_px * 1.35f;
}

static int vg_text_layout_is_valid_params(const vg_text_layout_params* p) {
    if (!p) {
        return 0;
    }
    if (!(p->size_px > 0.0f) || !(p->letter_spacing_px == p->letter_spacing_px)) {
        return 0;
    }
    if (p->bounds.w <= 0.0f || p->bounds.h <= 0.0f) {
        return 0;
    }
    if (p->align < VG_TEXT_ALIGN_LEFT || p->align > VG_TEXT_ALIGN_RIGHT) {
        return 0;
    }
    return 1;
}

void vg_text_layout_reset(vg_text_layout* layout) {
    if (!layout) {
        return;
    }
    free(layout->text);
    free(layout->lines);
    memset(layout, 0, sizeof(*layout));
}

static vg_result vg_text_layout_emit_line(
    vg_text_layout* out,
    size_t line_idx,
    size_t start,
    size_t len,
    float width_px,
    float y
) {
    vg_text_layout_line* line = &out->lines[line_idx];
    float x = out->params.bounds.x;
    if (out->params.align == VG_TEXT_ALIGN_CENTER) {
        x = out->params.bounds.x + (out->params.bounds.w - width_px) * 0.5f;
    } else if (out->params.align == VG_TEXT_ALIGN_RIGHT) {
        x = out->params.bounds.x + out->params.bounds.w - width_px;
    }
    line->text_offset = start;
    line->text_length = len;
    line->x = x;
    line->y = y;
    line->width_px = width_px;
    if (width_px > out->content_width_px) {
        out->content_width_px = width_px;
    }
    return VG_OK;
}

vg_result vg_text_layout_build(const char* text, const vg_text_layout_params* params, vg_text_layout* out_layout) {
    if (!text || !out_layout || !vg_text_layout_is_valid_params(params)) {
        return VG_ERROR_INVALID_ARGUMENT;
    }

    vg_text_layout_reset(out_layout);
    out_layout->params = *params;

    size_t n = strlen(text);
    out_layout->text = (char*)malloc(n + 1u);
    if (!out_layout->text) {
        return VG_ERROR_OUT_OF_MEMORY;
    }
    memcpy(out_layout->text, text, n + 1u);

    out_layout->lines = (vg_text_layout_line*)calloc(n + 1u, sizeof(*out_layout->lines));
    if (!out_layout->lines) {
        vg_text_layout_reset(out_layout);
        return VG_ERROR_OUT_OF_MEMORY;
    }

    float adv = params->size_px + params->letter_spacing_px;
    float line_h = vg_text_layout_line_height(params);
    size_t idx = 0u;
    size_t start = 0u;
    size_t line_len = 0u;
    float line_w = 0.0f;
    float y = params->bounds.y;
    size_t line_idx = 0u;

    while (idx <= n) {
        char c = out_layout->text[idx];
        if (c == '\0' || c == '\n') {
            vg_result r = vg_text_layout_emit_line(out_layout, line_idx++, start, line_len, line_w, y);
            if (r != VG_OK) {
                vg_text_layout_reset(out_layout);
                return r;
            }
            y += line_h;
            if (c == '\0') {
                break;
            }
            idx++;
            start = idx;
            line_len = 0u;
            line_w = 0.0f;
            continue;
        }

        if (line_w > 0.0f && line_w + adv > params->bounds.w) {
            vg_result r = vg_text_layout_emit_line(out_layout, line_idx++, start, line_len, line_w, y);
            if (r != VG_OK) {
                vg_text_layout_reset(out_layout);
                return r;
            }
            y += line_h;
            start = idx;
            line_len = 0u;
            line_w = 0.0f;
            continue;
        }

        line_len++;
        line_w += adv;
        idx++;
    }

    out_layout->line_count = line_idx;
    out_layout->content_height_px = line_h * (float)line_idx;
    return VG_OK;
}

vg_result vg_text_layout_draw(
    vg_context* ctx,
    const vg_text_layout* layout,
    vg_text_draw_mode mode,
    const vg_stroke_style* text_style,
    float boxed_weight,
    const vg_fill_style* panel_fill,
    const vg_stroke_style* panel_border
) {
    if (!ctx || !layout || !layout->text || !layout->lines || !text_style || layout->line_count == 0u) {
        return VG_ERROR_INVALID_ARGUMENT;
    }
    float line_h = vg_text_layout_line_height(&layout->params);
    size_t n = strlen(layout->text);
    char* line_buf = (char*)malloc(n + 1u);
    if (!line_buf) {
        return VG_ERROR_OUT_OF_MEMORY;
    }

    for (size_t i = 0; i < layout->line_count; ++i) {
        const vg_text_layout_line* ln = &layout->lines[i];
        if (ln->y + layout->params.size_px > layout->params.bounds.y + layout->params.bounds.h) {
            break;
        }
        if (ln->text_length == 0u) {
            continue;
        }
        memcpy(line_buf, layout->text + ln->text_offset, ln->text_length);
        line_buf[ln->text_length] = '\0';

        vg_result r = VG_OK;
        switch (mode) {
            case VG_TEXT_DRAW_MODE_STROKE:
                r = vg_draw_text(ctx, line_buf, (vg_vec2){ln->x, ln->y}, layout->params.size_px, layout->params.letter_spacing_px, text_style, NULL);
                break;
            case VG_TEXT_DRAW_MODE_BOXED:
                r = vg_draw_text_boxed(ctx, line_buf, (vg_vec2){ln->x, ln->y}, layout->params.size_px, layout->params.letter_spacing_px, text_style, NULL);
                break;
            case VG_TEXT_DRAW_MODE_BOXED_WEIGHTED:
                r = vg_draw_text_boxed_weighted(ctx, line_buf, (vg_vec2){ln->x, ln->y}, layout->params.size_px, layout->params.letter_spacing_px, text_style, boxed_weight, NULL);
                break;
            case VG_TEXT_DRAW_MODE_VECTOR_FILL:
                r = vg_draw_text_vector_fill(ctx, line_buf, (vg_vec2){ln->x, ln->y}, layout->params.size_px, layout->params.letter_spacing_px, text_style, NULL);
                break;
            case VG_TEXT_DRAW_MODE_STENCIL_CUTOUT:
                if (!panel_fill || !panel_border) {
                    free(line_buf);
                    return VG_ERROR_INVALID_ARGUMENT;
                }
                r = vg_draw_text_stencil_cutout(
                    ctx,
                    line_buf,
                    (vg_vec2){ln->x, ln->y},
                    layout->params.size_px,
                    layout->params.letter_spacing_px,
                    panel_fill,
                    panel_border,
                    text_style,
                    NULL
                );
                break;
            default:
                free(line_buf);
                return VG_ERROR_INVALID_ARGUMENT;
        }
        if (r != VG_OK) {
            free(line_buf);
            return r;
        }
        (void)line_h;
    }

    free(line_buf);
    return VG_OK;
}
