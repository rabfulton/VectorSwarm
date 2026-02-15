#include "vg.h"
#include "vg_internal.h"
#include "vg_palette.h"

#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static int vg_path_reserve(vg_path* path, size_t extra) {
    if (path->count + extra <= path->cap) {
        return 1;
    }

    size_t new_cap = path->cap == 0 ? 32u : path->cap * 2u;
    while (new_cap < path->count + extra) {
        new_cap *= 2u;
    }

    vg_path_cmd* next = (vg_path_cmd*)realloc(path->cmds, new_cap * sizeof(*next));
    if (!next) {
        return 0;
    }

    path->cmds = next;
    path->cap = new_cap;
    return 1;
}

static int vg_style_is_valid(const vg_stroke_style* style) {
    if (!style) {
        return 0;
    }
    if (!isfinite(style->width_px) || style->width_px <= 0.0f) {
        return 0;
    }
    if (!isfinite(style->intensity) || style->intensity < 0.0f) {
        return 0;
    }
    if (!isfinite(style->miter_limit) || style->miter_limit <= 0.0f) {
        return 0;
    }
    if (style->cap < VG_LINE_CAP_BUTT || style->cap > VG_LINE_CAP_SQUARE) {
        return 0;
    }
    if (style->join < VG_LINE_JOIN_MITER || style->join > VG_LINE_JOIN_BEVEL) {
        return 0;
    }
    if (style->blend < VG_BLEND_ALPHA || style->blend > VG_BLEND_ADDITIVE) {
        return 0;
    }
    if (style->stencil.enabled) {
        if (style->stencil.compare_op < VG_COMPARE_NEVER || style->stencil.compare_op > VG_COMPARE_ALWAYS) {
            return 0;
        }
        if (style->stencil.fail_op < VG_STENCIL_OP_KEEP || style->stencil.fail_op > VG_STENCIL_OP_DECREMENT_AND_WRAP) {
            return 0;
        }
        if (style->stencil.pass_op < VG_STENCIL_OP_KEEP || style->stencil.pass_op > VG_STENCIL_OP_DECREMENT_AND_WRAP) {
            return 0;
        }
        if (style->stencil.depth_fail_op < VG_STENCIL_OP_KEEP || style->stencil.depth_fail_op > VG_STENCIL_OP_DECREMENT_AND_WRAP) {
            return 0;
        }
    }
    return 1;
}

static int vg_fill_style_is_valid(const vg_fill_style* style) {
    if (!style) {
        return 0;
    }
    if (!isfinite(style->intensity) || style->intensity < 0.0f) {
        return 0;
    }
    if (style->blend < VG_BLEND_ALPHA || style->blend > VG_BLEND_ADDITIVE) {
        return 0;
    }
    if (style->stencil.enabled) {
        if (style->stencil.compare_op < VG_COMPARE_NEVER || style->stencil.compare_op > VG_COMPARE_ALWAYS) {
            return 0;
        }
        if (style->stencil.fail_op < VG_STENCIL_OP_KEEP || style->stencil.fail_op > VG_STENCIL_OP_DECREMENT_AND_WRAP) {
            return 0;
        }
        if (style->stencil.pass_op < VG_STENCIL_OP_KEEP || style->stencil.pass_op > VG_STENCIL_OP_DECREMENT_AND_WRAP) {
            return 0;
        }
        if (style->stencil.depth_fail_op < VG_STENCIL_OP_KEEP || style->stencil.depth_fail_op > VG_STENCIL_OP_DECREMENT_AND_WRAP) {
            return 0;
        }
    }
    return 1;
}

static vg_mat2x3 vg_mat_identity(void) {
    vg_mat2x3 m = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
    return m;
}

static int vg_mat_is_identity(vg_mat2x3 m) {
    return m.m00 == 1.0f && m.m01 == 0.0f && m.m02 == 0.0f &&
           m.m10 == 0.0f && m.m11 == 1.0f && m.m12 == 0.0f;
}

static vg_mat2x3 vg_mat_mul(vg_mat2x3 a, vg_mat2x3 b) {
    vg_mat2x3 out = {
        a.m00 * b.m00 + a.m01 * b.m10,
        a.m00 * b.m01 + a.m01 * b.m11,
        a.m00 * b.m02 + a.m01 * b.m12 + a.m02,
        a.m10 * b.m00 + a.m11 * b.m10,
        a.m10 * b.m01 + a.m11 * b.m11,
        a.m10 * b.m02 + a.m11 * b.m12 + a.m12
    };
    return out;
}

static vg_vec2 vg_transform_point(vg_mat2x3 m, vg_vec2 p) {
    vg_vec2 out = {
        m.m00 * p.x + m.m01 * p.y + m.m02,
        m.m10 * p.x + m.m11 * p.y + m.m12
    };
    return out;
}

static int vg_rect_is_valid(vg_rect rect) {
    return isfinite(rect.x) && isfinite(rect.y) && isfinite(rect.w) && isfinite(rect.h) && rect.w > 0.0f && rect.h > 0.0f;
}

static vg_rect vg_rect_intersection(vg_rect a, vg_rect b) {
    float x0 = fmaxf(a.x, b.x);
    float y0 = fmaxf(a.y, b.y);
    float x1 = fminf(a.x + a.w, b.x + b.w);
    float y1 = fminf(a.y + a.h, b.y + b.h);
    vg_rect out = {x0, y0, x1 - x0, y1 - y0};
    if (out.w < 0.0f) {
        out.w = 0.0f;
    }
    if (out.h < 0.0f) {
        out.h = 0.0f;
    }
    return out;
}

static vg_rect vg_transform_rect_aabb(vg_mat2x3 m, vg_rect rect) {
    vg_vec2 p0 = vg_transform_point(m, (vg_vec2){rect.x, rect.y});
    vg_vec2 p1 = vg_transform_point(m, (vg_vec2){rect.x + rect.w, rect.y});
    vg_vec2 p2 = vg_transform_point(m, (vg_vec2){rect.x + rect.w, rect.y + rect.h});
    vg_vec2 p3 = vg_transform_point(m, (vg_vec2){rect.x, rect.y + rect.h});
    float min_x = fminf(fminf(p0.x, p1.x), fminf(p2.x, p3.x));
    float min_y = fminf(fminf(p0.y, p1.y), fminf(p2.y, p3.y));
    float max_x = fmaxf(fmaxf(p0.x, p1.x), fmaxf(p2.x, p3.x));
    float max_y = fmaxf(fmaxf(p0.y, p1.y), fmaxf(p2.y, p3.y));
    vg_rect out = {min_x, min_y, max_x - min_x, max_y - min_y};
    if (!isfinite(out.x) || !isfinite(out.y) || !isfinite(out.w) || !isfinite(out.h)) {
        out = (vg_rect){0.0f, 0.0f, 0.0f, 0.0f};
    }
    if (out.w < 0.0f) {
        out.w = 0.0f;
    }
    if (out.h < 0.0f) {
        out.h = 0.0f;
    }
    return out;
}

static vg_crt_profile vg_crt_profile_for_preset(vg_crt_preset preset) {
    vg_crt_profile p = {0};
    switch (preset) {
        case VG_CRT_PRESET_CLEAN_VECTOR:
            p.beam_core_width_px = 1.25f;
            p.beam_halo_width_px = 1.4f;
            p.beam_intensity = 1.0f;
            p.bloom_strength = 0.25f;
            p.bloom_radius_px = 2.0f;
            p.persistence_decay = 0.86f;
            p.jitter_amount = 0.01f;
            p.flicker_amount = 0.01f;
            p.vignette_strength = 0.05f;
            p.barrel_distortion = 0.0f;
            p.scanline_strength = 0.03f;
            p.noise_strength = 0.02f;
            break;
        case VG_CRT_PRESET_HEAVY_CRT:
            p.beam_core_width_px = 2.0f;
            p.beam_halo_width_px = 4.2f;
            p.beam_intensity = 1.3f;
            p.bloom_strength = 1.15f;
            p.bloom_radius_px = 6.0f;
            p.persistence_decay = 0.94f;
            p.jitter_amount = 0.18f;
            p.flicker_amount = 0.14f;
            p.vignette_strength = 0.24f;
            p.barrel_distortion = 0.13f;
            p.scanline_strength = 0.22f;
            p.noise_strength = 0.08f;
            break;
        case VG_CRT_PRESET_WOPR:
        default:
            p.beam_core_width_px = 1.6f;
            p.beam_halo_width_px = 2.8f;
            p.beam_intensity = 1.15f;
            p.bloom_strength = 0.75f;
            p.bloom_radius_px = 4.0f;
            p.persistence_decay = 0.90f;
            p.jitter_amount = 0.08f;
            p.flicker_amount = 0.06f;
            p.vignette_strength = 0.14f;
            p.barrel_distortion = 0.06f;
            p.scanline_strength = 0.12f;
            p.noise_strength = 0.04f;
            break;
    }
    return p;
}

