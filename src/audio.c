#include "audio.h"
#include "game.h"

#include <math.h>
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
            }
            const float step = 2.0f * 3.14159265358979323846f * freq / sample_rate;
            const float tone = sinf(v->phase);
            const float noise = audio_rand01_from_state(rng_state) * 2.0f - 1.0f;
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
