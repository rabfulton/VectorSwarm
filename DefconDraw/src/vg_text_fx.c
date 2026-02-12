#include "vg_text_fx.h"

#include <math.h>
#include <string.h>

void vg_text_fx_typewriter_reset(vg_text_fx_typewriter* fx) {
    if (!fx) {
        return;
    }
    fx->visible_chars = 0u;
    fx->timer_s = 0.0f;
}

void vg_text_fx_typewriter_set_text(vg_text_fx_typewriter* fx, const char* text) {
    if (!fx) {
        return;
    }
    fx->text = text ? text : "";
    vg_text_fx_typewriter_reset(fx);
}

void vg_text_fx_typewriter_set_rate(vg_text_fx_typewriter* fx, float char_dt_s) {
    if (!fx) {
        return;
    }
    if (char_dt_s <= 0.001f) {
        char_dt_s = 0.001f;
    }
    fx->char_dt_s = char_dt_s;
}

void vg_text_fx_typewriter_set_beep(vg_text_fx_typewriter* fx, vg_text_fx_beep_fn fn, void* user) {
    if (!fx) {
        return;
    }
    fx->beep_fn = fn;
    fx->beep_user = user;
}

void vg_text_fx_typewriter_set_beep_profile(vg_text_fx_typewriter* fx, float base_hz, float step_hz, float dur_s, float amp) {
    if (!fx) {
        return;
    }
    fx->beep_base_hz = base_hz > 0.0f ? base_hz : 900.0f;
    fx->beep_step_hz = step_hz > 0.0f ? step_hz : 55.0f;
    fx->beep_dur_s = dur_s > 0.0f ? dur_s : 0.028f;
    fx->beep_amp = amp > 0.0f ? amp : 0.17f;
}

void vg_text_fx_typewriter_enable_beep(vg_text_fx_typewriter* fx, int enabled) {
    if (!fx) {
        return;
    }
    fx->beep_enabled = enabled ? 1 : 0;
}

size_t vg_text_fx_typewriter_update(vg_text_fx_typewriter* fx, float dt_s) {
    if (!fx || !fx->text || fx->char_dt_s <= 0.0f || dt_s <= 0.0f) {
        return 0u;
    }
    size_t text_len = strlen(fx->text);
    if (fx->visible_chars >= text_len) {
        return 0u;
    }
    fx->timer_s -= dt_s;
    size_t added = 0u;
    while (fx->timer_s <= 0.0f && fx->visible_chars < text_len) {
        char ch = fx->text[fx->visible_chars];
        fx->visible_chars++;
        fx->timer_s += fx->char_dt_s;
        added++;
        if (fx->beep_enabled && fx->beep_fn && ch > ' ' && ch != '\n') {
            float f = fx->beep_base_hz + (float)((unsigned char)ch % 7u) * fx->beep_step_hz;
            fx->beep_fn(fx->beep_user, ch, f, fx->beep_dur_s, fx->beep_amp);
        }
    }
    return added;
}

size_t vg_text_fx_typewriter_copy_visible(const vg_text_fx_typewriter* fx, char* out, size_t out_cap) {
    if (!fx || !fx->text || !out || out_cap == 0u) {
        return 0u;
    }
    size_t text_len = strlen(fx->text);
    size_t n = fx->visible_chars;
    if (n > text_len) {
        n = text_len;
    }
    if (n >= out_cap) {
        n = out_cap - 1u;
    }
    if (n > 0u) {
        memcpy(out, fx->text, n);
    }
    out[n] = '\0';
    return n;
}

void vg_text_fx_marquee_reset(vg_text_fx_marquee* fx) {
    if (!fx) {
        return;
    }
    fx->offset_px = 0.0f;
}

void vg_text_fx_marquee_set_text(vg_text_fx_marquee* fx, const char* text) {
    if (!fx) {
        return;
    }
    fx->text = text ? text : "";
    fx->offset_px = 0.0f;
}

void vg_text_fx_marquee_set_speed(vg_text_fx_marquee* fx, float speed_px_s) {
    if (!fx) {
        return;
    }
    fx->speed_px_s = speed_px_s;
}

void vg_text_fx_marquee_set_gap(vg_text_fx_marquee* fx, float gap_px) {
    if (!fx) {
        return;
    }
    fx->gap_px = gap_px > 8.0f ? gap_px : 8.0f;
}

