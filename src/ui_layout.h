#ifndef V_TYPE_UI_LAYOUT_H
#define V_TYPE_UI_LAYOUT_H

#include "vg.h"

#include <math.h>

#define UI_REF_WIDTH 1920.0f
#define UI_REF_HEIGHT 1080.0f
#define UI_SCALE_MIN 0.75f
#define UI_SCALE_MAX 2.50f
#define UI_SAFE_MARGIN 0.04f
#define UI_SAFE_ASPECT (16.0f / 9.0f)

static inline float ui_layout_clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static inline float ui_reference_scale(float w, float h) {
    const float sx = w / UI_REF_WIDTH;
    const float sy = h / UI_REF_HEIGHT;
    const float s = fminf(sx, sy);
    return ui_layout_clampf(s, UI_SCALE_MIN, UI_SCALE_MAX);
}

static inline vg_rect fit_rect_aspect(vg_rect bounds, float aspect_w_over_h) {
    vg_rect out = bounds;
    const float b_aspect = bounds.w / fmaxf(bounds.h, 1e-5f);
    if (b_aspect > aspect_w_over_h) {
        out.h = bounds.h;
        out.w = out.h * aspect_w_over_h;
        out.x = bounds.x + (bounds.w - out.w) * 0.5f;
        out.y = bounds.y;
    } else {
        out.w = bounds.w;
        out.h = out.w / aspect_w_over_h;
        out.x = bounds.x;
        out.y = bounds.y + (bounds.h - out.h) * 0.5f;
    }
    return out;
}

static inline vg_rect make_ui_safe_frame(float w, float h) {
    const vg_rect outer = {
        w * UI_SAFE_MARGIN,
        h * UI_SAFE_MARGIN,
        w * (1.0f - 2.0f * UI_SAFE_MARGIN),
        h * (1.0f - 2.0f * UI_SAFE_MARGIN)
    };
    return fit_rect_aspect(outer, UI_SAFE_ASPECT);
}

#endif
