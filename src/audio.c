#include "audio.h"
#include "game.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

float audio_rand01_from_state(uint32_t* state) {
    if (!state) {
        return 0.0f;
    }
    *state = (*state * 1664525u) + 1013904223u;
    return (float)((*state >> 8) & 0x00ffffffu) / 16777215.0f;
}

static float lerpf(float a, float b, float t) {
    return a + (b - a) * t;
}

static float round_to(float v, float step) {
    if (step <= 0.0f) {
        return v;
    }
    return floorf(v / step + 0.5f) * step;
}

static float osc_sample(uint8_t waveform, float phase, uint32_t* rng_state) {
    const float p = fmodf(phase, 6.28318530718f);
    const float u = p * (1.0f / 6.28318530718f);
    switch (waveform) {
        case 0: /* sine */
            return sinf(p);
        case 1: /* saw */
            return 2.0f * u - 1.0f;
        case 2: /* square */
            return (u < 0.5f) ? 1.0f : -1.0f;
        case 3: { /* triangle */
            const float tri = 2.0f * fabsf(2.0f * u - 1.0f) - 1.0f;
            return -tri;
        }
        default: /* noise */
            return audio_rand01_from_state(rng_state) * 2.0f - 1.0f;
    }
}

float acoustics_value_to_display(int id, float t01) {
    const float t = clampf(t01, 0.0f, 1.0f);
    switch (id) {
        case ACOUST_FIRE_WAVE: return floorf(t * 4.0f + 0.5f);
        case ACOUST_FIRE_PITCH: return lerpf(90.0f, 420.0f, t);
        case ACOUST_FIRE_ATTACK: return lerpf(0.2f, 28.0f, t);
        case ACOUST_FIRE_DECAY: return lerpf(12.0f, 220.0f, t);
        case ACOUST_FIRE_CUTOFF: return lerpf(600.0f, 10000.0f, t);
        case ACOUST_FIRE_RESONANCE: return lerpf(0.05f, 0.98f, t);
        case ACOUST_FIRE_SWEEP_ST: return lerpf(-24.0f, 24.0f, t);
        case ACOUST_FIRE_SWEEP_DECAY: return lerpf(2.0f, 260.0f, t);
        case ACOUST_THR_LEVEL: return lerpf(0.04f, 0.60f, t);
        case ACOUST_THR_PITCH: return lerpf(30.0f, 180.0f, t);
        case ACOUST_THR_ATTACK: return lerpf(4.0f, 140.0f, t);
        case ACOUST_THR_RELEASE: return lerpf(18.0f, 650.0f, t);
        case ACOUST_THR_CUTOFF: return lerpf(120.0f, 3200.0f, t);
        case ACOUST_THR_RESONANCE: return lerpf(0.02f, 0.90f, t);
        default: return t;
    }
}

float acoustics_value_to_ui_display(int id, float t01) {
    const float v = acoustics_value_to_display(id, t01);
    switch (id) {
        case ACOUST_FIRE_WAVE:
            return v;
        case ACOUST_FIRE_PITCH:
        case ACOUST_FIRE_ATTACK:
        case ACOUST_FIRE_DECAY:
        case ACOUST_FIRE_SWEEP_DECAY:
        case ACOUST_THR_PITCH:
        case ACOUST_THR_ATTACK:
        case ACOUST_THR_RELEASE:
            return round_to(v, 1.0f);
        case ACOUST_FIRE_CUTOFF:
        case ACOUST_THR_CUTOFF:
            return round_to(v * 0.001f, 0.01f);
        case ACOUST_FIRE_RESONANCE:
        case ACOUST_THR_RESONANCE:
        case ACOUST_THR_LEVEL:
            return round_to(v, 0.01f);
        case ACOUST_FIRE_SWEEP_ST:
            return round_to(v, 0.1f);
        default:
            return v;
    }
}

float acoustics_combat_value_to_display(int id, float t01) {
    const float t = clampf(t01, 0.0f, 1.0f);
    switch (id) {
        case ACOUST_COMBAT_EXP_LEVEL:
            return lerpf(0.02f, 0.95f, t);
        case ACOUST_COMBAT_ENEMY_WAVE:
            return floorf(t * 4.0f + 0.5f);
        case ACOUST_COMBAT_ENEMY_PITCH:
            return lerpf(90.0f, 420.0f, t);
        case ACOUST_COMBAT_EXP_PITCH:
            return lerpf(40.0f, 280.0f, t);
        case ACOUST_COMBAT_ENEMY_ATTACK:
            return lerpf(0.2f, 28.0f, t);
        case ACOUST_COMBAT_EXP_ATTACK:
            return lerpf(0.1f, 45.0f, t);
        case ACOUST_COMBAT_ENEMY_DECAY:
            return lerpf(12.0f, 220.0f, t);
        case ACOUST_COMBAT_EXP_DECAY:
            return lerpf(60.0f, 900.0f, t);
        case ACOUST_COMBAT_ENEMY_CUTOFF:
            return lerpf(600.0f, 10000.0f, t);
        case ACOUST_COMBAT_ENEMY_RESONANCE:
            return lerpf(0.05f, 0.98f, t);
        case ACOUST_COMBAT_ENEMY_SWEEP_ST:
            return lerpf(-24.0f, 24.0f, t);
        case ACOUST_COMBAT_ENEMY_SWEEP_DECAY:
            return lerpf(2.0f, 260.0f, t);
        case ACOUST_COMBAT_EXP_NOISE:
            return t;
        case ACOUST_COMBAT_EXP_FM_DEPTH:
            return lerpf(0.0f, 420.0f, t);
        case ACOUST_COMBAT_EXP_FM_RATE:
            return lerpf(8.0f, 1600.0f, t);
        case ACOUST_COMBAT_EXP_PANW:
            return lerpf(0.25f, 1.20f, t);
        default: return t;
    }
}

float acoustics_combat_value_to_ui_display(int id, float t01) {
    const float v = acoustics_combat_value_to_display(id, t01);
    switch (id) {
        case ACOUST_COMBAT_ENEMY_WAVE:
            return v;
        case ACOUST_COMBAT_EXP_LEVEL:
        case ACOUST_COMBAT_EXP_NOISE:
        case ACOUST_COMBAT_EXP_PANW:
        case ACOUST_COMBAT_ENEMY_RESONANCE:
            return round_to(v, 0.01f);
        case ACOUST_COMBAT_ENEMY_CUTOFF:
            return round_to(v * 0.001f, 0.01f);
        case ACOUST_COMBAT_ENEMY_SWEEP_ST:
            return round_to(v, 0.1f);
        case ACOUST_COMBAT_ENEMY_PITCH:
        case ACOUST_COMBAT_ENEMY_ATTACK:
        case ACOUST_COMBAT_ENEMY_DECAY:
        case ACOUST_COMBAT_ENEMY_SWEEP_DECAY:
        case ACOUST_COMBAT_EXP_ATTACK:
        case ACOUST_COMBAT_EXP_DECAY:
        case ACOUST_COMBAT_EXP_FM_DEPTH:
        case ACOUST_COMBAT_EXP_FM_RATE:
        case ACOUST_COMBAT_EXP_PITCH:
            return round_to(v, 1.0f);
        default:
            return round_to(v, 0.01f);
    }
}

