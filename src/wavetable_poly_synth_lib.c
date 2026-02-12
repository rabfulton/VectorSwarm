// Extracted standalone wavetable polyphonic synth library
// Origin: rableton_live project DSP code (voice/instrument, ADSR, SVF filter)
//
// Integration quick start:
// 1. Add this file to your build and include stdatomic support (`-std=c11`).
// 2. Create one `wtp_instrument_t` per synth instance.
// 3. Call `wtp_instrument_init(&instr, sample_rate, frame_size, num_voices, wavetable_size)`.
// 4. In your audio callback, call `wtp_render_instrument(&instr, out_mono, frame_size)` once per block.
// 5. Route note events:
//    - Note on:  `wtp_note_on_midi(&instr, midi_note)` or `wtp_note_on_hz(&instr, note_id, freq_hz)`
//    - Note off: `wtp_note_off(&instr, note_id)`
// 6. Set voice/filter tone:
//    - Waveform: `wtp_set_waveform(&instr, WTP_WT_SINE|WTP_WT_SAW|WTP_WT_TRIANGLE|WTP_WT_SQUARE)`
//    - ADSR:     `wtp_set_adsr_ms(&instr, attack_ms, decay_ms, sustain_0_to_1, release_ms)`
//    - Filter:   `wtp_set_filter(&instr, cutoff_hz, resonance)`
// 7. On shutdown, call `wtp_instrument_free(&instr)`.
//
// Dial smoothing for glitch-free filter updates:
// - This file includes the same one-pole parameter smoothing approach used in the original project.
// - Keep two `wtp_parameter_t` values in your app (cutoff + resonance).
// - Init once:
//     `wtp_param_smooth_init(&cutoff_sm, 16, sample_rate, 1000.0f);`
//     `wtp_param_smooth_init(&res_sm,    16, sample_rate, 0.2f);`
// - On each control update (or audio block), call:
//     `wtp_set_filter_from_dials_smoothed(&instr, &cutoff_sm, &res_sm, cutoffDial01, resDial01, frame_size);`
//   where `cutoffDial01` and `resDial01` are normalized dial values.
//
// Notes:
// - Output is mono float. Pan/mix to stereo in host code.
// - Wavetable size is expected to be a power-of-two for fast masked indexing.
// - This file intentionally contains no GUI or platform/audio backend code.

#include <math.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>

#ifndef WTPS_PI
#define WTPS_PI 3.141592653f
#endif

#ifndef WTPS_DEFAULT_SAMPLE_RATE
#define WTPS_DEFAULT_SAMPLE_RATE 48000u
#endif

#ifndef WTPS_DEFAULT_WAVETABLE_SIZE
#define WTPS_DEFAULT_WAVETABLE_SIZE 8192u
#endif

typedef struct {
    float a;
    float b;
    float z;
    float value;
} wtp_parameter_t;

typedef struct {
    // coefficients
    wtp_parameter_t b0;
    wtp_parameter_t b1;
    wtp_parameter_t b2;
    wtp_parameter_t a1;
    wtp_parameter_t a2;
    // state
    float xn1;
    float xn2;
    float yn1;
    float yn2;
} wtp_filter_state_t;

typedef struct {
    wtp_filter_state_t state[2];
    atomic_int active_filter;
    float cutoff;
    float resonance;
    int lowpass_mode; // 1 = lowpass(v1), 0 = bandpass(v2)
} wtp_filter_t;

typedef struct {
    uint32_t attack_time;
    uint32_t decay_time;
    uint32_t sustain_time;
    uint32_t release_time;
    float sustain_level;
    float attack_level;
} wtp_envelope_t;

typedef struct {
    float *samples;
    uint32_t length; // expected power-of-two for masked indexing path
} wtp_chunk_t;

enum wtp_waveform_type { WTP_WT_TRIANGLE, WTP_WT_SAW, WTP_WT_SINE, WTP_WT_SQUARE, WTP_WT_NOISE, WTP_WT_TYPES };
enum wtp_voice_state { WTP_VOICE_OFF, WTP_NOTE_OFF, WTP_NOTE_ON };

typedef struct {
    enum wtp_voice_state state;
    wtp_chunk_t *source;
    float freq;
    float base_freq;
    float current_vol;
    uint32_t release_timer;
    uint32_t phase;
    uint32_t index;
    int32_t note_id;
    float *buffer;
} wtp_voice_t;

typedef struct {
    uint32_t sample_rate;
    uint32_t frame_size;
    uint32_t wavetable_size;
    uint32_t num_voices;
    wtp_voice_t *voices;
    wtp_envelope_t env;
    wtp_filter_t filter;

    wtp_chunk_t wavetables[WTP_WT_TYPES];
    uint32_t active_wave;
    float pitch_env_amount_st;
    uint32_t pitch_env_decay_time;
    uint32_t pitch_env_attack_time;

    float gain;
    float clip_level;
} wtp_instrument_t;