void vg_text_fx_marquee_update(vg_text_fx_marquee* fx, float dt_s) {
    if (!fx || !fx->text || dt_s <= 0.0f) {
        return;
    }
    fx->offset_px += fx->speed_px_s * dt_s;
    if (!isfinite(fx->offset_px)) {
        fx->offset_px = 0.0f;
    }
}

static vg_result vg_text_fx_draw_mode(
    vg_context* ctx,
    vg_text_draw_mode mode,
    const char* text,
    vg_vec2 origin,
    float size_px,
    float spacing,
    const vg_stroke_style* text_style,
    float boxed_weight,
    const vg_fill_style* panel_fill,
    const vg_stroke_style* panel_border
) {
    switch (mode) {
        case VG_TEXT_DRAW_MODE_STROKE:
            return vg_draw_text(ctx, text, origin, size_px, spacing, text_style, NULL);
        case VG_TEXT_DRAW_MODE_BOXED:
            return vg_draw_text_boxed(ctx, text, origin, size_px, spacing, text_style, NULL);
        case VG_TEXT_DRAW_MODE_BOXED_WEIGHTED:
            return vg_draw_text_boxed_weighted(ctx, text, origin, size_px, spacing, text_style, boxed_weight, NULL);
        case VG_TEXT_DRAW_MODE_VECTOR_FILL:
            return vg_draw_text_vector_fill(ctx, text, origin, size_px, spacing, text_style, NULL);
        case VG_TEXT_DRAW_MODE_STENCIL_CUTOUT:
            if (!panel_fill || !panel_border) {
                return VG_ERROR_INVALID_ARGUMENT;
            }
            return vg_draw_text_stencil_cutout(ctx, text, origin, size_px, spacing, panel_fill, panel_border, text_style, NULL);
        default:
            return VG_ERROR_INVALID_ARGUMENT;
    }
}

vg_result vg_text_fx_marquee_draw(
    vg_context* ctx,
    const vg_text_fx_marquee* fx,
    vg_rect box,
    float size_px,
    float letter_spacing_px,
    vg_text_draw_mode mode,
    const vg_stroke_style* text_style,
    float boxed_weight,
    const vg_fill_style* panel_fill,
    const vg_stroke_style* panel_border,
    const vg_fill_style* clip_fill
) {
    if (!ctx || !fx || !fx->text || !text_style || box.w <= 1.0f || box.h <= 1.0f || size_px <= 0.0f) {
        return VG_ERROR_INVALID_ARGUMENT;
    }
    float text_w = vg_measure_text(fx->text, size_px, letter_spacing_px);
    if (text_w <= 0.5f) {
        return VG_OK;
    }
    float gap = fx->gap_px > 8.0f ? fx->gap_px : 8.0f;
    float cycle = text_w + gap;
    float ofs = fmodf(fx->offset_px, cycle);
    if (ofs < 0.0f) {
        ofs += cycle;
    }

    if (panel_fill) {
        vg_result r = vg_fill_rect(ctx, box, panel_fill);
        if (r != VG_OK) {
            return r;
        }
    }
    if (panel_border) {
        vg_result r = vg_draw_rect(ctx, box, panel_border);
        if (r != VG_OK) {
            return r;
        }
    }

    float y = box.y + (box.h - size_px) * 0.5f;
    for (float x = box.x - ofs; x < box.x + box.w + text_w; x += cycle) {
        vg_result r = vg_text_fx_draw_mode(
            ctx,
            mode,
            fx->text,
            (vg_vec2){x, y},
            size_px,
            letter_spacing_px,
            text_style,
            boxed_weight,
            panel_fill,
            panel_border
        );
        if (r != VG_OK) {
            return r;
        }
    }

    if (clip_fill) {
        vg_rect left = {box.x - 4096.0f, box.y, 4096.0f, box.h};
        vg_rect right = {box.x + box.w, box.y, 4096.0f, box.h};
        vg_result r = vg_fill_rect(ctx, left, clip_fill);
        if (r != VG_OK) {
            return r;
        }
        r = vg_fill_rect(ctx, right, clip_fill);
        if (r != VG_OK) {
            return r;
        }
    }
    return VG_OK;
}