float acoustics_equipment_value_to_display(int id, float t01) {
    const float t = clampf(t01, 0.0f, 1.0f);
    switch (id) {
        case ACOUST_EQUIP_SHIELD_LEVEL:
        case ACOUST_EQUIP_AUX_LEVEL:
            return lerpf(0.02f, 0.90f, t);
        case ACOUST_EQUIP_SHIELD_PITCH:
        case ACOUST_EQUIP_AUX_PITCH:
            return lerpf(30.0f, 240.0f, t);
        case ACOUST_EQUIP_SHIELD_ATTACK:
        case ACOUST_EQUIP_AUX_ATTACK:
            return lerpf(1.0f, 220.0f, t);
        case ACOUST_EQUIP_SHIELD_RELEASE:
        case ACOUST_EQUIP_AUX_RELEASE:
            return lerpf(10.0f, 1800.0f, t);
        case ACOUST_EQUIP_SHIELD_NOISE:
        case ACOUST_EQUIP_AUX_NOISE:
            return t;
        case ACOUST_EQUIP_SHIELD_FM_DEPTH:
        case ACOUST_EQUIP_AUX_FM_DEPTH:
            return lerpf(0.0f, 320.0f, t);
        case ACOUST_EQUIP_SHIELD_FM_RATE:
        case ACOUST_EQUIP_AUX_FM_RATE:
            return lerpf(0.5f, 120.0f, t);
        case ACOUST_EQUIP_SHIELD_CUTOFF:
        case ACOUST_EQUIP_AUX_CUTOFF:
            return lerpf(80.0f, 5200.0f, t);
        default:
            return t;
    }
}

float acoustics_equipment_value_to_ui_display(int id, float t01) {
    const float v = acoustics_equipment_value_to_display(id, t01);
    switch (id) {
        case ACOUST_EQUIP_SHIELD_LEVEL:
        case ACOUST_EQUIP_AUX_LEVEL:
        case ACOUST_EQUIP_SHIELD_NOISE:
        case ACOUST_EQUIP_AUX_NOISE:
            return round_to(v, 0.01f);
        case ACOUST_EQUIP_SHIELD_PITCH:
        case ACOUST_EQUIP_AUX_PITCH:
        case ACOUST_EQUIP_SHIELD_ATTACK:
        case ACOUST_EQUIP_AUX_ATTACK:
        case ACOUST_EQUIP_SHIELD_RELEASE:
        case ACOUST_EQUIP_AUX_RELEASE:
        case ACOUST_EQUIP_SHIELD_FM_DEPTH:
        case ACOUST_EQUIP_AUX_FM_DEPTH:
        case ACOUST_EQUIP_SHIELD_FM_RATE:
        case ACOUST_EQUIP_AUX_FM_RATE:
            return round_to(v, 1.0f);
        case ACOUST_EQUIP_SHIELD_CUTOFF:
        case ACOUST_EQUIP_AUX_CUTOFF:
            return round_to(v * 0.001f, 0.01f);
        default:
            return round_to(v, 0.01f);
    }
}

void acoustics_defaults_init(float out_values_01[ACOUSTICS_SLIDER_COUNT]) {
    if (!out_values_01) {
        return;
    }
    out_values_01[ACOUST_FIRE_WAVE] = 0.275879592f;
    out_values_01[ACOUST_FIRE_PITCH] = 0.602183819f;
    out_values_01[ACOUST_FIRE_ATTACK] = 0.003753547f;
    out_values_01[ACOUST_FIRE_DECAY] = 0.460912049f;
    out_values_01[ACOUST_FIRE_CUTOFF] = 0.100429699f;
    out_values_01[ACOUST_FIRE_RESONANCE] = 0.985629857f;
    out_values_01[ACOUST_FIRE_SWEEP_ST] = 0.949483037f;
    out_values_01[ACOUST_FIRE_SWEEP_DECAY] = 0.827205420f;
    out_values_01[ACOUST_THR_LEVEL] = 0.570973873f;
    out_values_01[ACOUST_THR_PITCH] = 0.997384906f;
    out_values_01[ACOUST_THR_ATTACK] = 0.814027071f;
    out_values_01[ACOUST_THR_RELEASE] = 0.294867337f;
    out_values_01[ACOUST_THR_CUTOFF] = 0.035423841f;
    out_values_01[ACOUST_THR_RESONANCE] = 0.998682797f;
}

void acoustics_combat_defaults_init(float out_values_01[ACOUST_COMBAT_SLIDER_COUNT]) {
    if (!out_values_01) {
        return;
    }
    out_values_01[ACOUST_COMBAT_ENEMY_WAVE] = 0.275879592f;
    out_values_01[ACOUST_COMBAT_ENEMY_PITCH] = 0.602183819f;
    out_values_01[ACOUST_COMBAT_ENEMY_ATTACK] = 0.003753547f;
    out_values_01[ACOUST_COMBAT_ENEMY_DECAY] = 0.460912049f;
    out_values_01[ACOUST_COMBAT_ENEMY_CUTOFF] = 0.100429699f;
    out_values_01[ACOUST_COMBAT_ENEMY_RESONANCE] = 0.985629857f;
    out_values_01[ACOUST_COMBAT_ENEMY_SWEEP_ST] = 0.949483037f;
    out_values_01[ACOUST_COMBAT_ENEMY_SWEEP_DECAY] = 0.827205420f;
    out_values_01[ACOUST_COMBAT_EXP_LEVEL] = 0.58f;
    out_values_01[ACOUST_COMBAT_EXP_PITCH] = 0.28f;
    out_values_01[ACOUST_COMBAT_EXP_ATTACK] = 0.07f;
    out_values_01[ACOUST_COMBAT_EXP_DECAY] = 0.54f;
    out_values_01[ACOUST_COMBAT_EXP_NOISE] = 0.64f;
    out_values_01[ACOUST_COMBAT_EXP_FM_DEPTH] = 0.28f;
    out_values_01[ACOUST_COMBAT_EXP_FM_RATE] = 0.21f;
    out_values_01[ACOUST_COMBAT_EXP_PANW] = 0.90f;
}

void acoustics_equipment_defaults_init(float out_values_01[ACOUST_EQUIP_SLIDER_COUNT]) {
    if (!out_values_01) {
        return;
    }
    out_values_01[ACOUST_EQUIP_SHIELD_LEVEL] = 0.54f;
    out_values_01[ACOUST_EQUIP_SHIELD_PITCH] = 0.32f;
    out_values_01[ACOUST_EQUIP_SHIELD_ATTACK] = 0.08f;
    out_values_01[ACOUST_EQUIP_SHIELD_RELEASE] = 0.22f;
    out_values_01[ACOUST_EQUIP_SHIELD_NOISE] = 0.34f;
    out_values_01[ACOUST_EQUIP_SHIELD_FM_DEPTH] = 0.26f;
    out_values_01[ACOUST_EQUIP_SHIELD_FM_RATE] = 0.16f;
    out_values_01[ACOUST_EQUIP_SHIELD_CUTOFF] = 0.28f;

    out_values_01[ACOUST_EQUIP_AUX_LEVEL] = 0.42f;
    out_values_01[ACOUST_EQUIP_AUX_PITCH] = 0.40f;
    out_values_01[ACOUST_EQUIP_AUX_ATTACK] = 0.05f;
    out_values_01[ACOUST_EQUIP_AUX_RELEASE] = 0.18f;
    out_values_01[ACOUST_EQUIP_AUX_NOISE] = 0.50f;
    out_values_01[ACOUST_EQUIP_AUX_FM_DEPTH] = 0.20f;
    out_values_01[ACOUST_EQUIP_AUX_FM_RATE] = 0.12f;
    out_values_01[ACOUST_EQUIP_AUX_CUTOFF] = 0.30f;
}