static void vg_retro_from_crt(vg_retro_params* retro, const vg_crt_profile* crt) {
    retro->bloom_strength = crt->bloom_strength;
    retro->bloom_radius_px = crt->bloom_radius_px;
    retro->persistence_decay = crt->persistence_decay;
    retro->jitter_amount = crt->jitter_amount;
    retro->flicker_amount = crt->flicker_amount;
}

static void vg_crt_from_retro(vg_crt_profile* crt, const vg_retro_params* retro) {
    crt->bloom_strength = retro->bloom_strength;
    crt->bloom_radius_px = retro->bloom_radius_px;
    crt->persistence_decay = retro->persistence_decay;
    crt->jitter_amount = retro->jitter_amount;
    crt->flicker_amount = retro->flicker_amount;
}

#define VG_FONT_UP 0xfeu
#define VG_FONT_LAST 0xffu
#define VG_FONT_POINT(x, y) (uint8_t)((((x) & 0xFu) << 4) | ((y) & 0xFu))

static const uint8_t vg_glyph_space[] = {VG_FONT_LAST};
static const uint8_t vg_glyph_dot[] = {VG_FONT_POINT(3, 0), VG_FONT_POINT(4, 0), VG_FONT_LAST};
static const uint8_t vg_glyph_comma[] = {VG_FONT_POINT(2, 0), VG_FONT_POINT(4, 2), VG_FONT_LAST};
static const uint8_t vg_glyph_dash[] = {VG_FONT_POINT(2, 6), VG_FONT_POINT(6, 6), VG_FONT_LAST};
static const uint8_t vg_glyph_plus[] = {VG_FONT_POINT(1, 6), VG_FONT_POINT(7, 6), VG_FONT_UP, VG_FONT_POINT(4, 9), VG_FONT_POINT(4, 3), VG_FONT_LAST};
static const uint8_t vg_glyph_colon[] = {VG_FONT_POINT(4, 9), VG_FONT_POINT(4, 7), VG_FONT_UP, VG_FONT_POINT(4, 5), VG_FONT_POINT(4, 3), VG_FONT_LAST};
static const uint8_t vg_glyph_slash[] = {VG_FONT_POINT(0, 0), VG_FONT_POINT(8, 12), VG_FONT_LAST};
static const uint8_t vg_glyph_percent[] = {
    VG_FONT_POINT(0, 0), VG_FONT_POINT(8, 12), VG_FONT_UP, VG_FONT_POINT(2, 10), VG_FONT_POINT(2, 8), VG_FONT_UP, VG_FONT_POINT(6, 4), VG_FONT_POINT(6, 2), VG_FONT_LAST
};
static const uint8_t vg_glyph_lparen[] = {VG_FONT_POINT(6, 0), VG_FONT_POINT(2, 4), VG_FONT_POINT(2, 8), VG_FONT_POINT(6, 12), VG_FONT_LAST};
static const uint8_t vg_glyph_rparen[] = {VG_FONT_POINT(2, 0), VG_FONT_POINT(6, 4), VG_FONT_POINT(6, 8), VG_FONT_POINT(2, 12), VG_FONT_LAST};
static const uint8_t vg_glyph_question[] = {
    VG_FONT_POINT(0, 8), VG_FONT_POINT(4, 12), VG_FONT_POINT(8, 8), VG_FONT_POINT(4, 4), VG_FONT_UP, VG_FONT_POINT(4, 1), VG_FONT_POINT(4, 0), VG_FONT_LAST
};

static const uint8_t vg_glyph_0[] = {VG_FONT_POINT(0, 0), VG_FONT_POINT(8, 0), VG_FONT_POINT(8, 12), VG_FONT_POINT(0, 12), VG_FONT_POINT(0, 0), VG_FONT_POINT(8, 12), VG_FONT_LAST};
static const uint8_t vg_glyph_1[] = {VG_FONT_POINT(4, 0), VG_FONT_POINT(4, 12), VG_FONT_POINT(3, 10), VG_FONT_LAST};
static const uint8_t vg_glyph_2[] = {VG_FONT_POINT(0, 12), VG_FONT_POINT(8, 12), VG_FONT_POINT(8, 7), VG_FONT_POINT(0, 5), VG_FONT_POINT(0, 0), VG_FONT_POINT(8, 0), VG_FONT_LAST};
static const uint8_t vg_glyph_3[] = {VG_FONT_POINT(0, 12), VG_FONT_POINT(8, 12), VG_FONT_POINT(8, 0), VG_FONT_POINT(0, 0), VG_FONT_UP, VG_FONT_POINT(0, 6), VG_FONT_POINT(8, 6), VG_FONT_LAST};
static const uint8_t vg_glyph_4[] = {VG_FONT_POINT(0, 12), VG_FONT_POINT(0, 6), VG_FONT_POINT(8, 6), VG_FONT_UP, VG_FONT_POINT(8, 12), VG_FONT_POINT(8, 0), VG_FONT_LAST};
static const uint8_t vg_glyph_5[] = {VG_FONT_POINT(0, 0), VG_FONT_POINT(8, 0), VG_FONT_POINT(8, 6), VG_FONT_POINT(0, 7), VG_FONT_POINT(0, 12), VG_FONT_POINT(8, 12), VG_FONT_LAST};
static const uint8_t vg_glyph_6[] = {VG_FONT_POINT(0, 12), VG_FONT_POINT(0, 0), VG_FONT_POINT(8, 0), VG_FONT_POINT(8, 5), VG_FONT_POINT(0, 7), VG_FONT_LAST};
static const uint8_t vg_glyph_7[] = {VG_FONT_POINT(0, 12), VG_FONT_POINT(8, 12), VG_FONT_POINT(8, 6), VG_FONT_POINT(4, 0), VG_FONT_LAST};
static const uint8_t vg_glyph_8[] = {VG_FONT_POINT(0, 0), VG_FONT_POINT(8, 0), VG_FONT_POINT(8, 12), VG_FONT_POINT(0, 12), VG_FONT_POINT(0, 0), VG_FONT_UP, VG_FONT_POINT(0, 6), VG_FONT_POINT(8, 6), VG_FONT_LAST};
static const uint8_t vg_glyph_9[] = {VG_FONT_POINT(8, 0), VG_FONT_POINT(8, 12), VG_FONT_POINT(0, 12), VG_FONT_POINT(0, 7), VG_FONT_POINT(8, 5), VG_FONT_LAST};

