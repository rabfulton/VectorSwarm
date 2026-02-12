#include "vg_svg.h"
#include "vg_palette.h"

#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define NANOSVG_IMPLEMENTATION
#include "nanosvg.h"
#include "tesselator.h"

typedef struct svg_polyline {
    vg_vec2* points;
    uint32_t count;
    int closed;
    int has_fill;
    int has_stroke;
    float stroke_width;
    vg_color fill_color;
    vg_color stroke_color;
} svg_polyline;

typedef struct svg_fill_mesh {
    vg_vec2* tris;
    uint32_t tri_count;
    vg_color fill_color;
} svg_fill_mesh;

struct vg_svg_asset {
    svg_polyline* polylines;
    uint32_t polyline_count;
    uint32_t polyline_capacity;
    uint32_t max_points_per_polyline;
    svg_fill_mesh* fills;
    uint32_t fill_count;
    uint32_t fill_capacity;
    vg_rect bounds;
};

typedef struct point_builder {
    vg_vec2* points;
    uint32_t count;
    uint32_t capacity;
} point_builder;

static vg_result svg_push_point(point_builder* pb, float x, float y) {
    if (!pb) {
        return VG_ERROR_INVALID_ARGUMENT;
    }
    if (pb->count == pb->capacity) {
        uint32_t next_cap = pb->capacity ? pb->capacity * 2u : 64u;
        vg_vec2* next = (vg_vec2*)realloc(pb->points, (size_t)next_cap * sizeof(vg_vec2));
        if (!next) {
            return VG_ERROR_OUT_OF_MEMORY;
        }
        pb->points = next;
        pb->capacity = next_cap;
    }
    pb->points[pb->count].x = x;
    pb->points[pb->count].y = y;
    pb->count++;
    return VG_OK;
}

static vg_vec2 svg_cubic_eval(vg_vec2 p0, vg_vec2 p1, vg_vec2 p2, vg_vec2 p3, float t) {
    float it = 1.0f - t;
    float b0 = it * it * it;
    float b1 = 3.0f * it * it * t;
    float b2 = 3.0f * it * t * t;
    float b3 = t * t * t;
    vg_vec2 out = {
        p0.x * b0 + p1.x * b1 + p2.x * b2 + p3.x * b3,
        p0.y * b0 + p1.y * b1 + p2.y * b2 + p3.y * b3
    };
    return out;
}

static float svg_len(vg_vec2 a, vg_vec2 b) {
    float dx = b.x - a.x;
    float dy = b.y - a.y;
    return sqrtf(dx * dx + dy * dy);
}

static vg_result svg_push_polyline(vg_svg_asset* asset, point_builder* pb, int closed) {
    if (!asset || !pb || pb->count < 2u) {
        return VG_ERROR_INVALID_ARGUMENT;
    }
    if (asset->polyline_count == asset->polyline_capacity) {
        uint32_t next_cap = asset->polyline_capacity ? asset->polyline_capacity * 2u : 32u;
        svg_polyline* next = (svg_polyline*)realloc(asset->polylines, (size_t)next_cap * sizeof(svg_polyline));
        if (!next) {
            return VG_ERROR_OUT_OF_MEMORY;
        }
        asset->polylines = next;
        asset->polyline_capacity = next_cap;
    }
    svg_polyline* pl = &asset->polylines[asset->polyline_count++];
    pl->points = pb->points;
    pl->count = pb->count;
    pl->closed = closed;
    if (pl->count > asset->max_points_per_polyline) {
        asset->max_points_per_polyline = pl->count;
    }
    pb->points = NULL;
    pb->count = 0u;
    pb->capacity = 0u;
    return VG_OK;
}

static vg_color svg_color_from_u32(uint32_t c) {
    float r = (float)(c & 0xffu) / 255.0f;
    float g = (float)((c >> 8) & 0xffu) / 255.0f;
    float b = (float)((c >> 16) & 0xffu) / 255.0f;
    float a = (float)((c >> 24) & 0xffu) / 255.0f;
    vg_color out = {r, g, b, a};
    return out;
}