static int file_exists_readable(const char* path) {
    if (!path || path[0] == '\0') {
        return 0;
    }
    FILE* f = fopen(path, "r");
    if (!f) {
        return 0;
    }
    fclose(f);
    return 1;
}

const char* resolve_acoustics_slots_path(void) {
    static const char* candidates[] = {
        "acoustics_slots.cfg",
        "build/acoustics_slots.cfg",
        "../build/acoustics_slots.cfg"
    };
    for (int i = 0; i < (int)(sizeof(candidates) / sizeof(candidates[0])); ++i) {
        if (file_exists_readable(candidates[i])) {
            return candidates[i];
        }
    }
    return "acoustics_slots.cfg";
}

void acoustics_slot_defaults_view(acoustics_slot_view* v) {
    if (!v || !v->fire_slot_selected || !v->thr_slot_selected || !v->enemy_slot_selected || !v->exp_slot_selected ||
        !v->shield_slot_selected || !v->aux_slot_selected ||
        !v->fire_slot_defined || !v->thr_slot_defined || !v->enemy_slot_defined || !v->exp_slot_defined ||
        !v->shield_slot_defined || !v->aux_slot_defined ||
        !v->fire_slots || !v->thr_slots || !v->enemy_slots || !v->exp_slots || !v->shield_slots || !v->aux_slots ||
        !v->value_01 || !v->combat_value_01 || !v->equipment_value_01) {
        return;
    }
    *v->fire_slot_selected = 0;
    *v->thr_slot_selected = 0;
    *v->enemy_slot_selected = 0;
    *v->exp_slot_selected = 0;
    *v->shield_slot_selected = 0;
    *v->aux_slot_selected = 0;
    memset(v->fire_slot_defined, 0, ACOUSTICS_SLOT_COUNT * sizeof(v->fire_slot_defined[0]));
    memset(v->thr_slot_defined, 0, ACOUSTICS_SLOT_COUNT * sizeof(v->thr_slot_defined[0]));
    memset(v->enemy_slot_defined, 0, ACOUSTICS_SLOT_COUNT * sizeof(v->enemy_slot_defined[0]));
    memset(v->exp_slot_defined, 0, ACOUSTICS_SLOT_COUNT * sizeof(v->exp_slot_defined[0]));
    memset(v->shield_slot_defined, 0, ACOUSTICS_SLOT_COUNT * sizeof(v->shield_slot_defined[0]));
    memset(v->aux_slot_defined, 0, ACOUSTICS_SLOT_COUNT * sizeof(v->aux_slot_defined[0]));
    memset(v->fire_slots, 0, ACOUSTICS_SLOT_COUNT * sizeof(v->fire_slots[0]));
    memset(v->thr_slots, 0, ACOUSTICS_SLOT_COUNT * sizeof(v->thr_slots[0]));
    memset(v->enemy_slots, 0, ACOUSTICS_SLOT_COUNT * sizeof(v->enemy_slots[0]));
    memset(v->exp_slots, 0, ACOUSTICS_SLOT_COUNT * sizeof(v->exp_slots[0]));
    memset(v->shield_slots, 0, ACOUSTICS_SLOT_COUNT * sizeof(v->shield_slots[0]));
    memset(v->aux_slots, 0, ACOUSTICS_SLOT_COUNT * sizeof(v->aux_slots[0]));
    for (int i = 0; i < 8; ++i) {
        v->fire_slots[0][i] = v->value_01[i];
    }
    for (int i = 0; i < 6; ++i) {
        v->thr_slots[0][i] = v->value_01[8 + i];
    }
    for (int i = 0; i < 8; ++i) {
        v->enemy_slots[0][i] = v->combat_value_01[i];
    }
    for (int i = 0; i < 8; ++i) {
        v->exp_slots[0][i] = v->combat_value_01[8 + i];
    }
    for (int i = 0; i < 8; ++i) {
        v->shield_slots[0][i] = v->equipment_value_01[i];
    }
    for (int i = 0; i < 8; ++i) {
        v->aux_slots[0][i] = v->equipment_value_01[8 + i];
    }
    v->fire_slot_defined[0] = 1u;
    v->thr_slot_defined[0] = 1u;
    v->enemy_slot_defined[0] = 1u;
    v->exp_slot_defined[0] = 1u;
    v->shield_slot_defined[0] = 1u;
    v->aux_slot_defined[0] = 1u;
}

void acoustics_capture_current_to_selected_slot_view(acoustics_slot_view* v, int is_fire) {
    if (!v || !v->value_01 || !v->fire_slots || !v->thr_slots || !v->fire_slot_defined || !v->thr_slot_defined ||
        !v->fire_slot_selected || !v->thr_slot_selected) {
        return;
    }
    if (is_fire) {
        const int s = *v->fire_slot_selected;
        if (s < 0 || s >= ACOUSTICS_SLOT_COUNT) {
            return;
        }
        for (int i = 0; i < 8; ++i) {
            v->fire_slots[s][i] = v->value_01[i];
        }
        v->fire_slot_defined[s] = 1u;
        return;
    }
    const int s = *v->thr_slot_selected;
    if (s < 0 || s >= ACOUSTICS_SLOT_COUNT) {
        return;
    }
    for (int i = 0; i < 6; ++i) {
        v->thr_slots[s][i] = v->value_01[8 + i];
    }
    v->thr_slot_defined[s] = 1u;
}

void acoustics_capture_current_to_selected_combat_slot_view(acoustics_slot_view* v, int is_enemy) {
    if (!v || !v->combat_value_01 || !v->enemy_slots || !v->exp_slots ||
        !v->enemy_slot_defined || !v->exp_slot_defined || !v->enemy_slot_selected || !v->exp_slot_selected) {
        return;
    }
    if (is_enemy) {
        const int s = *v->enemy_slot_selected;
        if (s < 0 || s >= ACOUSTICS_SLOT_COUNT) {
            return;
        }
        for (int i = 0; i < 8; ++i) {
            v->enemy_slots[s][i] = v->combat_value_01[i];
        }
        v->enemy_slot_defined[s] = 1u;
        return;
    }
    const int s = *v->exp_slot_selected;
    if (s < 0 || s >= ACOUSTICS_SLOT_COUNT) {
        return;
    }
    for (int i = 0; i < 8; ++i) {
        v->exp_slots[s][i] = v->combat_value_01[8 + i];
    }
    v->exp_slot_defined[s] = 1u;
}