static const uint8_t vg_glyph_A[] = {VG_FONT_POINT(0, 0), VG_FONT_POINT(0, 8), VG_FONT_POINT(4, 12), VG_FONT_POINT(8, 8), VG_FONT_POINT(8, 0), VG_FONT_UP, VG_FONT_POINT(0, 4), VG_FONT_POINT(8, 4), VG_FONT_LAST};
static const uint8_t vg_glyph_B[] = {VG_FONT_POINT(0, 0), VG_FONT_POINT(0, 12), VG_FONT_POINT(4, 12), VG_FONT_POINT(8, 10), VG_FONT_POINT(4, 6), VG_FONT_POINT(8, 2), VG_FONT_POINT(4, 0), VG_FONT_POINT(0, 0), VG_FONT_LAST};
static const uint8_t vg_glyph_C[] = {VG_FONT_POINT(8, 0), VG_FONT_POINT(0, 0), VG_FONT_POINT(0, 12), VG_FONT_POINT(8, 12), VG_FONT_LAST};
static const uint8_t vg_glyph_D[] = {VG_FONT_POINT(0, 0), VG_FONT_POINT(0, 12), VG_FONT_POINT(4, 12), VG_FONT_POINT(8, 8), VG_FONT_POINT(8, 4), VG_FONT_POINT(4, 0), VG_FONT_POINT(0, 0), VG_FONT_LAST};
static const uint8_t vg_glyph_E[] = {VG_FONT_POINT(8, 0), VG_FONT_POINT(0, 0), VG_FONT_POINT(0, 12), VG_FONT_POINT(8, 12), VG_FONT_UP, VG_FONT_POINT(0, 6), VG_FONT_POINT(6, 6), VG_FONT_LAST};
static const uint8_t vg_glyph_F[] = {VG_FONT_POINT(0, 0), VG_FONT_POINT(0, 12), VG_FONT_POINT(8, 12), VG_FONT_UP, VG_FONT_POINT(0, 6), VG_FONT_POINT(6, 6), VG_FONT_LAST};
static const uint8_t vg_glyph_G[] = {VG_FONT_POINT(6, 6), VG_FONT_POINT(8, 4), VG_FONT_POINT(8, 0), VG_FONT_POINT(0, 0), VG_FONT_POINT(0, 12), VG_FONT_POINT(8, 12), VG_FONT_LAST};
static const uint8_t vg_glyph_H[] = {VG_FONT_POINT(0, 0), VG_FONT_POINT(0, 12), VG_FONT_UP, VG_FONT_POINT(0, 6), VG_FONT_POINT(8, 6), VG_FONT_UP, VG_FONT_POINT(8, 12), VG_FONT_POINT(8, 0), VG_FONT_LAST};
static const uint8_t vg_glyph_I[] = {VG_FONT_POINT(0, 0), VG_FONT_POINT(8, 0), VG_FONT_UP, VG_FONT_POINT(4, 0), VG_FONT_POINT(4, 12), VG_FONT_UP, VG_FONT_POINT(0, 12), VG_FONT_POINT(8, 12), VG_FONT_LAST};
static const uint8_t vg_glyph_J[] = {VG_FONT_POINT(0, 4), VG_FONT_POINT(4, 0), VG_FONT_POINT(8, 0), VG_FONT_POINT(8, 12), VG_FONT_LAST};
static const uint8_t vg_glyph_K[] = {VG_FONT_POINT(0, 0), VG_FONT_POINT(0, 12), VG_FONT_UP, VG_FONT_POINT(8, 12), VG_FONT_POINT(0, 6), VG_FONT_POINT(6, 0), VG_FONT_LAST};
static const uint8_t vg_glyph_L[] = {VG_FONT_POINT(8, 0), VG_FONT_POINT(0, 0), VG_FONT_POINT(0, 12), VG_FONT_LAST};
static const uint8_t vg_glyph_M[] = {VG_FONT_POINT(0, 0), VG_FONT_POINT(0, 12), VG_FONT_POINT(4, 8), VG_FONT_POINT(8, 12), VG_FONT_POINT(8, 0), VG_FONT_LAST};
static const uint8_t vg_glyph_N[] = {VG_FONT_POINT(0, 0), VG_FONT_POINT(0, 12), VG_FONT_POINT(8, 0), VG_FONT_POINT(8, 12), VG_FONT_LAST};
static const uint8_t vg_glyph_O[] = {VG_FONT_POINT(0, 0), VG_FONT_POINT(0, 12), VG_FONT_POINT(8, 12), VG_FONT_POINT(8, 0), VG_FONT_POINT(0, 0), VG_FONT_LAST};
static const uint8_t vg_glyph_P[] = {VG_FONT_POINT(0, 0), VG_FONT_POINT(0, 12), VG_FONT_POINT(8, 12), VG_FONT_POINT(8, 6), VG_FONT_POINT(0, 5), VG_FONT_LAST};
static const uint8_t vg_glyph_Q[] = {VG_FONT_POINT(0, 0), VG_FONT_POINT(0, 12), VG_FONT_POINT(8, 12), VG_FONT_POINT(8, 4), VG_FONT_POINT(0, 0), VG_FONT_UP, VG_FONT_POINT(4, 4), VG_FONT_POINT(8, 0), VG_FONT_LAST};
static const uint8_t vg_glyph_R[] = {VG_FONT_POINT(0, 0), VG_FONT_POINT(0, 12), VG_FONT_POINT(8, 12), VG_FONT_POINT(8, 6), VG_FONT_POINT(0, 5), VG_FONT_UP, VG_FONT_POINT(4, 5), VG_FONT_POINT(8, 0), VG_FONT_LAST};
static const uint8_t vg_glyph_S[] = {VG_FONT_POINT(0, 2), VG_FONT_POINT(2, 0), VG_FONT_POINT(8, 0), VG_FONT_POINT(8, 5), VG_FONT_POINT(0, 7), VG_FONT_POINT(0, 12), VG_FONT_POINT(6, 12), VG_FONT_POINT(8, 10), VG_FONT_LAST};
static const uint8_t vg_glyph_T[] = {VG_FONT_POINT(0, 12), VG_FONT_POINT(8, 12), VG_FONT_UP, VG_FONT_POINT(4, 12), VG_FONT_POINT(4, 0), VG_FONT_LAST};
static const uint8_t vg_glyph_U[] = {VG_FONT_POINT(0, 12), VG_FONT_POINT(0, 2), VG_FONT_POINT(4, 0), VG_FONT_POINT(8, 2), VG_FONT_POINT(8, 12), VG_FONT_LAST};
static const uint8_t vg_glyph_V[] = {VG_FONT_POINT(0, 12), VG_FONT_POINT(4, 0), VG_FONT_POINT(8, 12), VG_FONT_LAST};
static const uint8_t vg_glyph_W[] = {VG_FONT_POINT(0, 12), VG_FONT_POINT(2, 0), VG_FONT_POINT(4, 4), VG_FONT_POINT(6, 0), VG_FONT_POINT(8, 12), VG_FONT_LAST};
static const uint8_t vg_glyph_X[] = {VG_FONT_POINT(0, 0), VG_FONT_POINT(8, 12), VG_FONT_UP, VG_FONT_POINT(0, 12), VG_FONT_POINT(8, 0), VG_FONT_LAST};
static const uint8_t vg_glyph_Y[] = {VG_FONT_POINT(0, 12), VG_FONT_POINT(4, 6), VG_FONT_POINT(8, 12), VG_FONT_UP, VG_FONT_POINT(4, 6), VG_FONT_POINT(4, 0), VG_FONT_LAST};
static const uint8_t vg_glyph_Z[] = {VG_FONT_POINT(0, 12), VG_FONT_POINT(8, 12), VG_FONT_POINT(0, 0), VG_FONT_POINT(8, 0), VG_FONT_UP, VG_FONT_POINT(2, 6), VG_FONT_POINT(6, 6), VG_FONT_LAST};

static const uint8_t* vg_lookup_glyph(char c) {
    unsigned char uc = (unsigned char)c;
    char up = (char)toupper((int)uc);
    switch (up) {
        case ' ':
            return vg_glyph_space;
        case '.':
            return vg_glyph_dot;
        case ',':
            return vg_glyph_comma;
        case '-':
            return vg_glyph_dash;
        case '+':
            return vg_glyph_plus;
        case ':':
            return vg_glyph_colon;
        case '/':
            return vg_glyph_slash;
        case '%':
            return vg_glyph_percent;
        case '(':
            return vg_glyph_lparen;
        case ')':
            return vg_glyph_rparen;
        case '0':
            return vg_glyph_0;
        case '1':
            return vg_glyph_1;
        case '2':
            return vg_glyph_2;
        case '3':
            return vg_glyph_3;
        case '4':
            return vg_glyph_4;
        case '5':
            return vg_glyph_5;
        case '6':
            return vg_glyph_6;
        case '7':
            return vg_glyph_7;
        case '8':
            return vg_glyph_8;
        case '9':
            return vg_glyph_9;
        case 'A':
            return vg_glyph_A;
        case 'B':
            return vg_glyph_B;
        case 'C':
            return vg_glyph_C;
        case 'D':
            return vg_glyph_D;
        case 'E':
            return vg_glyph_E;
        case 'F':
            return vg_glyph_F;
        case 'G':
            return vg_glyph_G;
        case 'H':
            return vg_glyph_H;
        case 'I':
            return vg_glyph_I;
        case 'J':
            return vg_glyph_J;
        case 'K':
            return vg_glyph_K;
        case 'L':
            return vg_glyph_L;
        case 'M':
            return vg_glyph_M;
        case 'N':
            return vg_glyph_N;
        case 'O':
            return vg_glyph_O;
        case 'P':
            return vg_glyph_P;
        case 'Q':
            return vg_glyph_Q;
        case 'R':
            return vg_glyph_R;
        case 'S':
            return vg_glyph_S;
        case 'T':
            return vg_glyph_T;
        case 'U':
            return vg_glyph_U;
        case 'V':
            return vg_glyph_V;
        case 'W':
            return vg_glyph_W;
        case 'X':
            return vg_glyph_X;
        case 'Y':
            return vg_glyph_Y;
        case 'Z':
            return vg_glyph_Z;
        default:
            return vg_glyph_question;
    }
}

