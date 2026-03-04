#include "render.h"

#include "acoustics_ui_layout.h"
#include "leveldef.h"
#include "menu.h"
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
#define WORMHOLE_ROWS 33
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

static float repeatf(float v, float period);
static int wrapi(int i, int n);
static float lerpf(float a, float b, float t);
static float cylinder_period(const game_state* g);
static vg_vec2 project_cylinder_point(const game_state* g, float x, float y, float* depth01);
static vg_result draw_structure_prefab_tile(
    vg_context* ctx,
    int prefab_id,
    int layer,
    float bx,
    float by,
    float unit_w,
    float unit_h,
    int rotation_quadrants,
    int flip_x,
    int flip_y,
    const vg_stroke_style* st,
    const vg_fill_style* fill
);

static vg_vec2 structure_map_point(
    float lx,
    float ly,
    float bx,
    float by,
    float w,
    float h,
    int rotation_quadrants,
    int flip_x,
    int flip_y
);

static vg_stroke_style make_stroke(float width, float intensity, vg_color color, vg_blend_mode blend) {
    vg_stroke_style s;
    s.width_px = width;
    s.intensity = intensity;
    s.color = color;
    s.cap = VG_LINE_CAP_ROUND;
    s.join = VG_LINE_JOIN_ROUND;
    s.miter_limit = 4.0f;
    s.blend = blend;
    s.stencil = vg_stencil_state_disabled();
    return s;
}

static vg_fill_style make_fill(float intensity, vg_color color, vg_blend_mode blend) {
    vg_fill_style f;
    f.intensity = intensity;
    f.color = color;
    f.blend = blend;
    f.stencil = vg_stencil_state_disabled();
    return f;
}

static float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static float hash01_u32(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return (float)(x & 0x00ffffffU) / 16777215.0f;
}

