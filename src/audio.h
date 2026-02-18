#ifndef V_TYPE_AUDIO_H
#define V_TYPE_AUDIO_H

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
    float time_s;
} audio_combat_voice;

typedef struct combat_sound_params {
    float level;
    float pitch_hz;
    float attack_ms;
    float decay_ms;
    float noise_mix;
    float fm_depth_hz;
    float fm_rate_hz;
    float pan_width;
} combat_sound_params;

float audio_rand01_from_state(uint32_t* state);

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