static const uint8_t* vg_lookup_glyph_boxed(char c) {
    return vg_lookup_glyph(c);
}

static float vg_text_advance(float size_px, float letter_spacing_px) {
    float scale = size_px / 12.0f;
    return 12.0f * scale + letter_spacing_px;
}

typedef struct vg_glyph_decoded {
    const uint8_t* glyph;
    uint8_t point_count;
    uint8_t run_count;
    uint8_t run_start[8];
    uint8_t run_len[8];
    uint8_t point_x[64];
    uint8_t point_y[64];
} vg_glyph_decoded;

static int vg_decode_glyph(const uint8_t* glyph, vg_glyph_decoded* out) {
    if (!glyph || !out) {
        return 0;
    }
    memset(out, 0, sizeof(*out));
    out->glyph = glyph;
    uint8_t run_start = 0u;
    uint8_t run_len = 0u;

    for (size_t i = 0;; ++i) {
        uint8_t d = glyph[i];
        if (d == VG_FONT_UP || d == VG_FONT_LAST) {
            if (run_len >= 2u && out->run_count < (uint8_t)(sizeof(out->run_start) / sizeof(out->run_start[0]))) {
                out->run_start[out->run_count] = run_start;
                out->run_len[out->run_count] = run_len;
                out->run_count++;
            }
            if (d == VG_FONT_LAST) {
                break;
            }
            run_start = out->point_count;
            run_len = 0u;
            continue;
        }
        if (out->point_count >= (uint8_t)(sizeof(out->point_x) / sizeof(out->point_x[0]))) {
            return 0;
        }
        out->point_x[out->point_count] = (uint8_t)((d >> 4) & 0xFu);
        out->point_y[out->point_count] = (uint8_t)(d & 0xFu);
        out->point_count++;
        run_len++;
    }
    return 1;
}

static const vg_glyph_decoded* vg_get_decoded_glyph(const uint8_t* glyph) {
    enum { VG_GLYPH_CACHE_CAP = 96 };
    static vg_glyph_decoded cache[VG_GLYPH_CACHE_CAP];
    static size_t cache_count = 0u;
    static size_t cache_next = 0u;

    for (size_t i = 0; i < cache_count; ++i) {
        if (cache[i].glyph == glyph) {
            return &cache[i];
        }
    }

    size_t slot = (cache_count < VG_GLYPH_CACHE_CAP) ? cache_count++ : cache_next++;
    if (cache_next >= VG_GLYPH_CACHE_CAP) {
        cache_next = 0u;
    }
    if (!vg_decode_glyph(glyph, &cache[slot])) {
        return NULL;
    }
    return &cache[slot];
}

vg_result vg_context_create(const vg_context_desc* desc, vg_context** out_ctx) {
    if (!desc || !out_ctx) {
        return VG_ERROR_INVALID_ARGUMENT;
    }

    vg_context* ctx = (vg_context*)calloc(1, sizeof(*ctx));
    if (!ctx) {
        return VG_ERROR_OUT_OF_MEMORY;
    }

    ctx->desc = *desc;
    ctx->crt = vg_crt_profile_for_preset(VG_CRT_PRESET_WOPR);
    vg_retro_from_crt(&ctx->retro, &ctx->crt);
    vg_palette_make_wopr(&ctx->palette);
    ctx->transform = vg_mat_identity();
    ctx->transform_stack_count = 0u;

    switch (desc->backend) {
        case VG_BACKEND_VULKAN:
            if (vg_vk_backend_create(ctx) != VG_OK) {
                free(ctx);
                return VG_ERROR_BACKEND;
            }
            break;
        default:
            free(ctx);
            return VG_ERROR_UNSUPPORTED;
    }

    *out_ctx = ctx;
    return VG_OK;
}

void vg_context_destroy(vg_context* ctx) {
    if (!ctx) {
        return;
    }
    if (ctx->backend.ops && ctx->backend.ops->destroy) {
        ctx->backend.ops->destroy(ctx);
    }
    free(ctx);
}

vg_result vg_begin_frame(vg_context* ctx, const vg_frame_desc* frame) {
    if (!ctx || !frame || frame->width == 0 || frame->height == 0) {
        return VG_ERROR_INVALID_ARGUMENT;
    }
    if (ctx->in_frame) {
        return VG_ERROR_INVALID_ARGUMENT;
    }

    if (ctx->backend.ops && ctx->backend.ops->begin_frame) {
        vg_result backend_res = ctx->backend.ops->begin_frame(ctx, frame);
        if (backend_res != VG_OK) {
            return backend_res;
        }
    }

    ctx->frame = *frame;
    ctx->in_frame = 1;
    vg_transform_reset(ctx);
    vg_clip_reset(ctx);
    return VG_OK;
}

vg_result vg_end_frame(vg_context* ctx) {
    if (!ctx || !ctx->in_frame) {
        return VG_ERROR_INVALID_ARGUMENT;
    }

    if (ctx->backend.ops && ctx->backend.ops->end_frame) {
        vg_result backend_res = ctx->backend.ops->end_frame(ctx);
        if (backend_res != VG_OK) {
            return backend_res;
        }
    }

    ctx->in_frame = 0;
    return VG_OK;
}

vg_result vg_stencil_clear(vg_context* ctx, uint32_t value) {
    if (!ctx || !ctx->in_frame) {
        return VG_ERROR_INVALID_ARGUMENT;
    }
    if (!ctx->backend.ops || !ctx->backend.ops->stencil_clear) {
        return VG_ERROR_UNSUPPORTED;
    }
    return ctx->backend.ops->stencil_clear(ctx, value);
}

void vg_stencil_state_init(vg_stencil_state* out_state) {
    if (!out_state) {
        return;
    }
    out_state->enabled = 0;
    out_state->compare_op = VG_COMPARE_ALWAYS;
    out_state->fail_op = VG_STENCIL_OP_KEEP;
    out_state->pass_op = VG_STENCIL_OP_KEEP;
    out_state->depth_fail_op = VG_STENCIL_OP_KEEP;
    out_state->reference = 0u;
    out_state->compare_mask = 0xffu;
    out_state->write_mask = 0xffu;
}

vg_stencil_state vg_stencil_state_disabled(void) {
    vg_stencil_state out;
    vg_stencil_state_init(&out);
    return out;
}

vg_stencil_state vg_stencil_state_make_write_replace(uint32_t reference, uint32_t write_mask) {
    vg_stencil_state out = vg_stencil_state_disabled();
    out.enabled = 1;
    out.compare_op = VG_COMPARE_ALWAYS;
    out.fail_op = VG_STENCIL_OP_KEEP;
    out.pass_op = VG_STENCIL_OP_REPLACE;
    out.depth_fail_op = VG_STENCIL_OP_KEEP;
    out.reference = reference;
    out.compare_mask = 0xffu;
    out.write_mask = write_mask;
    return out;
}

vg_stencil_state vg_stencil_state_make_test_equal(uint32_t reference, uint32_t compare_mask) {
    vg_stencil_state out = vg_stencil_state_disabled();
    out.enabled = 1;
    out.compare_op = VG_COMPARE_EQUAL;
    out.fail_op = VG_STENCIL_OP_KEEP;
    out.pass_op = VG_STENCIL_OP_KEEP;
    out.depth_fail_op = VG_STENCIL_OP_KEEP;
    out.reference = reference;
    out.compare_mask = compare_mask;
    out.write_mask = 0u;
    return out;
}

void vg_set_retro_params(vg_context* ctx, const vg_retro_params* params) {
    if (!ctx || !params) {
        return;
    }
    ctx->retro = *params;
    vg_crt_from_retro(&ctx->crt, params);
    if (ctx->backend.ops && ctx->backend.ops->set_retro_params) {
        ctx->backend.ops->set_retro_params(ctx, params);
    }
    if (ctx->backend.ops && ctx->backend.ops->set_crt_profile) {
        ctx->backend.ops->set_crt_profile(ctx, &ctx->crt);
    }
}

