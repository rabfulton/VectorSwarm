#include "vg_ui_ext.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static float vg_ui_ext_clampf(float v, float lo, float hi) {
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

static float vg_ui_ext_norm(float v, float lo, float hi) {
    if (hi <= lo) {
        return 0.0f;
    }
    return vg_ui_ext_clampf((v - lo) / (hi - lo), 0.0f, 1.0f);
}

static float vg_ui_ext_resolved_scale(float s) {
    if (!isfinite(s) || s <= 0.0f) {
        return 1.0f;
    }
    return s;
}

static vg_fill_style vg_ui_ext_fill_from_stroke(const vg_stroke_style* s, float alpha_scale) {
    vg_fill_style f;
    f.intensity = s->intensity;
    f.color = s->color;
    f.color.a *= alpha_scale;
    f.blend = s->blend;
    return f;
}

vg_result vg_ui_meter_linear_layout_compute(
    const vg_ui_meter_desc* desc,
    const vg_ui_meter_style* style,
    vg_ui_meter_linear_layout* out_layout
) {
    if (!desc || !style || !out_layout) {
        return VG_ERROR_INVALID_ARGUMENT;
    }
    if (desc->rect.w <= 0.0f || desc->rect.h <= 0.0f) {
        return VG_ERROR_INVALID_ARGUMENT;
    }
    float ui = vg_ui_ext_resolved_scale(desc->ui_scale);
    float value01 = vg_ui_ext_norm(desc->value, desc->min_value, desc->max_value);
    out_layout->outer_rect = desc->rect;
    float pad = style->frame.width_px + 2.0f * ui;
    if (pad > desc->rect.w * 0.35f) {
        pad = desc->rect.w * 0.35f;
    }
    if (pad > desc->rect.h * 0.35f) {
        pad = desc->rect.h * 0.35f;
    }
    out_layout->inner_rect = (vg_rect){
        desc->rect.x + pad,
        desc->rect.y + pad,
        desc->rect.w - 2.0f * pad,
        desc->rect.h - 2.0f * pad
    };
    out_layout->fill_rect = out_layout->inner_rect;
    out_layout->fill_rect.w = out_layout->inner_rect.w * value01;
    out_layout->label_pos = (vg_vec2){
        desc->rect.x,
        desc->rect.y + desc->rect.h + 8.0f * ui
    };
    out_layout->value_pos = out_layout->label_pos;
    return VG_OK;
}

vg_result vg_ui_meter_radial_layout_compute(
    vg_vec2 center,
    float radius_px,
    const vg_ui_meter_desc* desc,
    const vg_ui_meter_style* style,
    vg_ui_meter_radial_layout* out_layout
) {
    if (!desc || !style || !out_layout || !isfinite(radius_px) || radius_px <= 1.0f) {
        return VG_ERROR_INVALID_ARGUMENT;
    }
    float ui = vg_ui_ext_resolved_scale(desc->ui_scale);
    out_layout->center = center;
    out_layout->radius_px = radius_px;
    out_layout->a0 = 3.926990716f;
    out_layout->sweep = 4.712388980f;
    out_layout->tick_inner_radius = radius_px - 6.0f * ui;
    out_layout->tick_outer_radius = radius_px + 4.0f * ui;
    out_layout->needle_radius = radius_px - 8.0f * ui;
    out_layout->value_pos = (vg_vec2){center.x, center.y - 6.0f * ui};
    out_layout->label_pos = (vg_vec2){center.x, center.y - radius_px - 18.0f * ui};
    return VG_OK;
}

static vg_result vg_ui_ext_draw_arc(
    vg_context* ctx,
    vg_vec2 center,
    float radius,
    float a0,
    float a1,
    int steps,
    const vg_stroke_style* style
) {
    if (steps < 2) {
        steps = 2;
    }
    vg_vec2* pts = (vg_vec2*)malloc(sizeof(*pts) * (size_t)steps);
    if (!pts) {
        return VG_ERROR_OUT_OF_MEMORY;
    }
    for (int i = 0; i < steps; ++i) {
        float t = (float)i / (float)(steps - 1);
        float a = a0 + (a1 - a0) * t;
        pts[i].x = center.x + cosf(a) * radius;
        pts[i].y = center.y + sinf(a) * radius;
    }
    vg_result r = vg_draw_polyline(ctx, pts, (size_t)steps, style, 0);
    free(pts);
    return r;
}