static int clampi(int v, int lo, int hi) {
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
    /* Batch contiguous visible segments to reduce draw-call count. */
    enum { FADE_BUCKETS = 8, MAX_BATCH_EDGES = 512 };
    uint8_t edge_bucket[MAX_BATCH_EDGES];
    const int edge_count = count - 1;
    if (edge_count > MAX_BATCH_EDGES) {
        for (int i = 0; i < edge_count; ++i) {
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
        return VG_OK;
    }
    for (int i = 0; i < edge_count; ++i) {
        const float f = facing_soft(0.5f * (facing01[i] + facing01[i + 1]), cutoff01);
        int b = (int)floorf(f * (float)FADE_BUCKETS + 0.5f);
        if (b < 0) b = 0;
        if (b > FADE_BUCKETS) b = FADE_BUCKETS;
        edge_bucket[i] = (uint8_t)b;
    }
    int i = 0;
    while (i < edge_count) {
        const uint8_t b = edge_bucket[i];
        if (b == 0u) {
            ++i;
            continue;
        }
        const int start = i;
        ++i;
        while (i < edge_count && edge_bucket[i] == b) {
            ++i;
        }
        const int pt_count = (i - start) + 1;
        if (pt_count < 2) {
            continue;
        }
        vg_stroke_style s = *base;
        const float fade = (float)b / (float)FADE_BUCKETS;
        s.intensity *= fade;
        s.color.a *= fade;
        vg_result r = vg_draw_polyline(ctx, &pts[start], pt_count, &s, 0);
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
            const vg_vec2 a = pts[count - 1];
            const vg_vec2 b = pts[0];
            if (fabsf(a.x - b.x) > 1e-5f || fabsf(a.y - b.y) > 1e-5f) {
                const vg_vec2 seg[2] = {a, b};
                vg_result r = vg_draw_polyline(ctx, seg, 2, &s, 0);
                if (r != VG_OK) {
                    return r;
                }
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
    const float rx_throat = world_w * 0.024f;
    const float ry_ratio = 0.17f;
    const float flare_s = 3.9f; /* larger = longer/narrower, still curved */
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
        k = powf(k, 1.45f);
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
        /* Smooth hemisphere transition through center to avoid a visible spacing seam. */
        const float hemi = -tanhf(sy * 7.0f);

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
            const float hemi = -tanhf(sy * 7.0f);
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

static size_t wormhole_emit_segment(
    wormhole_line_vertex* out,
    size_t out_cap,
    size_t count,
    float ax,
    float ay,
    float bx,
    float by,
    float az,
    float bz,
    float fade
) {
    if (!out || count + 2u > out_cap) {
        return count;
    }
    out[count + 0u] = (wormhole_line_vertex){ax, ay, az, fade};
    out[count + 1u] = (wormhole_line_vertex){bx, by, bz, fade};
    return count + 2u;
}

static float wormhole_phase_depth(float phase, float hemi) {
    /* Modern wormhole flips angular hemisphere between top/bottom halves. */
    const float s = sinf(phase) * hemi;
    return clampf(0.52f - 0.28f * s, 0.04f, 0.96f);
}

static float wormhole_row_sy(int j) {
    const float tj = (float)j / (float)(WORMHOLE_ROWS - 1);
    return tj * 2.0f - 1.0f;
}

static float wormhole_row_hemi_smooth(int j) {
    const float sy = wormhole_row_sy(j);
    /* Keep top behavior; flip bottom-half orientation to align center column winding. */
    float h = -tanhf(sy * 6.0f);
    if (sy < 0.0f) {
        h = -h;
    }
    return h;
}

size_t render_build_event_horizon_gpu_lines(const game_state* g, wormhole_line_vertex* out, size_t out_cap) {
    if (!g || !out || out_cap == 0u) {
        return 0u;
    }
    if (g->level_style != LEVEL_STYLE_EVENT_HORIZON) {
        return 0u;
    }

    static wormhole_cache wh;
    wormhole_cache_ensure(&wh, g->world_w, g->world_h);

    const float period = cylinder_period(g);
    const vg_vec2 vc = project_cylinder_point(g, g->camera_x, g->world_h * 0.50f, NULL);
    const float cx = vc.x;
    const float cy = vc.y;
    const float phase_turns = repeatf((-1.0f) * g->player.b.x / fmaxf(period * 0.85f, 1.0f), 1.0f);
    const float loop_shift_modern = phase_turns * (float)WORMHOLE_VN;
    const float rail_shift = phase_turns * (float)WORMHOLE_COLS;

    size_t count = 0u;
    for (int j = 0; j < WORMHOLE_ROWS; ++j) {
        const float fade = wh.row_fade[j];
        const float hemi = wormhole_row_hemi_smooth(j);
        vg_vec2 loop[WORMHOLE_VN];
        for (int i = 0; i < WORMHOLE_VN; ++i) {
            const float u = (float)i + loop_shift_modern;
            const int i0 = wrapi((int)floorf(u), WORMHOLE_VN);
            const int i1 = wrapi(i0 + 1, WORMHOLE_VN);
            const float t = u - floorf(u);
            loop[i].x = cx + lerpf(wh.loop_rel_modern[j][i0].x, wh.loop_rel_modern[j][i1].x, t);
            loop[i].y = cy + lerpf(wh.loop_rel_modern[j][i0].y, wh.loop_rel_modern[j][i1].y, t);
        }
        for (int i = 0; i < WORMHOLE_VN; ++i) {
            const int i1 = wrapi(i + 1, WORMHOLE_VN);
            const float u0 = (float)i + loop_shift_modern;
            const float u1 = (float)(i + 1) + loop_shift_modern;
            const float phase0 = (u0 / (float)WORMHOLE_VN) * 6.28318530718f;
            const float phase1 = (u1 / (float)WORMHOLE_VN) * 6.28318530718f;
            const float z0 = clampf(wormhole_phase_depth(phase0, hemi) - 0.0025f, 0.0f, 1.0f);
            const float z1 = clampf(wormhole_phase_depth(phase1, hemi) - 0.0025f, 0.0f, 1.0f);
            count = wormhole_emit_segment(out, out_cap, count, loop[i].x, loop[i].y, loop[i1].x, loop[i1].y, z0, z1, fade);
            if (count + 1u >= out_cap) {
                return count;
            }
        }
    }

    /* Ensure a visible waist ring for even row counts (no exact sy=0 row). */
    if ((WORMHOLE_ROWS % 2) == 0) {
        const int j0 = (WORMHOLE_ROWS / 2) - 1;
        const int j1 = (WORMHOLE_ROWS / 2);
        const float fade = 0.5f * (wh.row_fade[j0] + wh.row_fade[j1]);
        vg_vec2 loop[WORMHOLE_VN];
        for (int i = 0; i < WORMHOLE_VN; ++i) {
            const float u = (float)i + loop_shift_modern;
            const int i0 = wrapi((int)floorf(u), WORMHOLE_VN);
            const int i1 = wrapi(i0 + 1, WORMHOLE_VN);
            const float t = u - floorf(u);
            const float x0 = lerpf(wh.loop_rel_modern[j0][i0].x, wh.loop_rel_modern[j0][i1].x, t);
            const float y0 = lerpf(wh.loop_rel_modern[j0][i0].y, wh.loop_rel_modern[j0][i1].y, t);
            const float x1 = lerpf(wh.loop_rel_modern[j1][i0].x, wh.loop_rel_modern[j1][i1].x, t);
            const float y1 = lerpf(wh.loop_rel_modern[j1][i0].y, wh.loop_rel_modern[j1][i1].y, t);
            loop[i].x = cx + 0.5f * (x0 + x1);
            loop[i].y = cy + 0.5f * (y0 + y1);
        }
        for (int i = 0; i < WORMHOLE_VN; ++i) {
            const int i1 = wrapi(i + 1, WORMHOLE_VN);
            const float u0 = (float)i + loop_shift_modern;
            const float u1 = (float)(i + 1) + loop_shift_modern;
            const float phase0 = (u0 / (float)WORMHOLE_VN) * 6.28318530718f;
            const float phase1 = (u1 / (float)WORMHOLE_VN) * 6.28318530718f;
            const float z0 = clampf(wormhole_phase_depth(phase0, 0.0f) - 0.0035f, 0.0f, 1.0f);
            const float z1 = clampf(wormhole_phase_depth(phase1, 0.0f) - 0.0035f, 0.0f, 1.0f);
            count = wormhole_emit_segment(out, out_cap, count, loop[i].x, loop[i].y, loop[i1].x, loop[i1].y, z0, z1, fade);
            if (count + 1u >= out_cap) {
                return count;
            }
        }
    }

    for (int c = 0; c < WORMHOLE_COLS; ++c) {
        const float cu = (float)c + rail_shift;
        const int c0 = wrapi((int)floorf(cu), WORMHOLE_COLS);
        const int c1 = wrapi(c0 + 1, WORMHOLE_COLS);
        const float ct = cu - floorf(cu);
        vg_vec2 rail[WORMHOLE_ROWS];
        for (int j = 0; j < WORMHOLE_ROWS; ++j) {
            rail[j].x = cx + lerpf(wh.rail_rel_modern[c0][j].x, wh.rail_rel_modern[c1][j].x, ct);
            rail[j].y = cy + lerpf(wh.rail_rel_modern[c0][j].y, wh.rail_rel_modern[c1][j].y, ct);
        }
        for (int j = 0; j + 1 < WORMHOLE_ROWS; ++j) {
            const float phase = (cu / (float)WORMHOLE_COLS) * 6.28318530718f;
            const float hemi0 = wormhole_row_hemi_smooth(j);
            const float hemi1 = wormhole_row_hemi_smooth(j + 1);
            const float z0 = clampf(wormhole_phase_depth(phase, hemi0) - 0.0025f, 0.0f, 1.0f);
            const float z1 = clampf(wormhole_phase_depth(phase, hemi1) - 0.0025f, 0.0f, 1.0f);
            count = wormhole_emit_segment(out, out_cap, count, rail[j].x, rail[j].y, rail[j + 1].x, rail[j + 1].y, z0, z1, 0.90f);
            if (count + 1u >= out_cap) {
                return count;
            }
        }
    }
    return count;
}

size_t render_build_event_horizon_gpu_tris(const game_state* g, wormhole_line_vertex* out, size_t out_cap) {
    if (!g || !out || out_cap == 0u) {
        return 0u;
    }
    if (g->level_style != LEVEL_STYLE_EVENT_HORIZON) {
        return 0u;
    }

    static wormhole_cache wh;
    wormhole_cache_ensure(&wh, g->world_w, g->world_h);

    const float period = cylinder_period(g);
    const vg_vec2 vc = project_cylinder_point(g, g->camera_x, g->world_h * 0.50f, NULL);
    const float cx = vc.x;
    const float cy = vc.y;
    const float phase_turns = repeatf((-1.0f) * g->player.b.x / fmaxf(period * 0.85f, 1.0f), 1.0f);
    const float rail_shift = phase_turns * (float)WORMHOLE_COLS;

    vg_vec2 p[WORMHOLE_COLS][WORMHOLE_ROWS];
    float z[WORMHOLE_COLS][WORMHOLE_ROWS];
    for (int c = 0; c < WORMHOLE_COLS; ++c) {
        const float cu = (float)c + rail_shift;
        const int c0 = wrapi((int)floorf(cu), WORMHOLE_COLS);
        const int c1 = wrapi(c0 + 1, WORMHOLE_COLS);
        const float ct = cu - floorf(cu);
        const float phase = (cu / (float)WORMHOLE_COLS) * 6.28318530718f;
        for (int j = 0; j < WORMHOLE_ROWS; ++j) {
            const float hemi = wormhole_row_hemi_smooth(j);
            p[c][j].x = cx + lerpf(wh.rail_rel_modern[c0][j].x, wh.rail_rel_modern[c1][j].x, ct);
            p[c][j].y = cy + lerpf(wh.rail_rel_modern[c0][j].y, wh.rail_rel_modern[c1][j].y, ct);
            z[c][j] = wormhole_phase_depth(phase, hemi);
        }
    }

    size_t count = 0u;
    for (int c = 0; c < WORMHOLE_COLS; ++c) {
        const int cn = wrapi(c + 1, WORMHOLE_COLS);
        for (int j = 0; j + 1 < WORMHOLE_ROWS; ++j) {
            const wormhole_line_vertex a = {p[c][j].x, p[c][j].y, z[c][j], 1.0f};
            const wormhole_line_vertex b = {p[cn][j].x, p[cn][j].y, z[cn][j], 1.0f};
            const wormhole_line_vertex d = {p[cn][j + 1].x, p[cn][j + 1].y, z[cn][j + 1], 1.0f};
            const wormhole_line_vertex e = {p[c][j + 1].x, p[c][j + 1].y, z[c][j + 1], 1.0f};
            if (count + 6u > out_cap) {
                return count;
            }
            out[count++] = a;
            out[count++] = b;
            out[count++] = d;
            out[count++] = a;
            out[count++] = d;
            out[count++] = e;
        }
    }
    return count;
}

size_t render_build_enemy_radar_gpu_lines(const game_state* g, wormhole_line_vertex* out, size_t out_cap) {
    if (!g || !out || out_cap == 0u) {
        return 0u;
    }
    if (g->level_style != LEVEL_STYLE_ENEMY_RADAR) {
        return 0u;
    }

    enum { N = 96 };
    const float period = cylinder_period(g);
    const float ring_y_top = g->world_h * 0.06f;
    const float ring_y_bottom = g->world_h * 0.86f;
    const float depth_eps = 0.0018f;
    size_t count = 0u;

    /* Bottom cylinder ring. */
    vg_vec2 bottom[N];
    float bottom_depth[N];
    for (int i = 0; i < N; ++i) {
        const float u = (float)i / (float)(N - 1);
        const float xw = g->camera_x + (u - 0.5f) * period;
        bottom[i] = project_cylinder_point(g, xw, ring_y_bottom, &bottom_depth[i]);
    }
    for (int i = 0; i < N - 1; ++i) {
        const float z0 = clampf(bottom_depth[i] - depth_eps, 0.0f, 1.0f);
        const float z1 = clampf(bottom_depth[i + 1] - depth_eps, 0.0f, 1.0f);
        count = wormhole_emit_segment(
            out, out_cap, count,
            bottom[i].x, bottom[i].y,
            bottom[i + 1].x, bottom[i + 1].y,
            z0, z1, 0.92f
        );
        if (count + 1u >= out_cap) {
            return count;
        }
    }

    /* Radar plate edge built from top ring projection, then scaled out. */
    vg_vec2 edge[N];
    vg_vec2 radar_edge[N];
    float edge_depth[N];
    float cx = 0.0f;
    float cy = 0.0f;
    for (int i = 0; i < N; ++i) {
        const float u = (float)i / (float)(N - 1);
        const float xw = g->camera_x + (u - 0.5f) * period;
        edge[i] = project_cylinder_point(g, xw, ring_y_top, &edge_depth[i]);
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

    /* Concentric loops on the radar plate. */
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
            const float z0 = clampf(loop_depth[i] - depth_eps * 2.0f, 0.0f, 1.0f);
            const float z1 = clampf(loop_depth[i + 1] - depth_eps * 2.0f, 0.0f, 1.0f);
            count = wormhole_emit_segment(
                out, out_cap, count,
                loop[i].x, loop[i].y,
                loop[i + 1].x, loop[i + 1].y,
                z0, z1, 0.88f
            );
            if (count + 1u >= out_cap) {
                return count;
            }
        }
    }

    /* Radial spokes. */
    for (int s = 0; s < 20; ++s) {
        const float idxf = (float)(s * (N - 1)) / 20.0f + radar_shift;
        const int i0 = wrapi((int)floorf(idxf), N - 1);
        const int i1 = wrapi(i0 + 1, N - 1);
        const float t = idxf - floorf(idxf);
        const vg_vec2 tip = {
            lerpf(radar_edge[i0].x, radar_edge[i1].x, t),
            lerpf(radar_edge[i0].y, radar_edge[i1].y, t)
        };
        const float depth = lerpf(edge_depth[i0], edge_depth[i1], t);
        const float z = clampf(depth - depth_eps * 3.0f, 0.0f, 1.0f);
        count = wormhole_emit_segment(out, out_cap, count, cx, cy, tip.x, tip.y, z, z, 0.72f);
        if (count + 1u >= out_cap) {
            return count;
        }
    }

    /* Sweep trail: denser and smoother to get a stronger persistence look. */
    {
        const float sweep = fmodf(g->t * 1.6f, 6.28318530718f);
        for (int t = 13; t >= 0; --t) {
            const float lag = (float)t * 0.10f;
            const float a = sweep - lag;
            float u = fmodf(a / 6.28318530718f + phase_turns, 1.0f);
            if (u < 0.0f) {
                u += 1.0f;
            }
            const float fi = u * (float)(N - 1);
            const int i0 = wrapi((int)floorf(fi), N - 1);
            const int i1 = wrapi(i0 + 1, N - 1);
            const float ft = fi - floorf(fi);
            const vg_vec2 tip = {
                radar_edge[i0].x + (radar_edge[i1].x - radar_edge[i0].x) * ft,
                radar_edge[i0].y + (radar_edge[i1].y - radar_edge[i0].y) * ft
            };
            const float tip_depth = edge_depth[i0] + (edge_depth[i1] - edge_depth[i0]) * ft;
            const float trail = expf(-(float)t * 0.23f);
            const float z = clampf(tip_depth - depth_eps * 4.0f, 0.0f, 1.0f);
            const float fade = (0.12f + 0.88f * trail) * (0.88f + 0.12f * trail);
            count = wormhole_emit_segment(out, out_cap, count, cx, cy, tip.x, tip.y, z, z, fade);
            if (count + 1u >= out_cap) {
                return count;
            }
        }
    }

    return count;
}

size_t render_build_enemy_radar_gpu_tris(const game_state* g, wormhole_line_vertex* out, size_t out_cap) {
    if (!g || !out || out_cap == 0u) {
        return 0u;
    }
    if (g->level_style != LEVEL_STYLE_ENEMY_RADAR) {
        return 0u;
    }

    enum { N = 96 };
    const float period = cylinder_period(g);
    const float ring_y_top = g->world_h * 0.06f;
    const float depth_eps = 0.0030f;
    size_t count = 0u;

    vg_vec2 edge[N];
    vg_vec2 radar_edge[N];
    float edge_depth[N];
    float cx = 0.0f;
    float cy = 0.0f;
    for (int i = 0; i < N; ++i) {
        const float u = (float)i / (float)(N - 1);
        const float xw = g->camera_x + (u - 0.5f) * period;
        edge[i] = project_cylinder_point(g, xw, ring_y_top, &edge_depth[i]);
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
    const float sweep = fmodf(g->t * 1.6f, 6.28318530718f);

    for (int t = 13; t >= 0; --t) {
        const float lag = (float)t * 0.10f;
        const float a = sweep - lag;
        float u = fmodf(a / 6.28318530718f + phase_turns, 1.0f);
        if (u < 0.0f) {
            u += 1.0f;
        }
        const float fi = u * (float)(N - 1);
        const float trail = expf(-(float)t * 0.22f);
        const float du = (0.010f + 0.022f * trail) * (float)(N - 1);
        const float f0 = fi - du;
        const float f1 = fi + du;

        const int a0 = wrapi((int)floorf(f0), N - 1);
        const int a1 = wrapi(a0 + 1, N - 1);
        const float at = f0 - floorf(f0);
        const int b0 = wrapi((int)floorf(f1), N - 1);
        const int b1 = wrapi(b0 + 1, N - 1);
        const float bt = f1 - floorf(f1);
        const int c0 = wrapi((int)floorf(fi), N - 1);
        const int c1 = wrapi(c0 + 1, N - 1);
        const float ct = fi - floorf(fi);

        const vg_vec2 p0 = {
            radar_edge[a0].x + (radar_edge[a1].x - radar_edge[a0].x) * at,
            radar_edge[a0].y + (radar_edge[a1].y - radar_edge[a0].y) * at
        };
        const vg_vec2 p1 = {
            radar_edge[b0].x + (radar_edge[b1].x - radar_edge[b0].x) * bt,
            radar_edge[b0].y + (radar_edge[b1].y - radar_edge[b0].y) * bt
        };
        const float z_tip = clampf(edge_depth[c0] + (edge_depth[c1] - edge_depth[c0]) * ct - depth_eps, 0.0f, 1.0f);
        const float fade = 0.05f + trail * 0.70f;

        if (count + 3u > out_cap) {
            return count;
        }
        out[count++] = (wormhole_line_vertex){cx, cy, z_tip, fade};
        out[count++] = (wormhole_line_vertex){p0.x, p0.y, z_tip, fade};
        out[count++] = (wormhole_line_vertex){p1.x, p1.y, z_tip, fade};
    }

    return count;
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

static int level_uses_cylinder_render(const game_state* g) {
    return g && g->render_style == LEVEL_RENDER_CYLINDER;
}

static int level_draws_star_background(const game_state* g) {
    const leveldef_level* lvl;
    if (!g) {
        return 1;
    }
    lvl = game_current_leveldef(g);
    if (!lvl) {
        return (g->render_style != LEVEL_RENDER_BLANK) ? 1 : 0;
    }
    if (lvl->background_style == LEVELDEF_BACKGROUND_NONE) {
        return 0;
    }
    if (lvl->background_style == LEVELDEF_BACKGROUND_STARS) {
        return 1;
    }
    return 0;
}

static int level_background_mask_style(const game_state* g) {
    const leveldef_level* lvl;
    if (!g) {
        return LEVELDEF_BG_MASK_NONE;
    }
    lvl = game_current_leveldef(g);
    if (!lvl) {
        return LEVELDEF_BG_MASK_NONE;
    }
    return clampi(lvl->background_mask_style, LEVELDEF_BG_MASK_NONE, LEVELDEF_BG_MASK_WINDOWS);
}

static int point_in_window_trapezoid(float px, float py, float cx, float cy, float width, float height, int flip_vertical) {
    const float hh = height * 0.5f;
    const float dy = py - cy;
    if (fabsf(dy) > hh) {
        return 0;
    }
    /* Match draw order: frame uses +Y as top edge (half_top), -Y as bottom edge (half_bottom). */
    const float t = (hh - dy) / fmaxf(height, 1.0e-5f);
    const float half_top = width * (flip_vertical ? 0.44f : 0.50f);
    const float half_bottom = width * (flip_vertical ? 0.50f : 0.44f);
    const float half_w = lerpf(half_top, half_bottom, t);
    return fabsf(px - cx) <= half_w;
}

static vg_result draw_window_frame_scifi(
    vg_context* ctx,
    float cx,
    float cy,
    float width,
    float height,
    int flip_vertical,
    float inset_px,
    const vg_stroke_style* style
) {
    if (!ctx || !style) {
        return VG_OK;
    }
    if (width <= 2.0f || height <= 2.0f) {
        return VG_OK;
    }
    const float ww = fmaxf(2.0f, width - inset_px * 2.0f);
    const float wh = fmaxf(2.0f, height - inset_px * 2.0f);
    const float hh = wh * 0.5f;
    const float half_top = ww * (flip_vertical ? 0.44f : 0.50f);
    const float half_bottom = ww * (flip_vertical ? 0.50f : 0.44f);
    const float notch = fmaxf(2.0f, wh * 0.11f);
    const float bevel = fmaxf(2.0f, wh * 0.16f);
    const vg_vec2 frame[15] = {
        {cx - half_top,            cy + hh},
        {cx - half_top * 0.52f,    cy + hh},
        {cx - half_top * 0.44f,    cy + hh + notch},
        {cx + half_top * 0.44f,    cy + hh + notch},
        {cx + half_top * 0.52f,    cy + hh},
        {cx + half_top,            cy + hh},
        {cx + half_bottom,         cy - hh + bevel},
        {cx + half_bottom,         cy - hh},
        {cx + half_bottom * 0.10f, cy - hh},
        {cx + half_bottom * 0.02f, cy - hh - notch * 0.80f},
        {cx - half_bottom * 0.18f, cy - hh - notch * 0.80f},
        {cx - half_bottom * 0.26f, cy - hh},
        {cx - half_bottom * 0.88f, cy - hh},
        {cx - half_bottom,         cy - hh + bevel},
        {cx - half_top,            cy + hh}
    };
    return vg_draw_polyline(ctx, frame, 15, style, 0);
}

static vg_result draw_window_frame_accents(
    vg_context* ctx,
    float cx,
    float cy,
    float width,
    float height,
    int flip_vertical,
    float inset_px,
    const vg_stroke_style* style
) {
    if (!ctx || !style) {
        return VG_OK;
    }
    const float ww = fmaxf(2.0f, width - inset_px * 2.0f);
    const float wh = fmaxf(2.0f, height - inset_px * 2.0f);
    const float hh = wh * 0.5f;
    const float half_top = ww * (flip_vertical ? 0.44f : 0.50f);
    const float half_bottom = ww * (flip_vertical ? 0.50f : 0.44f);
    const float notch = fmaxf(2.0f, wh * 0.11f);
    const float top_notch_l = cx - half_top * 0.44f;
    const float top_notch_r = cx + half_top * 0.44f;
    const float bottom_notch_l = cx - half_bottom * 0.18f;
    const float bottom_notch_r = cx + half_bottom * 0.02f;
    const float top_y = cy + hh + notch * 0.70f;
    const float bottom_y = cy - hh - notch * 0.70f;
    const vg_vec2 top_l[2] = {
        {cx - half_top, top_y},
        {cx - half_top * 0.54f, top_y}
    };
    const vg_vec2 top_r[2] = {
        {cx + half_top * 0.54f, top_y},
        {cx + half_top, top_y}
    };
    const vg_vec2 top_mid[2] = {
        {top_notch_l + (top_notch_r - top_notch_l) * 0.10f, top_y},
        {top_notch_l + (top_notch_r - top_notch_l) * 0.90f, top_y}
    };
    const vg_vec2 bottom_l[2] = {
        {cx - half_bottom * 0.90f, bottom_y},
        {bottom_notch_l - (half_bottom * 0.10f), bottom_y}
    };
    const vg_vec2 bottom_r[2] = {
        {bottom_notch_r + (half_bottom * 0.10f), bottom_y},
        {cx + half_bottom * 0.90f, bottom_y}
    };
    vg_result r = vg_draw_polyline(ctx, top_l, 2, style, 0);
    if (r != VG_OK) return r;
    r = vg_draw_polyline(ctx, top_mid, 2, style, 0);
    if (r != VG_OK) return r;
    r = vg_draw_polyline(ctx, top_r, 2, style, 0);
    if (r != VG_OK) return r;
    r = vg_draw_polyline(ctx, bottom_l, 2, style, 0);
    if (r != VG_OK) return r;
    r = vg_draw_polyline(ctx, bottom_r, 2, style, 0);
    return r;
}

static vg_result draw_background_window_mask_overlays(
    vg_context* ctx,
    const game_state* g,
    const vg_stroke_style* halo,
    const vg_stroke_style* main
) {
    if (!ctx || !g || !halo || !main) {
        return VG_OK;
    }
    if (level_background_mask_style(g) != LEVELDEF_BG_MASK_WINDOWS) {
        return VG_OK;
    }
    const leveldef_level* lvl = game_current_leveldef(g);
    if (!lvl || lvl->window_mask_count <= 0) {
        return VG_OK;
    }

    vg_stroke_style sh = *halo;
    vg_stroke_style sm = *main;
    sh.intensity *= 0.46f;
    sm.intensity *= 0.56f;
    sh.color.a *= 0.46f;
    sm.color.a *= 0.62f;
    sh.width_px *= 1.12f;
    sm.width_px *= 1.06f;
    vg_stroke_style ih = sh;
    vg_stroke_style im = sm;
    ih.intensity *= 0.34f;
    im.intensity *= 0.42f;
    ih.color.a *= 0.40f;
    im.color.a *= 0.48f;
    ih.color.r *= 0.58f;
    ih.color.g *= 0.58f;
    ih.color.b *= 0.58f;
    im.color.r *= 0.62f;
    im.color.g *= 0.62f;
    im.color.b *= 0.62f;
    ih.width_px *= 0.92f;
    im.width_px *= 0.88f;

    for (int i = 0; i < lvl->window_mask_count && i < LEVELDEF_MAX_WINDOW_MASKS; ++i) {
        const leveldef_window_mask* wm = &lvl->window_masks[i];
        const float center_world_x = wm->anchor_x01 * g->world_w;
        const float cx = center_world_x - g->camera_x + g->world_w * 0.5f;
        const float cy = wm->anchor_y01 * g->world_h;
        const float ww = wm->width_h01 * g->world_w;
        const float wh = wm->height_v01 * g->world_h;
        const float hh = wh * 0.5f;
        const int flip_vertical = (wm->flip_vertical != 0) ? 1 : 0;
        if (cx < -ww || cx > g->world_w + ww || ww <= 0.0f || wh <= 0.0f) {
            continue;
        }
        const float outer_pad = fmaxf(2.0f, fminf(ww, wh) * 0.065f);
        const float inner_inset = fmaxf(2.0f, fminf(ww, wh) * 0.060f);
        const float frame_ww = ww + outer_pad * 2.0f;
        const float frame_wh = wh + outer_pad * 2.0f;
        vg_result r = draw_window_frame_scifi(ctx, cx, cy, frame_ww, frame_wh, flip_vertical, 0.0f, &sh);
        if (r != VG_OK) {
            return r;
        }
        r = draw_window_frame_scifi(ctx, cx, cy, frame_ww, frame_wh, flip_vertical, 0.0f, &sm);
        if (r != VG_OK) {
            return r;
        }
        r = draw_window_frame_scifi(ctx, cx, cy, frame_ww, frame_wh, flip_vertical, inner_inset, &ih);
        if (r != VG_OK) {
            return r;
        }
        r = draw_window_frame_scifi(ctx, cx, cy, frame_ww, frame_wh, flip_vertical, inner_inset, &im);
        if (r != VG_OK) {
            return r;
        }
        {
            vg_stroke_style ah = sh;
            vg_stroke_style am = sm;
            ah.width_px *= 1.85f;
            am.width_px *= 1.65f;
            ah.intensity *= 1.24f;
            am.intensity *= 1.20f;
            ah.color.a *= 0.88f;
            am.color.a *= 0.84f;
            r = draw_window_frame_accents(ctx, cx, cy, frame_ww, frame_wh, flip_vertical, 0.0f, &ah);
            if (r != VG_OK) {
                return r;
            }
            r = draw_window_frame_accents(ctx, cx, cy, frame_ww, frame_wh, flip_vertical, 0.0f, &am);
        }
        if (r != VG_OK) {
            return r;
        }
    }

    return VG_OK;
}

static int star_visible_with_mask(const game_state* g, float sx, float sy) {
    const int mask = level_background_mask_style(g);
    if (!g || mask == LEVELDEF_BG_MASK_NONE) {
        return 1;
    }
    if (mask == LEVELDEF_BG_MASK_TERRAIN) {
        if (g->render_style == LEVEL_RENDER_DRIFTER ||
            g->render_style == LEVEL_RENDER_DRIFTER_SHADED ||
            g->render_style == LEVEL_RENDER_DEFENDER) {
            return (sy >= g->world_h * 0.40f) ? 1 : 0;
        }
        return 1;
    }
    if (mask == LEVELDEF_BG_MASK_WINDOWS) {
        const leveldef_level* lvl = game_current_leveldef(g);
        if (!lvl || lvl->window_mask_count <= 0) {
            return 0;
        }
        for (int i = 0; i < lvl->window_mask_count && i < LEVELDEF_MAX_WINDOW_MASKS; ++i) {
            const leveldef_window_mask* wm = &lvl->window_masks[i];
            const float center_world_x = wm->anchor_x01 * g->world_w;
            const float center_screen_x = center_world_x - g->camera_x + g->world_w * 0.5f;
            const float center_screen_y = wm->anchor_y01 * g->world_h;
            const float ww = wm->width_h01 * g->world_w;
            const float wh = wm->height_v01 * g->world_h;
            const float clip_inset = fmaxf(1.0f, fminf(ww, wh) * 0.04f);
            const float clip_ww = fmaxf(2.0f, ww - 2.0f * clip_inset);
            const float clip_wh = fmaxf(2.0f, wh - 2.0f * clip_inset);
            if (point_in_window_trapezoid(sx, sy, center_screen_x, center_screen_y, clip_ww, clip_wh, wm->flip_vertical)) {
                return 1;
            }
        }
        return 0;
    }
    return 1;
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
            p.primary = (vg_color){0.08f, 0.66f, 0.18f, 0.95f};
            p.primary_dim = (vg_color){0.03f, 0.52f, 0.12f, 0.40f};
            p.secondary = (vg_color){0.13f, 0.66f, 0.25f, 1.0f};
            p.haze = (vg_color){0.008f, 0.050f, 0.020f, 0.55f};
            p.star = (vg_color){0.11f, 0.60f, 0.20f, 1.0f};
            p.ship = (vg_color){0.09f, 0.66f, 0.17f, 1.0f};
            p.thruster = (vg_color){0.18f, 0.66f, 0.30f, 0.92f};
            break;
    }
    return p;
}

static vg_result draw_searchlights(
    vg_context* ctx,
    const game_state* g,
    const palette_theme* pal,
    float intensity_scale,
    const vg_stroke_style* land_halo,
    const vg_stroke_style* land_main
) {
    if (!ctx || !g || !pal || g->searchlight_count <= 0) {
        return VG_OK;
    }
    const int can_stencil = (vg_stencil_clear(ctx, 0u) == VG_OK);
    const int tip_slices = 28;
    for (int i = 0; i < g->searchlight_count && i < MAX_SEARCHLIGHTS; ++i) {
        const searchlight* sl = &g->searchlights[i];
        if (!sl->active || sl->length <= 1.0f) {
            continue;
        }
        if (can_stencil) {
            vg_result sr = vg_stencil_clear(ctx, 0u);
            if (sr != VG_OK) {
                return sr;
            }
            /* Mark emitter footprint in stencil so beam pixels can be rejected there. */
            const float rr = fmaxf(sl->source_radius, 2.0f);
            vg_fill_style src_mask = make_fill(0.0f, (vg_color){0.0f, 0.0f, 0.0f, 0.0f}, VG_BLEND_ALPHA);
            src_mask.stencil = vg_stencil_state_make_write_replace(1u, 0xffu);
            if (sl->source_type == SEARCHLIGHT_SOURCE_ORB) {
                vg_result sr2 = vg_fill_circle(ctx, (vg_vec2){sl->origin_x, sl->origin_y}, rr, &src_mask, 18);
                if (sr2 != VG_OK) {
                    return sr2;
                }
            } else {
                enum { DOME_SEG = 18 };
                vg_vec2 dome_fill[DOME_SEG + 2];
                const float mask_rr = rr + 2.0f; /* Slight overscan to suppress seam glow at emitter origin. */
                dome_fill[0] = (vg_vec2){sl->origin_x, sl->origin_y};
                for (int k = 0; k <= DOME_SEG; ++k) {
                    const float u = (float)k / (float)DOME_SEG;
                    const float a = u * 3.14159265359f;
                    dome_fill[k + 1] = (vg_vec2){
                        sl->origin_x + cosf(a) * mask_rr,
                        sl->origin_y + sinf(a) * mask_rr
                    };
                }
                vg_result sr2 = vg_fill_convex(ctx, dome_fill, DOME_SEG + 2, &src_mask);
                if (sr2 != VG_OK) {
                    return sr2;
                }
                /* Seal the dome base edge and emitter point against AA/bloom leakage. */
                const vg_vec2 base_cap[4] = {
                    {sl->origin_x - mask_rr - 2.0f, sl->origin_y - 2.5f},
                    {sl->origin_x + mask_rr + 2.0f, sl->origin_y - 2.5f},
                    {sl->origin_x + mask_rr + 2.0f, sl->origin_y + 2.0f},
                    {sl->origin_x - mask_rr - 2.0f, sl->origin_y + 2.0f}
                };
                sr2 = vg_fill_convex(ctx, base_cap, 4, &src_mask);
                if (sr2 != VG_OK) {
                    return sr2;
                }
            }
        }
        {
            const float rr = fmaxf(sl->source_radius, 2.0f);
            const vg_color src_red = {1.0f, 0.22f, 0.22f, 0.95f};
            const vg_fill_style src_fill = make_fill(0.72f * intensity_scale, src_red, VG_BLEND_ALPHA);
            const vg_stroke_style src_stroke = make_stroke(1.6f, 0.90f * intensity_scale, src_red, VG_BLEND_ALPHA);
            if (sl->source_type == SEARCHLIGHT_SOURCE_ORB) {
                enum { ORB_SEG = 20 };
                vg_vec2 orb[ORB_SEG + 1];
                for (int k = 0; k <= ORB_SEG; ++k) {
                    const float u = (float)k / (float)ORB_SEG;
                    const float a = u * 6.28318530718f;
                    orb[k] = (vg_vec2){
                        sl->origin_x + cosf(a) * rr,
                        sl->origin_y + sinf(a) * rr
                    };
                }
                vg_result r = vg_fill_circle(ctx, (vg_vec2){sl->origin_x, sl->origin_y}, rr, &src_fill, 18);
                if (r != VG_OK) {
                    return r;
                }
                r = vg_draw_polyline(ctx, orb, ORB_SEG + 1, &src_stroke, 1);
                if (r != VG_OK) {
                    return r;
                }
            } else {
                enum { DOME_SEG = 18 };
                vg_vec2 dome_fill[DOME_SEG + 2];
                vg_vec2 dome_arc[DOME_SEG + 1];
                dome_fill[0] = (vg_vec2){sl->origin_x, sl->origin_y};
                for (int k = 0; k <= DOME_SEG; ++k) {
                    const float u = (float)k / (float)DOME_SEG;
                    const float a = u * 3.14159265359f;
                    const vg_vec2 p = {
                        sl->origin_x + cosf(a) * rr,
                        sl->origin_y + sinf(a) * rr
                    };
                    dome_fill[k + 1] = p;
                    dome_arc[k] = p;
                }
                vg_result r = vg_fill_convex(ctx, dome_fill, DOME_SEG + 2, &src_fill);
                if (r != VG_OK) {
                    return r;
                }
                r = vg_draw_polyline(ctx, dome_arc, DOME_SEG + 1, &src_stroke, 0);
                if (r != VG_OK) {
                    return r;
                }
            }
        }
        const float a0 = sl->current_angle_rad - sl->half_angle_rad;
        const float a1 = sl->current_angle_rad + sl->half_angle_rad;
        const vg_vec2 origin = {sl->origin_x, sl->origin_y};
        const vg_vec2 dir0 = {cosf(a0), sinf(a0)};
        const vg_vec2 dir1 = {cosf(a1), sinf(a1)};
        const float body_len = sl->length * 0.80f;
        const vg_color beam_col = pal->primary_dim;
        vg_fill_style body_fill = make_fill(
            0.14f + 0.06f * intensity_scale,
            (vg_color){beam_col.r, beam_col.g, beam_col.b, 0.06f},
            VG_BLEND_ADDITIVE
        );
        if (can_stencil) {
            body_fill.stencil = vg_stencil_state_make_test_equal(1u, 0xffu);
            body_fill.stencil.compare_op = VG_COMPARE_NOT_EQUAL;
        }
        const vg_vec2 body_tri[3] = {
            origin,
            {origin.x + dir0.x * body_len, origin.y + dir0.y * body_len},
            {origin.x + dir1.x * body_len, origin.y + dir1.y * body_len}
        };
        vg_result r = vg_fill_convex(ctx, body_tri, 3, &body_fill);
        if (r != VG_OK) {
            return r;
        }
        for (int s = 0; s < tip_slices; ++s) {
            const float u0 = (float)s / (float)tip_slices;
            const float u1 = (float)(s + 1) / (float)tip_slices;
            const float t0 = 0.80f + 0.20f * u0;
            const float t1 = 0.80f + 0.20f * u1;
            const float l0 = sl->length * t0;
            const float l1 = sl->length * t1;
            float fade = 1.0f - u1;
            fade = fade * fade * (3.0f - 2.0f * fade);
            vg_fill_style tip_fill = make_fill(
                (0.14f + 0.06f * intensity_scale) * fade,
                (vg_color){beam_col.r, beam_col.g, beam_col.b, 0.06f * fade},
                VG_BLEND_ADDITIVE
            );
            if (can_stencil) {
                tip_fill.stencil = vg_stencil_state_make_test_equal(1u, 0xffu);
                tip_fill.stencil.compare_op = VG_COMPARE_NOT_EQUAL;
            }
            const vg_vec2 a = {origin.x + dir0.x * l0, origin.y + dir0.y * l0};
            const vg_vec2 b = {origin.x + dir1.x * l0, origin.y + dir1.y * l0};
            const vg_vec2 c = {origin.x + dir1.x * l1, origin.y + dir1.y * l1};
            const vg_vec2 d = {origin.x + dir0.x * l1, origin.y + dir0.y * l1};
            const vg_vec2 tip_tri0[3] = {a, b, c};
            const vg_vec2 tip_tri1[3] = {a, c, d};
            r = vg_fill_convex(ctx, tip_tri0, 3, &tip_fill);
            if (r != VG_OK) {
                return r;
            }
            r = vg_fill_convex(ctx, tip_tri1, 3, &tip_fill);
            if (r != VG_OK) {
                return r;
            }
        }
        vg_stroke_style rail_halo = *land_halo;
        vg_stroke_style rail_main = *land_main;
        if (can_stencil) {
            rail_halo.stencil = vg_stencil_state_make_test_equal(1u, 0xffu);
            rail_halo.stencil.compare_op = VG_COMPARE_NOT_EQUAL;
            rail_main.stencil = vg_stencil_state_make_test_equal(1u, 0xffu);
            rail_main.stencil.compare_op = VG_COMPARE_NOT_EQUAL;
        }
        rail_halo.width_px *= 1.18f;
        rail_main.width_px *= 1.06f;
        rail_halo.intensity *= 0.78f;
        rail_main.intensity *= 0.86f;
        const vg_vec2 left_body[2] = {
            origin,
            {origin.x + dir0.x * body_len, origin.y + dir0.y * body_len}
        };
        const vg_vec2 right_body[2] = {
            origin,
            {origin.x + dir1.x * body_len, origin.y + dir1.y * body_len}
        };
        r = vg_draw_polyline(ctx, left_body, 2, &rail_halo, 0);
        if (r != VG_OK) {
            return r;
        }
        r = vg_draw_polyline(ctx, left_body, 2, &rail_main, 0);
        if (r != VG_OK) {
            return r;
        }
        r = vg_draw_polyline(ctx, right_body, 2, &rail_halo, 0);
        if (r != VG_OK) {
            return r;
        }
        r = vg_draw_polyline(ctx, right_body, 2, &rail_main, 0);
        if (r != VG_OK) {
            return r;
        }
        for (int s = 0; s < tip_slices; ++s) {
            const float u0 = (float)s / (float)tip_slices;
            const float u1 = (float)(s + 1) / (float)tip_slices;
            const float t0 = 0.80f + 0.20f * u0;
            const float t1 = 0.80f + 0.20f * u1;
            const float fade = 1.0f - u1;
            vg_stroke_style lh = rail_halo;
            vg_stroke_style lm = rail_main;
            lh.intensity *= fade;
            lm.intensity *= fade;
            lh.color.a *= fade;
            lm.color.a *= fade;
            const vg_vec2 left_tip[2] = {
                {origin.x + dir0.x * (sl->length * t0), origin.y + dir0.y * (sl->length * t0)},
                {origin.x + dir0.x * (sl->length * t1), origin.y + dir0.y * (sl->length * t1)}
            };
            const vg_vec2 right_tip[2] = {
                {origin.x + dir1.x * (sl->length * t0), origin.y + dir1.y * (sl->length * t0)},
                {origin.x + dir1.x * (sl->length * t1), origin.y + dir1.y * (sl->length * t1)}
            };
            r = vg_draw_polyline(ctx, left_tip, 2, &lh, 0);
            if (r != VG_OK) {
                return r;
            }
            r = vg_draw_polyline(ctx, left_tip, 2, &lm, 0);
            if (r != VG_OK) {
                return r;
            }
            r = vg_draw_polyline(ctx, right_tip, 2, &lh, 0);
            if (r != VG_OK) {
                return r;
            }
            r = vg_draw_polyline(ctx, right_tip, 2, &lm, 0);
            if (r != VG_OK) {
                return r;
            }
        }
    }
    return VG_OK;
}

static int arc_pair_energized(const arc_node_runtime* a, float t) {
    if (!a || !a->active) {
        return 0;
    }
    const float period = fmaxf(a->period_s, 0.10f);
    const float on_s = clampf(a->on_s, 0.0f, period);
    if (on_s <= 0.0f) {
        return 0;
    }
    return (fmodf(t + a->phase_s, period) <= on_s) ? 1 : 0;
}

static vg_result draw_arc_nodes(
    vg_context* ctx,
    const game_state* g,
    const palette_theme* pal,
    const vg_stroke_style* land_halo,
    const vg_stroke_style* land_main,
    int draw_beam
) {
    if (!ctx || !g || !pal || !land_halo || !land_main || g->arc_node_count < 2) {
        return VG_OK;
    }
    for (int i = 0; i + 1 < g->arc_node_count && i + 1 < MAX_ARC_NODES; i += 2) {
        const arc_node_runtime* a = &g->arc_nodes[i];
        const arc_node_runtime* b = &g->arc_nodes[i + 1];
        if (!a->active || !b->active) {
            continue;
        }
        const int energized = arc_pair_energized(a, g->t);
        vg_stroke_style node = *land_main;
        node.width_px *= 1.06f;
        node.intensity *= energized ? 1.18f : 0.72f;
        node.color = energized ? pal->secondary : pal->primary_dim;
        const vg_vec2 p0[2] = {{a->x - 8.0f, a->y}, {a->x + 8.0f, a->y}};
        const vg_vec2 p1[2] = {{b->x - 8.0f, b->y}, {b->x + 8.0f, b->y}};
        vg_result r = vg_draw_polyline(ctx, p0, 2, &node, 0);
        if (r != VG_OK) return r;
        r = vg_draw_polyline(ctx, p1, 2, &node, 0);
        if (r != VG_OK) return r;
        {
            vg_fill_style dot = make_fill(0.98f, energized ? pal->secondary : pal->primary_dim, VG_BLEND_ALPHA);
            r = vg_fill_circle(ctx, (vg_vec2){a->x, a->y}, 1.9f, &dot, 12);
            if (r != VG_OK) return r;
            r = vg_fill_circle(ctx, (vg_vec2){b->x, b->y}, 1.9f, &dot, 12);
            if (r != VG_OK) return r;
        }
        if (!draw_beam || !energized) {
            continue;
        }
        const float period = fmaxf(a->period_s, 0.10f);
        const float on_s = clampf(a->on_s, 0.0f, period);
        float pulse = 1.0f;
        if (on_s > 0.0f) {
            const float phase = fmodf(g->t + a->phase_s, period);
            const float t_on = clampf(phase / on_s, 0.0f, 1.0f);
            pulse = 0.65f + 0.35f * (1.0f - t_on);
        }
        vg_stroke_style arc_halo = *land_halo;
        vg_stroke_style arc_main = *land_main;
        arc_halo.width_px *= 1.10f;
        arc_main.width_px *= 1.05f;
        arc_halo.intensity *= (0.90f + 0.20f * pulse);
        arc_main.intensity *= (1.00f + 0.20f * pulse);
        arc_halo.color = pal->primary_dim;
        arc_main.color = pal->secondary;
        const int seg_n = 14;
        for (int strand = 0; strand < 2; ++strand) {
            vg_vec2 arc[14];
            const float strand_off = (strand == 0) ? -1.2f : 1.2f;
            for (int k = 0; k < seg_n; ++k) {
                const float u = (float)k / (float)(seg_n - 1);
                const float x = a->x + (b->x - a->x) * u;
                const float y = a->y + (b->y - a->y) * u;
                const float jag = 2.2f + 2.3f * pulse;
                const float j0 = sinf(g->t * (42.0f + 2.0f * strand) + (float)i * 0.71f + (float)k * 1.77f);
                const float j1 = sinf(g->t * 9.0f + (float)i * 0.43f + (float)k * 0.87f);
                arc[k] = (vg_vec2){x, y + strand_off + j0 * jag + j1 * 1.35f};
            }
            vg_result r = vg_draw_polyline(ctx, arc, seg_n, &arc_halo, 0);
            if (r != VG_OK) return r;
            r = vg_draw_polyline(ctx, arc, seg_n, &arc_main, 0);
            if (r != VG_OK) return r;
        }
    }
    return VG_OK;
}

static vg_result draw_exit_portal(
    vg_context* ctx,
    const game_state* g,
    const palette_theme* pal,
    float intensity_scale,
    const vg_stroke_style* land_halo,
    const vg_stroke_style* land_main
) {
    if (!ctx || !g || !pal || !land_halo || !land_main || !g->exit_portal_active) {
        return VG_OK;
    }
    const float cx = g->exit_portal_x;
    const float cy = g->exit_portal_y;
    const float max_half = fmaxf(g->exit_portal_radius * 2.30f, 42.0f);
    const float min_half = fmaxf(g->exit_portal_radius * 0.42f, 10.0f);
    const float cycle_s = 6.20f;
    const int ring_count = 6;
    const float c45 = 0.70710678f;
    const float s45 = 0.70710678f;

    for (int i = 0; i < ring_count; ++i) {
        float phase = fmodf(g->t / cycle_s + (float)i / (float)ring_count, 1.0f);
        if (phase < 0.0f) {
            phase += 1.0f;
        }
        /* Ping-pong phase (0->1->0) yields expand/shrink ring motion. */
        const float tri = 1.0f - fabsf(phase * 2.0f - 1.0f);
        const float ease = tri * tri * (3.0f - 2.0f * tri);
        const float half = min_half + (max_half - min_half) * ease;
        const float edge_fade = 1.0f - fabsf(phase * 2.0f - 1.0f);
        const float alpha = 0.16f + edge_fade * 0.56f;
        const vg_vec2 square[5] = {
            {cx + ((-half) * c45 - (-half) * s45), cy + ((-half) * s45 + (-half) * c45)},
            {cx + (( half) * c45 - (-half) * s45), cy + (( half) * s45 + (-half) * c45)},
            {cx + (( half) * c45 - ( half) * s45), cy + (( half) * s45 + ( half) * c45)},
            {cx + ((-half) * c45 - ( half) * s45), cy + ((-half) * s45 + ( half) * c45)},
            {cx + ((-half) * c45 - (-half) * s45), cy + ((-half) * s45 + (-half) * c45)}
        };
        vg_stroke_style sh = *land_halo;
        vg_stroke_style sm = *land_main;
        sh.color = (vg_color){pal->primary.r, pal->primary.g, pal->primary.b, alpha * 0.95f};
        sm.color = (vg_color){pal->secondary.r, pal->secondary.g, pal->secondary.b, alpha};
        sh.intensity *= (0.90f + edge_fade * 1.10f) * intensity_scale;
        sm.intensity *= (0.96f + edge_fade * 1.18f) * intensity_scale;
        sh.width_px *= 1.24f;
        sm.width_px *= 1.18f;
        vg_result r = vg_draw_polyline(ctx, square, 5, &sh, 0);
        if (r != VG_OK) {
            return r;
        }
        r = vg_draw_polyline(ctx, square, 5, &sm, 0);
        if (r != VG_OK) {
            return r;
        }
    }

    return VG_OK;
}

static vg_result draw_asteroid_storm(
    vg_context* ctx,
    const game_state* g,
    const palette_theme* pal,
    const vg_stroke_style* land_halo,
    const vg_stroke_style* land_main
) {
    static const vg_vec2 k_shape[11] = {
        {-1.00f, -0.28f},
        {-0.78f, -0.84f},
        {-0.36f, -0.62f},
        {0.04f, -0.98f},
        {0.46f, -0.52f},
        {0.92f, -0.34f},
        {0.84f, 0.14f},
        {0.58f, 0.78f},
        {0.06f, 0.92f},
        {-0.46f, 0.66f},
        {-0.88f, 0.20f}
    };
    if (!ctx || !g || !pal || !land_halo || !land_main ||
        !g->asteroid_storm_enabled || g->asteroid_count <= 0) {
        return VG_OK;
    }
    vg_stroke_style halo = *land_halo;
    vg_stroke_style main = *land_main;
    halo.width_px *= 1.16f;
    main.width_px *= 1.07f;
    halo.intensity *= 0.78f;
    main.intensity *= 0.92f;
    halo.color = (vg_color){pal->primary.r, pal->primary.g, pal->primary.b, 0.58f};
    main.color = (vg_color){pal->secondary.r, pal->secondary.g, pal->secondary.b, 0.78f};
    vg_color fill_c = pal->primary_dim;
    fill_c.a = fminf(fill_c.a, 0.22f);
    const vg_fill_style fill = make_fill(0.55f, fill_c, VG_BLEND_ALPHA);

    for (int i = 0; i < g->asteroid_count && i < MAX_ASTEROIDS; ++i) {
        const asteroid_body* a = &g->asteroids[i];
        if (!a->active) {
            continue;
        }
        const float c = cosf(a->angle);
        const float s = sinf(a->angle);
        vg_vec2 poly[11];
        for (int k = 0; k < 11; ++k) {
            const float px = k_shape[k].x * a->size;
            const float py = k_shape[k].y * a->size;
            poly[k].x = a->b.x + px * c - py * s;
            poly[k].y = a->b.y + px * s + py * c;
        }
        vg_result r = vg_fill_convex(ctx, poly, 11, &fill);
        if (r != VG_OK) {
            return r;
        }
        r = vg_draw_polyline(ctx, poly, 11, &halo, 1);
        if (r != VG_OK) {
            return r;
        }
        r = vg_draw_polyline(ctx, poly, 11, &main, 1);
        if (r != VG_OK) {
            return r;
        }
    }
    return VG_OK;
}

static vg_result draw_minefields(
    vg_context* ctx,
    const game_state* g,
    const palette_theme* pal,
    const vg_stroke_style* land_halo,
    const vg_stroke_style* land_main
) {
    if (!ctx || !g || !pal || !land_halo || !land_main || g->mine_count <= 0) {
        return VG_OK;
    }
    vg_stroke_style halo = *land_halo;
    vg_stroke_style main = *land_main;
    halo.width_px *= 1.14f;
    main.width_px *= 1.08f;
    halo.intensity *= 0.95f;
    main.intensity *= 1.05f;
    halo.color = (vg_color){pal->primary.r, pal->primary.g, pal->primary.b, 0.62f};
    main.color = (vg_color){pal->secondary.r, pal->secondary.g, pal->secondary.b, 0.90f};
    vg_color fill_c = pal->primary_dim;
    fill_c.a = fminf(fill_c.a, 0.30f);
    const vg_fill_style fill = make_fill(0.75f, fill_c, VG_BLEND_ALPHA);
    for (int i = 0; i < g->mine_count && i < MAX_MINES; ++i) {
        const mine* m = &g->mines[i];
        if (!m->active) {
            continue;
        }
        vg_vec2 hex[7];
        const float c = cosf(m->angle);
        const float s = sinf(m->angle);
        const float r = m->radius;
        for (int k = 0; k < 6; ++k) {
            const float a = ((float)k / 6.0f) * 6.2831853f;
            const float px = cosf(a) * r;
            const float py = sinf(a) * r;
            hex[k].x = m->b.x + px * c - py * s;
            hex[k].y = m->b.y + px * s + py * c;
        }
        hex[6] = hex[0];
        vg_result vr = vg_fill_convex(ctx, hex, 6, &fill);
        if (vr != VG_OK) {
            return vr;
        }
        for (int k = 0; k < 6; ++k) {
            const int k1 = (k + 1) % 6;
            const float mx = (hex[k].x + hex[k1].x) * 0.5f;
            const float my = (hex[k].y + hex[k1].y) * 0.5f;
            float nx = mx - m->b.x;
            float ny = my - m->b.y;
            const float nl = sqrtf(nx * nx + ny * ny);
            if (nl > 1e-4f) {
                nx /= nl;
                ny /= nl;
            }
            const vg_vec2 spike[3] = {
                {hex[k].x, hex[k].y},
                {mx + nx * (r * 0.68f), my + ny * (r * 0.68f)},
                {hex[k1].x, hex[k1].y}
            };
            vr = vg_draw_polyline(ctx, spike, 3, &halo, 0);
            if (vr != VG_OK) {
                return vr;
            }
            vr = vg_draw_polyline(ctx, spike, 3, &main, 0);
            if (vr != VG_OK) {
                return vr;
            }
        }
        vr = vg_draw_polyline(ctx, hex, 7, &halo, 0);
        if (vr != VG_OK) {
            return vr;
        }
        vr = vg_draw_polyline(ctx, hex, 7, &main, 0);
        if (vr != VG_OK) {
            return vr;
        }
    }
    return VG_OK;
}

static void structure_prefab_dims_world(int prefab_id, int* out_w, int* out_h) {
    int w = 1;
    int h = 1;
    if (prefab_id == 4) {
        h = 3;
    }
    if (out_w) {
        *out_w = w;
    }
    if (out_h) {
        *out_h = h;
    }
}

static vg_result draw_level_structures(
    vg_context* ctx,
    const game_state* g,
    const palette_theme* pal,
    const vg_stroke_style* land_halo,
    const vg_stroke_style* land_main,
    int draw_cpu_vent_smoke
) {
    if (!ctx || !g || !pal || !land_halo || !land_main || g->render_style == LEVEL_RENDER_CYLINDER) {
        return VG_OK;
    }
    const leveldef_level* lvl = game_current_leveldef(g);
    if (!lvl || lvl->structure_count <= 0) {
        return VG_OK;
    }

    vg_stroke_style base_halo = *land_halo;
    vg_stroke_style base_main = *land_main;
    base_halo.width_px *= 0.90f;
    base_main.width_px *= 0.88f;
    base_halo.color = (vg_color){pal->primary.r, pal->primary.g, pal->primary.b, 0.48f};
    base_main.color = (vg_color){pal->primary_dim.r, pal->primary_dim.g, pal->primary_dim.b, 0.84f};

    vg_stroke_style feature_halo = *land_halo;
    vg_stroke_style feature_main = *land_main;
    feature_halo.width_px *= 0.94f;
    feature_main.width_px *= 0.92f;
    feature_halo.color = (vg_color){pal->secondary.r, pal->secondary.g, pal->secondary.b, 0.58f};
    feature_main.color = (vg_color){pal->secondary.r, pal->secondary.g, pal->secondary.b, 0.94f};

    vg_fill_style base_fill = make_fill(0.65f, pal->primary_dim, VG_BLEND_ALPHA);
    base_fill.color.a = fminf(base_fill.color.a, 0.22f);

    const float unit_w = g->world_w * (float)LEVELDEF_STRUCTURE_GRID_SCALE / (float)(LEVELDEF_STRUCTURE_GRID_W - 1);
    const float unit_h = g->world_h / (float)((LEVELDEF_STRUCTURE_GRID_H - 1) / LEVELDEF_STRUCTURE_GRID_SCALE);
    const float view_min_x = g->camera_x - g->world_w * 0.58f;
    const float view_max_x = g->camera_x + g->world_w * 0.58f;
    const float view_min_y = g->camera_y - g->world_h * 0.58f;
    const float view_max_y = g->camera_y + g->world_h * 0.58f;

    for (int i = 0; i < lvl->structure_count && i < LEVELDEF_MAX_STRUCTURES; ++i) {
        const leveldef_structure_instance* st = &lvl->structures[i];
        const vg_stroke_style* hl = (st->layer > 0) ? &feature_halo : &base_halo;
        const vg_stroke_style* mn = (st->layer > 0) ? &feature_main : &base_main;
        int w_units = 1;
        int h_units = 1;
        structure_prefab_dims_world(st->prefab_id, &w_units, &h_units);
        const float bx = (float)st->grid_x * unit_w;
        const float by = (float)st->grid_y * unit_h;
        const float bw = unit_w * (float)w_units;
        const float bh = unit_h * (float)h_units;
        if (bx + bw < view_min_x - unit_w || bx > view_max_x + unit_w ||
            by + bh < view_min_y - unit_h || by > view_max_y + unit_h) {
            continue;
        }

        vg_result vr = VG_OK;

        vr = draw_structure_prefab_tile(
            ctx,
            st->prefab_id,
            st->layer,
            bx,
            by,
            unit_w,
            unit_h,
            st->rotation_quadrants,
            st->flip_x,
            st->flip_y,
            hl,
            &base_fill
        );
        if (vr != VG_OK) {
            return vr;
        }
        vr = draw_structure_prefab_tile(
            ctx,
            st->prefab_id,
            st->layer,
            bx,
            by,
            unit_w,
            unit_h,
            st->rotation_quadrants,
            st->flip_x,
            st->flip_y,
            mn,
            NULL
        );
        if (vr != VG_OK) {
            return vr;
        }

        if (st->layer > 0 && st->prefab_id == 5) {
            const float streak_half = fmaxf(unit_w * 0.14f, 2.0f);
            for (int lane = 0; lane < 2; ++lane) {
                const float ly = (lane == 0) ? (bh * 0.40f) : (bh * 0.60f);
                for (int p = 0; p < 6; ++p) {
                    const float h = (float)(((st->grid_x * 37 + st->grid_y * 19 + p * 13) & 255)) / 255.0f;
                    const float ph = repeatf(g->t * (0.55f + 0.35f * h) + (float)p * 0.31f + h, 1.0f);
                    const float x = ph * bw;
                    const float alpha = 0.44f * (0.65f + 0.35f * sinf(g->t * 3.2f + h * 7.1f));
                    const vg_fill_style spark = {
                        .intensity = 0.95f,
                        .color = (vg_color){pal->secondary.r, pal->secondary.g, pal->secondary.b, alpha},
                        .blend = VG_BLEND_ALPHA
                    };
                    const vg_vec2 core[2] = {
                        structure_map_point(
                            x - streak_half, ly, bx, by, bw, bh,
                            st->rotation_quadrants, st->flip_x, st->flip_y
                        ),
                        structure_map_point(
                            x + streak_half, ly, bx, by, bw, bh,
                            st->rotation_quadrants, st->flip_x, st->flip_y
                        )
                    };
                    const vg_vec2 center = structure_map_point(
                        x, ly, bx, by, bw, bh,
                        st->rotation_quadrants, st->flip_x, st->flip_y
                    );
                    vr = vg_draw_polyline(ctx, core, 2, mn, 0);
                    if (vr != VG_OK) {
                        return vr;
                    }
                    vr = vg_fill_circle(ctx, center, fmaxf(unit_h * 0.07f, 1.2f), &spark, 10);
                    if (vr != VG_OK) {
                        return vr;
                    }
                }
            }
        } else if (draw_cpu_vent_smoke && st->layer > 0 && st->prefab_id >= 7) {
            const float vent_density = (st->vent_density > 0.0f) ? st->vent_density : 1.0f;
            const float vent_opacity = (st->vent_opacity > 0.0f) ? st->vent_opacity : 1.0f;
            const float vent_height = (st->vent_plume_height > 0.0f) ? st->vent_plume_height : 1.0f;
            const int streams = clampi((int)lroundf(5.0f * vent_density), 2, 14);
            const int puffs_per_stream = clampi((int)lroundf(10.0f * vent_density), 4, 24);
            const float vent_y = bh;
            for (int stream = 0; stream < streams; ++stream) {
                const float stream_t = (streams > 1) ? ((float)stream / (float)(streams - 1)) : 0.5f;
                const float vent_x = bw * (0.24f + 0.52f * stream_t);
                for (int puff = 0; puff < puffs_per_stream; ++puff) {
                    const int seed = st->grid_x * 73 + st->grid_y * 41 + stream * 101 + puff * 31;
                    const float h = (float)(seed & 255) / 255.0f;
                    const float h2 = (float)((seed * 131) & 255) / 255.0f;
                    const float ph = repeatf(g->t * (0.11f + 0.10f * h) + (float)puff * 0.17f + h * 0.9f + stream_t * 0.31f, 1.0f);
                    const float rise = ph * (bh * 5.4f * vent_height);
                    const float swirl = sinf((ph + h) * (8.6f + 2.1f * stream_t) + (float)puff * 1.1f) * bw * (0.10f + 0.20f * ph);
                    const float spread = ((float)puff / (float)puffs_per_stream) * bw * 0.18f;
                    const float life_in = clampf(ph / 0.14f, 0.0f, 1.0f);
                    const float life_out = clampf((1.0f - ph) / 0.48f, 0.0f, 1.0f);
                    const float life = life_in * life_out;
                    const float life_s = life * life * (3.0f - 2.0f * life);
                    const float size_u = ph * ph * (3.0f - 2.0f * ph);
                    const float r_outer = fmaxf(unit_h * (0.20f + 0.36f * size_u + 0.14f * h), 1.3f);
                    const float r_mid = r_outer * (0.62f + 0.10f * h);
                    const float r_core = r_outer * 0.34f;
                    const float base_a = 0.28f * vent_opacity * life_s * (0.82f + 0.18f * (1.0f - stream_t));
                    const float tone = (h2 - 0.5f) * 0.16f;
                    const float shade_outer = 0.54f + 0.22f * h + tone;
                    const float shade_mid = 0.44f + 0.20f * h + tone * 0.85f;
                    const float shade_core = 0.34f + 0.16f * h + tone * 0.70f;
                    const float ch_r = 1.00f + (h2 - 0.5f) * 0.08f;
                    const float ch_g = 1.00f + (h - 0.5f) * 0.06f;
                    const float ch_b = 1.00f + (0.5f - h2) * 0.08f;
                    const vg_vec2 center = structure_map_point(
                        vent_x + swirl + spread,
                        vent_y + rise,
                        bx, by, bw, bh,
                        st->rotation_quadrants, st->flip_x, st->flip_y
                    );
                    const vg_fill_style smoke_outer = {
                        .intensity = 0.64f,
                        .color = (vg_color){
                            pal->primary_dim.r * shade_outer * ch_r,
                            pal->primary_dim.g * shade_outer * ch_g,
                            pal->primary_dim.b * shade_outer * ch_b,
                            base_a * (0.90f + 0.16f * h2)
                        },
                        .blend = VG_BLEND_ALPHA
                    };
                    const vg_fill_style smoke_mid = {
                        .intensity = 0.56f,
                        .color = (vg_color){
                            pal->primary_dim.r * shade_mid * ch_r,
                            pal->primary_dim.g * shade_mid * ch_g,
                            pal->primary_dim.b * shade_mid * ch_b,
                            base_a * 0.72f * (0.88f + 0.18f * h)
                        },
                        .blend = VG_BLEND_ALPHA
                    };
                    const vg_fill_style smoke_core = {
                        .intensity = 0.48f,
                        .color = (vg_color){
                            pal->primary_dim.r * shade_core * ch_r,
                            pal->primary_dim.g * shade_core * ch_g,
                            pal->primary_dim.b * shade_core * ch_b,
                            base_a * 0.46f * (0.92f + 0.12f * h2)
                        },
                        .blend = VG_BLEND_ALPHA
                    };
                    {
                        enum { SPLAT_VERTS = 18 };
                        vg_vec2 splat_outer[SPLAT_VERTS];
                        vg_vec2 splat_mid[SPLAT_VERTS];
                        vg_vec2 splat_core[SPLAT_VERTS];
                        const float rot = h * 6.2831853f + ph * 1.6f;
                        const float nseed_x = (float)(st->grid_x * 13 + st->grid_y * 7) * 0.173f + (float)stream * 0.91f + (float)puff * 0.37f;
                        const float nseed_y = (float)(st->grid_x * 5 + st->grid_y * 11) * 0.191f + (float)stream * 0.67f + (float)puff * 0.53f;
                        const float ox = (perlin2(nseed_x + ph * 2.7f, nseed_y + 1.7f + h2) * 0.5f + 0.5f) - 0.5f;
                        const float oy = (perlin2(nseed_x + 2.9f + ph * 1.9f, nseed_y + 0.9f + h) * 0.5f + 0.5f) - 0.5f;
                        const vg_vec2 c_outer = {center.x + ox * r_outer * 0.18f, center.y + oy * r_outer * 0.18f};
                        const vg_vec2 c_mid = {center.x - ox * r_mid * 0.14f, center.y + oy * r_mid * 0.10f};
                        const vg_vec2 c_core = {center.x + ox * r_core * 0.10f, center.y - oy * r_core * 0.12f};
                        for (int sv = 0; sv < SPLAT_VERTS; ++sv) {
                            const float t = (float)sv / (float)SPLAT_VERTS;
                            const float a = rot + t * 6.2831853f;
                            const float ca = cosf(a);
                            const float sa = sinf(a);
                            const float no = 0.5f + 0.5f * perlin2(
                                nseed_x + ca * 1.8f + ph * 2.3f,
                                nseed_y + sa * 1.8f + h * 1.7f
                            );
                            const float nm = 0.5f + 0.5f * perlin2(
                                nseed_x + 1.9f + ca * 2.2f + ph * 2.9f,
                                nseed_y + 1.3f + sa * 2.2f + h2 * 1.6f
                            );
                            const float nc = 0.5f + 0.5f * perlin2(
                                nseed_x + 3.7f + ca * 2.6f + ph * 3.4f,
                                nseed_y + 2.1f + sa * 2.6f + h * 1.9f
                            );
                            const float ro = r_outer * (0.52f + 0.88f * no);
                            const float rm = r_mid * (0.56f + 0.74f * nm);
                            const float rc = r_core * (0.64f + 0.52f * nc);
                            splat_outer[sv] = (vg_vec2){c_outer.x + ca * ro, c_outer.y + sa * ro};
                            splat_mid[sv] = (vg_vec2){c_mid.x + ca * rm, c_mid.y + sa * rm};
                            splat_core[sv] = (vg_vec2){c_core.x + ca * rc, c_core.y + sa * rc};
                        }
                        vr = vg_fill_convex(ctx, splat_outer, SPLAT_VERTS, &smoke_outer);
                        if (vr != VG_OK) {
                            return vr;
                        }
                        vr = vg_fill_convex(ctx, splat_mid, SPLAT_VERTS, &smoke_mid);
                        if (vr != VG_OK) {
                            return vr;
                        }
                        vr = vg_fill_convex(ctx, splat_core, SPLAT_VERTS, &smoke_core);
                        if (vr != VG_OK) {
                            return vr;
                        }
                        {
                            /* Add one low-alpha offset wisp to break uniform blob silhouettes. */
                            vg_fill_style wisp = smoke_outer;
                            vg_vec2 wisp_poly[SPLAT_VERTS];
                            const float w_rot = rot + 0.7f + h2 * 1.6f;
                            const vg_vec2 cw = {
                                center.x + (ox - oy) * r_outer * 0.26f,
                                center.y + (ox + oy) * r_outer * 0.18f
                            };
                            wisp.color.a *= 0.34f;
                            for (int sv = 0; sv < SPLAT_VERTS; ++sv) {
                                const float t = (float)sv / (float)SPLAT_VERTS;
                                const float a = w_rot + t * 6.2831853f;
                                const float ca = cosf(a);
                                const float sa = sinf(a);
                                const float nw = 0.5f + 0.5f * perlin2(
                                    nseed_x + 4.7f + ca * 2.3f + ph * 2.0f,
                                    nseed_y + 3.9f + sa * 2.3f + h2 * 2.1f
                                );
                                const float rw = r_outer * (0.34f + 0.42f * nw);
                                wisp_poly[sv] = (vg_vec2){cw.x + ca * rw, cw.y + sa * rw};
                            }
                            vr = vg_fill_convex(ctx, wisp_poly, SPLAT_VERTS, &wisp);
                            if (vr != VG_OK) {
                                return vr;
                            }
                        }
                    }
                }
            }
        }
    }
    return VG_OK;
}

static vg_result draw_missile_system(
    vg_context* ctx,
    const game_state* g,
    const palette_theme* pal,
    const vg_stroke_style* land_halo,
    const vg_stroke_style* land_main
) {
    if (!ctx || !g || !pal || !land_halo || !land_main) {
        return VG_OK;
    }
    vg_stroke_style halo = *land_halo;
    vg_stroke_style main = *land_main;
    halo.width_px *= 1.12f;
    main.width_px *= 1.08f;
    halo.intensity *= 0.96f;
    main.intensity *= 1.05f;
    halo.color = (vg_color){pal->primary.r, pal->primary.g, pal->primary.b, 0.66f};
    main.color = (vg_color){pal->secondary.r, pal->secondary.g, pal->secondary.b, 0.94f};
    vg_fill_style fill = make_fill(0.52f, pal->primary_dim, VG_BLEND_ALPHA);

    if (g->missile_launcher_count > 0) {
        for (int i = 0; i < g->missile_launcher_count && i < MAX_MISSILE_LAUNCHERS; ++i) {
            const missile_launcher* ml = &g->missile_launchers[i];
            if (!ml->active || ml->fired) {
                continue;
            }
            vg_stroke_style l_halo = halo;
            vg_stroke_style l_main = main;
            l_halo.color = (vg_color){1.0f, 0.24f, 0.24f, 0.68f};
            l_main.color = (vg_color){1.0f, 0.32f, 0.32f, 0.96f};
            const float half = ((float)ml->count - 1.0f) * 0.5f;
            for (int k = 0; k < ml->count; ++k) {
                if (k < ml->launched_count) {
                    continue;
                }
                const float slot = (float)k - half;
                const float x = ml->anchor_x + slot * ml->spacing;
                const float y = ml->anchor_y;
                const vg_vec2 shape[5] = {
                    {x, y + 14.0f},
                    {x - 8.5f, y - 8.0f},
                    {x - 3.8f, y - 2.2f},
                    {x + 8.5f, y - 8.0f},
                    {x, y + 14.0f}
                };
                vg_result r = vg_fill_convex(ctx, shape, 4, &fill);
                if (r != VG_OK) return r;
                r = vg_draw_polyline(ctx, shape, 5, &l_halo, 1);
                if (r != VG_OK) return r;
                r = vg_draw_polyline(ctx, shape, 5, &l_main, 1);
                if (r != VG_OK) return r;
            }
        }
    }

    if (g->missile_count <= 0) {
        return VG_OK;
    }
    for (int i = 0; i < MAX_MISSILES; ++i) {
        const homing_missile* m = &g->missiles[i];
        if (!m->active) {
            continue;
        }
        vg_stroke_style m_halo = halo;
        vg_stroke_style m_main = main;
        vg_fill_style m_fill = fill;
        if (m->owner == MISSILE_OWNER_ENEMY) {
            m_halo.color = (vg_color){1.0f, 0.24f, 0.24f, 0.72f};
            m_main.color = (vg_color){1.0f, 0.30f, 0.30f, 1.0f};
            m_fill.color = (vg_color){0.35f, 0.08f, 0.08f, 0.48f};
        }
        const float c = cosf(m->heading_rad);
        const float s = sinf(m->heading_rad);
        const float len = m->radius * 1.6f;
        const float half_w = m->radius * 0.68f;
        const float notch = m->radius * 0.46f;
        const vg_vec2 shape[5] = {
            {m->b.x + c * len, m->b.y + s * len},
            {m->b.x - c * len * 0.56f - s * half_w, m->b.y - s * len * 0.56f + c * half_w},
            {m->b.x - c * notch, m->b.y - s * notch},
            {m->b.x - c * len * 0.56f + s * half_w, m->b.y - s * len * 0.56f - c * half_w},
            {m->b.x + c * len, m->b.y + s * len}
        };
        vg_result r = vg_fill_convex(ctx, shape, 4, &m_fill);
        if (r != VG_OK) return r;
        r = vg_draw_polyline(ctx, shape, 5, &m_halo, 1);
        if (r != VG_OK) return r;
        r = vg_draw_polyline(ctx, shape, 5, &m_main, 1);
        if (r != VG_OK) return r;
    }
    return VG_OK;
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
    const float x0 = safe.x + safe.w * 0.015f;
    const float y0 = safe.y + safe.h - 20.0f * ui;
    const float lh = 20.0f * ui;

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

static vg_result draw_terrain_tuning_overlay(
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
    return draw_text_vector_glow(
        ctx,
        text,
        (vg_vec2){14.0f * ui, h - 18.0f * ui},
        10.0f * ui,
        0.75f * ui,
        halo_style,
        main_style
    );
}

static vg_result draw_fps_overlay(
    vg_context* ctx,
    float w,
    float h,
    float fps,
    const vg_stroke_style* halo_style,
    const vg_stroke_style* main_style
) {
    char fps_text[32];
    snprintf(fps_text, sizeof(fps_text), "FPS %.1f", fps);
    const float ui = ui_reference_scale(w, h);
    return draw_text_vector_glow(
        ctx,
        fps_text,
        (vg_vec2){14.0f * ui, 24.0f * ui},
        12.0f * ui,
        0.70f * ui,
        halo_style,
        main_style
    );
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
    d.text_scale = ui * 0.82f;

    const float w = safe.w;
    const float h = safe.h;
    const float margin_x = w * 0.022f;
    const float top_margin = 46.0f * ui;
    const float meter_w = w * 0.20f;
    const float meter_h = 16.0f * ui;
    const float meter_gap = 26.0f * ui;
    const float y_quota = safe.y + h - top_margin - meter_h;
    const float y_vitality = y_quota + meter_h + meter_gap;
    const float x_block = safe.x + w - margin_x - meter_w;

    vg_result r;
    d.rect = (vg_rect){x_block, y_vitality, meter_w, meter_h};
    d.label = "VITALITY";
    d.value = ((float)g->lives / 3.0f) * 100.0f;
    r = vg_ui_meter_linear(ctx, &d, &ms);
    if (r != VG_OK) {
        return r;
    }

    d.rect = (vg_rect){x_block, y_quota, meter_w, meter_h};
    d.label = "QUOTA";
    d.min_value = 0.0f;
    d.max_value = 40.0f;
    d.mode = VG_UI_METER_SEGMENTED;
    d.segments = 20;
    d.segment_gap_px = 2.0f * ui;
    d.value_fmt = "%4.0f";
    d.value = (float)g->kills;
    r = vg_ui_meter_linear(ctx, &d, &ms);
    if (r != VG_OK) {
        return r;
    }

    if (level_uses_cylinder_render(g) && g->level_time_remaining_s > 0.0f) {
        char timer_txt[32];
        const int secs = (int)ceilf(fmaxf(g->level_time_remaining_s, 0.0f));
        snprintf(timer_txt, sizeof(timer_txt), "TIME %03d", secs);
        const float timer_size = 22.0f * ui;
        const float timer_track = 1.6f * ui;
        const float timer_w = vg_measure_text(timer_txt, timer_size, timer_track);
        const vg_vec2 timer_pos = {
            safe.x + (safe.w - timer_w) * 0.5f,
            safe.y + safe.h - 38.0f * ui
        };
        vg_stroke_style timer_halo = *halo_s;
        vg_stroke_style timer_main = *main_s;
        timer_halo.width_px = fmaxf(1.8f * ui, halo_s->width_px * 1.45f);
        timer_main.width_px = fmaxf(1.2f * ui, main_s->width_px * 1.2f);
        timer_halo.intensity = halo_s->intensity * 0.95f;
        timer_main.intensity = main_s->intensity * 1.12f;
        r = draw_text_vector_glow(ctx, timer_txt, timer_pos, timer_size, timer_track, &timer_halo, &timer_main);
        if (r != VG_OK) {
            return r;
        }
    }
    return VG_OK;
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

static vg_result draw_ui_button_shaded(
    vg_context* ctx,
    vg_rect rect,
    const char* label,
    float size_px,
    const vg_stroke_style* frame,
    const vg_stroke_style* text,
    int selected
) {
    if (!ctx || !frame || !text) {
        return VG_ERROR_INVALID_ARGUMENT;
    }
    if (selected) {
        vg_fill_style fill = {
            .intensity = 0.55f,
            .color = (vg_color){frame->color.r, frame->color.g, frame->color.b, 0.26f},
            .blend = VG_BLEND_ALPHA
        };
        vg_result fr = vg_fill_rect(ctx, rect, &fill);
        if (fr != VG_OK) {
            return fr;
        }
    }
    return vg_draw_button(ctx, rect, label, size_px, frame, text, 0);
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
    static const char* combat_enemy_labels[8] = {
        "WAVEFORM", "PITCH HZ", "ATTACK MS", "DECAY MS", "CUTOFF KHZ", "RESONANCE", "SWEEP ST", "SWEEP DECAY"
    };
    static const char* combat_exp_labels[8] = {
        "LEVEL", "PITCH HZ", "ATTACK MS", "DECAY MS", "NOISE MIX", "FM DEPTH", "FM RATE", "PAN WIDTH"
    };
    static const char* equip_shield_labels[8] = {
        "LEVEL", "PITCH HZ", "ATTACK MS", "RELEASE MS", "NOISE MIX", "FM DEPTH", "FM RATE", "CUTOFF KHZ"
    };
    static const char* equip_aux_labels[8] = {
        "LEVEL", "PITCH HZ", "ATTACK MS", "RELEASE MS", "NOISE MIX", "FM DEPTH", "FM RATE", "CUTOFF KHZ"
    };
    static const char* fx_lightning_labels[8] = {
        "LEVEL", "PITCH HZ", "ATTACK MS", "DECAY MS", "CRACKLE", "FM DEPTH", "FM RATE", "CUTOFF KHZ"
    };
    static const char* fx_pickup_labels[8] = {
        "LEVEL", "PITCH HZ", "ATTACK MS", "DECAY MS", "NOISE MIX", "FM DEPTH", "FM RATE", "CUTOFF KHZ"
    };
    const int page = metrics->acoustics_page;
    const int combat_page = (page == 1);
    const int equipment_page = (page == 2);
    const int effects_page = (page == 3);
    const int mixtape_page = (page == 4);
    const int row_count_right = (equipment_page || combat_page || effects_page) ? 8 : 6;
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

    if (mixtape_page) {
        const float v_disp = metrics->acoustics_mixtape_volume_display;
        const float values[1] = {v_disp};
        const float value_col_width_px = acoustics_compute_value_col_width(ui, 11.5f * ui, values, 1);
        const acoustics_ui_layout l = make_acoustics_ui_layout(w, h, value_col_width_px, 1, 1);
        const vg_rect page_btns[5] = {
            {l.panel[0].x, l.panel[0].y + l.panel[0].h + 10.0f * ui, l.panel[0].w * 0.14f, l.panel[0].h * 0.042f},
            {l.panel[0].x + l.panel[0].w * 0.16f, l.panel[0].y + l.panel[0].h + 10.0f * ui, l.panel[0].w * 0.18f, l.panel[0].h * 0.042f},
            {l.panel[0].x + l.panel[0].w * 0.36f, l.panel[0].y + l.panel[0].h + 10.0f * ui, l.panel[0].w * 0.20f, l.panel[0].h * 0.042f},
            {l.panel[0].x + l.panel[0].w * 0.58f, l.panel[0].y + l.panel[0].h + 10.0f * ui, l.panel[0].w * 0.18f, l.panel[0].h * 0.042f},
            {l.panel[0].x + l.panel[0].w * 0.78f, l.panel[0].y + l.panel[0].h + 10.0f * ui, l.panel[0].w * 0.19f, l.panel[0].h * 0.042f}
        };
        vg_ui_slider_item vol_item[1];
        vol_item[0].label = "MUSIC VOL";
        vol_item[0].value_01 = metrics->acoustics_mixtape_volume_01;
        vol_item[0].value_display = v_disp;
        vol_item[0].selected = (metrics->acoustics_mixtape_selected == 0) ? 1 : 0;
        const vg_ui_slider_panel_metrics sm = acoustics_scaled_slider_metrics(ui, l.value_col_width_px);
        vg_ui_slider_panel_desc left = {
            .rect = l.panel[0],
            .title_line_0 = "SHIPYARD ACOUSTICS - MIXTAPE",
            .title_line_1 = "UP DOWN SELECT  LEFT RIGHT FOCUS  FIRE ADD REMOVE",
            .footer_line = NULL,
            .items = vol_item,
            .item_count = 1u,
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
        vg_result r = vg_ui_draw_slider_panel(ctx, &left);
        if (r != VG_OK) {
            return r;
        }
        r = vg_fill_rect(ctx, l.panel[1], &trace_panel_fill);
        if (r != VG_OK) {
            return r;
        }
        {
            const vg_svg_asset* tape_svg = (const vg_svg_asset*)metrics->acoustics_tape_svg_asset;
            if (tape_svg) {
                vg_svg_draw_params sdp;
                memset(&sdp, 0, sizeof(sdp));
                sdp.dst = l.panel[1];
                sdp.preserve_aspect = 1;
                sdp.flip_y = 1;
                sdp.fill_closed_paths = 1;
                sdp.use_source_colors = 0;
                sdp.fill_intensity = 0.13f;
                sdp.stroke_intensity = 0.25f;
                sdp.use_context_palette = 0;
                r = vg_transform_push(ctx);
                if (r != VG_OK) {
                    return r;
                }
                vg_transform_translate(ctx, l.panel[1].x + l.panel[1].w * 0.5f, l.panel[1].y + l.panel[1].h * 0.5f);
                vg_transform_rotate(ctx, 3.14159265f);
                vg_transform_translate(ctx, -(l.panel[1].x + l.panel[1].w * 0.5f), -(l.panel[1].y + l.panel[1].h * 0.5f));
                r = vg_svg_draw(ctx, tape_svg, &sdp, &panel);
                {
                    vg_result pop_r = vg_transform_pop(ctx);
                    if (r == VG_OK && pop_r != VG_OK) {
                        r = pop_r;
                    }
                }
                if (r != VG_OK) {
                    return r;
                }
            }
        }
        r = vg_draw_rect(ctx, l.panel[1], &panel);
        if (r != VG_OK) {
            return r;
        }
        {
            const char* top_left = "LIBRARY";
            const char* top_right = "PLAYLIST";
            vg_stroke_style title_frame = panel;
            vg_stroke_style title_text = text;
            title_frame.width_px = 1.85f * ui;
            title_frame.intensity = 1.22f;
            title_text.width_px = 1.60f * ui;
            title_text.intensity = 1.28f;
            title_text.color = (vg_color){
                fminf(1.0f, pal.secondary.r * 1.08f),
                fminf(1.0f, pal.secondary.g * 1.08f),
                fminf(1.0f, pal.secondary.b * 1.08f),
                1.0f
            };
            const float top_margin = 30.0f * ui;
            const float side_margin = 30.0f * ui;
            const float title_y_left = l.panel[0].y + l.panel[0].h - top_margin;
            const float title_y_right = l.panel[1].y + l.panel[1].h - top_margin;
            r = draw_text_vector_glow(ctx, top_left, (vg_vec2){l.panel[0].x + side_margin, title_y_left},
                14.8f * ui, 1.02f * ui, &title_frame, &title_text);
            if (r != VG_OK) return r;
            r = draw_text_vector_glow(ctx, top_right, (vg_vec2){l.panel[1].x + side_margin, title_y_right},
                14.8f * ui, 1.02f * ui, &title_frame, &title_text);
            if (r != VG_OK) return r;
        }
        {
            const float row_h = 19.0f * ui;
            const float list_size = 12.2f * ui;
            const float list_spacing = 0.90f * ui;
            const float side_margin = 30.0f * ui;
            const float list_top_y = l.panel[0].y + l.panel[0].h - 74.0f * ui;
            const float list_bot_y = l.panel[0].y + 30.0f * ui;
            const int max_rows = (int)((list_top_y - list_bot_y) / row_h);
            int start_left = 0;
            int start_right = 0;
            if (metrics->acoustics_mixtape_track_index >= max_rows && max_rows > 0) {
                start_left = metrics->acoustics_mixtape_track_index - (max_rows - 1);
            }
            if (metrics->acoustics_mixtape_playlist_selected >= max_rows && max_rows > 0) {
                start_right = metrics->acoustics_mixtape_playlist_selected - (max_rows - 1);
            }
            for (int row = 0; row < max_rows; ++row) {
                const float y = list_top_y - (float)row * row_h;
                const int li = start_left + row;
                if (li < metrics->acoustics_mixtape_track_count && li < MIXTAPE_MAX_TRACKS) {
                    vg_stroke_style lt = text;
                    vg_stroke_style lp = panel;
                    if (li == metrics->acoustics_mixtape_track_index) {
                        lt.intensity = 1.20f;
                        lp.intensity = 1.15f;
                    } else {
                        lt.intensity = 0.82f;
                        lp.intensity = 0.78f;
                    }
                    r = draw_text_vector_glow(ctx, metrics->acoustics_mixtape_track_labels[li], (vg_vec2){l.panel[0].x + side_margin, y},
                        list_size, list_spacing, &lp, &lt);
                    if (r != VG_OK) return r;
                }
                const int ri = start_right + row;
                if (ri < metrics->acoustics_mixtape_playlist_count) {
                    const int track_idx = metrics->acoustics_mixtape_playlist_indices[ri];
                    if (track_idx >= 0 && track_idx < metrics->acoustics_mixtape_track_count && track_idx < MIXTAPE_MAX_TRACKS) {
                        vg_stroke_style rt = text;
                        vg_stroke_style rp = panel;
                        if (ri == metrics->acoustics_mixtape_playlist_selected) {
                            rt.intensity = 1.20f;
                            rp.intensity = 1.15f;
                        } else {
                            rt.intensity = 0.82f;
                            rp.intensity = 0.78f;
                        }
                        r = draw_text_vector_glow(ctx, metrics->acoustics_mixtape_track_labels[track_idx], (vg_vec2){l.panel[1].x + side_margin, y},
                            list_size, list_spacing, &rp, &rt);
                        if (r != VG_OK) return r;
                    }
                }
            }
        }
        if (metrics->acoustics_mixtape_drag_active) {
            const int target = metrics->acoustics_mixtape_drag_target;
            if (target == 1 || target == 2) {
                const vg_rect zone = (target == 1) ? l.panel[0] : l.panel[1];
                const vg_rect ghost = {
                    zone.x + 5.0f * ui,
                    zone.y + 5.0f * ui,
                    zone.w - 10.0f * ui,
                    zone.h - 10.0f * ui
                };
                vg_fill_style ghost_fill = {
                    .intensity = 0.30f,
                    .color = (vg_color){pal.primary.r, pal.primary.g, pal.primary.b, 0.12f},
                    .blend = VG_BLEND_ALPHA
                };
                vg_stroke_style ghost_stroke = panel;
                ghost_stroke.width_px = 1.0f * ui;
                ghost_stroke.intensity = 1.12f;
                r = vg_fill_rect(ctx, ghost, &ghost_fill);
                if (r != VG_OK) return r;
                r = vg_draw_rect(ctx, ghost, &ghost_stroke);
                if (r != VG_OK) return r;
            }
        }
        {
            const vg_rect randomize_btn = {
                l.panel[1].x + l.panel[1].w * 0.52f,
                l.panel[1].y + 12.0f * ui,
                l.panel[1].w * 0.40f,
                26.0f * ui
            };
            if (metrics->acoustics_mixtape_randomize) {
                vg_fill_style on_fill = {
                    .intensity = 0.55f,
                    .color = (vg_color){pal.primary_dim.r, pal.primary_dim.g, pal.primary_dim.b, 0.30f},
                    .blend = VG_BLEND_ALPHA
                };
                r = vg_fill_rect(ctx, randomize_btn, &on_fill);
                if (r != VG_OK) return r;
            }
            r = draw_ui_button_shaded(
                ctx,
                randomize_btn,
                "RANDOM",
                10.2f * ui,
                &panel,
                &text,
                0
            );
            if (r != VG_OK) return r;
        }
        for (int i = 0; i < 5; ++i) {
            static const char* page_labels[5] = {"SHIP", "COMBAT", "EQUIPMENT", "EFFECTS", "MIXTAPE"};
            r = draw_ui_button_shaded(ctx, page_btns[i], page_labels[i], 10.8f * ui, &panel, &text, (page == i) ? 1 : 0);
            if (r != VG_OK) return r;
        }
        return VG_OK;
    }

    vg_ui_slider_item fire_items[8];
    vg_ui_slider_item thr_items[8];
    if (combat_page) {
        for (int i = 0; i < 8; ++i) {
            fire_items[i].label = combat_enemy_labels[i];
            fire_items[i].value_01 = metrics->acoustics_combat_value_01[i];
            fire_items[i].value_display = metrics->acoustics_combat_display[i];
            fire_items[i].selected = (metrics->acoustics_combat_selected == i) ? 1 : 0;
        }
        for (int i = 0; i < 8; ++i) {
            thr_items[i].label = combat_exp_labels[i];
            thr_items[i].value_01 = metrics->acoustics_combat_value_01[8 + i];
            thr_items[i].value_display = metrics->acoustics_combat_display[8 + i];
            thr_items[i].selected = (metrics->acoustics_combat_selected == (8 + i)) ? 1 : 0;
        }
    } else if (!equipment_page && !effects_page) {
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
    } else {
        const float* v01 = effects_page ? metrics->acoustics_effects_value_01 : metrics->acoustics_equipment_value_01;
        const float* disp = effects_page ? metrics->acoustics_effects_display : metrics->acoustics_equipment_display;
        const int selected = effects_page ? metrics->acoustics_effects_selected : metrics->acoustics_equipment_selected;
        const char** left_labels = effects_page ? fx_lightning_labels : equip_shield_labels;
        const char** right_labels = effects_page ? fx_pickup_labels : equip_aux_labels;
        for (int i = 0; i < 8; ++i) {
            fire_items[i].label = left_labels[i];
            fire_items[i].value_01 = v01[i];
            fire_items[i].value_display = disp[i];
            fire_items[i].selected = (selected == i) ? 1 : 0;
        }
        for (int i = 0; i < 8; ++i) {
            thr_items[i].label = right_labels[i];
            thr_items[i].value_01 = v01[8 + i];
            thr_items[i].value_display = disp[8 + i];
            thr_items[i].selected = (selected == (8 + i)) ? 1 : 0;
        }
    }

    const float value_col_width_px = acoustics_compute_value_col_width(
        ui,
        11.5f * ui,
        combat_page ? metrics->acoustics_combat_display
                    : (equipment_page ? metrics->acoustics_equipment_display
                                      : (effects_page ? metrics->acoustics_effects_display : metrics->acoustics_display)),
        combat_page ? ACOUSTICS_COMBAT_SLIDER_COUNT
                    : (equipment_page ? ACOUSTICS_EQUIPMENT_SLIDER_COUNT
                                      : (effects_page ? ACOUSTICS_EFFECTS_SLIDER_COUNT : ACOUSTICS_SLIDER_COUNT))
    );
    const acoustics_ui_layout l = make_acoustics_ui_layout(w, h, value_col_width_px, 8, row_count_right);
    const vg_rect page_btns[5] = {
        {l.panel[0].x, l.panel[0].y + l.panel[0].h + 10.0f * ui, l.panel[0].w * 0.14f, l.panel[0].h * 0.042f},
        {l.panel[0].x + l.panel[0].w * 0.16f, l.panel[0].y + l.panel[0].h + 10.0f * ui, l.panel[0].w * 0.18f, l.panel[0].h * 0.042f},
        {l.panel[0].x + l.panel[0].w * 0.36f, l.panel[0].y + l.panel[0].h + 10.0f * ui, l.panel[0].w * 0.20f, l.panel[0].h * 0.042f},
        {l.panel[0].x + l.panel[0].w * 0.58f, l.panel[0].y + l.panel[0].h + 10.0f * ui, l.panel[0].w * 0.18f, l.panel[0].h * 0.042f},
        {l.panel[0].x + l.panel[0].w * 0.78f, l.panel[0].y + l.panel[0].h + 10.0f * ui, l.panel[0].w * 0.19f, l.panel[0].h * 0.042f}
    };
    const vg_rect fire_rect = l.panel[0];
    const vg_rect thr_rect = l.panel[1];
    const vg_rect fire_btn = l.button[0];
    const vg_rect thr_btn = l.button[1];
    const vg_rect fire_save_btn = l.save_button[0];
    const vg_rect thr_save_btn = l.save_button[1];
    const vg_ui_slider_panel_metrics sm = acoustics_scaled_slider_metrics(ui, l.value_col_width_px);

    vg_ui_slider_panel_desc fire = {
        .rect = fire_rect,
        .title_line_0 = effects_page
                            ? "SHIPYARD ACOUSTICS - LIGHTNING"
                            : (equipment_page ? "SHIPYARD ACOUSTICS - SHIELD"
                                              : (combat_page ? "SHIPYARD ACOUSTICS - ENEMY FIRE"
                                                             : "SHIPYARD ACOUSTICS - FIRE")),
        .title_line_1 = "Q/E OR PAGE BUTTONS  ARROWS OR MOUSE TO TUNE",
        .footer_line = NULL,
        .items = fire_items,
        .item_count = 8u,
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
    thr.title_line_0 = effects_page
                           ? "SHIPYARD ACOUSTICS - PICKUP FX"
                           : (equipment_page ? "SHIPYARD ACOUSTICS - EMP"
                                             : (combat_page ? "SHIPYARD ACOUSTICS - EXPLOSION"
                                                            : "SHIPYARD ACOUSTICS - THRUST"));
    thr.items = thr_items;
    thr.item_count = (size_t)row_count_right;

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

    for (int i = 0; i < 5; ++i) {
        static const char* page_labels[5] = {"SHIP", "COMBAT", "EQUIPMENT", "EFFECTS", "MIXTAPE"};
        r = draw_ui_button_shaded(
            ctx,
            page_btns[i],
            page_labels[i],
            10.8f * ui,
            &panel,
            &text,
            (page == i) ? 1 : 0
        );
        if (r != VG_OK) {
            return r;
        }
    }

    {
        const char* fire_test = effects_page
                                    ? "LIGHTNING"
                                    : (equipment_page ? "TEST SHIELD" : (combat_page ? "TEST ENEMY" : "TEST FIRE"));
        const char* thr_test = effects_page
                                   ? "TEST PICKUP"
                                   : (equipment_page ? "TEST EMP" : (combat_page ? "TEST BOOM" : "TEST THRUST"));
        r = draw_ui_button_shaded(ctx, fire_btn, fire_test, 11.5f * ui, &panel, &text, 0);
        if (r != VG_OK) {
            return r;
        }
        r = draw_ui_button_shaded(ctx, thr_btn, thr_test, 11.5f * ui, &panel, &text, 0);
        if (r != VG_OK) {
            return r;
        }
        r = draw_ui_button_shaded(ctx, fire_save_btn, "SAVE", 11.0f * ui, &panel, &text, 0);
        if (r != VG_OK) {
            return r;
        }
        r = draw_ui_button_shaded(ctx, thr_save_btn, "SAVE", 11.0f * ui, &panel, &text, 0);
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
            r = draw_ui_button_shaded(
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
            r = draw_ui_button_shaded(
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
            effects_page
                ? "LIGHTNING PREVIEW"
                : (equipment_page ? "SHIELD PREVIEW" : (combat_page ? "ENEMY SHOT PREVIEW" : "ENV + PITCH SWEEP")),
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
        const float* fire_preview = combat_page
                                        ? metrics->acoustics_combat_display
                                        : (equipment_page
                                               ? metrics->acoustics_equipment_display
                                               : (effects_page ? metrics->acoustics_effects_display : metrics->acoustics_display));
        const float a_ms = fire_preview[2];
        const float d_ms = fire_preview[3] + (equipment_page ? fire_preview[3] : 0.0f);
        const float sweep_st = effects_page ? 0.0f : (equipment_page ? (fire_preview[5] / 60.0f) : fire_preview[6]);
        const float sweep_d_ms = effects_page ? 220.0f : (equipment_page ? (1000.0f / fmaxf(0.1f, fire_preview[6])) : fire_preview[7]);
        const float fx_fm_depth = effects_page ? fire_preview[5] : 0.0f;
        const float fx_fm_rate = effects_page ? fire_preview[6] : 0.0f;
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
            if (effects_page) {
                const float depth01 = clampf(fx_fm_depth / 960.0f, 0.0f, 1.0f);
                const float rate01 = clampf(fx_fm_rate / 3000.0f, 0.0f, 1.0f);
                const float cyc = 1.0f + 6.0f * rate01;
                p = 0.5f + sinf(t * cyc * 6.28318530718f) * (0.06f + 0.26f * depth01);
            }
            p = clampf(p, 0.04f, 0.96f);
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
            equipment_page ? "EMP PREVIEW" : (combat_page ? "EXPLOSION PREVIEW" : "OSCILLOSCOPE"),
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

        if (equipment_page) {
            enum { AUX_TRACE_SAMPLES = 96 };
            vg_vec2 aux_line[AUX_TRACE_SAMPLES];
            const float* aux_preview = &metrics->acoustics_equipment_display[8];
            const float rate_hz = fmaxf(0.10f, aux_preview[6]);
            const float noise = clampf(aux_preview[4], 0.0f, 1.0f);
            for (int i = 0; i < AUX_TRACE_SAMPLES; ++i) {
                const float t = (float)i / (float)(AUX_TRACE_SAMPLES - 1);
                const float x = thr_display.x + 8.0f * ui + (thr_display.w - 16.0f * ui) * t;
                const float lfo = sinf(6.2831853f * rate_hz * t);
                const float y_norm = 0.5f + 0.32f * lfo * (1.0f - 0.5f * noise);
                aux_line[i] = (vg_vec2){x, thr_display.y + 8.0f * ui + y_norm * (thr_display.h - 20.0f * ui)};
            }
            r = draw_beam_trace(
                ctx,
                aux_line,
                AUX_TRACE_SAMPLES,
                &text,
                (vg_color){0.55f, 1.0f, 1.0f, 1.0f},
                1.45f * ui,
                1.08f
            );
            if (r != VG_OK) {
                return r;
            }
        } else {
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

static vg_result draw_lcars_text_button(
    vg_context* ctx,
    vg_rect rect,
    const char* label,
    int hover,
    int selected,
    float ui,
    const palette_theme* pal,
    const vg_stroke_style* frame,
    const vg_stroke_style* txt
) {
    vg_fill_style base = make_fill(
        selected ? 0.42f : (hover ? 0.34f : 0.22f),
        pal->primary_dim,
        VG_BLEND_ALPHA
    );
    vg_result r = vg_fill_rect(ctx, rect, &base);
    if (r != VG_OK) {
        return r;
    }
    vg_stroke_style lcars = *frame;
    lcars.width_px = 1.1f * ui;
    lcars.intensity = selected ? 1.3f : (hover ? 1.1f : 0.8f);
    {
        const vg_vec2 stripe[2] = {
            {rect.x + rect.w * 0.02f, rect.y + rect.h * 0.06f},
            {rect.x + rect.w * 0.98f, rect.y + rect.h * 0.06f}
        };
        r = vg_draw_polyline(ctx, stripe, 2, &lcars, 0);
        if (r != VG_OK) {
            return r;
        }
    }
    return draw_text_vector_glow(
        ctx,
        label,
        (vg_vec2){rect.x + rect.w * 0.08f, rect.y + rect.h * 0.34f},
        14.0f * ui,
        1.0f * ui,
        frame,
        txt
    );
}

static vg_result draw_shipyard_menu(vg_context* ctx, float w, float h, const render_metrics* metrics) {
    const float ui = ui_reference_scale(w, h);
    const vg_rect panel = make_ui_safe_frame(w, h);
    const palette_theme pal = get_palette_theme(metrics->palette_mode);
    vg_stroke_style frame = make_stroke(1.6f * ui, 1.0f, pal.primary, VG_BLEND_ALPHA);
    vg_stroke_style txt = make_stroke(1.2f * ui, 1.0f, pal.secondary, VG_BLEND_ALPHA);
    vg_fill_style haze = make_fill(0.20f, pal.haze, VG_BLEND_ALPHA);
    vg_result r = vg_fill_rect(ctx, panel, &haze);
    if (r != VG_OK) {
        return r;
    }

    r = draw_text_vector_glow(
        ctx,
        "SHIPYARD",
        (vg_vec2){panel.x + panel.w * 0.04f, panel.y + panel.h * 0.92f},
        20.0f * ui,
        1.4f * ui,
        &frame,
        &txt
    );
    if (r != VG_OK) {
        return r;
    }
    r = draw_text_vector_glow(
        ctx,
        "ESC BACK",
        (vg_vec2){panel.x + panel.w * 0.04f, panel.y + panel.h * 0.875f},
        9.8f * ui,
        0.8f * ui,
        &frame,
        &txt
    );
    if (r != VG_OK) {
        return r;
    }

    const vg_rect links_box = {panel.x + panel.w * 0.005f, panel.y + panel.h * 0.17f, panel.w * 0.19f, panel.h * 0.66f};
    const vg_rect ship_box = {panel.x + panel.w * 0.27f, panel.y + panel.h * 0.18f, panel.w * 0.52f, panel.h * 0.64f};
    const vg_rect weap_box = {panel.x + panel.w * 0.855f, panel.y + panel.h * 0.18f, panel.w * 0.13f, panel.h * 0.64f};

    {
        static const char* labels[4] = {
            "ACOUSTICS",
            "DISPLAY",
            "PLANETARIUM",
            "CONTROLS"
        };
        const float btn_h = links_box.h * 0.20f;
        const float btn_gap = links_box.h * 0.05f;
        float by = links_box.y + links_box.h - btn_h;
        for (int i = 0; i < 4; ++i) {
            vg_rect b = {links_box.x, by, links_box.w, btn_h};
            const int hover =
                metrics->mouse_in_window &&
                metrics->mouse_x >= b.x && metrics->mouse_x <= (b.x + b.w) &&
                metrics->mouse_y >= b.y && metrics->mouse_y <= (b.y + b.h);
            const int nav_selected =
                (metrics->shipyard_nav_column == 0 && metrics->shipyard_link_selected == i) ? 1 : 0;
            r = draw_lcars_text_button(ctx, b, labels[i], hover, nav_selected, ui, &pal, &frame, &txt);
            if (r != VG_OK) {
                return r;
            }
            by -= (btn_h + btn_gap);
        }
    }

    {
        const vg_svg_asset* ship_svg = (const vg_svg_asset*)metrics->shipyard_ship_svg_asset;
        if (ship_svg) {
            vg_svg_draw_params sdp;
            memset(&sdp, 0, sizeof(sdp));
            sdp.dst = ship_box;
            sdp.preserve_aspect = 1;
            sdp.flip_y = 1;
            sdp.fill_closed_paths = 1;
            sdp.use_source_colors = 0;
            sdp.fill_intensity = 0.82f;
            sdp.stroke_intensity = 1.0f;
            sdp.use_context_palette = 0;
            r = vg_svg_draw(ctx, ship_svg, &sdp, &frame);
            if (r != VG_OK) {
                return r;
            }
        }
    }
    {
        static const char* wlabels[4] = {"SHIELD", "MISSILE", "EMP", "REAR GUN"};
        const float icon_h = weap_box.h * 0.21f;
        const float icon_gap = weap_box.h * 0.05f;
        float y = weap_box.y + weap_box.h - icon_h;
        for (int i = 0; i < 4; ++i) {
            const vg_rect d = {weap_box.x, y, weap_box.w, icon_h};
            const vg_svg_asset* weapon_svg = (const vg_svg_asset*)metrics->shipyard_weapon_svg_assets[i];
            const int hover =
                metrics->mouse_in_window &&
                metrics->mouse_x >= d.x && metrics->mouse_x <= (d.x + d.w) &&
                metrics->mouse_y >= d.y && metrics->mouse_y <= (d.y + d.h);
            const int selected = (metrics->shipyard_weapon_selected == i) ? 1 : 0;
            vg_fill_style bg = make_fill(selected ? 0.36f : (hover ? 0.25f : 0.14f), pal.primary_dim, VG_BLEND_ALPHA);
            r = vg_fill_rect(ctx, d, &bg);
            if (r != VG_OK) {
                return r;
            }
            {
                vg_stroke_style slot = frame;
                slot.width_px = 1.3f * ui;
                slot.intensity = selected ? 1.25f : (hover ? 1.0f : 0.72f);
                r = vg_draw_rect(ctx, d, &slot);
                if (r != VG_OK) {
                    return r;
                }
            }
            if (weapon_svg) {
                vg_svg_draw_params sdp;
                memset(&sdp, 0, sizeof(sdp));
                sdp.dst = (vg_rect){d.x + d.w * 0.34f, d.y + d.h * 0.10f, d.w * 0.60f, d.h * 0.80f};
                sdp.preserve_aspect = 1;
                sdp.flip_y = 1;
                sdp.fill_closed_paths = 1;
                sdp.use_source_colors = 0;
                sdp.fill_intensity = selected ? 0.96f : 0.70f;
                sdp.stroke_intensity = selected ? 1.10f : 0.90f;
                sdp.use_context_palette = 0;
                r = vg_svg_draw(ctx, weapon_svg, &sdp, &frame);
                if (r != VG_OK) {
                    return r;
                }
            }
            r = draw_text_vector_glow(
                ctx,
                wlabels[i],
                (vg_vec2){d.x + d.w * 0.05f, d.y + d.h * 0.08f},
                9.0f * ui,
                0.7f * ui,
                &frame,
                &txt
            );
            if (r != VG_OK) {
                return r;
            }
            {
                char ammo[24];
                snprintf(ammo, sizeof(ammo), "%d", metrics->shipyard_weapon_ammo[i]);
                r = draw_text_vector_glow(
                    ctx,
                    ammo,
                    (vg_vec2){d.x + d.w * 0.05f, d.y + d.h * 0.22f},
                    10.2f * ui,
                    0.78f * ui,
                    &frame,
                    &txt
                );
                if (r != VG_OK) {
                    return r;
                }
            }
            y -= (icon_h + icon_gap);
        }
    }
    return VG_OK;
}

static vg_result draw_opening_menu(vg_context* ctx, float w, float h, const render_metrics* metrics) {
    const float ui = ui_reference_scale(w, h);
    const vg_rect panel = make_ui_safe_frame(w, h);
    const palette_theme pal = get_palette_theme(metrics->palette_mode);
    vg_stroke_style frame = make_stroke(1.6f * ui, 1.0f, pal.primary, VG_BLEND_ALPHA);
    vg_stroke_style txt = make_stroke(1.2f * ui, 1.0f, pal.secondary, VG_BLEND_ALPHA);
    vg_fill_style haze = make_fill(0.16f, pal.haze, VG_BLEND_ALPHA);
    vg_result r = vg_fill_rect(ctx, panel, &haze);
    if (r != VG_OK) {
        return r;
    }

    {
        const char* title = "VECTOR SWARM";
        const float title_size = 60.0f * ui;
        const float title_spacing = 2.55f * ui;
        const float title_w = vg_measure_text(title, title_size, title_spacing);
        const float tx = (w - title_w) * 0.5f;
        const float ty = panel.y + panel.h * 0.90f;
        const float sweep_margin = 240.0f * ui;
        const float sweep_span = title_w + sweep_margin * 2.0f;
        float sweep_t = fmodf(metrics->ui_time_s * 0.22f, 1.0f);
        if (sweep_t < 0.0f) {
            sweep_t += 1.0f;
        }
        const float sweep_x = (tx - sweep_margin) + sweep_span * sweep_t;
        const float sweep_half = 190.0f * ui;
        const float sweep_half_blur = 265.0f * ui;
        const float sweep_y = ty - 14.0f * ui;
        const float sweep_h = 80.0f * ui;

        /* Keep title mostly hidden outside the sweep. */
        {
            vg_stroke_style latent_frame = frame;
            vg_stroke_style latent_text = txt;
            latent_frame.width_px = 1.6f * ui;
            latent_text.width_px = 1.1f * ui;
            latent_frame.intensity = 0.07f;
            latent_text.intensity = 0.06f;
            latent_frame.color.a = 0.18f;
            latent_text.color.a = 0.16f;
            r = draw_text_vector_glow(
                ctx,
                title,
                (vg_vec2){tx, ty},
                title_size,
                title_spacing,
                &latent_frame,
                &latent_text
            );
            if (r != VG_OK) {
                return r;
            }
        }

        /* Sweeping reveal with symmetric falloff and a broad blur layer. */
        {
            const int slices = 30;
            const float clip_w = (2.0f * sweep_half) / (float)slices;
            for (int i = 0; i < slices; ++i) {
                const float u = ((float)i + 0.5f) / (float)slices * 2.0f - 1.0f;
                const float glow = expf(-u * u * 2.4f);
                const float core = expf(-u * u * 14.0f);
                const vg_rect clip = {
                    (sweep_x - sweep_half) + (float)i * clip_w,
                    sweep_y,
                    clip_w + 1.0f,
                    sweep_h
                };
                r = vg_clip_push_rect(ctx, clip);
                if (r != VG_OK) {
                    return r;
                }
                {
                    vg_stroke_style blur_frame = frame;
                    vg_stroke_style blur_text = txt;
                    blur_frame.width_px = 4.8f * ui;
                    blur_text.width_px = 3.4f * ui;
                    blur_frame.intensity = 0.30f * glow;
                    blur_text.intensity = 0.26f * glow;
                    blur_frame.color.a = 0.35f * glow;
                    blur_text.color.a = 0.30f * glow;
                    r = draw_text_vector_glow(
                        ctx,
                        title,
                        (vg_vec2){tx, ty},
                        title_size,
                        title_spacing,
                        &blur_frame,
                        &blur_text
                    );
                }
                if (r == VG_OK) {
                    vg_stroke_style lit_frame = frame;
                    vg_stroke_style lit_text = txt;
                    lit_frame.width_px = 2.1f * ui;
                    lit_text.width_px = 1.45f * ui;
                    lit_frame.intensity = 0.45f * glow + 1.35f * core;
                    lit_text.intensity = 0.36f * glow + 1.10f * core;
                    lit_frame.color.a = 0.60f * glow + 0.30f * core;
                    lit_text.color.a = 0.52f * glow + 0.38f * core;
                    lit_text.color = (vg_color){
                        fminf(1.0f, pal.secondary.r * 1.15f),
                        fminf(1.0f, pal.secondary.g * 1.15f),
                        fminf(1.0f, pal.secondary.b * 1.15f),
                        lit_text.color.a
                    };
                    r = draw_text_vector_glow(
                        ctx,
                        title,
                        (vg_vec2){tx, ty},
                        title_size,
                        title_spacing,
                        &lit_frame,
                        &lit_text
                    );
                }
                {
                    vg_result pop_r = vg_clip_pop(ctx);
                    if (r == VG_OK && pop_r != VG_OK) {
                        r = pop_r;
                    }
                }
                if (r != VG_OK) {
                    return r;
                }
            }
        }

        /* Extra wide blur around the sweep center for strong glow bloom feel. */
        {
            const int slices = 18;
            const float clip_w = (2.0f * sweep_half_blur) / (float)slices;
            for (int i = 0; i < slices; ++i) {
                const float u = ((float)i + 0.5f) / (float)slices * 2.0f - 1.0f;
                const float glow = expf(-u * u * 4.2f);
                if (glow < 0.02f) {
                    continue;
                }
                const vg_rect clip = {
                    (sweep_x - sweep_half_blur) + (float)i * clip_w,
                    sweep_y - 5.0f * ui,
                    clip_w + 1.0f,
                    sweep_h + 10.0f * ui
                };
                r = vg_clip_push_rect(ctx, clip);
                if (r != VG_OK) {
                    return r;
                }
                vg_stroke_style bloom_frame = frame;
                vg_stroke_style bloom_text = txt;
                bloom_frame.width_px = 7.4f * ui;
                bloom_text.width_px = 5.6f * ui;
                bloom_frame.intensity = 0.14f * glow;
                bloom_text.intensity = 0.11f * glow;
                bloom_frame.color.a = 0.24f * glow;
                bloom_text.color.a = 0.22f * glow;
                r = draw_text_vector_glow(
                    ctx,
                    title,
                    (vg_vec2){tx, ty},
                    title_size,
                    title_spacing,
                    &bloom_frame,
                    &bloom_text
                );
                {
                    vg_result pop_r = vg_clip_pop(ctx);
                    if (r == VG_OK && pop_r != VG_OK) {
                        r = pop_r;
                    }
                }
                if (r != VG_OK) {
                    return r;
                }
            }
        }
    }

    const vg_rect links_box = {panel.x + panel.w * 0.005f, panel.y + panel.h * 0.17f, panel.w * 0.19f, panel.h * 0.66f};
    const vg_rect ship_box = {panel.x + panel.w * 0.24f, panel.y + panel.h * 0.10f, panel.w * 0.74f, panel.h * 0.76f};
    {
        static const char* labels[5] = {"ARCADE", "CAMPAIGN", "DISPLAY", "ACOUSTICS", "CONTROLS"};
        const float btn_h = links_box.h * 0.165f;
        const float btn_gap = links_box.h * 0.045f;
        float by = links_box.y + links_box.h - btn_h;
        for (int i = 0; i < 5; ++i) {
            const vg_rect b = {links_box.x, by, links_box.w, btn_h};
            const int hover =
                metrics->mouse_in_window &&
                metrics->mouse_x >= b.x && metrics->mouse_x <= (b.x + b.w) &&
                metrics->mouse_y >= b.y && metrics->mouse_y <= (b.y + b.h);
            const int selected = (metrics->opening_menu_selected == i) ? 1 : 0;
            r = draw_lcars_text_button(ctx, b, labels[i], hover, selected, ui, &pal, &frame, &txt);
            if (r != VG_OK) {
                return r;
            }
            by -= (btn_h + btn_gap);
        }
    }

    if (metrics->opening_ship_positions_xyz &&
        metrics->opening_ship_edges &&
        metrics->opening_ship_vertex_count > 0u &&
        metrics->opening_ship_edge_count > 0u) {
        const float* p = metrics->opening_ship_positions_xyz;
        float min_x = p[0], max_x = p[0];
        float min_y = p[1], max_y = p[1];
        float min_z = p[2], max_z = p[2];
        for (uint32_t i = 1u; i < metrics->opening_ship_vertex_count; ++i) {
            const float x = p[i * 3u + 0u];
            const float y = p[i * 3u + 1u];
            const float z = p[i * 3u + 2u];
            if (x < min_x) min_x = x;
            if (x > max_x) max_x = x;
            if (y < min_y) min_y = y;
            if (y > max_y) max_y = y;
            if (z < min_z) min_z = z;
            if (z > max_z) max_z = z;
        }
        const float cx = 0.5f * (min_x + max_x);
        const float cy = 0.5f * (min_y + max_y);
        const float cz = 0.5f * (min_z + max_z);
        const float ex = fmaxf(0.001f, max_x - min_x);
        const float ey = fmaxf(0.001f, max_y - min_y);
        const float ez = fmaxf(0.001f, max_z - min_z);
        const float model_scale = 1.0f / fmaxf(ex, fmaxf(ey, ez));
        const float deg_to_rad = 0.01745329252f;
        float yaw = metrics->opening_ship_yaw_deg * deg_to_rad;
        float pitch = metrics->opening_ship_pitch_deg * deg_to_rad;
        float roll = metrics->opening_ship_roll_deg * deg_to_rad;
        const float spin = metrics->ui_time_s * metrics->opening_ship_spin_rate;
        if (metrics->opening_ship_spin_axis == 1) {
            pitch += spin;
        } else if (metrics->opening_ship_spin_axis == 2) {
            roll += spin;
        } else {
            yaw += spin;
        }
        const float cyaw = cosf(yaw);
        const float syaw = sinf(yaw);
        const float cp = cosf(pitch);
        const float sp = sinf(pitch);
        const float cr = cosf(roll);
        const float sr = sinf(roll);
        const float focal = 1.8f;
        const float z_bias = 2.9f;
        const float draw_scale = fminf(ship_box.w, ship_box.h) * 0.84f * fmaxf(0.01f, metrics->opening_ship_scale);
        const float sx = ship_box.x + ship_box.w * 0.54f;
        const float sy = ship_box.y + ship_box.h * 0.52f;

        vg_vec2* screen = (vg_vec2*)malloc(sizeof(vg_vec2) * (size_t)metrics->opening_ship_vertex_count);
        if (!screen) {
            return VG_ERROR_OUT_OF_MEMORY;
        }
        for (uint32_t i = 0u; i < metrics->opening_ship_vertex_count; ++i) {
            const float lx = (p[i * 3u + 0u] - cx) * model_scale;
            const float ly = (p[i * 3u + 1u] - cy) * model_scale;
            const float lz = (p[i * 3u + 2u] - cz) * model_scale;
            const float x1 = lx * cyaw - lz * syaw;
            const float z1 = lx * syaw + lz * cyaw;
            const float y2 = ly * cp - z1 * sp;
            const float z2 = ly * sp + z1 * cp;
            const float xr = x1 * cr - y2 * sr;
            const float yr = x1 * sr + y2 * cr;
            const float zr2 = z2 + z_bias;
            const float invz = (zr2 > 0.1f) ? (1.0f / zr2) : 10.0f;
            screen[i].x = sx + xr * focal * invz * draw_scale;
            screen[i].y = sy + yr * focal * invz * draw_scale;
        }

        vg_stroke_style wire = frame;
        wire.width_px = fmaxf(1.2f, 1.5f * ui);
        wire.intensity = 1.1f;
        wire.blend = VG_BLEND_ALPHA;
        wire.color = pal.primary;
        for (uint32_t i = 0u; i < metrics->opening_ship_edge_count; ++i) {
            const uint32_t i0 = metrics->opening_ship_edges[i * 2u + 0u];
            const uint32_t i1 = metrics->opening_ship_edges[i * 2u + 1u];
            if (i0 >= metrics->opening_ship_vertex_count || i1 >= metrics->opening_ship_vertex_count) {
                continue;
            }
            const vg_vec2 seg[2] = {screen[i0], screen[i1]};
            r = vg_draw_polyline(ctx, seg, 2u, &wire, 0);
            if (r != VG_OK) {
                free(screen);
                return r;
            }
        }
        free(screen);
    }
    return VG_OK;
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
        "UP/DOWN SELECT  ENTER APPLY  ESC BACK",
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
            const int hover =
                metrics->mouse_in_window &&
                metrics->mouse_x >= b.x && metrics->mouse_x <= (b.x + b.w) &&
                metrics->mouse_y >= b.y && metrics->mouse_y <= (b.y + b.h);
            r = draw_lcars_text_button(
                ctx,
                b,
                labels[i],
                hover,
                (metrics->palette_mode == i) ? 1 : 0,
                ui,
                &pal,
                &frame,
                &txt
            );
            if (r != VG_OK) {
                return r;
            }
        }
    }

    const int item_count = VIDEO_MENU_RES_COUNT + 1;
    const float row_h = panel.h * 0.082f;
    const float row_w = panel.w * 0.29f;
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
        const int hover =
            metrics->mouse_in_window &&
            metrics->mouse_x >= row.x && metrics->mouse_x <= (row.x + row.w) &&
            metrics->mouse_y >= row.y && metrics->mouse_y <= (row.y + row.h);
        r = draw_lcars_text_button(
            ctx,
            row,
            label,
            hover,
            (metrics->video_menu_selected == i) ? 1 : 0,
            ui,
            &pal,
            &frame,
            &txt
        );
        if (r != VG_OK) {
            return r;
        }
    }

    {
        const vg_rect b = {
            panel.x + panel.w * 0.05f,
            panel.y + panel.h * 0.08f,
            panel.w * 0.29f,
            panel.h * 0.065f
        };
        const int hover =
            metrics->mouse_in_window &&
            metrics->mouse_x >= b.x && metrics->mouse_x <= (b.x + b.w) &&
            metrics->mouse_y >= b.y && metrics->mouse_y <= (b.y + b.h);
        r = draw_lcars_text_button(
            ctx,
            b,
            metrics->video_menu_high_quality ? "HIGH QUALITY ON" : "HIGH QUALITY OFF",
            hover,
            metrics->video_menu_high_quality ? 1 : 0,
            ui,
            &pal,
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
            const float radius = lab.w * 0.058f;
            for (int i = 0; i < VIDEO_MENU_DIAL_COUNT; ++i) {
                const int row = i / 4;
                const int col = i % 4;
                const vg_vec2 c = {
                    lab.x + lab.w * (0.12f + 0.25f * (float)col),
                    lab.y + lab.h * (0.74f - 0.30f * (float)row)
                };
                float v = metrics->video_dial_01[i];
                if (v < 0.0f) v = 0.0f;
                if (v > 1.0f) v = 1.0f;
                d.label = NULL;
                d.value = v * 100.0f;
                r = vg_ui_meter_radial(ctx, c, radius, &d, &ms);
                if (r != VG_OK) {
                    return r;
                }
                {
                    vg_ui_meter_desc ld = d;
                    ld.label = dial_labels[i];
                    vg_ui_meter_radial_layout ll;
                    if (vg_ui_meter_radial_layout_compute(c, radius, &ld, &ms, &ll) == VG_OK) {
                        const float label_size = 11.0f * ui;
                        const float label_spacing = 0.8f * ui;
                        const float label_w = vg_measure_text(dial_labels[i], label_size, label_spacing);
                        const vg_vec2 p = {
                            ll.label_pos.x - label_w * 0.5f,
                            ll.label_pos.y - 6.0f * ui
                        };
                        r = draw_text_vector_glow(ctx, dial_labels[i], p, label_size, label_spacing, &frame, &txt);
                        if (r != VG_OK) {
                            return r;
                        }
                    }
                }
            }
        }
    }
    return VG_OK;
}

static vg_result draw_controls_menu(vg_context* ctx, float w, float h, const render_metrics* metrics) {
    const float ui = ui_reference_scale(w, h);
    const vg_rect panel = make_ui_safe_frame(w, h);
    const palette_theme pal = get_palette_theme(metrics->palette_mode);
    vg_stroke_style frame = make_stroke(2.0f * ui, 1.0f, pal.primary, VG_BLEND_ALPHA);
    vg_stroke_style txt = make_stroke(1.2f * ui, 1.0f, pal.secondary, VG_BLEND_ALPHA);
    vg_fill_style haze = make_fill(0.30f, pal.haze, VG_BLEND_ALPHA);
    vg_result r = vg_fill_rect(ctx, panel, &haze);
    if (r != VG_OK) return r;
    r = vg_draw_rect(ctx, panel, &frame);
    if (r != VG_OK) return r;

    r = draw_text_vector_glow(ctx, "CONTROLS", (vg_vec2){panel.x + panel.w * 0.04f, panel.y + panel.h * 0.92f}, 18.0f * ui, 1.3f * ui, &frame, &txt);
    if (r != VG_OK) return r;
    r = draw_text_vector_glow(ctx, "ESC BACK   ARROWS/NAV SELECT   ENTER BIND", (vg_vec2){panel.x + panel.w * 0.04f, panel.y + panel.h * 0.875f}, 10.0f * ui, 0.8f * ui, &frame, &txt);
    if (r != VG_OK) return r;

    {
        char gp[128];
        snprintf(gp, sizeof(gp), "GAMEPAD: %s", (metrics->controls_pad_name && metrics->controls_pad_name[0]) ? metrics->controls_pad_name : "NO GAMEPAD");
        r = draw_text_vector_glow(ctx, gp, (vg_vec2){panel.x + panel.w * 0.04f, panel.y + panel.h * 0.82f}, 10.8f * ui, 0.8f * ui, &frame, &txt);
        if (r != VG_OK) return r;
    }

    const float table_x = panel.x + panel.w * 0.04f;
    const float table_y0 = panel.y + panel.h * 0.74f;
    const float row_h = panel.h * 0.085f;
    const float act_w = panel.w * 0.34f;
    const float key_w = panel.w * 0.25f;
    const float pad_w = panel.w * 0.25f;
    for (int i = 0; i < CONTROL_ACTION_COUNT_RENDER; ++i) {
        const vg_rect ra = {table_x, table_y0 - (float)i * row_h, act_w, row_h * 0.72f};
        const vg_rect rk = {ra.x + ra.w + panel.w * 0.02f, ra.y, key_w, ra.h};
        const vg_rect rp = {rk.x + rk.w + panel.w * 0.02f, ra.y, pad_w, ra.h};
        const int hover_a = metrics->mouse_in_window &&
                            metrics->mouse_x >= ra.x && metrics->mouse_x <= (ra.x + ra.w) &&
                            metrics->mouse_y >= ra.y && metrics->mouse_y <= (ra.y + ra.h);
        const int hover_k = metrics->mouse_in_window &&
                            metrics->mouse_x >= rk.x && metrics->mouse_x <= (rk.x + rk.w) &&
                            metrics->mouse_y >= rk.y && metrics->mouse_y <= (rk.y + rk.h);
        const int hover_p = metrics->mouse_in_window &&
                            metrics->mouse_x >= rp.x && metrics->mouse_x <= (rp.x + rp.w) &&
                            metrics->mouse_y >= rp.y && metrics->mouse_y <= (rp.y + rp.h);
        r = draw_lcars_text_button(ctx, ra, metrics->controls_action_label[i], hover_a, 0, ui, &pal, &frame, &txt);
        if (r != VG_OK) return r;
        r = draw_lcars_text_button(
            ctx,
            rk,
            metrics->controls_key_label[i],
            hover_k,
            (metrics->controls_selected == i && metrics->controls_selected_column == 0) ? 1 : 0,
            ui,
            &pal,
            &frame,
            &txt
        );
        if (r != VG_OK) return r;
        r = draw_lcars_text_button(
            ctx,
            rp,
            metrics->controls_pad_label[i],
            hover_p,
            (metrics->controls_selected == i && metrics->controls_selected_column == 1) ? 1 : 0,
            ui,
            &pal,
            &frame,
            &txt
        );
        if (r != VG_OK) return r;
    }
    {
        const int row = CONTROL_ACTION_COUNT_RENDER;
        const vg_rect rt = {table_x, table_y0 - (float)row * row_h, act_w + panel.w * 0.02f + key_w + panel.w * 0.02f + pad_w, row_h * 0.72f};
        const int hover_t = metrics->mouse_in_window &&
                            metrics->mouse_x >= rt.x && metrics->mouse_x <= (rt.x + rt.w) &&
                            metrics->mouse_y >= rt.y && metrics->mouse_y <= (rt.y + rt.h);
        r = draw_lcars_text_button(
            ctx,
            rt,
            metrics->controls_use_gamepad ? "USE JOYPAD: ON" : "USE JOYPAD: OFF",
            hover_t,
            (metrics->controls_selected == row) ? 1 : 0,
            ui,
            &pal,
            &frame,
            &txt
        );
        if (r != VG_OK) return r;
    }
    if (metrics->controls_rebinding_action >= 0) {
        r = draw_text_vector_glow(ctx, "PRESS A KEY OR GAMEPAD BUTTON  (ESC TO CANCEL)", (vg_vec2){panel.x + panel.w * 0.04f, panel.y + panel.h * 0.08f}, 11.0f * ui, 0.85f * ui, &frame, &txt);
        if (r != VG_OK) return r;
    }
    return VG_OK;
}

static const char* level_editor_marker_name(int kind) {
    switch (kind) {
        case 0: return "EXIT";
        case 1: return "SEARCHLIGHT";
        case 2: return "SINE WAVE";
        case 3: return "V WAVE";
        case 4: return "KAMIKAZE";
        case 5: return "BOID";
        case 10: return "SWARM FISH";
        case 11: return "SWARM FIREFLY";
        case 12: return "SWARM BIRD";
        case 15: return "JELLY SWARM";
        case 16: return "MANTA WING";
        case 6: return "ASTEROID STORM";
        case 7: return "MINEFIELD";
        case 8: return "MISSILE LAUNCHER";
        case 13: return "ARC NODE";
        case 14: return "WINDOW MASK";
        case 9: return "STRUCTURE";
        default: return "MARKER";
    }
}

static vg_color level_editor_marker_color(const palette_theme* pal, int kind) {
    if (kind == 0) {
        return (vg_color){0.95f, 0.4f, 0.95f, 1.0f};
    }
    if (kind == 1) {
        return (vg_color){0.95f, 0.36f, 0.36f, 1.0f};
    }
    if (kind == 6) {
        return (vg_color){1.0f, 0.64f, 0.20f, 1.0f};
    }
    if (kind == 7) {
        return (vg_color){1.0f, 0.46f, 0.22f, 1.0f};
    }
    if (kind == 8) {
        return (vg_color){1.0f, 0.34f, 0.18f, 1.0f};
    }
    if (kind == 13) {
        return (vg_color){0.95f, 0.82f, 0.24f, 1.0f};
    }
    if (kind == 14) {
        return (vg_color){0.62f, 0.90f, 1.0f, 1.0f};
    }
    if (kind == 9) {
        return (vg_color){0.48f, 0.90f, 1.0f, 1.0f};
    }
    if (kind == 2 || kind == 3 || kind == 4 || kind == 5 || kind == 10 || kind == 11 || kind == 12 || kind == 15 || kind == 16) {
        return (vg_color){1.0f, 0.26f, 0.26f, 1.0f};
    }
    return pal->secondary;
}

static vg_result draw_editor_diamond(vg_context* ctx, vg_vec2 c, float half, const vg_stroke_style* s) {
    const vg_vec2 p[5] = {
        {c.x, c.y + half},
        {c.x + half, c.y},
        {c.x, c.y - half},
        {c.x - half, c.y},
        {c.x, c.y + half}
    };
    return vg_draw_polyline(ctx, p, 5, s, 0);
}

static vg_result draw_editor_ship(vg_context* ctx, vg_vec2 c, float scale, const vg_stroke_style* s) {
    const float x = c.x;
    const float y = c.y;
    const float sx = scale;
    const vg_vec2 hull[5] = {
        {x - 16.0f * sx, y},
        {x - 4.0f * sx, y + 7.0f * sx},
        {x + 12.0f * sx, y},
        {x - 4.0f * sx, y - 7.0f * sx},
        {x - 16.0f * sx, y}
    };
    vg_result r = vg_draw_polyline(ctx, hull, 5, s, 0);
    if (r != VG_OK) {
        return r;
    }
    const vg_vec2 spine[2] = {{x - 13.0f * sx, y}, {x + 8.0f * sx, y}};
    return vg_draw_polyline(ctx, spine, 2, s, 0);
}

static int editor_structure_grid_steps_x(float level_screens) {
    const float ls = fmaxf(level_screens, 1.0f);
    const int base_steps = (int)lroundf(ls * (float)(LEVELDEF_STRUCTURE_GRID_W - 1));
    const int steps = base_steps / LEVELDEF_STRUCTURE_GRID_SCALE;
    return (steps < 1) ? 1 : steps;
}

static int editor_structure_grid_steps_y(void) {
    const int steps = (LEVELDEF_STRUCTURE_GRID_H - 1) / LEVELDEF_STRUCTURE_GRID_SCALE;
    return (steps < 1) ? 1 : steps;
}

static void editor_structure_prefab_dims(int prefab_id, int* out_w, int* out_h) {
    int w = 1;
    int h = 1;
    if (prefab_id == 4) {
        h = 3;
    }
    if (out_w) *out_w = w;
    if (out_h) *out_h = h;
}

static vg_vec2 structure_map_point(
    float lx,
    float ly,
    float bx,
    float by,
    float w,
    float h,
    int rotation_quadrants,
    int flip_x,
    int flip_y
) {
    const float cx = w * 0.5f;
    const float cy = h * 0.5f;
    const int q = ((rotation_quadrants % 4) + 4) % 4;
    if (flip_x) {
        lx = w - lx;
    }
    if (flip_y) {
        ly = h - ly;
    }
    {
        const float dx = lx - cx;
        const float dy = ly - cy;
        if (q == 1) {
            lx = cx - dy;
            ly = cy + dx;
        } else if (q == 2) {
            lx = cx - dx;
            ly = cy - dy;
        } else if (q == 3) {
            lx = cx + dy;
            ly = cy - dx;
        }
    }
    return (vg_vec2){bx + lx, by + ly};
}

static vg_result structure_draw_rect_transformed(
    vg_context* ctx,
    float lx,
    float ly,
    float rw,
    float rh,
    float bx,
    float by,
    float w,
    float h,
    int rotation_quadrants,
    int flip_x,
    int flip_y,
    const vg_stroke_style* st,
    const vg_fill_style* fill
) {
    vg_vec2 q[4];
    vg_vec2 loop[5];
    q[0] = structure_map_point(lx, ly, bx, by, w, h, rotation_quadrants, flip_x, flip_y);
    q[1] = structure_map_point(lx + rw, ly, bx, by, w, h, rotation_quadrants, flip_x, flip_y);
    q[2] = structure_map_point(lx + rw, ly + rh, bx, by, w, h, rotation_quadrants, flip_x, flip_y);
    q[3] = structure_map_point(lx, ly + rh, bx, by, w, h, rotation_quadrants, flip_x, flip_y);
    if (fill) {
        vg_result r = vg_fill_convex(ctx, q, 4, fill);
        if (r != VG_OK) {
            return r;
        }
    }
    loop[0] = q[0];
    loop[1] = q[1];
    loop[2] = q[2];
    loop[3] = q[3];
    loop[4] = q[0];
    return vg_draw_polyline(ctx, loop, 5, st, 0);
}

static vg_result draw_structure_prefab_tile(
    vg_context* ctx,
    int prefab_id,
    int layer,
    float bx,
    float by,
    float unit_w,
    float unit_h,
    int rotation_quadrants,
    int flip_x,
    int flip_y,
    const vg_stroke_style* st,
    const vg_fill_style* fill
) {
    int w_units = 1;
    int h_units = 1;
    editor_structure_prefab_dims(prefab_id, &w_units, &h_units);
    const float w = unit_w * (float)w_units;
    const float h = unit_h * (float)h_units;
    vg_result r = VG_OK;
    if (prefab_id != 3) {
        r = structure_draw_rect_transformed(
            ctx, 0.0f, 0.0f, w, h, bx, by, w, h,
            rotation_quadrants, flip_x ? 1 : 0, flip_y ? 1 : 0,
            st, (fill && layer == 0) ? fill : NULL
        );
        if (r != VG_OK) return r;
    }

    if (prefab_id == 1) {
        const float mx = fminf(unit_w * 0.20f, w * 0.26f);
        const float my = fminf(unit_h * 0.20f, h * 0.26f);
        return structure_draw_rect_transformed(
            ctx,
            mx,
            my,
            fmaxf(w - 2.0f * mx, 1.0f),
            fmaxf(h - 2.0f * my, 1.0f),
            bx,
            by,
            w,
            h,
            rotation_quadrants,
            flip_x ? 1 : 0,
            flip_y ? 1 : 0,
            st,
            NULL
        );
    }
    if (prefab_id == 2) {
        const vg_vec2 d0[2] = {
            structure_map_point(0.0f, 0.0f, bx, by, w, h, rotation_quadrants, flip_x, flip_y),
            structure_map_point(w, h, bx, by, w, h, rotation_quadrants, flip_x, flip_y)
        };
        const vg_vec2 d1[2] = {
            structure_map_point(w, 0.0f, bx, by, w, h, rotation_quadrants, flip_x, flip_y),
            structure_map_point(0.0f, h, bx, by, w, h, rotation_quadrants, flip_x, flip_y)
        };
        r = vg_draw_polyline(ctx, d0, 2, st, 0);
        if (r != VG_OK) return r;
        return vg_draw_polyline(ctx, d1, 2, st, 0);
    }
    if (prefab_id == 3) {
        vg_vec2 tri_fill[3];
        vg_vec2 tri[4];
        const vg_vec2 base[3] = {
            {0.0f, h},
            {0.0f, 0.0f},
            {w, h}
        };
        for (int i = 0; i < 3; ++i) {
            tri_fill[i] = structure_map_point(
                base[i].x, base[i].y,
                bx, by, w, h,
                rotation_quadrants, flip_x, flip_y
            );
            tri[i] = tri_fill[i];
        }
        tri[3] = tri[0];
        if (fill && layer == 0) {
            r = vg_fill_convex(ctx, tri_fill, 3, fill);
            if (r != VG_OK) {
                return r;
            }
        }
        return vg_draw_polyline(ctx, tri, 4, st, 0);
    }
    if (prefab_id == 4) {
        return VG_OK;
    }
    if (prefab_id == 5) {
        const float y0 = by + h * 0.40f;
        const float y1 = by + h * 0.60f;
        const vg_vec2 p0[2] = {
            structure_map_point(0.0f, y0 - by, bx, by, w, h, rotation_quadrants, flip_x, flip_y),
            structure_map_point(w, y0 - by, bx, by, w, h, rotation_quadrants, flip_x, flip_y)
        };
        const vg_vec2 p1[2] = {
            structure_map_point(0.0f, y1 - by, bx, by, w, h, rotation_quadrants, flip_x, flip_y),
            structure_map_point(w, y1 - by, bx, by, w, h, rotation_quadrants, flip_x, flip_y)
        };
        r = vg_draw_polyline(ctx, p0, 2, st, 0);
        if (r != VG_OK) return r;
        r = vg_draw_polyline(ctx, p1, 2, st, 0);
        if (r != VG_OK) return r;
        return VG_OK;
    }
    if (prefab_id == 6) {
        const float rr = fminf(w, h) * 0.34f;
        const float cx = w * 0.5f;
        const float cy = h * 0.5f;
        vg_vec2 ring[17];
        for (int k = 0; k < 16; ++k) {
            const float a = ((float)k / 16.0f) * 6.2831853f;
            ring[k] = structure_map_point(
                cx + cosf(a) * rr,
                cy + sinf(a) * rr,
                bx, by, w, h,
                rotation_quadrants, flip_x, flip_y
            );
        }
        ring[16] = ring[0];
        r = vg_draw_polyline(ctx, ring, 17, st, 0);
        if (r != VG_OK) return r;
        for (int s = 0; s < 4; ++s) {
            const float a = ((float)s / 4.0f) * 6.2831853f;
            const vg_vec2 spoke[2] = {
                structure_map_point(cx, cy, bx, by, w, h, rotation_quadrants, flip_x, flip_y),
                structure_map_point(cx + cosf(a) * rr, cy + sinf(a) * rr, bx, by, w, h, rotation_quadrants, flip_x, flip_y)
            };
            r = vg_draw_polyline(ctx, spoke, 2, st, 0);
            if (r != VG_OK) return r;
        }
        return VG_OK;
    }
    if (prefab_id >= 7) {
        const float mx = fminf(unit_w * 0.18f, w * 0.24f);
        const float my = fminf(unit_h * 0.22f, h * 0.30f);
        return structure_draw_rect_transformed(
            ctx,
            mx,
            my,
            fmaxf(w - 2.0f * mx, 1.0f),
            fmaxf(h - 2.0f * my, 1.0f),
            bx,
            by,
            w,
            h,
            rotation_quadrants,
            flip_x ? 1 : 0,
            flip_y ? 1 : 0,
            st,
            NULL
        );
    }
    return VG_OK;
}

static void editor_sanitize_label(const char* in, char* out, size_t out_cap) {
    if (!out || out_cap == 0) {
        return;
    }
    out[0] = '\0';
    if (!in) {
        return;
    }
    size_t n = 0;
    for (const char* p = in; *p && n + 1 < out_cap; ++p) {
        char c = *p;
        if (c == '_') {
            c = ' ';
        }
        if (c >= 'a' && c <= 'z') {
            c = (char)(c - 'a' + 'A');
        }
        out[n++] = c;
    }
    out[n] = '\0';
}

static const char* editor_wave_type_name(int kind) {
    switch (kind) {
        case 2: return "SINE";
        case 3: return "V";
        case 4: return "KAMIKAZE";
        case 5: return "BOID";
        case 10: return "SWARM FISH";
        case 11: return "SWARM FIREFLY";
        case 12: return "SWARM BIRD";
        case 15: return "JELLY SWARM";
        case 16: return "MANTA WING";
        default: return "UNKNOWN";
    }
}

static const char* editor_wave_mode_name(int mode) {
    if (mode == LEVELDEF_WAVES_BOID_ONLY) return "BOID ONLY";
    if (mode == LEVELDEF_WAVES_CURATED) return "CURATED";
    return "NORMAL";
}

static const char* editor_searchlight_motion_name(float v) {
    const int mode = (int)lroundf(v);
    if (mode == 0) return "LINEAR";
    if (mode == 2) return "SPIN";
    return "PENDULUM";
}

static const char* editor_searchlight_source_name(float v) {
    const int src = (int)lroundf(v);
    if (src == 1) return "ORB";
    return "DOME";
}

static const char* editor_render_style_name(int style) {
    if (style == LEVEL_RENDER_CYLINDER) return "CYLINDER";
    if (style == LEVEL_RENDER_DRIFTER) return "DRIFTER";
    if (style == LEVEL_RENDER_DRIFTER_SHADED) return "DRIFTER SHADED";
    if (style == LEVEL_RENDER_FOG) return "FOG";
    if (style == LEVEL_RENDER_BLANK) return "BLANK";
    return "DEFENDER";
}

static const char* editor_theme_palette_name(int palette) {
    if (palette == 1) return "AMBER";
    if (palette == 2) return "ICE";
    return "GREEN";
}

static const char* editor_background_style_name(int style) {
    if (style == LEVELDEF_BACKGROUND_NONE) return "NONE";
    if (style == LEVELDEF_BACKGROUND_NEBULA) return "NEBULA";
    if (style == LEVELDEF_BACKGROUND_GRID) return "GRID";
    if (style == LEVELDEF_BACKGROUND_SOLID) return "SOLID";
    if (style == LEVELDEF_BACKGROUND_UNDERWATER) return "UNDERWATER";
    if (style == LEVELDEF_BACKGROUND_FIRE) return "FIRE";
    return "STARS";
}

static const char* editor_background_mask_style_name(int style) {
    if (style == LEVELDEF_BG_MASK_TERRAIN) return "TERRAIN";
    if (style == LEVELDEF_BG_MASK_WINDOWS) return "WINDOWS";
    return "NONE";
}

static int editor_marker_properties_text(
    int kind,
    const render_metrics* metrics,
    int sel,
    const char** out_labels,
    char out_values[][32],
    int cap
) {
    if (!metrics || sel < 0 || cap <= 0) {
        return 0;
    }
    int n = 0;
    if (kind == 1) {
        if (n < cap) { out_labels[n] = "POS X"; snprintf(out_values[n], 32, "%.3f", metrics->level_editor_marker_x01[sel]); n++; }
        if (n < cap) { out_labels[n] = "POS Y"; snprintf(out_values[n], 32, "%.3f", metrics->level_editor_marker_y01[sel]); n++; }
        if (n < cap) { out_labels[n] = "LENGTH"; snprintf(out_values[n], 32, "%.3f", metrics->level_editor_marker_a[sel]); n++; }
        if (n < cap) { out_labels[n] = "BEAM WIDTH"; snprintf(out_values[n], 32, "%.1f", metrics->level_editor_marker_b[sel]); n++; }
        if (n < cap) { out_labels[n] = "SWEEP SPEED"; snprintf(out_values[n], 32, "%.2f", metrics->level_editor_marker_c[sel]); n++; }
        if (n < cap) { out_labels[n] = "SWEEP ANGLE"; snprintf(out_values[n], 32, "%.1f", metrics->level_editor_marker_d[sel]); n++; }
        if (n < cap) { out_labels[n] = "TYPE"; snprintf(out_values[n], 32, "%s", editor_searchlight_motion_name(metrics->level_editor_marker_g[sel])); n++; }
        if (n < cap) { out_labels[n] = "SOURCE"; snprintf(out_values[n], 32, "%s", editor_searchlight_source_name(metrics->level_editor_marker_e[sel])); n++; }
        if (n < cap) { out_labels[n] = "SIZE"; snprintf(out_values[n], 32, "%.1f", metrics->level_editor_marker_f[sel]); n++; }
        return n;
    }
    if (kind == 0) {
        if (n < cap) { out_labels[n] = "POS X"; snprintf(out_values[n], 32, "%.3f", metrics->level_editor_marker_x01[sel]); n++; }
        if (n < cap) { out_labels[n] = "POS Y"; snprintf(out_values[n], 32, "%.3f", metrics->level_editor_marker_y01[sel]); n++; }
        return n;
    }
    if (kind == 6) {
        const int event_item = (metrics->level_editor_marker_track[sel] == 1);
        if (event_item) {
            if (n < cap) { out_labels[n] = "ORDER"; snprintf(out_values[n], 32, "%d", metrics->level_editor_marker_order[sel]); n++; }
            if (n < cap) { out_labels[n] = "DELAY S"; snprintf(out_values[n], 32, "%.2f", metrics->level_editor_marker_delay_s[sel]); n++; }
        } else {
            if (n < cap) { out_labels[n] = "POS X"; snprintf(out_values[n], 32, "%.3f", metrics->level_editor_marker_x01[sel]); n++; }
            if (n < cap) { out_labels[n] = "POS Y"; snprintf(out_values[n], 32, "%.3f", metrics->level_editor_marker_y01[sel]); n++; }
        }
        if (n < cap) { out_labels[n] = "DURATION S"; snprintf(out_values[n], 32, "%.2f", metrics->level_editor_marker_a[sel]); n++; }
        if (n < cap) { out_labels[n] = "ANGLE DEG"; snprintf(out_values[n], 32, "%.1f", metrics->level_editor_marker_b[sel]); n++; }
        if (n < cap) { out_labels[n] = "SPEED"; snprintf(out_values[n], 32, "%.1f", metrics->level_editor_marker_c[sel]); n++; }
        if (n < cap) { out_labels[n] = "DENSITY"; snprintf(out_values[n], 32, "%.2f", metrics->level_editor_marker_d[sel]); n++; }
        return n;
    }
    if (kind == 7) {
        if (n < cap) { out_labels[n] = "POS X"; snprintf(out_values[n], 32, "%.3f", metrics->level_editor_marker_x01[sel]); n++; }
        if (n < cap) { out_labels[n] = "POS Y"; snprintf(out_values[n], 32, "%.3f", metrics->level_editor_marker_y01[sel]); n++; }
        if (n < cap) { out_labels[n] = "COUNT"; snprintf(out_values[n], 32, "%.0f", metrics->level_editor_marker_a[sel]); n++; }
        return n;
    }
    if (kind == 8) {
        if (n < cap) { out_labels[n] = "POS X"; snprintf(out_values[n], 32, "%.3f", metrics->level_editor_marker_x01[sel]); n++; }
        if (n < cap) { out_labels[n] = "POS Y"; snprintf(out_values[n], 32, "%.3f", metrics->level_editor_marker_y01[sel]); n++; }
        if (n < cap) { out_labels[n] = "COUNT"; snprintf(out_values[n], 32, "%.0f", metrics->level_editor_marker_a[sel]); n++; }
        if (n < cap) { out_labels[n] = "SPACING"; snprintf(out_values[n], 32, "%.1f", metrics->level_editor_marker_b[sel]); n++; }
        if (n < cap) { out_labels[n] = "ACT RANGE"; snprintf(out_values[n], 32, "%.1f", metrics->level_editor_marker_c[sel]); n++; }
        if (n < cap) { out_labels[n] = "TTL S"; snprintf(out_values[n], 32, "%.2f", metrics->level_editor_marker_d[sel]); n++; }
        return n;
    }
    if (kind == 13) {
        if (n < cap) { out_labels[n] = "POS X"; snprintf(out_values[n], 32, "%.3f", metrics->level_editor_marker_x01[sel]); n++; }
        if (n < cap) { out_labels[n] = "POS Y"; snprintf(out_values[n], 32, "%.3f", metrics->level_editor_marker_y01[sel]); n++; }
        if (n < cap) { out_labels[n] = "PERIOD S"; snprintf(out_values[n], 32, "%.2f", metrics->level_editor_marker_a[sel]); n++; }
        if (n < cap) { out_labels[n] = "ON S"; snprintf(out_values[n], 32, "%.2f", metrics->level_editor_marker_b[sel]); n++; }
        if (n < cap) { out_labels[n] = "RADIUS"; snprintf(out_values[n], 32, "%.1f", metrics->level_editor_marker_c[sel]); n++; }
        if (n < cap) { out_labels[n] = "PUSH"; snprintf(out_values[n], 32, "%.0f", metrics->level_editor_marker_d[sel]); n++; }
        if (n < cap) { out_labels[n] = "DAMAGE INT"; snprintf(out_values[n], 32, "%.2f", metrics->level_editor_marker_e[sel]); n++; }
        return n;
    }
    if (kind == 14) {
        if (n < cap) { out_labels[n] = "POS X"; snprintf(out_values[n], 32, "%.3f", metrics->level_editor_marker_x01[sel]); n++; }
        if (n < cap) { out_labels[n] = "POS Y"; snprintf(out_values[n], 32, "%.3f", metrics->level_editor_marker_y01[sel]); n++; }
        if (n < cap) { out_labels[n] = "WIDTH"; snprintf(out_values[n], 32, "%.3f", metrics->level_editor_marker_a[sel]); n++; }
        if (n < cap) { out_labels[n] = "HEIGHT"; snprintf(out_values[n], 32, "%.3f", metrics->level_editor_marker_b[sel]); n++; }
        if (n < cap) { out_labels[n] = "FLIP V"; snprintf(out_values[n], 32, "%s", (metrics->level_editor_marker_c[sel] >= 0.5f) ? "YES" : "NO"); n++; }
        return n;
    }
    if (kind == 9) {
        if (n < cap) { out_labels[n] = "POS X"; snprintf(out_values[n], 32, "%.3f", metrics->level_editor_marker_x01[sel]); n++; }
        if (n < cap) { out_labels[n] = "POS Y"; snprintf(out_values[n], 32, "%.3f", metrics->level_editor_marker_y01[sel]); n++; }
        if (n < cap) { out_labels[n] = "PREFAB"; snprintf(out_values[n], 32, "%.0f", metrics->level_editor_marker_a[sel]); n++; }
        if (n < cap) { out_labels[n] = "LAYER"; snprintf(out_values[n], 32, "%.0f", metrics->level_editor_marker_b[sel]); n++; }
        if (n < cap) { out_labels[n] = "ROT"; snprintf(out_values[n], 32, "%.0f", metrics->level_editor_marker_c[sel]); n++; }
        if (n < cap) { out_labels[n] = "FLIP"; snprintf(out_values[n], 32, "%.0f", metrics->level_editor_marker_d[sel]); n++; }
        if (n < cap) { out_labels[n] = "VENT DENSITY"; snprintf(out_values[n], 32, "%.2f", metrics->level_editor_marker_e[sel]); n++; }
        if (n < cap) { out_labels[n] = "VENT OPACITY"; snprintf(out_values[n], 32, "%.2f", metrics->level_editor_marker_f[sel]); n++; }
        if (n < cap) { out_labels[n] = "VENT HEIGHT"; snprintf(out_values[n], 32, "%.2f", metrics->level_editor_marker_g[sel]); n++; }
        return n;
    }
    if (kind == 2 || kind == 3 || kind == 4 || kind == 5 || kind == 10 || kind == 11 || kind == 12 || kind == 15 || kind == 16) {
        const int event_item = (metrics->level_editor_marker_track[sel] == 1);
        const int boid_item = (kind == 5 || kind == 10 || kind == 11 || kind == 12 || kind == 15);
        const int kamikaze_item = (kind == 4);
        if (n < cap) { out_labels[n] = "TYPE"; snprintf(out_values[n], 32, "%s", editor_wave_type_name(kind)); n++; }
        if (event_item) {
            if (n < cap) { out_labels[n] = "ORDER"; snprintf(out_values[n], 32, "%d", metrics->level_editor_marker_order[sel]); n++; }
            if (n < cap) { out_labels[n] = "DELAY S"; snprintf(out_values[n], 32, "%.2f", metrics->level_editor_marker_delay_s[sel]); n++; }
        } else {
            if (n < cap) { out_labels[n] = "POS X"; snprintf(out_values[n], 32, "%.3f", metrics->level_editor_marker_x01[sel]); n++; }
            if (n < cap) { out_labels[n] = "POS Y"; snprintf(out_values[n], 32, "%.3f", metrics->level_editor_marker_y01[sel]); n++; }
        }
        if (n < cap) { out_labels[n] = "COUNT"; snprintf(out_values[n], 32, "%.0f", metrics->level_editor_marker_a[sel]); n++; }
        if (n < cap) {
            if (kind == 16) {
                out_labels[n] = "SIZE";
            } else {
                out_labels[n] = (kind == 2 || kind == 3) ? "FORMATION AMP" : "MAX SPEED";
            }
            snprintf(out_values[n], 32, "%.3f", metrics->level_editor_marker_b[sel]);
            n++;
        }
        if (n < cap) {
            if (kind == 16) {
                out_labels[n] = "MISSILES";
                snprintf(out_values[n], 32, "%.0f", metrics->level_editor_marker_c[sel]);
                n++;
            } else {
                out_labels[n] = (kind == 2 || kind == 3) ? "MAX SPEED" : "ACCEL";
                snprintf(out_values[n], 32, "%.3f", metrics->level_editor_marker_c[sel]);
                n++;
            }
        }
        if (boid_item && n < cap) {
            if (kind == 15) {
                out_labels[n] = "SIZE SCALE";
                snprintf(out_values[n], 32, "%.2f", metrics->level_editor_marker_d[sel]);
            } else {
                out_labels[n] = "MAX TURN DEG";
                snprintf(out_values[n], 32, "%.1f", metrics->level_editor_marker_d[sel]);
            }
            n++;
        }
        if (kamikaze_item && n < cap) { out_labels[n] = "RADIUS MIN"; snprintf(out_values[n], 32, "%.1f", metrics->level_editor_kamikaze_radius_min); n++; }
        if (kamikaze_item && n < cap) { out_labels[n] = "RADIUS MAX"; snprintf(out_values[n], 32, "%.1f", metrics->level_editor_kamikaze_radius_max); n++; }
        if (!event_item && n < cap) { out_labels[n] = "DELAY S"; snprintf(out_values[n], 32, "%.2f", metrics->level_editor_marker_delay_s[sel]); n++; }
        return n;
    }

    if (n < cap) { out_labels[n] = "POS X"; snprintf(out_values[n], 32, "%.3f", metrics->level_editor_marker_x01[sel]); n++; }
    if (n < cap) { out_labels[n] = "POS Y"; snprintf(out_values[n], 32, "%.3f", metrics->level_editor_marker_y01[sel]); n++; }
    if (kind == 1) {
        if (n < cap) { out_labels[n] = "LENGTH H01"; snprintf(out_values[n], 32, "%.3f", metrics->level_editor_marker_a[sel]); n++; }
        if (n < cap) { out_labels[n] = "HALF ANGLE DEG"; snprintf(out_values[n], 32, "%.2f", metrics->level_editor_marker_b[sel]); n++; }
        if (n < cap) { out_labels[n] = "SWEEP SPEED"; snprintf(out_values[n], 32, "%.3f", metrics->level_editor_marker_c[sel]); n++; }
        if (n < cap) { out_labels[n] = "SWEEP AMP DEG"; snprintf(out_values[n], 32, "%.2f", metrics->level_editor_marker_d[sel]); n++; }
    }
    return n;
}

static vg_result draw_level_editor_ui(vg_context* ctx, float w, float h, const render_metrics* metrics, float t_s) {
    (void)t_s;
    const float ui = ui_reference_scale(w, h);
    const palette_theme pal = get_palette_theme(metrics->palette_mode);
    const float m = 22.0f * ui;
    const float gap = 16.0f * ui;
    const float right_total_w = w * 0.30f;
    const float left_w = w - right_total_w - m * 2.0f - gap;
    const float timeline_h = h * 0.18f;
    const float toolbar_h = 44.0f * ui;
    const float top_h = h - m * 2.0f - timeline_h - gap - toolbar_h - gap;
    const float side_gap = 10.0f * ui;
    const float props_w = right_total_w * 0.72f;
    const float entities_w = right_total_w - props_w - side_gap;
    const vg_rect viewport = {m, m + timeline_h + gap, left_w, top_h};
    const vg_rect construction_toolbar = {viewport.x, viewport.y + viewport.h + gap, viewport.w, toolbar_h};
    const vg_rect timeline = {m, m, left_w, timeline_h};
    const vg_rect timeline_track = {
        timeline.x + 14.0f * ui,
        timeline.y + timeline.h * 0.36f + 8.0f * ui,
        timeline.w - 28.0f * ui,
        timeline.h * 0.40f
    };
    const vg_rect timeline_enemy_track = {
        timeline_track.x,
        timeline_track.y - timeline_track.h + 3.0f * ui,
        timeline_track.w,
        timeline_track.h * 0.60f
    };
    const vg_rect props = {m + left_w + gap, m + timeline_h + gap, props_w, top_h};
    const vg_rect entities = {props.x + props.w + side_gap, props.y, entities_w, top_h};
    const float row_h = 42.0f * ui;
    const float nav_w = row_h * 0.92f;
    const float name_gap = 8.0f * ui;
    const float controls_w = right_total_w;
    const float controls_x = props.x;
    const vg_rect name_box = {controls_x + nav_w + name_gap, m + timeline_h - row_h, controls_w - (nav_w * 2.0f + name_gap * 2.0f), row_h};
    const vg_rect prev_btn = {controls_x, m + timeline_h - row_h, nav_w, row_h};
    const vg_rect next_btn = {name_box.x + name_box.w + name_gap, name_box.y, nav_w, row_h};
    const vg_rect load_btn = {controls_x, m, controls_w * 0.48f, row_h};
    const vg_rect del_btn = {controls_x, m + row_h + 8.0f * ui, controls_w * 0.48f, row_h};
    const vg_rect new_btn = {controls_x, m + (row_h + 8.0f * ui) * 2.0f, controls_w * 0.48f, row_h};
    const vg_rect save_new_btn = {controls_x + controls_w * 0.52f, m + row_h + 8.0f * ui, controls_w * 0.48f, row_h};
    const vg_rect save_btn = {controls_x + controls_w * 0.52f, m, controls_w * 0.48f, row_h};
    const vg_rect swarm_btn = {entities.x + 8.0f * ui, entities.y + entities.h - 54.0f * ui, entities.w - 16.0f * ui, 42.0f * ui};
    const vg_rect watcher_btn = {entities.x + 8.0f * ui, entities.y + entities.h - 106.0f * ui, entities.w - 16.0f * ui, 42.0f * ui};
    const vg_rect asteroid_btn = {entities.x + 8.0f * ui, entities.y + entities.h - 158.0f * ui, entities.w - 16.0f * ui, 42.0f * ui};
    const vg_rect mine_btn = {entities.x + 8.0f * ui, entities.y + entities.h - 210.0f * ui, entities.w - 16.0f * ui, 42.0f * ui};
    const vg_rect missile_btn = {entities.x + 8.0f * ui, entities.y + entities.h - 262.0f * ui, entities.w - 16.0f * ui, 42.0f * ui};
    const vg_rect arc_btn = {entities.x + 8.0f * ui, entities.y + entities.h - 314.0f * ui, entities.w - 16.0f * ui, 42.0f * ui};
    const vg_rect window_btn = {entities.x + 8.0f * ui, entities.y + entities.h - 366.0f * ui, entities.w - 16.0f * ui, 42.0f * ui};
    const float ctb_x = construction_toolbar.x + 8.0f * ui;
    const float ctb_y = construction_toolbar.y + 3.0f * ui;
    const float ctb_w = construction_toolbar.w - 16.0f * ui;
    const float ctb_gap = 8.0f * ui;
    const float ctb_btn_w = (ctb_w - ctb_gap * 9.0f) / 10.0f;
    const float ctb_btn_h = construction_toolbar.h - 6.0f * ui;
    const vg_rect ctb_btn0 = {ctb_x + (ctb_btn_w + ctb_gap) * 0.0f, ctb_y, ctb_btn_w, ctb_btn_h};
    const vg_rect ctb_btn1 = {ctb_x + (ctb_btn_w + ctb_gap) * 1.0f, ctb_y, ctb_btn_w, ctb_btn_h};
    const vg_rect ctb_btn2 = {ctb_x + (ctb_btn_w + ctb_gap) * 2.0f, ctb_y, ctb_btn_w, ctb_btn_h};
    const vg_rect ctb_btn3 = {ctb_x + (ctb_btn_w + ctb_gap) * 3.0f, ctb_y, ctb_btn_w, ctb_btn_h};
    const vg_rect ctb_btn4 = {ctb_x + (ctb_btn_w + ctb_gap) * 4.0f, ctb_y, ctb_btn_w, ctb_btn_h};
    const vg_rect ctb_btn5 = {ctb_x + (ctb_btn_w + ctb_gap) * 5.0f, ctb_y, ctb_btn_w, ctb_btn_h};
    const vg_rect ctb_btn6 = {ctb_x + (ctb_btn_w + ctb_gap) * 6.0f, ctb_y, ctb_btn_w, ctb_btn_h};
    const vg_rect ctb_btn7 = {ctb_x + (ctb_btn_w + ctb_gap) * 7.0f, ctb_y, ctb_btn_w, ctb_btn_h};
    char level_name_disp[96];
    const int enemy_spatial =
        (metrics->level_editor_wave_mode == LEVELDEF_WAVES_CURATED);
    editor_sanitize_label(metrics->level_editor_level_name ? metrics->level_editor_level_name : "level_defender", level_name_disp, sizeof(level_name_disp));

    vg_stroke_style frame = {
        .width_px = 1.8f * ui,
        .intensity = 0.95f,
        .color = pal.primary,
        .cap = VG_LINE_CAP_ROUND,
        .join = VG_LINE_JOIN_ROUND,
        .miter_limit = 4.0f,
        .blend = VG_BLEND_ALPHA
    };
    vg_stroke_style text = frame;
    text.width_px = 1.25f * ui;
    text.intensity = 1.12f;
    text.color = pal.secondary;
    vg_fill_style haze = {
        .intensity = 0.25f,
        .color = pal.haze,
        .blend = VG_BLEND_ALPHA
    };

    vg_result r = vg_fill_rect(ctx, viewport, &haze);
    if (r != VG_OK) return r;
    r = vg_draw_rect(ctx, viewport, &frame);
    if (r != VG_OK) return r;
    r = vg_fill_rect(ctx, construction_toolbar, &haze);
    if (r != VG_OK) return r;
    r = vg_draw_rect(ctx, construction_toolbar, &frame);
    if (r != VG_OK) return r;
    r = vg_fill_rect(ctx, props, &haze);
    if (r != VG_OK) return r;
    r = vg_draw_rect(ctx, props, &frame);
    if (r != VG_OK) return r;
    r = vg_fill_rect(ctx, entities, &haze);
    if (r != VG_OK) return r;
    r = vg_draw_rect(ctx, entities, &frame);
    if (r != VG_OK) return r;
    r = vg_fill_rect(ctx, timeline, &haze);
    if (r != VG_OK) return r;
    r = vg_draw_rect(ctx, timeline, &frame);
    if (r != VG_OK) return r;

    r = draw_text_vector_glow(ctx, "TIMELINE", (vg_vec2){timeline.x + 10.0f * ui, timeline.y + timeline.h - 14.0f * ui}, 11.6f * ui, 0.82f * ui, &frame, &text);
    if (r != VG_OK) return r;

    r = draw_ui_button_shaded(ctx, load_btn, "REVERT", 10.6f * ui, &frame, &text, 0);
    if (r != VG_OK) return r;
    r = draw_ui_button_shaded(ctx, del_btn, "DELETE", 10.6f * ui, &frame, &text, 0);
    if (r != VG_OK) return r;
    r = draw_ui_button_shaded(ctx, new_btn, "NEW", 11.0f * ui, &frame, &text, 0);
    if (r != VG_OK) return r;
    r = draw_ui_button_shaded(ctx, save_btn, "SAVE", 11.0f * ui, &frame, &text, 0);
    if (r != VG_OK) return r;
    r = draw_ui_button_shaded(ctx, save_new_btn, "SAVE NEW", 10.4f * ui, &frame, &text, 0);
    if (r != VG_OK) return r;
    r = draw_ui_button_shaded(ctx, prev_btn, "", 12.6f * ui, &frame, &text, 0);
    if (r != VG_OK) return r;
    r = draw_ui_button_shaded(ctx, name_box, level_name_disp, 11.4f * ui, &frame, &text, 0);
    if (r != VG_OK) return r;
    r = draw_ui_button_shaded(ctx, next_btn, "", 12.6f * ui, &frame, &text, 0);
    if (r != VG_OK) return r;
    vg_stroke_style layer1_frame = frame;
    vg_stroke_style layer1_text = text;
    vg_stroke_style layer2_frame = frame;
    vg_stroke_style layer2_text = text;
    layer1_frame.color = pal.primary_dim;
    layer1_frame.intensity *= 1.06f;
    layer1_text.color = pal.secondary;
    layer2_frame.color = pal.secondary;
    layer2_frame.intensity *= 1.12f;
    layer2_text.color = pal.primary;
    r = draw_ui_button_shaded(ctx, ctb_btn0, "PANEL SQ", 9.8f * ui, &layer1_frame, &layer1_text, metrics->level_editor_structure_tool_selected == 1 ? 1 : 0);
    if (r != VG_OK) return r;
    r = draw_ui_button_shaded(ctx, ctb_btn1, "INSET SQ", 9.8f * ui, &layer1_frame, &layer1_text, metrics->level_editor_structure_tool_selected == 2 ? 1 : 0);
    if (r != VG_OK) return r;
    r = draw_ui_button_shaded(ctx, ctb_btn2, "X BRACE", 9.8f * ui, &layer1_frame, &layer1_text, metrics->level_editor_structure_tool_selected == 3 ? 1 : 0);
    if (r != VG_OK) return r;
    r = draw_ui_button_shaded(ctx, ctb_btn3, "TRI", 9.8f * ui, &layer1_frame, &layer1_text, metrics->level_editor_structure_tool_selected == 4 ? 1 : 0);
    if (r != VG_OK) return r;
    r = draw_ui_button_shaded(ctx, ctb_btn4, "TRIPLE", 9.8f * ui, &layer1_frame, &layer1_text, metrics->level_editor_structure_tool_selected == 5 ? 1 : 0);
    if (r != VG_OK) return r;
    r = draw_ui_button_shaded(ctx, ctb_btn5, "PIPE", 9.8f * ui, &layer2_frame, &layer2_text, metrics->level_editor_structure_tool_selected == 6 ? 1 : 0);
    if (r != VG_OK) return r;
    r = draw_ui_button_shaded(ctx, ctb_btn6, "VALVE", 9.8f * ui, &layer2_frame, &layer2_text, metrics->level_editor_structure_tool_selected == 7 ? 1 : 0);
    if (r != VG_OK) return r;
    r = draw_ui_button_shaded(ctx, ctb_btn7, "VENT", 9.8f * ui, &layer2_frame, &layer2_text, metrics->level_editor_structure_tool_selected == 8 ? 1 : 0);
    if (r != VG_OK) return r;
    r = draw_ui_button_shaded(ctx, swarm_btn, "SWARM", 10.2f * ui, &frame, &text, metrics->level_editor_tool_selected == 5 ? 1 : 0);
    if (r != VG_OK) return r;
    r = draw_ui_button_shaded(ctx, watcher_btn, "WATCHER", 10.2f * ui, &frame, &text, metrics->level_editor_tool_selected == 1 ? 1 : 0);
    if (r != VG_OK) return r;
    r = draw_ui_button_shaded(ctx, asteroid_btn, "ASTEROID", 10.2f * ui, &frame, &text, metrics->level_editor_tool_selected == 6 ? 1 : 0);
    if (r != VG_OK) return r;
    r = draw_ui_button_shaded(ctx, mine_btn, "MINEFIELD", 10.2f * ui, &frame, &text, metrics->level_editor_tool_selected == 7 ? 1 : 0);
    if (r != VG_OK) return r;
    r = draw_ui_button_shaded(ctx, missile_btn, "MISSILES", 10.2f * ui, &frame, &text, metrics->level_editor_tool_selected == 8 ? 1 : 0);
    if (r != VG_OK) return r;
    r = draw_ui_button_shaded(ctx, arc_btn, "ARC NODE", 10.2f * ui, &frame, &text, metrics->level_editor_tool_selected == 13 ? 1 : 0);
    if (r != VG_OK) return r;
    if (metrics->level_editor_background_mask_style == LEVELDEF_BG_MASK_WINDOWS) {
        r = draw_ui_button_shaded(ctx, window_btn, "WINDOW", 10.2f * ui, &frame, &text, metrics->level_editor_tool_selected == 14 ? 1 : 0);
        if (r != VG_OK) return r;
    }
    {
        vg_stroke_style icon = frame;
        icon.width_px = 1.2f * ui;
        icon.intensity = 1.10f;
        icon.color = pal.secondary;
        {
            const vg_vec2 ltri[3] = {
                {prev_btn.x + prev_btn.w * 0.62f, prev_btn.y + prev_btn.h * 0.25f},
                {prev_btn.x + prev_btn.w * 0.38f, prev_btn.y + prev_btn.h * 0.50f},
                {prev_btn.x + prev_btn.w * 0.62f, prev_btn.y + prev_btn.h * 0.75f}
            };
            r = vg_draw_polyline(ctx, ltri, 3, &icon, 0);
            if (r != VG_OK) return r;
            const vg_vec2 rtri[3] = {
                {next_btn.x + next_btn.w * 0.38f, next_btn.y + next_btn.h * 0.25f},
                {next_btn.x + next_btn.w * 0.62f, next_btn.y + next_btn.h * 0.50f},
                {next_btn.x + next_btn.w * 0.38f, next_btn.y + next_btn.h * 0.75f}
            };
            r = vg_draw_polyline(ctx, rtri, 3, &icon, 0);
            if (r != VG_OK) return r;
        }
        r = draw_editor_diamond(ctx, (vg_vec2){swarm_btn.x + 14.0f * ui, swarm_btn.y + swarm_btn.h * 0.52f}, 4.2f * ui, &icon);
        if (r != VG_OK) return r;
        r = draw_editor_diamond(ctx, (vg_vec2){swarm_btn.x + 24.0f * ui, swarm_btn.y + swarm_btn.h * 0.40f}, 3.5f * ui, &icon);
        if (r != VG_OK) return r;
        r = draw_editor_diamond(ctx, (vg_vec2){watcher_btn.x + 18.0f * ui, watcher_btn.y + watcher_btn.h * 0.50f}, 5.0f * ui, &icon);
        if (r != VG_OK) return r;
        {
            vg_fill_style af = {
                .intensity = 0.90f,
                .color = (vg_color){icon.color.r, icon.color.g, icon.color.b, 0.80f},
                .blend = VG_BLEND_ALPHA
            };
            r = vg_fill_circle(ctx, (vg_vec2){asteroid_btn.x + 18.0f * ui, asteroid_btn.y + asteroid_btn.h * 0.50f}, 4.8f * ui, &af, 20);
        }
        if (r != VG_OK) return r;
        {
            vg_stroke_style ms = icon;
            r = vg_draw_polyline(
                ctx,
                (vg_vec2[]){
                    {mine_btn.x + 12.0f * ui, mine_btn.y + mine_btn.h * 0.48f},
                    {mine_btn.x + 20.0f * ui, mine_btn.y + mine_btn.h * 0.68f},
                    {mine_btn.x + 28.0f * ui, mine_btn.y + mine_btn.h * 0.48f},
                    {mine_btn.x + 20.0f * ui, mine_btn.y + mine_btn.h * 0.28f},
                    {mine_btn.x + 12.0f * ui, mine_btn.y + mine_btn.h * 0.48f}
                },
                5,
                &ms,
                0
            );
            if (r != VG_OK) return r;
        }
        {
            vg_stroke_style mis = icon;
            const float x0 = missile_btn.x + 10.0f * ui;
            const float y0 = missile_btn.y + missile_btn.h * 0.50f;
            const vg_vec2 tri[4] = {
                {x0 + 16.0f * ui, y0},
                {x0 + 4.0f * ui, y0 + 6.0f * ui},
                {x0 + 7.0f * ui, y0},
                {x0 + 4.0f * ui, y0 - 6.0f * ui}
            };
            r = vg_draw_polyline(ctx, tri, 4, &mis, 1);
            if (r != VG_OK) return r;
        }
        {
            vg_stroke_style as = icon;
            const vg_vec2 seg[2] = {
                {arc_btn.x + 10.0f * ui, arc_btn.y + arc_btn.h * 0.50f},
                {arc_btn.x + 28.0f * ui, arc_btn.y + arc_btn.h * 0.50f}
            };
            const vg_vec2 n0[2] = {
                {arc_btn.x + 10.0f * ui, arc_btn.y + arc_btn.h * 0.50f},
                {arc_btn.x + 13.0f * ui, arc_btn.y + arc_btn.h * 0.50f}
            };
            const vg_vec2 n1[2] = {
                {arc_btn.x + 25.0f * ui, arc_btn.y + arc_btn.h * 0.50f},
                {arc_btn.x + 28.0f * ui, arc_btn.y + arc_btn.h * 0.50f}
            };
            r = vg_draw_polyline(ctx, seg, 2, &as, 0);
            if (r != VG_OK) return r;
            r = vg_draw_polyline(ctx, n0, 2, &as, 0);
            if (r != VG_OK) return r;
            r = vg_draw_polyline(ctx, n1, 2, &as, 0);
            if (r != VG_OK) return r;
        }
        if (metrics->level_editor_background_mask_style == LEVELDEF_BG_MASK_WINDOWS) {
            vg_stroke_style ws = icon;
            const float cx = window_btn.x + 20.0f * ui;
            const float cy = window_btn.y + window_btn.h * 0.50f;
            const float ww = 18.0f * ui;
            const float wh = 12.0f * ui;
            const vg_vec2 trap[5] = {
                {cx - ww * 0.50f, cy + wh * 0.50f},
                {cx + ww * 0.50f, cy + wh * 0.50f},
                {cx + ww * 0.44f, cy - wh * 0.50f},
                {cx - ww * 0.44f, cy - wh * 0.50f},
                {cx - ww * 0.50f, cy + wh * 0.50f}
            };
            r = vg_draw_polyline(ctx, trap, 5, &ws, 0);
            if (r != VG_OK) return r;
        }
    }

    {
        const float len_screens = fmaxf(metrics->level_editor_level_length_screens, 1.0f);
        const float span_screens = fmaxf(len_screens - 1.0f, 0.0f);
        const float t01 = clampf(metrics->level_editor_timeline_01, 0.0f, 1.0f);
        const float window_w = timeline_track.w / len_screens;
        const float window_x = timeline_track.x + t01 * span_screens * window_w;
        vg_rect timeline_window = {window_x, timeline_track.y, window_w, timeline_track.h};
        vg_fill_style track_fill = {
            .intensity = 0.22f,
            .color = (vg_color){pal.primary_dim.r, pal.primary_dim.g, pal.primary_dim.b, 0.60f},
            .blend = VG_BLEND_ALPHA
        };
        vg_fill_style win_fill = {
            .intensity = 0.42f,
            .color = (vg_color){pal.primary.r, pal.primary.g, pal.primary.b, 0.36f},
            .blend = VG_BLEND_ALPHA
        };
        r = vg_fill_rect(ctx, timeline_track, &track_fill);
        if (r != VG_OK) return r;
        r = vg_draw_rect(ctx, timeline_track, &frame);
        if (r != VG_OK) return r;
        if (!enemy_spatial) {
            r = vg_fill_rect(ctx, timeline_enemy_track, &track_fill);
            if (r != VG_OK) return r;
            r = vg_draw_rect(ctx, timeline_enemy_track, &frame);
            if (r != VG_OK) return r;
            r = draw_text_vector_glow(
                ctx, "ENEMY EVENTS",
                (vg_vec2){timeline_enemy_track.x + 8.0f * ui, timeline_enemy_track.y + timeline_enemy_track.h - 10.0f * ui},
                8.4f * ui, 0.50f * ui, &frame, &text
            );
            if (r != VG_OK) return r;
        }
        r = vg_fill_rect(ctx, timeline_window, &win_fill);
        if (r != VG_OK) return r;
        r = vg_draw_rect(ctx, timeline_window, &frame);
        if (r != VG_OK) return r;
    }

    {
        const float len_screens = fmaxf(metrics->level_editor_level_length_screens, 1.0f);
        const float start_screen = clampf(metrics->level_editor_timeline_01, 0.0f, 1.0f) * fmaxf(len_screens - 1.0f, 0.0f);
        const float view_min = start_screen / len_screens;
        const float view_max = (start_screen + 1.0f) / len_screens;
        const int marker_n = metrics->level_editor_marker_count;
        const int selected = metrics->level_editor_selected_marker;
        int event_n = 0;
        if (!enemy_spatial) {
            for (int i = 0; i < marker_n && i < LEVEL_EDITOR_MAX_MARKERS; ++i) {
                const int kind_i = metrics->level_editor_marker_kind[i];
                const int track_i = metrics->level_editor_marker_track[i];
                const int is_enemy_i =
                    (kind_i == 2 || kind_i == 3 || kind_i == 4 || kind_i == 5 || kind_i == 6 ||
                     kind_i == 10 || kind_i == 11 || kind_i == 12 || kind_i == 15);
                const int event_item_i = is_enemy_i && (track_i == 1);
                if (event_item_i) {
                    event_n += 1;
                }
            }
        }
        for (int i = 0; i < marker_n && i < LEVEL_EDITOR_MAX_MARKERS; ++i) {
            const float mx01 = clampf(metrics->level_editor_marker_x01[i], 0.0f, 1.0f);
            const float my01 = clampf(metrics->level_editor_marker_y01[i], 0.0f, 1.0f);
            const int kind = metrics->level_editor_marker_kind[i];
            const int track = metrics->level_editor_marker_track[i];
            const int is_enemy = (kind == 2 || kind == 3 || kind == 4 || kind == 5 || kind == 6 || kind == 10 || kind == 11 || kind == 12 || kind == 15);
            const int event_item = is_enemy && (track == 1);
            const vg_color c = level_editor_marker_color(&pal, kind);

            vg_stroke_style mk = frame;
            mk.width_px = 1.4f * ui;
            mk.color = c;
            mk.intensity = (i == selected) ? 1.45f : 1.0f;

            float tx = timeline_track.x + mx01 * timeline_track.w;
            const vg_vec2 tick[2] = {
                {tx, timeline_track.y + 2.0f * ui},
                {tx, timeline_track.y + timeline_track.h - 2.0f * ui}
            };
            if ((enemy_spatial || !event_item) && kind != 9) {
                r = vg_draw_polyline(ctx, tick, 2, &mk, 0);
                if (r != VG_OK) return r;
            } else {
                int rank = 0;
                for (int j = 0; j < marker_n && j < LEVEL_EDITOR_MAX_MARKERS; ++j) {
                    const int kind_j = metrics->level_editor_marker_kind[j];
                    const int track_j = metrics->level_editor_marker_track[j];
                    const int is_enemy_j =
                        (kind_j == 2 || kind_j == 3 || kind_j == 4 || kind_j == 5 || kind_j == 6 ||
                         kind_j == 10 || kind_j == 11 || kind_j == 12 || kind_j == 15);
                    const int event_item_j = is_enemy_j && (track_j == 1);
                    if (!event_item_j) {
                        continue;
                    }
                    const int oj = metrics->level_editor_marker_order[j];
                    const int oi = metrics->level_editor_marker_order[i];
                    if (oj < oi || (oj == oi && j < i)) {
                        rank += 1;
                    }
                }
                const float slot_w = (event_n > 0) ? (timeline_enemy_track.w / (float)event_n) : timeline_enemy_track.w;
                tx = timeline_enemy_track.x + ((float)rank + 0.5f) * slot_w;
                const float bw = fmaxf(10.0f * ui, slot_w - 2.0f * ui);
                const vg_rect block = {
                    tx - bw * 0.5f,
                    timeline_enemy_track.y + 2.0f * ui,
                    bw,
                    timeline_enemy_track.h - 4.0f * ui
                };
                vg_fill_style bf = {
                    .intensity = 0.32f,
                    .color = (vg_color){c.r, c.g, c.b, 0.38f},
                    .blend = VG_BLEND_ALPHA
                };
                r = vg_fill_rect(ctx, block, &bf);
                if (r != VG_OK) return r;
                r = vg_draw_rect(ctx, block, &mk);
                if (r != VG_OK) return r;
            }

            if (mx01 < view_min || mx01 > view_max) {
                continue;
            }
            if (!enemy_spatial && event_item) {
                continue;
            }
            const float vx = viewport.x + ((mx01 - view_min) / fmaxf(view_max - view_min, 1.0e-5f)) * viewport.w;
            const float vy = viewport.y + my01 * viewport.h;
            const float glyph_scale = (i == selected) ? 1.20f : 1.0f;
            if (kind == 1) {
                const float len = fmaxf(metrics->level_editor_marker_a[i] * viewport.h, 8.0f * ui);
                const float half_deg = fmaxf(metrics->level_editor_marker_b[i], 2.0f);
                const float sweep_speed = metrics->level_editor_marker_c[i];
                const float sweep_angle_deg = fmaxf(metrics->level_editor_marker_d[i], 0.0f);
                const float sweep_amp = sweep_angle_deg * 0.5f;
                const int sweep_type = clampi((int)lroundf(metrics->level_editor_marker_g[i]), 0, 2);
                const float base = metrics->level_editor_marker_delay_s[i] * (3.14159265f / 180.0f);
                const float phase = t_s * sweep_speed;
                float q = 0.0f;
                if (sweep_type == 2) {
                    q = 0.0f;
                } else if (sweep_type == 0) {
                    const float tri = (2.0f / 3.14159265359f) * asinf(sinf(phase));
                    q = clampf(tri, -1.0f, 1.0f);
                } else {
                    q = sinf(phase);
                }
                const float a_center = (sweep_type == 2)
                    ? (base + phase)
                    : (base + q * (sweep_amp * (3.14159265f / 180.0f)));
                const float half = half_deg * (3.14159265f / 180.0f);
                const float a0 = a_center - half;
                const float a1 = a_center + half;
                const vg_vec2 tri[3] = {
                    {vx, vy},
                    {vx + cosf(a0) * len, vy + sinf(a0) * len},
                    {vx + cosf(a1) * len, vy + sinf(a1) * len}
                };
                vg_fill_style cone = {
                    .intensity = 0.20f,
                    .color = (vg_color){pal.primary_dim.r, pal.primary_dim.g, pal.primary_dim.b, 0.20f},
                    .blend = VG_BLEND_ADDITIVE
                };
                r = vg_fill_convex(ctx, tri, 3, &cone);
                if (r != VG_OK) return r;
                {
                    const vg_vec2 left[2] = {{vx, vy}, tri[1]};
                    const vg_vec2 right[2] = {{vx, vy}, tri[2]};
                    r = vg_draw_polyline(ctx, left, 2, &mk, 0);
                    if (r != VG_OK) return r;
                    r = vg_draw_polyline(ctx, right, 2, &mk, 0);
                    if (r != VG_OK) return r;
                }
                {
                    vg_fill_style src = {
                        .intensity = 0.90f,
                        .color = (vg_color){1.0f, 0.30f, 0.30f, 0.95f},
                        .blend = VG_BLEND_ALPHA
                    };
                    r = vg_fill_circle(ctx, (vg_vec2){vx, vy}, 6.2f * ui * glyph_scale, &src, 16);
                }
            } else if (kind == 0) {
                r = draw_editor_diamond(ctx, (vg_vec2){vx, vy}, 10.0f * ui * glyph_scale, &mk);
                if (r != VG_OK) return r;
                {
                    vg_stroke_style mk2 = mk;
                    mk2.intensity *= 0.74f;
                    r = draw_editor_diamond(ctx, (vg_vec2){vx, vy}, 6.0f * ui * glyph_scale, &mk2);
                }
            } else if (kind == 5 || kind == 10 || kind == 11 || kind == 12 || kind == 15) {
                vg_stroke_style mk2 = mk;
                mk2.intensity *= 0.78f;
                r = draw_editor_diamond(ctx, (vg_vec2){vx, vy}, 21.0f * ui * glyph_scale, &mk);
                if (r != VG_OK) return r;
                r = draw_editor_diamond(ctx, (vg_vec2){vx + 22.0f * ui, vy + 9.0f * ui}, 16.2f * ui * glyph_scale, &mk2);
                if (r != VG_OK) return r;
                r = draw_editor_diamond(ctx, (vg_vec2){vx - 20.0f * ui, vy - 11.0f * ui}, 14.7f * ui * glyph_scale, &mk2);
            } else if (kind == 6) {
                const vg_vec2 tri[4] = {
                    {vx - 10.0f * ui, vy - 9.0f * ui},
                    {vx + 11.0f * ui, vy},
                    {vx - 10.0f * ui, vy + 9.0f * ui},
                    {vx - 10.0f * ui, vy - 9.0f * ui}
                };
                r = vg_draw_polyline(ctx, tri, 4, &mk, 0);
            } else if (kind == 7) {
                const float rr = 14.0f * ui * glyph_scale;
                vg_vec2 hex[7];
                for (int k = 0; k < 6; ++k) {
                    const float a = ((float)k / 6.0f) * 6.2831853f;
                    hex[k] = (vg_vec2){vx + cosf(a) * rr, vy + sinf(a) * rr};
                }
                hex[6] = hex[0];
                r = vg_draw_polyline(ctx, hex, 7, &mk, 0);
            } else if (kind == 8) {
                const float sx = 1.5f * ui * glyph_scale;
                const vg_vec2 tri[4] = {
                    {vx + 12.0f * sx, vy},
                    {vx - 8.0f * sx, vy + 6.0f * sx},
                    {vx - 4.0f * sx, vy},
                    {vx - 8.0f * sx, vy - 6.0f * sx}
                };
                r = vg_draw_polyline(ctx, tri, 4, &mk, 1);
            } else if (kind == 13) {
                const float rr = fmaxf(metrics->level_editor_marker_c[i], 8.0f * ui) * 0.16f;
                const vg_vec2 n0[2] = {{vx - rr, vy}, {vx - rr * 0.65f, vy}};
                const vg_vec2 n1[2] = {{vx + rr * 0.65f, vy}, {vx + rr, vy}};
                const vg_vec2 arc[6] = {
                    {vx - rr * 0.45f, vy},
                    {vx - rr * 0.28f, vy - rr * 0.20f},
                    {vx - rr * 0.10f, vy + rr * 0.12f},
                    {vx + rr * 0.10f, vy - rr * 0.18f},
                    {vx + rr * 0.28f, vy + rr * 0.10f},
                    {vx + rr * 0.45f, vy}
                };
                r = vg_draw_polyline(ctx, n0, 2, &mk, 0);
                if (r != VG_OK) return r;
                r = vg_draw_polyline(ctx, n1, 2, &mk, 0);
                if (r != VG_OK) return r;
                r = vg_draw_polyline(ctx, arc, 6, &mk, 0);
                if (r != VG_OK) return r;
                {
                    vg_fill_style af = {
                        .intensity = 0.95f,
                        .color = (vg_color){mk.color.r, mk.color.g, mk.color.b, 0.98f},
                        .blend = VG_BLEND_ALPHA
                    };
                    r = vg_fill_circle(ctx, (vg_vec2){vx, vy}, 1.7f * ui, &af, 12);
                }
            } else if (kind == 14) {
                const float ww = fmaxf(metrics->level_editor_marker_a[i] * viewport.w, 12.0f * ui);
                const float wh = fmaxf(metrics->level_editor_marker_b[i] * viewport.h, 8.0f * ui);
                const int flip_vertical = (metrics->level_editor_marker_c[i] >= 0.5f) ? 1 : 0;
                const float half_top = ww * (flip_vertical ? 0.44f : 0.50f);
                const float half_bottom = ww * (flip_vertical ? 0.50f : 0.44f);
                const vg_vec2 trap[5] = {
                    {vx - half_top, vy + wh * 0.5f},
                    {vx + half_top, vy + wh * 0.5f},
                    {vx + half_bottom, vy - wh * 0.5f},
                    {vx - half_bottom, vy - wh * 0.5f},
                    {vx - half_top, vy + wh * 0.5f}
                };
                r = vg_draw_polyline(ctx, trap, 5, &mk, 0);
            } else if (kind == 9) {
                const int prefab = (int)lroundf(metrics->level_editor_marker_a[i]);
                const int layer = (int)lroundf(metrics->level_editor_marker_b[i]);
                vg_stroke_style st = mk;
                if (layer > 0) {
                    st.color = (vg_color){pal.secondary.r, pal.secondary.g, pal.secondary.b, 1.0f};
                    st.intensity *= 1.12f;
                } else {
                    st.color = pal.primary_dim;
                    st.intensity *= 0.95f;
                }
                {
                    const float len_screens = fmaxf(metrics->level_editor_level_length_screens, 1.0f);
                    const float start_screen = clampf(metrics->level_editor_timeline_01, 0.0f, 1.0f) * fmaxf(len_screens - 1.0f, 0.0f);
                    const float view_min_local = start_screen / len_screens;
                    const float view_max_local = (start_screen + 1.0f) / len_screens;
                    const float view_span_local = fmaxf(view_max_local - view_min_local, 1.0e-6f);
                    const int gx_steps_total = editor_structure_grid_steps_x(len_screens);
                    const int gy_steps = editor_structure_grid_steps_y();
                    const int gx = clampi((int)lroundf(mx01 * (float)gx_steps_total), 0, gx_steps_total);
                    const int gy = clampi((int)lroundf(my01 * (float)gy_steps), 0, gy_steps);
                    const float x0 = (float)gx / (float)gx_steps_total;
                    const float y0 = (float)gy / (float)gy_steps;
                    const float visible_x_steps = (float)gx_steps_total * view_span_local;
                    const float cell_w = viewport.w / fmaxf(visible_x_steps, 1.0f);
                    const float cell_h = viewport.h / (float)gy_steps;
                    const float bx = viewport.x + ((x0 - view_min_local) / view_span_local) * viewport.w;
                    const float by = viewport.y + y0 * viewport.h;
                    vg_fill_style fill = {
                        .intensity = 0.55f,
                        .color = (vg_color){pal.primary_dim.r, pal.primary_dim.g, pal.primary_dim.b, 0.15f},
                        .blend = VG_BLEND_ALPHA
                    };
                    r = draw_structure_prefab_tile(
                        ctx,
                        prefab,
                        layer,
                        bx,
                        by,
                        cell_w,
                        cell_h,
                        (int)lroundf(metrics->level_editor_marker_c[i]),
                        (((int)lroundf(metrics->level_editor_marker_d[i])) & 1),
                        ((((int)lroundf(metrics->level_editor_marker_d[i])) >> 1) & 1),
                        &st,
                        &fill
                    );
                }
            } else {
                r = draw_editor_diamond(ctx, (vg_vec2){vx, vy}, 19.5f * ui * glyph_scale, &mk);
            }
            if (r != VG_OK) return r;
        }
        {
            /* Player spawn representation anchored in world-space (not screen-space). */
            const float su = fmaxf(0.5f, fminf(w / 1920.0f, h / 1080.0f));
            const float len_screens = fmaxf(metrics->level_editor_level_length_screens, 1.0f);
            const float start_screen = clampf(metrics->level_editor_timeline_01, 0.0f, 1.0f) * fmaxf(len_screens - 1.0f, 0.0f);
            const float view_min = start_screen / len_screens;
            const float view_max = (start_screen + 1.0f) / len_screens;
            const float player_world_x = 170.0f * su;
            const float player_x01 = clampf((player_world_x / fmaxf(w, 1.0f)) / len_screens, 0.0f, 1.0f);
            const float px = viewport.x + ((player_x01 - view_min) / fmaxf(view_max - view_min, 1.0e-5f)) * viewport.w;
            const float py = viewport.y + viewport.h * 0.50f;
            if (px >= viewport.x - 18.0f * ui && px <= viewport.x + viewport.w + 18.0f * ui) {
                vg_stroke_style ps = frame;
                ps.width_px = 1.5f * ui;
                ps.intensity = 1.08f;
                ps.color = pal.ship;
                r = draw_editor_ship(ctx, (vg_vec2){px, py}, 0.85f * ui, &ps);
                if (r != VG_OK) return r;
                r = draw_text_vector_glow(ctx, "PLAYER", (vg_vec2){px - 20.0f * ui, py - 18.0f * ui}, 7.6f * ui, 0.45f * ui, &frame, &text);
                if (r != VG_OK) return r;
            }
        }
    }

    {
        const int sel = metrics->level_editor_selected_marker;
        char line0[96];
        if (sel >= 0 && sel < metrics->level_editor_marker_count && sel < LEVEL_EDITOR_MAX_MARKERS) {
            const int kind = metrics->level_editor_marker_kind[sel];
            snprintf(line0, sizeof(line0), "SELECTED %s", level_editor_marker_name(kind));
            const float tx = props.x + 12.0f * ui;
            float ty = props.y + props.h - 42.0f * ui;
            r = draw_text_vector_glow(ctx, line0, (vg_vec2){tx, ty}, 11.2f * ui, 0.72f * ui, &frame, &text);
            if (r != VG_OK) return r;
            ty -= 28.0f * ui;
            {
                const char* labels[10] = {0};
                char values[10][32];
                memset(values, 0, sizeof(values));
                const int pn = editor_marker_properties_text(kind, metrics, sel, labels, values, 10);
                int selected_prop = metrics->level_editor_selected_property;
                if (selected_prop < 0) selected_prop = 0;
                if (selected_prop >= pn) selected_prop = pn - 1;
                for (int i = 0; i < pn; ++i) {
                    const vg_rect rb = {tx, ty - 22.0f * ui, props.w - 24.0f * ui, 24.0f * ui};
                    const float row_text_px = 10.4f * ui;
                    const float tracking = row_text_px * 0.08f;
                    const float pad = 8.0f * ui;
                    const float text_y = rb.y + (rb.h - row_text_px) * 0.5f;
                    vg_stroke_style row_text = text;
                    const float value_w = vg_measure_text(values[i], row_text_px, tracking);
                    const vg_vec2 label_pos = {rb.x + pad, text_y};
                    const vg_vec2 value_pos = {rb.x + rb.w - pad - value_w, text_y};
                    r = draw_ui_button_shaded(ctx, rb, "", row_text_px, &frame, &text, (i == selected_prop) ? 1 : 0);
                    if (r != VG_OK) return r;
                    if (i == selected_prop) {
                        row_text.intensity *= 1.12f;
                    }
                    r = vg_draw_text(ctx, labels[i], label_pos, row_text_px, tracking, &row_text, NULL);
                    if (r != VG_OK) return r;
                    r = vg_draw_text(ctx, values[i], value_pos, row_text_px, tracking, &row_text, NULL);
                    if (r != VG_OK) return r;
                    ty -= 32.0f * ui;
                }
                r = draw_text_vector_glow(ctx, "TAB FIELD  LEFT/RIGHT EDIT", (vg_vec2){tx, ty - 4.0f * ui}, 9.2f * ui, 0.52f * ui, &frame, &text);
                if (r != VG_OK) return r;
            }
        } else {
            char line1[96];
            char status_disp[128];
            editor_sanitize_label(metrics->level_editor_status_text ? metrics->level_editor_status_text : "ready", status_disp, sizeof(status_disp));
            snprintf(line0, sizeof(line0), "LEVEL PROPERTIES");
            snprintf(line1, sizeof(line1), "OBJECTS %d", metrics->level_editor_marker_count);
            const float tx = props.x + 12.0f * ui;
            float ty = props.y + props.h - 42.0f * ui;
            r = draw_text_vector_glow(ctx, line0, (vg_vec2){tx, ty}, 11.2f * ui, 0.72f * ui, &frame, &text);
            if (r != VG_OK) return r;
            ty -= 28.0f * ui;
            r = draw_text_vector_glow(ctx, line1, (vg_vec2){tx, ty}, 10.8f * ui, 0.68f * ui, &frame, &text);
            if (r != VG_OK) return r;
            ty -= 30.0f * ui;
            {
                int selected_prop = metrics->level_editor_selected_property;
                if (selected_prop < 0) selected_prop = 0;
                if (selected_prop > 6) selected_prop = 6;
                char row[96];
                const vg_rect rb0 = {tx, ty - 22.0f * ui, props.w - 24.0f * ui, 24.0f * ui};
                snprintf(row, sizeof(row), "WAVE MODE      %s", editor_wave_mode_name(metrics->level_editor_wave_mode));
                r = draw_ui_button_shaded(ctx, rb0, row, 10.4f * ui, &frame, &text, (selected_prop == 0) ? 1 : 0);
                if (r != VG_OK) return r;
                ty -= 32.0f * ui;
                const vg_rect rb1 = {tx, ty - 22.0f * ui, props.w - 24.0f * ui, 24.0f * ui};
                snprintf(row, sizeof(row), "RENDER STYLE   %s", editor_render_style_name(metrics->level_editor_render_style));
                r = draw_ui_button_shaded(ctx, rb1, row, 10.4f * ui, &frame, &text, (selected_prop == 1) ? 1 : 0);
                if (r != VG_OK) return r;
                ty -= 32.0f * ui;
                const vg_rect rb2 = {tx, ty - 22.0f * ui, props.w - 24.0f * ui, 24.0f * ui};
                snprintf(row, sizeof(row), "THEME          %s", editor_theme_palette_name(metrics->level_editor_theme_palette));
                r = draw_ui_button_shaded(ctx, rb2, row, 10.4f * ui, &frame, &text, (selected_prop == 2) ? 1 : 0);
                if (r != VG_OK) return r;
                ty -= 32.0f * ui;
                const vg_rect rb3 = {tx, ty - 22.0f * ui, props.w - 24.0f * ui, 24.0f * ui};
                snprintf(row, sizeof(row), "BACKGROUND     %s", editor_background_style_name(metrics->level_editor_background_style));
                r = draw_ui_button_shaded(ctx, rb3, row, 10.4f * ui, &frame, &text, (selected_prop == 3) ? 1 : 0);
                if (r != VG_OK) return r;
                ty -= 32.0f * ui;
                const vg_rect rb4 = {tx, ty - 22.0f * ui, props.w - 24.0f * ui, 24.0f * ui};
                snprintf(row, sizeof(row), "BG MASK        %s", editor_background_mask_style_name(metrics->level_editor_background_mask_style));
                r = draw_ui_button_shaded(ctx, rb4, row, 10.4f * ui, &frame, &text, (selected_prop == 4) ? 1 : 0);
                if (r != VG_OK) return r;
                ty -= 32.0f * ui;
                const vg_rect rb5 = {tx, ty - 22.0f * ui, props.w - 24.0f * ui, 24.0f * ui};
                snprintf(row, sizeof(row), "LENGTH         %.1f", metrics->level_editor_level_length_screens);
                r = draw_ui_button_shaded(ctx, rb5, row, 10.4f * ui, &frame, &text, (selected_prop == 5) ? 1 : 0);
                if (r != VG_OK) return r;
                ty -= 32.0f * ui;
                const vg_rect rb6 = {tx, ty - 22.0f * ui, props.w - 24.0f * ui, 24.0f * ui};
                snprintf(row, sizeof(row), "POWERUP DROP   %.2f", metrics->level_editor_powerup_drop_chance);
                r = draw_ui_button_shaded(ctx, rb6, row, 10.4f * ui, &frame, &text, (selected_prop == 6) ? 1 : 0);
                if (r != VG_OK) return r;
            }
            ty -= 34.0f * ui;
            r = draw_text_vector_glow(ctx, status_disp, (vg_vec2){tx, ty}, 10.0f * ui, 0.58f * ui, &frame, &text);
            if (r != VG_OK) return r;
        }
    }

    r = draw_text_vector_glow(
        ctx,
        "ESC BACK  ENTER LOAD  DRAG TIMELINE  CLICK SELECT/PLACE  DRAG ENTITY TO PLACE  LEFT/RIGHT EDIT",
        (vg_vec2){timeline.x, timeline.y - 14.0f * ui},
        9.0f * ui,
        0.55f * ui,
        &frame,
        &text
    );
    if (r != VG_OK) return r;
    if (metrics->level_editor_drag_active &&
        (metrics->level_editor_drag_kind == 5 || metrics->level_editor_drag_kind == 10 || metrics->level_editor_drag_kind == 11 || metrics->level_editor_drag_kind == 12 || metrics->level_editor_drag_kind == 15 || metrics->level_editor_drag_kind == 1 || metrics->level_editor_drag_kind == 6 || metrics->level_editor_drag_kind == 7 || metrics->level_editor_drag_kind == 8 || metrics->level_editor_drag_kind == 9 || metrics->level_editor_drag_kind == 13 || metrics->level_editor_drag_kind == 14)) {
        vg_stroke_style gs = frame;
        gs.intensity = 1.2f;
        gs.color = level_editor_marker_color(&pal, metrics->level_editor_drag_kind);
        if (metrics->level_editor_drag_kind == 1) {
            r = draw_editor_diamond(ctx, (vg_vec2){metrics->level_editor_drag_x, metrics->level_editor_drag_y}, 10.0f * ui, &gs);
        } else if (metrics->level_editor_drag_kind == 6) {
            vg_fill_style af = {
                .intensity = 0.90f,
                .color = (vg_color){gs.color.r, gs.color.g, gs.color.b, 0.75f},
                .blend = VG_BLEND_ALPHA
            };
            r = vg_fill_circle(ctx, (vg_vec2){metrics->level_editor_drag_x, metrics->level_editor_drag_y}, 6.0f * ui, &af, 20);
        } else if (metrics->level_editor_drag_kind == 7) {
            const float rr = 8.0f * ui;
            vg_vec2 hex[7];
            for (int k = 0; k < 6; ++k) {
                const float a = ((float)k / 6.0f) * 6.2831853f;
                hex[k] = (vg_vec2){metrics->level_editor_drag_x + cosf(a) * rr, metrics->level_editor_drag_y + sinf(a) * rr};
            }
            hex[6] = hex[0];
            r = vg_draw_polyline(ctx, hex, 7, &gs, 0);
        } else if (metrics->level_editor_drag_kind == 8) {
            const float sx = 1.5f * ui;
            const vg_vec2 tri[4] = {
                {metrics->level_editor_drag_x + 12.0f * sx, metrics->level_editor_drag_y},
                {metrics->level_editor_drag_x - 8.0f * sx, metrics->level_editor_drag_y + 6.0f * sx},
                {metrics->level_editor_drag_x - 4.0f * sx, metrics->level_editor_drag_y},
                {metrics->level_editor_drag_x - 8.0f * sx, metrics->level_editor_drag_y - 6.0f * sx}
            };
            r = vg_draw_polyline(ctx, tri, 4, &gs, 1);
        } else if (metrics->level_editor_drag_kind == 13) {
            const float rr = 10.0f * ui;
            const vg_vec2 n0[2] = {
                {metrics->level_editor_drag_x - rr, metrics->level_editor_drag_y},
                {metrics->level_editor_drag_x - rr * 0.65f, metrics->level_editor_drag_y}
            };
            const vg_vec2 n1[2] = {
                {metrics->level_editor_drag_x + rr * 0.65f, metrics->level_editor_drag_y},
                {metrics->level_editor_drag_x + rr, metrics->level_editor_drag_y}
            };
            const vg_vec2 arc[6] = {
                {metrics->level_editor_drag_x - rr * 0.45f, metrics->level_editor_drag_y},
                {metrics->level_editor_drag_x - rr * 0.28f, metrics->level_editor_drag_y - rr * 0.20f},
                {metrics->level_editor_drag_x - rr * 0.10f, metrics->level_editor_drag_y + rr * 0.12f},
                {metrics->level_editor_drag_x + rr * 0.10f, metrics->level_editor_drag_y - rr * 0.18f},
                {metrics->level_editor_drag_x + rr * 0.28f, metrics->level_editor_drag_y + rr * 0.10f},
                {metrics->level_editor_drag_x + rr * 0.45f, metrics->level_editor_drag_y}
            };
            r = vg_draw_polyline(ctx, n0, 2, &gs, 0);
            if (r != VG_OK) return r;
            r = vg_draw_polyline(ctx, n1, 2, &gs, 0);
            if (r != VG_OK) return r;
            r = vg_draw_polyline(ctx, arc, 6, &gs, 0);
            if (r != VG_OK) return r;
            {
                vg_fill_style af = {
                    .intensity = 0.95f,
                    .color = (vg_color){gs.color.r, gs.color.g, gs.color.b, 0.98f},
                    .blend = VG_BLEND_ALPHA
                };
                r = vg_fill_circle(ctx, (vg_vec2){metrics->level_editor_drag_x, metrics->level_editor_drag_y}, 1.7f * ui, &af, 12);
            }
        } else if (metrics->level_editor_drag_kind == 14) {
            const float cx = metrics->level_editor_drag_x;
            const float cy = metrics->level_editor_drag_y;
            const float ww = 26.0f * ui;
            const float wh = 16.0f * ui;
            const vg_vec2 trap[5] = {
                {cx - ww * 0.50f, cy + wh * 0.50f},
                {cx + ww * 0.50f, cy + wh * 0.50f},
                {cx + ww * 0.44f, cy - wh * 0.50f},
                {cx - ww * 0.44f, cy - wh * 0.50f},
                {cx - ww * 0.50f, cy + wh * 0.50f}
            };
            r = vg_draw_polyline(ctx, trap, 5, &gs, 0);
            if (r != VG_OK) return r;
        } else if (metrics->level_editor_drag_kind == 9) {
            const int prefab = metrics->level_editor_structure_tool_selected > 0
                ? (metrics->level_editor_structure_tool_selected - 1)
                : 0;
            const int layer = (prefab >= 5) ? 1 : 0;
            const float len_screens = fmaxf(metrics->level_editor_level_length_screens, 1.0f);
            const float start_screen = clampf(metrics->level_editor_timeline_01, 0.0f, 1.0f) * fmaxf(len_screens - 1.0f, 0.0f);
            const float view_min = start_screen / len_screens;
            const float view_max = (start_screen + 1.0f) / len_screens;
            const float view_span = fmaxf(view_max - view_min, 1.0e-6f);
            const int gx_steps_total = editor_structure_grid_steps_x(len_screens);
            const int gy_steps = editor_structure_grid_steps_y();
            const float mx01 = clampf((metrics->level_editor_drag_x - viewport.x) / fmaxf(viewport.w, 1.0f), 0.0f, 1.0f);
            const float my01 = clampf((metrics->level_editor_drag_y - viewport.y) / fmaxf(viewport.h, 1.0f), 0.0f, 1.0f);
            const float x01 = view_min + mx01 * fmaxf(view_max - view_min, 1.0e-6f);
            const int gx = clampi((int)floorf(x01 * (float)gx_steps_total + 1.0e-6f), 0, gx_steps_total);
            const int gy = clampi((int)floorf(my01 * (float)gy_steps + 1.0e-6f), 0, gy_steps);
            const float gx01 = (float)gx / (float)gx_steps_total;
            const float gy01 = (float)gy / (float)gy_steps;
            const float visible_x_steps = (float)gx_steps_total * view_span;
            const float cell_w = viewport.w / fmaxf(visible_x_steps, 1.0f);
            const float cell_h = viewport.h / (float)gy_steps;
            const float bx = viewport.x + ((gx01 - view_min) / view_span) * viewport.w;
            const float by = viewport.y + gy01 * viewport.h;
            gs.color = (layer == 0) ? pal.primary_dim : pal.secondary;
            {
                vg_fill_style fill = {
                    .intensity = 0.60f,
                    .color = (vg_color){gs.color.r, gs.color.g, gs.color.b, 0.12f},
                    .blend = VG_BLEND_ALPHA
                };
                r = draw_structure_prefab_tile(
                    ctx,
                    prefab,
                    layer,
                    bx,
                    by,
                    cell_w,
                    cell_h,
                    0,
                    0,
                    0,
                    &gs,
                    &fill
                );
            }
        } else {
            r = draw_editor_diamond(ctx, (vg_vec2){metrics->level_editor_drag_x, metrics->level_editor_drag_y}, 7.0f * ui, &gs);
        }
        if (r != VG_OK) return r;
    }
    return r;
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
    r = draw_text_vector_glow(ctx, "ESC BACK   LEFT/RIGHT SELECT   ENTER ACCEPT", (vg_vec2){panel.x + panel.w * 0.03f, panel.y + panel.h * 0.03f}, 11.8f * ui, 0.90f * ui, &frame, &txt);
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
            .threshold = metrics->nick_threshold,
            .contrast = metrics->nick_contrast,
            .scanline_pitch_px = metrics->nick_scanline_pitch_px,
            .min_line_width_px = metrics->nick_min_line_width_px,
            .max_line_width_px = metrics->nick_max_line_width_px,
            .line_jitter_px = 0.0f,
            .cell_width_px = 0.0f,
            .cell_height_px = 0.0f,
            .block_levels = 0,
            .intensity = metrics->nick_intensity,
            .tint_color = pal.secondary,
            .blend = VG_BLEND_ALPHA,
            .use_crt_palette = 0,
            .use_context_palette = 0,
            .palette_index = 0,
            .invert = metrics->nick_invert,
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

static vg_result draw_defender_industrial_parallax(
    vg_context* ctx,
    float w,
    float h,
    float cam_x,
    const palette_theme* pal,
    const vg_stroke_style* halo,
    const vg_stroke_style* main
) {
    if (!ctx || !pal || !halo || !main) {
        return VG_OK;
    }

    enum { LAYERS = 6 };
    const float skyline_top_cap = h * 0.62f;

    for (int li = 0; li < LAYERS; ++li) {
        const float lf = (float)li / (float)(LAYERS - 1); /* far->near */
        const float depth = lf;
        const float parallax = 0.14f + depth * 1.52f;
        const float module_w = 72.0f + depth * 86.0f;
        /* In this Y-up scene, far layers should sit much higher than near layers. */
        const float depth_inv = (1.0f - depth);
        const float ground_y = h * (0.22f * depth_inv * depth_inv);
        const float h_min = h * (0.10f + depth * 0.05f);
        const float h_max = h * (0.23f + depth * 0.11f);

        vg_color layer_base = pal->primary_dim;
        layer_base.r = clampf(layer_base.r * (0.62f + depth * 0.50f), 0.0f, 1.0f);
        layer_base.g = clampf(layer_base.g * (0.62f + depth * 0.50f), 0.0f, 1.0f);
        layer_base.b = clampf(layer_base.b * (0.62f + depth * 0.50f), 0.0f, 1.0f);
        layer_base.a = clampf(0.06f + depth * 0.12f, 0.0f, 1.0f);
        vg_fill_style fill = make_fill(0.56f + depth * 0.16f, layer_base, VG_BLEND_ALPHA);

        vg_stroke_style sh = *halo;
        vg_stroke_style sm = *main;
        sh.width_px *= 0.78f + depth * 0.72f;
        sm.width_px *= 0.72f + depth * 0.66f;
        sh.intensity *= 0.30f + depth * 0.42f;
        sm.intensity *= 0.34f + depth * 0.46f;
        sh.color.a *= 0.22f + depth * 0.42f;
        sm.color.a *= 0.24f + depth * 0.46f;

        const float world_l = cam_x * parallax - w * 0.60f;
        const float world_r = cam_x * parallax + w * 0.60f;
        const int ix0 = (int)floorf(world_l / module_w) - 2;
        const int ix1 = (int)floorf(world_r / module_w) + 2;

        for (int ix = ix0; ix <= ix1; ++ix) {
            const float rn0 = hash01_2i(ix, li * 17 + 5);
            const float rn1 = hash01_2i(ix, li * 17 + 9);
            const float rn2 = hash01_2i(ix, li * 17 + 13);
            const float wx = ((float)ix + 0.5f) * module_w;
            const float cx = wx - cam_x * parallax + w * 0.5f;
            const float bw = module_w * (0.52f + rn0 * 0.44f);
            const float bh = h_min + (h_max - h_min) * rn1;
            if (cx + bw * 0.6f < -32.0f || cx - bw * 0.6f > w + 32.0f) {
                continue;
            }

            const float top_y = fminf(ground_y + bh, skyline_top_cap);
            const float x0 = cx - bw * 0.5f;
            const float x1 = cx + bw * 0.5f;
            const vg_vec2 body[4] = {
                {x0, ground_y}, {x1, ground_y}, {x1, top_y}, {x0, top_y}
            };
            vg_result r = vg_fill_convex(ctx, body, 4, &fill);
            if (r != VG_OK) {
                return r;
            }
            const int is_front_layer = (li == LAYERS - 1) ? 1 : 0;
            if (is_front_layer) {
                const vg_vec2 no_base[4] = {
                    {x0, ground_y}, {x0, top_y}, {x1, top_y}, {x1, ground_y}
                };
                r = vg_draw_polyline(ctx, no_base, 4, &sh, 0);
            } else {
                r = vg_draw_polyline(ctx, body, 4, &sh, 1);
            }
            if (r != VG_OK) {
                return r;
            }
            if (is_front_layer) {
                const vg_vec2 no_base[4] = {
                    {x0, ground_y}, {x0, top_y}, {x1, top_y}, {x1, ground_y}
                };
                r = vg_draw_polyline(ctx, no_base, 4, &sm, 0);
            } else {
                r = vg_draw_polyline(ctx, body, 4, &sm, 1);
            }
            if (r != VG_OK) {
                return r;
            }

            if (rn2 > 0.45f) {
                const float stack_w = bw * (0.14f + 0.10f * rn0);
                const float stack_h = bh * (0.22f + 0.32f * rn2);
                const float sx = cx + bw * (rn1 < 0.5f ? -0.22f : 0.22f);
                const vg_vec2 stack[4] = {
                    {sx - stack_w * 0.5f, top_y},
                    {sx + stack_w * 0.5f, top_y},
                    {sx + stack_w * 0.5f, fminf(top_y + stack_h, skyline_top_cap)},
                    {sx - stack_w * 0.5f, fminf(top_y + stack_h, skyline_top_cap)}
                };
                r = vg_fill_convex(ctx, stack, 4, &fill);
                if (r != VG_OK) {
                    return r;
                }
                r = vg_draw_polyline(ctx, stack, 4, &sh, 1);
                if (r != VG_OK) {
                    return r;
                }
                r = vg_draw_polyline(ctx, stack, 4, &sm, 1);
                if (r != VG_OK) {
                    return r;
                }
            }

            if (rn1 > 0.44f) {
                const float tank_w = bw * (0.20f + 0.10f * rn2);
                const float tank_h = bh * (0.20f + 0.24f * rn0);
                const float sep = tank_w * 0.18f;
                const float tx0 = cx - tank_w - sep * 0.5f;
                const float tx1 = cx + sep * 0.5f;
                const float ty0 = ground_y;
                const float ty1 = fminf(ground_y + tank_h, skyline_top_cap);
                const vg_vec2 tank_a[4] = {
                    {tx0, ty0}, {tx0 + tank_w, ty0}, {tx0 + tank_w, ty1}, {tx0, ty1}
                };
                const vg_vec2 tank_b[4] = {
                    {tx1, ty0}, {tx1 + tank_w, ty0}, {tx1 + tank_w, ty1}, {tx1, ty1}
                };
                r = vg_fill_convex(ctx, tank_a, 4, &fill);
                if (r != VG_OK) return r;
                r = vg_fill_convex(ctx, tank_b, 4, &fill);
                if (r != VG_OK) return r;
                r = vg_draw_polyline(ctx, tank_a, 4, &sh, 1);
                if (r != VG_OK) return r;
                r = vg_draw_polyline(ctx, tank_a, 4, &sm, 1);
                if (r != VG_OK) return r;
                r = vg_draw_polyline(ctx, tank_b, 4, &sh, 1);
                if (r != VG_OK) return r;
                r = vg_draw_polyline(ctx, tank_b, 4, &sm, 1);
                if (r != VG_OK) return r;
            }

            if (((ix + li) & 1) == 0 && rn2 > 0.30f) {
                const float p_h = bh * (0.45f + 0.28f * rn1);
                const float p_y0 = ground_y;
                const float p_y1 = fminf(ground_y + p_h, skyline_top_cap);
                const float p_w = bw * (0.10f + 0.06f * rn0);
                const float px = cx + bw * (rn0 < 0.5f ? -0.34f : 0.34f);
                const vg_vec2 frame[5] = {
                    {px - p_w * 0.5f, p_y0},
                    {px + p_w * 0.5f, p_y0},
                    {px + p_w * 0.5f, p_y1},
                    {px - p_w * 0.5f, p_y1},
                    {px - p_w * 0.5f, p_y0}
                };
                const vg_vec2 x0seg[2] = {{px - p_w * 0.5f, p_y0}, {px + p_w * 0.5f, p_y1}};
                const vg_vec2 x1seg[2] = {{px + p_w * 0.5f, p_y0}, {px - p_w * 0.5f, p_y1}};
                r = vg_draw_polyline(ctx, frame, 5, &sh, 0);
                if (r != VG_OK) return r;
                r = vg_draw_polyline(ctx, frame, 5, &sm, 0);
                if (r != VG_OK) return r;
                r = vg_draw_polyline(ctx, x0seg, 2, &sh, 0);
                if (r != VG_OK) return r;
                r = vg_draw_polyline(ctx, x0seg, 2, &sm, 0);
                if (r != VG_OK) return r;
                r = vg_draw_polyline(ctx, x1seg, 2, &sh, 0);
                if (r != VG_OK) return r;
                r = vg_draw_polyline(ctx, x1seg, 2, &sm, 0);
                if (r != VG_OK) return r;
            }

            if (((ix + li) & 2) == 0) {
                const float nx = ((float)(ix + 1) + 0.5f) * module_w - cam_x * parallax + w * 0.5f;
                const float py = ground_y + bh * (0.44f + 0.34f * rn0);
                const float arc = module_w * (0.18f + rn2 * 0.10f);
                const vg_vec2 pipe[4] = {
                    {cx + bw * 0.5f, py},
                    {cx + bw * 0.5f + arc, py},
                    {nx - arc, py},
                    {nx, py}
                };
                const vg_vec2 pipe_u[4] = {
                    {cx + bw * 0.28f, py + bh * 0.10f},
                    {cx + bw * 0.28f, py + bh * 0.26f},
                    {nx - bw * 0.22f, py + bh * 0.26f},
                    {nx - bw * 0.22f, py + bh * 0.08f}
                };
                vg_stroke_style p_h = sh;
                vg_stroke_style p_m = sm;
                p_h.width_px *= 1.20f;
                p_m.width_px *= 1.12f;
                p_h.intensity *= 0.92f;
                p_m.intensity *= 1.00f;
                r = vg_draw_polyline(ctx, pipe, 4, &p_h, 0);
                if (r != VG_OK) {
                    return r;
                }
                r = vg_draw_polyline(ctx, pipe, 4, &p_m, 0);
                if (r != VG_OK) {
                    return r;
                }
                r = vg_draw_polyline(ctx, pipe_u, 4, &p_h, 0);
                if (r != VG_OK) {
                    return r;
                }
                r = vg_draw_polyline(ctx, pipe_u, 4, &p_m, 0);
                if (r != VG_OK) {
                    return r;
                }
            }
        }
    }
    return VG_OK;
}

static vg_result draw_fog_of_war_nebula(
    vg_context* ctx,
    const game_state* g,
    const palette_theme* pal,
    float intensity_scale
) {
    (void)ctx;
    (void)g;
    (void)pal;
    (void)intensity_scale;
    /* Fog-of-war rendering now uses the dedicated GPU shader path in main.c.
       Keep this stub to avoid accidental image-API fallback. */
    return VG_OK;
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
    const int enable_horizon_cull = (g->render_style == LEVEL_RENDER_DRIFTER_SHADED);
    const float center_x = w * 0.50f;
    const float col_spacing = w * 0.050f;
    const float col_span = col_spacing * (float)(COLS - 1);
    const int x0 = (int)floorf((cam - col_span * 0.5f) / col_spacing) - 2;

    for (int r = 0; r < ROWS; ++r) {
        const float z = (float)r / (float)(ROWS - 1);
        const float p = powf(z, 0.82f);
        const float zw = lerpf(360.0f, 4200.0f, p);
        row_depth[r] = z;
        const float y_base = lerpf(y_near, y_far, p);
        const float row_scale = lerpf(1.04f, 0.23f, p);
        const float amp = lerpf(h * 0.21f, h * 0.08f, p);
        for (int c = 0; c < COLS; ++c) {
            const float world_x = (float)(x0 + c) * col_spacing;
            const float dx = world_x - cam;
            const float x = center_x + dx * row_scale;
            const float n = high_plains_looped_noise(world_x * 0.72f, zw * 0.0021f) * 1.95f;
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

    /* Non-culled drifter path: batch rows/columns into long polylines to cut draw calls. */
    for (int r = 0; r < ROWS; ++r) {
        const float z = row_depth[r];
        const float fade = 0.10f + (1.0f - z) * (1.0f - z) * 0.90f;
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
        vg_result vr = vg_draw_polyline(ctx, pts[r], COLS, &glow, 0);
        if (vr != VG_OK) {
            return vr;
        }
        vr = vg_draw_polyline(ctx, pts[r], COLS, &sh, 0);
        if (vr != VG_OK) {
            return vr;
        }
        vr = vg_draw_polyline(ctx, pts[r], COLS, &sm, 0);
        if (vr != VG_OK) {
            return vr;
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
        const int ring_start = (level_style == LEVEL_STYLE_ENEMY_RADAR || level_style == LEVEL_STYLE_REVOLVER) ? 2 : 1;
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
                    const float legacy_brightness = 0.78f;
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
		                vh.color = (vg_color){halo->color.r, halo->color.g, halo->color.b, 0.20f * fade * legacy_brightness};
		                vm.color = (vg_color){main->color.r, main->color.g, main->color.b, 0.58f * fade * legacy_brightness};
		                vh.intensity *= (0.42f + fade * 0.48f) * legacy_brightness;
		                vm.intensity *= (0.48f + fade * 0.56f) * legacy_brightness;
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
		                rh.color = (vg_color){halo->color.r, halo->color.g, halo->color.b, 0.20f * fade * legacy_brightness};
		                rm.color = (vg_color){main->color.r, main->color.g, main->color.b, 0.58f * fade * legacy_brightness};
		                rh.width_px *= 1.55f;
		                rm.width_px *= 1.35f;
		                rh.intensity *= (0.42f + fade * 0.48f) * legacy_brightness;
		                rm.intensity *= (0.48f + fade * 0.56f) * legacy_brightness;
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
		                vh.color = (vg_color){halo->color.r, halo->color.g, halo->color.b, 0.20f * fade * modern_brightness};
		                vm.color = (vg_color){main->color.r, main->color.g, main->color.b, 0.58f * fade * modern_brightness};
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
		                rh.color = (vg_color){halo->color.r, halo->color.g, halo->color.b, 0.20f * fade * modern_brightness};
		                rm.color = (vg_color){main->color.r, main->color.g, main->color.b, 0.58f * fade * modern_brightness};
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

static vg_result draw_player_shield(
    vg_context* ctx,
    float x,
    float y,
    float base_radius,
    float t_s,
    const vg_fill_style* glow_fill,
    const vg_stroke_style* ring_style
) {
    const float pulse = 0.5f + 0.5f * sinf(t_s * 8.6f);
    const float rr = base_radius * (0.97f + pulse * 0.08f);
    vg_fill_style f0 = *glow_fill;
    vg_fill_style f1 = *glow_fill;
    vg_stroke_style rs = *ring_style;

    f0.intensity *= 0.22f + pulse * 0.12f;
    f1.intensity *= 0.30f + pulse * 0.12f;
    rs.intensity *= 0.82f + pulse * 0.36f;

    vg_result r = vg_fill_circle(ctx, (vg_vec2){x, y}, rr * 1.20f, &f0, 36);
    if (r != VG_OK) {
        return r;
    }
    r = vg_fill_circle(ctx, (vg_vec2){x, y}, rr * 0.96f, &f1, 30);
    if (r != VG_OK) {
        return r;
    }
    return vg_draw_polyline(
        ctx,
        (vg_vec2[]){
            {x + rr, y},
            {x + rr * 0.707f, y + rr * 0.707f},
            {x, y + rr},
            {x - rr * 0.707f, y + rr * 0.707f},
            {x - rr, y},
            {x - rr * 0.707f, y - rr * 0.707f},
            {x, y - rr},
            {x + rr * 0.707f, y - rr * 0.707f}
        },
        8,
        &rs,
        1
    );
}

static vg_color powerup_symbol_color(int type) {
    switch (clampi(type, 0, POWERUP_COUNT - 1)) {
        case POWERUP_DOUBLE_SHOT: return (vg_color){0.90f, 0.95f, 1.00f, 1.0f};
        case POWERUP_TRIPLE_SHOT: return (vg_color){1.00f, 0.86f, 0.54f, 1.0f};
        case POWERUP_VITALITY: return (vg_color){0.64f, 1.00f, 0.72f, 1.0f};
        case POWERUP_ORBITAL_BOOST: return (vg_color){0.70f, 0.86f, 1.00f, 1.0f};
        default: return (vg_color){0.92f, 0.92f, 0.92f, 1.0f};
    }
}

static vg_vec2 powerup_project_local(float cx, float cy, float x_scale, float lx, float ly) {
    return (vg_vec2){cx + lx * x_scale, cy + ly};
}

static vg_result draw_powerup_symbol(
    vg_context* ctx,
    float cx,
    float cy,
    float rr,
    float x_scale,
    float spin,
    int type,
    float intensity_scale,
    vg_color c
) {
    const float x_scale_abs = fabsf(x_scale);
    vg_stroke_style sym = make_stroke(
        fmaxf(0.8f, rr * 0.10f),
        0.98f * intensity_scale,
        (vg_color){c.r, c.g, c.b, 0.97f},
        VG_BLEND_ALPHA
    );
    vg_result r = VG_OK;
    if (x_scale_abs < 0.20f) {
        return VG_OK;
    }

    if (type == POWERUP_VITALITY) {
        const vg_vec2 h[2] = {
            powerup_project_local(cx, cy, x_scale, -rr * 0.34f, 0.0f),
            powerup_project_local(cx, cy, x_scale, rr * 0.34f, 0.0f)
        };
        const vg_vec2 v[2] = {
            powerup_project_local(cx, cy, x_scale, 0.0f, -rr * 0.34f),
            powerup_project_local(cx, cy, x_scale, 0.0f, rr * 0.34f)
        };
        r = vg_draw_polyline(ctx, h, 2, &sym, 0);
        if (r != VG_OK) {
            return r;
        }
        return vg_draw_polyline(ctx, v, 2, &sym, 0);
    }

    if (type == POWERUP_ORBITAL_BOOST) {
        vg_vec2 ring[21];
        for (int i = 0; i < 20; ++i) {
            const float a = ((float)i / 20.0f) * 6.2831853f;
            ring[i] = powerup_project_local(cx, cy, x_scale, cosf(a) * rr * 0.33f, sinf(a) * rr * 0.33f);
        }
        ring[20] = ring[0];
        r = vg_draw_polyline(ctx, ring, 21, &sym, 0);
        if (r != VG_OK) {
            return r;
        }
        {
            const float oa = spin * 1.8f + 0.7f;
            const vg_vec2 dot = powerup_project_local(cx, cy, x_scale, cosf(oa) * rr * 0.52f, sinf(oa) * rr * 0.52f);
            vg_fill_style df = make_fill(
                0.92f * intensity_scale,
                (vg_color){c.r, c.g, c.b, 0.95f},
                VG_BLEND_ALPHA
            );
            return vg_fill_circle(ctx, dot, fmaxf(1.5f, rr * 0.09f), &df, 12);
        }
    }

    if (type == POWERUP_TRIPLE_SHOT) {
        const float yv[3] = {-0.23f, 0.0f, 0.23f};
        for (int i = 0; i < 3; ++i) {
            const vg_vec2 line[2] = {
                powerup_project_local(cx, cy, x_scale, -rr * 0.44f, rr * yv[i]),
                powerup_project_local(cx, cy, x_scale, rr * 0.38f, rr * yv[i])
            };
            r = vg_draw_polyline(ctx, line, 2, &sym, 0);
            if (r != VG_OK) {
                return r;
            }
        }
        return VG_OK;
    }

    {
        const float yv[2] = {-0.18f, 0.18f};
        for (int i = 0; i < 2; ++i) {
            const vg_vec2 line[2] = {
                powerup_project_local(cx, cy, x_scale, -rr * 0.44f, rr * yv[i]),
                powerup_project_local(cx, cy, x_scale, rr * 0.38f, rr * yv[i])
            };
            r = vg_draw_polyline(ctx, line, 2, &sym, 0);
            if (r != VG_OK) {
                return r;
            }
        }
        return VG_OK;
    }
}

static vg_result draw_powerup_medallion(
    vg_context* ctx,
    float x,
    float y,
    float radius,
    float spin,
    float bob_phase,
    int type,
    float intensity_scale,
    const vg_stroke_style* txt_halo,
    const vg_stroke_style* txt_main
) {
    const vg_color c = powerup_symbol_color(type);
    const float bob = sinf(bob_phase) * fmaxf(2.6f, radius * 0.20f);
    const float rr = fmaxf(radius * 2.03f, 10.0f);
    const float face = cosf(spin);
    const float face01 = fabsf(face);
    const float x_scale_abs = 0.12f + 0.88f * face01;
    const float x_scale = x_scale_abs * ((face >= 0.0f) ? 1.0f : -1.0f);
    const float cy = y + bob;
    vg_vec2 outer[7];
    vg_vec2 inner[7];
    const float base_a = 0.5235987756f;
    for (int i = 0; i < 6; ++i) {
        const float a = base_a + ((float)i / 6.0f) * 6.2831853f;
        outer[i] = powerup_project_local(x, cy, x_scale, cosf(a) * rr, sinf(a) * rr);
        inner[i] = powerup_project_local(x, cy, x_scale, cosf(a) * rr * 0.58f, sinf(a) * rr * 0.58f);
    }
    outer[6] = outer[0];
    inner[6] = inner[0];

    vg_fill_style fill = make_fill(
        (0.30f + 0.20f * face01) * intensity_scale,
        (vg_color){c.r * 0.76f, c.g * 0.76f, c.b * 0.76f, 0.34f},
        VG_BLEND_ALPHA
    );
    vg_stroke_style edge = make_stroke(
        fmaxf(0.7f, rr * 0.045f),
        (0.66f + 0.18f * face01) * intensity_scale,
        (vg_color){c.r, c.g, c.b, 0.74f},
        VG_BLEND_ALPHA
    );
    vg_stroke_style ring = edge;
    ring.width_px *= 0.78f;
    ring.intensity *= 0.86f;
    ring.color.a *= 0.86f;

    vg_result r = vg_fill_convex(ctx, outer, 6, &fill);
    if (r != VG_OK) {
        return r;
    }
    r = vg_draw_polyline(ctx, outer, 7, &edge, 0);
    if (r != VG_OK) {
        return r;
    }
    r = vg_draw_polyline(ctx, inner, 7, &ring, 0);
    if (r != VG_OK) {
        return r;
    }
    (void)txt_halo;
    (void)txt_main;
    return draw_powerup_symbol(
        ctx,
        x,
        cy,
        rr * 0.92f,
        x_scale,
        spin,
        type,
        intensity_scale * (0.55f + 0.45f * face01),
        c
    );
}

static vg_result draw_emp_blast(
    vg_context* ctx,
    const game_state* g,
    const palette_theme* pal,
    float intensity_scale,
    int uses_cylinder
) {
    if (!g || !g->emp_effect_active || g->emp_effect_duration_s <= 0.0f) {
        return VG_OK;
    }
    const float t01 = clampf(g->emp_effect_t / fmaxf(g->emp_effect_duration_s, 0.001f), 0.0f, 1.0f);
    const float fade = 1.0f - t01;
    const float pulse = 0.82f + 0.18f * sinf((g->emp_effect_t / fmaxf(g->emp_effect_duration_s, 0.001f)) * 18.0f);
    const float r_primary = g->emp_primary_radius * (0.18f + 0.82f * t01);
    const float r_blast = g->emp_blast_radius * (0.12f + 0.88f * t01);
    const vg_color emp_col = (vg_color){pal->primary_dim.r, pal->primary_dim.g, pal->primary_dim.b, 1.0f};
    vg_fill_style flash = make_fill(
        0.36f * fade * intensity_scale,
        (vg_color){pal->secondary.r, pal->secondary.g, pal->secondary.b, 0.55f * fade},
        VG_BLEND_ADDITIVE
    );
    vg_stroke_style ring0 = make_stroke(
        2.4f,
        (1.20f + pulse * 0.34f) * fade * intensity_scale,
        (vg_color){emp_col.r, emp_col.g, emp_col.b, 0.88f},
        VG_BLEND_ADDITIVE
    );
    vg_stroke_style ring1 = make_stroke(
        1.7f,
        (0.92f + pulse * 0.22f) * fade * intensity_scale,
        (vg_color){emp_col.r, emp_col.g, emp_col.b, 0.66f},
        VG_BLEND_ALPHA
    );
    vg_result r = VG_OK;
    const int segs = 56;
    vg_vec2 pts[56];
    vg_vec2 pts2[56];

    if (!uses_cylinder) {
        r = vg_fill_circle(ctx, (vg_vec2){g->emp_effect_x, g->emp_effect_y}, r_primary * (0.12f + 0.25f * fade), &flash, 22);
        if (r != VG_OK) {
            return r;
        }
        for (int i = 0; i < segs; ++i) {
            const float a = ((float)i / (float)segs) * 6.2831853f;
            const float ca = cosf(a);
            const float sa = sinf(a);
            pts[i] = (vg_vec2){g->emp_effect_x + ca * r_primary, g->emp_effect_y + sa * r_primary};
            pts2[i] = (vg_vec2){g->emp_effect_x + ca * r_blast, g->emp_effect_y + sa * r_blast};
        }
    } else {
        float depth = 0.0f;
        const vg_vec2 cp = project_cylinder_point(g, g->emp_effect_x, g->emp_effect_y, &depth);
        r = vg_fill_circle(ctx, cp, r_primary * (0.08f + 0.12f * fade) * (0.35f + depth * 0.85f), &flash, 18);
        if (r != VG_OK) {
            return r;
        }
        for (int i = 0; i < segs; ++i) {
            const float a = ((float)i / (float)segs) * 6.2831853f;
            const float ca = cosf(a);
            const float sa = sinf(a);
            pts[i] = project_cylinder_point(
                g,
                g->emp_effect_x + ca * r_primary,
                g->emp_effect_y + sa * r_primary,
                NULL
            );
            pts2[i] = project_cylinder_point(
                g,
                g->emp_effect_x + ca * r_blast,
                g->emp_effect_y + sa * r_blast,
                NULL
            );
        }
    }

    r = vg_draw_polyline(ctx, pts, segs, &ring0, 1);
    if (r != VG_OK) {
        return r;
    }
    r = vg_draw_polyline(ctx, pts, segs, &ring1, 1);
    if (r != VG_OK) {
        return r;
    }
    ring0.width_px *= 0.92f;
    ring0.intensity *= 0.78f;
    ring1.width_px *= 0.88f;
    ring1.intensity *= 0.72f;
    r = vg_draw_polyline(ctx, pts2, segs, &ring0, 1);
    if (r != VG_OK) {
        return r;
    }
    return vg_draw_polyline(ctx, pts2, segs, &ring1, 1);
}

static void enemy_glyph_basis(const enemy* e, float* out_fx, float* out_fy, float* out_nx, float* out_ny) {
    float fx = e ? e->facing_x : -1.0f;
    float fy = e ? e->facing_y : 0.0f;
    float f_len = sqrtf(fx * fx + fy * fy);
    if (f_len < 1e-5f) {
        fx = -1.0f;
        fy = 0.0f;
        f_len = 1.0f;
    }
    fx /= f_len;
    fy /= f_len;
    if (out_fx) {
        *out_fx = fx;
    }
    if (out_fy) {
        *out_fy = fy;
    }
    if (out_nx) {
        *out_nx = -fy;
    }
    if (out_ny) {
        *out_ny = fx;
    }
}

static vg_result draw_enemy_tail(vg_context* ctx, const enemy* e, float x, float y, float rr, const vg_stroke_style* enemy_style) {
    float fx = 0.0f;
    float fy = 0.0f;
    enemy_glyph_basis(e, &fx, &fy, NULL, NULL);
    {
        const float tx = -fx;
        const float ty = -fy;
        const float nx = -ty;
        const float ny = tx;
        const float tail01 = clampf(e->kamikaze_tail, 0.0f, 1.0f);
        const float pulse01 = clampf(e->kamikaze_thrust, 0.0f, 1.0f);
        const float speed = sqrtf(e->b.vx * e->b.vx + e->b.vy * e->b.vy);
        const float speed01 = clampf(speed / fmaxf(e->max_speed, 1.0f), 0.0f, 1.0f);
        const float speed_brightness = lerpf(0.50f, 1.30f, speed01);
        const float base_back = rr * (0.72f + 0.18f * pulse01);
        const float tail_len = rr * (0.55f + 2.05f * tail01);
        const float spread = rr * (0.10f + 0.42f * tail01);
        const vg_vec2 tail_center = {
            x + tx * (base_back + rr * 0.22f),
            y + ty * (base_back + rr * 0.22f)
        };
        const vg_vec2 tail_tip = {
            tail_center.x + tx * tail_len,
            tail_center.y + ty * tail_len
        };
        const vg_vec2 tail_l[] = {
            {tail_center.x + nx * spread, tail_center.y + ny * spread},
            {tail_tip.x, tail_tip.y}
        };
        const vg_vec2 tail_r[] = {
            {tail_center.x - nx * spread, tail_center.y - ny * spread},
            {tail_tip.x, tail_tip.y}
        };
        const vg_vec2 tail_mid[] = {
            {tail_center.x, tail_center.y},
            {tail_tip.x, tail_tip.y}
        };
        const vg_vec2 left_back[] = {
            tail_l[0],
            {lerpf(tail_l[0].x, tail_l[1].x, 0.52f), lerpf(tail_l[0].y, tail_l[1].y, 0.52f)}
        };
        const vg_vec2 left_front[] = {
            left_back[1],
            tail_l[1]
        };
        const vg_vec2 right_back[] = {
            tail_r[0],
            {lerpf(tail_r[0].x, tail_r[1].x, 0.52f), lerpf(tail_r[0].y, tail_r[1].y, 0.52f)}
        };
        const vg_vec2 right_front[] = {
            right_back[1],
            tail_r[1]
        };
        const vg_vec2 mid_back[] = {
            tail_mid[0],
            {lerpf(tail_mid[0].x, tail_mid[1].x, 0.55f), lerpf(tail_mid[0].y, tail_mid[1].y, 0.55f)}
        };
        const vg_vec2 mid_front[] = {
            mid_back[1],
            tail_mid[1]
        };
        vg_stroke_style shade_back = *enemy_style;
        vg_stroke_style shade_front = *enemy_style;
        vg_stroke_style shade_core = *enemy_style;
        shade_back.intensity *= (0.28f + 0.16f * tail01) * speed_brightness;
        shade_front.intensity *= (0.54f + 0.28f * tail01) * speed_brightness;
        shade_core.intensity *= (0.70f + 0.45f * tail01) * speed_brightness;
        shade_back.color.a *= 0.42f + 0.12f * speed01;
        shade_front.color.a *= 0.58f + 0.20f * speed01;
        shade_core.color.a *= 0.72f + 0.22f * speed01;

        vg_result r = vg_draw_polyline(ctx, left_back, 2, &shade_back, 0);
        if (r != VG_OK) {
            return r;
        }
        r = vg_draw_polyline(ctx, left_front, 2, &shade_front, 0);
        if (r != VG_OK) {
            return r;
        }
        r = vg_draw_polyline(ctx, right_back, 2, &shade_back, 0);
        if (r != VG_OK) {
            return r;
        }
        r = vg_draw_polyline(ctx, right_front, 2, &shade_front, 0);
        if (r != VG_OK) {
            return r;
        }
        r = vg_draw_polyline(ctx, mid_back, 2, &shade_front, 0);
        if (r != VG_OK) {
            return r;
        }
        return vg_draw_polyline(ctx, mid_front, 2, &shade_core, 0);
    }
}

static vg_result draw_enemy_glyph_default(vg_context* ctx, const enemy* e, float x, float y, float rr, const vg_stroke_style* enemy_style) {
    float fx = 0.0f;
    float fy = 0.0f;
    float nx = 0.0f;
    float ny = 0.0f;
    enemy_glyph_basis(e, &fx, &fy, &nx, &ny);
    {
        const vg_vec2 body[] = {
            {x + fx * (-1.0f * rr) + nx * (0.0f * rr), y + fy * (-1.0f * rr) + ny * (0.0f * rr)},
            {x + fx * (-0.2f * rr) + nx * (-0.8f * rr), y + fy * (-0.2f * rr) + ny * (-0.8f * rr)},
            {x + fx * (1.0f * rr) + nx * (0.0f * rr), y + fy * (1.0f * rr) + ny * (0.0f * rr)},
            {x + fx * (-0.2f * rr) + nx * (0.8f * rr), y + fy * (-0.2f * rr) + ny * (0.8f * rr)},
            {x + fx * (-1.0f * rr) + nx * (0.0f * rr), y + fy * (-1.0f * rr) + ny * (0.0f * rr)}
        };
        vg_result r = vg_draw_polyline(ctx, body, sizeof(body) / sizeof(body[0]), enemy_style, 0);
        if (r != VG_OK) {
            return r;
        }
    }
    if (e->kamikaze_tail <= 0.02f) {
        return VG_OK;
    }
    return draw_enemy_tail(ctx, e, x, y, rr, enemy_style);
}

static vg_result draw_enemy_glyph_jelly(vg_context* ctx, const enemy* e, float x, float y, float rr, const vg_stroke_style* enemy_style) {
    float fx = 0.0f;
    float fy = 0.0f;
    float nx = 0.0f;
    float ny = 0.0f;
    enemy_glyph_basis(e, &fx, &fy, &nx, &ny);
    const float pulse_freq = (e->visual_param_a > 0.01f) ? e->visual_param_a : 2.0f;
    const float tentacle_amp = clampf((e->visual_param_b > 0.01f) ? e->visual_param_b : 0.12f, 0.03f, 0.28f);
    const float phase = e->ai_timer_s * pulse_freq + e->visual_phase;
    const float p = sinf(phase);
    const float pulse01 = 0.5f + 0.5f * p;
    const float accel01 = clampf(p, 0.0f, 1.0f);
    const float scale_y = 1.0f + 0.18f * p;
    const float scale_x = 1.0f - 0.10f * p;
    const float skirt_drop = rr * (0.20f + 0.10f * pulse01);
    const float seed_phase = (float)(e->visual_seed & 1023u) * 0.013f;
    const float draw_y = y + rr * 0.20f * sinf(phase * 0.5f + seed_phase);
    vg_stroke_style main = *enemy_style;
    vg_stroke_style inner = *enemy_style;
    vg_stroke_style rim = *enemy_style;
    main.intensity *= lerpf(0.82f, 1.36f, accel01);
    main.color.a *= lerpf(0.80f, 1.00f, accel01);
    inner.width_px *= 0.78f;
    inner.intensity *= lerpf(0.56f, 0.96f, accel01);
    inner.color.a *= lerpf(0.62f, 0.92f, accel01);
    rim.width_px *= 1.30f;
    rim.intensity *= lerpf(0.22f, 0.72f, accel01);
    rim.color.a *= lerpf(0.36f, 0.88f, accel01);
    rim.blend = VG_BLEND_ADDITIVE;

    {
        const float k_bell[][2] = {
            {-1.00f, -0.10f}, {-0.74f, 0.40f}, {-0.38f, 0.78f}, {0.00f, 0.98f},
            {0.38f, 0.78f}, {0.74f, 0.40f}, {1.00f, -0.10f}, {0.48f, -0.34f},
            {0.00f, -0.48f}, {-0.48f, -0.34f}, {-1.00f, -0.10f}
        };
        vg_vec2 bell[11];
        for (int i = 0; i < 11; ++i) {
            const float local_n = k_bell[i][0] * rr * scale_x;
            float local_f = k_bell[i][1] * rr * scale_y;
            if (i >= 7 && i <= 9) {
                local_f -= skirt_drop * (0.85f - 0.25f * fabsf(k_bell[i][0]));
            }
            bell[i].x = x + fx * local_f + nx * local_n;
            bell[i].y = draw_y + fy * local_f + ny * local_n;
        }
        vg_result r = vg_draw_polyline(ctx, bell, 11, &main, 0);
        if (r != VG_OK) {
            return r;
        }
        r = vg_draw_polyline(ctx, bell, 11, &rim, 0);
        if (r != VG_OK) {
            return r;
        }
    }

    for (int arc_i = 0; arc_i < 2; ++arc_i) {
        const float af = (arc_i == 0) ? 0.40f : 0.18f;
        const float aw = (arc_i == 0) ? 0.36f : 0.25f;
        const vg_vec2 arc[5] = {
            {x + fx * (rr * af) + nx * (-rr * aw), draw_y + fy * (rr * af) + ny * (-rr * aw)},
            {x + fx * (rr * (af + 0.05f)) + nx * (-rr * aw * 0.35f), draw_y + fy * (rr * (af + 0.05f)) + ny * (-rr * aw * 0.35f)},
            {x + fx * (rr * (af + 0.06f)), draw_y + fy * (rr * (af + 0.06f))},
            {x + fx * (rr * (af + 0.05f)) + nx * (rr * aw * 0.35f), draw_y + fy * (rr * (af + 0.05f)) + ny * (rr * aw * 0.35f)},
            {x + fx * (rr * af) + nx * (rr * aw), draw_y + fy * (rr * af) + ny * (rr * aw)}
        };
        vg_result r = vg_draw_polyline(ctx, arc, 5, &inner, 0);
        if (r != VG_OK) {
            return r;
        }
    }

    {
        const int seg_n = (rr < 10.0f) ? 5 : 6;
        int tentacle_n = 8 + (int)(e->visual_seed % 5u);
        if (rr < 9.0f) {
            tentacle_n = 5;
        } else if (rr < 14.0f) {
            tentacle_n = 7;
        }
        tentacle_n = clampi(tentacle_n, 4, 12);
        const float wave_freq = 2.2f + 1.2f * hash01_u32(e->visual_seed ^ 0x517u);
        const float wave_phase = 5.0f + 3.0f * hash01_u32(e->visual_seed ^ 0x19fu);
        for (int ti = 0; ti < tentacle_n; ++ti) {
            vg_vec2 line[6];
            const float v = ((float)ti + 0.5f) / (float)tentacle_n;
            const float side = (v * 2.0f - 1.0f) * 0.84f;
            const float tent_phase = 6.2831853f * hash01_u32(e->visual_seed ^ (uint32_t)(ti * 0x9e37u));
            for (int si = 0; si <= seg_n; ++si) {
                const float u = (float)si / (float)seg_n;
                const float amp_u = rr * tentacle_amp * powf(u, 1.25f);
                const float offset = amp_u * sinf(e->ai_timer_s * wave_freq - u * wave_phase + tent_phase);
                const float local_f = rr * (-0.24f - u * (1.70f + 0.20f * pulse01));
                const float local_n = rr * side + offset;
                line[si].x = x + fx * local_f + nx * local_n;
                line[si].y = draw_y + fy * local_f + ny * local_n;
            }
            vg_result r = vg_draw_polyline(ctx, line, seg_n + 1, &inner, 0);
            if (r != VG_OK) {
                return r;
            }
        }
    }

    if (e->kamikaze_tail > 0.02f) {
        return draw_enemy_tail(ctx, e, x, draw_y, rr, &inner);
    }
    return VG_OK;
}

static vg_result draw_enemy_glyph_manta(vg_context* ctx, const enemy* e, float x, float y, float rr, const vg_stroke_style* enemy_style) {
    float fx = 0.0f;
    float fy = 0.0f;
    float nx = 0.0f;
    float ny = 0.0f;
    /* Side-view manta: use a stable left/right basis so wing motion doesn't discontinuously flip
     * when steering/facing changes during on-screen turns. */
    {
        float lane = (e && e->lane_dir != 0.0f) ? e->lane_dir : 0.0f;
        if (lane > -0.01f && lane < 0.01f) {
            lane = (e && e->b.vx != 0.0f) ? e->b.vx : (e ? e->facing_x : -1.0f);
        }
        fx = (lane < 0.0f) ? -1.0f : 1.0f;
        fy = 0.0f;
        nx = 0.0f;
        ny = 1.0f;
    }
    const float flap_speed = (e->visual_param_a > 0.01f) ? e->visual_param_a : 1.5f;
    const float flap_amp = clampf((e->visual_param_b > 0.01f) ? e->visual_param_b : 0.16f, 0.06f, 0.32f);
    const float flap_t = e->ai_timer_s * (2.2f + flap_speed) + e->visual_phase;
    /* Organic-ish wingbeat: fundamental + harmonic, plus a velocity term for mild twist.
     * This keeps the fin readable at mid-stroke (where pure sine would go edge-on). */
    const float flap_s1 = sinf(flap_t);
    const float flap_s2 = sinf(flap_t * 2.0f + 0.65f);
    float flap_pos = flap_s1 + 0.22f * flap_s2;
    flap_pos = clampf(flap_pos, -1.0f, 1.0f);
    float flap_vel = cosf(flap_t) + 0.44f * cosf(flap_t * 2.0f + 0.65f);
    flap_vel = clampf(flap_vel, -1.0f, 1.0f);
    const float charge01 = (e->missile_charge_duration_s > 0.01f)
        ? clampf(e->missile_charge_s / e->missile_charge_duration_s, 0.0f, 1.0f)
        : 0.0f;
    vg_stroke_style main = *enemy_style;
    vg_stroke_style glow = *enemy_style;
    vg_stroke_style detail = *enemy_style;
    main.intensity *= lerpf(0.90f, 1.65f, charge01);
    main.color.a *= lerpf(0.86f, 1.00f, charge01);
    detail.width_px *= 0.78f;
    detail.intensity *= lerpf(0.70f, 1.22f, charge01);
    detail.color.a *= lerpf(0.66f, 0.98f, charge01);
    glow.width_px *= 1.40f;
    glow.blend = VG_BLEND_ADDITIVE;
    glow.intensity *= lerpf(0.35f, 1.25f, charge01);
    glow.color.a *= lerpf(0.45f, 0.95f, charge01);
    vg_stroke_style wing_side = main;
    wing_side.width_px *= 0.92f;
    /* Continuous wing shading by flap phase: brighter on underside (up), dimmer on topside (down). */
    wing_side.intensity *= lerpf(0.80f, 1.20f, 0.5f + 0.5f * flap_pos);
    wing_side.color.a = 1.0f;
    vg_stroke_style wing_shade = wing_side;
    wing_shade.intensity *= 0.86f;
    wing_shade.color.a = 1.0f;
    wing_shade.blend = VG_BLEND_ALPHA;
    const float face01 = clampf(0.5f + 0.5f * flap_pos, 0.0f, 1.0f);
    const int underside_face = (flap_pos >= 0.0f) ? 1 : 0;
    vg_fill_style wing_fill = {
        /* Needs to be fairly strong; otherwise it reads as "barely tinted" under CRT bloom. */
        .intensity = wing_side.intensity * 0.92f,
        .color = (vg_color){wing_side.color.r, wing_side.color.g, wing_side.color.b, 0.82f + 0.10f * charge01},
        .blend = VG_BLEND_ALPHA
    };
    vg_fill_style wing_fill_glow = wing_fill;
    wing_fill_glow.blend = VG_BLEND_ADDITIVE;
    wing_fill_glow.intensity *= 0.28f;
    wing_fill_glow.color.a *= 0.30f;
    vg_fill_style wing_edge_band = wing_fill_glow;
    wing_edge_band.intensity *= 1.10f;
    wing_edge_band.color.a *= 1.20f;

    /* Slightly manta-ish: flatter belly, arched back, pointier nose. */
    const vg_vec2 body[] = {
        {x + fx * (1.40f * rr) + nx * (0.00f * rr), y + fy * (1.40f * rr) + ny * (0.00f * rr)},
        {x + fx * (1.08f * rr) + nx * (0.22f * rr), y + fy * (1.08f * rr) + ny * (0.22f * rr)},
        {x + fx * (0.40f * rr) + nx * (0.33f * rr), y + fy * (0.40f * rr) + ny * (0.33f * rr)},
        {x + fx * (-0.50f * rr) + nx * (0.22f * rr), y + fy * (-0.50f * rr) + ny * (0.22f * rr)},
        {x + fx * (-1.30f * rr) + nx * (0.06f * rr), y + fy * (-1.30f * rr) + ny * (0.06f * rr)},
        {x + fx * (-1.84f * rr) + nx * (0.00f * rr), y + fy * (-1.84f * rr) + ny * (0.00f * rr)},
        {x + fx * (-1.30f * rr) + nx * (-0.10f * rr), y + fy * (-1.30f * rr) + ny * (-0.10f * rr)},
        {x + fx * (-0.50f * rr) + nx * (-0.20f * rr), y + fy * (-0.50f * rr) + ny * (-0.20f * rr)},
        {x + fx * (0.40f * rr) + nx * (-0.26f * rr), y + fy * (0.40f * rr) + ny * (-0.26f * rr)},
        {x + fx * (1.08f * rr) + nx * (-0.16f * rr), y + fy * (1.08f * rr) + ny * (-0.16f * rr)},
        {x + fx * (1.40f * rr) + nx * (0.00f * rr), y + fy * (1.40f * rr) + ny * (0.00f * rr)}
    };

    /* Wing membrane: deforming outer edge + root attachment line.
     * Fill is a triangle mesh between root and edge (2 triangles/segment). This reads as a flat fin,
     * and lets us modulate brightness per-segment (approx "per-vertex") without relying on a single fan center. */
    const float wing_base_front_f = 1.06f * rr;
    /* Match the tail attachment so the membrane reaches the body all the way to the tail join. */
    const float wing_base_back_f = -1.76f * rr;
    const float wing_beat_amp = rr * (0.54f + 2.05f * flap_amp);
    const float wing_twist_amp = rr * (0.10f + 0.24f * flap_amp);
    const float wing_rest_amp = rr * (0.18f + 0.12f * flap_amp);
    const float wing_ripple_amp = rr * (0.06f + 0.20f * flap_amp);
    enum { WING_SEG_N = 32 };
    vg_vec2 wing_edge[WING_SEG_N + 1];
    float wing_edge_wave[WING_SEG_N + 1];
    const float root_front_f = 0.72f * rr;
    /* Extend the root attachment line to the same tail join point as the wing edge. */
    const float root_back_f = -1.76f * rr;
    vg_vec2 wing_root[WING_SEG_N + 1];

    /* Sample the body top edge in local (f,n) coords so the wing attachment follows the body silhouette. */
    const float body_top_f_unit[] = {1.40f, 1.08f, 0.40f, -0.50f, -1.30f, -1.84f};
    const float body_top_n_unit[] = {0.00f, 0.22f, 0.33f, 0.22f, 0.06f, 0.00f};
    const int body_top_count = (int)(sizeof(body_top_f_unit) / sizeof(body_top_f_unit[0]));
    for (int i = 0; i <= WING_SEG_N; ++i) {
        const float u = (float)i / (float)WING_SEG_N;
        /* Root attachment curve: follow the top edge of the manta body, inset slightly. */
        const float root_f = lerpf(root_front_f, root_back_f, u);
        const float f_unit = root_f / fmaxf(rr, 1.0e-4f);
        float n_unit = body_top_n_unit[body_top_count - 1];
        if (f_unit >= body_top_f_unit[0]) {
            n_unit = body_top_n_unit[0];
        } else if (f_unit <= body_top_f_unit[body_top_count - 1]) {
            n_unit = body_top_n_unit[body_top_count - 1];
        } else {
            for (int j = 0; j < body_top_count - 1; ++j) {
                const float f0 = body_top_f_unit[j];
                const float f1 = body_top_f_unit[j + 1];
                if (f_unit <= f0 && f_unit >= f1) {
                    const float t = (f0 - f_unit) / fmaxf(f0 - f1, 1.0e-6f);
                    n_unit = lerpf(body_top_n_unit[j], body_top_n_unit[j + 1], clampf(t, 0.0f, 1.0f));
                    break;
                }
            }
        }
        float root_n = (n_unit - 0.035f) * rr;
        root_n = fmaxf(root_n, -0.02f * rr);
        wing_root[i].x = x + fx * root_f + nx * root_n;
        wing_root[i].y = y + fy * root_f + ny * root_n;
        /* Profile: manta wings are swept and forward-weighted, not semicircular. */
        const float prof0 = sinf(u * 3.14159265f); /* 0..1..0 */
        float prof = prof0 * (1.10f - 0.55f * u); /* bias more area toward the front */
        if (prof < 0.0f) {
            prof = 0.0f;
        }
        /* Slight sweep/curve in planform so the trailing edge isn't a perfect arc. */
        const float sweep_u = clampf(u + 0.10f * sinf(u * 3.14159265f), 0.0f, 1.0f);
        const float f = lerpf(wing_base_front_f, wing_base_back_f, sweep_u);

        const float base_n = 0.0f;
        const float rest_n = wing_rest_amp * (0.35f + 0.65f * (1.0f - u)) * prof;
        const float beat_n = wing_beat_amp * flap_pos * prof;
        const float twist_n = wing_twist_amp * flap_vel * prof * (0.85f - 0.65f * u);

        /* Traveling wave that moves from front to back; emphasize on the outer edge. */
        const float trailing_w = u * (0.35f + 0.65f * u);
        const float ripple_phase = flap_t * 2.25f - u * 14.5f + e->visual_phase * 0.7f;
        const float wave = sinf(ripple_phase);
        const float wave_env = (0.10f + 0.90f * prof0) * trailing_w;
        const float wave_n = wave * wave_env;
        wing_edge_wave[i] = wave_n;
        const float ripple = wing_ripple_amp * wave_n;

        float outer_n = base_n + rest_n + beat_n + twist_n + ripple;
        /* Avoid degenerate triangles at mid-stroke without destroying the sign (needed for up/down beats). */
        if (fabsf(outer_n) < rr * 0.02f) {
            outer_n = (outer_n < 0.0f) ? (-rr * 0.02f) : (rr * 0.02f);
        }
        wing_edge[i].x = x + fx * f + nx * outer_n;
        wing_edge[i].y = y + fy * f + ny * outer_n;
    }
    const vg_vec2 wing_root_front = wing_root[0];
    const vg_vec2 wing_root_back = wing_root[WING_SEG_N];
    /* Ensure the outer edge meets the attachment curve at both ends. */
    wing_edge[0] = wing_root_front;
    wing_edge[WING_SEG_N] = wing_root_back;

    vg_result r = VG_OK;
    {
        const float root_face_boost = underside_face ? 1.35f : 0.78f;
        for (int i = 0; i < WING_SEG_N; ++i) {
            /* Interior fill stays steady (no wave modulation): modulation should only read on the edge. */
            const vg_vec2 inner0 = (vg_vec2){
                lerpf(wing_root[i].x, wing_edge[i].x, 0.74f),
                lerpf(wing_root[i].y, wing_edge[i].y, 0.74f)
            };
            const vg_vec2 inner1 = (vg_vec2){
                lerpf(wing_root[i + 1].x, wing_edge[i + 1].x, 0.74f),
                lerpf(wing_root[i + 1].y, wing_edge[i + 1].y, 0.74f)
            };

            vg_fill_style base_fill = wing_fill;
            base_fill.intensity *= lerpf(0.90f, 1.15f, face01);
            const vg_vec2 tri0[] = {wing_root[i], inner0, inner1};
            r = vg_fill_convex(ctx, tri0, 3, &base_fill);
            if (r != VG_OK) {
                return r;
            }
            const vg_vec2 tri1[] = {wing_root[i], inner1, wing_root[i + 1]};
            r = vg_fill_convex(ctx, tri1, 3, &base_fill);
            if (r != VG_OK) {
                return r;
            }

            /* Edge band: additive, modulated by the traveling wave, localized to the outer span. */
            const float wave01 = 0.5f + 0.5f * clampf(0.5f * (wing_edge_wave[i] + wing_edge_wave[i + 1]) * 1.15f, -1.0f, 1.0f);
            vg_fill_style band = wing_edge_band;
            band.intensity *= (0.25f + 1.05f * wave01) * lerpf(0.88f, 1.22f, face01);
            band.color.a *= (0.22f + 0.78f * wave01);
            const vg_vec2 band0[] = {inner0, wing_edge[i], wing_edge[i + 1]};
            r = vg_fill_convex(ctx, band0, 3, &band);
            if (r != VG_OK) {
                return r;
            }
            const vg_vec2 band1[] = {inner0, wing_edge[i + 1], inner1};
            r = vg_fill_convex(ctx, band1, 3, &band);
            if (r != VG_OK) {
                return r;
            }
        }
    }
	    /* Draw body on top to hide the wing's attachment seam. */
	    {
	        /* Keep body fill very subtle and mostly neutral so it doesn't read as a solid colored blob. */
	        vg_fill_style body_fill = wing_fill;
	        body_fill.blend = VG_BLEND_ALPHA;
	        body_fill.intensity = 1.0f;
	        body_fill.color = (vg_color){0.0f, 0.0f, 0.0f, (0.018f + 0.010f * charge01) * lerpf(0.85f, 1.10f, face01)};
	        const int body_fill_n = (int)(sizeof(body) / sizeof(body[0])) - 1;
	        r = vg_fill_convex(ctx, body, (size_t)body_fill_n, &body_fill);
	        if (r != VG_OK) {
	            return r;
	        }
	    }
    r = vg_draw_polyline(ctx, body, sizeof(body) / sizeof(body[0]), &main, 0);
    if (r != VG_OK) {
        return r;
    }
    r = vg_draw_polyline(ctx, body, sizeof(body) / sizeof(body[0]), &glow, 0);
    if (r != VG_OK) {
        return r;
    }

    /* Stroke passes after body so the edge highlight stays readable. */
    {
        /* Edge segments with per-segment brightness driven by the traveling wave. */
        const float edge_face_boost = lerpf(0.80f, 1.22f, face01);
        for (int i = 0; i < WING_SEG_N; ++i) {
            const float wave01 = 0.5f + 0.5f * (0.5f * (wing_edge_wave[i] + wing_edge_wave[i + 1]));
            vg_stroke_style seg = wing_side;
            seg.intensity *= edge_face_boost * (0.70f + 0.60f * wave01);
            seg.color.a *= (0.80f + 0.35f * wave01);
            const vg_vec2 seg_pts[] = {wing_edge[i], wing_edge[i + 1]};
            r = vg_draw_polyline(ctx, seg_pts, 2, &seg, 0);
            if (r != VG_OK) {
                return r;
            }
        }

        /* Root/attachment highlight flips with face visibility. */
        vg_stroke_style root = detail;
        root.width_px *= 1.10f;
        root.intensity *= underside_face ? 1.25f : 0.78f;
        root.color.a *= underside_face ? 1.00f : 0.75f;
        /* Draw the join as a curve (matching the body top edge) to avoid a straight stray chord. */
        r = vg_draw_polyline(ctx, wing_root, WING_SEG_N + 1, &root, 0);
        if (r != VG_OK) {
            return r;
        }

        /* Interior shade and a few ribs. */
        vg_vec2 shade[WING_SEG_N + 1];
        for (int i = 0; i <= WING_SEG_N; ++i) {
            /* Start slightly off the body so lines don't read inside the body fill. */
            const vg_vec2 root_out = (vg_vec2){
                lerpf(wing_root[i].x, wing_edge[i].x, 0.10f),
                lerpf(wing_root[i].y, wing_edge[i].y, 0.10f)
            };
            shade[i].x = lerpf(root_out.x, wing_edge[i].x, 0.62f);
            shade[i].y = lerpf(root_out.y, wing_edge[i].y, 0.62f);
        }
        r = vg_draw_polyline(ctx, shade, WING_SEG_N + 1, &wing_shade, 0);
        if (r != VG_OK) {
            return r;
        }
        const int rib_idx[] = {6, 14, 22, 30};
        for (int i = 0; i < (int)(sizeof(rib_idx) / sizeof(rib_idx[0])); ++i) {
            const int idx = clampi(rib_idx[i], 0, WING_SEG_N);
            const vg_vec2 root_out = (vg_vec2){
                lerpf(wing_root[idx].x, wing_edge[idx].x, 0.10f),
                lerpf(wing_root[idx].y, wing_edge[idx].y, 0.10f)
            };
            const vg_vec2 rib[] = {root_out, wing_edge[idx]};
            r = vg_draw_polyline(ctx, rib, 2, &detail, 0);
            if (r != VG_OK) {
                return r;
            }
        }
    }

    const float tail_wave = rr * 0.13f;
    const float tail_t = flap_t * 0.5f;
    const vg_vec2 tail[] = {
        {x + fx * (-1.76f * rr), y + fy * (-1.76f * rr)},
        {x + fx * (-2.10f * rr) + nx * (tail_wave * sinf(tail_t + 0.8f)), y + fy * (-2.10f * rr) + ny * (tail_wave * sinf(tail_t + 0.8f))},
        {x + fx * (-2.45f * rr) + nx * (tail_wave * 1.25f * sinf(tail_t + 1.8f)), y + fy * (-2.45f * rr) + ny * (tail_wave * 1.25f * sinf(tail_t + 1.8f))},
        {x + fx * (-2.82f * rr) + nx * (tail_wave * 1.55f * sinf(tail_t + 2.6f)), y + fy * (-2.82f * rr) + ny * (tail_wave * 1.55f * sinf(tail_t + 2.6f))}
    };

    r = vg_draw_polyline(ctx, tail, 4, &main, 0);
    if (r != VG_OK) {
        return r;
    }
    r = vg_draw_polyline(ctx, tail, 4, &glow, 0);
    if (r != VG_OK) {
        return r;
    }
    return VG_OK;
}

static vg_result draw_enemy_glyph(vg_context* ctx, const enemy* e, float x, float y, float rr, const vg_stroke_style* enemy_style) {
    switch (e->visual_kind) {
        case ENEMY_VISUAL_MANTA:
            return draw_enemy_glyph_manta(ctx, e, x, y, rr, enemy_style);
        case ENEMY_VISUAL_JELLY:
            return draw_enemy_glyph_jelly(ctx, e, x, y, rr, enemy_style);
        case ENEMY_VISUAL_DEFAULT:
        default:
            return draw_enemy_glyph_default(ctx, e, x, y, rr, enemy_style);
    }
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
    /* Stabilize persistence clear against frame-time jitter (major on line-dense scenes). */
    static float persistence_dt_s = 1.0f / 60.0f;
    const float dt_clamped = clampf(metrics->dt, 1.0f / 120.0f, 1.0f / 45.0f);
    persistence_dt_s += (dt_clamped - persistence_dt_s) * 0.08f;
    float frame_decay = powf(persistence, persistence_dt_s * 95.0f);
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
    const vg_stroke_style bullet_style = make_stroke(0.95f, 0.94f * intensity_scale, (vg_color){1.0f, 0.9f, 0.55f, 1.0f}, VG_BLEND_ALPHA);
    const vg_stroke_style bullet_halo_style = make_stroke(
        4.8f,
        0.24f * intensity_scale,
        (vg_color){1.0f, 0.92f, 0.62f, 0.30f},
        VG_BLEND_ADDITIVE
    );
    const vg_stroke_style enemy_style = make_stroke(2.5f, 1.0f * intensity_scale, (vg_color){1.0f, 0.3f, 0.3f, 1.0f}, VG_BLEND_ALPHA);
    const vg_stroke_style enemy_bullet_style = make_stroke(0.82f, 0.88f * intensity_scale, (vg_color){1.0f, 0.36f, 0.36f, 1.0f}, VG_BLEND_ALPHA);
    const vg_stroke_style enemy_bullet_halo_style = make_stroke(
        4.0f,
        0.21f * intensity_scale,
        (vg_color){1.0f, 0.38f, 0.38f, 0.28f},
        VG_BLEND_ADDITIVE
    );
    const vg_fill_style thruster_fill = make_fill(1.0f * intensity_scale, pal.thruster, VG_BLEND_ADDITIVE);
    const vg_fill_style shield_glow = make_fill(
        0.50f * intensity_scale,
        (vg_color){pal.secondary.r, pal.secondary.g, pal.secondary.b, 0.15f},
        VG_BLEND_ADDITIVE
    );
    const vg_stroke_style shield_ring = make_stroke(
        1.8f,
        1.0f * intensity_scale,
        (vg_color){pal.secondary.r, pal.secondary.g, pal.secondary.b, 0.88f},
        VG_BLEND_ALPHA
    );

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

    if (metrics->menu_screen == APP_SCREEN_SHIPYARD) {
        const vg_fill_style bg_shipyard = make_fill(1.0f, (vg_color){0.0f, 0.0f, 0.0f, 1.0f}, VG_BLEND_ALPHA);
        vg_result r = vg_fill_rect(ctx, (vg_rect){0.0f, 0.0f, g->world_w, g->world_h}, &bg_shipyard);
        if (r != VG_OK) {
            return r;
        }
        r = draw_shipyard_menu(ctx, g->world_w, g->world_h, metrics);
        if (r != VG_OK) {
            return r;
        }
        return draw_mouse_pointer(ctx, g->world_w, g->world_h, metrics, &txt_main);
    }
    if (metrics->menu_screen == APP_SCREEN_OPENING) {
        const vg_fill_style bg_opening = make_fill(1.0f, (vg_color){0.0f, 0.0f, 0.0f, 1.0f}, VG_BLEND_ALPHA);
        vg_result r = vg_fill_rect(ctx, (vg_rect){0.0f, 0.0f, g->world_w, g->world_h}, &bg_opening);
        if (r != VG_OK) {
            return r;
        }
        r = draw_opening_menu(ctx, g->world_w, g->world_h, metrics);
        if (r != VG_OK) {
            return r;
        }
        return draw_mouse_pointer(ctx, g->world_w, g->world_h, metrics, &txt_main);
    }
    if (metrics->menu_screen == APP_SCREEN_ACOUSTICS) {
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
    if (metrics->menu_screen == APP_SCREEN_VIDEO) {
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
    if (metrics->menu_screen == APP_SCREEN_PLANETARIUM) {
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
    if (metrics->menu_screen == APP_SCREEN_LEVEL_EDITOR) {
        vg_result r = vg_fill_rect(ctx, (vg_rect){0.0f, 0.0f, g->world_w, g->world_h}, &bg);
        if (r != VG_OK) {
            return r;
        }
        r = draw_level_editor_ui(ctx, g->world_w, g->world_h, metrics, metrics->ui_time_s);
        if (r != VG_OK) {
            return r;
        }
        return draw_mouse_pointer(ctx, g->world_w, g->world_h, metrics, &txt_main);
    }
    if (metrics->menu_screen == APP_SCREEN_CONTROLS) {
        vg_result r = vg_fill_rect(ctx, (vg_rect){0.0f, 0.0f, g->world_w, g->world_h}, &bg);
        if (r != VG_OK) {
            return r;
        }
        r = draw_controls_menu(ctx, g->world_w, g->world_h, metrics);
        if (r != VG_OK) {
            return r;
        }
        return draw_mouse_pointer(ctx, g->world_w, g->world_h, metrics, &txt_main);
    }

    const float jx = sinf(g->t * 17.0f + 0.2f) * crt.jitter_amount * 0.75f;
    const float jy = cosf(g->t * 21.0f) * crt.jitter_amount * 0.75f;
    const int draw_star_background = level_draws_star_background(g);
    const int background_only = (metrics->scene_phase == 1);
    const int foreground_only = (metrics->scene_phase == 2);
    const int overlay_no_clear = (metrics->scene_phase == 3);

    vg_transform_reset(ctx);
    vg_transform_translate(ctx, jx, jy);

    vg_result r = VG_OK;
    if (!foreground_only && !overlay_no_clear) {
        r = vg_fill_rect(ctx, (vg_rect){0.0f, 0.0f, g->world_w, g->world_h}, &bg);
        if (r != VG_OK) {
            return r;
        }
    }

    if (level_uses_cylinder_render(g)) {
        const float period = cylinder_period(g);
        if (!foreground_only) {
            vg_stroke_style cyl_halo = land_halo;
            vg_stroke_style cyl_main = land_main;
            cyl_halo.intensity *= 1.15f;
            cyl_main.intensity *= 1.20f;
            if (!((metrics->use_gpu_wormhole && g->level_style == LEVEL_STYLE_EVENT_HORIZON) ||
                  (metrics->use_gpu_radar && g->level_style == LEVEL_STYLE_ENEMY_RADAR))) {
                r = draw_cylinder_wire(ctx, g, &cyl_halo, &cyl_main, g->level_style);
                if (r != VG_OK) {
                    return r;
                }
            }

            if (draw_star_background) {
                for (size_t i = 0; i < MAX_STARS; ++i) {
                    const float su = repeatf(g->stars[i].x - g->camera_x * 0.22f, g->world_w) / fmaxf(g->world_w, 1.0f);
                    const float sx_world = g->camera_x + (su - 0.5f) * period;
                    float depth = 0.0f;
                    const vg_vec2 sp = project_cylinder_point(g, sx_world, g->stars[i].y, &depth);
                    if (!star_visible_with_mask(g, sp.x, sp.y)) {
                        continue;
                    }
                    vg_fill_style sf = star_fill;
                    sf.intensity *= 0.45f + depth * 0.9f;
                    r = vg_fill_circle(ctx, sp, g->stars[i].size * (0.5f + depth), &sf, 8);
                    if (r != VG_OK) {
                        return r;
                    }
                }
            }
            r = draw_background_window_mask_overlays(ctx, g, &land_halo, &land_main);
            if (r != VG_OK) {
                return r;
            }
        }

        if (background_only) {
            return VG_OK;
        }

        if (!metrics->use_gpu_particles) {
            for (size_t i = 0; i < MAX_PARTICLES; ++i) {
                /* Particle LOD: keep frame time stable under heavy explosion loads. */
                const int active_particles = g->active_particles;
                int stride = 1;
                if (active_particles > 360) {
                    stride = 2;
                }
                if (active_particles > 620) {
                    stride = 3;
                }
                if (active_particles > 900) {
                    stride = 4;
                }
                if ((int)(i % (size_t)stride) != 0) {
                    continue;
                }
                const particle* p = &g->particles[i];
                if (!p->active) {
                    continue;
                }
                if (p->a <= 0.02f || p->size <= 0.15f) {
                    continue;
                }
                float depth = 0.0f;
                const vg_vec2 pp = project_cylinder_point(g, p->b.x, p->b.y, &depth);
                const float pr = p->size * (0.35f + 0.9f * depth);
                if (pp.x < -24.0f || pp.x > g->world_w + 24.0f || pp.y < -24.0f || pp.y > g->world_h + 24.0f) {
                    continue;
                }
                if (pr <= 0.10f) {
                    continue;
                }
                vg_fill_style pf = make_fill(1.0f, (vg_color){p->r, p->g, p->bcol, p->a}, VG_BLEND_ADDITIVE);
                const float rr = (p->type == PARTICLE_FLASH) ? (pr * 1.7f) : pr;
                r = vg_fill_circle(ctx, pp, rr, &pf, 8);
                if (r != VG_OK) {
                    return r;
                }
            }
        }

        if (g->lives > 0) {
            game_state gp = *g;
            gp.player.b = g->player.b;
            float player_depth = 1.0f;
            {
                float d = 0.0f;
                const vg_vec2 pp = project_cylinder_point(g, g->player.b.x, g->player.b.y, &d);
                gp.player.b.x = pp.x;
                gp.player.b.y = pp.y;
                player_depth = d;
            }
            r = draw_player_ship(ctx, &gp, &ship_style, &thruster_fill);
            if (r != VG_OK) {
                return r;
            }
            if (g->shield_active && g->shield_time_remaining_s > 0.0f) {
                r = draw_player_shield(
                    ctx,
                    gp.player.b.x,
                    gp.player.b.y,
                    g->shield_radius * (0.45f + player_depth * 0.90f),
                    g->t,
                    &shield_glow,
                    &shield_ring
                );
                if (r != VG_OK) {
                    return r;
                }
            }
        }
        for (int i = 0; i < MAX_POWERUPS; ++i) {
            const powerup_pickup* p = &g->powerups[i];
            float depth = 0.0f;
            vg_vec2 cp;
            float rr;
            if (!p->active) {
                continue;
            }
            cp = project_cylinder_point(g, p->b.x, p->b.y, &depth);
            if (cp.x < -36.0f || cp.x > g->world_w + 36.0f || cp.y < -36.0f || cp.y > g->world_h + 36.0f) {
                continue;
            }
            rr = p->radius * (0.42f + depth * 0.96f);
            r = draw_powerup_medallion(
                ctx,
                cp.x,
                cp.y,
                rr,
                p->spin,
                p->bob_phase,
                p->type,
                intensity_scale * (0.45f + depth * 0.95f),
                &txt_halo,
                &txt_main
            );
            if (r != VG_OK) {
                return r;
            }
        }
        if (g->emp_effect_active) {
            r = draw_emp_blast(ctx, g, &pal, intensity_scale, 1);
            if (r != VG_OK) {
                return r;
            }
        }

        for (size_t i = 0; i < MAX_BULLETS; ++i) {
            if (!g->bullets[i].active) {
                continue;
            }
            const bullet* b = &g->bullets[i];
            float ux = b->b.vx;
            float uy = b->b.vy;
            float speed = sqrtf(ux * ux + uy * uy);
            if (speed > 1.0e-4f) {
                ux /= speed;
                uy /= speed;
            } else {
                ux = 1.0f;
                uy = 0.0f;
                speed = 0.0f;
            }
            const float core_f = 13.5f;
            const float core_b = 8.0f;
            const float trail = 22.0f + clampf(speed * 0.030f, 10.0f, 34.0f);
            const float x0w = b->b.x - ux * trail;
            const float y0w = b->b.y - uy * trail;
            const float x1w = b->b.x - ux * core_b;
            const float y1w = b->b.y - uy * core_b;
            const float x2w = b->b.x + ux * core_f;
            const float y2w = b->b.y + uy * core_f;
            float d0 = 0.0f, d1 = 0.0f, d2 = 0.0f;
            const vg_vec2 p0 = project_cylinder_point(g, x0w, y0w, &d0);
            const vg_vec2 p1 = project_cylinder_point(g, x1w, y1w, &d1);
            const vg_vec2 p2 = project_cylinder_point(g, x2w, y2w, &d2);
            const float depth = (d0 + d1 + d2) * (1.0f / 3.0f);
            vg_stroke_style core = bullet_style;
            vg_stroke_style halo = bullet_halo_style;
            core.width_px *= 0.44f + 0.92f * depth;
            halo.width_px *= 0.40f + 0.95f * depth;
            core.intensity *= 0.34f + 0.98f * depth;
            halo.intensity *= 0.28f + 0.92f * depth;
            core.color.a *= 0.36f + 0.76f * depth;
            halo.color.a *= 0.34f + 0.74f * depth;
            vg_stroke_style core_tail = core;
            vg_stroke_style halo_tail = halo;
            core_tail.intensity *= 0.58f;
            halo_tail.intensity *= 0.62f;
            core_tail.color.a *= 0.62f;
            halo_tail.color.a *= 0.62f;
            const vg_vec2 seg_tail[] = {p0, p1};
            const vg_vec2 seg_core[] = {p1, p2};
            r = vg_draw_polyline(ctx, seg_tail, 2, &halo_tail, 0);
            if (r != VG_OK) {
                return r;
            }
            r = vg_draw_polyline(ctx, seg_tail, 2, &core_tail, 0);
            if (r != VG_OK) {
                return r;
            }
            r = vg_draw_polyline(ctx, seg_core, 2, &halo, 0);
            if (r != VG_OK) {
                return r;
            }
            r = vg_draw_polyline(ctx, seg_core, 2, &core, 0);
            if (r != VG_OK) {
                return r;
            }
        }

        for (size_t i = 0; i < MAX_ENEMY_BULLETS; ++i) {
            if (!g->enemy_bullets[i].active) {
                continue;
            }
            const enemy_bullet* b = &g->enemy_bullets[i];
            float ux = b->b.vx;
            float uy = b->b.vy;
            float speed = sqrtf(ux * ux + uy * uy);
            if (speed > 1.0e-4f) {
                ux /= speed;
                uy /= speed;
            } else {
                ux = -1.0f;
                uy = 0.0f;
                speed = 0.0f;
            }
            const float core_f = 10.5f;
            const float core_b = 6.2f;
            const float trail = 16.0f + clampf(speed * 0.026f, 7.0f, 24.0f);
            const float x0w = b->b.x - ux * trail;
            const float y0w = b->b.y - uy * trail;
            const float x1w = b->b.x - ux * core_b;
            const float y1w = b->b.y - uy * core_b;
            const float x2w = b->b.x + ux * core_f;
            const float y2w = b->b.y + uy * core_f;
            float d0 = 0.0f, d1 = 0.0f, d2 = 0.0f;
            const vg_vec2 p0 = project_cylinder_point(g, x0w, y0w, &d0);
            const vg_vec2 p1 = project_cylinder_point(g, x1w, y1w, &d1);
            const vg_vec2 p2 = project_cylinder_point(g, x2w, y2w, &d2);
            const float depth = (d0 + d1 + d2) * (1.0f / 3.0f);
            vg_stroke_style core = enemy_bullet_style;
            vg_stroke_style halo = enemy_bullet_halo_style;
            core.width_px *= 0.42f + depth * 0.92f;
            halo.width_px *= 0.42f + depth * 0.98f;
            core.intensity *= 0.30f + depth * 0.90f;
            halo.intensity *= 0.28f + depth * 0.86f;
            core.color.a *= 0.30f + depth * 0.78f;
            halo.color.a *= 0.30f + depth * 0.72f;
            vg_stroke_style core_tail = core;
            vg_stroke_style halo_tail = halo;
            core_tail.intensity *= 0.56f;
            halo_tail.intensity *= 0.60f;
            core_tail.color.a *= 0.60f;
            halo_tail.color.a *= 0.60f;
            const vg_vec2 seg_tail[] = {p0, p1};
            const vg_vec2 seg_core[] = {p1, p2};
            r = vg_draw_polyline(ctx, seg_tail, 2, &halo_tail, 0);
            if (r != VG_OK) {
                return r;
            }
            r = vg_draw_polyline(ctx, seg_tail, 2, &core_tail, 0);
            if (r != VG_OK) {
                return r;
            }
            r = vg_draw_polyline(ctx, seg_core, 2, &halo, 0);
            if (r != VG_OK) {
                return r;
            }
            r = vg_draw_polyline(ctx, seg_core, 2, &core, 0);
            if (r != VG_OK) {
                return r;
            }
        }

        for (size_t i = 0; i < MAX_ENEMY_DEBRIS; ++i) {
            const enemy_debris* dbr = &g->debris[i];
            if (!dbr->active || dbr->alpha <= 0.01f) {
                continue;
            }
            const float c = cosf(dbr->angle);
            const float s = sinf(dbr->angle);
            const float hx = c * dbr->half_len;
            const float hy = s * dbr->half_len;
            float d0 = 0.0f;
            float d1 = 0.0f;
            const vg_vec2 a = project_cylinder_point(g, dbr->b.x - hx, dbr->b.y - hy, &d0);
            const vg_vec2 b = project_cylinder_point(g, dbr->b.x + hx, dbr->b.y + hy, &d1);
            const float depth = 0.5f * (d0 + d1);
            vg_stroke_style ds = enemy_style;
            ds.width_px *= 0.72f + depth * 0.90f;
            ds.intensity *= (0.26f + depth * 0.86f) * dbr->alpha;
            ds.color.a *= dbr->alpha * (0.24f + depth * 0.82f);
            {
                const vg_vec2 seg[] = {a, b};
                r = vg_draw_polyline(ctx, seg, 2, &ds, 0);
            }
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
            vg_stroke_style es = enemy_style;
            es.width_px *= 0.55f + d * 0.8f;
            es.intensity *= 0.20f + d * 0.80f;
            es.color.a *= 0.20f + d * 0.80f;
            {
                /* Cylinder-only orientation correction:
                 * derive on-screen facing from projected nearby points so
                 * the glyph flips naturally on the back half of the cylinder. */
                enemy e_proj = *e;
                const float sample = fmaxf(e->radius * 0.9f, 4.0f);
                const vg_vec2 p_a = project_cylinder_point(
                    g,
                    e->b.x + e->facing_x * sample,
                    e->b.y + e->facing_y * sample,
                    NULL
                );
                float sfx = p_a.x - c.x;
                float sfy = p_a.y - c.y;
                const float sl = sqrtf(sfx * sfx + sfy * sfy);
                if (sl > 1e-4f) {
                    sfx /= sl;
                    sfy /= sl;
                    e_proj.facing_x = sfx;
                    e_proj.facing_y = sfy;
                }
                r = draw_enemy_glyph(ctx, &e_proj, c.x, c.y, rr, &es);
            }
            if (r != VG_OK) {
                return r;
            }
        }

        r = draw_top_meters(ctx, g, &txt_halo, &txt_main);
        if (r == VG_OK && metrics->show_fps) {
            r = draw_fps_overlay(ctx, g->world_w, g->world_h, metrics->fps, &txt_halo, &txt_main);
        }
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
        r = draw_terrain_tuning_overlay(ctx, g->world_w, g->world_h, metrics->terrain_tuning_text, &txt_halo, &txt_main);
        if (r != VG_OK) {
            return r;
        }
        if (g->lives <= 0) {
            const float ui = ui_reference_scale(g->world_w, g->world_h);
            const float go_size = 36.0f * ui;
            const float go_spacing = 2.2f * ui;
            const char* go_title = g->orbit_decay_timeout ? "ORBIT DECAYED" : "GAME OVER";
            const float go_w = vg_measure_text(go_title, go_size, go_spacing);
            r = draw_text_vector_glow(
                ctx,
                go_title,
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

    if (!foreground_only && draw_star_background) {
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
            const int vis_head = star_visible_with_mask(g, sx, g->stars[i].y);
            const int vis_tail = star_visible_with_mask(g, stx, ty);
            if (!vis_head) {
                continue;
            }
            /* Keep mask strict: if trail exits the window, skip the streak. */
            const int draw_streak = vis_head && vis_tail;
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

            if (draw_streak) {
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
            }

            r = vg_fill_circle(ctx, (vg_vec2){sx, g->stars[i].y}, g->stars[i].size + 0.4f * u, &star_fill, 10);
            if (r != VG_OK) {
                return r;
            }
            /* Draw seam-duplicate heads near edges for continuous wrap. */
            if (sx < 8.0f) {
                const float sx2 = sx + g->world_w;
                if (star_visible_with_mask(g, sx2, g->stars[i].y)) {
                    r = vg_fill_circle(ctx, (vg_vec2){sx2, g->stars[i].y}, g->stars[i].size + 0.4f * u, &star_fill, 10);
                    if (r != VG_OK) {
                        return r;
                    }
                }
            } else if (sx > g->world_w - 8.0f) {
                const float sx2 = sx - g->world_w;
                if (star_visible_with_mask(g, sx2, g->stars[i].y)) {
                    r = vg_fill_circle(ctx, (vg_vec2){sx2, g->stars[i].y}, g->stars[i].size + 0.4f * u, &star_fill, 10);
                    if (r != VG_OK) {
                        return r;
                    }
                }
            }
        }
        if (g->render_style == LEVEL_RENDER_FOG) {
            r = draw_fog_of_war_nebula(ctx, g, &pal, intensity_scale);
            if (r != VG_OK) {
                return r;
            }
        }
    }
    if (!foreground_only) {
        r = draw_background_window_mask_overlays(ctx, g, &land_halo, &land_main);
        if (r != VG_OK) {
            return r;
        }
    }

    if (!foreground_only) {
        if (g->render_style == LEVEL_RENDER_DRIFTER) {
            if (!metrics->use_gpu_terrain) {
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
            }
        } else if (g->render_style != LEVEL_RENDER_DRIFTER_SHADED &&
                   g->render_style != LEVEL_RENDER_FOG &&
                   g->render_style != LEVEL_RENDER_BLANK) {
            if (g->render_style == LEVEL_RENDER_DEFENDER) {
                if (metrics->use_gpu_industry) {
                    goto skip_legacy_landscape;
                }
                r = draw_defender_industrial_parallax(
                    ctx,
                    g->world_w,
                    g->world_h,
                    g->camera_x,
                    &pal,
                    &land_halo,
                    &land_main
                );
                if (r != VG_OK) {
                    return r;
                }
                goto skip_legacy_landscape;
            }
            /* Foreground vector landscape layers for depth/parallax. */
            vg_stroke_style land1_halo = land_halo;
            vg_stroke_style land1_main = land_main;
            r = draw_parallax_landscape(ctx, g->world_w, g->world_h, g->camera_x, 1.20f, g->world_h * 0.18f, 22.0f, &land1_halo, &land1_main);
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
        }
    }
skip_legacy_landscape:

    if (background_only) {
        return VG_OK;
    }

    r = vg_transform_push(ctx);
    if (r != VG_OK) {
        return r;
    }
    vg_transform_translate(ctx, g->world_w * 0.5f - g->camera_x, g->world_h * 0.5f - g->camera_y);

    if (g->searchlight_count > 0) {
        r = draw_searchlights(ctx, g, &pal, intensity_scale, &land_halo, &land_main);
        if (r != VG_OK) {
            (void)vg_transform_pop(ctx);
            return r;
        }
    }
    if (g->arc_node_count > 1) {
        r = draw_arc_nodes(ctx, g, &pal, &land_halo, &land_main, metrics->use_gpu_arc ? 0 : 1);
        if (r != VG_OK) {
            (void)vg_transform_pop(ctx);
            return r;
        }
    }
    if (g->asteroid_storm_enabled && g->asteroid_count > 0) {
        r = draw_asteroid_storm(ctx, g, &pal, &land_halo, &land_main);
        if (r != VG_OK) {
            (void)vg_transform_pop(ctx);
            return r;
        }
    }
    if (g->mine_count > 0) {
        r = draw_minefields(ctx, g, &pal, &land_halo, &land_main);
        if (r != VG_OK) {
            (void)vg_transform_pop(ctx);
            return r;
        }
    }
    r = draw_level_structures(ctx, g, &pal, &land_halo, &land_main, metrics->use_gpu_particles ? 0 : 1);
    if (r != VG_OK) {
        (void)vg_transform_pop(ctx);
        return r;
    }
    if (g->missile_launcher_count > 0 || g->missile_count > 0) {
        r = draw_missile_system(ctx, g, &pal, &land_halo, &land_main);
        if (r != VG_OK) {
            (void)vg_transform_pop(ctx);
            return r;
        }
    }
    if (g->exit_portal_active) {
        r = draw_exit_portal(ctx, g, &pal, intensity_scale, &land_halo, &land_main);
        if (r != VG_OK) {
            (void)vg_transform_pop(ctx);
            return r;
        }
    }

    if (!metrics->use_gpu_particles) {
    for (size_t i = 0; i < MAX_PARTICLES; ++i) {
        /* Particle LOD: keep frame time stable under heavy explosion loads. */
        const int active_particles = g->active_particles;
        int stride = 1;
        if (active_particles > 360) {
            stride = 2;
        }
        if (active_particles > 620) {
            stride = 3;
        }
        if (active_particles > 900) {
            stride = 4;
        }
        if ((int)(i % (size_t)stride) != 0) {
            continue;
        }
        const particle* p = &g->particles[i];
        if (!p->active) {
            continue;
        }
        if (p->a <= 0.02f || p->size <= 0.15f) {
            continue;
        }
        if (p->b.x < g->camera_x - g->world_w * 0.58f || p->b.x > g->camera_x + g->world_w * 0.58f ||
            p->b.y < g->camera_y - g->world_h * 0.58f || p->b.y > g->camera_y + g->world_h * 0.58f) {
            continue;
        }
        vg_fill_style pf = make_fill(1.0f, (vg_color){p->r, p->g, p->bcol, p->a}, VG_BLEND_ADDITIVE);
        vg_stroke_style ps = make_stroke(1.0f, 1.0f, (vg_color){p->r, p->g, p->bcol, p->a}, VG_BLEND_ADDITIVE);
        const int simplify_geom = (active_particles > 520);
        if (p->type == PARTICLE_POINT || p->type == PARTICLE_FLASH || simplify_geom) {
            const float rr = (p->type == PARTICLE_FLASH) ? (p->size * 1.7f) : p->size;
            r = vg_fill_circle(ctx, (vg_vec2){p->b.x, p->b.y}, rr, &pf, 8);
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
    }

    if (g->lives > 0) {
        r = draw_player_ship(ctx, g, &ship_style, &thruster_fill);
        if (r != VG_OK) {
            (void)vg_transform_pop(ctx);
            return r;
        }
        if (g->shield_active && g->shield_time_remaining_s > 0.0f) {
            r = draw_player_shield(
                ctx,
                g->player.b.x,
                g->player.b.y,
                g->shield_radius,
                g->t,
                &shield_glow,
                &shield_ring
            );
            if (r != VG_OK) {
                (void)vg_transform_pop(ctx);
                return r;
            }
        }
    }
    for (int i = 0; i < MAX_POWERUPS; ++i) {
        const powerup_pickup* p = &g->powerups[i];
        if (!p->active) {
            continue;
        }
        r = draw_powerup_medallion(
            ctx,
            p->b.x,
            p->b.y,
            p->radius,
            p->spin,
            p->bob_phase,
            p->type,
            intensity_scale,
            &txt_halo,
            &txt_main
        );
        if (r != VG_OK) {
            (void)vg_transform_pop(ctx);
            return r;
        }
    }
    if (g->emp_effect_active) {
        r = draw_emp_blast(ctx, g, &pal, intensity_scale, 0);
        if (r != VG_OK) {
            (void)vg_transform_pop(ctx);
            return r;
        }
    }

    for (size_t i = 0; i < MAX_BULLETS; ++i) {
        if (!g->bullets[i].active) {
            continue;
        }
        const bullet* b = &g->bullets[i];
        float ux = b->b.vx;
        float uy = b->b.vy;
        float speed = sqrtf(ux * ux + uy * uy);
        if (speed > 1.0e-4f) {
            ux /= speed;
            uy /= speed;
        } else {
            ux = 1.0f;
            uy = 0.0f;
            speed = 0.0f;
        }
        const float core_f = 13.5f;
        const float core_b = 8.0f;
        const float trail = 22.0f + clampf(speed * 0.030f, 10.0f, 34.0f);
        const vg_vec2 seg_tail[] = {
            {b->b.x - ux * trail, b->b.y - uy * trail},
            {b->b.x - ux * core_b, b->b.y - uy * core_b}
        };
        const vg_vec2 seg_core[] = {
            {b->b.x - ux * core_b, b->b.y - uy * core_b},
            {b->b.x + ux * core_f, b->b.y + uy * core_f}
        };
        vg_stroke_style core = bullet_style;
        vg_stroke_style halo = bullet_halo_style;
        vg_stroke_style core_tail = core;
        vg_stroke_style halo_tail = halo;
        core_tail.intensity *= 0.60f;
        halo_tail.intensity *= 0.64f;
        core_tail.color.a *= 0.66f;
        halo_tail.color.a *= 0.66f;
        r = vg_draw_polyline(ctx, seg_tail, 2, &halo_tail, 0);
        if (r != VG_OK) {
            (void)vg_transform_pop(ctx);
            return r;
        }
        r = vg_draw_polyline(ctx, seg_tail, 2, &core_tail, 0);
        if (r != VG_OK) {
            (void)vg_transform_pop(ctx);
            return r;
        }
        r = vg_draw_polyline(ctx, seg_core, 2, &halo, 0);
        if (r != VG_OK) {
            (void)vg_transform_pop(ctx);
            return r;
        }
        r = vg_draw_polyline(ctx, seg_core, 2, &core, 0);
        if (r != VG_OK) {
            (void)vg_transform_pop(ctx);
            return r;
        }
    }

    for (size_t i = 0; i < MAX_ENEMY_BULLETS; ++i) {
        if (!g->enemy_bullets[i].active) {
            continue;
        }
        const enemy_bullet* b = &g->enemy_bullets[i];
        float ux = b->b.vx;
        float uy = b->b.vy;
        float speed = sqrtf(ux * ux + uy * uy);
        if (speed > 1.0e-4f) {
            ux /= speed;
            uy /= speed;
        } else {
            ux = -1.0f;
            uy = 0.0f;
            speed = 0.0f;
        }
        const float core_f = 10.5f;
        const float core_b = 6.2f;
        const float trail = 16.0f + clampf(speed * 0.026f, 7.0f, 24.0f);
        const vg_vec2 seg_tail[] = {
            {b->b.x - ux * trail, b->b.y - uy * trail},
            {b->b.x - ux * core_b, b->b.y - uy * core_b}
        };
        const vg_vec2 seg_core[] = {
            {b->b.x - ux * core_b, b->b.y - uy * core_b},
            {b->b.x + ux * core_f, b->b.y + uy * core_f}
        };
        vg_stroke_style core = enemy_bullet_style;
        vg_stroke_style halo = enemy_bullet_halo_style;
        vg_stroke_style core_tail = core;
        vg_stroke_style halo_tail = halo;
        core_tail.intensity *= 0.58f;
        halo_tail.intensity *= 0.62f;
        core_tail.color.a *= 0.62f;
        halo_tail.color.a *= 0.62f;
        r = vg_draw_polyline(ctx, seg_tail, 2, &halo_tail, 0);
        if (r != VG_OK) {
            (void)vg_transform_pop(ctx);
            return r;
        }
        r = vg_draw_polyline(ctx, seg_tail, 2, &core_tail, 0);
        if (r != VG_OK) {
            (void)vg_transform_pop(ctx);
            return r;
        }
        r = vg_draw_polyline(ctx, seg_core, 2, &halo, 0);
        if (r != VG_OK) {
            (void)vg_transform_pop(ctx);
            return r;
        }
        r = vg_draw_polyline(ctx, seg_core, 2, &core, 0);
        if (r != VG_OK) {
            (void)vg_transform_pop(ctx);
            return r;
        }
    }

    for (size_t i = 0; i < MAX_ENEMY_DEBRIS; ++i) {
        const enemy_debris* dbr = &g->debris[i];
        if (!dbr->active || dbr->alpha <= 0.01f) {
            continue;
        }
        const float c = cosf(dbr->angle);
        const float s = sinf(dbr->angle);
        const float hx = c * dbr->half_len;
        const float hy = s * dbr->half_len;
        const vg_vec2 seg[] = {
            {dbr->b.x - hx, dbr->b.y - hy},
            {dbr->b.x + hx, dbr->b.y + hy}
        };
        vg_stroke_style ds = enemy_style;
        ds.width_px *= 0.90f;
        ds.intensity *= 0.92f * dbr->alpha;
        ds.color.a *= dbr->alpha;
        r = vg_draw_polyline(ctx, seg, 2, &ds, 0);
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
        r = draw_enemy_glyph(ctx, e, e->b.x, e->b.y, rr, &enemy_style);
        if (r != VG_OK) {
            (void)vg_transform_pop(ctx);
            return r;
        }
    }
    r = vg_transform_pop(ctx);
    if (r != VG_OK) {
        return r;
    }

    r = draw_top_meters(ctx, g, &txt_halo, &txt_main);
    if (r == VG_OK && metrics->show_fps) {
        r = draw_fps_overlay(ctx, g->world_w, g->world_h, metrics->fps, &txt_halo, &txt_main);
    }
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
    r = draw_terrain_tuning_overlay(ctx, g->world_w, g->world_h, metrics->terrain_tuning_text, &txt_halo, &txt_main);
    if (r != VG_OK) {
        return r;
    }

    if (g->lives <= 0) {
        const float ui = ui_reference_scale(g->world_w, g->world_h);
        const float go_size = 36.0f * ui;
        const float go_spacing = 2.2f * ui;
        const char* go_title = g->orbit_decay_timeout ? "ORBIT DECAYED" : "GAME OVER";
        const float go_w = vg_measure_text(go_title, go_size, go_spacing);
        r = draw_text_vector_glow(
            ctx,
            go_title,
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