void vg_get_retro_params(vg_context* ctx, vg_retro_params* out_params) {
    if (!ctx || !out_params) {
        return;
    }
    *out_params = ctx->retro;
}

void vg_make_crt_profile(vg_crt_preset preset, vg_crt_profile* out_profile) {
    if (!out_profile) {
        return;
    }
    *out_profile = vg_crt_profile_for_preset(preset);
}

void vg_set_crt_profile(vg_context* ctx, const vg_crt_profile* profile) {
    if (!ctx || !profile) {
        return;
    }
    ctx->crt = *profile;
    vg_retro_from_crt(&ctx->retro, &ctx->crt);
    if (ctx->backend.ops && ctx->backend.ops->set_crt_profile) {
        ctx->backend.ops->set_crt_profile(ctx, &ctx->crt);
    }
    if (ctx->backend.ops && ctx->backend.ops->set_retro_params) {
        ctx->backend.ops->set_retro_params(ctx, &ctx->retro);
    }
}

void vg_get_crt_profile(vg_context* ctx, vg_crt_profile* out_profile) {
    if (!ctx || !out_profile) {
        return;
    }
    *out_profile = ctx->crt;
}

void vg_transform_reset(vg_context* ctx) {
    if (!ctx) {
        return;
    }
    ctx->transform = vg_mat_identity();
    ctx->transform_stack_count = 0u;
}

vg_result vg_transform_push(vg_context* ctx) {
    if (!ctx) {
        return VG_ERROR_INVALID_ARGUMENT;
    }
    if (ctx->transform_stack_count >= (uint32_t)(sizeof(ctx->transform_stack) / sizeof(ctx->transform_stack[0]))) {
        return VG_ERROR_OUT_OF_MEMORY;
    }
    ctx->transform_stack[ctx->transform_stack_count++] = ctx->transform;
    return VG_OK;
}

vg_result vg_transform_pop(vg_context* ctx) {
    if (!ctx || ctx->transform_stack_count == 0u) {
        return VG_ERROR_INVALID_ARGUMENT;
    }
    ctx->transform = ctx->transform_stack[--ctx->transform_stack_count];
    return VG_OK;
}

void vg_transform_set(vg_context* ctx, vg_mat2x3 m) {
    if (!ctx) {
        return;
    }
    ctx->transform = m;
}

vg_mat2x3 vg_transform_get(vg_context* ctx) {
    if (!ctx) {
        return vg_mat_identity();
    }
    return ctx->transform;
}

void vg_transform_translate(vg_context* ctx, float tx, float ty) {
    if (!ctx || !isfinite(tx) || !isfinite(ty)) {
        return;
    }
    vg_mat2x3 t = {1.0f, 0.0f, tx, 0.0f, 1.0f, ty};
    ctx->transform = vg_mat_mul(ctx->transform, t);
}

void vg_transform_scale(vg_context* ctx, float sx, float sy) {
    if (!ctx || !isfinite(sx) || !isfinite(sy)) {
        return;
    }
    vg_mat2x3 s = {sx, 0.0f, 0.0f, 0.0f, sy, 0.0f};
    ctx->transform = vg_mat_mul(ctx->transform, s);
}

void vg_transform_rotate(vg_context* ctx, float radians) {
    if (!ctx || !isfinite(radians)) {
        return;
    }
    float c = cosf(radians);
    float s = sinf(radians);
    vg_mat2x3 r = {c, -s, 0.0f, s, c, 0.0f};
    ctx->transform = vg_mat_mul(ctx->transform, r);
}

vg_result vg_clip_push_rect(vg_context* ctx, vg_rect rect) {
    if (!ctx || !ctx->in_frame || !vg_rect_is_valid(rect)) {
        return VG_ERROR_INVALID_ARGUMENT;
    }
    if (ctx->clip_stack_count >= (uint32_t)(sizeof(ctx->clip_stack) / sizeof(ctx->clip_stack[0]))) {
        return VG_ERROR_OUT_OF_MEMORY;
    }
    vg_rect clip = vg_transform_rect_aabb(ctx->transform, rect);
    if (ctx->clip_stack_count > 0u) {
        clip = vg_rect_intersection(ctx->clip_stack[ctx->clip_stack_count - 1u], clip);
    }
    ctx->clip_stack[ctx->clip_stack_count++] = clip;
    return VG_OK;
}

vg_result vg_clip_pop(vg_context* ctx) {
    if (!ctx || !ctx->in_frame || ctx->clip_stack_count == 0u) {
        return VG_ERROR_INVALID_ARGUMENT;
    }
    ctx->clip_stack_count--;
    return VG_OK;
}

void vg_clip_reset(vg_context* ctx) {
    if (!ctx) {
        return;
    }
    ctx->clip_stack_count = 0u;
}

int vg_context_get_clip(const vg_context* ctx, vg_rect* out_clip) {
    if (!ctx || ctx->clip_stack_count == 0u) {
        return 0;
    }
    if (out_clip) {
        *out_clip = ctx->clip_stack[ctx->clip_stack_count - 1u];
    }
    return 1;
}

vg_result vg_path_create(vg_context* ctx, vg_path** out_path) {
    if (!ctx || !out_path) {
        return VG_ERROR_INVALID_ARGUMENT;
    }

    vg_path* path = (vg_path*)calloc(1, sizeof(*path));
    if (!path) {
        return VG_ERROR_OUT_OF_MEMORY;
    }

    path->owner = ctx;
    *out_path = path;
    return VG_OK;
}

void vg_path_destroy(vg_path* path) {
    if (!path) {
        return;
    }
    free(path->cmds);
    free(path);
}

void vg_path_clear(vg_path* path) {
    if (!path) {
        return;
    }
    path->count = 0;
}

vg_result vg_path_move_to(vg_path* path, vg_vec2 p) {
    if (!path || !vg_path_reserve(path, 1)) {
        return path ? VG_ERROR_OUT_OF_MEMORY : VG_ERROR_INVALID_ARGUMENT;
    }
    path->cmds[path->count].type = VG_CMD_MOVE_TO;
    path->cmds[path->count].p[0] = p;
    path->count++;
    return VG_OK;
}

vg_result vg_path_line_to(vg_path* path, vg_vec2 p) {
    if (!path || !vg_path_reserve(path, 1)) {
        return path ? VG_ERROR_OUT_OF_MEMORY : VG_ERROR_INVALID_ARGUMENT;
    }
    path->cmds[path->count].type = VG_CMD_LINE_TO;
    path->cmds[path->count].p[0] = p;
    path->count++;
    return VG_OK;
}

vg_result vg_path_cubic_to(vg_path* path, vg_vec2 c0, vg_vec2 c1, vg_vec2 p1) {
    if (!path || !vg_path_reserve(path, 1)) {
        return path ? VG_ERROR_OUT_OF_MEMORY : VG_ERROR_INVALID_ARGUMENT;
    }
    path->cmds[path->count].type = VG_CMD_CUBIC_TO;
    path->cmds[path->count].p[0] = c0;
    path->cmds[path->count].p[1] = c1;
    path->cmds[path->count].p[2] = p1;
    path->count++;
    return VG_OK;
}

vg_result vg_path_close(vg_path* path) {
    if (!path || !vg_path_reserve(path, 1)) {
        return path ? VG_ERROR_OUT_OF_MEMORY : VG_ERROR_INVALID_ARGUMENT;
    }
    path->cmds[path->count].type = VG_CMD_CLOSE;
    memset(path->cmds[path->count].p, 0, sizeof(path->cmds[path->count].p));
    path->count++;
    return VG_OK;
}