static vg_result vg_ui_ext_draw_circle(vg_context* ctx, vg_vec2 center, float radius, int steps, const vg_stroke_style* style) {
    if (steps < 8) {
        steps = 8;
    }
    vg_vec2* pts = (vg_vec2*)malloc(sizeof(*pts) * (size_t)steps);
    if (!pts) {
        return VG_ERROR_OUT_OF_MEMORY;
    }
    for (int i = 0; i < steps; ++i) {
        float t = (float)i / (float)steps;
        float a = 6.28318530718f * t;
        pts[i].x = center.x + cosf(a) * radius;
        pts[i].y = center.y + sinf(a) * radius;
    }
    vg_result r = vg_draw_polyline(ctx, pts, (size_t)steps, style, 1);
    free(pts);
    return r;
}

vg_result vg_ui_meter_linear(vg_context* ctx, const vg_ui_meter_desc* desc, const vg_ui_meter_style* style) {
    if (!ctx || !desc || !style) {
        return VG_ERROR_INVALID_ARGUMENT;
    }
    if (desc->rect.w <= 0.0f || desc->rect.h <= 0.0f) {
        return VG_ERROR_INVALID_ARGUMENT;
    }

    float ui = vg_ui_ext_resolved_scale(desc->ui_scale);
    float text = vg_ui_ext_resolved_scale(desc->text_scale);
    float value01 = vg_ui_ext_norm(desc->value, desc->min_value, desc->max_value);
    vg_ui_meter_linear_layout layout;
    vg_result lr = vg_ui_meter_linear_layout_compute(desc, style, &layout);
    if (lr != VG_OK) {
        return lr;
    }
    vg_result r = vg_draw_rect(ctx, layout.outer_rect, &style->frame);
    if (r != VG_OK) {
        return r;
    }
    vg_rect inner = layout.inner_rect;
    if (inner.w <= 1.0f || inner.h <= 1.0f) {
        return VG_OK;
    }

    vg_fill_style bg_fill = vg_ui_ext_fill_from_stroke(&style->bg, 0.45f);
    r = vg_fill_rect(ctx, inner, &bg_fill);
    if (r != VG_OK) {
        return r;
    }

    vg_fill_style fg_fill = vg_ui_ext_fill_from_stroke(&style->fill, 0.75f);
    if (desc->mode == VG_UI_METER_SEGMENTED) {
        int segs = desc->segments > 0 ? desc->segments : 10;
        float gap = desc->segment_gap_px >= 0.0f ? desc->segment_gap_px : 2.0f * ui;
        float seg_w = (inner.w - (float)(segs - 1) * gap) / (float)segs;
        if (seg_w < 1.0f) {
            seg_w = 1.0f;
            gap = (inner.w - (float)segs * seg_w) / (float)(segs - 1 > 0 ? segs - 1 : 1);
            if (gap < 0.0f) {
                gap = 0.0f;
            }
        }
        int lit = (int)floorf(value01 * (float)segs + 1e-5f);
        if (lit < 0) lit = 0;
        if (lit > segs) lit = segs;
        for (int i = 0; i < lit; ++i) {
            vg_rect seg = {inner.x + (seg_w + gap) * (float)i, inner.y, seg_w, inner.h};
            r = vg_fill_rect(ctx, seg, &fg_fill);
            if (r != VG_OK) {
                return r;
            }
        }
    } else {
        vg_rect fill = layout.fill_rect;
        if (fill.w > 0.5f) {
            r = vg_fill_rect(ctx, fill, &fg_fill);
            if (r != VG_OK) {
                return r;
            }
        }
    }

    if (desc->show_ticks) {
        const int nt = 5;
        for (int i = 0; i <= nt; ++i) {
            float u = (float)i / (float)nt;
            float x = inner.x + inner.w * u;
            vg_vec2 tick[2] = {
                {x, inner.y},
                {x, inner.y + inner.h * (0.24f * ui)}
            };
            r = vg_draw_polyline(ctx, tick, 2u, &style->tick, 0);
            if (r != VG_OK) {
                return r;
            }
        }
    }

    if (desc->label && desc->label[0] != '\0') {
        r = vg_draw_text(
            ctx,
            desc->label,
            layout.label_pos,
            12.0f * text,
            0.9f,
            &style->text,
            NULL
        );
        if (r != VG_OK) {
            return r;
        }
    }

    if (desc->show_value) {
        char vtxt[64];
        const char* fmt = (desc->value_fmt && desc->value_fmt[0] != '\0') ? desc->value_fmt : "%.1f";
        snprintf(vtxt, sizeof(vtxt), fmt, desc->value);
        float tw = vg_measure_text(vtxt, 12.0f * text, 0.8f * text);
        vg_vec2 value_pos = layout.value_pos;
        value_pos.x = desc->rect.x + desc->rect.w - tw;
        r = vg_draw_text(
            ctx,
            vtxt,
            value_pos,
            12.0f * text,
            0.8f * text,
            &style->text,
            NULL
        );
        if (r != VG_OK) {
            return r;
        }
    }

    return VG_OK;
}

