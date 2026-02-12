#include "vg_pointer.h"

#include <math.h>
#include <stdlib.h>

static vg_vec2 vg_ptr_rotate(vg_vec2 p, float a) {
    float c = cosf(a);
    float s = sinf(a);
    vg_vec2 out = {p.x * c - p.y * s, p.x * s + p.y * c};
    return out;
}

static vg_vec2 vg_ptr_transform(vg_vec2 p, vg_vec2 center, float scale, float angle) {
    vg_vec2 r = vg_ptr_rotate(p, angle);
    r.x = center.x + r.x * scale;
    r.y = center.y + r.y * scale;
    return r;
}

static vg_result vg_ptr_draw_asteroids(vg_context* ctx, const vg_pointer_desc* d) {
    float s = d->size_px > 0.0f ? d->size_px : 18.0f;
    float a = d->angle_rad;
    float base = 20.0f;
    float scale = s / base;

    /* User-provided two-triangle shape in right-facing local space:
       {-4,0, 12,0, -8,8,  -4,0, -8,-8, 12,0} */
    /* Anchor at tip (12,0) so pointer position equals selectable hotspot. */
    vg_vec2 tri_outer_local[3] = {
        {-16.0f, 0.0f},
        {0.0f, 0.0f},
        {-20.0f, 8.0f}
    };
    vg_vec2 tri_inner_local[3] = {
        {-16.0f, 0.0f},
        {-20.0f, -8.0f},
        {0.0f, 0.0f}
    };
    vg_vec2 tri_outer[3];
    vg_vec2 tri_inner[3];
    for (int i = 0; i < 3; ++i) {
        tri_outer[i] = vg_ptr_transform(tri_outer_local[i], d->position, scale, a);
        tri_inner[i] = vg_ptr_transform(tri_inner_local[i], d->position, scale, a);
    }
    if (d->use_fill) {
        vg_result r = vg_fill_convex(ctx, tri_outer, 3u, &d->fill);
        if (r != VG_OK) {
            return r;
        }
        vg_fill_style hi_fill = d->fill;
        hi_fill.intensity *= 1.15f;
        hi_fill.color.r = fminf(1.0f, d->fill.color.r + 0.20f);
        hi_fill.color.g = fminf(1.0f, d->fill.color.g + 0.15f);
        hi_fill.color.b = fminf(1.0f, d->fill.color.b + 0.15f);
        hi_fill.color.a *= 0.95f;
        r = vg_fill_convex(ctx, tri_inner, 3u, &hi_fill);
        if (r != VG_OK) {
            return r;
        }
    }

    vg_result r = vg_draw_polyline(ctx, tri_outer, 3u, &d->stroke, 1);
    if (r != VG_OK) {
        return r;
    }
    return vg_draw_polyline(ctx, tri_inner, 3u, &d->stroke, 1);
}

static vg_result vg_ptr_draw_crosshair(vg_context* ctx, const vg_pointer_desc* d) {
    float s = d->size_px > 0.0f ? d->size_px : 16.0f;
    float r0 = s * 0.34f;
    float r1 = s * 0.92f;
    float gap = s * 0.18f;
    float pulse = 1.0f + 0.08f * sinf(d->phase * 2.3f);
    int segs = 26;

    vg_vec2 ring[26];
    for (int i = 0; i < segs; ++i) {
        float t = (float)i / (float)segs;
        float a = t * 6.28318530718f + d->angle_rad;
        ring[i].x = d->position.x + cosf(a) * r0 * pulse;
        ring[i].y = d->position.y + sinf(a) * r0 * pulse;
    }
    vg_result r = vg_draw_polyline(ctx, ring, (size_t)segs, &d->stroke, 1);
    if (r != VG_OK) {
        return r;
    }

    vg_vec2 h1[2] = {{d->position.x - r1, d->position.y}, {d->position.x - gap, d->position.y}};
    vg_vec2 h2[2] = {{d->position.x + gap, d->position.y}, {d->position.x + r1, d->position.y}};
    vg_vec2 v1[2] = {{d->position.x, d->position.y - r1}, {d->position.x, d->position.y - gap}};
    vg_vec2 v2[2] = {{d->position.x, d->position.y + gap}, {d->position.x, d->position.y + r1}};
    r = vg_draw_polyline(ctx, h1, 2u, &d->stroke, 0);
    if (r != VG_OK) return r;
    r = vg_draw_polyline(ctx, h2, 2u, &d->stroke, 0);
    if (r != VG_OK) return r;
    r = vg_draw_polyline(ctx, v1, 2u, &d->stroke, 0);
    if (r != VG_OK) return r;
    r = vg_draw_polyline(ctx, v2, 2u, &d->stroke, 0);
    if (r != VG_OK) return r;

    if (d->use_fill) {
        return vg_fill_circle(ctx, d->position, s * 0.08f, &d->fill, 12);
    }
    return VG_OK;
}

vg_result vg_draw_pointer(vg_context* ctx, vg_pointer_style style, const vg_pointer_desc* desc) {
    if (!ctx || !desc || desc->size_px <= 0.0f) {
        return VG_ERROR_INVALID_ARGUMENT;
    }
    if (style == VG_POINTER_NONE) {
        return VG_OK;
    }
    if (style == VG_POINTER_ASTEROIDS) {
        return vg_ptr_draw_asteroids(ctx, desc);
    }
    if (style == VG_POINTER_CROSSHAIR) {
        return vg_ptr_draw_crosshair(ctx, desc);
    }
    return VG_ERROR_UNSUPPORTED;
}