vg_result vg_draw_path_stroke(vg_context* ctx, const vg_path* path, const vg_stroke_style* style) {
    if (!ctx || !path || !style) {
        return VG_ERROR_INVALID_ARGUMENT;
    }
    if (!ctx->in_frame || path->owner != ctx || !vg_style_is_valid(style)) {
        return VG_ERROR_INVALID_ARGUMENT;
    }
    if (path->count == 0) {
        return VG_OK;
    }
    if (!ctx->backend.ops || !ctx->backend.ops->draw_path_stroke) {
        return VG_ERROR_UNSUPPORTED;
    }

    if (vg_mat_is_identity(ctx->transform)) {
        return ctx->backend.ops->draw_path_stroke(ctx, path, style);
    }

    vg_path tmp = {0};
    tmp.owner = ctx;
    tmp.count = path->count;
    tmp.cap = path->count;
    tmp.cmds = (vg_path_cmd*)malloc(sizeof(*tmp.cmds) * path->count);
    if (!tmp.cmds) {
        return VG_ERROR_OUT_OF_MEMORY;
    }
    for (size_t i = 0; i < path->count; ++i) {
        tmp.cmds[i] = path->cmds[i];
        switch (tmp.cmds[i].type) {
            case VG_CMD_MOVE_TO:
            case VG_CMD_LINE_TO:
                tmp.cmds[i].p[0] = vg_transform_point(ctx->transform, tmp.cmds[i].p[0]);
                break;
            case VG_CMD_CUBIC_TO:
                tmp.cmds[i].p[0] = vg_transform_point(ctx->transform, tmp.cmds[i].p[0]);
                tmp.cmds[i].p[1] = vg_transform_point(ctx->transform, tmp.cmds[i].p[1]);
                tmp.cmds[i].p[2] = vg_transform_point(ctx->transform, tmp.cmds[i].p[2]);
                break;
            case VG_CMD_CLOSE:
                break;
            default:
                break;
        }
    }
    vg_result r = ctx->backend.ops->draw_path_stroke(ctx, &tmp, style);
    free(tmp.cmds);
    return r;
}

vg_result vg_draw_polyline(vg_context* ctx, const vg_vec2* points, size_t count, const vg_stroke_style* style, int closed) {
    if (!ctx || !points || !style || count < 2) {
        return VG_ERROR_INVALID_ARGUMENT;
    }
    if (!ctx->in_frame || !vg_style_is_valid(style)) {
        return VG_ERROR_INVALID_ARGUMENT;
    }
    if (!ctx->backend.ops || !ctx->backend.ops->draw_polyline) {
        return VG_ERROR_UNSUPPORTED;
    }

    if (vg_mat_is_identity(ctx->transform)) {
        return ctx->backend.ops->draw_polyline(ctx, points, count, style, closed);
    }

    vg_vec2* transformed = (vg_vec2*)malloc(sizeof(*transformed) * count);
    if (!transformed) {
        return VG_ERROR_OUT_OF_MEMORY;
    }
    for (size_t i = 0; i < count; ++i) {
        transformed[i] = vg_transform_point(ctx->transform, points[i]);
    }
    vg_result r = ctx->backend.ops->draw_polyline(ctx, transformed, count, style, closed);
    free(transformed);
    return r;
}

vg_result vg_fill_convex(vg_context* ctx, const vg_vec2* points, size_t count, const vg_fill_style* style) {
    if (!ctx || !points || count < 3u || !style) {
        return VG_ERROR_INVALID_ARGUMENT;
    }
    if (!ctx->in_frame || !vg_fill_style_is_valid(style)) {
        return VG_ERROR_INVALID_ARGUMENT;
    }
    if (!ctx->backend.ops || !ctx->backend.ops->fill_convex) {
        return VG_ERROR_UNSUPPORTED;
    }
    if (vg_mat_is_identity(ctx->transform)) {
        return ctx->backend.ops->fill_convex(ctx, points, count, style);
    }

    vg_vec2* transformed = (vg_vec2*)malloc(sizeof(*transformed) * count);
    if (!transformed) {
        return VG_ERROR_OUT_OF_MEMORY;
    }
    for (size_t i = 0; i < count; ++i) {
        transformed[i] = vg_transform_point(ctx->transform, points[i]);
    }
    vg_result r = ctx->backend.ops->fill_convex(ctx, transformed, count, style);
    free(transformed);
    return r;
}

vg_result vg_fill_rect(vg_context* ctx, vg_rect rect, const vg_fill_style* style) {
    if (rect.w <= 0.0f || rect.h <= 0.0f) {
        return VG_ERROR_INVALID_ARGUMENT;
    }
    vg_vec2 p[4] = {
        {rect.x, rect.y},
        {rect.x + rect.w, rect.y},
        {rect.x + rect.w, rect.y + rect.h},
        {rect.x, rect.y + rect.h}
    };
    return vg_fill_convex(ctx, p, 4u, style);
}

vg_result vg_fill_circle(vg_context* ctx, vg_vec2 center, float radius_px, const vg_fill_style* style, int segments) {
    if (!isfinite(radius_px) || radius_px <= 0.0f || segments < 8 || segments > 512) {
        return VG_ERROR_INVALID_ARGUMENT;
    }
    vg_vec2* pts = (vg_vec2*)malloc(sizeof(*pts) * (size_t)segments);
    if (!pts) {
        return VG_ERROR_OUT_OF_MEMORY;
    }
    for (int i = 0; i < segments; ++i) {
        float a = ((float)i / (float)segments) * 2.0f * 3.14159265358979323846f;
        pts[i].x = center.x + cosf(a) * radius_px;
        pts[i].y = center.y + sinf(a) * radius_px;
    }
    vg_result r = vg_fill_convex(ctx, pts, (size_t)segments, style);
    free(pts);
    return r;
}

static float vg_measure_text_internal(const char* text, float size_px, float letter_spacing_px) {
    if (!text || !isfinite(size_px) || size_px <= 0.0f || !isfinite(letter_spacing_px)) {
        return 0.0f;
    }
    float adv = vg_text_advance(size_px, letter_spacing_px);
    float x = 0.0f;
    float max_x = 0.0f;
    for (const char* p = text; *p; ++p) {
        if (*p == '\n') {
            if (x > max_x) {
                max_x = x;
            }
            x = 0.0f;
            continue;
        }
        x += adv;
    }
    if (x > max_x) {
        max_x = x;
    }
    return max_x;
}

static vg_result vg_draw_text_internal(
    vg_context* ctx,
    const char* text,
    vg_vec2 origin,
    float size_px,
    float letter_spacing_px,
    const vg_stroke_style* style,
    const uint8_t* (*lookup)(char),
    float* out_width_px
) {
    if (!ctx || !text || !style || !lookup || !ctx->in_frame || !vg_style_is_valid(style) || !isfinite(size_px) || size_px <= 0.0f || !isfinite(letter_spacing_px)) {
        return VG_ERROR_INVALID_ARGUMENT;
    }

    float scale = size_px / 12.0f;
    float adv = vg_text_advance(size_px, letter_spacing_px);
    float line_h = size_px * 1.35f;
    float pen_x = origin.x;
    float pen_y = origin.y;
    float line_start_x = origin.x;
    float max_x = pen_x;

    for (const char* p = text; *p; ++p) {
        if (*p == '\n') {
            if (pen_x > max_x) {
                max_x = pen_x;
            }
            pen_x = line_start_x;
            pen_y += line_h;
            continue;
        }

        const uint8_t* glyph = lookup(*p);
        const vg_glyph_decoded* dec = vg_get_decoded_glyph(glyph);
        if (!dec) {
            return VG_ERROR_OUT_OF_MEMORY;
        }
        vg_vec2 run[64];
        for (uint8_t ri = 0u; ri < dec->run_count; ++ri) {
            uint8_t rs = dec->run_start[ri];
            uint8_t rl = dec->run_len[ri];
            for (uint8_t j = 0u; j < rl; ++j) {
                uint8_t pi = (uint8_t)(rs + j);
                run[j].x = pen_x + (float)dec->point_x[pi] * scale;
                run[j].y = pen_y + (float)dec->point_y[pi] * scale;
            }
            vg_result r = vg_draw_polyline(ctx, run, (size_t)rl, style, 0);
            if (r != VG_OK) {
                return r;
            }
        }

        pen_x += adv;
        if (pen_x > max_x) {
            max_x = pen_x;
        }
    }

    if (out_width_px) {
        *out_width_px = max_x - origin.x;
    }
    return VG_OK;
}

float vg_measure_text(const char* text, float size_px, float letter_spacing_px) {
    return vg_measure_text_internal(text, size_px, letter_spacing_px);
}

float vg_measure_text_boxed(const char* text, float size_px, float letter_spacing_px) {
    return vg_measure_text_internal(text, size_px, letter_spacing_px);
}