vg_result vg_ui_meter_radial(vg_context* ctx, vg_vec2 center, float radius_px, const vg_ui_meter_desc* desc, const vg_ui_meter_style* style) {
    if (!ctx || !desc || !style || !isfinite(radius_px) || radius_px <= 1.0f) {
        return VG_ERROR_INVALID_ARGUMENT;
    }

    float ui = vg_ui_ext_resolved_scale(desc->ui_scale);
    float text = vg_ui_ext_resolved_scale(desc->text_scale);
    float value01 = vg_ui_ext_norm(desc->value, desc->min_value, desc->max_value);
    vg_ui_meter_radial_layout layout;
    vg_result lr = vg_ui_meter_radial_layout_compute(center, radius_px, desc, style, &layout);
    if (lr != VG_OK) {
        return lr;
    }
    const float a0 = layout.a0;
    const float sweep = layout.sweep;
    const float a1 = a0 + sweep;
    vg_result r = vg_ui_ext_draw_arc(ctx, center, radius_px, a0, a1, 72, &style->bg);
    if (r != VG_OK) {
        return r;
    }

    if (desc->mode == VG_UI_METER_SEGMENTED) {
        int segs = desc->segments > 0 ? desc->segments : 18;
        float gap_px = desc->segment_gap_px >= 0.0f ? desc->segment_gap_px : 3.0f * ui;
        float gap_a = gap_px / radius_px;
        float seg_a = (sweep - gap_a * (float)(segs - 1)) / (float)segs;
        if (seg_a < 0.02f) {
            seg_a = 0.02f;
            gap_a = 0.0f;
        }
        int lit = (int)floorf(value01 * (float)segs + 1e-5f);
        if (lit < 0) lit = 0;
        if (lit > segs) lit = segs;
        for (int i = 0; i < lit; ++i) {
            float s0 = a0 + (seg_a + gap_a) * (float)i;
            float s1 = s0 + seg_a;
            r = vg_ui_ext_draw_arc(ctx, center, radius_px, s0, s1, 10, &style->fill);
            if (r != VG_OK) {
                return r;
            }
        }
    } else {
        r = vg_ui_ext_draw_arc(ctx, center, radius_px, a0, a0 + sweep * value01, 72, &style->fill);
        if (r != VG_OK) {
            return r;
        }
    }

    r = vg_ui_ext_draw_arc(ctx, center, radius_px + style->frame.width_px * 0.6f, a0, a1, 72, &style->frame);
    if (r != VG_OK) {
        return r;
    }

    if (desc->show_ticks) {
        for (int i = 0; i <= 10; ++i) {
            float u = (float)i / 10.0f;
            float a = a0 + sweep * u;
            float c = cosf(a);
            float s = sinf(a);
            vg_vec2 tick[2] = {
                {center.x + c * layout.tick_inner_radius, center.y + s * layout.tick_inner_radius},
                {center.x + c * layout.tick_outer_radius, center.y + s * layout.tick_outer_radius}
            };
            r = vg_draw_polyline(ctx, tick, 2u, &style->tick, 0);
            if (r != VG_OK) {
                return r;
            }
        }
    }

    /* Needle */
    {
        float an = a0 + sweep * value01;
        vg_vec2 needle[2] = {
            center,
            {center.x + cosf(an) * layout.needle_radius, center.y + sinf(an) * layout.needle_radius}
        };
        r = vg_draw_polyline(ctx, needle, 2u, &style->tick, 0);
        if (r != VG_OK) {
            return r;
        }
    }

    if (desc->show_value) {
        char vtxt[64];
        const char* fmt = (desc->value_fmt && desc->value_fmt[0] != '\0') ? desc->value_fmt : "%.1f";
        snprintf(vtxt, sizeof(vtxt), fmt, desc->value);
        float tw = vg_measure_text(vtxt, 12.0f * text, 0.8f * text);
        r = vg_draw_text(ctx, vtxt, (vg_vec2){layout.value_pos.x - tw * 0.5f, layout.value_pos.y}, 12.0f * text, 0.8f * text, &style->text, NULL);
        if (r != VG_OK) {
            return r;
        }
    }

    if (desc->label && desc->label[0] != '\0') {
        float tw = vg_measure_text(desc->label, 11.0f * text, 0.8f * text);
        r = vg_draw_text(ctx, desc->label, (vg_vec2){layout.label_pos.x - tw * 0.5f, layout.label_pos.y}, 11.0f * text, 0.8f * text, &style->text, NULL);
        if (r != VG_OK) {
            return r;
        }
    }

    return VG_OK;
}

