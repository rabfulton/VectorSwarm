#ifndef WAVETABLE_POLY_SYNTH_LIB_H
#define WAVETABLE_POLY_SYNTH_LIB_H

#include <stdint.h>
#include <stdatomic.h>

#ifdef __cplusplus
extern "C" {
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
    wtp_parameter_t b0;
    wtp_parameter_t b1;
    wtp_parameter_t b2;
    wtp_parameter_t a1;
    wtp_parameter_t a2;
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
    int lowpass_mode;
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
    uint32_t length;
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

typedef struct {
    float *data;
    uint32_t capacity;
    uint32_t mask;
    atomic_uint write_idx;
    atomic_uint read_idx;
} wtp_ringbuffer_t;

float wtp_clampf(float x, float lo, float hi);

void wtp_param_smooth_init(wtp_parameter_t *p, uint32_t ms, uint32_t sample_rate, float initial_value);
void wtp_param_smooth_set_target(wtp_parameter_t *p, float target_value);
float wtp_param_smooth_tick(wtp_parameter_t *p);

float wtp_cutoff_hz_from_dial(float dial_norm);
float wtp_resonance_from_dial(float dial_norm);

void wtp_default_config(wtp_config_t *cfg);
int wtp_instrument_init_ex(wtp_instrument_t *instr, const wtp_config_t *cfg);
int wtp_instrument_init(
    wtp_instrument_t *instr,
    uint32_t sample_rate,
    uint32_t frame_size,
    uint32_t num_voices,
    uint32_t wavetable_size
);
void wtp_instrument_free(wtp_instrument_t *instr);

void wtp_set_waveform(wtp_instrument_t *instr, enum wtp_waveform_type wave);
void wtp_set_filter_mode(wtp_instrument_t *instr, int lowpass_mode);
void wtp_set_filter(wtp_instrument_t *instr, float cutoff_hz, float resonance);
void wtp_set_filter_from_dials(wtp_instrument_t *instr, float cutoff_dial_norm, float resonance_dial_norm);
void wtp_set_filter_from_dials_smoothed(
    wtp_instrument_t *instr,
    wtp_parameter_t *cutoff_smoother,
    wtp_parameter_t *resonance_smoother,
    float cutoff_dial_norm,
    float resonance_dial_norm,
    uint32_t smoothing_steps
);
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

void wtp_note_on_hz(wtp_instrument_t *instr, int32_t note_id, float freq_hz);
void wtp_note_on_midi(wtp_instrument_t *instr, uint8_t midi_note);
void wtp_note_off(wtp_instrument_t *instr, int32_t note_id);

void wtp_render_instrument(wtp_instrument_t *instr, float *dst_mono, uint32_t count);
void wtp_render_instrument_stereo(
    wtp_instrument_t *instr,
    float *dst_interleaved_lr,
    uint32_t frames,
    float pan_0_left_1_right,
    float output_gain
);

uint32_t wtp_get_active_voice_count(const wtp_instrument_t *instr);
void wtp_reset_voice(wtp_voice_t *voice, uint32_t frame_size);
void wtp_reset_instrument(wtp_instrument_t *instr, int reset_filter_state);

void wtp_preset_from_instrument(const wtp_instrument_t *instr, wtp_preset_t *preset_out);
void wtp_apply_preset(wtp_instrument_t *instr, const wtp_preset_t *preset);

void wtp_white_noise(float *dest, uint32_t num_samples, float vol);

int wtp_ringbuffer_init(wtp_ringbuffer_t *rb, uint32_t capacity_samples);
void wtp_ringbuffer_free(wtp_ringbuffer_t *rb);
uint32_t wtp_ringbuffer_available_read(const wtp_ringbuffer_t *rb);
uint32_t wtp_ringbuffer_available_write(const wtp_ringbuffer_t *rb);
uint32_t wtp_ringbuffer_write(wtp_ringbuffer_t *rb, const float *src, uint32_t count);
uint32_t wtp_ringbuffer_read(wtp_ringbuffer_t *rb, float *dst, uint32_t count);

#ifdef __cplusplus
}
#endif

#endif
