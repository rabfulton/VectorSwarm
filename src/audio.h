#ifndef V_TYPE_AUDIO_H
#define V_TYPE_AUDIO_H

#include "render.h"
#include "wavetable_poly_synth_lib.h"

#include <stdint.h>
#include <stdatomic.h>

#define AUDIO_SPATIAL_EVENT_CAP 256
#define AUDIO_COMBAT_VOICE_COUNT 24
#define AUDIO_MAX_BEEP_SAMPLES 4096

typedef struct audio_spatial_event {
    uint8_t type;
    float pan;
    float gain;
} audio_spatial_event;

typedef struct audio_combat_voice {
    int active;
    uint8_t type;
    uint8_t waveform;
    float pan;
    float gain;
    float phase;
    float freq_hz;
    float attack_s;
    float decay_s;
    float noise_mix;
    float fm_depth_hz;
    float fm_rate_hz;
    float fm_phase;
    float cutoff_hz;
    float resonance;
    float sweep_st;
    float sweep_decay_s;
    float filter_lp;
    float filter_bp;
    float time_s;
} audio_combat_voice;

typedef struct combat_sound_params {
    float level;
    float waveform;
    float pitch_hz;
    float attack_ms;
    float decay_ms;
    float cutoff_hz;
    float resonance;
    float sweep_st;
    float sweep_decay_ms;
    float noise_mix;
    float fm_depth_hz;
    float fm_rate_hz;
    float pan_width;
} combat_sound_params;

enum acoustics_slider_id {
    ACOUST_FIRE_WAVE = 0,
    ACOUST_FIRE_PITCH = 1,
    ACOUST_FIRE_ATTACK = 2,
    ACOUST_FIRE_DECAY = 3,
    ACOUST_FIRE_CUTOFF = 4,
    ACOUST_FIRE_RESONANCE = 5,
    ACOUST_FIRE_SWEEP_ST = 6,
    ACOUST_FIRE_SWEEP_DECAY = 7,
    ACOUST_THR_LEVEL = 8,
    ACOUST_THR_PITCH = 9,
    ACOUST_THR_ATTACK = 10,
    ACOUST_THR_RELEASE = 11,
    ACOUST_THR_CUTOFF = 12,
    ACOUST_THR_RESONANCE = 13
};

enum acoustics_combat_slider_id {
    ACOUST_COMBAT_ENEMY_WAVE = 0,
    ACOUST_COMBAT_ENEMY_PITCH = 1,
    ACOUST_COMBAT_ENEMY_ATTACK = 2,
    ACOUST_COMBAT_ENEMY_DECAY = 3,
    ACOUST_COMBAT_ENEMY_CUTOFF = 4,
    ACOUST_COMBAT_ENEMY_RESONANCE = 5,
    ACOUST_COMBAT_ENEMY_SWEEP_ST = 6,
    ACOUST_COMBAT_ENEMY_SWEEP_DECAY = 7,
    ACOUST_COMBAT_EXP_LEVEL = 8,
    ACOUST_COMBAT_EXP_PITCH = 9,
    ACOUST_COMBAT_EXP_ATTACK = 10,
    ACOUST_COMBAT_EXP_DECAY = 11,
    ACOUST_COMBAT_EXP_NOISE = 12,
    ACOUST_COMBAT_EXP_FM_DEPTH = 13,
    ACOUST_COMBAT_EXP_FM_RATE = 14,
    ACOUST_COMBAT_EXP_PANW = 15,
    ACOUST_COMBAT_SLIDER_COUNT = 16
};

float audio_rand01_from_state(uint32_t* state);

float acoustics_value_to_display(int id, float t01);
float acoustics_value_to_ui_display(int id, float t01);
float acoustics_combat_value_to_display(int id, float t01);
float acoustics_combat_value_to_ui_display(int id, float t01);
void acoustics_defaults_init(float out_values_01[ACOUSTICS_SLIDER_COUNT]);
void acoustics_combat_defaults_init(float out_values_01[ACOUST_COMBAT_SLIDER_COUNT]);
const char* resolve_acoustics_slots_path(void);

typedef struct acoustics_slot_view {
    int* fire_slot_selected;
    int* thr_slot_selected;
    int* enemy_slot_selected;
    int* exp_slot_selected;
    uint8_t* fire_slot_defined;
    uint8_t* thr_slot_defined;
    uint8_t* enemy_slot_defined;
    uint8_t* exp_slot_defined;
    float (*fire_slots)[8];
    float (*thr_slots)[6];
    float (*enemy_slots)[8];
    float (*exp_slots)[8];
    float* value_01;
    float* combat_value_01;
} acoustics_slot_view;

typedef struct acoustics_runtime_view {
    float* value_01;
    float* combat_value_01;
    wtp_instrument_t* weapon_synth;
    wtp_instrument_t* thruster_synth;
    combat_sound_params* enemy_fire_sound;
    combat_sound_params* explosion_sound;
} acoustics_runtime_view;

void acoustics_apply_locked(acoustics_runtime_view* v);
void acoustics_slot_defaults_view(acoustics_slot_view* v);
void acoustics_capture_current_to_selected_slot_view(acoustics_slot_view* v, int is_fire);
void acoustics_capture_current_to_selected_combat_slot_view(acoustics_slot_view* v, int is_enemy);
void acoustics_load_slot_to_current_view(acoustics_slot_view* v, int is_fire, int slot_idx);
void acoustics_load_combat_slot_to_current_view(acoustics_slot_view* v, int is_enemy, int slot_idx);
int acoustics_save_slots_view(const acoustics_slot_view* v, const char* path);
int acoustics_load_slots_view(acoustics_slot_view* v, const char* path);

int audio_spatial_enqueue_ring(
    atomic_uint* write_idx,
    atomic_uint* read_idx,
    audio_spatial_event* queue,
    uint32_t cap,
    uint8_t type,
    float pan,
    float gain
);

int audio_spatial_dequeue_ring(
    atomic_uint* read_idx,
    atomic_uint* write_idx,
    const audio_spatial_event* queue,
    uint32_t cap,
    audio_spatial_event* out
);

void audio_spawn_combat_voice(
    audio_combat_voice* voices,
    int voice_count,
    uint32_t* rng_state,
    const audio_spatial_event* ev,
    const combat_sound_params* enemy_fire_sound,
    const combat_sound_params* explosion_sound
);

void audio_render_combat_voices(
    audio_combat_voice* voices,
    int voice_count,
    uint32_t* rng_state,
    float sample_rate,
    float* left,
    float* right,
    uint32_t frame_count
);

void audio_queue_teletype_beep(wtp_ringbuffer_t* rb, int sample_rate, float freq_hz, float dur_s, float amp);

#endif
