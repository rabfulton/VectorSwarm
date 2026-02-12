#include "render.h"

#include "vg_ui.h"
#include "vg_ui_ext.h"
#include "vg_pointer.h"

#include <math.h>
#include <stdio.h>

typedef struct v3 {
    float x;
    float y;
    float z;
} v3;

/* Event Horizon wormhole mesh is static; cache it to avoid per-frame recompute. */
#define WORMHOLE_VN 84
#define WORMHOLE_ROWS 24
#define WORMHOLE_COLS 24

typedef struct wormhole_cache {
    int valid;
    float world_w;
    float world_h;
    vg_vec2 loop_rel[WORMHOLE_ROWS][WORMHOLE_VN];
    float loop_face[WORMHOLE_ROWS][WORMHOLE_VN];
    vg_vec2 rail_rel[WORMHOLE_COLS][WORMHOLE_ROWS];
    float rail_face[WORMHOLE_COLS][WORMHOLE_ROWS];
    float row_fade[WORMHOLE_ROWS];
} wormhole_cache;

static vg_stroke_style make_stroke(float width, float intensity, vg_color color, vg_blend_mode blend) {
    vg_stroke_style s;
    s.width_px = width;
    s.intensity = intensity;
    s.color = color;
    s.cap = VG_LINE_CAP_ROUND;
    s.join = VG_LINE_JOIN_ROUND;
    s.miter_limit = 4.0f;
    s.blend = blend;
    return s;
}

static vg_fill_style make_fill(float intensity, vg_color color, vg_blend_mode blend) {
    vg_fill_style f;
    f.intensity = intensity;
    f.color = color;
    f.blend = blend;
    return f;
}

static v3 v3_norm(v3 a) {
    const float l2 = a.x * a.x + a.y * a.y + a.z * a.z;
    if (l2 > 1e-12f) {
        const float inv = 1.0f / sqrtf(l2);
        a.x *= inv;
        a.y *= inv;
        a.z *= inv;
    }
    return a;
}

