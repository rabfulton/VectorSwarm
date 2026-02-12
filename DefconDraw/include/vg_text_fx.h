#ifndef VG_TEXT_FX_H
#define VG_TEXT_FX_H

#ifdef __cplusplus
extern "C" {
#endif

#include "vg.h"
#include "vg_text_layout.h"

#include <stddef.h>

typedef void (*vg_text_fx_beep_fn)(void* user, char ch, float freq_hz, float dur_s, float amp);

typedef struct vg_text_fx_typewriter {
    const char* text;
    size_t visible_chars;
    float timer_s;
    float char_dt_s;
    vg_text_fx_beep_fn beep_fn;
    void* beep_user;
    float beep_base_hz;
    float beep_step_hz;
    float beep_dur_s;
    float beep_amp;
    int beep_enabled;
} vg_text_fx_typewriter;

void vg_text_fx_typewriter_reset(vg_text_fx_typewriter* fx);
void vg_text_fx_typewriter_set_text(vg_text_fx_typewriter* fx, const char* text);
void vg_text_fx_typewriter_set_rate(vg_text_fx_typewriter* fx, float char_dt_s);
void vg_text_fx_typewriter_set_beep(vg_text_fx_typewriter* fx, vg_text_fx_beep_fn fn, void* user);
void vg_text_fx_typewriter_set_beep_profile(vg_text_fx_typewriter* fx, float base_hz, float step_hz, float dur_s, float amp);
void vg_text_fx_typewriter_enable_beep(vg_text_fx_typewriter* fx, int enabled);
size_t vg_text_fx_typewriter_update(vg_text_fx_typewriter* fx, float dt_s);
size_t vg_text_fx_typewriter_copy_visible(const vg_text_fx_typewriter* fx, char* out, size_t out_cap);

typedef struct vg_text_fx_marquee {
    const char* text;
    float offset_px;
    float speed_px_s;
    float gap_px;
} vg_text_fx_marquee;

void vg_text_fx_marquee_reset(vg_text_fx_marquee* fx);
void vg_text_fx_marquee_set_text(vg_text_fx_marquee* fx, const char* text);
void vg_text_fx_marquee_set_speed(vg_text_fx_marquee* fx, float speed_px_s);
void vg_text_fx_marquee_set_gap(vg_text_fx_marquee* fx, float gap_px);
void vg_text_fx_marquee_update(vg_text_fx_marquee* fx, float dt_s);
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
    const vg_stroke_style* panel_border
);

#ifdef __cplusplus
}
#endif

#endif