void acoustics_capture_current_to_selected_equipment_slot_view(acoustics_slot_view* v, int is_shield) {
    if (!v || !v->equipment_value_01 || !v->shield_slots || !v->aux_slots ||
        !v->shield_slot_defined || !v->aux_slot_defined || !v->shield_slot_selected || !v->aux_slot_selected) {
        return;
    }
    if (is_shield) {
        const int s = *v->shield_slot_selected;
        if (s < 0 || s >= ACOUSTICS_SLOT_COUNT) {
            return;
        }
        for (int i = 0; i < 8; ++i) {
            v->shield_slots[s][i] = v->equipment_value_01[i];
        }
        v->shield_slot_defined[s] = 1u;
        return;
    }
    {
        const int s = *v->aux_slot_selected;
        if (s < 0 || s >= ACOUSTICS_SLOT_COUNT) {
            return;
        }
        for (int i = 0; i < 8; ++i) {
            v->aux_slots[s][i] = v->equipment_value_01[8 + i];
        }
        v->aux_slot_defined[s] = 1u;
    }
}

void acoustics_load_slot_to_current_view(acoustics_slot_view* v, int is_fire, int slot_idx) {
    if (!v || !v->value_01 || !v->fire_slots || !v->thr_slots || !v->fire_slot_defined || !v->thr_slot_defined ||
        slot_idx < 0 || slot_idx >= ACOUSTICS_SLOT_COUNT) {
        return;
    }
    if (is_fire) {
        if (!v->fire_slot_defined[slot_idx]) {
            return;
        }
        for (int i = 0; i < 8; ++i) {
            v->value_01[i] = v->fire_slots[slot_idx][i];
        }
        return;
    }
    if (!v->thr_slot_defined[slot_idx]) {
        return;
    }
    for (int i = 0; i < 6; ++i) {
        v->value_01[8 + i] = v->thr_slots[slot_idx][i];
    }
}

void acoustics_load_combat_slot_to_current_view(acoustics_slot_view* v, int is_enemy, int slot_idx) {
    if (!v || !v->combat_value_01 || !v->enemy_slots || !v->exp_slots ||
        !v->enemy_slot_defined || !v->exp_slot_defined || slot_idx < 0 || slot_idx >= ACOUSTICS_SLOT_COUNT) {
        return;
    }
    if (is_enemy) {
        if (!v->enemy_slot_defined[slot_idx]) {
            return;
        }
        for (int i = 0; i < 8; ++i) {
            v->combat_value_01[i] = v->enemy_slots[slot_idx][i];
        }
        return;
    }
    if (!v->exp_slot_defined[slot_idx]) {
        return;
    }
    for (int i = 0; i < 8; ++i) {
        v->combat_value_01[8 + i] = v->exp_slots[slot_idx][i];
    }
}

void acoustics_load_equipment_slot_to_current_view(acoustics_slot_view* v, int is_shield, int slot_idx) {
    if (!v || !v->equipment_value_01 || !v->shield_slots || !v->aux_slots ||
        !v->shield_slot_defined || !v->aux_slot_defined || slot_idx < 0 || slot_idx >= ACOUSTICS_SLOT_COUNT) {
        return;
    }
    if (is_shield) {
        if (!v->shield_slot_defined[slot_idx]) {
            return;
        }
        for (int i = 0; i < 8; ++i) {
            v->equipment_value_01[i] = v->shield_slots[slot_idx][i];
        }
        return;
    }
    if (!v->aux_slot_defined[slot_idx]) {
        return;
    }
    for (int i = 0; i < 8; ++i) {
        v->equipment_value_01[8 + i] = v->aux_slots[slot_idx][i];
    }
}

int acoustics_save_slots_view(const acoustics_slot_view* v, const char* path) {
    if (!v || !path || !v->fire_slot_selected || !v->thr_slot_selected || !v->enemy_slot_selected || !v->exp_slot_selected ||
        !v->shield_slot_selected || !v->aux_slot_selected ||
        !v->fire_slot_defined || !v->thr_slot_defined || !v->enemy_slot_defined || !v->exp_slot_defined ||
        !v->shield_slot_defined || !v->aux_slot_defined ||
        !v->fire_slots || !v->thr_slots || !v->enemy_slots || !v->exp_slots || !v->shield_slots || !v->aux_slots ||
        !v->combat_value_01 || !v->equipment_value_01) {
        return 0;
    }
    FILE* f = fopen(path, "w");
    if (!f) {
        return 0;
    }
    fprintf(f, "version=4\n");
    fprintf(f, "fsel=%d\n", *v->fire_slot_selected);
    fprintf(f, "tsel=%d\n", *v->thr_slot_selected);
    fprintf(f, "cfsel=%d\n", *v->enemy_slot_selected);
    fprintf(f, "ctsel=%d\n", *v->exp_slot_selected);
    fprintf(f, "esel=%d\n", *v->shield_slot_selected);
    fprintf(f, "atsel=%d\n", *v->aux_slot_selected);
    for (int s = 0; s < ACOUSTICS_SLOT_COUNT; ++s) {
        fprintf(f, "fd%d=%d\n", s, v->fire_slot_defined[s] ? 1 : 0);
        fprintf(f, "td%d=%d\n", s, v->thr_slot_defined[s] ? 1 : 0);
        fprintf(f, "cfd%d=%d\n", s, v->enemy_slot_defined[s] ? 1 : 0);
        fprintf(f, "ctd%d=%d\n", s, v->exp_slot_defined[s] ? 1 : 0);
        fprintf(f, "ed%d=%d\n", s, v->shield_slot_defined[s] ? 1 : 0);
        fprintf(f, "atd%d=%d\n", s, v->aux_slot_defined[s] ? 1 : 0);
        for (int i = 0; i < 8; ++i) {
            fprintf(f, "fv%d_%d=%.9f\n", s, i, v->fire_slots[s][i]);
        }
        for (int i = 0; i < 6; ++i) {
            fprintf(f, "tv%d_%d=%.9f\n", s, i, v->thr_slots[s][i]);
        }
        for (int i = 0; i < 8; ++i) {
            fprintf(f, "cfv%d_%d=%.9f\n", s, i, v->enemy_slots[s][i]);
        }
        for (int i = 0; i < 8; ++i) {
            fprintf(f, "ctv%d_%d=%.9f\n", s, i, v->exp_slots[s][i]);
        }
        for (int i = 0; i < 8; ++i) {
            fprintf(f, "ev%d_%d=%.9f\n", s, i, v->shield_slots[s][i]);
        }
        for (int i = 0; i < 8; ++i) {
            fprintf(f, "atv%d_%d=%.9f\n", s, i, v->aux_slots[s][i]);
        }
    }
    for (int i = 0; i < ACOUST_COMBAT_SLIDER_COUNT; ++i) {
        fprintf(f, "cv%d=%.9f\n", i, v->combat_value_01[i]);
    }
    for (int i = 0; i < ACOUST_EQUIP_SLIDER_COUNT; ++i) {
        fprintf(f, "evc%d=%.9f\n", i, v->equipment_value_01[i]);
    }
    fclose(f);
    return 1;
}