float vg_measure_text_wrapped(const char* text, float size_px, float letter_spacing_px, float wrap_width_px, size_t* out_line_count) {
    if (out_line_count) {
        *out_line_count = 0u;
    }
    if (!text || !isfinite(size_px) || size_px <= 0.0f || !isfinite(letter_spacing_px)) {
        return 0.0f;
    }
    if (!isfinite(wrap_width_px) || wrap_width_px <= 0.0f) {
        float w = vg_measure_text_internal(text, size_px, letter_spacing_px);
        if (out_line_count) {
            *out_line_count = 1u;
        }
        return w;
    }
    float adv = vg_text_advance(size_px, letter_spacing_px);
    float x = 0.0f;
    float max_x = 0.0f;
    size_t lines = 1u;
    for (const char* p = text; *p; ++p) {
        if (*p == '\n') {
            if (x > max_x) {
                max_x = x;
            }
            x = 0.0f;
            lines++;
            continue;
        }
        if (x > 0.0f && x + adv > wrap_width_px) {
            if (x > max_x) {
                max_x = x;
            }
            x = 0.0f;
            lines++;
        }
        x += adv;
    }
    if (x > max_x) {
        max_x = x;
    }
    if (out_line_count) {
        *out_line_count = lines;
    }
    return max_x;
}

vg_result vg_draw_text(
    vg_context* ctx,
    const char* text,
    vg_vec2 origin,
    float size_px,
    float letter_spacing_px,
    const vg_stroke_style* style,
    float* out_width_px
) {
    return vg_draw_text_internal(ctx, text, origin, size_px, letter_spacing_px, style, vg_lookup_glyph, out_width_px);
}

vg_result vg_draw_text_boxed_weighted(
    vg_context* ctx,
    const char* text,
    vg_vec2 origin,
    float size_px,
    float letter_spacing_px,
    const vg_stroke_style* style,
    float weight,
    float* out_width_px
);

vg_result vg_draw_text_boxed(
    vg_context* ctx,
    const char* text,
    vg_vec2 origin,
    float size_px,
    float letter_spacing_px,
    const vg_stroke_style* style,
    float* out_width_px
) {
    return vg_draw_text_boxed_weighted(ctx, text, origin, size_px, letter_spacing_px, style, 1.0f, out_width_px);
}

vg_result vg_draw_text_boxed_weighted(
    vg_context* ctx,
    const char* text,
    vg_vec2 origin,
    float size_px,
    float letter_spacing_px,
    const vg_stroke_style* style,
    float weight,
    float* out_width_px
) {
    if (!ctx || !text || !style || !ctx->in_frame || !vg_style_is_valid(style) || !isfinite(size_px) || size_px <= 0.0f || !isfinite(letter_spacing_px)) {
        return VG_ERROR_INVALID_ARGUMENT;
    }
    if (!isfinite(weight)) {
        return VG_ERROR_INVALID_ARGUMENT;
    }
    if (weight < 0.25f) {
        weight = 0.25f;
    }
    if (weight > 3.0f) {
        weight = 3.0f;
    }
    {
        float w01 = (weight - 0.25f) / (3.0f - 0.25f);
        if (w01 < 0.0f) {
            w01 = 0.0f;
        }
        if (w01 > 1.0f) {
            w01 = 1.0f;
        }
        weight = w01;
    }

    /* Embolden glyph structure itself: thick contour pass + inner cutout + crisp edge. */
    float px = size_px / 12.0f;
    float outer_w = style->width_px + (0.9f + 2.7f * weight) * px;
    float inner_w = outer_w - (0.55f + 1.4f * weight) * px;
    float edge_w = style->width_px + (0.10f + 0.55f * weight) * px;
    if (inner_w < 0.8f) {
        inner_w = 0.8f;
    }
    if (edge_w < style->width_px) {
        edge_w = style->width_px;
    }

    vg_stroke_style outer = *style;
    outer.width_px = outer_w;
    outer.cap = VG_LINE_CAP_BUTT;
    outer.join = VG_LINE_JOIN_MITER;
    outer.blend = VG_BLEND_ALPHA;
    outer.intensity = style->intensity * 1.08f;

    vg_stroke_style inner = outer;
    inner.width_px = inner_w;
    inner.color = (vg_color){0.0f, 0.0f, 0.0f, 1.0f};
    inner.intensity = 1.0f;
    inner.blend = VG_BLEND_ALPHA;

    vg_stroke_style edge = *style;
    edge.width_px = edge_w;
    edge.cap = VG_LINE_CAP_BUTT;
    edge.join = VG_LINE_JOIN_MITER;
    edge.blend = VG_BLEND_ALPHA;

    vg_result r = vg_draw_text_internal(ctx, text, origin, size_px, letter_spacing_px, &outer, vg_lookup_glyph_boxed, out_width_px);
    if (r != VG_OK) {
        return r;
    }
    r = vg_draw_text_internal(ctx, text, origin, size_px, letter_spacing_px, &inner, vg_lookup_glyph_boxed, NULL);
    if (r != VG_OK) {
        return r;
    }
    return vg_draw_text_internal(ctx, text, origin, size_px, letter_spacing_px, &edge, vg_lookup_glyph_boxed, NULL);
}

vg_result vg_draw_text_vector_fill(
    vg_context* ctx,
    const char* text,
    vg_vec2 origin,
    float size_px,
    float letter_spacing_px,
    const vg_stroke_style* style,
    float* out_width_px
) {
    if (!ctx || !text || !style || !ctx->in_frame || !vg_style_is_valid(style) || !isfinite(size_px) || size_px <= 0.0f || !isfinite(letter_spacing_px)) {
        return VG_ERROR_INVALID_ARGUMENT;
    }
    float px = size_px / 12.0f;

    vg_stroke_style body = *style;
    body.width_px = style->width_px + 2.2f * px;
    body.intensity = style->intensity * 0.52f;
    body.blend = VG_BLEND_ALPHA;

    vg_stroke_style fill = *style;
    fill.width_px = style->width_px + 1.35f * px;
    fill.intensity = style->intensity * 0.96f;
    fill.blend = VG_BLEND_ALPHA;

    vg_stroke_style edge = *style;
    edge.width_px = style->width_px + 0.20f * px;
    edge.intensity = style->intensity * 1.12f;
    edge.blend = style->blend;

    vg_result r = vg_draw_text_internal(ctx, text, origin, size_px, letter_spacing_px, &body, vg_lookup_glyph, out_width_px);
    if (r != VG_OK) {
        return r;
    }
    r = vg_draw_text_internal(ctx, text, origin, size_px, letter_spacing_px, &fill, vg_lookup_glyph, NULL);
    if (r != VG_OK) {
        return r;
    }
    return vg_draw_text_internal(ctx, text, origin, size_px, letter_spacing_px, &edge, vg_lookup_glyph, NULL);
}

vg_result vg_draw_text_stencil_cutout(
    vg_context* ctx,
    const char* text,
    vg_vec2 origin,
    float size_px,
    float letter_spacing_px,
    const vg_fill_style* panel_fill,
    const vg_stroke_style* panel_border,
    const vg_stroke_style* text_style,
    float* out_width_px
) {
    if (!ctx || !text || !panel_fill || !panel_border || !text_style || !ctx->in_frame ||
        !vg_fill_style_is_valid(panel_fill) || !vg_style_is_valid(panel_border) || !vg_style_is_valid(text_style) ||
        !isfinite(size_px) || size_px <= 0.0f || !isfinite(letter_spacing_px)) {
        return VG_ERROR_INVALID_ARGUMENT;
    }

    size_t lines = 1u;
    for (const char* p = text; *p; ++p) {
        if (*p == '\n') {
            lines++;
        }
    }
    float w = vg_measure_text_internal(text, size_px, letter_spacing_px);
    float line_h = size_px * 1.35f;
    float h = line_h * (float)lines;
    float pad_x = size_px * 0.40f;
    float pad_y = size_px * 0.34f;
    vg_rect panel = {
        origin.x - pad_x,
        origin.y - pad_y,
        w + 2.0f * pad_x,
        h + 2.0f * pad_y
    };

    vg_result r = vg_fill_rect(ctx, panel, panel_fill);
    if (r != VG_OK) {
        return r;
    }
    r = vg_draw_rect(ctx, panel, panel_border);
    if (r != VG_OK) {
        return r;
    }

    vg_stroke_style cut = *text_style;
    cut.color = (vg_color){0.0f, 0.0f, 0.0f, 1.0f};
    cut.intensity = 1.0f;
    cut.width_px = text_style->width_px + (size_px / 12.0f) * 1.15f;
    cut.cap = VG_LINE_CAP_ROUND;
    cut.join = VG_LINE_JOIN_ROUND;
    cut.blend = VG_BLEND_ALPHA;
    r = vg_draw_text_internal(ctx, text, origin, size_px, letter_spacing_px, &cut, vg_lookup_glyph, out_width_px);
    if (r != VG_OK) {
        return r;
    }

    vg_stroke_style cut_core = cut;
    cut_core.width_px = cut.width_px * 0.72f;
    r = vg_draw_text_internal(ctx, text, origin, size_px, letter_spacing_px, &cut_core, vg_lookup_glyph, NULL);
    if (r != VG_OK) {
        return r;
    }

    vg_stroke_style edge = *text_style;
    edge.width_px = text_style->width_px * 0.55f;
    if (edge.width_px < 0.6f) {
        edge.width_px = 0.6f;
    }
    edge.intensity = text_style->intensity * 0.55f;
    edge.color.a *= 0.65f;
    edge.cap = VG_LINE_CAP_ROUND;
    edge.join = VG_LINE_JOIN_ROUND;
    edge.blend = VG_BLEND_ALPHA;
    return vg_draw_text_internal(ctx, text, origin, size_px, letter_spacing_px, &edge, vg_lookup_glyph, NULL);
}