void vg_ui_history_reset(vg_ui_history* h) {
    if (!h) {
        return;
    }
    h->count = 0u;
    h->head = 0u;
}

void vg_ui_history_push(vg_ui_history* h, float value) {
    if (!h || !h->data || h->capacity == 0u || !isfinite(value)) {
        return;
    }
    h->data[h->head] = value;
    h->head = (h->head + 1u) % h->capacity;
    if (h->count < h->capacity) {
        h->count++;
    }
}

size_t vg_ui_history_linearize(const vg_ui_history* h, float* out, size_t out_cap) {
    if (!h || !h->data || !out || out_cap == 0u || h->count == 0u) {
        return 0u;
    }
    size_t n = h->count < out_cap ? h->count : out_cap;
    size_t start = (h->head + h->capacity - h->count) % h->capacity;
    for (size_t i = 0; i < n; ++i) {
        out[i] = h->data[(start + i) % h->capacity];
    }
    return n;
}

static vg_result vg_ui_graph_common_frame(vg_context* ctx, const vg_ui_graph_desc* d, const vg_ui_graph_style* s, vg_rect* out_inner) {
    if (!ctx || !d || !s || !out_inner) {
        return VG_ERROR_INVALID_ARGUMENT;
    }
    if (d->rect.w <= 2.0f || d->rect.h <= 2.0f || !d->samples || d->sample_count == 0u) {
        return VG_ERROR_INVALID_ARGUMENT;
    }
    vg_result r = vg_draw_rect(ctx, d->rect, &s->frame);
    if (r != VG_OK) {
        return r;
    }
    float pad = s->frame.width_px + 2.0f;
    if (pad < 2.0f) {
        pad = 2.0f;
    }
    out_inner->x = d->rect.x + pad;
    out_inner->y = d->rect.y + pad;
    out_inner->w = d->rect.w - 2.0f * pad;
    out_inner->h = d->rect.h - 2.0f * pad;
    if (out_inner->w <= 2.0f || out_inner->h <= 2.0f) {
        return VG_ERROR_INVALID_ARGUMENT;
    }
    return VG_OK;
}