int acoustics_load_slots_view(acoustics_slot_view* v, const char* path) {
    if (!v || !path || !v->fire_slot_selected || !v->thr_slot_selected || !v->enemy_slot_selected || !v->exp_slot_selected ||
        !v->shield_slot_selected || !v->aux_slot_selected ||
        !v->fire_slot_defined || !v->thr_slot_defined || !v->enemy_slot_defined || !v->exp_slot_defined ||
        !v->shield_slot_defined || !v->aux_slot_defined ||
        !v->fire_slots || !v->thr_slots || !v->enemy_slots || !v->exp_slots || !v->shield_slots || !v->aux_slots ||
        !v->combat_value_01 || !v->equipment_value_01 || !v->value_01) {
        return 0;
    }
    FILE* f = fopen(path, "r");
    if (!f) {
        return 0;
    }
    char key[64];
    float value = 0.0f;
    int version = 1;
    while (fscanf(f, "%63[^=]=%f\n", key, &value) == 2) {
        if (strcmp(key, "version") == 0) {
            version = (int)clampf(value, 1.0f, 999.0f);
            continue;
        }
        if (strcmp(key, "fsel") == 0) {
            *v->fire_slot_selected = (int)clampf(value, 0.0f, (float)(ACOUSTICS_SLOT_COUNT - 1));
            continue;
        }
        if (strcmp(key, "tsel") == 0) {
            *v->thr_slot_selected = (int)clampf(value, 0.0f, (float)(ACOUSTICS_SLOT_COUNT - 1));
            continue;
        }
        if (strcmp(key, "cfsel") == 0) {
            *v->enemy_slot_selected = (int)clampf(value, 0.0f, (float)(ACOUSTICS_SLOT_COUNT - 1));
            continue;
        }
        if (strcmp(key, "ctsel") == 0) {
            *v->exp_slot_selected = (int)clampf(value, 0.0f, (float)(ACOUSTICS_SLOT_COUNT - 1));
            continue;
        }
        if (strcmp(key, "esel") == 0) {
            *v->shield_slot_selected = (int)clampf(value, 0.0f, (float)(ACOUSTICS_SLOT_COUNT - 1));
            continue;
        }
        if (strcmp(key, "atsel") == 0) {
            *v->aux_slot_selected = (int)clampf(value, 0.0f, (float)(ACOUSTICS_SLOT_COUNT - 1));
            continue;
        }
        int s = 0;
        int i = 0;
        if (sscanf(key, "fd%d", &s) == 1 && s >= 0 && s < ACOUSTICS_SLOT_COUNT) {
            v->fire_slot_defined[s] = (value >= 0.5f) ? 1u : 0u;
            continue;
        }
        if (sscanf(key, "td%d", &s) == 1 && s >= 0 && s < ACOUSTICS_SLOT_COUNT) {
            v->thr_slot_defined[s] = (value >= 0.5f) ? 1u : 0u;
            continue;
        }
        if (sscanf(key, "cfd%d", &s) == 1 && s >= 0 && s < ACOUSTICS_SLOT_COUNT) {
            v->enemy_slot_defined[s] = (value >= 0.5f) ? 1u : 0u;
            continue;
        }
        if (sscanf(key, "ctd%d", &s) == 1 && s >= 0 && s < ACOUSTICS_SLOT_COUNT) {
            v->exp_slot_defined[s] = (value >= 0.5f) ? 1u : 0u;
            continue;
        }
        if (sscanf(key, "ed%d", &s) == 1 && s >= 0 && s < ACOUSTICS_SLOT_COUNT) {
            v->shield_slot_defined[s] = (value >= 0.5f) ? 1u : 0u;
            continue;
        }
        if (sscanf(key, "atd%d", &s) == 1 && s >= 0 && s < ACOUSTICS_SLOT_COUNT) {
            v->aux_slot_defined[s] = (value >= 0.5f) ? 1u : 0u;
            continue;
        }
        if (sscanf(key, "fv%d_%d", &s, &i) == 2 && s >= 0 && s < ACOUSTICS_SLOT_COUNT && i >= 0 && i < 8) {
            v->fire_slots[s][i] = clampf(value, 0.0f, 1.0f);
            continue;
        }
        if (sscanf(key, "tv%d_%d", &s, &i) == 2 && s >= 0 && s < ACOUSTICS_SLOT_COUNT && i >= 0 && i < 6) {
            v->thr_slots[s][i] = clampf(value, 0.0f, 1.0f);
            continue;
        }
        if (sscanf(key, "cfv%d_%d", &s, &i) == 2 && s >= 0 && s < ACOUSTICS_SLOT_COUNT && i >= 0) {
            if (version >= 3) {
                if (i < 8) {
                    v->enemy_slots[s][i] = clampf(value, 0.0f, 1.0f);
                }
            } else {
                /* v2 enemy slot mapping:
                 * 0=level 1=pitch 2=attack 3=decay 4=noise 5=pan
                 * -> v3 keeps pitch/attack/decay; other modules retain defaults. */
                int ni = -1;
                if (i == 1) ni = ACOUST_COMBAT_ENEMY_PITCH;
                else if (i == 2) ni = ACOUST_COMBAT_ENEMY_ATTACK;
                else if (i == 3) ni = ACOUST_COMBAT_ENEMY_DECAY;
                if (ni >= 0) {
                    v->enemy_slots[s][ni] = clampf(value, 0.0f, 1.0f);
                }
            }
            continue;
        }
        if (sscanf(key, "ctv%d_%d", &s, &i) == 2 && s >= 0 && s < ACOUSTICS_SLOT_COUNT && i >= 0 && i < 8) {
            v->exp_slots[s][i] = clampf(value, 0.0f, 1.0f);
            continue;
        }
        if (sscanf(key, "ev%d_%d", &s, &i) == 2 && s >= 0 && s < ACOUSTICS_SLOT_COUNT && i >= 0 && i < 8) {
            v->shield_slots[s][i] = clampf(value, 0.0f, 1.0f);
            continue;
        }
        if (sscanf(key, "atv%d_%d", &s, &i) == 2 && s >= 0 && s < ACOUSTICS_SLOT_COUNT && i >= 0 && i < 8) {
            v->aux_slots[s][i] = clampf(value, 0.0f, 1.0f);
            continue;
        }
        if (sscanf(key, "cv%d", &i) == 1 && i >= 0) {
            if (version >= 3) {
                if (i < ACOUST_COMBAT_SLIDER_COUNT) {
                    v->combat_value_01[i] = clampf(value, 0.0f, 1.0f);
                }
            } else {
                /* v2 -> v3 remap for live combat page values. */
                int ni = -1;
                if (i == 1) ni = ACOUST_COMBAT_ENEMY_PITCH;
                else if (i == 2) ni = ACOUST_COMBAT_ENEMY_ATTACK;
                else if (i == 3) ni = ACOUST_COMBAT_ENEMY_DECAY;
                else if (i == 6) ni = ACOUST_COMBAT_EXP_LEVEL;
                else if (i == 7) ni = ACOUST_COMBAT_EXP_PITCH;
                else if (i == 8) ni = ACOUST_COMBAT_EXP_ATTACK;
                else if (i == 9) ni = ACOUST_COMBAT_EXP_DECAY;
                else if (i == 10) ni = ACOUST_COMBAT_EXP_NOISE;
                else if (i == 11) ni = ACOUST_COMBAT_EXP_FM_DEPTH;
                else if (i == 12) ni = ACOUST_COMBAT_EXP_FM_RATE;
                else if (i == 13) ni = ACOUST_COMBAT_EXP_PANW;
                if (ni >= 0) {
                    v->combat_value_01[ni] = clampf(value, 0.0f, 1.0f);
                }
            }
            continue;
        }
        if (sscanf(key, "evc%d", &i) == 1 && i >= 0 && i < ACOUST_EQUIP_SLIDER_COUNT) {
            v->equipment_value_01[i] = clampf(value, 0.0f, 1.0f);
            continue;
        }
    }
    fclose(f);
    acoustics_load_slot_to_current_view(v, 1, *v->fire_slot_selected);
    acoustics_load_slot_to_current_view(v, 0, *v->thr_slot_selected);
    acoustics_load_combat_slot_to_current_view(v, 1, *v->enemy_slot_selected);
    acoustics_load_combat_slot_to_current_view(v, 0, *v->exp_slot_selected);
    acoustics_load_equipment_slot_to_current_view(v, 1, *v->shield_slot_selected);
    acoustics_load_equipment_slot_to_current_view(v, 0, *v->aux_slot_selected);
    return 1;
}

