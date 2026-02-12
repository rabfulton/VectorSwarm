#include "vg_image.h"
#include "vg_palette.h"

#include <math.h>
#include <stdlib.h>

static float vg_image_clampf(float v, float lo, float hi) {
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

static float vg_image_luma(const uint8_t* px) {
    float r = (float)px[0] / 255.0f;
    float g = (float)px[1] / 255.0f;
    float b = (float)px[2] / 255.0f;
    return r * 0.299f + g * 0.587f + b * 0.114f;
}

static float vg_image_apply_contrast(float v, float c) {
    return vg_image_clampf((v - 0.5f) * c + 0.5f, 0.0f, 1.0f);
}

vg_result vg_draw_image_stylized(
    vg_context* ctx,
    const vg_image_desc* src,
    vg_rect dst,
    const vg_image_style* style
) {
    if (!ctx || !src || !style || !src->pixels_rgba8 || src->width == 0u || src->height == 0u ||
        src->stride_bytes < src->width * 4u || dst.w <= 1.0f || dst.h <= 1.0f) {
        return VG_ERROR_INVALID_ARGUMENT;
    }
    if (style->kind != VG_IMAGE_STYLE_MONO_SCANLINE) {
        if (style->kind != VG_IMAGE_STYLE_BLOCK_GRAPHICS) {
            return VG_ERROR_UNSUPPORTED;
        }
    }

    vg_color col = style->tint_color;
    if (style->use_context_palette) {
        vg_palette pal;
        vg_get_palette(ctx, &pal);
        uint32_t idx = 0u;
        if (pal.count > 0u) {
            if (style->palette_index >= 0) {
                idx = (uint32_t)style->palette_index % pal.count;
            }
            col = pal.entries[idx].color;
            col.a = 1.0f;
        }
    } else if (style->use_crt_palette) {
        vg_crt_profile crt;
        vg_get_crt_profile(ctx, &crt);
        float glow = vg_image_clampf(crt.beam_intensity / 2.0f, 0.0f, 1.0f);
        col.r = 0.16f + glow * 0.20f;
        col.g = 0.84f + glow * 0.16f;
        col.b = 0.26f + glow * 0.24f;
        col.a = 1.0f;
    }

    float contrast = style->contrast > 0.0f ? style->contrast : 1.0f;
    float threshold = vg_image_clampf(style->threshold, 0.0f, 1.0f);
    float min_w = vg_image_clampf(style->min_line_width_px, 0.2f, 16.0f);
    float max_w = vg_image_clampf(style->max_line_width_px, min_w, 20.0f);
    if (style->kind == VG_IMAGE_STYLE_MONO_SCANLINE) {
        float pitch = vg_image_clampf(style->scanline_pitch_px, 1.0f, 16.0f);
        int row_count = (int)(dst.h / pitch);
        if (row_count < 2) {
            row_count = 2;
        }
        int col_count = (int)(dst.w / 2.0f);
        if (col_count < 32) {
            col_count = 32;
        }
        if (col_count > (int)src->width) {
            col_count = (int)src->width;
        }
        float x_step = dst.w / (float)col_count;

        vg_fill_style fs = {
            .intensity = style->intensity > 0.0f ? style->intensity : 1.0f,
            .color = col,
            .blend = style->blend
        };

        for (int ry = 0; ry < row_count; ++ry) {
            float y_u = ((float)ry + 0.5f) / (float)row_count;
            uint32_t sy = (uint32_t)(y_u * (float)(src->height - 1u));
            const uint8_t* row = src->pixels_rgba8 + (size_t)sy * src->stride_bytes;

            int run_start = -1;
            float run_sum = 0.0f;
            int run_count = 0;

            for (int cx = 0; cx <= col_count; ++cx) {
                float lum = 0.0f;
                int lit = 0;
                if (cx < col_count) {
                    float x_u = ((float)cx + 0.5f) / (float)col_count;
                    uint32_t sx = (uint32_t)(x_u * (float)(src->width - 1u));
                    lum = vg_image_luma(row + (size_t)sx * 4u);
                    if (style->invert) {
                        lum = 1.0f - lum;
                    }
                    lum = vg_image_apply_contrast(lum, contrast);
                    lit = lum >= threshold;
                }

                if (lit) {
                    if (run_start < 0) {
                        run_start = cx;
                        run_sum = 0.0f;
                        run_count = 0;
                    }
                    run_sum += lum;
                    run_count++;
                    continue;
                }

                if (run_start >= 0 && run_count > 0) {
                    float avg = run_sum / (float)run_count;
                    float lw = min_w + (max_w - min_w) * avg;
                    float y = dst.y + (1.0f - y_u) * dst.h;
                    float j = 0.0f;
                    if (style->line_jitter_px > 0.0f) {
                        float seed = sinf((float)(ry * 97 + run_start * 17));
                        j = seed * style->line_jitter_px;
                    }
                    vg_rect seg = {
                        dst.x + (float)run_start * x_step,
                        y - lw * 0.5f + j,
                        (float)(cx - run_start) * x_step,
                        lw
                    };
                    if (seg.w > 0.4f && seg.h > 0.2f) {
                        vg_result r = vg_fill_rect(ctx, seg, &fs);
                        if (r != VG_OK) {
                            return r;
                        }
                    }
                }
                run_start = -1;
                run_sum = 0.0f;
                run_count = 0;
            }
        }
        return VG_OK;
    }

    {
        /* Block brightness mode:
           - compute average luminance from original source region per cell
           - quantize to N luminance levels
           - render one solid rectangle per cell with quantized brightness */
        float cw = style->cell_width_px > 0.0f ? style->cell_width_px : 14.0f;
        float ch = style->cell_height_px > 0.0f ? style->cell_height_px : 18.0f;
        int levels = style->block_levels;
        if (levels < 2) {
            levels = 2;
        }
        if (levels > 32) {
            levels = 32;
        }
        cw = vg_image_clampf(cw, 2.0f, 48.0f);
        ch = vg_image_clampf(ch, 2.0f, 56.0f);
        int cols = (int)(dst.w / cw);
        int rows = (int)(dst.h / ch);
        if (cols < 2 || rows < 2) {
            return VG_OK;
        }
        vg_fill_style fs = {
            .intensity = style->intensity > 0.0f ? style->intensity : 1.0f,
            .color = col,
            .blend = style->blend
        };
        for (int ry = 0; ry < rows; ++ry) {
            for (int cx = 0; cx < cols; ++cx) {
                float u0 = (float)cx / (float)cols;
                float u1 = (float)(cx + 1) / (float)cols;
                float v0 = (float)ry / (float)rows;
                float v1 = (float)(ry + 1) / (float)rows;
                uint32_t sx0 = (uint32_t)floorf(u0 * (float)src->width);
                uint32_t sx1 = (uint32_t)ceilf(u1 * (float)src->width);
                uint32_t sy0 = (uint32_t)floorf(v0 * (float)src->height);
                uint32_t sy1 = (uint32_t)ceilf(v1 * (float)src->height);
                if (sx1 <= sx0) sx1 = sx0 + 1u;
                if (sy1 <= sy0) sy1 = sy0 + 1u;
                if (sx1 > src->width) sx1 = src->width;
                if (sy1 > src->height) sy1 = src->height;

                float lum_sum = 0.0f;
                uint32_t lum_n = 0u;
                for (uint32_t sy = sy0; sy < sy1; ++sy) {
                    const uint8_t* row = src->pixels_rgba8 + (size_t)sy * src->stride_bytes;
                    for (uint32_t sx = sx0; sx < sx1; ++sx) {
                        lum_sum += vg_image_luma(row + (size_t)sx * 4u);
                        lum_n++;
                    }
                }
                if (lum_n == 0u) {
                    continue;
                }
                float lum = lum_sum / (float)lum_n;
                if (style->invert) {
                    lum = 1.0f - lum;
                }
                lum = vg_image_apply_contrast(lum, contrast);
                /* Block mode uses a softer threshold mapping and perceptual lift to preserve mid/shadow detail. */
                float t_soft = threshold * 0.72f;
                float den = 1.0f - t_soft;
                if (den < 0.001f) {
                    den = 0.001f;
                }
                float norm = (lum - t_soft) / den;
                norm = vg_image_clampf(norm, 0.0f, 1.0f);
                if (norm <= 0.001f) {
                    continue;
                }
                norm = powf(norm, 0.78f);
                float q = floorf(norm * (float)(levels - 1) + 0.5f) / (float)(levels - 1);
                q = vg_image_clampf(q, 0.0f, 1.0f);
                if (q <= 0.001f) {
                    continue;
                }
                float cell_x = dst.x + (float)cx * cw;
                float cell_y = dst.y + dst.h - (float)(ry + 1) * ch;
                vg_fill_style cell_fs = fs;
                float shade = 0.12f + 0.88f * q;
                cell_fs.intensity = fs.intensity * shade;
                cell_fs.color.a = fs.color.a * shade;
                vg_rect r = {
                    cell_x + 0.16f,
                    cell_y + 0.16f,
                    cw - 0.32f,
                    ch - 0.32f
                };
                vg_result dr = vg_fill_rect(ctx, r, &cell_fs);
                if (dr != VG_OK) {
                    return dr;
                }
            }
        }
    }

    return VG_OK;
}