typedef struct {
    uint32_t sample_rate;
    uint32_t frame_size;
    uint32_t num_voices;
    uint32_t wavetable_size;
    float gain;
    float clip_level;
    enum wtp_waveform_type waveform;
    // ADSR values are milliseconds except sustain in [0..1].
    float attack_ms;
    float decay_ms;
    float sustain_level;
    float release_ms;
    float pitch_env_amount_st;
    float pitch_env_attack_ms;
    float pitch_env_decay_ms;
    float filter_cutoff_hz;
    float filter_resonance;
    int filter_lowpass_mode;
} wtp_config_t;

typedef struct {
    uint32_t version;
    enum wtp_waveform_type waveform;
    float gain;
    float clip_level;
    float attack_ms;
    float decay_ms;
    float sustain_level;
    float release_ms;
    float pitch_env_amount_st;
    float pitch_env_attack_ms;
    float pitch_env_decay_ms;
    float filter_cutoff_hz;
    float filter_resonance;
    int filter_lowpass_mode;
} wtp_preset_t;

#define WTP_PRESET_VERSION 1u

// Optional SPSC float ringbuffer for host/audio transport.
// Capacity must be a power-of-two (sample slots, not frames).
typedef struct {
    float *data;
    uint32_t capacity;
    uint32_t mask;
    atomic_uint write_idx;
    atomic_uint read_idx;
} wtp_ringbuffer_t;

static void wtp_parameter_smooth_init(wtp_parameter_t *p, uint32_t ms, uint32_t sample_rate) {
    p->a = expf(-2.0f * WTPS_PI / (ms * 0.001f * (float)sample_rate));
    p->b = 1.0f - p->a;
    p->z = 0.0f;
    p->value = 0.0f;
}

static void wtp_parameter_smooth_set(wtp_parameter_t *p, float new_value) {
    p->z = new_value;
    p->value = new_value;
}

static float wtp_parameter_smooth(wtp_parameter_t *p) {
    p->z = p->value * p->b + p->z * p->a;
    return p->z;
}