void acoustics_apply_locked(acoustics_runtime_view* v) {
    if (!v || !v->value_01 || !v->combat_value_01 || !v->equipment_value_01 || !v->weapon_synth || !v->thruster_synth ||
        !v->enemy_fire_sound || !v->explosion_sound || !v->shield_sound || !v->aux_sound) {
        return;
    }
    const int fire_wave_idx = (int)floorf(clampf(v->value_01[ACOUST_FIRE_WAVE], 0.0f, 1.0f) * 4.0f + 0.5f);
    enum wtp_waveform_type fire_wave = (enum wtp_waveform_type)fire_wave_idx;
    if (fire_wave >= WTP_WT_TYPES) {
        fire_wave = WTP_WT_SAW;
    }
    wtp_set_waveform(v->weapon_synth, fire_wave);
    wtp_set_adsr_ms(
        v->weapon_synth,
        acoustics_value_to_display(ACOUST_FIRE_ATTACK, v->value_01[ACOUST_FIRE_ATTACK]),
        acoustics_value_to_display(ACOUST_FIRE_DECAY, v->value_01[ACOUST_FIRE_DECAY]),
        0.0f,
        80.0f
    );
    wtp_set_pitch_env(
        v->weapon_synth,
        acoustics_value_to_display(ACOUST_FIRE_SWEEP_ST, v->value_01[ACOUST_FIRE_SWEEP_ST]),
        0.0f,
        acoustics_value_to_display(ACOUST_FIRE_SWEEP_DECAY, v->value_01[ACOUST_FIRE_SWEEP_DECAY])
    );
    wtp_set_filter(
        v->weapon_synth,
        acoustics_value_to_display(ACOUST_FIRE_CUTOFF, v->value_01[ACOUST_FIRE_CUTOFF]),
        acoustics_value_to_display(ACOUST_FIRE_RESONANCE, v->value_01[ACOUST_FIRE_RESONANCE])
    );
    v->weapon_synth->gain = 0.40f;
    v->weapon_synth->clip_level = 0.92f;

    wtp_set_waveform(v->thruster_synth, WTP_WT_NOISE);
    wtp_set_adsr_ms(
        v->thruster_synth,
        acoustics_value_to_display(ACOUST_THR_ATTACK, v->value_01[ACOUST_THR_ATTACK]),
        30.0f,
        0.92f,
        acoustics_value_to_display(ACOUST_THR_RELEASE, v->value_01[ACOUST_THR_RELEASE])
    );
    wtp_set_filter(
        v->thruster_synth,
        acoustics_value_to_display(ACOUST_THR_CUTOFF, v->value_01[ACOUST_THR_CUTOFF]),
        acoustics_value_to_display(ACOUST_THR_RESONANCE, v->value_01[ACOUST_THR_RESONANCE])
    );
    v->thruster_synth->gain = acoustics_value_to_display(ACOUST_THR_LEVEL, v->value_01[ACOUST_THR_LEVEL]);
    v->thruster_synth->clip_level = 0.85f;

    v->enemy_fire_sound->level = 0.40f;
    v->enemy_fire_sound->waveform =
        acoustics_combat_value_to_display(ACOUST_COMBAT_ENEMY_WAVE, v->combat_value_01[ACOUST_COMBAT_ENEMY_WAVE]);
    v->enemy_fire_sound->pitch_hz =
        acoustics_combat_value_to_display(ACOUST_COMBAT_ENEMY_PITCH, v->combat_value_01[ACOUST_COMBAT_ENEMY_PITCH]);
    v->enemy_fire_sound->attack_ms =
        acoustics_combat_value_to_display(ACOUST_COMBAT_ENEMY_ATTACK, v->combat_value_01[ACOUST_COMBAT_ENEMY_ATTACK]);
    v->enemy_fire_sound->decay_ms =
        acoustics_combat_value_to_display(ACOUST_COMBAT_ENEMY_DECAY, v->combat_value_01[ACOUST_COMBAT_ENEMY_DECAY]);
    v->enemy_fire_sound->cutoff_hz =
        acoustics_combat_value_to_display(ACOUST_COMBAT_ENEMY_CUTOFF, v->combat_value_01[ACOUST_COMBAT_ENEMY_CUTOFF]);
    v->enemy_fire_sound->resonance =
        acoustics_combat_value_to_display(ACOUST_COMBAT_ENEMY_RESONANCE, v->combat_value_01[ACOUST_COMBAT_ENEMY_RESONANCE]);
    v->enemy_fire_sound->sweep_st =
        acoustics_combat_value_to_display(ACOUST_COMBAT_ENEMY_SWEEP_ST, v->combat_value_01[ACOUST_COMBAT_ENEMY_SWEEP_ST]);
    v->enemy_fire_sound->sweep_decay_ms =
        acoustics_combat_value_to_display(ACOUST_COMBAT_ENEMY_SWEEP_DECAY, v->combat_value_01[ACOUST_COMBAT_ENEMY_SWEEP_DECAY]);
    v->enemy_fire_sound->noise_mix = 0.0f;
    v->enemy_fire_sound->fm_depth_hz = 0.0f;
    v->enemy_fire_sound->fm_rate_hz = 0.0f;
    v->enemy_fire_sound->pan_width = 0.78f;

    v->explosion_sound->level =
        acoustics_combat_value_to_display(ACOUST_COMBAT_EXP_LEVEL, v->combat_value_01[ACOUST_COMBAT_EXP_LEVEL]);
    v->explosion_sound->waveform = 0.0f;
    v->explosion_sound->pitch_hz =
        acoustics_combat_value_to_display(ACOUST_COMBAT_EXP_PITCH, v->combat_value_01[ACOUST_COMBAT_EXP_PITCH]);
    v->explosion_sound->attack_ms =
        acoustics_combat_value_to_display(ACOUST_COMBAT_EXP_ATTACK, v->combat_value_01[ACOUST_COMBAT_EXP_ATTACK]);
    v->explosion_sound->decay_ms =
        acoustics_combat_value_to_display(ACOUST_COMBAT_EXP_DECAY, v->combat_value_01[ACOUST_COMBAT_EXP_DECAY]);
    v->explosion_sound->cutoff_hz = 0.0f;
    v->explosion_sound->resonance = 0.0f;
    v->explosion_sound->sweep_st = 0.0f;
    v->explosion_sound->sweep_decay_ms = 0.0f;
    v->explosion_sound->noise_mix =
        acoustics_combat_value_to_display(ACOUST_COMBAT_EXP_NOISE, v->combat_value_01[ACOUST_COMBAT_EXP_NOISE]);
    v->explosion_sound->fm_depth_hz =
        acoustics_combat_value_to_display(ACOUST_COMBAT_EXP_FM_DEPTH, v->combat_value_01[ACOUST_COMBAT_EXP_FM_DEPTH]);
    v->explosion_sound->fm_rate_hz =
        acoustics_combat_value_to_display(ACOUST_COMBAT_EXP_FM_RATE, v->combat_value_01[ACOUST_COMBAT_EXP_FM_RATE]);
    v->explosion_sound->pan_width =
        acoustics_combat_value_to_display(ACOUST_COMBAT_EXP_PANW, v->combat_value_01[ACOUST_COMBAT_EXP_PANW]);

    v->shield_sound->level =
        acoustics_equipment_value_to_display(ACOUST_EQUIP_SHIELD_LEVEL, v->equipment_value_01[ACOUST_EQUIP_SHIELD_LEVEL]);
    v->shield_sound->pitch_hz =
        acoustics_equipment_value_to_display(ACOUST_EQUIP_SHIELD_PITCH, v->equipment_value_01[ACOUST_EQUIP_SHIELD_PITCH]);
    v->shield_sound->attack_ms =
        acoustics_equipment_value_to_display(ACOUST_EQUIP_SHIELD_ATTACK, v->equipment_value_01[ACOUST_EQUIP_SHIELD_ATTACK]);
    v->shield_sound->decay_ms =
        acoustics_equipment_value_to_display(ACOUST_EQUIP_SHIELD_RELEASE, v->equipment_value_01[ACOUST_EQUIP_SHIELD_RELEASE]);
    v->shield_sound->noise_mix =
        acoustics_equipment_value_to_display(ACOUST_EQUIP_SHIELD_NOISE, v->equipment_value_01[ACOUST_EQUIP_SHIELD_NOISE]);
    v->shield_sound->fm_depth_hz =
        acoustics_equipment_value_to_display(ACOUST_EQUIP_SHIELD_FM_DEPTH, v->equipment_value_01[ACOUST_EQUIP_SHIELD_FM_DEPTH]);
    v->shield_sound->fm_rate_hz =
        acoustics_equipment_value_to_display(ACOUST_EQUIP_SHIELD_FM_RATE, v->equipment_value_01[ACOUST_EQUIP_SHIELD_FM_RATE]);
    v->shield_sound->cutoff_hz =
        acoustics_equipment_value_to_display(ACOUST_EQUIP_SHIELD_CUTOFF, v->equipment_value_01[ACOUST_EQUIP_SHIELD_CUTOFF]);

    v->aux_sound->level =
        acoustics_equipment_value_to_display(ACOUST_EQUIP_AUX_LEVEL, v->equipment_value_01[ACOUST_EQUIP_AUX_LEVEL]);
    v->aux_sound->pitch_hz =
        acoustics_equipment_value_to_display(ACOUST_EQUIP_AUX_PITCH, v->equipment_value_01[ACOUST_EQUIP_AUX_PITCH]);
    v->aux_sound->attack_ms =
        acoustics_equipment_value_to_display(ACOUST_EQUIP_AUX_ATTACK, v->equipment_value_01[ACOUST_EQUIP_AUX_ATTACK]);
    v->aux_sound->decay_ms =
        acoustics_equipment_value_to_display(ACOUST_EQUIP_AUX_RELEASE, v->equipment_value_01[ACOUST_EQUIP_AUX_RELEASE]);
    v->aux_sound->noise_mix =
        acoustics_equipment_value_to_display(ACOUST_EQUIP_AUX_NOISE, v->equipment_value_01[ACOUST_EQUIP_AUX_NOISE]);
    v->aux_sound->fm_depth_hz =
        acoustics_equipment_value_to_display(ACOUST_EQUIP_AUX_FM_DEPTH, v->equipment_value_01[ACOUST_EQUIP_AUX_FM_DEPTH]);
    v->aux_sound->fm_rate_hz =
        acoustics_equipment_value_to_display(ACOUST_EQUIP_AUX_FM_RATE, v->equipment_value_01[ACOUST_EQUIP_AUX_FM_RATE]);
    v->aux_sound->cutoff_hz =
        acoustics_equipment_value_to_display(ACOUST_EQUIP_AUX_CUTOFF, v->equipment_value_01[ACOUST_EQUIP_AUX_CUTOFF]);
}