vg_result vg_ui_graph_line(vg_context* ctx, const vg_ui_graph_desc* desc, const vg_ui_graph_style* style) {
    float ui = vg_ui_ext_resolved_scale(desc->ui_scale);
    float text = vg_ui_ext_resolved_scale(desc->text_scale);
    vg_rect inner;
    vg_result r = vg_ui_graph_common_frame(ctx, desc, style, &inner);
    if (r != VG_OK) {
        return r;
    }

    float min_v = desc->min_value;
    float max_v = desc->max_value;
    if (!(isfinite(min_v) && isfinite(max_v)) || max_v <= min_v) {
        min_v = -1.0f;
        max_v = 1.0f;
    }

    if (desc->show_grid) {
        for (int i = 1; i < 4; ++i) {
            float u = (float)i / 4.0f;
            float y = inner.y + inner.h * u;
            vg_vec2 hline[2] = {{inner.x, y}, {inner.x + inner.w, y}};
            r = vg_draw_polyline(ctx, hline, 2u, &style->grid, 0);
            if (r != VG_OK) return r;
        }
    }

    size_t n = desc->sample_count;
    if (n < 2u) {
        return VG_OK;
    }
    vg_vec2* pts = (vg_vec2*)malloc(sizeof(*pts) * n);
    if (!pts) {
        return VG_ERROR_OUT_OF_MEMORY;
    }
    for (size_t i = 0; i < n; ++i) {
        float u = (float)i / (float)(n - 1u);
        float v = vg_ui_ext_norm(desc->samples[i], min_v, max_v);
        pts[i].x = inner.x + inner.w * u;
        pts[i].y = inner.y + inner.h * v;
    }
    r = vg_draw_polyline(ctx, pts, n, &style->line, 0);
    free(pts);
    if (r != VG_OK) {
        return r;
    }

    if (desc->label && desc->label[0] != '\0') {
        r = vg_draw_text(
            ctx,
            desc->label,
            (vg_vec2){desc->rect.x, desc->rect.y + desc->rect.h + 8.0f * ui},
            11.0f * text,
            0.8f * text,
            &style->text,
            NULL
        );
        if (r != VG_OK) return r;
    }
    if (desc->show_minmax_labels) {
        char min_txt[32];
        char max_txt[32];
        snprintf(min_txt, sizeof(min_txt), "%.1f", min_v);
        snprintf(max_txt, sizeof(max_txt), "%.1f", max_v);
        r = vg_draw_text(ctx, min_txt, (vg_vec2){desc->rect.x, desc->rect.y - 14.0f * ui}, 10.0f * text, 0.7f * text, &style->text, NULL);
        if (r != VG_OK) return r;
        float tw = vg_measure_text(max_txt, 10.0f * text, 0.7f * text);
        r = vg_draw_text(ctx, max_txt, (vg_vec2){desc->rect.x + desc->rect.w - tw, desc->rect.y - 14.0f * ui}, 10.0f * text, 0.7f * text, &style->text, NULL);
        if (r != VG_OK) return r;
    }
    return VG_OK;
}

vg_result vg_ui_graph_bars(vg_context* ctx, const vg_ui_graph_desc* desc, const vg_ui_graph_style* style) {
    float ui = vg_ui_ext_resolved_scale(desc->ui_scale);
    float text = vg_ui_ext_resolved_scale(desc->text_scale);
    vg_rect inner;
    vg_result r = vg_ui_graph_common_frame(ctx, desc, style, &inner);
    if (r != VG_OK) {
        return r;
    }
    float min_v = desc->min_value;
    float max_v = desc->max_value;
    if (!(isfinite(min_v) && isfinite(max_v)) || max_v <= min_v) {
        min_v = 0.0f;
        max_v = 1.0f;
    }

    size_t n = desc->sample_count;
    float gap = 1.5f * ui;
    float bw = (inner.w - (float)(n - 1u) * gap) / (float)n;
    if (bw < 1.0f) {
        bw = 1.0f;
        gap = 0.0f;
    }
    vg_fill_style bar_fill = vg_ui_ext_fill_from_stroke(&style->bar, 0.85f);
    for (size_t i = 0; i < n; ++i) {
        float v = vg_ui_ext_norm(desc->samples[i], min_v, max_v);
        float bh = inner.h * v;
        vg_rect bar = {
            inner.x + (bw + gap) * (float)i,
            inner.y,
            bw,
            bh
        };
        if (bar.h <= 0.5f) {
            continue;
        }
        r = vg_fill_rect(ctx, bar, &bar_fill);
        if (r != VG_OK) return r;
    }

    if (desc->label && desc->label[0] != '\0') {
        r = vg_draw_text(
            ctx,
            desc->label,
            (vg_vec2){desc->rect.x, desc->rect.y + desc->rect.h + 8.0f * ui},
            11.0f * text,
            0.8f * text,
            &style->text,
            NULL
        );
        if (r != VG_OK) return r;
    }
    return VG_OK;
}