float wtp_clampf(float x, float lo, float hi) {
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

// Extracted from the original parameter_smooth_* path used for click-free parameter transitions.
void wtp_param_smooth_init(wtp_parameter_t *p, uint32_t ms, uint32_t sample_rate, float initial_value) {
    if (!p) return;
    if (!sample_rate) sample_rate = WTPS_DEFAULT_SAMPLE_RATE;
    if (!ms) ms = 1;
    wtp_parameter_smooth_init(p, ms, sample_rate);
    wtp_parameter_smooth_set(p, initial_value);
}

void wtp_param_smooth_set_target(wtp_parameter_t *p, float target_value) {
    if (!p) return;
    p->value = target_value;
}

float wtp_param_smooth_tick(wtp_parameter_t *p) {
    if (!p) return 0.0f;
    return wtp_parameter_smooth(p);
}

// Same dial mapping that gui_set_cutoff() used:
// hz = 100 * exp(log(10000/100) * dial_norm), where dial_norm is [0..1].
float wtp_cutoff_hz_from_dial(float dial_norm) {
    dial_norm = wtp_clampf(dial_norm, 0.0f, 1.0f);
    return 100.0f * expf(logf(10000.0f / 100.0f) * dial_norm);
}

// Resonance dial in the original UI was effectively [0.1..1.0].
float wtp_resonance_from_dial(float dial_norm) {
    return wtp_clampf(dial_norm, 0.1f, 1.0f);
}

static void wtp_filter_reset(wtp_filter_state_t *f, uint32_t sample_rate) {
    const uint32_t smooth_time_ms = 128;
    f->xn1 = 0.0f;
    f->xn2 = 0.0f;
    f->yn1 = 0.0f;
    f->yn2 = 0.0f;
    wtp_parameter_smooth_init(&f->b0, smooth_time_ms, sample_rate);
    wtp_parameter_smooth_init(&f->b1, smooth_time_ms, sample_rate);
    wtp_parameter_smooth_init(&f->b2, smooth_time_ms, sample_rate);
    wtp_parameter_smooth_init(&f->a1, smooth_time_ms, sample_rate);
    wtp_parameter_smooth_init(&f->a2, smooth_time_ms, sample_rate);
}

static void wtp_filter_svf_init(wtp_filter_state_t *f, float freq, float res, uint32_t sample_rate) {
    float g = tanf(WTPS_PI * freq / (float)sample_rate);
    float k = 2.0f - 2.0f * res;
    float a1 = 1.0f / (1.0f + g * (g + k));
    float a2 = g * a1;
    float a3 = g * a2;

    wtp_parameter_smooth_set(&f->b0, a1);
    wtp_parameter_smooth_set(&f->b1, a2);
    wtp_parameter_smooth_set(&f->b2, a3);
    wtp_parameter_smooth_set(&f->a1, g);
    wtp_parameter_smooth_set(&f->a2, k);
}

static void wtp_filter_update(wtp_filter_t *f, float freq, float res, uint32_t sample_rate) {
    int active = atomic_load_explicit(&f->active_filter, memory_order_acquire);
    if (active) {
        wtp_filter_svf_init(&f->state[0], freq, res, sample_rate);
    } else {
        wtp_filter_svf_init(&f->state[1], freq, res, sample_rate);
    }

    // preserve continuity between states, then swap active state
    if (active) {
        f->state[0].yn1 = f->state[1].yn1;
        f->state[0].yn2 = f->state[1].yn2;
        atomic_store_explicit(&f->active_filter, 0, memory_order_release);
    } else {
        f->state[1].yn1 = f->state[0].yn1;
        f->state[1].yn2 = f->state[0].yn2;
        atomic_store_explicit(&f->active_filter, 1, memory_order_release);
    }

    f->cutoff = freq;
    f->resonance = res;
}

static void wtp_filter_process(wtp_filter_t *filter, float *samples, uint32_t count) {
    wtp_filter_state_t *f = &filter->state[atomic_load_explicit(&filter->active_filter, memory_order_acquire)];
    float yn1 = f->yn1;
    float yn2 = f->yn2;

    for (uint32_t i = 0; i < count; ++i) {
        float a1 = wtp_parameter_smooth(&f->b0);
        float a2 = wtp_parameter_smooth(&f->b1);
        float a3 = wtp_parameter_smooth(&f->b2);

        float v3 = samples[i] - yn2;
        float v1 = a1 * yn1 + a2 * v3;
        float v2 = yn2 + a2 * yn1 + a3 * v3;

        yn1 = 2.0f * v1 - yn1;
        yn2 = 2.0f * v2 - yn2;

        samples[i] = filter->lowpass_mode ? v1 : v2;
    }

    f->yn1 = yn1;
    f->yn2 = yn2;
}

static float *wtp_sine_wavetable_init(uint32_t samples) {
    float *sd = (float *)malloc(sizeof(float) * samples);
    if (!sd) return NULL;
    float inc = (2.0f * WTPS_PI) / (float)samples;
    for (uint32_t i = 0; i < samples; ++i) sd[i] = sinf(inc * (float)i);
    return sd;
}

static float *wtp_tri_wavetable_init(uint32_t samples) {
    float *sd = (float *)malloc(sizeof(float) * samples);
    if (!sd) return NULL;
    float inc = (2.0f * WTPS_PI) / (float)samples;
    for (uint32_t i = 0; i < samples; ++i) sd[i] = (2.0f * asinf(sinf(inc * (float)i))) / WTPS_PI;
    return sd;
}

static float *wtp_square_wavetable_init(uint32_t samples, int harmonics) {
    float *sd = (float *)malloc(sizeof(float) * samples);
    if (!sd) return NULL;
    float inc = (2.0f * WTPS_PI) / (float)samples;
    for (uint32_t i = 0; i < samples; ++i) {
        float v = 0.0f;
        for (int j = 1; j < harmonics; j += 2) v += sinf((float)j * inc * (float)i) / (float)j;
        sd[i] = v;
    }
    return sd;
}

static float *wtp_saw_wavetable_init(uint32_t samples, int harmonics) {
    float *sd = (float *)malloc(sizeof(float) * samples);
    if (!sd) return NULL;
    float inc = (2.0f * WTPS_PI) / (float)samples;
    for (uint32_t i = 0; i < samples; ++i) {
        float v = 0.0f;
        for (int j = 1; j < harmonics; ++j) v += sinf((float)j * inc * (float)i) / (float)j;
        sd[i] = v * 0.52f;
    }
    return sd;
}

static float *wtp_noise_wavetable_init(uint32_t samples) {
    float *sd = (float *)malloc(sizeof(float) * samples);
    if (!sd) return NULL;
    for (uint32_t i = 0; i < samples; ++i) {
        float n = (float)rand() / (float)RAND_MAX;
        n = n * 2.0f - 1.0f;
        sd[i] = n * 0.8f;
    }
    return sd;
}

static void wtp_gen_wavetable_freq(wtp_voice_t *v, uint32_t count, uint32_t sample_rate) {
    float *buf = v->buffer;
    float *wt = v->source->samples;
    uint32_t table_size = v->source->length;
    uint32_t mask = table_size - 1;
    float scan_rate = v->freq * ((float)table_size / (float)sample_rate);
    uint32_t table_index = 0;
    uint32_t table_offset = v->phase;

    for (uint32_t i = 0; i < count; ++i) {
        table_index = (uint32_t)((float)i * scan_rate) + table_offset;
        table_index &= mask;
        buf[i] = wt[table_index];
    }
    v->phase = (uint32_t)((float)table_index + scan_rate) & mask;
}

static void wtp_apply_envelope(wtp_voice_t *v, const wtp_envelope_t *e, float *dst, uint32_t count) {
    uint32_t index = v->index;
    float vol = 0.0f;
    float at = (float)e->attack_time;
    float dt = (float)e->decay_time;
    float st = at + (float)e->decay_time;
    float rt = (float)e->release_time;
    float decay_delta = e->attack_level - e->sustain_level;
    float sustain_level = e->sustain_level;
    int kill_flag = 0;

    switch (v->state) {
        case WTP_NOTE_ON:
            for (uint32_t i = 0; i < count; ++i) {
                if ((float)index >= st) {
                    vol = sustain_level;
                } else if ((float)index > at && dt > 0.0f) {
                    vol = e->attack_level - ((float)index - at) * decay_delta / dt;
                } else if (at > 0.0f) {
                    vol = e->attack_level * (float)index / at;
                }
                dst[i] *= vol;
                ++index;
            }
            v->current_vol = vol;
            break;

        case WTP_NOTE_OFF:
            for (uint32_t i = 0; i < count; ++i) {
                if (v->release_timer) {
                    if (rt > 0.0f) {
                        vol = v->current_vol - (rt - (float)v->release_timer) * (v->current_vol / rt);
                    } else {
                        vol = 0.0f;
                    }
                    if (vol < 0.0001f) vol = 0.0f;
                    --v->release_timer;
                    dst[i] *= vol;
                } else {
                    kill_flag = 1;
                    dst[i] = 0.0f;
                }
            }
            if (kill_flag) v->state = WTP_VOICE_OFF;
            break;

        default:
            break;
    }
}

static wtp_voice_t *wtp_get_free_voice(wtp_instrument_t *instr) {
    for (uint32_t i = 0; i < instr->num_voices; ++i) {
        if (instr->voices[i].state == WTP_VOICE_OFF) return &instr->voices[i];
    }

    // Voice steal: oldest note by sample index.
    wtp_voice_t *oldest = &instr->voices[0];
    uint32_t oldest_note = oldest->index;
    for (uint32_t i = 1; i < instr->num_voices; ++i) {
        if (instr->voices[i].index >= oldest_note) {
            oldest = &instr->voices[i];
            oldest_note = oldest->index;
        }
    }
    return oldest;
}

static float wtp_clip(float x, float level) {
    if (x > level) return level;
    if (x < -level) return -level;
    return x;
}

void wtp_white_noise(float *dest, uint32_t num_samples, float vol) {
    if (!dest) return;
    for (uint32_t i = 0; i < num_samples; ++i) {
        float res = (float)(rand() / (double)RAND_MAX);
        res -= 0.5f;
        res *= 2.0f;
        dest[i] = res * vol;
    }
}

void wtp_default_config(wtp_config_t *cfg) {
    if (!cfg) return;
    memset(cfg, 0, sizeof(*cfg));
    cfg->sample_rate = WTPS_DEFAULT_SAMPLE_RATE;
    cfg->frame_size = 256u;
    cfg->num_voices = 8u;
    cfg->wavetable_size = WTPS_DEFAULT_WAVETABLE_SIZE;
    cfg->gain = 0.75f;
    cfg->clip_level = 1.0f;
    cfg->waveform = WTP_WT_TRIANGLE;
    cfg->attack_ms = 62.5f;
    cfg->decay_ms = 83.333f;
    cfg->sustain_level = 0.75f;
    cfg->release_ms = 416.666f;
    cfg->pitch_env_amount_st = 0.0f;
    cfg->pitch_env_attack_ms = 0.0f;
    cfg->pitch_env_decay_ms = 90.0f;
    cfg->filter_cutoff_hz = 22000.0f;
    cfg->filter_resonance = 0.125f;
    cfg->filter_lowpass_mode = 1;
}

void wtp_set_adsr_ms(
    wtp_instrument_t *instr,
    float attack_ms,
    float decay_ms,
    float sustain_level_0_1,
    float release_ms
);
void wtp_set_pitch_env(
    wtp_instrument_t *instr,
    float amount_semitones,
    float attack_ms,
    float decay_ms
);

void wtp_instrument_free(wtp_instrument_t *instr);

int wtp_instrument_init_ex(wtp_instrument_t *instr, const wtp_config_t *cfg) {
    if (!instr || !cfg || !cfg->num_voices || !cfg->frame_size) return 0;

    memset(instr, 0, sizeof(*instr));
    instr->sample_rate = cfg->sample_rate ? cfg->sample_rate : WTPS_DEFAULT_SAMPLE_RATE;
    instr->frame_size = cfg->frame_size;
    instr->wavetable_size = cfg->wavetable_size ? cfg->wavetable_size : WTPS_DEFAULT_WAVETABLE_SIZE;
    instr->num_voices = cfg->num_voices;
    instr->active_wave = (uint32_t)cfg->waveform < WTP_WT_TYPES ? (uint32_t)cfg->waveform : WTP_WT_TRIANGLE;
    instr->gain = cfg->gain;
    instr->clip_level = cfg->clip_level;
    instr->filter.lowpass_mode = cfg->filter_lowpass_mode ? 1 : 0;

    instr->wavetables[WTP_WT_TRIANGLE].samples = wtp_tri_wavetable_init(instr->wavetable_size);
    instr->wavetables[WTP_WT_SAW].samples = wtp_saw_wavetable_init(instr->wavetable_size, 64);
    instr->wavetables[WTP_WT_SQUARE].samples = wtp_square_wavetable_init(instr->wavetable_size, 64);
    instr->wavetables[WTP_WT_SINE].samples = wtp_sine_wavetable_init(instr->wavetable_size);
    instr->wavetables[WTP_WT_NOISE].samples = wtp_noise_wavetable_init(instr->wavetable_size);
    for (uint32_t i = 0; i < WTP_WT_TYPES; ++i) instr->wavetables[i].length = instr->wavetable_size;

    for (uint32_t i = 0; i < WTP_WT_TYPES; ++i) {
        if (!instr->wavetables[i].samples) {
            wtp_instrument_free(instr);
            return 0;
        }
    }

    instr->voices = (wtp_voice_t *)calloc(instr->num_voices, sizeof(wtp_voice_t));
    if (!instr->voices) {
        wtp_instrument_free(instr);
        return 0;
    }

    for (uint32_t i = 0; i < instr->num_voices; ++i) {
        instr->voices[i].source = &instr->wavetables[instr->active_wave];
        instr->voices[i].state = WTP_VOICE_OFF;
        instr->voices[i].note_id = -1;
        instr->voices[i].buffer = (float *)calloc(instr->frame_size, sizeof(float));
        if (!instr->voices[i].buffer) {
            wtp_instrument_free(instr);
            return 0;
        }
    }

    wtp_set_adsr_ms(instr, cfg->attack_ms, cfg->decay_ms, cfg->sustain_level, cfg->release_ms);
    wtp_set_pitch_env(instr, cfg->pitch_env_amount_st, cfg->pitch_env_attack_ms, cfg->pitch_env_decay_ms);
    instr->env.attack_level = 0.5f;

    wtp_filter_reset(&instr->filter.state[0], instr->sample_rate);
    wtp_filter_reset(&instr->filter.state[1], instr->sample_rate);
    atomic_init(&instr->filter.active_filter, 0);
    wtp_filter_update(&instr->filter, cfg->filter_cutoff_hz, cfg->filter_resonance, instr->sample_rate);
    return 1;
}

int wtp_instrument_init(
    wtp_instrument_t *instr,
    uint32_t sample_rate,
    uint32_t frame_size,
    uint32_t num_voices,
    uint32_t wavetable_size
) {
    wtp_config_t cfg;
    wtp_default_config(&cfg);
    if (sample_rate) cfg.sample_rate = sample_rate;
    if (frame_size) cfg.frame_size = frame_size;
    if (num_voices) cfg.num_voices = num_voices;
    if (wavetable_size) cfg.wavetable_size = wavetable_size;
    return wtp_instrument_init_ex(instr, &cfg);
}

void wtp_instrument_free(wtp_instrument_t *instr) {
    if (!instr) return;

    for (uint32_t i = 0; i < WTP_WT_TYPES; ++i) {
        free(instr->wavetables[i].samples);
        instr->wavetables[i].samples = NULL;
        instr->wavetables[i].length = 0;
    }

    if (instr->voices) {
        for (uint32_t i = 0; i < instr->num_voices; ++i) {
            free(instr->voices[i].buffer);
            instr->voices[i].buffer = NULL;
        }
        free(instr->voices);
        instr->voices = NULL;
    }

    instr->num_voices = 0;
}

void wtp_set_waveform(wtp_instrument_t *instr, enum wtp_waveform_type wave) {
    if (!instr || wave >= WTP_WT_TYPES) return;
    instr->active_wave = (uint32_t)wave;
    for (uint32_t i = 0; i < instr->num_voices; ++i) {
        instr->voices[i].source = &instr->wavetables[instr->active_wave];
    }
}

void wtp_set_filter_mode(wtp_instrument_t *instr, int lowpass_mode) {
    if (!instr) return;
    instr->filter.lowpass_mode = lowpass_mode ? 1 : 0;
}

void wtp_set_filter(wtp_instrument_t *instr, float cutoff_hz, float resonance) {
    if (!instr) return;
    if (cutoff_hz < 1.0f) cutoff_hz = 1.0f;
    if (cutoff_hz > (float)instr->sample_rate * 0.49f) cutoff_hz = (float)instr->sample_rate * 0.49f;
    if (resonance < 0.0f) resonance = 0.0f;
    if (resonance > 0.999f) resonance = 0.999f;
    wtp_filter_update(&instr->filter, cutoff_hz, resonance, instr->sample_rate);
}

void wtp_set_filter_from_dials(wtp_instrument_t *instr, float cutoff_dial_norm, float resonance_dial_norm) {
    if (!instr) return;
    float cutoff_hz = wtp_cutoff_hz_from_dial(cutoff_dial_norm);
    float resonance = wtp_resonance_from_dial(resonance_dial_norm);
    wtp_set_filter(instr, cutoff_hz, resonance);
}

// Applies dial targets to 1-pole smoothers, advances them, then updates filter.
// This is intended to be called by GUI/control code before feeding values to the instrument.
void wtp_set_filter_from_dials_smoothed(
    wtp_instrument_t *instr,
    wtp_parameter_t *cutoff_smoother,
    wtp_parameter_t *resonance_smoother,
    float cutoff_dial_norm,
    float resonance_dial_norm,
    uint32_t smoothing_steps
) {
    if (!instr || !cutoff_smoother || !resonance_smoother) return;
    if (!smoothing_steps) smoothing_steps = 1;

    wtp_param_smooth_set_target(cutoff_smoother, wtp_cutoff_hz_from_dial(cutoff_dial_norm));
    wtp_param_smooth_set_target(resonance_smoother, wtp_resonance_from_dial(resonance_dial_norm));

    float cutoff_hz = 0.0f;
    float resonance = 0.0f;
    for (uint32_t i = 0; i < smoothing_steps; ++i) {
        cutoff_hz = wtp_param_smooth_tick(cutoff_smoother);
        resonance = wtp_param_smooth_tick(resonance_smoother);
    }

    wtp_set_filter(instr, cutoff_hz, resonance);
}

static int wtp_is_power_of_two_u32(uint32_t x) {
    return x && ((x & (x - 1u)) == 0u);
}

int wtp_ringbuffer_init(wtp_ringbuffer_t *rb, uint32_t capacity_samples) {
    if (!rb || !wtp_is_power_of_two_u32(capacity_samples)) return 0;
    rb->data = (float *)calloc(capacity_samples, sizeof(float));
    if (!rb->data) return 0;
    rb->capacity = capacity_samples;
    rb->mask = capacity_samples - 1u;
    atomic_init(&rb->write_idx, 0u);
    atomic_init(&rb->read_idx, 0u);
    return 1;
}

void wtp_ringbuffer_free(wtp_ringbuffer_t *rb) {
    if (!rb) return;
    free(rb->data);
    rb->data = NULL;
    rb->capacity = 0;
    rb->mask = 0;
    atomic_store_explicit(&rb->write_idx, 0u, memory_order_release);
    atomic_store_explicit(&rb->read_idx, 0u, memory_order_release);
}

uint32_t wtp_ringbuffer_available_read(const wtp_ringbuffer_t *rb) {
    if (!rb || !rb->data) return 0u;
    uint32_t w = atomic_load_explicit((atomic_uint *)&rb->write_idx, memory_order_acquire);
    uint32_t r = atomic_load_explicit((atomic_uint *)&rb->read_idx, memory_order_acquire);
    return (w - r) & rb->mask;
}

uint32_t wtp_ringbuffer_available_write(const wtp_ringbuffer_t *rb) {
    if (!rb || !rb->data) return 0u;
    // Keep one slot empty to disambiguate full vs empty.
    return (rb->capacity - 1u) - wtp_ringbuffer_available_read(rb);
}

uint32_t wtp_ringbuffer_write(wtp_ringbuffer_t *rb, const float *src, uint32_t count) {
    if (!rb || !rb->data || !src || !count) return 0u;

    uint32_t w = atomic_load_explicit(&rb->write_idx, memory_order_relaxed);
    uint32_t r = atomic_load_explicit(&rb->read_idx, memory_order_acquire);
    uint32_t free_slots = (r - w - 1u) & rb->mask;
    uint32_t to_write = (count < free_slots) ? count : free_slots;

    uint32_t pos = w & rb->mask;
    uint32_t first = rb->capacity - pos;
    if (first > to_write) first = to_write;
    memcpy(&rb->data[pos], src, sizeof(float) * first);

    uint32_t remain = to_write - first;
    if (remain) {
        memcpy(&rb->data[0], src + first, sizeof(float) * remain);
    }

    atomic_store_explicit(&rb->write_idx, (w + to_write) & rb->mask, memory_order_release);
    return to_write;
}

uint32_t wtp_ringbuffer_read(wtp_ringbuffer_t *rb, float *dst, uint32_t count) {
    if (!rb || !rb->data || !dst || !count) return 0u;

    uint32_t r = atomic_load_explicit(&rb->read_idx, memory_order_relaxed);
    uint32_t w = atomic_load_explicit(&rb->write_idx, memory_order_acquire);
    uint32_t available = (w - r) & rb->mask;
    uint32_t to_read = (count < available) ? count : available;

    uint32_t pos = r & rb->mask;
    uint32_t first = rb->capacity - pos;
    if (first > to_read) first = to_read;
    memcpy(dst, &rb->data[pos], sizeof(float) * first);

    uint32_t remain = to_read - first;
    if (remain) {
        memcpy(dst + first, &rb->data[0], sizeof(float) * remain);
    }

    atomic_store_explicit(&rb->read_idx, (r + to_read) & rb->mask, memory_order_release);
    return to_read;
}

void wtp_set_adsr_ms(
    wtp_instrument_t *instr,
    float attack_ms,
    float decay_ms,
    float sustain_level_0_1,
    float release_ms
) {
    if (!instr) return;
    if (attack_ms < 0.0f) attack_ms = 0.0f;
    if (decay_ms < 0.0f) decay_ms = 0.0f;
    if (release_ms < 0.0f) release_ms = 0.0f;
    if (sustain_level_0_1 < 0.0f) sustain_level_0_1 = 0.0f;
    if (sustain_level_0_1 > 1.0f) sustain_level_0_1 = 1.0f;

    instr->env.attack_time = (uint32_t)(attack_ms * (float)instr->sample_rate * 0.001f);
    instr->env.decay_time = (uint32_t)(decay_ms * (float)instr->sample_rate * 0.001f);
    instr->env.release_time = (uint32_t)(release_ms * (float)instr->sample_rate * 0.001f);
    instr->env.sustain_level = sustain_level_0_1 * 0.5f;
}

void wtp_set_pitch_env(
    wtp_instrument_t *instr,
    float amount_semitones,
    float attack_ms,
    float decay_ms
) {
    if (!instr) return;
    if (attack_ms < 0.0f) attack_ms = 0.0f;
    if (decay_ms < 0.0f) decay_ms = 0.0f;
    if (amount_semitones < -48.0f) amount_semitones = -48.0f;
    if (amount_semitones > 48.0f) amount_semitones = 48.0f;
    instr->pitch_env_amount_st = amount_semitones;
    instr->pitch_env_attack_time = (uint32_t)(attack_ms * (float)instr->sample_rate * 0.001f);
    instr->pitch_env_decay_time = (uint32_t)(decay_ms * (float)instr->sample_rate * 0.001f);
}

void wtp_note_on_hz(wtp_instrument_t *instr, int32_t note_id, float freq_hz) {
    if (!instr || freq_hz <= 0.0f) return;
    wtp_voice_t *v = wtp_get_free_voice(instr);

    v->index = 0;
    v->freq = freq_hz;
    v->base_freq = freq_hz;
    v->note_id = note_id;
    v->phase = 0;
    v->release_timer = instr->env.release_time + 1;
    v->state = WTP_NOTE_ON;
}

void wtp_note_on_midi(wtp_instrument_t *instr, uint8_t midi_note) {
    float freq = 440.0f * powf(2.0f, ((float)midi_note - 69.0f) / 12.0f);
    wtp_note_on_hz(instr, (int32_t)midi_note, freq);
}

void wtp_note_off(wtp_instrument_t *instr, int32_t note_id) {
    if (!instr) return;
    for (uint32_t i = 0; i < instr->num_voices; ++i) {
        if (instr->voices[i].note_id == note_id && instr->voices[i].state == WTP_NOTE_ON) {
            instr->voices[i].state = WTP_NOTE_OFF;
            break;
        }
    }
}

static void wtp_render_instrument_block(wtp_instrument_t *instr, float *dst, uint32_t count) {
    memset(dst, 0, sizeof(float) * count);
    for (uint32_t i = 0; i < instr->num_voices; ++i) {
        wtp_voice_t *v = &instr->voices[i];
        if (v->state == WTP_VOICE_OFF) continue;

        if (instr->pitch_env_amount_st != 0.0f) {
            float env_t = 0.0f;
            if (instr->pitch_env_attack_time > 0 && v->index < instr->pitch_env_attack_time) {
                env_t = (float)v->index / (float)instr->pitch_env_attack_time;
            } else if (instr->pitch_env_decay_time > 0) {
                uint32_t rel = (v->index > instr->pitch_env_attack_time) ? (v->index - instr->pitch_env_attack_time) : 0u;
                if (rel < instr->pitch_env_decay_time) {
                    env_t = 1.0f - ((float)rel / (float)instr->pitch_env_decay_time);
                }
            } else {
                env_t = 0.0f;
            }
            if (env_t < 0.0f) env_t = 0.0f;
            if (env_t > 1.0f) env_t = 1.0f;
            v->freq = v->base_freq * powf(2.0f, (instr->pitch_env_amount_st * env_t) / 12.0f);
        } else {
            v->freq = v->base_freq;
        }

        wtp_gen_wavetable_freq(v, count, instr->sample_rate);
        wtp_apply_envelope(v, &instr->env, v->buffer, count);
        v->index += count;

        for (uint32_t n = 0; n < count; ++n) {
            dst[n] += v->buffer[n] * instr->gain;
        }
    }

    wtp_filter_process(&instr->filter, dst, count);

    if (instr->clip_level < 1.0f) {
        for (uint32_t i = 0; i < count; ++i) {
            dst[i] = wtp_clip(dst[i], instr->clip_level);
        }
    }
}

void wtp_render_instrument(wtp_instrument_t *instr, float *dst, uint32_t count) {
    if (!instr || !dst || !count) return;

    uint32_t remaining = count;
    uint32_t offset = 0;
    while (remaining) {
        uint32_t n = remaining > instr->frame_size ? instr->frame_size : remaining;
        wtp_render_instrument_block(instr, dst + offset, n);
        offset += n;
        remaining -= n;
    }
}

void wtp_render_instrument_stereo(
    wtp_instrument_t *instr,
    float *dst_interleaved_lr,
    uint32_t frames,
    float pan_0_left_1_right,
    float output_gain
) {
    if (!instr || !dst_interleaved_lr || !frames) return;

    float pan = wtp_clampf(pan_0_left_1_right, 0.0f, 1.0f);
    float gain_l = (1.0f - pan) * output_gain;
    float gain_r = pan * output_gain;

    float *mono = (float *)malloc(sizeof(float) * frames);
    if (!mono) return;
    wtp_render_instrument(instr, mono, frames);

    for (uint32_t i = 0, j = 0; i < frames; ++i) {
        float s = mono[i];
        dst_interleaved_lr[j++] = s * gain_l;
        dst_interleaved_lr[j++] = s * gain_r;
    }
    free(mono);
}

uint32_t wtp_get_active_voice_count(const wtp_instrument_t *instr) {
    if (!instr || !instr->voices) return 0u;
    uint32_t active = 0u;
    for (uint32_t i = 0; i < instr->num_voices; ++i) {
        if (instr->voices[i].state != WTP_VOICE_OFF) ++active;
    }
    return active;
}

void wtp_reset_voice(wtp_voice_t *voice, uint32_t frame_size) {
    if (!voice) return;
    voice->state = WTP_VOICE_OFF;
    voice->freq = 0.0f;
    voice->base_freq = 0.0f;
    voice->current_vol = 0.0f;
    voice->release_timer = 0u;
    voice->phase = 0u;
    voice->index = 0u;
    voice->note_id = -1;
    if (voice->buffer && frame_size) {
        memset(voice->buffer, 0, sizeof(float) * frame_size);
    }
}

void wtp_reset_instrument(wtp_instrument_t *instr, int reset_filter_state) {
    if (!instr || !instr->voices) return;
    for (uint32_t i = 0; i < instr->num_voices; ++i) {
        wtp_reset_voice(&instr->voices[i], instr->frame_size);
        instr->voices[i].source = &instr->wavetables[instr->active_wave];
    }
    if (reset_filter_state) {
        wtp_filter_reset(&instr->filter.state[0], instr->sample_rate);
        wtp_filter_reset(&instr->filter.state[1], instr->sample_rate);
        atomic_store_explicit(&instr->filter.active_filter, 0, memory_order_release);
        wtp_set_filter(instr, instr->filter.cutoff, instr->filter.resonance);
    }
}

void wtp_preset_from_instrument(const wtp_instrument_t *instr, wtp_preset_t *preset_out) {
    if (!instr || !preset_out) return;
    memset(preset_out, 0, sizeof(*preset_out));
    preset_out->version = WTP_PRESET_VERSION;
    preset_out->waveform = (enum wtp_waveform_type)instr->active_wave;
    preset_out->gain = instr->gain;
    preset_out->clip_level = instr->clip_level;
    preset_out->attack_ms = 1000.0f * (float)instr->env.attack_time / (float)instr->sample_rate;
    preset_out->decay_ms = 1000.0f * (float)instr->env.decay_time / (float)instr->sample_rate;
    preset_out->sustain_level = instr->env.sustain_level * 2.0f;
    preset_out->release_ms = 1000.0f * (float)instr->env.release_time / (float)instr->sample_rate;
    preset_out->pitch_env_amount_st = instr->pitch_env_amount_st;
    preset_out->pitch_env_attack_ms = 1000.0f * (float)instr->pitch_env_attack_time / (float)instr->sample_rate;
    preset_out->pitch_env_decay_ms = 1000.0f * (float)instr->pitch_env_decay_time / (float)instr->sample_rate;
    preset_out->filter_cutoff_hz = instr->filter.cutoff;
    preset_out->filter_resonance = instr->filter.resonance;
    preset_out->filter_lowpass_mode = instr->filter.lowpass_mode;
}

void wtp_apply_preset(wtp_instrument_t *instr, const wtp_preset_t *preset) {
    if (!instr || !preset) return;
    if (preset->version != WTP_PRESET_VERSION) return;
    wtp_set_waveform(instr, preset->waveform);
    instr->gain = preset->gain;
    instr->clip_level = preset->clip_level;
    wtp_set_adsr_ms(instr, preset->attack_ms, preset->decay_ms, preset->sustain_level, preset->release_ms);
    wtp_set_pitch_env(instr, preset->pitch_env_amount_st, preset->pitch_env_attack_ms, preset->pitch_env_decay_ms);
    wtp_set_filter_mode(instr, preset->filter_lowpass_mode);
    wtp_set_filter(instr, preset->filter_cutoff_hz, preset->filter_resonance);
}