int audio_spatial_enqueue_ring(
    atomic_uint* write_idx,
    atomic_uint* read_idx,
    audio_spatial_event* queue,
    uint32_t cap,
    uint8_t type,
    float pan,
    float gain
) {
    if (!write_idx || !read_idx || !queue || cap < 2u) {
        return 0;
    }
    const uint32_t w = atomic_load_explicit(write_idx, memory_order_relaxed);
    const uint32_t r = atomic_load_explicit(read_idx, memory_order_acquire);
    const uint32_t next = (w + 1u) % cap;
    if (next == r) {
        return 0;
    }
    audio_spatial_event* e = &queue[w];
    e->type = type;
    e->pan = clampf(pan, -1.0f, 1.0f);
    e->gain = clampf(gain, 0.0f, 2.0f);
    atomic_store_explicit(write_idx, next, memory_order_release);
    return 1;
}

int audio_spatial_dequeue_ring(
    atomic_uint* read_idx,
    atomic_uint* write_idx,
    const audio_spatial_event* queue,
    uint32_t cap,
    audio_spatial_event* out
) {
    if (!read_idx || !write_idx || !queue || cap < 2u || !out) {
        return 0;
    }
    const uint32_t r = atomic_load_explicit(read_idx, memory_order_relaxed);
    const uint32_t w = atomic_load_explicit(write_idx, memory_order_acquire);
    if (r == w) {
        return 0;
    }
    *out = queue[r];
    atomic_store_explicit(read_idx, (r + 1u) % cap, memory_order_release);
    return 1;
}