static float v3_dot(v3 a, v3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static float facing01_from_normal(v3 normal, v3 view_dir) {
    normal = v3_norm(normal);
    view_dir = v3_norm(view_dir);
    const float d = v3_dot(normal, view_dir);
    return (d > 0.0f) ? d : 0.0f;
}

static float facing_soft(float facing01, float cutoff01) {
    float f = facing01;
    if (f < 0.0f) f = 0.0f;
    if (f > 1.0f) f = 1.0f;
    float c = cutoff01;
    if (c < 0.0f) c = 0.0f;
    if (c > 0.95f) c = 0.95f;
    if (f <= c) {
        return 0.0f;
    }
    float t = (f - c) / (1.0f - c);
    return t * t * (3.0f - 2.0f * t);
}

static vg_result draw_polyline_culled(
    vg_context* ctx,
    const vg_vec2* pts,
    const float* facing01,
    int count,
    const vg_stroke_style* base,
    int closed,
    float cutoff01
) {
    if (!ctx || !pts || !facing01 || !base || count < 2) {
        return VG_OK;
    }
    for (int i = 0; i < count - 1; ++i) {
        const float f = facing_soft(0.5f * (facing01[i] + facing01[i + 1]), cutoff01);
        if (f <= 0.0f) {
            continue;
        }
        vg_stroke_style s = *base;
        s.intensity *= f;
        s.color.a *= f;
        const vg_vec2 seg[2] = {pts[i], pts[i + 1]};
        vg_result r = vg_draw_polyline(ctx, seg, 2, &s, 0);
        if (r != VG_OK) {
            return r;
        }
    }
    if (closed) {
        const float f = facing_soft(0.5f * (facing01[count - 1] + facing01[0]), cutoff01);
        if (f > 0.0f) {
            vg_stroke_style s = *base;
            s.intensity *= f;
            s.color.a *= f;
            const vg_vec2 seg[2] = {pts[count - 1], pts[0]};
            vg_result r = vg_draw_polyline(ctx, seg, 2, &s, 0);
            if (r != VG_OK) {
                return r;
            }
        }
    }
    return VG_OK;
}

static void wormhole_cache_build(wormhole_cache* c, float world_w, float world_h) {
    if (!c) {
        return;
    }

    c->world_w = world_w;
    c->world_h = world_h;
    c->valid = 1;

    const float h_span = world_h * 0.46f;
    const float rx_outer = world_w * 0.64f;
    const float rx_throat = world_w * 0.030f;
    const float ry_ratio = 0.18f;
    const float flare_s = 3.4f; /* larger = longer/narrower, still curved */
    const float flare_norm = 1.0f / sinhf(flare_s);
    const v3 view_dir = (v3){0.0f, 0.0f, 1.0f};

    float row_sy[WORMHOLE_ROWS];
    float row_rx[WORMHOLE_ROWS];
    float row_ry[WORMHOLE_ROWS];
    float row_drdy[WORMHOLE_ROWS];

    for (int j = 0; j < WORMHOLE_ROWS; ++j) {
        const float tj = (float)j / (float)(WORMHOLE_ROWS - 1);
        const float sy = tj * 2.0f - 1.0f;
        const float a = fabsf(sy);
        float k = sinhf(flare_s * a) * flare_norm;
        k = powf(k, 1.35f);
        row_sy[j] = sy;
        row_rx[j] = rx_throat + (rx_outer - rx_throat) * k;
        row_ry[j] = row_rx[j] * (ry_ratio * (0.92f + 0.10f * (1.0f - k)));
        c->row_fade[j] = 0.22f + powf(1.0f - a, 1.35f) * 0.78f;
    }

    for (int j = 0; j < WORMHOLE_ROWS; ++j) {
        const int j0 = (j > 0) ? (j - 1) : j;
        const int j1 = (j + 1 < WORMHOLE_ROWS) ? (j + 1) : j;
        const float y0 = row_sy[j0] * h_span;
        const float y1 = row_sy[j1] * h_span;
        const float dy = y1 - y0;
        row_drdy[j] = (fabsf(dy) > 1e-6f) ? ((row_rx[j1] - row_rx[j0]) / dy) : 0.0f;
    }

    for (int j = 0; j < WORMHOLE_ROWS; ++j) {
        const float sy = row_sy[j];
        const float rx = row_rx[j];
        const float ry = row_ry[j];
        const float drdy = row_drdy[j];

        for (int i = 0; i < WORMHOLE_VN; ++i) {
            const float ang = (float)i / (float)(WORMHOLE_VN - 1) * 6.28318530718f;
            const float ca = cosf(ang);
            const float sa = sinf(ang);
            c->loop_rel[j][i].x = ca * rx;
            c->loop_rel[j][i].y = sy * h_span + sa * ry;
            /* Surface of revolution normal: N ~ (cos(phi), -dr/dy, sin(phi)). */
            c->loop_face[j][i] = facing01_from_normal((v3){ca, -drdy, sa}, view_dir);
        }
    }

    for (int col = 0; col < WORMHOLE_COLS; ++col) {
        const float phi = (float)col / (float)WORMHOLE_COLS * 6.28318530718f;
        const float cp = cosf(phi);
        const float sp = sinf(phi);
        for (int j = 0; j < WORMHOLE_ROWS; ++j) {
            const float sy = row_sy[j];
            const float rx = row_rx[j];
            const float ry = row_ry[j];
            const float drdy = row_drdy[j];
            c->rail_rel[col][j].x = cp * rx;
            c->rail_rel[col][j].y = sy * h_span + sp * ry;
            c->rail_face[col][j] =
                facing01_from_normal((v3){cp, -drdy, sp}, view_dir) * c->row_fade[j];
        }
    }
}

static void wormhole_cache_ensure(wormhole_cache* c, float world_w, float world_h) {
    if (!c) {
        return;
    }
    if (!c->valid || fabsf(c->world_w - world_w) > 1e-3f || fabsf(c->world_h - world_h) > 1e-3f) {
        wormhole_cache_build(c, world_w, world_h);
    }
}

static float repeatf(float v, float period) {
    if (period <= 0.0f) {
        return v;
    }
    float x = fmodf(v, period);
    if (x < 0.0f) {
        x += period;
    }
    return x;
}

typedef struct palette_theme {
    vg_color primary;
    vg_color primary_dim;
    vg_color secondary;
    vg_color haze;
    vg_color star;
    vg_color ship;
    vg_color thruster;
} palette_theme;

static palette_theme get_palette_theme(int mode) {
    palette_theme p;
    switch (mode) {
        case 1: /* amber phosphor */
            p.primary = (vg_color){1.0f, 0.68f, 0.24f, 0.95f};
            p.primary_dim = (vg_color){0.85f, 0.52f, 0.16f, 0.42f};
            p.secondary = (vg_color){1.0f, 0.82f, 0.48f, 1.0f};
            p.haze = (vg_color){0.11f, 0.06f, 0.02f, 0.55f};
            p.star = (vg_color){1.0f, 0.74f, 0.42f, 1.0f};
            p.ship = (vg_color){1.0f, 0.75f, 0.35f, 1.0f};
            p.thruster = (vg_color){1.0f, 0.88f, 0.64f, 0.92f};
            break;
        case 2: /* ice/cyan */
            p.primary = (vg_color){0.40f, 0.95f, 1.0f, 0.95f};
            p.primary_dim = (vg_color){0.26f, 0.72f, 0.92f, 0.42f};
            p.secondary = (vg_color){0.72f, 0.98f, 1.0f, 1.0f};
            p.haze = (vg_color){0.02f, 0.07f, 0.10f, 0.55f};
            p.star = (vg_color){0.56f, 0.84f, 1.0f, 1.0f};
            p.ship = (vg_color){0.55f, 0.96f, 1.0f, 1.0f};
            p.thruster = (vg_color){0.75f, 0.96f, 1.0f, 0.92f};
            break;
        default: /* green */
            p.primary = (vg_color){0.22f, 1.0f, 0.58f, 0.95f};
            p.primary_dim = (vg_color){0.10f, 0.90f, 0.45f, 0.40f};
            p.secondary = (vg_color){0.52f, 1.0f, 0.76f, 1.0f};
            p.haze = (vg_color){0.02f, 0.10f, 0.08f, 0.55f};
            p.star = (vg_color){0.30f, 0.72f, 1.0f, 1.0f};
            p.ship = (vg_color){0.20f, 1.0f, 0.35f, 1.0f};
            p.thruster = (vg_color){0.70f, 1.0f, 0.80f, 0.92f};
            break;
    }
    return p;
}

static vg_result draw_text_vector_glow(
    vg_context* ctx,
    const char* text,
    vg_vec2 origin,
    float size_px,
    float letter_spacing_px,
    const vg_stroke_style* halo_style,
    const vg_stroke_style* main_style
) {
    vg_result r = vg_draw_text(ctx, text, origin, size_px, letter_spacing_px, halo_style, NULL);
    if (r != VG_OK) {
        return r;
    }
    return vg_draw_text(ctx, text, origin, size_px, letter_spacing_px, main_style, NULL);
}

static vg_result draw_teletype_overlay(
    vg_context* ctx,
    float w,
    float h,
    const char* text,
    const vg_stroke_style* halo_style,
    const vg_stroke_style* main_style
) {
    (void)w;
    if (!text || text[0] == '\0') {
        return VG_OK;
    }

    char line[256];
    size_t li = 0;
    int row = 0;
    const float x0 = 30.0f;
    const float y0 = h - 34.0f;
    const float lh = 17.0f;

    for (size_t i = 0;; ++i) {
        const char c = text[i];
        if (c == '\n' || c == '\0') {
            line[li] = '\0';
            vg_result r = draw_text_vector_glow(
                ctx,
                line,
                (vg_vec2){x0, y0 - lh * (float)row},
                12.5f,
                0.8f,
                halo_style,
                main_style
            );
            if (r != VG_OK) {
                return r;
            }
            li = 0;
            row += 1;
            if (c == '\0') {
                break;
            }
            continue;
        }
        if (li + 1u < sizeof(line)) {
            line[li++] = c;
        }
    }
    return VG_OK;
}

static vg_result draw_top_meters(
    vg_context* ctx,
    const game_state* g,
    const vg_stroke_style* halo_s,
    const vg_stroke_style* main_s
) {
    vg_ui_meter_style ms;
    ms.frame = *main_s;
    ms.frame.blend = VG_BLEND_ALPHA;
    ms.frame.intensity = main_s->intensity * 1.10f;
    ms.frame.width_px = fmaxf(main_s->width_px + 0.6f, 1.5f);
    ms.bg = *halo_s;
    ms.bg.blend = VG_BLEND_ALPHA;
    ms.bg.intensity = halo_s->intensity * 0.45f;
    ms.fill = *main_s;
    ms.fill.blend = VG_BLEND_ADDITIVE;
    ms.fill.intensity = main_s->intensity * 1.15f;
    ms.tick = *main_s;
    ms.tick.blend = VG_BLEND_ALPHA;
    ms.tick.width_px = 1.0f;
    ms.tick.intensity = 0.9f;
    ms.text = ms.tick;
    ms.text.width_px = 1.25f;

    vg_ui_meter_desc d;
    d.min_value = 0.0f;
    d.max_value = 100.0f;
    d.mode = VG_UI_METER_SEGMENTED;
    d.segments = 18;
    d.segment_gap_px = 2.0f;
    d.value_fmt = "%5.1f";
    d.show_value = 1;
    d.show_ticks = 1;

    const float w = g->world_w;
    const float h = g->world_h;
    const float margin_x = w * 0.04f;
    const float top_margin = 46.0f;
    const float total_w = w * 0.40f;
    const float meter_gap = w * 0.02f;
    const float meter_w = (total_w - meter_gap) * 0.5f;
    const float meter_h = 16.0f;
    const float y_top = h - top_margin - meter_h;
    const float x_block = w - margin_x - total_w;

    vg_result r;
    d.rect = (vg_rect){x_block, y_top, meter_w, meter_h};
    d.label = "VITALITY";
    d.value = ((float)g->lives / 3.0f) * 100.0f;
    r = vg_ui_meter_linear(ctx, &d, &ms);
    if (r != VG_OK) {
        return r;
    }

    d.rect = (vg_rect){x_block + meter_w + meter_gap, y_top, meter_w, meter_h};
    d.label = "QUOTA";
    d.min_value = 0.0f;
    d.max_value = 40.0f;
    d.mode = VG_UI_METER_SEGMENTED;
    d.segments = 20;
    d.segment_gap_px = 2.0f;
    d.value_fmt = "%4.0f";
    d.value = (float)g->kills;
    return vg_ui_meter_linear(ctx, &d, &ms);
}

static float norm_range(float v, float lo, float hi) {
    if (hi <= lo) {
        return 0.0f;
    }
    float t = (v - lo) / (hi - lo);
    if (t < 0.0f) {
        return 0.0f;
    }
    if (t > 1.0f) {
        return 1.0f;
    }
    return t;
}

static vg_result draw_crt_debug_ui(vg_context* ctx, float w, float h, const vg_crt_profile* crt, int selected) {
    static const char* labels[12] = {
        "BLOOM STRENGTH", "BLOOM RADIUS", "PERSISTENCE", "JITTER",
        "FLICKER", "BEAM CORE", "BEAM HALO", "BEAM INTENSITY",
        "VIGNETTE", "BARREL", "SCANLINE", "NOISE"
    };
    float value_display[12] = {
        crt->bloom_strength,
        crt->bloom_radius_px,
        crt->persistence_decay,
        crt->jitter_amount,
        crt->flicker_amount,
        crt->beam_core_width_px,
        crt->beam_halo_width_px,
        crt->beam_intensity,
        crt->vignette_strength,
        crt->barrel_distortion,
        crt->scanline_strength,
        crt->noise_strength
    };
    float value_01[12] = {
        norm_range(crt->bloom_strength, 0.0f, 3.0f),
        norm_range(crt->bloom_radius_px, 0.0f, 14.0f),
        norm_range(crt->persistence_decay, 0.70f, 0.94f),
        norm_range(crt->jitter_amount, 0.0f, 1.5f),
        norm_range(crt->flicker_amount, 0.0f, 1.0f),
        norm_range(crt->beam_core_width_px, 0.5f, 3.5f),
        norm_range(crt->beam_halo_width_px, 0.0f, 10.0f),
        norm_range(crt->beam_intensity, 0.2f, 3.0f),
        norm_range(crt->vignette_strength, 0.0f, 1.0f),
        norm_range(crt->barrel_distortion, 0.0f, 0.30f),
        norm_range(crt->scanline_strength, 0.0f, 1.0f),
        norm_range(crt->noise_strength, 0.0f, 0.30f)
    };

    vg_stroke_style panel = {
        .width_px = 1.4f,
        .intensity = 0.9f,
        .color = {0.15f, 1.0f, 0.38f, 0.9f},
        .cap = VG_LINE_CAP_ROUND,
        .join = VG_LINE_JOIN_ROUND,
        .miter_limit = 4.0f,
        .blend = VG_BLEND_ALPHA
    };
    vg_stroke_style text = panel;
    text.width_px = 1.15f;
    text.intensity = 1.0f;
    text.color = (vg_color){0.45f, 1.0f, 0.62f, 1.0f};

    vg_ui_slider_item items[12];
    for (int i = 0; i < 12; ++i) {
        items[i].label = labels[i];
        items[i].value_01 = value_01[i];
        items[i].value_display = value_display[i];
        items[i].selected = (i == selected);
    }

    vg_ui_slider_panel_desc ui = {
        .rect = {24.0f, h * 0.12f, w * 0.44f, h * 0.76f},
        .title_line_0 = "CRT DEBUG",
        .title_line_1 = "TAB TOGGLE  ARROWS ADJUST",
        .footer_line = NULL,
        .items = items,
        .item_count = 12u,
        .row_height_px = 34.0f,
        .label_size_px = 11.0f,
        .value_size_px = 11.5f,
        .border_style = panel,
        .text_style = text,
        .track_style = text,
        .knob_style = text
    };
    return vg_ui_draw_slider_panel(ctx, &ui);
}

static vg_result draw_acoustics_ui(vg_context* ctx, float w, float h, const render_metrics* metrics) {
    static const char* fire_labels[8] = {
        "WAVEFORM", "PITCH HZ", "ATTACK MS", "DECAY MS", "CUTOFF KHZ", "RESONANCE", "SWEEP ST", "SWEEP DECAY"
    };
    static const char* thr_labels[6] = {
        "LEVEL", "PITCH HZ", "ATTACK MS", "RELEASE MS", "CUTOFF KHZ", "RESONANCE"
    };

    vg_stroke_style panel = {
        .width_px = 1.45f,
        .intensity = 0.95f,
        .color = {0.22f, 1.0f, 0.55f, 0.95f},
        .cap = VG_LINE_CAP_ROUND,
        .join = VG_LINE_JOIN_ROUND,
        .miter_limit = 4.0f,
        .blend = VG_BLEND_ALPHA
    };
    vg_stroke_style text = panel;
    text.width_px = 1.2f;
    text.intensity = 1.0f;
    text.color = (vg_color){0.52f, 1.0f, 0.72f, 1.0f};

    vg_ui_slider_item fire_items[8];
    vg_ui_slider_item thr_items[6];
    for (int i = 0; i < 8; ++i) {
        fire_items[i].label = fire_labels[i];
        fire_items[i].value_01 = metrics->acoustics_value_01[i];
        fire_items[i].value_display = metrics->acoustics_display[i];
        fire_items[i].selected = 0;
    }
    for (int i = 0; i < 6; ++i) {
        thr_items[i].label = thr_labels[i];
        thr_items[i].value_01 = metrics->acoustics_value_01[8 + i];
        thr_items[i].value_display = metrics->acoustics_display[8 + i];
        thr_items[i].selected = 0;
    }

    const vg_rect fire_rect = {w * 0.02f, h * 0.10f, w * 0.47f, h * 0.80f};
    const vg_rect thr_rect = {w * 0.51f, h * 0.10f, w * 0.47f, h * 0.80f};
    const float btn_x_pad = fire_rect.w * 0.03f;
    const float btn_w_fire = fire_rect.w * 0.28f;
    const float btn_w_thr = thr_rect.w * 0.32f;
    const float btn_h = fire_rect.h * 0.042f;
    const float btn_y_off = fire_rect.h * 0.08f;
    const vg_rect fire_btn = {fire_rect.x + btn_x_pad, fire_rect.y + fire_rect.h - btn_y_off, btn_w_fire, btn_h};
    const vg_rect thr_btn = {thr_rect.x + btn_x_pad, thr_rect.y + thr_rect.h - btn_y_off, btn_w_thr, btn_h};
    const float save_w_fire = fire_rect.w * 0.15f;
    const float save_w_thr = thr_rect.w * 0.15f;
    const vg_rect fire_save_btn = {fire_rect.x + fire_rect.w - fire_rect.w * 0.03f - save_w_fire, fire_btn.y, save_w_fire, btn_h};
    const vg_rect thr_save_btn = {thr_rect.x + thr_rect.w - thr_rect.w * 0.03f - save_w_thr, thr_btn.y, save_w_thr, btn_h};
    vg_rect fire_slot_btn[ACOUSTICS_SLOT_COUNT];
    vg_rect thr_slot_btn[ACOUSTICS_SLOT_COUNT];
    {
        const float fire_sx = fire_btn.x + fire_btn.w + fire_rect.w * 0.02f;
        const float thr_sx = thr_btn.x + thr_btn.w + thr_rect.w * 0.02f;
        const float fire_sw = fire_rect.w * 0.052f;
        const float thr_sw = thr_rect.w * 0.052f;
        const float gap = fire_rect.w * 0.006f;
        for (int i = 0; i < ACOUSTICS_SLOT_COUNT; ++i) {
            fire_slot_btn[i] = (vg_rect){fire_sx + (fire_sw + gap) * (float)i, fire_btn.y, fire_sw, btn_h};
            thr_slot_btn[i] = (vg_rect){thr_sx + (thr_sw + gap) * (float)i, thr_btn.y, thr_sw, btn_h};
        }
    }
    const float row_y0 = fire_rect.h * 0.13f;
    const float row_h = fire_rect.h * 0.062f;
    const float fire_rows_top = fire_rect.y + row_y0 + row_h * 8.0f;
    const float thr_rows_top = thr_rect.y + row_y0 + row_h * 6.0f;
    const float display_margin = fire_rect.h * 0.02f;
    const float min_display_h = fire_rect.h * 0.11f;
    const float fire_display_y = fire_rows_top + display_margin;
    const float fire_display_h = fmaxf(min_display_h, fire_btn.y - fire_display_y - display_margin);
    const float thr_display_y = thr_rows_top + display_margin;
    const float thr_display_h = fmaxf(min_display_h, thr_btn.y - thr_display_y - display_margin);
    const vg_rect fire_display = {
        fire_rect.x + fire_rect.w * 0.03f,
        fire_display_y,
        fire_rect.w * 0.94f,
        fire_display_h
    };
    const vg_rect thr_display = {
        thr_rect.x + thr_rect.w * 0.03f,
        thr_display_y,
        thr_rect.w * 0.94f,
        thr_display_h
    };

    vg_ui_slider_panel_desc fire = {
        .rect = fire_rect,
        .title_line_0 = "SHIPYARD ACOUSTICS - FIRE",
        .title_line_1 = "ARROWS OR MOUSE TO TUNE",
        .footer_line = NULL,
        .items = fire_items,
        .item_count = 8u,
        .row_height_px = 34.0f,
        .label_size_px = 11.0f,
        .value_size_px = 11.5f,
        .border_style = panel,
        .text_style = text,
        .track_style = text,
        .knob_style = text
    };
    vg_result r = vg_ui_draw_slider_panel(ctx, &fire);
    if (r != VG_OK) {
        return r;
    }

    vg_ui_slider_panel_desc thr = fire;
    thr.rect = thr_rect;
    thr.title_line_0 = "SHIPYARD ACOUSTICS - THRUST";
    thr.items = thr_items;
    thr.item_count = 6u;
    r = vg_ui_draw_slider_panel(ctx, &thr);
    if (r != VG_OK) {
        return r;
    }

    {
        r = vg_draw_button(ctx, fire_btn, "TEST FIRE", 11.5f, &panel, &text, 0);
        if (r != VG_OK) {
            return r;
        }
        r = vg_draw_button(ctx, thr_btn, "TEST THRUST", 11.5f, &panel, &text, 0);
        if (r != VG_OK) {
            return r;
        }
        r = vg_draw_button(ctx, fire_save_btn, "SAVE", 11.0f, &panel, &text, 0);
        if (r != VG_OK) {
            return r;
        }
        r = vg_draw_button(ctx, thr_save_btn, "SAVE", 11.0f, &panel, &text, 0);
        if (r != VG_OK) {
            return r;
        }
        for (int i = 0; i < ACOUSTICS_SLOT_COUNT; ++i) {
            char label[2];
            label[0] = (char)('1' + i);
            label[1] = '\0';
            vg_stroke_style slot_text = text;
            if ((metrics->acoustics_fire_slot_defined[i] == 0) && (metrics->acoustics_fire_slot_selected != i)) {
                slot_text.intensity *= 0.55f;
            }
            r = vg_draw_button(
                ctx,
                fire_slot_btn[i],
                label,
                11.0f,
                &panel,
                &slot_text,
                (metrics->acoustics_fire_slot_selected == i) ? 1 : 0
            );
            if (r != VG_OK) {
                return r;
            }
            slot_text = text;
            if ((metrics->acoustics_thr_slot_defined[i] == 0) && (metrics->acoustics_thr_slot_selected != i)) {
                slot_text.intensity *= 0.55f;
            }
            r = vg_draw_button(
                ctx,
                thr_slot_btn[i],
                label,
                11.0f,
                &panel,
                &slot_text,
                (metrics->acoustics_thr_slot_selected == i) ? 1 : 0
            );
            if (r != VG_OK) {
                return r;
            }
        }

        r = vg_draw_rect(ctx, fire_display, &panel);
        if (r != VG_OK) {
            return r;
        }
        r = draw_text_vector_glow(
            ctx,
            "ENV + PITCH SWEEP",
            (vg_vec2){fire_display.x + 8.0f, fire_display.y + fire_display.h - 16.0f},
            10.5f,
            0.7f,
            &panel,
            &text
        );
        if (r != VG_OK) {
            return r;
        }

        vg_vec2 amp_line[32];
        vg_vec2 pitch_line[32];
        const float a_ms = metrics->acoustics_display[2];
        const float d_ms = metrics->acoustics_display[3];
        const float sweep_st = metrics->acoustics_display[6];
        const float sweep_d_ms = metrics->acoustics_display[7];
        for (int i = 0; i < 32; ++i) {
            const float t = (float)i / 31.0f;
            const float x = fire_display.x + 8.0f + (fire_display.w - 16.0f) * t;
            float amp;
            if (t < a_ms / 280.0f) {
                amp = t / (a_ms / 280.0f + 1e-4f);
            } else {
                float td = (t - a_ms / 280.0f) / (d_ms / 280.0f + 1e-4f);
                amp = 1.0f - td;
                if (amp < 0.0f) amp = 0.0f;
            }
            float p = 0.5f + (sweep_st / 24.0f) * expf(-t * (280.0f / (sweep_d_ms + 1.0f))) * 0.35f;
            amp_line[i] = (vg_vec2){x, fire_display.y + 8.0f + amp * (fire_display.h - 20.0f)};
            pitch_line[i] = (vg_vec2){x, fire_display.y + 8.0f + p * (fire_display.h - 20.0f)};
        }
        vg_stroke_style amp_s = text;
        amp_s.color = (vg_color){0.35f, 1.0f, 0.65f, 1.0f};
        vg_stroke_style pitch_s = text;
        pitch_s.color = (vg_color){0.85f, 1.0f, 0.25f, 1.0f};
        r = vg_draw_polyline(ctx, amp_line, 32, &amp_s, 0);
        if (r != VG_OK) {
            return r;
        }
        r = vg_draw_polyline(ctx, pitch_line, 32, &pitch_s, 0);
        if (r != VG_OK) {
            return r;
        }

        r = vg_draw_rect(ctx, thr_display, &panel);
        if (r != VG_OK) {
            return r;
        }
        r = draw_text_vector_glow(
            ctx,
            "OSCILLOSCOPE",
            (vg_vec2){thr_display.x + 8.0f, thr_display.y + thr_display.h - 16.0f},
            10.5f,
            0.7f,
            &panel,
            &text
        );
        if (r != VG_OK) {
            return r;
        }

        vg_stroke_style axis_s = panel;
        axis_s.width_px = 1.0f;
        axis_s.intensity = 0.65f;
        axis_s.color = (vg_color){0.28f, 0.96f, 0.58f, 0.72f};
        const vg_vec2 h_axis[2] = {
            {thr_display.x + 8.0f, thr_display.y + thr_display.h * 0.5f},
            {thr_display.x + thr_display.w - 8.0f, thr_display.y + thr_display.h * 0.5f}
        };
        r = vg_draw_polyline(ctx, h_axis, 2, &axis_s, 0);
        if (r != VG_OK) {
            return r;
        }

        vg_vec2 scope_line[ACOUSTICS_SCOPE_SAMPLES];
        for (int i = 0; i < ACOUSTICS_SCOPE_SAMPLES; ++i) {
            const float t = (float)i / (float)(ACOUSTICS_SCOPE_SAMPLES - 1);
            const float x = thr_display.x + 8.0f + (thr_display.w - 16.0f) * t;
            const float s = metrics->acoustics_scope[i];
            const float y = thr_display.y + thr_display.h * 0.5f + s * (thr_display.h * 0.35f);
            scope_line[i] = (vg_vec2){x, y};
        }
        vg_stroke_style scope_s = text;
        scope_s.color = (vg_color){0.42f, 0.95f, 1.0f, 1.0f};
        r = vg_draw_polyline(ctx, scope_line, ACOUSTICS_SCOPE_SAMPLES, &scope_s, 0);
        if (r != VG_OK) {
            return r;
        }
    }
    return VG_OK;
}

static vg_result draw_mouse_pointer(vg_context* ctx, float w, float h, const render_metrics* metrics, const vg_stroke_style* base) {
    if (!metrics->mouse_in_window) {
        return VG_OK;
    }
    const palette_theme pal = get_palette_theme(metrics->palette_mode);
    vg_stroke_style ps = *base;
    ps.blend = VG_BLEND_ALPHA;
    ps.width_px = fmaxf(1.0f, base->width_px * 0.90f);
    ps.intensity = base->intensity * 1.05f;
    ps.color = pal.secondary;
    vg_fill_style pf = {
        .intensity = 0.95f,
        .color = {pal.primary.r, pal.primary.g, pal.primary.b, 0.92f},
        .blend = VG_BLEND_ALPHA
    };
    const float size_px = fminf(w, h) * 0.032f;
    vg_pointer_desc pd = {
        .position = {metrics->mouse_x, metrics->mouse_y},
        .size_px = size_px,
        .angle_rad = 2.0943951f,
        .phase = 0.0f,
        .stroke = ps,
        .fill = pf,
        .use_fill = 1
    };
    return vg_draw_pointer(ctx, VG_POINTER_ASTEROIDS, &pd);
}

static vg_result draw_video_menu(vg_context* ctx, float w, float h, const render_metrics* metrics, float t_s) {
    (void)t_s;
    const palette_theme pal = get_palette_theme(metrics->palette_mode);
    vg_stroke_style frame = {
        .width_px = 2.2f,
        .intensity = 1.0f,
        .color = pal.primary,
        .cap = VG_LINE_CAP_ROUND,
        .join = VG_LINE_JOIN_ROUND,
        .miter_limit = 4.0f,
        .blend = VG_BLEND_ALPHA
    };
    vg_stroke_style txt = frame;
    txt.width_px = 1.2f;
    txt.color = pal.secondary;
    vg_fill_style haze = {
        .intensity = 0.28f,
        .color = pal.haze,
        .blend = VG_BLEND_ALPHA
    };

    const vg_rect panel = {w * 0.09f, h * 0.08f, w * 0.82f, h * 0.84f};
    const vg_rect inner = {panel.x + panel.w * 0.015f, panel.y + panel.h * 0.02f, panel.w * 0.97f, panel.h * 0.95f};
    vg_result r = vg_fill_rect(ctx, panel, &haze);
    if (r != VG_OK) {
        return r;
    }
    r = vg_draw_rect(ctx, panel, &frame);
    if (r != VG_OK) {
        return r;
    }
    r = vg_draw_rect(ctx, inner, &frame);
    if (r != VG_OK) {
        return r;
    }

    r = draw_text_vector_glow(
        ctx,
        "DISPLAY CONFIG",
        (vg_vec2){panel.x + panel.w * 0.04f, panel.y + panel.h - panel.h * 0.10f},
        18.0f,
        1.4f,
        &frame,
        &txt
    );
    if (r != VG_OK) {
        return r;
    }
    r = draw_text_vector_glow(
        ctx,
        "UP/DOWN SELECT  ENTER APPLY  2 EXIT",
        (vg_vec2){panel.x + panel.w * 0.04f, panel.y + panel.h - panel.h * 0.15f},
        10.0f,
        0.8f,
        &frame,
        &txt
    );
    if (r != VG_OK) {
        return r;
    }

    {
        static const char* labels[3] = {"GRN", "AMB", "ICE"};
        const float btn_h = panel.h * 0.055f;
        const float btn_w = panel.w * 0.09f;
        const float btn_gap = panel.w * 0.012f;
        const float btn_y = panel.y + panel.h - panel.h * 0.13f;
        const float btn_x0 = panel.x + panel.w - (3.0f * btn_w + 2.0f * btn_gap) - panel.w * 0.04f;
        for (int i = 0; i < 3; ++i) {
            const vg_rect b = {btn_x0 + (float)i * (btn_w + btn_gap), btn_y, btn_w, btn_h};
            r = vg_draw_button(ctx, b, labels[i], 11.0f, &frame, &txt, (metrics->palette_mode == i) ? 1 : 0);
            if (r != VG_OK) {
                return r;
            }
        }
    }

    const int item_count = VIDEO_MENU_RES_COUNT + 1;
    const float row_h = panel.h * 0.082f;
    const float row_w = panel.w * 0.36f;
    const float row_x = panel.x + panel.w * 0.05f;
    const float row_y0 = panel.y + panel.h * 0.68f;
    for (int i = 0; i < item_count; ++i) {
        vg_rect row = {row_x, row_y0 - row_h * (float)i, row_w, row_h * 0.72f};
        char label[64];
        if (i == 0) {
            snprintf(label, sizeof(label), "FULLSCREEN NATIVE");
        } else {
            const int idx = i - 1;
            snprintf(label, sizeof(label), "%d x %d", metrics->video_res_w[idx], metrics->video_res_h[idx]);
        }
        r = vg_draw_button(ctx, row, label, 12.0f, &frame, &txt, (metrics->video_menu_selected == i) ? 1 : 0);
        if (r != VG_OK) {
            return r;
        }
    }

    {
        const char* mode = metrics->video_menu_fullscreen ? "ACTIVE MODE: FULLSCREEN" : "ACTIVE MODE: WINDOWED";
        r = draw_text_vector_glow(
            ctx,
            mode,
            (vg_vec2){panel.x + panel.w * 0.05f, panel.y + panel.h * 0.10f},
            11.0f,
            0.8f,
            &frame,
            &txt
        );
        if (r != VG_OK) {
            return r;
        }
    }

    {
        const vg_rect lab = {panel.x + panel.w * 0.42f, panel.y + panel.h * 0.07f, panel.w * 0.54f, panel.h * 0.86f};
        {
            static const char* dial_labels[VIDEO_MENU_DIAL_COUNT] = {
                "BLOOM", "PERSIST", "JITTER",
                "FLICKER", "SCANLINE", "NOISE",
                "VIGNETTE", "BARREL", "BEAM"
            };
            vg_ui_meter_style ms;
            ms.frame = frame;
            ms.fill = frame;
            ms.bg = frame;
            ms.tick = txt;
            ms.text = txt;
            vg_ui_meter_desc d;
            d.rect = lab;
            d.min_value = 0.0f;
            d.max_value = 100.0f;
            d.mode = VG_UI_METER_SEGMENTED;
            d.segments = 12;
            d.segment_gap_px = 2.0f;
            d.value_fmt = "%3.0f";
            d.show_value = 0;
            d.show_ticks = 1;
            const float radius = lab.w * 0.070f;
            for (int i = 0; i < VIDEO_MENU_DIAL_COUNT; ++i) {
                const int row = i / 3;
                const int col = i % 3;
                const vg_vec2 c = {
                    lab.x + lab.w * (0.18f + 0.32f * (float)col),
                    lab.y + lab.h * (0.72f - 0.29f * (float)row)
                };
                float v = metrics->video_dial_01[i];
                if (v < 0.0f) v = 0.0f;
                if (v > 1.0f) v = 1.0f;
                d.label = dial_labels[i];
                d.value = v * 100.0f;
                r = vg_ui_meter_radial(ctx, c, radius, &d, &ms);
                if (r != VG_OK) {
                    return r;
                }
            }
        }
    }
    return VG_OK;
}

static vg_result draw_parallax_landscape(
    vg_context* ctx,
    float w,
    float h,
    float cam_x,
    float parallax,
    float base_y,
    float amp,
    const vg_stroke_style* halo,
    const vg_stroke_style* main
) {
    enum { N = 56 };
    vg_vec2 line[N];
    for (int i = 0; i < N; ++i) {
        const float x = ((float)i / (float)(N - 1)) * w;
        const float wx = cam_x * parallax + x;
        const float y =
            base_y +
            sinf(wx * 0.010f) * amp +
            sinf(wx * 0.026f + 1.4f) * amp * 0.55f +
            sinf(wx * 0.046f + 2.2f) * amp * 0.20f;
        line[i].x = x;
        line[i].y = y;
    }

    vg_result r = vg_draw_polyline(ctx, line, N, halo, 0);
    if (r != VG_OK) {
        return r;
    }
    return vg_draw_polyline(ctx, line, N, main, 0);
}

static float cylinder_period(const game_state* g) {
    return fmaxf(g->world_w * 2.4f, 1.0f);
}

static vg_vec2 project_cylinder_point(const game_state* g, float x, float y, float* depth01) {
    const float w = g->world_w;
    const float h = g->world_h;
    const float cx = w * 0.5f;
    const float cy = h * 0.50f;
    const float period = cylinder_period(g);
    const float theta = (x - g->camera_x) / period * 6.28318530718f;
    const float depth = cosf(theta) * 0.5f + 0.5f;
    const float radius = w * 0.485f;
    const float y_scale = 0.44f + depth * 0.62f;
    if (depth01) {
        *depth01 = depth;
    }
    return (vg_vec2){
        cx + sinf(theta) * radius,
        cy + (y - cy) * y_scale
    };
}

static vg_result draw_cylinder_wire(vg_context* ctx, const game_state* g, const vg_stroke_style* halo, const vg_stroke_style* main, int level_style) {
    const float period = cylinder_period(g);
    enum { N = 96 };
    const float ring_y[] = {g->world_h * 0.06f, g->world_h * 0.46f, g->world_h * 0.86f};
    vg_stroke_style cyl_h = *halo;
    vg_stroke_style cyl_m = *main;
    cyl_h.color = (vg_color){0.40f, 0.95f, 1.0f, 0.22f};
    cyl_m.color = (vg_color){0.40f, 0.95f, 1.0f, 0.50f};
    cyl_h.intensity *= 0.62f;
    cyl_m.intensity *= 0.58f;
    if (level_style != LEVEL_STYLE_EVENT_HORIZON) {
        for (int r = 1; r < 3; ++r) {
            vg_vec2 line[N];
            float z01[N];
            for (int i = 0; i < N; ++i) {
                const float u = (float)i / (float)(N - 1);
                const float xw = g->camera_x + (u - 0.5f) * period;
                line[i] = project_cylinder_point(g, xw, ring_y[r], &z01[i]);
            }
            for (int i = 0; i < N - 1; ++i) {
                const float d = 0.5f * (z01[i] + z01[i + 1]);
                const float fade = 0.03f + d * d * 0.97f;
                vg_stroke_style sh = cyl_h;
                vg_stroke_style sm = cyl_m;
                sh.intensity *= fade;
                sm.intensity *= fade;
                sh.color.a *= fade;
                sm.color.a *= fade;
                const vg_vec2 seg[2] = {line[i], line[i + 1]};
                vg_result vr = vg_draw_polyline(ctx, seg, 2, &sh, 0);
                if (vr != VG_OK) {
                    return vr;
                }
                vr = vg_draw_polyline(ctx, seg, 2, &sm, 0);
                if (vr != VG_OK) {
                    return vr;
                }
            }
        }
    }

    /* Flat radar-plate ground plane near bottom (Enemy Radar level). */
    if (level_style == LEVEL_STYLE_ENEMY_RADAR) {
    {
        vg_stroke_style tr_h = *halo;
        vg_stroke_style tr_m = *main;
        tr_h.intensity *= 1.35f;
        tr_m.intensity *= 1.35f;
        vg_vec2 edge[N];
        vg_vec2 radar_edge[N];
        float edge_depth[N];
        float cx = 0.0f;
        float cy = 0.0f;
        for (int i = 0; i < N; ++i) {
            const float u = (float)i / (float)(N - 1);
            const float xw = g->camera_x + (u - 0.5f) * period;
            edge[i] = project_cylinder_point(g, xw, ring_y[0], &edge_depth[i]);
            cx += edge[i].x;
            cy += edge[i].y;
        }
        cx /= (float)N;
        cy /= (float)N;
        {
            const float radar_scale = 1.45f;
            for (int i = 0; i < N; ++i) {
                radar_edge[i].x = cx + (edge[i].x - cx) * radar_scale;
                radar_edge[i].y = cy + (edge[i].y - cy) * radar_scale;
            }
        }

        for (int ring = 0; ring < 8; ++ring) {
            const float rs = 1.0f - 0.11f * (float)ring;
            vg_vec2 loop[N];
            for (int i = 0; i < N; ++i) {
                loop[i].x = cx + (radar_edge[i].x - cx) * rs;
                loop[i].y = cy + (radar_edge[i].y - cy) * rs;
            }
            for (int i = 0; i < N - 1; ++i) {
                const float d = 0.5f * (edge_depth[i] + edge_depth[i + 1]);
                const float fade = 0.03f + d * d * 0.97f;
                vg_stroke_style sh = tr_h;
                vg_stroke_style sm = tr_m;
                sh.intensity *= fade;
                sm.intensity *= fade;
                sh.color.a *= fade;
                sm.color.a *= fade;
                const vg_vec2 seg[2] = {loop[i], loop[i + 1]};
                vg_result vr = vg_draw_polyline(ctx, seg, 2, &sh, 0);
                if (vr != VG_OK) {
                    return vr;
                }
                vr = vg_draw_polyline(ctx, seg, 2, &sm, 0);
                if (vr != VG_OK) {
                    return vr;
                }
            }
        }

        for (int s = 0; s < 20; ++s) {
            const int idx = (s * (N - 1)) / 20;
            vg_vec2 spoke[2] = {
                {cx, cy},
                {radar_edge[idx].x, radar_edge[idx].y}
            };
            const float d = edge_depth[idx];
            const float fade = 0.03f + d * d * 0.97f;
            vg_stroke_style sh = tr_h;
            sh.intensity *= fade;
            sh.color.a *= fade;
            vg_result vr = vg_draw_polyline(ctx, spoke, 2, &sh, 0);
            if (vr != VG_OK) {
                return vr;
            }
        }

        {
            const float sw = fmodf(g->t * 1.6f, 6.28318530718f);
            for (int t = 7; t >= 0; --t) {
                const float lag = (float)t * 0.14f;
                const float a = sw - lag;
                float u = fmodf(a / 6.28318530718f, 1.0f);
                if (u < 0.0f) {
                    u += 1.0f;
                }
                const float fi = u * (float)(N - 1);
                int i0 = (int)fi;
                int i1 = i0 + 1;
                if (i1 >= N) {
                    i1 = 0;
                }
                const float ft = fi - (float)i0;
                const vg_vec2 tip = {
                    radar_edge[i0].x + (radar_edge[i1].x - radar_edge[i0].x) * ft,
                    radar_edge[i0].y + (radar_edge[i1].y - radar_edge[i0].y) * ft
                };
                const float tip_depth = edge_depth[i0] + (edge_depth[i1] - edge_depth[i0]) * ft;
                const float trail = 1.0f - (float)t / 8.0f;
                const vg_vec2 sweep[2] = {
                    {cx, cy},
                    {tip.x, tip.y}
                };
                vg_stroke_style sws = tr_m;
                const float zfade = 0.03f + tip_depth * tip_depth * 0.97f;
                sws.intensity *= (0.35f + trail * 1.05f) * zfade;
                sws.width_px *= 0.80f + trail * 0.35f;
                sws.color.a = (0.08f + trail * 0.72f) * zfade;
                vg_result vr = vg_draw_polyline(ctx, sweep, 2, &sws, 0);
                if (vr != VG_OK) {
                    return vr;
                }
            }
        }

        for (size_t i = 0; i < MAX_ENEMIES; ++i) {
            const enemy* e = &g->enemies[i];
            if (!e->active) {
                continue;
            }
            const float theta = (e->b.x - g->camera_x) / period * 6.28318530718f;
            const float z01 = cosf(theta) * 0.5f + 0.5f;
            const float zfade = 0.03f + z01 * z01 * 0.97f;
            const float r01 = 0.24f + 0.70f * (e->b.y / fmaxf(g->world_h, 1.0f));
            const vg_vec2 edge_p = project_cylinder_point(g, g->camera_x + theta / 6.28318530718f * period, ring_y[0], NULL);
            const vg_vec2 blip = {
                cx + (edge_p.x - cx) * 1.45f * r01,
                cy + (edge_p.y - cy) * 1.45f * r01
            };
            vg_fill_style bf = {
                .intensity = 1.0f * zfade,
                .color = {tr_m.color.r, tr_m.color.g, tr_m.color.b, 0.95f * zfade},
                .blend = VG_BLEND_ADDITIVE
            };
            vg_result vr = vg_fill_circle(ctx, blip, 1.8f, &bf, 10);
            if (vr != VG_OK) {
                return vr;
            }
        }
    }
    }

    if (level_style == LEVEL_STYLE_EVENT_HORIZON) {
        /* Classic spacetime-fabric hourglass (wormhole throat) through cylinder center. */
        static wormhole_cache wh;
        wormhole_cache_ensure(&wh, g->world_w, g->world_h);

        const vg_vec2 vc = project_cylinder_point(g, g->camera_x, g->world_h * 0.50f, NULL);
        const float cx = vc.x;
        const float cy = vc.y;
        for (int j = 0; j < WORMHOLE_ROWS; ++j) {
            const float fade = wh.row_fade[j];

            vg_vec2 loop[WORMHOLE_VN];
            for (int i = 0; i < WORMHOLE_VN; ++i) {
                loop[i].x = cx + wh.loop_rel[j][i].x;
                loop[i].y = cy + wh.loop_rel[j][i].y;
            }

            vg_stroke_style vh = *halo;
            vg_stroke_style vm = *main;
            vh.color = (vg_color){0.38f, 0.92f, 1.0f, 0.20f * fade};
            vm.color = (vg_color){0.44f, 0.97f, 1.0f, 0.58f * fade};
            vh.intensity *= 0.42f + fade * 0.48f;
            vm.intensity *= 0.48f + fade * 0.56f;
            vg_result vr = draw_polyline_culled(ctx, loop, wh.loop_face[j], WORMHOLE_VN, &vh, 1, 0.02f);
            if (vr != VG_OK) {
                return vr;
            }
            vr = draw_polyline_culled(ctx, loop, wh.loop_face[j], WORMHOLE_VN, &vm, 1, 0.02f);
            if (vr != VG_OK) {
                return vr;
            }
        }

        for (int c = 0; c < WORMHOLE_COLS; ++c) {
            vg_vec2 rail[WORMHOLE_ROWS];
            for (int j = 0; j < WORMHOLE_ROWS; ++j) {
                rail[j].x = cx + wh.rail_rel[c][j].x;
                rail[j].y = cy + wh.rail_rel[c][j].y;
            }
            vg_stroke_style rs = *main;
            rs.color = (vg_color){0.38f, 0.92f, 1.0f, 0.30f};
            rs.intensity *= 0.52f;
            vg_result vr = draw_polyline_culled(ctx, rail, wh.rail_face[c], WORMHOLE_ROWS, &rs, 0, 0.02f);
            if (vr != VG_OK) {
                return vr;
            }
        }
    }
    return VG_OK;
}

typedef struct ship_pose {
    float x;
    float y;
    float fx;
    float s;
} ship_pose;

static vg_result draw_ship_hull(vg_context* ctx, ship_pose p, const vg_stroke_style* ship_style) {
    const vg_vec2 hull[] = {
        {p.x + p.fx * -36.0f * p.s, p.y - 7.0f * p.s},
        {p.x + p.fx * -20.0f * p.s, p.y - 13.0f * p.s},
        {p.x + p.fx * 8.0f * p.s, p.y - 11.0f * p.s},
        {p.x + p.fx * 26.0f * p.s, p.y - 6.0f * p.s},
        {p.x + p.fx * 41.0f * p.s, p.y - 1.0f * p.s},
        {p.x + p.fx * 47.0f * p.s, p.y},
        {p.x + p.fx * 41.0f * p.s, p.y + 1.0f * p.s},
        {p.x + p.fx * 26.0f * p.s, p.y + 6.0f * p.s},
        {p.x + p.fx * 8.0f * p.s, p.y + 11.0f * p.s},
        {p.x + p.fx * -20.0f * p.s, p.y + 13.0f * p.s},
        {p.x + p.fx * -36.0f * p.s, p.y + 7.0f * p.s},
        {p.x + p.fx * -36.0f * p.s, p.y - 7.0f * p.s}
    };
    vg_result r = vg_draw_polyline(ctx, hull, sizeof(hull) / sizeof(hull[0]), ship_style, 0);
    if (r != VG_OK) {
        return r;
    }

    const vg_vec2 wing_top[] = {
        {p.x + p.fx * -18.0f * p.s, p.y - 13.0f * p.s},
        {p.x + p.fx * -5.0f * p.s, p.y - 24.0f * p.s},
        {p.x + p.fx * 13.0f * p.s, p.y - 13.0f * p.s}
    };
    r = vg_draw_polyline(ctx, wing_top, sizeof(wing_top) / sizeof(wing_top[0]), ship_style, 0);
    if (r != VG_OK) {
        return r;
    }

    const vg_vec2 wing_bot[] = {
        {p.x + p.fx * -18.0f * p.s, p.y + 13.0f * p.s},
        {p.x + p.fx * -5.0f * p.s, p.y + 24.0f * p.s},
        {p.x + p.fx * 13.0f * p.s, p.y + 13.0f * p.s}
    };
    r = vg_draw_polyline(ctx, wing_bot, sizeof(wing_bot) / sizeof(wing_bot[0]), ship_style, 0);
    if (r != VG_OK) {
        return r;
    }

    const vg_vec2 spine[] = {
        {p.x + p.fx * -26.0f * p.s, p.y},
        {p.x + p.fx * 43.0f * p.s, p.y}
    };
    return vg_draw_polyline(ctx, spine, 2, ship_style, 0);
}

static vg_result draw_ship_canopy(vg_context* ctx, ship_pose p, const vg_stroke_style* ship_style) {
    const vg_vec2 canopy[] = {
        {p.x + p.fx * -8.0f * p.s, p.y - 5.0f * p.s},
        {p.x + p.fx * 11.0f * p.s, p.y - 3.0f * p.s},
        {p.x + p.fx * 15.0f * p.s, p.y},
        {p.x + p.fx * 11.0f * p.s, p.y + 3.0f * p.s},
        {p.x + p.fx * -8.0f * p.s, p.y + 5.0f * p.s},
        {p.x + p.fx * -8.0f * p.s, p.y - 5.0f * p.s}
    };
    return vg_draw_polyline(ctx, canopy, sizeof(canopy) / sizeof(canopy[0]), ship_style, 0);
}

static vg_result draw_ship_hardpoints(vg_context* ctx, ship_pose p, const vg_stroke_style* ship_style) {
    const vg_vec2 top_rail[] = {
        {p.x + p.fx * -1.0f * p.s, p.y - 16.0f * p.s},
        {p.x + p.fx * 20.0f * p.s, p.y - 16.0f * p.s}
    };
    const vg_vec2 bot_rail[] = {
        {p.x + p.fx * -1.0f * p.s, p.y + 16.0f * p.s},
        {p.x + p.fx * 20.0f * p.s, p.y + 16.0f * p.s}
    };
    vg_result r = vg_draw_polyline(ctx, top_rail, 2, ship_style, 0);
    if (r != VG_OK) {
        return r;
    }
    r = vg_draw_polyline(ctx, bot_rail, 2, ship_style, 0);
    if (r != VG_OK) {
        return r;
    }
    const vg_vec2 nose_gun[] = {
        {p.x + p.fx * 44.0f * p.s, p.y},
        {p.x + p.fx * 57.0f * p.s, p.y}
    };
    return vg_draw_polyline(ctx, nose_gun, 2, ship_style, 0);
}

static vg_result draw_ship_pod(vg_context* ctx, ship_pose p, float y_off, const vg_stroke_style* ship_style) {
    const vg_vec2 pod[] = {
        {p.x + p.fx * 1.0f * p.s, p.y + y_off - 4.0f * p.s},
        {p.x + p.fx * 16.0f * p.s, p.y + y_off - 4.0f * p.s},
        {p.x + p.fx * 23.0f * p.s, p.y + y_off},
        {p.x + p.fx * 16.0f * p.s, p.y + y_off + 4.0f * p.s},
        {p.x + p.fx * 1.0f * p.s, p.y + y_off + 4.0f * p.s},
        {p.x + p.fx * 1.0f * p.s, p.y + y_off - 4.0f * p.s}
    };
    vg_result r = vg_draw_polyline(ctx, pod, sizeof(pod) / sizeof(pod[0]), ship_style, 0);
    if (r != VG_OK) {
        return r;
    }
    const vg_vec2 pod_gun[] = {
        {p.x + p.fx * 23.0f * p.s, p.y + y_off},
        {p.x + p.fx * 35.0f * p.s, p.y + y_off}
    };
    return vg_draw_polyline(ctx, pod_gun, 2, ship_style, 0);
}

static vg_result draw_ship_thruster(vg_context* ctx, ship_pose p, const vg_fill_style* thruster_fill) {
    vg_result r = vg_fill_circle(ctx, (vg_vec2){p.x + p.fx * -39.0f * p.s, p.y}, 3.2f * p.s, thruster_fill, 12);
    if (r != VG_OK) {
        return r;
    }
    return vg_fill_circle(ctx, (vg_vec2){p.x + p.fx * -44.0f * p.s, p.y}, 2.1f * p.s, thruster_fill, 10);
}

static vg_result draw_player_ship(
    vg_context* ctx,
    const game_state* g,
    const vg_stroke_style* ship_style,
    const vg_fill_style* thruster_fill
) {
    ship_pose p = {
        .x = g->player.b.x,
        .y = g->player.b.y,
        .fx = (g->player.facing_x < 0.0f) ? -1.0f : 1.0f,
        .s = 0.65f
    };

    vg_result r = draw_ship_hull(ctx, p, ship_style);
    if (r != VG_OK) {
        return r;
    }
    r = draw_ship_canopy(ctx, p, ship_style);
    if (r != VG_OK) {
        return r;
    }
    r = draw_ship_hardpoints(ctx, p, ship_style);
    if (r != VG_OK) {
        return r;
    }
    if (g->weapon_level >= 2) {
        r = draw_ship_pod(ctx, p, -16.0f * p.s, ship_style);
        if (r != VG_OK) {
            return r;
        }
    }
    if (g->weapon_level >= 3) {
        r = draw_ship_pod(ctx, p, 16.0f * p.s, ship_style);
        if (r != VG_OK) {
            return r;
        }
    }
    return draw_ship_thruster(ctx, p, thruster_fill);
}

vg_result render_frame(vg_context* ctx, const game_state* g, const render_metrics* metrics) {
    const palette_theme pal = get_palette_theme(metrics->palette_mode);
    vg_crt_profile crt;
    vg_get_crt_profile(ctx, &crt);
    float persistence = crt.persistence_decay;
    if (persistence < 0.0f) {
        persistence = 0.0f;
    }
    if (persistence > 1.0f) {
        persistence = 1.0f;
    }
    float frame_decay = powf(persistence, metrics->dt * 95.0f);
    float fade_alpha = 1.0f - frame_decay;
    if (fade_alpha < 0.08f) {
        fade_alpha = 0.08f;
    }
    if (metrics->force_clear) {
        fade_alpha = 1.0f;
    }
    const float flicker_n = 0.6f * sinf(g->t * 13.0f + 0.7f) + 0.4f * sinf(g->t * 23.0f);
    float intensity_scale = 1.0f + crt.flicker_amount * 0.08f * flicker_n;
    if (intensity_scale < 0.0f) {
        intensity_scale = 0.0f;
    }
    const vg_fill_style bg = make_fill(1.0f, (vg_color){0.0f, 0.0f, 0.0f, fade_alpha}, VG_BLEND_ALPHA);
    const vg_fill_style star_fill = make_fill(0.75f * intensity_scale, pal.star, VG_BLEND_ADDITIVE);
    const vg_stroke_style ship_style = make_stroke(2.0f, 1.15f * intensity_scale, pal.ship, VG_BLEND_ADDITIVE);
    const vg_stroke_style bullet_style = make_stroke(2.6f, 1.0f * intensity_scale, (vg_color){1.0f, 0.9f, 0.55f, 1.0f}, VG_BLEND_ADDITIVE);
    const vg_stroke_style enemy_style = make_stroke(2.5f, 1.0f * intensity_scale, (vg_color){1.0f, 0.3f, 0.3f, 1.0f}, VG_BLEND_ADDITIVE);
    const vg_fill_style thruster_fill = make_fill(1.0f * intensity_scale, pal.thruster, VG_BLEND_ADDITIVE);

    const float main_line_width = 1.5f;
    const vg_stroke_style star_halo = make_stroke(
        (main_line_width * crt.beam_core_width_px + crt.beam_halo_width_px * 0.45f) * (1.0f + crt.bloom_radius_px * 0.03f),
        0.30f * crt.beam_intensity * intensity_scale * (1.0f + crt.bloom_strength * 0.20f),
        (vg_color){pal.star.r, pal.star.g, pal.star.b, 0.35f},
        VG_BLEND_ADDITIVE
    );
    const vg_stroke_style star_main = make_stroke(
        main_line_width * crt.beam_core_width_px * 0.70f,
        0.95f * crt.beam_intensity * intensity_scale,
        (vg_color){pal.star.r, pal.star.g, pal.star.b, 0.85f},
        VG_BLEND_ADDITIVE
    );
    const vg_stroke_style txt_halo = make_stroke(
        (main_line_width * crt.beam_core_width_px + crt.beam_halo_width_px * 0.55f) * (1.0f + crt.bloom_radius_px * 0.02f),
        0.42f * crt.beam_intensity * intensity_scale * (1.0f + crt.bloom_strength * 0.15f),
        (vg_color){pal.primary_dim.r, pal.primary_dim.g, pal.primary_dim.b, 0.45f},
        VG_BLEND_ADDITIVE
    );
    const vg_stroke_style txt_main = make_stroke(
        main_line_width * crt.beam_core_width_px,
        1.2f * crt.beam_intensity * intensity_scale,
        (vg_color){pal.primary.r, pal.primary.g, pal.primary.b, 1.0f},
        VG_BLEND_ADDITIVE
    );
    vg_stroke_style over_halo = txt_halo;
    vg_stroke_style over_main = txt_main;
    over_halo.color = (vg_color){1.0f, 0.4f, 0.4f, 0.45f};
    over_main.color = (vg_color){1.0f, 0.35f, 0.35f, 1.0f};
    over_halo.width_px *= 1.15f;
    over_main.width_px *= 1.15f;
    const vg_stroke_style land_halo = make_stroke(
        main_line_width * crt.beam_core_width_px + crt.beam_halo_width_px * 0.70f,
        0.28f * crt.beam_intensity * intensity_scale,
        (vg_color){pal.primary_dim.r, pal.primary_dim.g, pal.primary_dim.b, 0.30f},
        VG_BLEND_ADDITIVE
    );
    const vg_stroke_style land_main = make_stroke(
        main_line_width * crt.beam_core_width_px * 0.90f,
        0.92f * crt.beam_intensity * intensity_scale,
        (vg_color){pal.primary.r, pal.primary.g, pal.primary.b, 0.85f},
        VG_BLEND_ADDITIVE
    );

    if (metrics->show_acoustics) {
        vg_result r = vg_fill_rect(ctx, (vg_rect){0.0f, 0.0f, g->world_w, g->world_h}, &bg);
        if (r != VG_OK) {
            return r;
        }
        r = draw_acoustics_ui(ctx, g->world_w, g->world_h, metrics);
        if (r != VG_OK) {
            return r;
        }
        return draw_mouse_pointer(ctx, g->world_w, g->world_h, metrics, &txt_main);
    }
    if (metrics->show_video_menu) {
        vg_result r = vg_fill_rect(ctx, (vg_rect){0.0f, 0.0f, g->world_w, g->world_h}, &bg);
        if (r != VG_OK) {
            return r;
        }
        r = draw_video_menu(ctx, g->world_w, g->world_h, metrics, g->t);
        if (r != VG_OK) {
            return r;
        }
        return draw_mouse_pointer(ctx, g->world_w, g->world_h, metrics, &txt_main);
    }

    const float jx = sinf(g->t * 17.0f + 0.2f) * crt.jitter_amount * 0.75f;
    const float jy = cosf(g->t * 21.0f) * crt.jitter_amount * 0.75f;

    vg_transform_translate(ctx, jx, jy);

    vg_result r = vg_fill_rect(ctx, (vg_rect){0.0f, 0.0f, g->world_w, g->world_h}, &bg);
    if (r != VG_OK) {
        return r;
    }

    if (g->level_style != LEVEL_STYLE_DEFENDER) {
        const float period = cylinder_period(g);
        vg_stroke_style cyl_halo = land_halo;
        vg_stroke_style cyl_main = land_main;
        cyl_halo.intensity *= 1.15f;
        cyl_main.intensity *= 1.20f;
        r = draw_cylinder_wire(ctx, g, &cyl_halo, &cyl_main, g->level_style);
        if (r != VG_OK) {
            return r;
        }

        for (size_t i = 0; i < MAX_STARS; ++i) {
            const float su = repeatf(g->stars[i].x - g->camera_x * 0.22f, g->world_w) / fmaxf(g->world_w, 1.0f);
            const float sx_world = g->camera_x + (su - 0.5f) * period;
            float depth = 0.0f;
            const vg_vec2 sp = project_cylinder_point(g, sx_world, g->stars[i].y, &depth);
            vg_fill_style sf = star_fill;
            sf.intensity *= 0.45f + depth * 0.9f;
            r = vg_fill_circle(ctx, sp, g->stars[i].size * (0.5f + depth), &sf, 8);
            if (r != VG_OK) {
                return r;
            }
        }

        for (size_t i = 0; i < MAX_PARTICLES; ++i) {
            const particle* p = &g->particles[i];
            if (!p->active) {
                continue;
            }
            float depth = 0.0f;
            const vg_vec2 pp = project_cylinder_point(g, p->b.x, p->b.y, &depth);
            vg_fill_style pf = make_fill(1.0f, (vg_color){p->r, p->g, p->bcol, p->a}, VG_BLEND_ADDITIVE);
            r = vg_fill_circle(ctx, pp, p->size * (0.35f + 0.9f * depth), &pf, 8);
            if (r != VG_OK) {
                return r;
            }
        }

        if (g->lives > 0) {
            game_state gp = *g;
            gp.player.b = g->player.b;
            {
                float d = 0.0f;
                const vg_vec2 pp = project_cylinder_point(g, g->player.b.x, g->player.b.y, &d);
                gp.player.b.x = pp.x;
                gp.player.b.y = pp.y;
            }
            r = draw_player_ship(ctx, &gp, &ship_style, &thruster_fill);
            if (r != VG_OK) {
                return r;
            }
        }

        for (size_t i = 0; i < MAX_BULLETS; ++i) {
            if (!g->bullets[i].active) {
                continue;
            }
            float d0 = 0.0f;
            float d1 = 0.0f;
            const vg_vec2 a = project_cylinder_point(g, g->bullets[i].b.x - 7.0f, g->bullets[i].b.y, &d0);
            const vg_vec2 b = project_cylinder_point(g, g->bullets[i].b.x + 8.0f, g->bullets[i].b.y, &d1);
            vg_stroke_style bs = bullet_style;
            bs.width_px *= 0.45f + 0.9f * (d0 + d1) * 0.5f;
            const vg_vec2 bolt[] = {a, b};
            r = vg_draw_polyline(ctx, bolt, 2, &bs, 0);
            if (r != VG_OK) {
                return r;
            }
        }

        for (size_t i = 0; i < MAX_ENEMIES; ++i) {
            if (!g->enemies[i].active) {
                continue;
            }
            const enemy* e = &g->enemies[i];
            float d = 0.0f;
            const vg_vec2 c = project_cylinder_point(g, e->b.x, e->b.y, &d);
            const float rr = e->radius * (0.45f + d * 0.9f);
            const vg_vec2 body[] = {
                {c.x - rr, c.y},
                {c.x - rr * 0.2f, c.y - rr * 0.8f},
                {c.x + rr, c.y},
                {c.x - rr * 0.2f, c.y + rr * 0.8f},
                {c.x - rr, c.y}
            };
            vg_stroke_style es = enemy_style;
            es.width_px *= 0.55f + d * 0.8f;
            es.intensity *= 0.20f + d * 0.80f;
            es.color.a *= 0.20f + d * 0.80f;
            r = vg_draw_polyline(ctx, body, sizeof(body) / sizeof(body[0]), &es, 0);
            if (r != VG_OK) {
                return r;
            }
        }

        r = draw_top_meters(ctx, g, &txt_halo, &txt_main);
        if (r == VG_OK && metrics->show_crt_ui) {
            vg_crt_profile crt_ui;
            vg_get_crt_profile(ctx, &crt_ui);
            r = draw_crt_debug_ui(ctx, g->world_w, g->world_h, &crt_ui, metrics->crt_ui_selected);
        }
        if (r != VG_OK) {
            return r;
        }

        r = draw_teletype_overlay(ctx, g->world_w, g->world_h, metrics->teletype_text, &txt_halo, &txt_main);
        if (r != VG_OK) {
            return r;
        }
        if (g->lives <= 0) {
            const float go_size = 36.0f;
            const float go_spacing = 2.2f;
            const float go_w = vg_measure_text("GAME OVER", go_size, go_spacing);
            r = draw_text_vector_glow(
                ctx,
                "GAME OVER",
                (vg_vec2){(g->world_w - go_w) * 0.5f, g->world_h * 0.45f},
                go_size,
                go_spacing,
                &over_halo,
                &over_main
            );
            if (r != VG_OK) {
                return r;
            }
            r = draw_text_vector_glow(
                ctx,
                "PRESS R TO RESTART",
                (vg_vec2){
                    (g->world_w - vg_measure_text("PRESS R TO RESTART", 14.0f, 1.2f)) * 0.5f,
                    g->world_h * 0.52f
                },
                14.0f,
                1.2f,
                &txt_halo,
                &txt_main
            );
            if (r != VG_OK) {
                return r;
            }
        }
        return VG_OK;
    }

    for (size_t i = 0; i < MAX_STARS; ++i) {
        const float speed_u = (g->stars[i].speed - 50.0f) / 190.0f;
        float u = speed_u;
        if (u < 0.0f) {
            u = 0.0f;
        }
        if (u > 1.0f) {
            u = 1.0f;
        }
        const float parallax = 0.08f + u * 0.28f;
        const float persistence_trail = 1.0f + (1.0f - crt.persistence_decay) * 6.0f;
        float tx = g->stars[i].x + (g->stars[i].prev_x - g->stars[i].x) * (1.4f + 2.6f * u) * persistence_trail;
        const float ty = g->stars[i].y + (g->stars[i].prev_y - g->stars[i].y) * (1.4f + 2.6f * u);
        float sx = repeatf(g->stars[i].x - g->camera_x * parallax, g->world_w);
        float stx = repeatf(tx - g->camera_x * parallax, g->world_w);
        if (stx - sx > g->world_w * 0.5f) {
            stx -= g->world_w;
        } else if (sx - stx > g->world_w * 0.5f) {
            stx += g->world_w;
        }
        const vg_vec2 seg[] = {
            {stx, ty},
            {sx, g->stars[i].y}
        };
        vg_stroke_style sh = star_halo;
        vg_stroke_style sm = star_main;
        sh.width_px *= 0.85f + u * 0.9f;
        sm.width_px *= 0.75f + u * 0.7f;
        sh.intensity *= 0.7f + u * 0.7f;
        sm.intensity *= 0.8f + u * 0.6f;

        r = vg_draw_polyline(ctx, seg, 2, &sh, 0);
        if (r != VG_OK) {
            return r;
        }
        r = vg_draw_polyline(ctx, seg, 2, &sm, 0);
        if (r != VG_OK) {
            return r;
        }

        r = vg_fill_circle(ctx, (vg_vec2){sx, g->stars[i].y}, g->stars[i].size + 0.4f * u, &star_fill, 10);
        if (r != VG_OK) {
            return r;
        }
        /* Draw seam-duplicate heads near edges for continuous wrap. */
        if (sx < 8.0f) {
            r = vg_fill_circle(ctx, (vg_vec2){sx + g->world_w, g->stars[i].y}, g->stars[i].size + 0.4f * u, &star_fill, 10);
            if (r != VG_OK) {
                return r;
            }
        } else if (sx > g->world_w - 8.0f) {
            r = vg_fill_circle(ctx, (vg_vec2){sx - g->world_w, g->stars[i].y}, g->stars[i].size + 0.4f * u, &star_fill, 10);
            if (r != VG_OK) {
                return r;
            }
        }
    }

    r = vg_transform_push(ctx);
    if (r != VG_OK) {
        return r;
    }
    vg_transform_translate(ctx, g->world_w * 0.5f - g->camera_x, g->world_h * 0.5f - g->camera_y);

    for (size_t i = 0; i < MAX_PARTICLES; ++i) {
        const particle* p = &g->particles[i];
        if (!p->active) {
            continue;
        }
        vg_fill_style pf = make_fill(1.0f, (vg_color){p->r, p->g, p->bcol, p->a}, VG_BLEND_ADDITIVE);
        vg_stroke_style ps = make_stroke(1.0f, 1.0f, (vg_color){p->r, p->g, p->bcol, p->a}, VG_BLEND_ADDITIVE);
        if (p->type == PARTICLE_POINT) {
            r = vg_fill_circle(ctx, (vg_vec2){p->b.x, p->b.y}, p->size, &pf, 8);
            if (r != VG_OK) {
                (void)vg_transform_pop(ctx);
                return r;
            }
        } else {
            const float c = cosf(p->spin);
            const float s = sinf(p->spin);
            const float r0 = p->size * 1.25f;
            const vg_vec2 geom[] = {
                {p->b.x + c * r0, p->b.y + s * r0},
                {p->b.x - s * p->size, p->b.y + c * p->size},
                {p->b.x - c * r0, p->b.y - s * r0},
                {p->b.x + s * p->size, p->b.y - c * p->size}
            };
            r = vg_fill_convex(ctx, geom, 4, &pf);
            if (r != VG_OK) {
                (void)vg_transform_pop(ctx);
                return r;
            }
            r = vg_draw_polyline(ctx, geom, 4, &ps, 1);
            if (r != VG_OK) {
                (void)vg_transform_pop(ctx);
                return r;
            }
        }
    }

    if (g->lives > 0) {
        r = draw_player_ship(ctx, g, &ship_style, &thruster_fill);
        if (r != VG_OK) {
            (void)vg_transform_pop(ctx);
            return r;
        }
    }

    for (size_t i = 0; i < MAX_BULLETS; ++i) {
        if (!g->bullets[i].active) {
            continue;
        }
        const vg_vec2 bolt[] = {
            {g->bullets[i].b.x - 7.0f, g->bullets[i].b.y},
            {g->bullets[i].b.x + 8.0f, g->bullets[i].b.y}
        };
        r = vg_draw_polyline(ctx, bolt, 2, &bullet_style, 0);
        if (r != VG_OK) {
            (void)vg_transform_pop(ctx);
            return r;
        }
    }

    for (size_t i = 0; i < MAX_ENEMIES; ++i) {
        if (!g->enemies[i].active) {
            continue;
        }
        const enemy* e = &g->enemies[i];
        const float rr = e->radius;
        const vg_vec2 body[] = {
            {e->b.x - rr, e->b.y},
            {e->b.x - rr * 0.2f, e->b.y - rr * 0.8f},
            {e->b.x + rr, e->b.y},
            {e->b.x - rr * 0.2f, e->b.y + rr * 0.8f},
            {e->b.x - rr, e->b.y}
        };
        r = vg_draw_polyline(ctx, body, sizeof(body) / sizeof(body[0]), &enemy_style, 0);
        if (r != VG_OK) {
            (void)vg_transform_pop(ctx);
            return r;
        }
    }
    r = vg_transform_pop(ctx);
    if (r != VG_OK) {
        return r;
    }

    /* Foreground vector landscape layers for depth/parallax. */
    r = draw_parallax_landscape(ctx, g->world_w, g->world_h, g->camera_x, 1.20f, g->world_h * 0.18f, 22.0f, &land_halo, &land_main);
    if (r != VG_OK) {
        return r;
    }
    vg_stroke_style land2_halo = land_halo;
    vg_stroke_style land2_main = land_main;
    land2_halo.width_px *= 1.15f;
    land2_main.width_px *= 1.10f;
    land2_halo.intensity *= 1.05f;
    land2_main.intensity *= 1.08f;
    land2_main.color = (vg_color){pal.secondary.r, pal.secondary.g, pal.secondary.b, 0.9f};
    r = draw_parallax_landscape(ctx, g->world_w, g->world_h, g->camera_x, 1.55f, g->world_h * 0.10f, 30.0f, &land2_halo, &land2_main);
    if (r != VG_OK) {
        return r;
    }

    r = draw_top_meters(ctx, g, &txt_halo, &txt_main);
    if (r == VG_OK && metrics->show_crt_ui) {
        vg_crt_profile crt_ui;
        vg_get_crt_profile(ctx, &crt_ui);
        r = draw_crt_debug_ui(ctx, g->world_w, g->world_h, &crt_ui, metrics->crt_ui_selected);
    }
    if (r != VG_OK) {
        return r;
    }

    r = draw_teletype_overlay(ctx, g->world_w, g->world_h, metrics->teletype_text, &txt_halo, &txt_main);
    if (r != VG_OK) {
        return r;
    }

    if (g->lives <= 0) {
        const float go_size = 36.0f;
        const float go_spacing = 2.2f;
        const float go_w = vg_measure_text("GAME OVER", go_size, go_spacing);
        r = draw_text_vector_glow(
            ctx,
            "GAME OVER",
            (vg_vec2){(g->world_w - go_w) * 0.5f, g->world_h * 0.45f},
            go_size,
            go_spacing,
            &over_halo,
            &over_main
        );
        if (r != VG_OK) {
            return r;
        }
        r = draw_text_vector_glow(
            ctx,
            "PRESS R TO RESTART",
            (vg_vec2){
                (g->world_w - vg_measure_text("PRESS R TO RESTART", 14.0f, 1.2f)) * 0.5f,
                g->world_h * 0.52f
            },
            14.0f,
            1.2f,
            &txt_halo,
            &txt_main
        );
        if (r != VG_OK) {
            return r;
        }
    }

    return VG_OK;
}