vg_result vg_ui_histogram(vg_context* ctx, const vg_ui_histogram_desc* desc, const vg_ui_graph_style* style) {
    if (!ctx || !desc || !style || !desc->bins || desc->bin_count == 0u) {
        return VG_ERROR_INVALID_ARGUMENT;
    }

    vg_ui_graph_desc gd;
    gd.rect = desc->rect;
    gd.samples = desc->bins;
    gd.sample_count = desc->bin_count;
    gd.min_value = desc->min_value;
    gd.max_value = desc->max_value;
    gd.label = NULL;
    gd.show_grid = desc->show_grid;
    gd.show_minmax_labels = 0;
    gd.ui_scale = desc->ui_scale;
    gd.text_scale = desc->text_scale;
    vg_result r = vg_ui_graph_bars(ctx, &gd, style);
    if (r != VG_OK) {
        return r;
    }

    if (desc->show_axes) {
        float ui = vg_ui_ext_resolved_scale(desc->ui_scale);
        float text = vg_ui_ext_resolved_scale(desc->text_scale);
        float pad = style->frame.width_px + 2.0f;
        if (pad < 2.0f) {
            pad = 2.0f;
        }
        vg_rect inner = {
            desc->rect.x + pad,
            desc->rect.y + pad,
            desc->rect.w - 2.0f * pad,
            desc->rect.h - 2.0f * pad
        };
        vg_vec2 xaxis[2] = {{inner.x, inner.y}, {inner.x + inner.w, inner.y}};
        vg_vec2 yaxis[2] = {{inner.x, inner.y}, {inner.x, inner.y + inner.h}};
        r = vg_draw_polyline(ctx, xaxis, 2u, &style->grid, 0);
        if (r != VG_OK) return r;
        r = vg_draw_polyline(ctx, yaxis, 2u, &style->grid, 0);
        if (r != VG_OK) return r;

        if (desc->label && desc->label[0] != '\0') {
            float tw = vg_measure_text(desc->label, 11.0f * text, 0.8f * text);
            r = vg_draw_text(
                ctx,
                desc->label,
                (vg_vec2){desc->rect.x + (desc->rect.w - tw) * 0.5f, desc->rect.y + desc->rect.h + 8.0f * ui},
                11.0f * text,
                0.8f * text,
                &style->text,
                NULL
            );
            if (r != VG_OK) return r;
        }
        if (desc->x_label && desc->x_label[0] != '\0') {
            float tw = vg_measure_text(desc->x_label, 10.0f * text, 0.7f * text);
            r = vg_draw_text(
                ctx,
                desc->x_label,
                (vg_vec2){desc->rect.x + (desc->rect.w - tw) * 0.5f, desc->rect.y - 14.0f * ui},
                10.0f * text,
                0.7f * text,
                &style->text,
                NULL
            );
            if (r != VG_OK) return r;
        }
        if (desc->y_label && desc->y_label[0] != '\0') {
            r = vg_draw_text(
                ctx,
                desc->y_label,
                (vg_vec2){desc->rect.x + 4.0f * ui, desc->rect.y + desc->rect.h + 20.0f * ui},
                10.0f * text,
                0.7f * text,
                &style->text,
                NULL
            );
            if (r != VG_OK) return r;
        }
    }
    return VG_OK;
}