void audio_spawn_combat_voice(
    audio_combat_voice* voices,
    int voice_count,
    uint32_t* rng_state,
    const audio_spatial_event* ev,
    const combat_sound_params* enemy_fire_sound,
    const combat_sound_params* explosion_sound
) {
    if (!voices || voice_count <= 0 || !rng_state || !ev || !enemy_fire_sound || !explosion_sound) {
        return;
    }

    const int type = (int)ev->type;
    const combat_sound_params* p = NULL;
    int limit = 0;
    float pitch_scale = 1.0f;
    if (type == GAME_AUDIO_EVENT_ENEMY_FIRE) {
        p = enemy_fire_sound;
        limit = 14;
    } else if (type == GAME_AUDIO_EVENT_SEARCHLIGHT_FIRE) {
        p = enemy_fire_sound;
        limit = 14;
        pitch_scale = 0.5f; /* One octave below standard enemy gun. */
    } else if (type == GAME_AUDIO_EVENT_EXPLOSION) {
        p = explosion_sound;
        limit = 10;
    } else {
        return;
    }

    int active_same = 0;
    int free_i = -1;
    int steal_i = 0;
    float oldest = -1.0f;
    for (int i = 0; i < voice_count; ++i) {
        audio_combat_voice* v = &voices[i];
        if (!v->active) {
            if (free_i < 0) {
                free_i = i;
            }
            continue;
        }
        if ((int)v->type == type) {
            active_same += 1;
            if (v->time_s > oldest) {
                oldest = v->time_s;
                steal_i = i;
            }
        }
    }
    if (active_same >= limit && free_i < 0) {
        free_i = steal_i;
    }
    if (free_i < 0) {
        for (int i = 0; i < voice_count; ++i) {
            if (voices[i].time_s > oldest) {
                oldest = voices[i].time_s;
                free_i = i;
            }
        }
    }
    if (free_i < 0) {
        return;
    }

    audio_combat_voice* v = &voices[free_i];
    const float jitter = (audio_rand01_from_state(rng_state) - 0.5f) *
                         ((type == GAME_AUDIO_EVENT_EXPLOSION) ? 0.18f : 0.08f);
    v->active = 1;
    v->type = (uint8_t)type;
    v->waveform = (uint8_t)clampf(floorf(p->waveform + 0.5f), 0.0f, 4.0f);
    v->pan = clampf(ev->pan * p->pan_width, -1.0f, 1.0f);
    v->gain = clampf(p->level * ev->gain, 0.0f, 1.2f);
    v->phase = audio_rand01_from_state(rng_state) * 6.28318530718f;
    v->freq_hz = p->pitch_hz * pitch_scale * (1.0f + jitter);
    v->attack_s = fmaxf(0.0001f, p->attack_ms * 0.001f);
    v->decay_s = fmaxf(0.005f, p->decay_ms * 0.001f);
    v->noise_mix = clampf(p->noise_mix, 0.0f, 1.0f);
    v->fm_depth_hz = (type == GAME_AUDIO_EVENT_EXPLOSION) ? fmaxf(0.0f, p->fm_depth_hz) : 0.0f;
    v->fm_rate_hz = (type == GAME_AUDIO_EVENT_EXPLOSION) ? fmaxf(0.0f, p->fm_rate_hz) : 0.0f;
    v->fm_phase = audio_rand01_from_state(rng_state) * 6.28318530718f;
    v->cutoff_hz = fmaxf(40.0f, p->cutoff_hz);
    v->resonance = clampf(p->resonance, 0.0f, 0.99f);
    v->sweep_st = p->sweep_st;
    v->sweep_decay_s = fmaxf(0.002f, p->sweep_decay_ms * 0.001f);
    v->filter_lp = 0.0f;
    v->filter_bp = 0.0f;
    v->time_s = 0.0f;
}

void audio_render_combat_voices(
    audio_combat_voice* voices,
    int voice_count,
    uint32_t* rng_state,
    float sample_rate,
    float* left,
    float* right,
    uint32_t frame_count
) {
    if (!voices || voice_count <= 0 || !rng_state || sample_rate <= 0.0f ||
        !left || !right || frame_count == 0u) {
        return;
    }
    for (int vi = 0; vi < voice_count; ++vi) {
        audio_combat_voice* v = &voices[vi];
        if (!v->active) {
            continue;
        }
        for (uint32_t i = 0; i < frame_count; ++i) {
            const float t = v->time_s;
            const float total_s = v->attack_s + v->decay_s;
            if (t >= total_s) {
                v->active = 0;
                break;
            }
            float env = 0.0f;
            if (t < v->attack_s) {
                env = t / v->attack_s;
            } else {
                env = 1.0f - (t - v->attack_s) / v->decay_s;
            }
            if (env < 0.0f) {
                env = 0.0f;
            }

            float freq = v->freq_hz;
            if (v->type == GAME_AUDIO_EVENT_EXPLOSION) {
                const float down = clampf((t / (v->decay_s + v->attack_s + 0.001f)), 0.0f, 1.0f);
                freq *= (1.0f - 0.55f * down);
                if (v->fm_depth_hz > 0.0f) {
                    const float fm = sinf(v->fm_phase) * v->fm_depth_hz * (0.35f + 0.65f * env);
                    freq = fmaxf(8.0f, freq + fm);
                }
            } else if (v->type == GAME_AUDIO_EVENT_ENEMY_FIRE || v->type == GAME_AUDIO_EVENT_SEARCHLIGHT_FIRE) {
                const float st = v->sweep_st * expf(-t / fmaxf(v->sweep_decay_s, 0.002f));
                freq *= powf(2.0f, st / 12.0f);
            }
            const float step = 2.0f * 3.14159265358979323846f * freq / sample_rate;
            float tone = osc_sample(v->waveform, v->phase, rng_state);
            const float noise = audio_rand01_from_state(rng_state) * 2.0f - 1.0f;
            if (v->type == GAME_AUDIO_EVENT_ENEMY_FIRE || v->type == GAME_AUDIO_EVENT_SEARCHLIGHT_FIRE) {
                /* 2-pole state-variable low-pass for enemy fire synth parity with player fire chain. */
                const float f = clampf(2.0f * sinf(3.14159265358979323846f * v->cutoff_hz / sample_rate), 0.0f, 0.99f);
                const float q = 2.0f - 1.9f * v->resonance;
                const float hp = tone - v->filter_lp - q * v->filter_bp;
                v->filter_bp += f * hp;
                v->filter_lp += f * v->filter_bp;
                tone = v->filter_lp;
            }
            const float s = ((1.0f - v->noise_mix) * tone + v->noise_mix * noise) * env * v->gain;
            const float pan = clampf(v->pan, -1.0f, 1.0f);
            const float l_gain = sqrtf(0.5f * (1.0f - pan));
            const float r_gain = sqrtf(0.5f * (1.0f + pan));
            left[i] += s * l_gain;
            right[i] += s * r_gain;

            v->phase += step;
            if (v->phase > 6.28318530718f) {
                v->phase -= 6.28318530718f;
            }
            v->fm_phase += 2.0f * 3.14159265358979323846f * v->fm_rate_hz / sample_rate;
            if (v->fm_phase > 6.28318530718f) {
                v->fm_phase -= 6.28318530718f;
            }
            v->time_s += 1.0f / sample_rate;
        }
    }
}

void audio_queue_teletype_beep(wtp_ringbuffer_t* rb, int sample_rate, float freq_hz, float dur_s, float amp) {
    if (!rb) {
        return;
    }
    int n = (int)(dur_s * (float)sample_rate);
    if (n < 64) {
        n = 64;
    }
    if (n > AUDIO_MAX_BEEP_SAMPLES) {
        n = AUDIO_MAX_BEEP_SAMPLES;
    }
    float samples[AUDIO_MAX_BEEP_SAMPLES];
    float phase = 0.0f;
    const float step = 2.0f * 3.14159265358979323846f * freq_hz / (float)sample_rate;
    for (int i = 0; i < n; ++i) {
        const float t = (float)i / (float)(n - 1);
        const float env = (1.0f - t) * (1.0f - t);
        samples[i] = sinf(phase) * amp * env;
        phase += step;
    }
    (void)wtp_ringbuffer_write(rb, samples, (uint32_t)n);
}
