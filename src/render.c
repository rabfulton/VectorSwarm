#include "render.h"

#include "acoustics_ui_layout.h"
#include "planetarium/commander_nick_dialogues.h"
#include "vg_ui.h"
#include "vg_ui_ext.h"
#include "vg_pointer.h"
#include "vg_image.h"
#include "vg_svg.h"
#include "vg_text_fx.h"
#include "vg_text_layout.h"
#include "ui_layout.h"

#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

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
    vg_vec2 loop_rel_modern[WORMHOLE_ROWS][WORMHOLE_VN];
    vg_vec2 loop_rel_legacy[WORMHOLE_ROWS][WORMHOLE_VN];
    float loop_face_legacy[WORMHOLE_ROWS][WORMHOLE_VN];
    vg_vec2 rail_rel_modern[WORMHOLE_COLS][WORMHOLE_ROWS];
    vg_vec2 rail_rel_legacy[WORMHOLE_COLS][WORMHOLE_ROWS];
    float rail_face_legacy[WORMHOLE_COLS][WORMHOLE_ROWS];
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

static float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
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
        /* Keep bottom bright; only fade toward the top of the throat. */
        if (sy < 0.0f) {
            c->row_fade[j] = 1.0f;
        } else {
            c->row_fade[j] = 0.22f + powf(1.0f - sy, 1.35f) * 0.78f;
        }
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
        /* Keep perceived "front" on the inner side of both halves of the hourglass. */
        const float hemi = (sy < 0.0f) ? 1.0f : -1.0f;

        for (int i = 0; i < WORMHOLE_VN; ++i) {
            {
                const float ang = (float)i / (float)WORMHOLE_VN * 6.28318530718f;
                const float ca = cosf(ang);
                const float sa = sinf(ang);
                const float sa_hemi = sa * hemi;
                c->loop_rel_modern[j][i].x = ca * rx;
                c->loop_rel_modern[j][i].y = sy * h_span + sa_hemi * ry;
            }
            {
                const float ang = (float)i / (float)(WORMHOLE_VN - 1) * 6.28318530718f;
                const float ca = cosf(ang);
                const float sa = sinf(ang);
                c->loop_rel_legacy[j][i].x = ca * rx;
                c->loop_rel_legacy[j][i].y = sy * h_span + sa * ry;
                /* Surface of revolution normal: N ~ (cos(phi), -dr/dy, sin(phi)). */
                c->loop_face_legacy[j][i] = facing01_from_normal((v3){ca, -drdy, sa}, view_dir);
            }
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
            const float hemi = (sy < 0.0f) ? 1.0f : -1.0f;
            const float sp_hemi = sp * hemi;
            c->rail_rel_modern[col][j].x = cp * rx;
            c->rail_rel_modern[col][j].y = sy * h_span + sp_hemi * ry;
            c->rail_rel_legacy[col][j].x = cp * rx;
            c->rail_rel_legacy[col][j].y = sy * h_span + sp * ry;
            c->rail_face_legacy[col][j] =
                facing01_from_normal((v3){cp, -row_drdy[j], sp}, view_dir) * c->row_fade[j];
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

static int wrapi(int i, int n) {
    if (n <= 0) {
        return 0;
    }
    i %= n;
    if (i < 0) {
        i += n;
    }
    return i;
}

static float lerpf(float a, float b, float t) {
    return a + (b - a) * t;
}

static int level_uses_cylinder_render(int level_style) {
    return level_style == LEVEL_STYLE_ENEMY_RADAR ||
           level_style == LEVEL_STYLE_EVENT_HORIZON ||
           level_style == LEVEL_STYLE_EVENT_HORIZON_LEGACY;
}

static float perlin_fade(float t) {
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

static uint32_t hash_u32(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

static float hash01_2i(int ix, int iy) {
    const uint32_t hx = hash_u32((uint32_t)ix * 0x9e3779b9u);
    const uint32_t hy = hash_u32((uint32_t)iy * 0x85ebca6bu);
    const uint32_t h = hash_u32(hx ^ hy ^ 0x27d4eb2du);
    return (float)(h & 0x00ffffffu) / 16777215.0f;
}

static float perlin_grad_dot(int ix, int iy, float x, float y) {
    const float a = hash01_2i(ix, iy) * 6.28318530718f;
    const float gx = cosf(a);
    const float gy = sinf(a);
    return gx * (x - (float)ix) + gy * (y - (float)iy);
}

static float perlin2(float x, float y) {
    const int x0 = (int)floorf(x);
    const int y0 = (int)floorf(y);
    const int x1 = x0 + 1;
    const int y1 = y0 + 1;
    const float sx = perlin_fade(x - (float)x0);
    const float sy = perlin_fade(y - (float)y0);
    const float n00 = perlin_grad_dot(x0, y0, x, y);
    const float n10 = perlin_grad_dot(x1, y0, x, y);
    const float n01 = perlin_grad_dot(x0, y1, x, y);
    const float n11 = perlin_grad_dot(x1, y1, x, y);
    const float ix0 = lerpf(n00, n10, sx);
    const float ix1 = lerpf(n01, n11, sx);
    return lerpf(ix0, ix1, sy);
}

static float high_plains_looped_noise(float world_x, float z) {
    const float period_world = 8192.0f;
    const float u = repeatf(world_x, period_world) / period_world;
    const float a = u * 6.28318530718f;
    const float nx = cosf(a) * 2.3f + z * 0.85f + 19.7f;
    const float ny = sinf(a) * 2.3f - z * 0.55f + 7.3f;
    const float n0 = perlin2(nx, ny);
    const float n1 = perlin2(nx * 1.9f + 13.2f, ny * 1.9f - 4.6f);
    return n0 * 0.78f + n1 * 0.22f;
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
            p.primary = (vg_color){0.12f, 0.92f, 0.27f, 0.95f};
            p.primary_dim = (vg_color){0.05f, 0.74f, 0.18f, 0.40f};
            p.secondary = (vg_color){0.22f, 0.92f, 0.38f, 1.0f};
            p.haze = (vg_color){0.01f, 0.06f, 0.025f, 0.55f};
            p.star = (vg_color){0.19f, 0.86f, 0.30f, 1.0f};
            p.ship = (vg_color){0.14f, 0.92f, 0.25f, 1.0f};
            p.thruster = (vg_color){0.28f, 0.92f, 0.45f, 0.92f};
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
    if (!text || text[0] == '\0') {
        return VG_OK;
    }
    const float ui = ui_reference_scale(w, h);
    const vg_rect safe = make_ui_safe_frame(w, h);

    char line[256];
    size_t li = 0;
    int row = 0;
    const float x0 = safe.x + safe.w * 0.025f;
    const float y0 = safe.y + safe.h - 34.0f * ui;
    const float lh = 17.0f * ui;

    for (size_t i = 0;; ++i) {
        const char c = text[i];
        if (c == '\n' || c == '\0') {
            line[li] = '\0';
            vg_result r = draw_text_vector_glow(
                ctx,
                line,
                (vg_vec2){x0, y0 - lh * (float)row},
                12.5f * ui,
                0.8f * ui,
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
    const float ui = ui_reference_scale(g->world_w, g->world_h);
    const vg_rect safe = make_ui_safe_frame(g->world_w, g->world_h);

    vg_ui_meter_style ms;
    ms.frame = *main_s;
    ms.frame.blend = VG_BLEND_ALPHA;
    ms.frame.intensity = main_s->intensity * 1.10f;
    ms.frame.width_px = fmaxf(main_s->width_px + 0.6f * ui, 1.5f * ui);
    ms.bg = *halo_s;
    ms.bg.blend = VG_BLEND_ALPHA;
    ms.bg.intensity = halo_s->intensity * 0.45f;
    ms.fill = *main_s;
    ms.fill.blend = VG_BLEND_ADDITIVE;
    ms.fill.intensity = main_s->intensity * 1.15f;
    ms.tick = *main_s;
    ms.tick.blend = VG_BLEND_ALPHA;
    ms.tick.width_px = fmaxf(main_s->width_px * 0.85f, 0.8f * ui);
    ms.tick.intensity = 0.9f;
    ms.text = ms.tick;
    ms.text.width_px = fmaxf(main_s->width_px * 1.05f, 1.0f * ui);

    vg_ui_meter_desc d;
    d.min_value = 0.0f;
    d.max_value = 100.0f;
    d.mode = VG_UI_METER_SEGMENTED;
    d.segments = 18;
    d.segment_gap_px = 2.0f * ui;
    d.value_fmt = "%5.1f";
    d.show_value = 1;
    d.show_ticks = 1;
    d.ui_scale = ui;
    d.text_scale = ui;

    const float w = safe.w;
    const float h = safe.h;
    const float margin_x = w * 0.04f;
    const float top_margin = 46.0f * ui;
    const float total_w = w * 0.40f;
    const float meter_gap = w * 0.02f;
    const float meter_w = (total_w - meter_gap) * 0.5f;
    const float meter_h = 16.0f * ui;
    const float y_top = safe.y + h - top_margin - meter_h;
    const float x_block = safe.x + w - margin_x - total_w;

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
    d.segment_gap_px = 2.0f * ui;
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

static vg_ui_slider_panel_metrics scaled_slider_metrics(float ui, float value_col_width_px) {
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

static vg_result draw_beam_trace(
    vg_context* ctx,
    const vg_vec2* points,
    size_t point_count,
    const vg_stroke_style* base,
    vg_color color,
    float core_width_px,
    float intensity
) {
    if (!ctx || !points || point_count < 2u || !base) {
        return VG_ERROR_INVALID_ARGUMENT;
    }
    vg_stroke_style halo = *base;
    halo.color = color;
    halo.width_px = fmaxf(1.0f, core_width_px * 2.6f);
    halo.intensity = intensity * 0.30f;
    halo.blend = VG_BLEND_ADDITIVE;
    vg_result r = vg_draw_polyline(ctx, points, point_count, &halo, 0);
    if (r != VG_OK) {
        return r;
    }

    vg_stroke_style mid = *base;
    mid.color = color;
    mid.width_px = fmaxf(1.0f, core_width_px * 1.6f);
    mid.intensity = intensity * 0.55f;
    mid.blend = VG_BLEND_ADDITIVE;
    r = vg_draw_polyline(ctx, points, point_count, &mid, 0);
    if (r != VG_OK) {
        return r;
    }

    vg_stroke_style core = *base;
    core.color = color;
    core.width_px = fmaxf(1.0f, core_width_px);
    core.intensity = intensity;
    core.blend = VG_BLEND_ALPHA;
    return vg_draw_polyline(ctx, points, point_count, &core, 0);
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
        norm_range(crt->bloom_strength, CRT_RANGE_BLOOM_STRENGTH_MIN, CRT_RANGE_BLOOM_STRENGTH_MAX),
        norm_range(crt->bloom_radius_px, CRT_RANGE_BLOOM_RADIUS_MIN, CRT_RANGE_BLOOM_RADIUS_MAX),
        norm_range(crt->persistence_decay, CRT_RANGE_PERSISTENCE_MIN, CRT_RANGE_PERSISTENCE_MAX),
        norm_range(crt->jitter_amount, CRT_RANGE_JITTER_MIN, CRT_RANGE_JITTER_MAX),
        norm_range(crt->flicker_amount, CRT_RANGE_FLICKER_MIN, CRT_RANGE_FLICKER_MAX),
        norm_range(crt->beam_core_width_px, CRT_RANGE_BEAM_CORE_MIN, CRT_RANGE_BEAM_CORE_MAX),
        norm_range(crt->beam_halo_width_px, CRT_RANGE_BEAM_HALO_MIN, CRT_RANGE_BEAM_HALO_MAX),
        norm_range(crt->beam_intensity, CRT_RANGE_BEAM_INTENSITY_MIN, CRT_RANGE_BEAM_INTENSITY_MAX),
        norm_range(crt->vignette_strength, CRT_RANGE_VIGNETTE_MIN, CRT_RANGE_VIGNETTE_MAX),
        norm_range(crt->barrel_distortion, CRT_RANGE_BARREL_MIN, CRT_RANGE_BARREL_MAX),
        norm_range(crt->scanline_strength, CRT_RANGE_SCANLINE_MIN, CRT_RANGE_SCANLINE_MAX),
        norm_range(crt->noise_strength, CRT_RANGE_NOISE_MIN, CRT_RANGE_NOISE_MAX)
    };

    const float ui_scale = ui_reference_scale(w, h);
    const vg_rect safe = make_ui_safe_frame(w, h);

    vg_stroke_style panel = {
        .width_px = 1.4f * ui_scale,
        .intensity = 0.9f,
        .color = {0.15f, 1.0f, 0.38f, 0.9f},
        .cap = VG_LINE_CAP_ROUND,
        .join = VG_LINE_JOIN_ROUND,
        .miter_limit = 4.0f,
        .blend = VG_BLEND_ALPHA
    };
    vg_stroke_style text = panel;
    text.width_px = 1.15f * ui_scale;
    text.intensity = 1.0f;
    text.color = (vg_color){0.45f, 1.0f, 0.62f, 1.0f};

    vg_ui_slider_item items[12];
    for (int i = 0; i < 12; ++i) {
        items[i].label = labels[i];
        items[i].value_01 = value_01[i];
        items[i].value_display = value_display[i];
        items[i].selected = (i == selected);
    }

    vg_ui_slider_panel_metrics m = scaled_slider_metrics(ui_scale, 70.0f * ui_scale);
    vg_ui_slider_panel_desc ui = {
        .rect = {safe.x + safe.w * 0.00f, safe.y + safe.h * 0.08f, safe.w * 0.44f, safe.h * 0.82f},
        .title_line_0 = "CRT DEBUG",
        .title_line_1 = "TAB TOGGLE  ARROWS ADJUST",
        .footer_line = NULL,
        .items = items,
        .item_count = 12u,
        .row_height_px = 34.0f * ui_scale,
        .label_size_px = 11.0f * ui_scale,
        .value_size_px = 11.5f * ui_scale,
        .value_text_x_offset_px = 0.0f,
        .border_style = panel,
        .text_style = text,
        .track_style = text,
        .knob_style = text,
        .metrics = &m
    };
    return vg_ui_draw_slider_panel(ctx, &ui);
}

static vg_result draw_acoustics_ui(vg_context* ctx, float w, float h, const render_metrics* metrics) {
    static const char* synth_fire_labels[8] = {
        "WAVEFORM", "PITCH HZ", "ATTACK MS", "DECAY MS", "CUTOFF KHZ", "RESONANCE", "SWEEP ST", "SWEEP DECAY"
    };
    static const char* synth_thr_labels[6] = {
        "LEVEL", "PITCH HZ", "ATTACK MS", "RELEASE MS", "CUTOFF KHZ", "RESONANCE"
    };
    static const char* combat_enemy_labels[6] = {
        "LEVEL", "PITCH HZ", "ATTACK MS", "DECAY MS", "NOISE MIX", "PAN WIDTH"
    };
    static const char* combat_exp_labels[8] = {
        "LEVEL", "PITCH HZ", "ATTACK MS", "DECAY MS", "NOISE MIX", "FM DEPTH", "FM RATE", "PAN WIDTH"
    };
    const int combat_page = (metrics->acoustics_page != 0);
    const palette_theme pal = get_palette_theme(metrics->palette_mode);

    const float ui = ui_reference_scale(w, h);
    vg_stroke_style panel = {
        .width_px = 1.45f * ui,
        .intensity = 0.95f,
        .color = (vg_color){pal.primary.r, pal.primary.g, pal.primary.b, 0.95f},
        .cap = VG_LINE_CAP_ROUND,
        .join = VG_LINE_JOIN_ROUND,
        .miter_limit = 4.0f,
        .blend = VG_BLEND_ALPHA
    };
    vg_stroke_style text = panel;
    text.width_px = 1.35f * ui;
    text.intensity = 1.12f;
    text.color = (vg_color){pal.secondary.r, pal.secondary.g, pal.secondary.b, 1.0f};
    vg_fill_style trace_panel_fill = {
        .intensity = 0.75f,
        .color = (vg_color){pal.haze.r, pal.haze.g, pal.haze.b, 0.35f},
        .blend = VG_BLEND_ALPHA
    };

    vg_ui_slider_item fire_items[8];
    vg_ui_slider_item thr_items[8];
    if (combat_page) {
        for (int i = 0; i < 6; ++i) {
            fire_items[i].label = combat_enemy_labels[i];
            fire_items[i].value_01 = metrics->acoustics_combat_value_01[i];
            fire_items[i].value_display = metrics->acoustics_combat_display[i];
            fire_items[i].selected = (metrics->acoustics_combat_selected == i) ? 1 : 0;
        }
        for (int i = 0; i < 8; ++i) {
            thr_items[i].label = combat_exp_labels[i];
            thr_items[i].value_01 = metrics->acoustics_combat_value_01[6 + i];
            thr_items[i].value_display = metrics->acoustics_combat_display[6 + i];
            thr_items[i].selected = (metrics->acoustics_combat_selected == (6 + i)) ? 1 : 0;
        }
    } else {
        for (int i = 0; i < 8; ++i) {
            fire_items[i].label = synth_fire_labels[i];
            fire_items[i].value_01 = metrics->acoustics_value_01[i];
            fire_items[i].value_display = metrics->acoustics_display[i];
            fire_items[i].selected = (metrics->acoustics_selected == i) ? 1 : 0;
        }
        for (int i = 0; i < 6; ++i) {
            thr_items[i].label = synth_thr_labels[i];
            thr_items[i].value_01 = metrics->acoustics_value_01[8 + i];
            thr_items[i].value_display = metrics->acoustics_display[8 + i];
            thr_items[i].selected = (metrics->acoustics_selected == (8 + i)) ? 1 : 0;
        }
    }

    const float value_col_width_px = acoustics_compute_value_col_width(
        ui,
        11.5f * ui,
        combat_page ? metrics->acoustics_combat_display : metrics->acoustics_display,
        combat_page ? ACOUSTICS_COMBAT_SLIDER_COUNT : ACOUSTICS_SLIDER_COUNT
    );
    const acoustics_ui_layout l = make_acoustics_ui_layout(w, h, value_col_width_px, combat_page ? 6 : 8, combat_page ? 8 : 6);
    const vg_rect page_btn = acoustics_page_toggle_button_rect(w, h);
    const vg_rect fire_rect = l.panel[0];
    const vg_rect thr_rect = l.panel[1];
    const vg_rect fire_btn = l.button[0];
    const vg_rect thr_btn = l.button[1];
    const vg_rect fire_save_btn = l.save_button[0];
    const vg_rect thr_save_btn = l.save_button[1];
    const vg_ui_slider_panel_metrics sm = acoustics_scaled_slider_metrics(ui, l.value_col_width_px);

    vg_ui_slider_panel_desc fire = {
        .rect = fire_rect,
        .title_line_0 = combat_page ? "SHIPYARD ACOUSTICS - ENEMY FIRE" : "SHIPYARD ACOUSTICS - FIRE",
        .title_line_1 = combat_page ? "Q/E SWITCH PAGE  ARROWS OR MOUSE TO TUNE" : "Q/E SWITCH PAGE  ARROWS OR MOUSE TO TUNE",
        .footer_line = NULL,
        .items = fire_items,
        .item_count = combat_page ? 6u : 8u,
        .row_height_px = 34.0f * ui,
        .label_size_px = 11.0f * ui,
        .value_size_px = 11.5f * ui,
        .value_text_x_offset_px = 0.0f,
        .border_style = panel,
        .text_style = text,
        .track_style = text,
        .knob_style = text,
        .metrics = &sm
    };
    vg_ui_slider_panel_desc thr = fire;
    thr.rect = thr_rect;
    thr.title_line_0 = combat_page ? "SHIPYARD ACOUSTICS - EXPLOSION" : "SHIPYARD ACOUSTICS - THRUST";
    thr.items = thr_items;
    thr.item_count = combat_page ? 8u : 6u;

    vg_ui_slider_panel_layout fire_layout;
    vg_ui_slider_panel_layout thr_layout;
    if (vg_ui_slider_panel_compute_layout(&fire, &fire_layout) != VG_OK ||
        vg_ui_slider_panel_compute_layout(&thr, &thr_layout) != VG_OK) {
        return VG_ERROR_INVALID_ARGUMENT;
    }
    const float fire_rows_top = fire_layout.row_start_y + fire.row_height_px * (float)fire.item_count;
    const float thr_rows_top = thr_layout.row_start_y + thr.row_height_px * (float)thr.item_count;
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

    vg_result r = vg_ui_draw_slider_panel(ctx, &fire);
    if (r != VG_OK) {
        return r;
    }

    r = vg_ui_draw_slider_panel(ctx, &thr);
    if (r != VG_OK) {
        return r;
    }

    {
        vg_stroke_style header_active = text;
        header_active.intensity *= 1.18f;
        const char* page_label = combat_page ? "COMBAT" : "SHIP";
        const float page_size = 18.0f * ui;
        const float y = fmaxf(fire_rect.y + fire_rect.h + 14.0f * ui, page_btn.y + page_btn.h * 0.5f + 2.0f * ui);
        r = draw_text_vector_glow(
            ctx,
            page_label,
            (vg_vec2){fire_rect.x, y},
            page_size,
            0.78f * ui,
            &panel,
            &header_active
        );
        if (r != VG_OK) {
            return r;
        }

        r = vg_draw_button(
            ctx,
            page_btn,
            combat_page ? "GO SHIP PAGE" : "GO COMBAT PAGE",
            10.8f * ui,
            &panel,
            &text,
            0
        );
        if (r != VG_OK) {
            return r;
        }
    }

    {
        r = vg_draw_button(ctx, fire_btn, combat_page ? "TEST ENEMY" : "TEST FIRE", 11.5f * ui, &panel, &text, 0);
        if (r != VG_OK) {
            return r;
        }
        r = vg_draw_button(ctx, thr_btn, combat_page ? "TEST BOOM" : "TEST THRUST", 11.5f * ui, &panel, &text, 0);
        if (r != VG_OK) {
            return r;
        }
        r = vg_draw_button(ctx, fire_save_btn, "SAVE", 11.0f * ui, &panel, &text, 0);
        if (r != VG_OK) {
            return r;
        }
        r = vg_draw_button(ctx, thr_save_btn, "SAVE", 11.0f * ui, &panel, &text, 0);
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
                l.slot_button[0][i],
                label,
                11.0f * ui,
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
                l.slot_button[1][i],
                label,
                11.0f * ui,
                &panel,
                &slot_text,
                (metrics->acoustics_thr_slot_selected == i) ? 1 : 0
            );
            if (r != VG_OK) {
                return r;
            }
        }

        r = vg_fill_rect(ctx, fire_display, &trace_panel_fill);
        if (r != VG_OK) {
            return r;
        }
        r = vg_draw_rect(ctx, fire_display, &panel);
        if (r != VG_OK) {
            return r;
        }
        r = draw_text_vector_glow(
            ctx,
            combat_page ? "ENEMY SHOT PREVIEW" : "ENV + PITCH SWEEP",
            (vg_vec2){fire_display.x + 8.0f * ui, fire_display.y + fire_display.h - 16.0f * ui},
            10.5f * ui,
            0.7f * ui,
            &panel,
            &text
        );
        if (r != VG_OK) {
            return r;
        }

        enum { FIRE_TRACE_SAMPLES = 96 };
        vg_vec2 amp_line[FIRE_TRACE_SAMPLES];
        vg_vec2 pitch_line[FIRE_TRACE_SAMPLES];
        const float a_ms = metrics->acoustics_display[2];
        const float d_ms = metrics->acoustics_display[3];
        const float sweep_st = metrics->acoustics_display[6];
        const float sweep_d_ms = metrics->acoustics_display[7];
        for (int i = 0; i < FIRE_TRACE_SAMPLES; ++i) {
            const float t = (float)i / (float)(FIRE_TRACE_SAMPLES - 1);
            const float x = fire_display.x + 8.0f * ui + (fire_display.w - 16.0f * ui) * t;
            float amp;
            if (t < a_ms / 280.0f) {
                amp = t / (a_ms / 280.0f + 1e-4f);
            } else {
                float td = (t - a_ms / 280.0f) / (d_ms / 280.0f + 1e-4f);
                amp = 1.0f - td;
                if (amp < 0.0f) amp = 0.0f;
            }
            float p = 0.5f + (sweep_st / 24.0f) * expf(-t * (280.0f / (sweep_d_ms + 1.0f))) * 0.35f;
            amp_line[i] = (vg_vec2){x, fire_display.y + 8.0f * ui + amp * (fire_display.h - 20.0f * ui)};
            pitch_line[i] = (vg_vec2){x, fire_display.y + 8.0f * ui + p * (fire_display.h - 20.0f * ui)};
        }
        r = draw_beam_trace(
            ctx,
            amp_line,
            FIRE_TRACE_SAMPLES,
            &text,
            (vg_color){0.35f, 1.0f, 0.65f, 1.0f},
            1.45f * ui,
            1.05f
        );
        if (r != VG_OK) {
            return r;
        }
        r = draw_beam_trace(
            ctx,
            pitch_line,
            FIRE_TRACE_SAMPLES,
            &text,
            (vg_color){0.95f, 1.0f, 0.30f, 1.0f},
            1.5f * ui,
            1.08f
        );
        if (r != VG_OK) {
            return r;
        }

        r = vg_fill_rect(ctx, thr_display, &trace_panel_fill);
        if (r != VG_OK) {
            return r;
        }
        r = vg_draw_rect(ctx, thr_display, &panel);
        if (r != VG_OK) {
            return r;
        }
        r = draw_text_vector_glow(
            ctx,
            combat_page ? "EXPLOSION PREVIEW" : "OSCILLOSCOPE",
            (vg_vec2){thr_display.x + 8.0f * ui, thr_display.y + thr_display.h - 16.0f * ui},
            10.5f * ui,
            0.7f * ui,
            &panel,
            &text
        );
        if (r != VG_OK) {
            return r;
        }

        vg_stroke_style axis_s = panel;
        axis_s.width_px = 1.0f * ui;
        axis_s.intensity = 0.65f;
        axis_s.color = (vg_color){0.28f, 0.96f, 0.58f, 0.72f};
        const vg_vec2 h_axis[2] = {
            {thr_display.x + 8.0f * ui, thr_display.y + thr_display.h * 0.5f},
            {thr_display.x + thr_display.w - 8.0f * ui, thr_display.y + thr_display.h * 0.5f}
        };
        r = vg_draw_polyline(ctx, h_axis, 2, &axis_s, 0);
        if (r != VG_OK) {
            return r;
        }

        static float scope_hold[ACOUSTICS_SCOPE_SAMPLES] = {0.0f};
        static float scope_smooth[ACOUSTICS_SCOPE_SAMPLES] = {0.0f};
        static int scope_init = 0;
        if (!scope_init) {
            for (int i = 0; i < ACOUSTICS_SCOPE_SAMPLES; ++i) {
                const float s0 = metrics->acoustics_scope[i];
                scope_hold[i] = s0;
                scope_smooth[i] = s0;
            }
            scope_init = 1;
        }
        const float dt = clampf(metrics->dt, 0.001f, 0.10f);
        const float hold_decay = expf(-dt / 0.30f);
        const float smooth_alpha = 1.0f - expf(-dt / 0.040f);

        vg_vec2 scope_line[ACOUSTICS_SCOPE_SAMPLES];
        vg_vec2 scope_hold_line[ACOUSTICS_SCOPE_SAMPLES];
        for (int i = 0; i < ACOUSTICS_SCOPE_SAMPLES; ++i) {
            const float t = (float)i / (float)(ACOUSTICS_SCOPE_SAMPLES - 1);
            const float x = thr_display.x + 8.0f * ui + (thr_display.w - 16.0f * ui) * t;
            const float s = clampf(metrics->acoustics_scope[i], -1.0f, 1.0f);
            scope_smooth[i] += (s - scope_smooth[i]) * smooth_alpha;
            if (fabsf(scope_smooth[i]) > fabsf(scope_hold[i])) {
                scope_hold[i] = scope_smooth[i];
            } else {
                scope_hold[i] *= hold_decay;
            }
            const float y_core = thr_display.y + thr_display.h * 0.5f + scope_smooth[i] * (thr_display.h * 0.35f);
            const float y_hold = thr_display.y + thr_display.h * 0.5f + scope_hold[i] * (thr_display.h * 0.35f);
            scope_line[i] = (vg_vec2){x, y_core};
            scope_hold_line[i] = (vg_vec2){x, y_hold};
        }
        r = draw_beam_trace(
            ctx,
            scope_hold_line,
            ACOUSTICS_SCOPE_SAMPLES,
            &text,
            (vg_color){0.35f, 0.80f, 1.0f, 0.95f},
            1.9f * ui,
            0.62f
        );
        if (r != VG_OK) {
            return r;
        }
        r = draw_beam_trace(
            ctx,
            scope_line,
            ACOUSTICS_SCOPE_SAMPLES,
            &text,
            (vg_color){0.55f, 1.0f, 1.0f, 1.0f},
            1.4f * ui,
            1.10f
        );
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
    const float ui = ui_reference_scale(w, h);
    const vg_rect safe = make_ui_safe_frame(w, h);
    const palette_theme pal = get_palette_theme(metrics->palette_mode);
    vg_stroke_style frame = {
        .width_px = 2.2f * ui,
        .intensity = 1.0f,
        .color = pal.primary,
        .cap = VG_LINE_CAP_ROUND,
        .join = VG_LINE_JOIN_ROUND,
        .miter_limit = 4.0f,
        .blend = VG_BLEND_ALPHA
    };
    vg_stroke_style txt = frame;
    txt.width_px = 1.2f * ui;
    txt.color = pal.secondary;
    vg_fill_style haze = {
        .intensity = 0.28f,
        .color = pal.haze,
        .blend = VG_BLEND_ALPHA
    };

    const vg_rect panel = safe;
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
        18.0f * ui,
        1.4f * ui,
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
        10.0f * ui,
        0.8f * ui,
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
            r = vg_draw_button(ctx, b, labels[i], 11.0f * ui, &frame, &txt, (metrics->palette_mode == i) ? 1 : 0);
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
        r = vg_draw_button(ctx, row, label, 12.0f * ui, &frame, &txt, (metrics->video_menu_selected == i) ? 1 : 0);
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
            11.0f * ui,
            0.8f * ui,
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
                "BLOOM", "BLOOM RAD", "PERSIST", "JITTER",
                "FLICKER", "BEAM CORE", "BEAM HALO", "BEAM",
                "SCANLINE", "NOISE", "VIGNETTE", "BARREL"
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
            d.segment_gap_px = 2.0f * ui;
            d.value_fmt = "%3.0f";
            d.show_value = 0;
            d.show_ticks = 1;
            d.ui_scale = ui;
            d.text_scale = ui;
            const float radius = lab.w * 0.052f;
            for (int i = 0; i < VIDEO_MENU_DIAL_COUNT; ++i) {
                const int row = i / 4;
                const int col = i % 4;
                const vg_vec2 c = {
                    lab.x + lab.w * (0.12f + 0.25f * (float)col),
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

static void planetarium_node_center(float w, float h, int system_count, int idx, float t_s, float* out_x, float* out_y) {
    static const int k_primes[PLANETARIUM_MAX_SYSTEMS] = {2, 3, 5, 7, 11, 13, 17, 19};
    const vg_rect panel = make_ui_safe_frame(w, h);
    const vg_rect map = {panel.x + panel.w * 0.03f, panel.y + panel.h * 0.08f, panel.w * 0.56f, panel.h * 0.85f};
    const float cx = map.x + map.w * 0.50f;
    const float cy = map.y + map.h * 0.52f;
    if (idx < system_count) {
        const float orbit_t = ((float)idx + 1.0f) / ((float)system_count + 1.0f);
        const float rx = map.w * (0.12f + orbit_t * 0.30f);
        const float ry = map.h * (0.04f + orbit_t * 0.11f);
        const float rot = 0.22f;
        const int p = k_primes[idx % PLANETARIUM_MAX_SYSTEMS];
        const int q = k_primes[(idx + 3) % PLANETARIUM_MAX_SYSTEMS];
        const float phase = t_s * (0.10f + 0.008f * (float)p) + 6.28318530718f * ((float)(q % 29) / 29.0f);
        const float c = cosf(phase);
        const float s = sinf(phase);
        *out_x = cx + c * rx * cosf(rot) - s * ry * sinf(rot);
        *out_y = cy + c * rx * sinf(rot) + s * ry * cosf(rot);
    } else {
        *out_x = cx + map.w * 0.38f;
        *out_y = cy - map.h * 0.08f;
    }
}

static const planet_def* metrics_planet(const render_metrics* metrics, int idx) {
    if (!metrics || !metrics->planetarium_system || !metrics->planetarium_system->planets) {
        return NULL;
    }
    if (idx < 0 || idx >= metrics->planetarium_system->planet_count) {
        return NULL;
    }
    return &metrics->planetarium_system->planets[idx];
}

static const char* fallback_planet_label(int idx) {
    static char label[32];
    snprintf(label, sizeof(label), "SYSTEM %02d", idx + 1);
    return label;
}

static int append_char_dyn(char** buf, size_t* len, size_t* cap, char c) {
    if (!buf || !len || !cap) {
        return 0;
    }
    if (*len + 1u >= *cap) {
        size_t next = (*cap == 0u) ? 256u : (*cap * 2u);
        char* grown = (char*)realloc(*buf, next);
        if (!grown) {
            return 0;
        }
        *buf = grown;
        *cap = next;
    }
    (*buf)[(*len)++] = c;
    (*buf)[*len] = '\0';
    return 1;
}

static int append_cstr_dyn(char** buf, size_t* len, size_t* cap, const char* s) {
    if (!s) {
        return 1;
    }
    for (const char* p = s; *p; ++p) {
        if (!append_char_dyn(buf, len, cap, *p)) {
            return 0;
        }
    }
    return 1;
}

static char* wrap_text_wordwise(const char* text, float size_px, float letter_spacing_px, float width_px) {
    if (!text) {
        return NULL;
    }
    char* out = NULL;
    size_t out_len = 0u;
    size_t out_cap = 0u;
    float line_w = 0.0f;
    int at_line_start = 1;
    const float space_w = vg_measure_text(" ", size_px, letter_spacing_px);

    const char* p = text;
    while (*p) {
        if (*p == '\n') {
            if (!append_char_dyn(&out, &out_len, &out_cap, '\n')) {
                free(out);
                return NULL;
            }
            line_w = 0.0f;
            at_line_start = 1;
            p++;
            while (*p == ' ' || *p == '\t') {
                p++;
            }
            continue;
        }
        while (*p == ' ' || *p == '\t' || *p == '\r') {
            p++;
        }
        if (!*p) {
            break;
        }

        char word[512];
        size_t wi = 0u;
        while (*p && *p != '\n' && *p != ' ' && *p != '\t' && *p != '\r') {
            if (wi + 1u < sizeof(word)) {
                word[wi++] = *p;
            }
            p++;
        }
        word[wi] = '\0';
        if (wi == 0u) {
            continue;
        }

        const float word_w = vg_measure_text(word, size_px, letter_spacing_px);
        const float needed = at_line_start ? word_w : (line_w + space_w + word_w);
        if (!at_line_start && needed > width_px) {
            if (!append_char_dyn(&out, &out_len, &out_cap, '\n')) {
                free(out);
                return NULL;
            }
            line_w = 0.0f;
            at_line_start = 1;
        }

        if (!at_line_start) {
            if (!append_char_dyn(&out, &out_len, &out_cap, ' ')) {
                free(out);
                return NULL;
            }
            line_w += space_w;
        }
        if (!append_cstr_dyn(&out, &out_len, &out_cap, word)) {
            free(out);
            return NULL;
        }
        line_w += word_w;
        at_line_start = 0;
    }

    if (!out) {
        out = (char*)malloc(1u);
        if (!out) {
            return NULL;
        }
        out[0] = '\0';
    }
    return out;
}

static vg_result draw_wrapped_text_block_down(
    vg_context* ctx,
    const char* text,
    float x,
    float top_y,
    float bottom_y,
    float width,
    float size_px,
    float letter_spacing_px,
    const vg_stroke_style* frame_style,
    const vg_stroke_style* text_style,
    float* out_height_px
) {
    if (!ctx || !text || !frame_style || !text_style || width <= 0.0f || top_y <= bottom_y) {
        return VG_ERROR_INVALID_ARGUMENT;
    }
    const float line_h = size_px * 1.60f;
    const float avail_h = top_y - bottom_y;
    if (avail_h < line_h) {
        if (out_height_px) {
            *out_height_px = 0.0f;
        }
        return VG_OK;
    }

    char* normalized = wrap_text_wordwise(text, size_px, letter_spacing_px, width);
    if (!normalized) {
        return VG_ERROR_OUT_OF_MEMORY;
    }
    size_t measured_lines = 0u;
    (void)vg_measure_text_wrapped(normalized, size_px, letter_spacing_px, width, &measured_lines);

    vg_text_layout layout = {0};
    vg_text_layout_params params = {
        .bounds = (vg_rect){0.0f, 0.0f, width, avail_h},
        .size_px = size_px,
        .letter_spacing_px = letter_spacing_px,
        .line_height_px = line_h,
        .align = VG_TEXT_ALIGN_LEFT
    };
    vg_result r = vg_text_layout_build(normalized, &params, &layout);
    if (r != VG_OK) {
        free(normalized);
        vg_text_layout_reset(&layout);
        return r;
    }

    int max_lines = (int)floorf(avail_h / line_h);
    if (max_lines < 0) {
        max_lines = 0;
    }
    int draw_lines = (int)layout.line_count;
    if (draw_lines > max_lines) {
        draw_lines = max_lines;
    }
    if ((size_t)draw_lines > measured_lines && measured_lines > 0u) {
        draw_lines = (int)measured_lines;
    }

    for (int i = 0; i < draw_lines; ++i) {
        const vg_text_layout_line* ln = &layout.lines[i];
        char line_buf[1024];
        size_t n = ln->text_length;
        if (n >= sizeof(line_buf)) {
            n = sizeof(line_buf) - 1u;
        }
        memcpy(line_buf, layout.text + ln->text_offset, n);
        line_buf[n] = '\0';
        r = draw_text_vector_glow(
            ctx,
            line_buf,
            (vg_vec2){x, top_y - (float)i * line_h},
            size_px,
            letter_spacing_px,
            frame_style,
            text_style
        );
        if (r != VG_OK) {
            free(normalized);
            vg_text_layout_reset(&layout);
            return r;
        }
    }

    if (out_height_px) {
        *out_height_px = (float)draw_lines * line_h;
    }
    free(normalized);
    vg_text_layout_reset(&layout);
    return VG_OK;
}

static vg_result draw_planetarium_ui(vg_context* ctx, float w, float h, const render_metrics* metrics, float t_s) {
    const float ui = ui_reference_scale(w, h);
    const palette_theme pal = get_palette_theme(metrics->palette_mode);
    vg_stroke_style frame = {
        .width_px = 2.0f * ui,
        .intensity = 1.0f,
        .color = pal.primary,
        .cap = VG_LINE_CAP_ROUND,
        .join = VG_LINE_JOIN_ROUND,
        .miter_limit = 4.0f,
        .blend = VG_BLEND_ALPHA
    };
    vg_stroke_style txt = frame;
    txt.width_px = 1.2f * ui;
    txt.color = pal.secondary;
    vg_fill_style haze = {.intensity = 0.30f, .color = pal.haze, .blend = VG_BLEND_ALPHA};

    const vg_rect panel = make_ui_safe_frame(w, h);
    const vg_rect map = {panel.x + panel.w * 0.03f, panel.y + panel.h * 0.08f, panel.w * 0.56f, panel.h * 0.85f};
    const vg_rect side = {panel.x + panel.w * 0.62f, panel.y + panel.h * 0.08f, panel.w * 0.35f, panel.h * 0.85f};
    const vg_rect nick_rect = {side.x + side.w * 0.05f, side.y + side.h * 0.56f, side.w * 0.32f, side.h * 0.40f};
    const planetary_system_def* system = metrics->planetarium_system;

    vg_result r = vg_fill_rect(ctx, panel, &haze);
    if (r != VG_OK) return r;
    r = vg_draw_rect(ctx, panel, &frame);
    if (r != VG_OK) return r;
    r = vg_draw_rect(ctx, map, &frame);
    if (r != VG_OK) return r;
    r = vg_draw_rect(ctx, side, &frame);
    if (r != VG_OK) return r;

    {
        const float marq_x = panel.x + panel.w * 0.025f;
        const float marq_r = side.x + side.w;
        const vg_rect marq_box = {marq_x, panel.y + panel.h * 0.945f, marq_r - marq_x, panel.h * 0.040f};
        vg_text_fx_marquee marquee = {0};
        marquee.text = (metrics->planetarium_marquee_text && metrics->planetarium_marquee_text[0] != '\0')
                           ? metrics->planetarium_marquee_text
                           : "PLANETARIUM CONTRACT GRID  ";
        /* Pixel-snap scroll to reduce sub-pixel crawl shimmer on thin vector glyphs. */
        marquee.offset_px = floorf(metrics->planetarium_marquee_offset_px + 0.5f);
        marquee.speed_px_s = 70.0f;
        marquee.gap_px = 48.0f;
        vg_fill_style marq_bg = {
            .intensity = 1.0f,
            .color = {pal.haze.r, pal.haze.g, pal.haze.b, 0.92f},
            .blend = VG_BLEND_ALPHA
        };
        vg_stroke_style marq_bd = txt;
        marq_bd.width_px = 1.4f * ui;
        marq_bd.intensity = 0.85f;
        r = vg_text_fx_marquee_draw(
            ctx,
            &marquee,
            marq_box,
            14.0f * ui,
            0.8f * ui,
            VG_TEXT_DRAW_MODE_STROKE,
            &txt,
            1.0f,
            &marq_bg,
            &marq_bd
        );
        if (r != VG_OK) return r;
    }
    r = draw_text_vector_glow(ctx, "3 TO EXIT   LEFT/RIGHT SELECT   ENTER ACCEPT", (vg_vec2){panel.x + panel.w * 0.03f, panel.y + panel.h * 0.03f}, 11.8f * ui, 0.90f * ui, &frame, &txt);
    if (r != VG_OK) return r;
    {
        char tty_fallback[96];
        const char* tty_line = metrics->teletype_text;
        if (!tty_line || tty_line[0] == '\0') {
            int tty_selected = metrics->planetarium_selected;
            if (tty_selected < 0) tty_selected = 0;
            if (tty_selected >= metrics->planetarium_system_count) {
                const char* boss = (system && system->boss_gate_label && system->boss_gate_label[0] != '\0')
                                       ? system->boss_gate_label
                                       : "BOSS GATE";
                snprintf(tty_fallback, sizeof(tty_fallback), "%s", boss);
            } else {
                const planet_def* p = metrics_planet(metrics, tty_selected);
                if (p && p->display_name && p->display_name[0] != '\0') {
                    snprintf(tty_fallback, sizeof(tty_fallback), "%s", p->display_name);
                } else {
                    snprintf(tty_fallback, sizeof(tty_fallback), "SYSTEM %02d", tty_selected + 1);
                }
            }
            tty_line = tty_fallback;
        }
        r = draw_text_vector_glow(
            ctx,
            tty_line,
            (vg_vec2){map.x + map.w * 0.02f, map.y + map.h * 0.95f},
            13.4f * ui,
            0.90f * ui,
            &frame,
            &txt
        );
        if (r != VG_OK) return r;
    }

    const float cx = map.x + map.w * 0.50f;
    const float cy = map.y + map.h * 0.52f;
    const int systems = (metrics->planetarium_system_count > 0)
                            ? ((metrics->planetarium_system_count > PLANETARIUM_MAX_SYSTEMS) ? PLANETARIUM_MAX_SYSTEMS : metrics->planetarium_system_count)
                            : 1;
    const int boss_idx = systems;
    int selected_idx = metrics->planetarium_selected;
    if (selected_idx < 0) selected_idx = 0;
    if (selected_idx > boss_idx) selected_idx = boss_idx;
    const float node_r = fminf(w, h) * 0.012f;
    const float orbit_rot = 0.22f;
    float node_x[PLANETARIUM_MAX_SYSTEMS];
    float node_y[PLANETARIUM_MAX_SYSTEMS];

    {
        char left_title[96];
        char left_status[80];
        const int boss_unlocked = (metrics->planetarium_systems_quelled >= systems);
        if (selected_idx >= systems) {
            const char* boss = (system && system->boss_gate_label && system->boss_gate_label[0] != '\0')
                                   ? system->boss_gate_label
                                   : "BOSS GATE";
            snprintf(left_title, sizeof(left_title), "%s", boss);
            snprintf(left_status, sizeof(left_status), "STATUS  %s", boss_unlocked ? "READY" : "LOCKED");
        } else {
            const planet_def* p = metrics_planet(metrics, selected_idx);
            const char* pending = "PENDING";
            const char* quelled = "QUELLED";
            if (p) {
                if (p->lore.status_pending && p->lore.status_pending[0] != '\0') {
                    pending = p->lore.status_pending;
                }
                if (p->lore.status_quelled && p->lore.status_quelled[0] != '\0') {
                    quelled = p->lore.status_quelled;
                }
            }
            left_title[0] = '\0';
            snprintf(left_status, sizeof(left_status), "STATUS  %s", metrics->planetarium_nodes_quelled[selected_idx] ? quelled : pending);
        }
        if (left_title[0] != '\0') {
            r = draw_text_vector_glow(ctx, left_title, (vg_vec2){map.x + map.w * 0.02f, map.y + map.h * 0.90f}, 12.8f * ui, 0.88f * ui, &frame, &txt);
            if (r != VG_OK) return r;
        }
        r = draw_text_vector_glow(ctx, left_status, (vg_vec2){map.x + map.w * 0.02f, map.y + map.h * 0.91f}, 9.6f * ui, 0.70f * ui, &frame, &txt);
        if (r != VG_OK) return r;
    }

    for (int i = 0; i < systems; ++i) {
        planetarium_node_center(w, h, systems, i, t_s, &node_x[i], &node_y[i]);
        const float orbit_t = ((float)i + 1.0f) / ((float)systems + 1.0f);
        const float rx = map.w * (0.12f + orbit_t * 0.30f);
        const float ry = map.h * (0.04f + orbit_t * 0.11f);
        const int seg_n = 128;
        vg_vec2 orbit[128];
        for (int j = 0; j < seg_n; ++j) {
            const float a = (float)j / (float)(seg_n - 1) * 6.28318530718f;
            const float c = cosf(a);
            const float s = sinf(a);
            orbit[j].x = cx + c * rx * cosf(orbit_rot) - s * ry * sinf(orbit_rot);
            orbit[j].y = cy + c * rx * sinf(orbit_rot) + s * ry * cosf(orbit_rot);
        }
        vg_stroke_style os = frame;
        os.width_px = 1.0f * ui;
        os.intensity = 0.45f + 0.07f * (float)i;
        os.color.a = 0.34f;
        r = vg_draw_polyline(ctx, orbit, seg_n, &os, 1);
        if (r != VG_OK) return r;
    }

    {
        vg_fill_style sun_c = {.intensity = 1.0f, .color = {1.0f, 0.86f, 0.45f, 0.92f}, .blend = VG_BLEND_ALPHA};
        r = vg_fill_circle(ctx, (vg_vec2){cx, cy}, node_r * 1.9f, &sun_c, 20);
        if (r != VG_OK) return r;
    }

    for (int i = 0; i < systems; ++i) {
        const float nx = node_x[i];
        const float ny = node_y[i];
        const int selected = (metrics->planetarium_selected == i);
        const int quelled = metrics->planetarium_nodes_quelled[i] ? 1 : 0;
        vg_fill_style f = {
            .intensity = selected ? 1.2f : 0.95f,
            .color = quelled ? (vg_color){0.35f, 1.0f, 0.62f, 0.95f} : (vg_color){0.35f, 0.72f, 1.0f, 0.85f},
            .blend = VG_BLEND_ALPHA
        };
        r = vg_fill_circle(ctx, (vg_vec2){nx, ny}, node_r, &f, 18);
        if (r != VG_OK) return r;
        vg_stroke_style ns = frame;
        ns.width_px = selected ? 2.4f : 1.4f;
        ns.color = selected ? pal.secondary : pal.primary;
        ns.intensity = selected ? 1.25f : 0.8f;
        {
            const int cn = 24;
            vg_vec2 ring[24];
            const float rr = node_r * 1.35f;
            for (int ci = 0; ci < cn; ++ci) {
                const float a = (float)ci / (float)(cn - 1) * 6.28318530718f;
                ring[ci].x = nx + cosf(a) * rr;
                ring[ci].y = ny + sinf(a) * rr;
            }
            r = vg_draw_polyline(ctx, ring, cn, &ns, 1);
        }
        if (r != VG_OK) return r;

        {
            const planet_def* p = metrics_planet(metrics, i);
            const char* label = (p && p->display_name && p->display_name[0] != '\0')
                                    ? p->display_name
                                    : fallback_planet_label(i);
            const float lx = nx + node_r * 1.6f;
            const float ly = ny + node_r * (i & 1 ? -1.2f : 1.4f);
        r = draw_text_vector_glow(ctx, label, (vg_vec2){lx, ly}, 7.4f * ui, 0.60f * ui, &frame, &txt);
        if (r != VG_OK) return r;
    }
    }

    {
        float bx = 0.0f;
        float by = 0.0f;
        planetarium_node_center(w, h, systems, boss_idx, t_s, &bx, &by);
        const int selected = (metrics->planetarium_selected == boss_idx);
        const int boss_unlocked = (metrics->planetarium_systems_quelled >= systems);
        vg_stroke_style gate = frame;
        gate.width_px = 1.2f * ui;
        gate.intensity = 0.78f;
        gate.color.a = 0.50f;
        vg_fill_style bf = {
            .intensity = 1.0f,
            .color = boss_unlocked ? (vg_color){1.0f, 0.34f, 0.32f, 0.95f} : (vg_color){0.52f, 0.20f, 0.22f, 0.58f},
            .blend = VG_BLEND_ALPHA
        };
        r = vg_fill_circle(ctx, (vg_vec2){bx, by}, node_r * 1.35f, &bf, 20);
        if (r != VG_OK) return r;
        gate.width_px = (selected ? 2.4f : 1.5f) * ui;
        gate.color = selected ? pal.secondary : pal.primary;
        gate.intensity = selected ? 1.28f : 0.9f;
        {
            const int cn = 26;
            vg_vec2 ring[26];
            for (int ci = 0; ci < cn; ++ci) {
                const float a = (float)ci / (float)(cn - 1) * 6.28318530718f;
                ring[ci].x = bx + cosf(a) * node_r * 1.75f;
                ring[ci].y = by + sinf(a) * node_r * 1.75f;
            }
            r = vg_draw_polyline(ctx, ring, cn, &gate, 1);
            if (r != VG_OK) return r;
        }
        {
            const char* boss_label = (system && system->boss_gate_label && system->boss_gate_label[0] != '\0')
                                         ? system->boss_gate_label
                                         : "BOSS GATE";
            r = draw_text_vector_glow(ctx, boss_label, (vg_vec2){bx + node_r * 2.0f, by + node_r * 1.6f}, 8.0f * ui, 0.62f * ui, &frame, &txt);
        }
        if (r != VG_OK) return r;
    }

    {
        vg_vec2 target = {cx, cy};
        if (metrics->planetarium_selected < systems) {
            target.x = node_x[metrics->planetarium_selected];
            target.y = node_y[metrics->planetarium_selected];
        } else {
            planetarium_node_center(w, h, systems, boss_idx, t_s, &target.x, &target.y);
        }
        const vg_vec2 sweep[2] = {{cx, cy}, {target.x, target.y}};
        vg_stroke_style sw = txt;
        sw.width_px = 1.4f * ui;
        sw.intensity = 1.12f;
        sw.color.a = 0.68f;
        r = vg_draw_polyline(ctx, sweep, 2, &sw, 0);
        if (r != VG_OK) return r;
    }

    {
        const int selected = selected_idx;
        const int boss_unlocked = (metrics->planetarium_systems_quelled >= systems);
        const int remaining = systems - metrics->planetarium_systems_quelled;
        const float stats_x = side.x + side.w * 0.42f;
        const float meta_size = 11.0f * ui;
        const float meta_weight = 0.42f * ui;
        const float body_size = 11.2f * ui;
        const float body_weight = 0.34f * ui;
        const float top_y = side.y + side.h * 0.95f;
        const float bottom_y = side.y + side.h * 0.03f;
        const float meta_step = body_size * 3.00f;
        vg_stroke_style frame_emph = frame;
        frame_emph.intensity *= 1.18f;
        frame_emph.width_px *= 1.20f;
        vg_stroke_style txt_emph = txt;
        txt_emph.intensity *= 1.24f;
        txt_emph.width_px *= 1.20f;
        char stat_line[128];
        float cursor_y = top_y;
        r = draw_text_vector_glow(
            ctx,
            "MISSION BRIEFING FROM COMMANDER NICK",
            (vg_vec2){side.x + side.w * 0.06f, cursor_y},
            11.8f * ui,
            0.92f * ui,
            &frame_emph,
            &txt_emph
        );
        if (r != VG_OK) return r;
        cursor_y -= body_size * 2.10f;

        float stats_cursor_y = cursor_y - body_size * 2.20f;
        if (system && system->display_name && system->display_name[0] != '\0') {
            snprintf(stat_line, sizeof(stat_line), "SECTOR  %s", system->display_name);
            r = draw_text_vector_glow(ctx, stat_line, (vg_vec2){stats_x, stats_cursor_y}, meta_size, meta_weight, &frame, &txt);
            if (r != VG_OK) return r;
            stats_cursor_y -= meta_step;
        }
        snprintf(stat_line, sizeof(stat_line), "SYSTEMS QUELLED  %d / %d", metrics->planetarium_systems_quelled, systems);
        r = draw_text_vector_glow(ctx, stat_line, (vg_vec2){stats_x, stats_cursor_y}, meta_size, meta_weight, &frame, &txt);
        if (r != VG_OK) return r;
        stats_cursor_y -= meta_step;
        snprintf(stat_line, sizeof(stat_line), "SYSTEMS REMAINING  %d", remaining > 0 ? remaining : 0);
        r = draw_text_vector_glow(ctx, stat_line, (vg_vec2){stats_x, stats_cursor_y}, meta_size, meta_weight, &frame, &txt);
        if (r != VG_OK) return r;
        stats_cursor_y -= meta_step;
        snprintf(stat_line, sizeof(stat_line), "BOSS GATE  %s", boss_unlocked ? "UNLOCKED" : "LOCKED");
        r = draw_text_vector_glow(ctx, stat_line, (vg_vec2){stats_x, stats_cursor_y}, meta_size, meta_weight, &frame, &txt);
        if (r != VG_OK) return r;
        stats_cursor_y -= meta_step;
        {
            const planet_def* p = metrics_planet(metrics, selected);
            const char* selected_label = (selected < systems)
                                             ? ((p && p->display_name && p->display_name[0] != '\0')
                                                    ? p->display_name
                                                    : fallback_planet_label(selected))
                                             : ((system && system->boss_gate_label && system->boss_gate_label[0] != '\0')
                                                    ? system->boss_gate_label
                                                    : "BOSS GATE");
            snprintf(stat_line, sizeof(stat_line), "SELECTED  %s", selected_label);
            r = draw_text_vector_glow(ctx, stat_line, (vg_vec2){stats_x, stats_cursor_y}, meta_size, meta_weight, &frame, &txt);
            if (r != VG_OK) return r;
            stats_cursor_y -= meta_step;
            if (p) {
                snprintf(stat_line, sizeof(stat_line), "ORBIT LANE  %d", p->orbit_lane + 1);
                r = draw_text_vector_glow(ctx, stat_line, (vg_vec2){stats_x, stats_cursor_y}, meta_size, meta_weight, &frame, &txt);
                if (r != VG_OK) return r;
            }
        }
        if (selected < systems) {
            const planet_def* p = metrics_planet(metrics, selected);
            float y = fminf(nick_rect.y - body_size * 0.95f, stats_cursor_y - body_size * 0.95f);
            if (y > top_y - body_size * 2.5f) {
                y = top_y - body_size * 2.5f;
            }
            const float text_x = side.x + side.w * 0.06f;
            const float text_w = side.w * 0.90f;
            const float text_bottom = bottom_y;
            const float gap = body_size * 0.70f;
            r = draw_text_vector_glow(ctx, "NICK:", (vg_vec2){side.x + side.w * 0.06f, y}, meta_size, meta_weight * 1.10f, &frame_emph, &txt_emph);
            if (r != VG_OK) return r;
            y -= body_size * 1.75f;

            {
                const char* msg = "KEEP YOUR HEAD COOL, KID.\nFLY CLEAN.";
                const float remaining_h = y - text_bottom;
                float used_h = 0.0f;
                if (p) {
                    msg = commander_nick_dialogue(p->lore.commander_message_id);
                }
                if (remaining_h > body_size * 1.35f) {
                    r = draw_wrapped_text_block_down(
                        ctx,
                        msg,
                        text_x,
                        y,
                        text_bottom,
                        text_w,
                        body_size,
                        body_weight,
                        &frame,
                        &txt,
                        &used_h
                    );
                    if (r != VG_OK) return r;
                    y -= used_h + gap;
                }
            }

            if (y - text_bottom > body_size * 1.8f) {
                r = draw_text_vector_glow(ctx, "INTEL:", (vg_vec2){side.x + side.w * 0.06f, y}, meta_size, meta_weight * 1.10f, &frame_emph, &txt_emph);
                if (r != VG_OK) return r;
                y -= body_size * 1.65f;
            }

            int paragraph_count = 0;
            if (p && p->lore.mission_paragraph_count > 0) {
                paragraph_count = p->lore.mission_paragraph_count;
                if (paragraph_count > 3) {
                    paragraph_count = 3;
                }
            }
            if (paragraph_count <= 0) {
                paragraph_count = 1;
            }
            for (int pi = 0; pi < paragraph_count; ++pi) {
                const char* para = (p && p->lore.mission_paragraphs[pi] && p->lore.mission_paragraphs[pi][0] != '\0')
                                       ? p->lore.mission_paragraphs[pi]
                                       : ((p && p->lore.briefing_lines[pi] && p->lore.briefing_lines[pi][0] != '\0')
                                              ? p->lore.briefing_lines[pi]
                                              : "NO ADDITIONAL INTEL.");
                const float remaining_h = y - text_bottom;
                float used_h = 0.0f;
                if (remaining_h <= body_size * 1.35f) {
                    break;
                }
                r = draw_wrapped_text_block_down(
                    ctx,
                    para,
                    text_x,
                    y,
                    text_bottom,
                    text_w,
                    body_size,
                    body_weight,
                    &frame,
                    &txt,
                    &used_h
                );
                if (r != VG_OK) return r;
                y -= used_h + gap;
            }
        } else {
            const char* boss_ready = (system && system->boss_gate_ready_text && system->boss_gate_ready_text[0] != '\0')
                                         ? system->boss_gate_ready_text
                                         : "BOSS GATE TELEMETRY SYNCHRONIZED.";
            const char* boss_locked = (system && system->boss_gate_locked_text && system->boss_gate_locked_text[0] != '\0')
                                          ? system->boss_gate_locked_text
                                          : "ALL SYSTEMS MUST BE QUELLED BEFORE LAUNCH.";
            const float text_x = side.x + side.w * 0.06f;
            const float text_w = side.w * 0.90f;
            const float text_bottom = bottom_y;
            float y = fminf(nick_rect.y - body_size * 0.95f, stats_cursor_y - body_size * 0.95f);
            float used_h = 0.0f;
            const float gap = body_size * 0.55f;
            r = draw_wrapped_text_block_down(ctx, boss_ready, text_x, y, text_bottom, text_w, body_size, body_weight, &frame, &txt, &used_h);
            if (r != VG_OK) return r;
            y -= used_h + gap;
            r = draw_wrapped_text_block_down(ctx, boss_locked, text_x, y, text_bottom, text_w, body_size, body_weight, &frame, &txt, &used_h);
            if (r != VG_OK) return r;
            y -= used_h + gap;
            r = draw_wrapped_text_block_down(ctx, "EXPECT COORDINATED ELITE RESISTANCE.", text_x, y, text_bottom, text_w, body_size, body_weight, &frame, &txt, &used_h);
            if (r != VG_OK) return r;
        }
    }

    if (metrics->nick_rgba8 && metrics->nick_w > 0 && metrics->nick_h > 0 && metrics->nick_stride > 0) {
        vg_image_desc img = {
            .pixels_rgba8 = metrics->nick_rgba8,
            .width = metrics->nick_w,
            .height = metrics->nick_h,
            .stride_bytes = metrics->nick_stride
        };
        vg_image_style is = {
            .kind = VG_IMAGE_STYLE_MONO_SCANLINE,
            .threshold = 0.40f,
            .contrast = 1.18f,
            .scanline_pitch_px = 1.65f,
            .min_line_width_px = 0.42f,
            .max_line_width_px = 1.45f,
            .line_jitter_px = 0.0f,
            .cell_width_px = 0.0f,
            .cell_height_px = 0.0f,
            .block_levels = 0,
            .intensity = 1.0f,
            .tint_color = pal.secondary,
            .blend = VG_BLEND_ALPHA,
            .use_crt_palette = 0,
            .use_context_palette = 0,
            .palette_index = 0,
            .invert = 0,
            .use_boxed_glyphs = 0
        };
        vg_rect dst = {nick_rect.x + 4.0f, nick_rect.y + 4.0f, nick_rect.w - 8.0f, nick_rect.h - 8.0f};
        const float img_ar = (float)metrics->nick_w / (float)metrics->nick_h;
        const float dst_ar = dst.w / fmaxf(dst.h, 1.0f);
        if (img_ar > dst_ar) {
            const float old_h = dst.h;
            dst.h = dst.w / fmaxf(img_ar, 1e-5f);
            dst.y += (old_h - dst.h) * 0.5f;
        } else {
            const float old_w = dst.w;
            dst.w = dst.h * img_ar;
            dst.x += (old_w - dst.w) * 0.5f;
        }
        r = vg_draw_image_stylized(ctx, &img, dst, &is);
        if (r != VG_OK) {
            r = draw_text_vector_glow(ctx, "NICK FEED DEGRADED", (vg_vec2){nick_rect.x + 10.0f * ui, nick_rect.y + nick_rect.h * 0.5f}, 10.0f * ui, 0.8f * ui, &frame, &txt);
            if (r != VG_OK) return r;
        }
    } else {
        r = draw_text_vector_glow(ctx, "NICK PORTRAIT OFFLINE", (vg_vec2){nick_rect.x + 10.0f * ui, nick_rect.y + nick_rect.h * 0.5f}, 10.0f * ui, 0.8f * ui, &frame, &txt);
        if (r != VG_OK) return r;
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
    enum { N = 96 };
    vg_vec2 line[N];
    for (int i = 0; i < N; ++i) {
        const float x = ((float)i / (float)(N - 1)) * w;
        const float wx = cam_x * parallax + x;
        const float y =
            base_y +
            sinf(wx * 0.010f) * amp +
            sinf(wx * 0.026f + 1.4f) * amp * 0.55f +
            sinf(wx * 0.040f + 2.2f) * amp * 0.12f;
        line[i].x = x;
        line[i].y = y;
    }

    vg_result r = vg_draw_polyline(ctx, line, N, halo, 0);
    if (r != VG_OK) {
        return r;
    }
    return vg_draw_polyline(ctx, line, N, main, 0);
}

static int horizon_bin_index(float x, float w, int bins) {
    if (bins <= 1 || w <= 1e-6f) {
        return 0;
    }
    float t = x / w;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    int i = (int)floorf(t * (float)(bins - 1) + 0.5f);
    if (i < 0) i = 0;
    if (i >= bins) i = bins - 1;
    return i;
}

static void horizon_segment_update(float* horizon, int bins, float w, vg_vec2 a, vg_vec2 b) {
    if (!horizon || bins <= 0) {
        return;
    }
    int i0 = horizon_bin_index(a.x, w, bins);
    int i1 = horizon_bin_index(b.x, w, bins);
    if (i0 > i1) {
        const int ti = i0;
        i0 = i1;
        i1 = ti;
        const vg_vec2 tp = a;
        a = b;
        b = tp;
    }
    const float dx = b.x - a.x;
    if (fabsf(dx) < 1e-4f) {
        const float y = fmaxf(a.y, b.y);
        if (y > horizon[i0]) {
            horizon[i0] = y;
        }
        return;
    }
    for (int i = i0; i <= i1; ++i) {
        const float x = ((float)i / (float)(bins - 1)) * w;
        float t = (x - a.x) / dx;
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
        const float y = lerpf(a.y, b.y, t);
        if (y > horizon[i]) {
            horizon[i] = y;
        }
    }
}

static vg_result draw_high_plains_drifter_terrain(
    vg_context* ctx,
    const game_state* g,
    const vg_stroke_style* halo,
    const vg_stroke_style* main
) {
    enum { ROWS = 24, COLS = 70 };
    vg_vec2 pts[ROWS][COLS];
    float row_depth[ROWS];
    enum { HORIZON_BINS = 384 };
    float horizon[HORIZON_BINS];
    const float w = g->world_w;
    const float h = g->world_h;
    const float y_near = h * 0.04f;
    const float y_far = h * 0.34f;
    const float cam = g->camera_x;
    const int enable_horizon_cull = (g->level_style == LEVEL_STYLE_HIGH_PLAINS_DRIFTER_2);

    for (int r = 0; r < ROWS; ++r) {
        const float z = (float)r / (float)(ROWS - 1);
        const float p = powf(z, 0.78f);
        row_depth[r] = z;
        const float y_base = lerpf(y_near, y_far, p);
        const float half_w = lerpf(w * 0.78f, w * 0.54f, p);
        const float center_x = w * 0.50f + lerpf(w * 0.03f, -w * 0.02f, p);
        const float amp = lerpf(h * 0.21f, h * 0.08f, p);
        for (int c = 0; c < COLS; ++c) {
            const float u = (float)c / (float)(COLS - 1);
            const float x = center_x + (u - 0.5f) * 2.0f * half_w;
            const float world_x = cam * (0.85f + p * 1.35f) + (u - 0.5f) * (2200.0f + p * 1800.0f);
            const float n = high_plains_looped_noise(world_x * 0.70f, p * 4.5f + 0.35f) * 1.95f;
            const float y = y_base + n * amp;
            pts[r][c] = (vg_vec2){x, y};
        }
    }

    if (enable_horizon_cull) {
        const float eps = h * 0.0025f;
        for (int i = 0; i < HORIZON_BINS; ++i) {
            horizon[i] = -1e9f;
        }

        for (int r = 0; r < ROWS - 1; ++r) {
            const float z_h = row_depth[r];
            const float fade_h = 0.10f + (1.0f - z_h) * (1.0f - z_h) * 0.90f;
            const float z_v = 0.5f * (row_depth[r] + row_depth[r + 1]);
            const float fade_v = 0.09f + (1.0f - z_v) * (1.0f - z_v) * 0.91f;
            for (int c = 0; c < COLS - 1; ++c) {
                const vg_vec2 p00 = pts[r][c];
                const vg_vec2 p10 = pts[r][c + 1];
                const vg_vec2 p01 = pts[r + 1][c];
                const vg_vec2 p11 = pts[r + 1][c + 1];

                const int i00 = horizon_bin_index(p00.x, w, HORIZON_BINS);
                const int i10 = horizon_bin_index(p10.x, w, HORIZON_BINS);
                const int i01 = horizon_bin_index(p01.x, w, HORIZON_BINS);
                const int i11 = horizon_bin_index(p11.x, w, HORIZON_BINS);
                const int quad_vis =
                    (p00.y > horizon[i00] + eps) ||
                    (p10.y > horizon[i10] + eps) ||
                    (p01.y > horizon[i01] + eps) ||
                    (p11.y > horizon[i11] + eps);
                if (!quad_vis) {
                    continue;
                }

                {
                    vg_stroke_style sh = *halo;
                    vg_stroke_style sm = *main;
                    vg_stroke_style glow = sm;
                    sh.intensity *= fade_h * 0.72f;
                    sm.intensity *= fade_h * 0.82f;
                    glow.intensity *= fade_h * 0.38f;
                    sh.color.a *= fade_h;
                    sm.color.a *= fade_h;
                    glow.color.a *= fade_h * 0.45f;
                    sh.width_px *= 0.94f + (1.0f - z_h) * 0.62f;
                    sm.width_px *= 0.90f + (1.0f - z_h) * 0.56f;
                    glow.width_px = fmaxf(glow.width_px * (1.35f + (1.0f - z_h) * 0.45f), sm.width_px * 1.35f);
                    glow.blend = VG_BLEND_ADDITIVE;
                    const vg_vec2 seg[2] = {p00, p10};
                    vg_result vr = vg_draw_polyline(ctx, seg, 2, &glow, 0);
                    if (vr != VG_OK) {
                        return vr;
                    }
                    vr = vg_draw_polyline(ctx, seg, 2, &sh, 0);
                    if (vr != VG_OK) {
                        return vr;
                    }
                    vr = vg_draw_polyline(ctx, seg, 2, &sm, 0);
                    if (vr != VG_OK) {
                        return vr;
                    }
                }

                if ((c % 2) == 0) {
                    const int major = (c % 8) == 0;
                    const float major_boost = major ? 1.0f : 0.62f;
                    vg_stroke_style sh = *halo;
                    vg_stroke_style sm = *main;
                    vg_stroke_style glow = sm;
                    sh.intensity *= fade_v * 0.56f * major_boost;
                    sm.intensity *= fade_v * 0.66f * major_boost;
                    glow.intensity *= fade_v * 0.34f * major_boost;
                    sh.color.a *= fade_v;
                    sm.color.a *= fade_v;
                    glow.color.a *= fade_v * 0.42f;
                    sh.width_px *= 0.82f + (1.0f - z_v) * 0.44f;
                    sm.width_px *= 0.80f + (1.0f - z_v) * 0.40f;
                    glow.width_px = fmaxf(glow.width_px * (1.28f + (1.0f - z_v) * 0.40f), sm.width_px * 1.30f);
                    glow.blend = VG_BLEND_ADDITIVE;
                    const vg_vec2 seg[2] = {p00, p01};
                    vg_result vr = vg_draw_polyline(ctx, seg, 2, &glow, 0);
                    if (vr != VG_OK) {
                        return vr;
                    }
                    vr = vg_draw_polyline(ctx, seg, 2, &sh, 0);
                    if (vr != VG_OK) {
                        return vr;
                    }
                    vr = vg_draw_polyline(ctx, seg, 2, &sm, 0);
                    if (vr != VG_OK) {
                        return vr;
                    }
                }

                horizon_segment_update(horizon, HORIZON_BINS, w, p00, p10);
                horizon_segment_update(horizon, HORIZON_BINS, w, p00, p01);
                horizon_segment_update(horizon, HORIZON_BINS, w, p01, p11);
                horizon_segment_update(horizon, HORIZON_BINS, w, p10, p11);
            }
        }
        return VG_OK;
    }

    for (int r = 0; r < ROWS; ++r) {
        const float z = row_depth[r];
        const float fade = 0.10f + (1.0f - z) * (1.0f - z) * 0.90f;
        for (int c = 0; c < COLS - 1; ++c) {
            const vg_vec2 seg[2] = {pts[r][c], pts[r][c + 1]};
            vg_stroke_style sh = *halo;
            vg_stroke_style sm = *main;
            vg_stroke_style glow = sm;
            sh.intensity *= fade * 0.72f;
            sm.intensity *= fade * 0.82f;
            glow.intensity *= fade * 0.38f;
            sh.color.a *= fade;
            sm.color.a *= fade;
            glow.color.a *= fade * 0.45f;
            sh.width_px *= 0.94f + (1.0f - z) * 0.62f;
            sm.width_px *= 0.90f + (1.0f - z) * 0.56f;
            glow.width_px = fmaxf(glow.width_px * (1.35f + (1.0f - z) * 0.45f), sm.width_px * 1.35f);
            glow.blend = VG_BLEND_ADDITIVE;
            vg_result vr = vg_draw_polyline(ctx, seg, 2, &glow, 0);
            if (vr != VG_OK) {
                return vr;
            }
            vr = vg_draw_polyline(ctx, seg, 2, &sh, 0);
            if (vr != VG_OK) {
                return vr;
            }
            vr = vg_draw_polyline(ctx, seg, 2, &sm, 0);
            if (vr != VG_OK) {
                return vr;
            }
        }
    }

    for (int c = 0; c < COLS; c += 2) {
        const int major = (c % 8) == 0;
        const float major_boost = major ? 1.0f : 0.62f;
        for (int r = 0; r < ROWS - 1; ++r) {
            const vg_vec2 seg[2] = {pts[r][c], pts[r + 1][c]};
            const float z = 0.5f * (row_depth[r] + row_depth[r + 1]);
            const float fade = 0.09f + (1.0f - z) * (1.0f - z) * 0.91f;
            vg_stroke_style sh = *halo;
            vg_stroke_style sm = *main;
            vg_stroke_style glow = sm;
            sh.intensity *= fade * 0.56f * major_boost;
            sm.intensity *= fade * 0.66f * major_boost;
            glow.intensity *= fade * 0.34f * major_boost;
            sh.color.a *= fade;
            sm.color.a *= fade;
            glow.color.a *= fade * 0.42f;
            sh.width_px *= 0.82f + (1.0f - z) * 0.44f;
            sm.width_px *= 0.80f + (1.0f - z) * 0.40f;
            glow.width_px = fmaxf(glow.width_px * (1.28f + (1.0f - z) * 0.40f), sm.width_px * 1.30f);
            glow.blend = VG_BLEND_ADDITIVE;
            vg_result vr = vg_draw_polyline(ctx, seg, 2, &glow, 0);
            if (vr != VG_OK) {
                return vr;
            }
            vr = vg_draw_polyline(ctx, seg, 2, &sh, 0);
            if (vr != VG_OK) {
                return vr;
            }
            vr = vg_draw_polyline(ctx, seg, 2, &sm, 0);
            if (vr != VG_OK) {
                return vr;
            }
        }
    }
    return VG_OK;
}

static vg_result draw_high_plains_drifter_terrain_traditional(
    vg_context* ctx,
    const game_state* g,
    const vg_stroke_style* halo,
    const vg_stroke_style* main
) {
    enum { ROWS = 28, COLS = 76 };
    vg_vec2 pts[ROWS][COLS];
    float depth01[ROWS][COLS];
    int valid[ROWS][COLS];

    const float w = g->world_w;
    const float h = g->world_h;
    const float cx = w * 0.5f;
    const float cy = h * 0.30f;    /* Lower-third anchor in this Y-up coordinate system. */
    const float focal = h * 1.22f; /* Perspective scale. */

    /* World basis: x=right, y=up, z=forward. */
    const float world_z_near = 420.0f;
    const float world_z_far = 4200.0f;
    const float cam_x = g->camera_x;
    const float cam_y = h * 0.16f;
    const float cam_z = 0.0f;
    const float pitch_down = 0.13f;
    const float cp = cosf(pitch_down);
    const float sp = sinf(pitch_down);
    const float col_spacing = w * 0.055f;
    const float span = col_spacing * (float)(COLS - 1);
    const int x0 = (int)floorf((cam_x - span * 0.5f) / col_spacing) - 2;

    for (int r = 0; r < ROWS; ++r) {
        const float v = (float)r / (float)(ROWS - 1); /* near -> far */
        const float p = powf(v, 1.12f);
        const float zw = lerpf(world_z_near, world_z_far, p);
        const float amp = lerpf(h * 0.18f, h * 0.050f, p);
        for (int c = 0; c < COLS; ++c) {
            const float xw = (float)(x0 + c) * col_spacing;
            const float n = high_plains_looped_noise(xw * 1.20f, zw * 1.75f) * 1.65f;
            const float yw = n * amp;

            /* View transform (camera translation + pitch around x-axis). */
            const float xt = xw - cam_x;
            const float yt = yw - cam_y;
            const float zt = zw - cam_z;
            /* View rotation around x-axis (positive pitch looks downward). */
            const float yv = yt * cp - zt * sp;
            const float zv = yt * sp + zt * cp;

            if (zv <= 4.0f) {
                valid[r][c] = 0;
                depth01[r][c] = 0.0f;
                pts[r][c] = (vg_vec2){0.0f, 0.0f};
                continue;
            }

            const float invz = 1.0f / zv;
            const float sx = cx + xt * focal * invz;
            const float sy = cy + yv * focal * invz;
            const float d = clampf((zv - world_z_near) / fmaxf(world_z_far - world_z_near, 1.0f), 0.0f, 1.0f);

            if (sy > h * 1.35f || sy < -h * 0.10f) {
                valid[r][c] = 0;
                depth01[r][c] = d;
                pts[r][c] = (vg_vec2){sx, sy};
                continue;
            }

            valid[r][c] = 1;
            depth01[r][c] = d;
            pts[r][c] = (vg_vec2){sx, sy};
        }
    }

    for (int r = 0; r < ROWS; ++r) {
        for (int c = 0; c < COLS - 1; ++c) {
            if (!valid[r][c] || !valid[r][c + 1]) {
                continue;
            }
            const float d = 0.5f * (depth01[r][c] + depth01[r][c + 1]);
            const float fade = 0.12f + (1.0f - d) * (1.0f - d) * 0.88f;
            vg_stroke_style sh = *halo;
            vg_stroke_style sm = *main;
            vg_stroke_style glow = sm;
            sh.intensity *= fade * 0.62f;
            sm.intensity *= fade * 0.78f;
            glow.intensity *= fade * 0.34f;
            sh.color.a *= fade;
            sm.color.a *= fade;
            glow.color.a *= fade * 0.40f;
            sh.width_px *= 0.80f + (1.0f - d) * 0.58f;
            sm.width_px *= 0.78f + (1.0f - d) * 0.50f;
            glow.width_px = fmaxf(glow.width_px * (1.22f + (1.0f - d) * 0.44f), sm.width_px * 1.30f);
            glow.blend = VG_BLEND_ADDITIVE;
            const vg_vec2 seg[2] = {pts[r][c], pts[r][c + 1]};
            vg_result vr = vg_draw_polyline(ctx, seg, 2, &glow, 0);
            if (vr != VG_OK) {
                return vr;
            }
            vr = vg_draw_polyline(ctx, seg, 2, &sh, 0);
            if (vr != VG_OK) {
                return vr;
            }
            vr = vg_draw_polyline(ctx, seg, 2, &sm, 0);
            if (vr != VG_OK) {
                return vr;
            }
        }
    }

    for (int c = 0; c < COLS; ++c) {
        const int major = (c % 6) == 0;
        const float major_boost = major ? 1.0f : 0.72f;
        for (int r = 0; r < ROWS - 1; ++r) {
            if (!valid[r][c] || !valid[r + 1][c]) {
                continue;
            }
            const float d = 0.5f * (depth01[r][c] + depth01[r + 1][c]);
            const float fade = 0.10f + (1.0f - d) * (1.0f - d) * 0.90f;
            vg_stroke_style sh = *halo;
            vg_stroke_style sm = *main;
            vg_stroke_style glow = sm;
            sh.intensity *= fade * 0.50f * major_boost;
            sm.intensity *= fade * 0.60f * major_boost;
            glow.intensity *= fade * 0.30f * major_boost;
            sh.color.a *= fade;
            sm.color.a *= fade;
            glow.color.a *= fade * 0.38f;
            sh.width_px *= 0.72f + (1.0f - d) * 0.44f;
            sm.width_px *= 0.70f + (1.0f - d) * 0.40f;
            glow.width_px = fmaxf(glow.width_px * (1.18f + (1.0f - d) * 0.40f), sm.width_px * 1.24f);
            glow.blend = VG_BLEND_ADDITIVE;
            const vg_vec2 seg[2] = {pts[r][c], pts[r + 1][c]};
            vg_result vr = vg_draw_polyline(ctx, seg, 2, &glow, 0);
            if (vr != VG_OK) {
                return vr;
            }
            vr = vg_draw_polyline(ctx, seg, 2, &sh, 0);
            if (vr != VG_OK) {
                return vr;
            }
            vr = vg_draw_polyline(ctx, seg, 2, &sm, 0);
            if (vr != VG_OK) {
                return vr;
            }
        }
    }
    return VG_OK;
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

static vg_result draw_cylinder_wire(
    vg_context* ctx,
    const game_state* g,
    const vg_stroke_style* halo,
    const vg_stroke_style* main,
    int level_style
) {
    const float period = cylinder_period(g);
    enum { N = 96 };
    const float ring_y[] = {g->world_h * 0.06f, g->world_h * 0.46f, g->world_h * 0.86f};
    vg_stroke_style cyl_h = *halo;
    vg_stroke_style cyl_m = *main;
    cyl_h.intensity *= 0.62f;
    cyl_m.intensity *= 0.58f;
    if (level_style != LEVEL_STYLE_EVENT_HORIZON && level_style != LEVEL_STYLE_EVENT_HORIZON_LEGACY) {
        const int ring_start = (level_style == LEVEL_STYLE_ENEMY_RADAR) ? 2 : 1;
        for (int r = ring_start; r < 3; ++r) {
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
        const float phase_turns = repeatf(-(g->player.b.x) / fmaxf(period * 0.85f, 1.0f), 1.0f);
        const float radar_shift = phase_turns * (float)(N - 1);

        for (int ring = 0; ring < 8; ++ring) {
            const float rs = 1.0f - 0.11f * (float)ring;
            vg_vec2 loop[N];
            float loop_depth[N];
            for (int i = 0; i < N - 1; ++i) {
                const float u = (float)i + radar_shift;
                const int i0 = wrapi((int)floorf(u), N - 1);
                const int i1 = wrapi(i0 + 1, N - 1);
                const float t = u - floorf(u);
                const float ex = lerpf(radar_edge[i0].x, radar_edge[i1].x, t);
                const float ey = lerpf(radar_edge[i0].y, radar_edge[i1].y, t);
                loop[i].x = cx + (ex - cx) * rs;
                loop[i].y = cy + (ey - cy) * rs;
                loop_depth[i] = lerpf(edge_depth[i0], edge_depth[i1], t);
            }
            loop[N - 1] = loop[0];
            loop_depth[N - 1] = loop_depth[0];
            for (int i = 0; i < N - 1; ++i) {
                const float d = 0.5f * (loop_depth[i] + loop_depth[i + 1]);
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
            const float idxf = (float)(s * (N - 1)) / 20.0f + radar_shift;
            const int i0 = wrapi((int)floorf(idxf), N - 1);
            const int i1 = wrapi(i0 + 1, N - 1);
            const float t = idxf - floorf(idxf);
            const vg_vec2 spoke_tip = {
                lerpf(radar_edge[i0].x, radar_edge[i1].x, t),
                lerpf(radar_edge[i0].y, radar_edge[i1].y, t)
            };
            vg_vec2 spoke[2] = {
                {cx, cy},
                {spoke_tip.x, spoke_tip.y}
            };
            const float d = lerpf(edge_depth[i0], edge_depth[i1], t);
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
                float u = fmodf(a / 6.28318530718f + phase_turns, 1.0f);
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
            float u = fmodf(theta / 6.28318530718f + phase_turns, 1.0f);
            if (u < 0.0f) {
                u += 1.0f;
            }
            const float fi = u * (float)(N - 1);
            const int i0 = wrapi((int)floorf(fi), N - 1);
            const int i1 = wrapi(i0 + 1, N - 1);
            const float ft = fi - floorf(fi);
            const vg_vec2 edge_p = {
                lerpf(edge[i0].x, edge[i1].x, ft),
                lerpf(edge[i0].y, edge[i1].y, ft)
            };
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

			    if (level_style == LEVEL_STYLE_EVENT_HORIZON || level_style == LEVEL_STYLE_EVENT_HORIZON_LEGACY) {
		        /* Classic spacetime-fabric hourglass (wormhole throat) through cylinder center. */
		        static wormhole_cache wh;
		        wormhole_cache_ensure(&wh, g->world_w, g->world_h);

		        const vg_vec2 vc = project_cylinder_point(g, g->camera_x, g->world_h * 0.50f, NULL);
		        const float cx = vc.x;
		        const float cy = vc.y;
                const float spin_sign = (level_style == LEVEL_STYLE_EVENT_HORIZON_LEGACY) ? 1.0f : -1.0f;
                const float phase_turns = repeatf(spin_sign * g->player.b.x / fmaxf(period * 0.85f, 1.0f), 1.0f);
                const float loop_shift_legacy = phase_turns * (float)(WORMHOLE_VN - 1);
                const float loop_shift_modern = phase_turns * (float)WORMHOLE_VN;
                const float rail_shift = phase_turns * (float)WORMHOLE_COLS;

			        if (level_style == LEVEL_STYLE_EVENT_HORIZON_LEGACY) {
		            /* Legacy cull-based wireframe (from older render.c). */
		            for (int j = 0; j < WORMHOLE_ROWS; ++j) {
		                const float fade = wh.row_fade[j];
                        const int is_top = (j >= (WORMHOLE_ROWS / 2));
                        const float face_boost = is_top ? 1.65f : 1.0f;
                        const float face_lift = is_top ? 0.07f : 0.0f;
                        const float face_cutoff = is_top ? 0.0f : 0.02f;

		                vg_vec2 loop[WORMHOLE_VN];
                        float loop_face[WORMHOLE_VN];
                        const int nsrc = WORMHOLE_VN - 1;
                        for (int i = 0; i < nsrc; ++i) {
                            const float u = (float)i + loop_shift_legacy;
                            const int i0 = wrapi((int)floorf(u), nsrc);
                            const int i1 = wrapi(i0 + 1, nsrc);
                            const float t = u - floorf(u);
                            const vg_vec2 p0 = wh.loop_rel_legacy[j][i0];
		                            const vg_vec2 p1 = wh.loop_rel_legacy[j][i1];
		                            loop[i].x = cx + lerpf(p0.x, p1.x, t);
		                            loop[i].y = cy + lerpf(p0.y, p1.y, t);
		                            float lf = lerpf(wh.loop_face_legacy[j][i0], wh.loop_face_legacy[j][i1], t);
                                    lf = lf * face_boost + face_lift;
                                    loop_face[i] = clampf(lf, 0.0f, 1.0f);
		                        }
		                        loop[WORMHOLE_VN - 1] = loop[0];
		                        loop_face[WORMHOLE_VN - 1] = loop_face[0];

		                vg_stroke_style vh = *halo;
		                vg_stroke_style vm = *main;
		                vh.color = (vg_color){0.38f, 0.92f, 1.0f, 0.20f * fade};
		                vm.color = (vg_color){0.44f, 0.97f, 1.0f, 0.58f * fade};
		                vh.intensity *= 0.42f + fade * 0.48f;
		                vm.intensity *= 0.48f + fade * 0.56f;
		                vg_result vr = draw_polyline_culled(ctx, loop, loop_face, WORMHOLE_VN, &vh, 1, face_cutoff);
		                if (vr != VG_OK) {
		                    return vr;
		                }
		                vr = draw_polyline_culled(ctx, loop, loop_face, WORMHOLE_VN, &vm, 1, face_cutoff);
		                if (vr != VG_OK) {
		                    return vr;
		                }
		            }

		            for (int c = 0; c < WORMHOLE_COLS; ++c) {
		                vg_vec2 rail[WORMHOLE_ROWS];
                        float rail_face[WORMHOLE_ROWS];
                        const float cu = (float)c + rail_shift;
                        const int c0 = wrapi((int)floorf(cu), WORMHOLE_COLS);
                        const int c1 = wrapi(c0 + 1, WORMHOLE_COLS);
                        const float ct = cu - floorf(cu);
		                for (int j = 0; j < WORMHOLE_ROWS; ++j) {
		                    rail[j].x = cx + lerpf(wh.rail_rel_legacy[c0][j].x, wh.rail_rel_legacy[c1][j].x, ct);
		                    rail[j].y = cy + lerpf(wh.rail_rel_legacy[c0][j].y, wh.rail_rel_legacy[c1][j].y, ct);
                            {
                                const int is_top = (j >= (WORMHOLE_ROWS / 2));
                                const float face_boost = is_top ? 1.65f : 1.0f;
                                const float face_lift = is_top ? 0.07f : 0.0f;
                                float rf = lerpf(wh.rail_face_legacy[c0][j], wh.rail_face_legacy[c1][j], ct);
                                rf = rf * face_boost + face_lift;
                                rail_face[j] = clampf(rf, 0.0f, 1.0f);
                            }
		                }
                        const float fade = 0.90f;
		                vg_stroke_style rh = *halo;
		                vg_stroke_style rm = *main;
		                rh.color = (vg_color){0.38f, 0.92f, 1.0f, 0.20f * fade};
		                rm.color = (vg_color){0.44f, 0.97f, 1.0f, 0.58f * fade};
		                rh.width_px *= 1.55f;
		                rm.width_px *= 1.35f;
		                rh.intensity *= 0.42f + fade * 0.48f;
		                rm.intensity *= 0.48f + fade * 0.56f;
		                vg_result vr = draw_polyline_culled(ctx, rail, rail_face, WORMHOLE_ROWS, &rh, 0, 0.02f);
		                if (vr != VG_OK) {
		                    return vr;
		                }
		                vr = draw_polyline_culled(ctx, rail, rail_face, WORMHOLE_ROWS, &rm, 0, 0.02f);
		                if (vr != VG_OK) {
		                    return vr;
		                }
		            }
		        } else {
		            /* Modern uniform wireframe. */
                    const float modern_brightness = 0.68f;
		            for (int j = 0; j < WORMHOLE_ROWS; ++j) {
		                const float fade = wh.row_fade[j];
                        vg_vec2 loop[WORMHOLE_VN];
		                for (int i = 0; i < WORMHOLE_VN; ++i) {
                            const float u = (float)i + loop_shift_modern;
                            const int i0 = wrapi((int)floorf(u), WORMHOLE_VN);
                            const int i1 = wrapi(i0 + 1, WORMHOLE_VN);
                            const float t = u - floorf(u);
		                    loop[i].x = cx + lerpf(wh.loop_rel_modern[j][i0].x, wh.loop_rel_modern[j][i1].x, t);
		                    loop[i].y = cy + lerpf(wh.loop_rel_modern[j][i0].y, wh.loop_rel_modern[j][i1].y, t);
		                }

		                vg_stroke_style vh = *halo;
		                vg_stroke_style vm = *main;
		                vh.color = (vg_color){0.38f, 0.92f, 1.0f, 0.20f * fade * modern_brightness};
		                vm.color = (vg_color){0.44f, 0.97f, 1.0f, 0.58f * fade * modern_brightness};
		                vh.width_px *= 1.40f;
		                vm.width_px *= 1.25f;
		                vh.intensity *= (0.42f + fade * 0.48f) * modern_brightness;
		                vm.intensity *= (0.48f + fade * 0.56f) * modern_brightness;
		                vg_result vr = vg_draw_polyline(ctx, loop, WORMHOLE_VN, &vh, 1);
		                if (vr != VG_OK) {
		                    return vr;
		                }
		                vr = vg_draw_polyline(ctx, loop, WORMHOLE_VN, &vm, 1);
		                if (vr != VG_OK) {
		                    return vr;
		                }
		            }

		            for (int c = 0; c < WORMHOLE_COLS; ++c) {
		                vg_vec2 rail[WORMHOLE_ROWS];
                        const float cu = (float)c + rail_shift;
                        const int c0 = wrapi((int)floorf(cu), WORMHOLE_COLS);
                        const int c1 = wrapi(c0 + 1, WORMHOLE_COLS);
                        const float ct = cu - floorf(cu);
		                for (int j = 0; j < WORMHOLE_ROWS; ++j) {
		                    rail[j].x = cx + lerpf(wh.rail_rel_modern[c0][j].x, wh.rail_rel_modern[c1][j].x, ct);
		                    rail[j].y = cy + lerpf(wh.rail_rel_modern[c0][j].y, wh.rail_rel_modern[c1][j].y, ct);
		                }
		                const float fade = 0.90f;
		                vg_stroke_style rh = *halo;
		                vg_stroke_style rm = *main;
		                rh.color = (vg_color){0.38f, 0.92f, 1.0f, 0.20f * fade * modern_brightness};
		                rm.color = (vg_color){0.44f, 0.97f, 1.0f, 0.58f * fade * modern_brightness};
		                rh.width_px *= 1.40f;
		                rm.width_px *= 1.25f;
		                rh.intensity *= 0.42f * modern_brightness;
		                rm.intensity *= 0.48f * modern_brightness;

		                vg_result vr = vg_draw_polyline(ctx, rail, WORMHOLE_ROWS, &rh, 0);
		                if (vr != VG_OK) {
		                    return vr;
		                }
		                vr = vg_draw_polyline(ctx, rail, WORMHOLE_ROWS, &rm, 0);
		                if (vr != VG_OK) {
		                    return vr;
		                }
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
    const float su = ui_reference_scale(g->world_w, g->world_h);
    ship_pose p = {
        .x = g->player.b.x,
        .y = g->player.b.y,
        .fx = (g->player.facing_x < 0.0f) ? -1.0f : 1.0f,
        .s = 0.65f * su
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
    float intensity_scale = 1.0f + crt.flicker_amount * 0.30f * flicker_n;
    if (intensity_scale < 0.0f) {
        intensity_scale = 0.0f;
    }
    const vg_fill_style bg = make_fill(1.0f, (vg_color){0.0f, 0.0f, 0.0f, fade_alpha}, VG_BLEND_ALPHA);
    const vg_color starfield_color = (vg_color){0.62f, 0.86f, 1.0f, 1.0f};
    const vg_fill_style star_fill = make_fill(0.68f * intensity_scale, starfield_color, VG_BLEND_ADDITIVE);
    const vg_stroke_style ship_style = make_stroke(2.0f, 1.15f * intensity_scale, pal.ship, VG_BLEND_ALPHA);
    const vg_stroke_style bullet_style = make_stroke(2.6f, 1.0f * intensity_scale, (vg_color){1.0f, 0.9f, 0.55f, 1.0f}, VG_BLEND_ALPHA);
    const vg_stroke_style enemy_style = make_stroke(2.5f, 1.0f * intensity_scale, (vg_color){1.0f, 0.3f, 0.3f, 1.0f}, VG_BLEND_ALPHA);
    const vg_fill_style thruster_fill = make_fill(1.0f * intensity_scale, pal.thruster, VG_BLEND_ADDITIVE);

    const float main_line_width = 1.5f;
    const vg_color streak_color = (vg_color){0.56f, 0.80f, 1.0f, 0.28f};
    const vg_color streak_core_color = (vg_color){0.66f, 0.88f, 1.0f, 0.68f};
    const vg_stroke_style star_halo = make_stroke(
        (main_line_width * crt.beam_core_width_px + crt.beam_halo_width_px * 0.45f) * (1.0f + crt.bloom_radius_px * 0.03f),
        0.22f * crt.beam_intensity * intensity_scale * (1.0f + crt.bloom_strength * 0.14f),
        streak_color,
        VG_BLEND_ADDITIVE
    );
    const vg_stroke_style star_main = make_stroke(
        main_line_width * crt.beam_core_width_px * 0.70f,
        0.80f * crt.beam_intensity * intensity_scale,
        streak_core_color,
        VG_BLEND_ALPHA
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
        VG_BLEND_ALPHA
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
        VG_BLEND_ALPHA
    );

    if (metrics->show_acoustics) {
        const vg_fill_style bg_acoustics = make_fill(1.0f, (vg_color){0.0f, 0.0f, 0.0f, 1.0f}, VG_BLEND_ALPHA);
        vg_result r = vg_fill_rect(ctx, (vg_rect){0.0f, 0.0f, g->world_w, g->world_h}, &bg_acoustics);
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
    if (metrics->show_planetarium) {
        vg_result r = vg_fill_rect(ctx, (vg_rect){0.0f, 0.0f, g->world_w, g->world_h}, &bg);
        if (r != VG_OK) {
            return r;
        }
        r = draw_planetarium_ui(ctx, g->world_w, g->world_h, metrics, metrics->ui_time_s);
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

    if (level_uses_cylinder_render(g->level_style)) {
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

        for (size_t i = 0; i < MAX_ENEMY_BULLETS; ++i) {
            if (!g->enemy_bullets[i].active) {
                continue;
            }
            float d0 = 0.0f;
            float d1 = 0.0f;
            const vg_vec2 a = project_cylinder_point(g, g->enemy_bullets[i].b.x - 5.0f, g->enemy_bullets[i].b.y, &d0);
            const vg_vec2 b = project_cylinder_point(g, g->enemy_bullets[i].b.x + 5.0f, g->enemy_bullets[i].b.y, &d1);
            vg_stroke_style es = enemy_style;
            const float depth = 0.5f * (d0 + d1);
            es.width_px *= 0.42f + depth * 0.95f;
            es.intensity *= 0.32f + depth * 0.92f;
            es.color.a *= 0.30f + depth * 0.80f;
            const vg_vec2 bolt[] = {a, b};
            r = vg_draw_polyline(ctx, bolt, 2, &es, 0);
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
            const float ui = ui_reference_scale(g->world_w, g->world_h);
            const float go_size = 36.0f * ui;
            const float go_spacing = 2.2f * ui;
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
                    (g->world_w - vg_measure_text("PRESS R TO RESTART", 14.0f * ui, 1.2f * ui)) * 0.5f,
                    g->world_h * 0.52f
                },
                14.0f * ui,
                1.2f * ui,
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
        const float persistence_trail = 1.0f + (1.0f - crt.persistence_decay) * 2.8f;
        const float dt_safe = fmaxf(metrics->dt, 1e-4f);
        const float vx = (g->stars[i].prev_x - g->stars[i].x) / dt_safe;
        const float vy = (g->stars[i].prev_y - g->stars[i].y) / dt_safe;
        const float exposure_s = (1.0f / 60.0f) * (1.4f + 2.6f * u) * persistence_trail;
        float tx = g->stars[i].x + vx * exposure_s;
        const float ty = g->stars[i].y + vy * exposure_s;
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
        const vg_vec2 mid = {
            seg[0].x + (seg[1].x - seg[0].x) * 0.55f,
            seg[0].y + (seg[1].y - seg[0].y) * 0.55f
        };
        const vg_vec2 seg_tail[] = {seg[0], mid};
        const vg_vec2 seg_head[] = {mid, seg[1]};
        vg_stroke_style sh = star_halo;
        vg_stroke_style sm = star_main;
        sh.width_px *= 0.70f + u * 0.55f;
        sm.width_px *= 0.62f + u * 0.50f;
        sh.intensity *= 0.40f + u * 0.36f;
        sm.intensity *= 0.52f + u * 0.34f;
        vg_stroke_style sh_tail = sh;
        vg_stroke_style sm_tail = sm;
        /* Fade the back half faster so tails don't stay bright too long. */
        sh_tail.intensity *= 0.34f;
        sm_tail.intensity *= 0.40f;
        sh_tail.color.a *= 0.38f;
        sm_tail.color.a *= 0.44f;

        r = vg_draw_polyline(ctx, seg_tail, 2, &sh_tail, 0);
        if (r != VG_OK) {
            return r;
        }
        r = vg_draw_polyline(ctx, seg_tail, 2, &sm_tail, 0);
        if (r != VG_OK) {
            return r;
        }
        r = vg_draw_polyline(ctx, seg_head, 2, &sh, 0);
        if (r != VG_OK) {
            return r;
        }
        r = vg_draw_polyline(ctx, seg_head, 2, &sm, 0);
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

    for (size_t i = 0; i < MAX_ENEMY_BULLETS; ++i) {
        if (!g->enemy_bullets[i].active) {
            continue;
        }
        const float dx = (g->enemy_bullets[i].b.vx < 0.0f) ? -5.0f : 5.0f;
        const vg_vec2 bolt[] = {
            {g->enemy_bullets[i].b.x - dx, g->enemy_bullets[i].b.y},
            {g->enemy_bullets[i].b.x + dx, g->enemy_bullets[i].b.y}
        };
        vg_stroke_style es = enemy_style;
        es.width_px *= 0.80f;
        es.intensity *= 0.95f;
        r = vg_draw_polyline(ctx, bolt, 2, &es, 0);
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

    if (g->level_style == LEVEL_STYLE_HIGH_PLAINS_DRIFTER) {
        vg_stroke_style plains_halo = land_halo;
        vg_stroke_style plains_main = land_main;
        plains_halo.intensity *= 1.10f;
        plains_main.intensity *= 1.18f;
        plains_halo.width_px *= 1.08f;
        plains_main.width_px *= 1.04f;
        plains_main.color = (vg_color){pal.secondary.r, pal.secondary.g, pal.secondary.b, 0.92f};
        r = draw_high_plains_drifter_terrain(ctx, g, &plains_halo, &plains_main);
        if (r != VG_OK) {
            return r;
        }
    } else if (g->level_style == LEVEL_STYLE_HIGH_PLAINS_DRIFTER_2) {
        vg_stroke_style plains_halo = land_halo;
        vg_stroke_style plains_main = land_main;
        plains_halo.intensity *= 1.10f;
        plains_main.intensity *= 1.18f;
        plains_halo.width_px *= 1.06f;
        plains_main.width_px *= 1.02f;
        plains_main.color = (vg_color){pal.secondary.r, pal.secondary.g, pal.secondary.b, 0.92f};
        r = draw_high_plains_drifter_terrain_traditional(ctx, g, &plains_halo, &plains_main);
        if (r != VG_OK) {
            return r;
        }
    } else {
        /* Foreground vector landscape layers for depth/parallax. */
        vg_stroke_style land1_halo = land_halo;
        vg_stroke_style land1_main = land_main;
        if (g->level_style == LEVEL_STYLE_DEFENDER) {
            land1_halo.width_px *= 1.16f;
            land1_main.width_px *= 1.14f;
        }
        r = draw_parallax_landscape(ctx, g->world_w, g->world_h, g->camera_x, 1.20f, g->world_h * 0.18f, 22.0f, &land1_halo, &land1_main);
        if (r != VG_OK) {
            return r;
        }
        vg_stroke_style land2_halo = land_halo;
        vg_stroke_style land2_main = land_main;
        land2_halo.width_px *= 1.15f;
        land2_main.width_px *= 1.10f;
        if (g->level_style == LEVEL_STYLE_DEFENDER) {
            land2_halo.width_px *= 1.12f;
            land2_main.width_px *= 1.10f;
        }
        land2_halo.intensity *= 1.05f;
        land2_main.intensity *= 1.08f;
        land2_main.color = (vg_color){pal.secondary.r, pal.secondary.g, pal.secondary.b, 0.9f};
        r = draw_parallax_landscape(ctx, g->world_w, g->world_h, g->camera_x, 1.55f, g->world_h * 0.10f, 30.0f, &land2_halo, &land2_main);
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
        const float ui = ui_reference_scale(g->world_w, g->world_h);
        const float go_size = 36.0f * ui;
        const float go_spacing = 2.2f * ui;
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
                (g->world_w - vg_measure_text("PRESS R TO RESTART", 14.0f * ui, 1.2f * ui)) * 0.5f,
                g->world_h * 0.52f
            },
            14.0f * ui,
            1.2f * ui,
            &txt_halo,
            &txt_main
        );
        if (r != VG_OK) {
            return r;
        }
    }

    return VG_OK;
}