static float svg_clampf(float v, float lo, float hi) {
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

static vg_color svg_quantize_palette(vg_color c, const vg_color* pal, uint32_t n) {
    if (!pal || n == 0u) {
        return c;
    }
    uint32_t best_i = 0u;
    float best_d = 1e30f;
    for (uint32_t i = 0; i < n; ++i) {
        float dr = c.r - pal[i].r;
        float dg = c.g - pal[i].g;
        float db = c.b - pal[i].b;
        float d = dr * dr + dg * dg + db * db;
        if (d < best_d) {
            best_d = d;
            best_i = i;
        }
    }
    vg_color out = pal[best_i];
    out.a = c.a;
    return out;
}

static int svg_effective_count(const vg_vec2* pts, uint32_t n) {
    if (!pts || n < 2u) {
        return (int)n;
    }
    float dx = pts[n - 1u].x - pts[0].x;
    float dy = pts[n - 1u].y - pts[0].y;
    float d2 = dx * dx + dy * dy;
    if (d2 < 1e-10f) {
        return (int)(n - 1u);
    }
    return (int)n;
}

static void svg_asset_free(vg_svg_asset* a) {
    if (!a) {
        return;
    }
    for (uint32_t i = 0; i < a->polyline_count; ++i) {
        free(a->polylines[i].points);
    }
    for (uint32_t i = 0; i < a->fill_count; ++i) {
        free(a->fills[i].tris);
    }
    free(a->fills);
    free(a->polylines);
    free(a);
}

static int svg_build_flattened_contour(const NSVGpath* path, float tol, vg_vec2** out_pts, int* out_count) {
    if (!path || path->npts < 4 || !out_pts || !out_count) {
        return 0;
    }
    *out_pts = NULL;
    *out_count = 0;
    point_builder pb = {0};
    int seg_count = (path->npts - 1) / 3;
    for (int seg = 0; seg < seg_count; ++seg) {
        int i = seg * 3;
        vg_vec2 p0 = {path->pts[(size_t)(i + 0) * 2u + 0u], path->pts[(size_t)(i + 0) * 2u + 1u]};
        vg_vec2 p1 = {path->pts[(size_t)(i + 1) * 2u + 0u], path->pts[(size_t)(i + 1) * 2u + 1u]};
        vg_vec2 p2 = {path->pts[(size_t)(i + 2) * 2u + 0u], path->pts[(size_t)(i + 2) * 2u + 1u]};
        vg_vec2 p3 = {path->pts[(size_t)(i + 3) * 2u + 0u], path->pts[(size_t)(i + 3) * 2u + 1u]};
        if (seg == 0) {
            if (svg_push_point(&pb, p0.x, p0.y) != VG_OK) {
                free(pb.points);
                return 0;
            }
        }
        float approx = svg_len(p0, p1) + svg_len(p1, p2) + svg_len(p2, p3);
        int steps = (int)ceilf(approx / tol);
        if (steps < 1) {
            steps = 1;
        }
        if (steps > 64) {
            steps = 64;
        }
        for (int s = 1; s <= steps; ++s) {
            float t = (float)s / (float)steps;
            vg_vec2 pt = svg_cubic_eval(p0, p1, p2, p3, t);
            if (svg_push_point(&pb, pt.x, pt.y) != VG_OK) {
                free(pb.points);
                return 0;
            }
        }
    }
    int eff = svg_effective_count(pb.points, pb.count);
    if (eff < 3) {
        free(pb.points);
        return 0;
    }
    if (eff != (int)pb.count) {
        pb.count = (uint32_t)eff;
    }
    *out_pts = pb.points;
    *out_count = (int)pb.count;
    return 1;
}

static vg_result svg_push_fill_mesh(vg_svg_asset* asset, const NSVGshape* shape, float tol) {
    if (!asset || !shape || shape->fill.type != NSVG_PAINT_COLOR || shape->opacity <= 0.0f) {
        return VG_OK;
    }
    TESStesselator* tess = tessNewTess(NULL);
    if (!tess) {
        return VG_ERROR_OUT_OF_MEMORY;
    }

    int contour_count = 0;
    for (const NSVGpath* path = shape->paths; path; path = path->next) {
        vg_vec2* pts = NULL;
        int count = 0;
        if (!svg_build_flattened_contour(path, tol, &pts, &count)) {
            continue;
        }
        tessAddContour(tess, 2, pts, (int)sizeof(vg_vec2), count);
        contour_count++;
        free(pts);
    }
    if (contour_count == 0) {
        tessDeleteTess(tess);
        return VG_OK;
    }

    int winding = (shape->fillRule == NSVG_FILLRULE_NONZERO) ? TESS_WINDING_NONZERO : TESS_WINDING_ODD;
    if (!tessTesselate(tess, winding, TESS_POLYGONS, 3, 2, NULL)) {
        tessDeleteTess(tess);
        return VG_OK;
    }

    int nelems = tessGetElementCount(tess);
    const TESSreal* verts = tessGetVertices(tess);
    const TESSindex* elems = tessGetElements(tess);
    if (!verts || !elems || nelems <= 0) {
        tessDeleteTess(tess);
        return VG_OK;
    }

    vg_vec2* tris = (vg_vec2*)malloc((size_t)nelems * 3u * sizeof(vg_vec2));
    if (!tris) {
        tessDeleteTess(tess);
        return VG_ERROR_OUT_OF_MEMORY;
    }
    uint32_t tri_n = 0u;
    for (int i = 0; i < nelems; ++i) {
        const TESSindex i0 = elems[i * 3 + 0];
        const TESSindex i1 = elems[i * 3 + 1];
        const TESSindex i2 = elems[i * 3 + 2];
        if (i0 == TESS_UNDEF || i1 == TESS_UNDEF || i2 == TESS_UNDEF) {
            continue;
        }
        tris[tri_n * 3u + 0u] = (vg_vec2){verts[i0 * 2 + 0], verts[i0 * 2 + 1]};
        tris[tri_n * 3u + 1u] = (vg_vec2){verts[i1 * 2 + 0], verts[i1 * 2 + 1]};
        tris[tri_n * 3u + 2u] = (vg_vec2){verts[i2 * 2 + 0], verts[i2 * 2 + 1]};
        tri_n++;
    }
    tessDeleteTess(tess);
    if (tri_n == 0u) {
        free(tris);
        return VG_OK;
    }

    if (asset->fill_count == asset->fill_capacity) {
        uint32_t next_cap = asset->fill_capacity ? asset->fill_capacity * 2u : 16u;
        svg_fill_mesh* next = (svg_fill_mesh*)realloc(asset->fills, (size_t)next_cap * sizeof(svg_fill_mesh));
        if (!next) {
            free(tris);
            return VG_ERROR_OUT_OF_MEMORY;
        }
        asset->fills = next;
        asset->fill_capacity = next_cap;
    }
    svg_fill_mesh* fm = &asset->fills[asset->fill_count++];
    fm->tris = tris;
    fm->tri_count = tri_n;
    fm->fill_color = svg_color_from_u32(shape->fill.color);
    fm->fill_color.a *= shape->opacity;
    return VG_OK;
}

vg_result vg_svg_load_from_file(
    const char* file_path,
    const vg_svg_load_params* params,
    vg_svg_asset** out_asset
) {
    if (!file_path || !out_asset) {
        return VG_ERROR_INVALID_ARGUMENT;
    }
    *out_asset = NULL;

    float tol = 1.2f;
    float dpi = 96.0f;
    const char* units = "px";
    if (params) {
        if (params->curve_tolerance_px > 0.0f) {
            tol = params->curve_tolerance_px;
        }
        if (params->dpi > 0.0f) {
            dpi = params->dpi;
        }
        if (params->units && params->units[0] != '\0') {
            units = params->units;
        }
    }
    if (tol < 0.2f) {
        tol = 0.2f;
    }

    NSVGimage* image = nsvgParseFromFile(file_path, units, dpi);
    if (!image) {
        return VG_ERROR_UNSUPPORTED;
    }

    vg_svg_asset* asset = (vg_svg_asset*)calloc(1, sizeof(*asset));
    if (!asset) {
        nsvgDelete(image);
        return VG_ERROR_OUT_OF_MEMORY;
    }

    float min_x = 1e30f;
    float min_y = 1e30f;
    float max_x = -1e30f;
    float max_y = -1e30f;

    for (const NSVGshape* shape = image->shapes; shape; shape = shape->next) {
        if ((shape->flags & NSVG_FLAGS_VISIBLE) == 0) {
            continue;
        }
        if (shape->bounds[0] < min_x) min_x = shape->bounds[0];
        if (shape->bounds[1] < min_y) min_y = shape->bounds[1];
        if (shape->bounds[2] > max_x) max_x = shape->bounds[2];
        if (shape->bounds[3] > max_y) max_y = shape->bounds[3];

        vg_result fr = svg_push_fill_mesh(asset, shape, tol);
        if (fr != VG_OK) {
            nsvgDelete(image);
            svg_asset_free(asset);
            return fr;
        }

        for (const NSVGpath* path = shape->paths; path; path = path->next) {
            if (path->npts < 4) {
                continue;
            }
            point_builder pb = {0};
            int seg_count = (path->npts - 1) / 3;
            for (int seg = 0; seg < seg_count; ++seg) {
                int i = seg * 3;
                vg_vec2 p0 = {path->pts[(size_t)(i + 0) * 2u + 0u], path->pts[(size_t)(i + 0) * 2u + 1u]};
                vg_vec2 p1 = {path->pts[(size_t)(i + 1) * 2u + 0u], path->pts[(size_t)(i + 1) * 2u + 1u]};
                vg_vec2 p2 = {path->pts[(size_t)(i + 2) * 2u + 0u], path->pts[(size_t)(i + 2) * 2u + 1u]};
                vg_vec2 p3 = {path->pts[(size_t)(i + 3) * 2u + 0u], path->pts[(size_t)(i + 3) * 2u + 1u]};

                if (seg == 0) {
                    vg_result pr = svg_push_point(&pb, p0.x, p0.y);
                    if (pr != VG_OK) {
                        free(pb.points);
                        nsvgDelete(image);
                        svg_asset_free(asset);
                        return pr;
                    }
                }

                float approx = svg_len(p0, p1) + svg_len(p1, p2) + svg_len(p2, p3);
                int steps = (int)ceilf(approx / tol);
                if (steps < 1) {
                    steps = 1;
                }
                if (steps > 64) {
                    steps = 64;
                }
                for (int s = 1; s <= steps; ++s) {
                    float t = (float)s / (float)steps;
                    vg_vec2 pt = svg_cubic_eval(p0, p1, p2, p3, t);
                    vg_result pr = svg_push_point(&pb, pt.x, pt.y);
                    if (pr != VG_OK) {
                        free(pb.points);
                        nsvgDelete(image);
                        svg_asset_free(asset);
                        return pr;
                    }
                }
            }
            if (pb.count >= 2u) {
                vg_result pr = svg_push_polyline(asset, &pb, path->closed ? 1 : 0);
                if (pr != VG_OK) {
                    free(pb.points);
                    nsvgDelete(image);
                    svg_asset_free(asset);
                    return pr;
                }
                svg_polyline* pl = &asset->polylines[asset->polyline_count - 1u];
                pl->has_fill = (shape->fill.type == NSVG_PAINT_COLOR);
                pl->has_stroke = (shape->stroke.type == NSVG_PAINT_COLOR) && (shape->strokeWidth > 0.0f);
                pl->stroke_width = shape->strokeWidth;
                pl->fill_color = svg_color_from_u32(shape->fill.color);
                pl->stroke_color = svg_color_from_u32(shape->stroke.color);
                pl->fill_color.a *= shape->opacity;
                pl->stroke_color.a *= shape->opacity;
            } else {
                free(pb.points);
            }
        }
    }

    nsvgDelete(image);

    if (asset->polyline_count == 0u) {
        svg_asset_free(asset);
        return VG_ERROR_UNSUPPORTED;
    }

    if (!(max_x > min_x) || !(max_y > min_y)) {
        min_x = 0.0f;
        min_y = 0.0f;
        max_x = 1.0f;
        max_y = 1.0f;
    }
    asset->bounds = (vg_rect){min_x, min_y, max_x - min_x, max_y - min_y};

    *out_asset = asset;
    return VG_OK;
}

void vg_svg_destroy(vg_svg_asset* asset) {
    svg_asset_free(asset);
}

vg_result vg_svg_get_bounds(const vg_svg_asset* asset, vg_rect* out_bounds) {
    if (!asset || !out_bounds) {
        return VG_ERROR_INVALID_ARGUMENT;
    }
    *out_bounds = asset->bounds;
    return VG_OK;
}

vg_result vg_svg_draw(
    vg_context* ctx,
    const vg_svg_asset* asset,
    const vg_svg_draw_params* params,
    const vg_stroke_style* style
) {
    if (!ctx || !asset || !params) {
        return VG_ERROR_INVALID_ARGUMENT;
    }
    if (params->dst.w <= 0.0f || params->dst.h <= 0.0f || asset->bounds.w <= 0.0f || asset->bounds.h <= 0.0f) {
        return VG_ERROR_INVALID_ARGUMENT;
    }

    vg_stroke_style stroke = {
        .width_px = 1.6f,
        .intensity = 1.0f,
        .color = {0.18f, 1.0f, 0.40f, 1.0f},
        .cap = VG_LINE_CAP_ROUND,
        .join = VG_LINE_JOIN_ROUND,
        .miter_limit = 4.0f,
        .blend = VG_BLEND_ADDITIVE
    };
    if (style) {
        stroke = *style;
    }
    float fill_intensity = params->fill_intensity > 0.0f ? params->fill_intensity : 1.0f;
    float stroke_intensity = params->stroke_intensity > 0.0f ? params->stroke_intensity : 1.0f;
    const vg_color* pal = params->palette;
    uint32_t pal_count = params->palette_count;
    vg_palette ctx_pal;
    if ((!pal || pal_count == 0u) && params->use_context_palette) {
        vg_get_palette(ctx, &ctx_pal);
        if (ctx_pal.count > 0u) {
            pal = &ctx_pal.entries[0].color;
            pal_count = ctx_pal.count;
        }
    }

    float sx = params->dst.w / asset->bounds.w;
    float sy = params->dst.h / asset->bounds.h;
    if (params->preserve_aspect) {
        float s = sx < sy ? sx : sy;
        sx = s;
        sy = s;
    }
    float draw_w = asset->bounds.w * sx;
    float draw_h = asset->bounds.h * sy;
    float off_x = params->dst.x + (params->dst.w - draw_w) * 0.5f;
    float off_y = params->dst.y + (params->dst.h - draw_h) * 0.5f;

    vg_vec2* tmp = (vg_vec2*)malloc((size_t)asset->max_points_per_polyline * sizeof(vg_vec2));
    if (!tmp) {
        return VG_ERROR_OUT_OF_MEMORY;
    }

    if (params->fill_closed_paths) {
        for (uint32_t i = 0; i < asset->fill_count; ++i) {
            const svg_fill_mesh* fm = &asset->fills[i];
            vg_fill_style fill = {
                .intensity = fill_intensity,
                .color = stroke.color,
                .blend = stroke.blend
            };
            if (params->use_source_colors) {
                fill.color = fm->fill_color;
                fill.blend = VG_BLEND_ALPHA;
            }
            fill.color = svg_quantize_palette(fill.color, pal, pal_count);
            fill.color.a = svg_clampf(fill.color.a, 0.0f, 1.0f);
            for (uint32_t t = 0; t < fm->tri_count; ++t) {
                vg_vec2 tri[3];
                for (int k = 0; k < 3; ++k) {
                    vg_vec2 p = fm->tris[t * 3u + (uint32_t)k];
                    float nx = (p.x - asset->bounds.x) * sx;
                    float ny = (p.y - asset->bounds.y) * sy;
                    tri[k].x = off_x + nx;
                    tri[k].y = params->flip_y ? (off_y + (draw_h - ny)) : (off_y + ny);
                }
                vg_result r = vg_fill_convex(ctx, tri, 3u, &fill);
                if (r != VG_OK) {
                    free(tmp);
                    return r;
                }
            }
        }
    }

    for (uint32_t i = 0; i < asset->polyline_count; ++i) {
        const svg_polyline* pl = &asset->polylines[i];
        for (uint32_t p = 0; p < pl->count; ++p) {
            float nx = (pl->points[p].x - asset->bounds.x) * sx;
            float ny = (pl->points[p].y - asset->bounds.y) * sy;
            tmp[p].x = off_x + nx;
            if (params->flip_y) {
                tmp[p].y = off_y + (draw_h - ny);
            } else {
                tmp[p].y = off_y + ny;
            }
        }
        if (!params->use_source_colors || pl->has_stroke || pl->has_fill) {
            vg_stroke_style draw_s = stroke;
            if (params->use_source_colors && pl->has_stroke) {
                draw_s.color = pl->stroke_color;
                draw_s.color.a = svg_clampf(draw_s.color.a, 0.0f, 1.0f);
                if (pl->stroke_width > 0.0f) {
                    draw_s.width_px = pl->stroke_width * ((sx + sy) * 0.5f);
                }
                draw_s.blend = VG_BLEND_ALPHA;
            } else if (params->use_source_colors && pl->has_fill) {
                /* Fallback for fill-only SVG shapes: outline using fill color so geometry stays visible. */
                draw_s.color = pl->fill_color;
                draw_s.color.a = svg_clampf(draw_s.color.a, 0.0f, 1.0f);
                draw_s.width_px = svg_clampf(stroke.width_px * 0.9f, 0.6f, 6.0f);
                draw_s.blend = VG_BLEND_ALPHA;
            }
            draw_s.color = svg_quantize_palette(draw_s.color, pal, pal_count);
            draw_s.intensity *= stroke_intensity;
            draw_s.width_px = svg_clampf(draw_s.width_px, 0.3f, 24.0f);
            vg_result r = vg_draw_polyline(ctx, tmp, pl->count, &draw_s, pl->closed);
            if (r != VG_OK) {
                free(tmp);
                return r;
            }
        }
    }

    free(tmp);
    return VG_OK;
}