vg_result vg_ui_pie_chart(vg_context* ctx, const vg_ui_pie_desc* desc, const vg_stroke_style* outline_style, const vg_stroke_style* text_style) {
    if (!ctx || !desc || !outline_style || !text_style || !desc->values || desc->value_count == 0u || !isfinite(desc->radius_px) || desc->radius_px <= 2.0f) {
        return VG_ERROR_INVALID_ARGUMENT;
    }
    float total = 0.0f;
    float ui = vg_ui_ext_resolved_scale(desc->ui_scale);
    float text = vg_ui_ext_resolved_scale(desc->text_scale);
    for (size_t i = 0; i < desc->value_count; ++i) {
        if (desc->values[i] > 0.0f && isfinite(desc->values[i])) {
            total += desc->values[i];
        }
    }
    if (total <= 0.0f) {
        return VG_OK;
    }

    float a = 0.0f;
    for (size_t i = 0; i < desc->value_count; ++i) {
        float v = desc->values[i] > 0.0f ? desc->values[i] : 0.0f;
        if (v <= 0.0f) {
            continue;
        }
        float span = 6.28318530718f * (v / total);
        int segs = (int)(20.0f * (span / 6.28318530718f)) + 6;
        if (segs < 6) {
            segs = 6;
        }
        vg_vec2* poly = (vg_vec2*)malloc(sizeof(*poly) * (size_t)(segs + 2));
        if (!poly) {
            return VG_ERROR_OUT_OF_MEMORY;
        }
        poly[0] = desc->center;
        for (int s = 0; s <= segs; ++s) {
            float u = (float)s / (float)segs;
            float ang = a + span * u;
            poly[s + 1].x = desc->center.x + cosf(ang) * desc->radius_px;
            poly[s + 1].y = desc->center.y + sinf(ang) * desc->radius_px;
        }
        vg_fill_style fs;
        fs.intensity = outline_style->intensity;
        if (desc->colors && i < desc->value_count) {
            fs.color = desc->colors[i];
        } else {
            float hue = (float)i / (float)desc->value_count;
            fs.color = (vg_color){0.25f + 0.75f * hue, 0.9f - 0.5f * hue, 0.35f + 0.4f * (1.0f - hue), 0.65f};
        }
        fs.blend = VG_BLEND_ALPHA;
        vg_result r = vg_fill_convex(ctx, poly, (size_t)(segs + 2), &fs);
        if (r != VG_OK) {
            free(poly);
            return r;
        }
        r = vg_draw_polyline(ctx, poly + 1, (size_t)(segs + 1), outline_style, 0);
        free(poly);
        if (r != VG_OK) {
            return r;
        }

        if (desc->show_percent_labels) {
            char pct[32];
            int pct_i = (int)floorf((v / total) * 100.0f + 0.5f);
            if (desc->labels && desc->labels[i] && desc->labels[i][0] != '\0') {
                snprintf(pct, sizeof(pct), "%s %d%%", desc->labels[i], pct_i);
            } else {
                snprintf(pct, sizeof(pct), "%d%%", pct_i);
            }
            float am = a + span * 0.5f;
            float c = cosf(am);
            float s = sinf(am);
            vg_vec2 p0 = {
                desc->center.x + c * desc->radius_px * 0.92f,
                desc->center.y + s * desc->radius_px * 0.92f
            };
            vg_vec2 p1 = {
                desc->center.x + c * desc->radius_px * 1.12f,
                desc->center.y + s * desc->radius_px * 1.12f
            };
            float sign = (c >= 0.0f) ? 1.0f : -1.0f;
            vg_vec2 p2 = {
                p1.x + sign * (18.0f * ui),
                p1.y
            };
            vg_result lr = vg_draw_polyline(ctx, (vg_vec2[]){p0, p1, p2}, 3u, text_style, 0);
            if (lr != VG_OK) {
                return lr;
            }
            float tw = vg_measure_text(pct, 10.0f * text, 0.7f * text);
            float tx = (sign > 0.0f) ? (p2.x + 4.0f * ui) : (p2.x - tw - 4.0f * ui);
            r = vg_draw_text(ctx, pct, (vg_vec2){tx, p2.y - 5.0f * ui}, 10.0f * text, 0.7f * text, text_style, NULL);
            if (r != VG_OK) {
                return r;
            }
        }
        a += span;
    }

    vg_result r = vg_fill_circle(
        ctx,
        desc->center,
        desc->radius_px * 0.40f,
        &(vg_fill_style){.intensity = 1.0f, .color = {0.0f, 0.0f, 0.0f, 0.75f}, .blend = VG_BLEND_ALPHA},
        40
    );
    if (r != VG_OK) {
        return r;
    }
    r = vg_ui_ext_draw_circle(ctx, desc->center, desc->radius_px, 72, outline_style);
    if (r != VG_OK) {
        return r;
    }
    if (desc->label && desc->label[0] != '\0') {
        float tw = vg_measure_text(desc->label, 11.0f * text, 0.8f * text);
        r = vg_draw_text(
            ctx,
            desc->label,
            (vg_vec2){desc->center.x - tw * 0.5f, desc->center.y - 6.0f * ui},
            11.0f * text,
            0.8f * text,
            text_style,
            NULL
        );
        if (r != VG_OK) {
            return r;
        }
    }
    return VG_OK;
}