vg_result vg_draw_text_wrapped(
    vg_context* ctx,
    const char* text,
    vg_rect bounds,
    float size_px,
    float letter_spacing_px,
    vg_text_align align,
    const vg_stroke_style* style,
    float* out_height_px
) {
    if (!ctx || !text || !style || !ctx->in_frame || !vg_style_is_valid(style) || !isfinite(size_px) || size_px <= 0.0f || !isfinite(letter_spacing_px) || bounds.w <= 0.0f || bounds.h <= 0.0f) {
        return VG_ERROR_INVALID_ARGUMENT;
    }
    if (align < VG_TEXT_ALIGN_LEFT || align > VG_TEXT_ALIGN_RIGHT) {
        return VG_ERROR_INVALID_ARGUMENT;
    }

    float adv = vg_text_advance(size_px, letter_spacing_px);
    float line_h = size_px * 1.35f;
    float pen_y = bounds.y;
    float max_w = 0.0f;
    char line[1024];
    size_t li = 0u;
    float line_w = 0.0f;

    for (size_t i = 0;; ++i) {
        char c = text[i];
        int flush = 0;
        int force_break = 0;
        if (c == '\0' || c == '\n') {
            flush = 1;
            force_break = (c == '\n');
        } else if (line_w > 0.0f && line_w + adv > bounds.w) {
            flush = 1;
            force_break = 0;
            i--;
        }

        if (flush) {
            line[li] = '\0';
            if (pen_y + size_px <= bounds.y + bounds.h) {
                float x = bounds.x;
                if (align == VG_TEXT_ALIGN_CENTER) {
                    x = bounds.x + (bounds.w - line_w) * 0.5f;
                } else if (align == VG_TEXT_ALIGN_RIGHT) {
                    x = bounds.x + bounds.w - line_w;
                }
                vg_result r = vg_draw_text_internal(ctx, line, (vg_vec2){x, pen_y}, size_px, letter_spacing_px, style, vg_lookup_glyph, NULL);
                if (r != VG_OK) {
                    return r;
                }
            }
            if (line_w > max_w) {
                max_w = line_w;
            }
            li = 0u;
            line_w = 0.0f;
            pen_y += line_h;
            if (c == '\0') {
                break;
            }
            if (force_break) {
                continue;
            }
        } else if (li + 1u < sizeof(line)) {
            line[li++] = c;
            line_w += adv;
        }
    }

    if (out_height_px) {
        *out_height_px = pen_y - bounds.y;
    }
    (void)max_w;
    return VG_OK;
}

vg_result vg_draw_rect(vg_context* ctx, vg_rect rect, const vg_stroke_style* style) {
    if (rect.w <= 0.0f || rect.h <= 0.0f) {
        return VG_ERROR_INVALID_ARGUMENT;
    }
    vg_vec2 points[4] = {
        {rect.x, rect.y},
        {rect.x + rect.w, rect.y},
        {rect.x + rect.w, rect.y + rect.h},
        {rect.x, rect.y + rect.h}
    };
    return vg_draw_polyline(ctx, points, 4u, style, 1);
}

vg_result vg_draw_button(
    vg_context* ctx,
    vg_rect rect,
    const char* label,
    float label_size_px,
    const vg_stroke_style* border_style,
    const vg_stroke_style* text_style,
    int pressed
) {
    if (!ctx || !label || !border_style || !text_style || !isfinite(label_size_px) || label_size_px <= 0.0f) {
        return VG_ERROR_INVALID_ARGUMENT;
    }

    vg_result r = vg_draw_rect(ctx, rect, border_style);
    if (r != VG_OK) {
        return r;
    }

    if (pressed) {
        float inset = rect.h * 0.16f;
        vg_rect inner = {rect.x + inset, rect.y + inset, rect.w - 2.0f * inset, rect.h - 2.0f * inset};
        if (inner.w > 0.0f && inner.h > 0.0f) {
            r = vg_draw_rect(ctx, inner, border_style);
            if (r != VG_OK) {
                return r;
            }
        }
    }

    float text_w = vg_measure_text(label, label_size_px, label_size_px * 0.08f);
    vg_vec2 text_pos = {
        rect.x + (rect.w - text_w) * 0.5f,
        rect.y + (rect.h - label_size_px) * 0.5f
    };
    return vg_draw_text(ctx, label, text_pos, label_size_px, label_size_px * 0.08f, text_style, NULL);
}

vg_result vg_draw_slider(
    vg_context* ctx,
    vg_rect rect,
    float value_01,
    const vg_stroke_style* border_style,
    const vg_stroke_style* track_style,
    const vg_stroke_style* knob_style
) {
    if (!ctx || !border_style || !track_style || !knob_style || !isfinite(value_01)) {
        return VG_ERROR_INVALID_ARGUMENT;
    }
    if (value_01 < 0.0f) {
        value_01 = 0.0f;
    }
    if (value_01 > 1.0f) {
        value_01 = 1.0f;
    }

    vg_result r = vg_draw_rect(ctx, rect, border_style);
    if (r != VG_OK) {
        return r;
    }

    float track_pad = rect.h * 0.35f;
    float track_y = rect.y + rect.h * 0.5f;
    vg_vec2 track[2] = {
        {rect.x + track_pad, track_y},
        {rect.x + rect.w - track_pad, track_y}
    };
    r = vg_draw_polyline(ctx, track, 2u, track_style, 0);
    if (r != VG_OK) {
        return r;
    }

    float knob_w = rect.h * 0.52f;
    float knob_h = rect.h * 0.74f;
    float track_span = (track[1].x - track[0].x);
    float knob_center = track[0].x + track_span * value_01;
    vg_rect knob = {
        knob_center - knob_w * 0.5f,
        rect.y + (rect.h - knob_h) * 0.5f,
        knob_w,
        knob_h
    };
    return vg_draw_rect(ctx, knob, knob_style);
}

vg_result vg_debug_rasterize_rgba8(
    vg_context* ctx,
    uint8_t* pixels,
    uint32_t width,
    uint32_t height,
    uint32_t stride_bytes
) {
    if (!ctx || !pixels || width == 0 || height == 0 || stride_bytes < width * 4u) {
        return VG_ERROR_INVALID_ARGUMENT;
    }
    if (!ctx->in_frame) {
        return VG_ERROR_INVALID_ARGUMENT;
    }
    if (!ctx->backend.ops || !ctx->backend.ops->debug_rasterize_rgba8) {
        return VG_ERROR_UNSUPPORTED;
    }
    return ctx->backend.ops->debug_rasterize_rgba8(ctx, pixels, width, height, stride_bytes);
}

const char* vg_result_string(vg_result result) {
    switch (result) {
        case VG_OK:
            return "VG_OK";
        case VG_ERROR_INVALID_ARGUMENT:
            return "VG_ERROR_INVALID_ARGUMENT";
        case VG_ERROR_OUT_OF_MEMORY:
            return "VG_ERROR_OUT_OF_MEMORY";
        case VG_ERROR_BACKEND:
            return "VG_ERROR_BACKEND";
        case VG_ERROR_UNSUPPORTED:
            return "VG_ERROR_UNSUPPORTED";
        default:
            return "VG_ERROR_UNKNOWN";
    }
}
