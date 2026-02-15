#include "game.h"
#include "acoustics_ui_layout.h"
#include "planetarium/planetarium_registry.h"
#include "planetarium/planetarium_validate.h"
#include "planetarium_propaganda.h"
#include "render.h"
#include "ui_layout.h"
#include "vg.h"
#include "vg_ui.h"
#include "vg_svg.h"
#include "vg_text_fx.h"
#include "wavetable_poly_synth_lib.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
#if V_TYPE_HAS_SDL_IMAGE
#include <SDL2/SDL_image.h>
#endif
#include <vulkan/vulkan.h>

#include <stdint.h>
#include <math.h>
#include <limits.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef V_TYPE_HAS_POST_SHADERS
#define V_TYPE_HAS_POST_SHADERS 0
#endif

#ifndef V_TYPE_HAS_TERRAIN_SHADERS
#define V_TYPE_HAS_TERRAIN_SHADERS 0
#endif

#if V_TYPE_HAS_POST_SHADERS
#include "demo_bloom_frag_spv.h"
#include "demo_composite_frag_spv.h"
#include "demo_fullscreen_vert_spv.h"
#endif

#if V_TYPE_HAS_TERRAIN_SHADERS
#include "terrain_frag_spv.h"
#include "terrain_wire_vert_spv.h"
#include "terrain_wire_frag_spv.h"
#include "terrain_vert_spv.h"
#include "particle_vert_spv.h"
#include "particle_frag_spv.h"
#include "particle_bloom_frag_spv.h"
#include "wormhole_line_vert_spv.h"
#include "wormhole_line_frag_spv.h"
#endif

#define APP_WIDTH 1280
#define APP_HEIGHT 720
#define APP_MAX_SWAPCHAIN_IMAGES 8
#define ACOUSTICS_SLOTS_PATH "acoustics_slots.cfg"
#define SETTINGS_PATH "settings.cfg"
#define ACOUSTICS_SCOPE_HISTORY_SAMPLES 8192
#define AUDIO_SPATIAL_EVENT_CAP 256
#define AUDIO_COMBAT_VOICE_COUNT 24
#define AUDIO_MAX_BEEP_SAMPLES 4096
#define GPU_PARTICLE_MAX_INSTANCES MAX_PARTICLES
#define TERRAIN_ROWS 24
#define TERRAIN_COLS 70

typedef struct video_resolution {
    int w;
    int h;
} video_resolution;

static const video_resolution k_video_resolutions[VIDEO_MENU_RES_COUNT] = {
    {1280, 720},
    {1366, 768},
    {1600, 900},
    {1920, 1080},
    {2560, 1440},
    {3840, 2160}
};

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

typedef struct post_pc {
    float p0[4]; /* texel.x, texel.y, bloom_strength, bloom_radius */
    float p1[4]; /* vignette, barrel, scanline, noise */
    float p2[4]; /* time_s, ui_enable, ui_x, ui_y */
    float p3[4]; /* ui_w, ui_h, pad0, pad1 */
} post_pc;

typedef struct terrain_vertex {
    float x;
    float y;
    float z;
} terrain_vertex;

typedef struct terrain_wire_vertex {
    float x;
    float y;
    float z;
    float bx;
    float by;
    float bz;
} terrain_wire_vertex;

typedef struct terrain_pc {
    float color[4];
    float params[4]; /* x=viewport_width, y=viewport_height, z=intensity, w=hue_shift */
    float tune[4];   /* x=brightness, y=opacity, z=normal_variation, w=depth_fade */
} terrain_pc;

typedef struct particle_instance {
    float x;
    float y;
    float radius_px;
    float kind;
    float r;
    float g;
    float b;
    float a;
    float dir_x;
    float dir_y;
    float trail;
    float heat;
} particle_instance;

typedef struct particle_pc {
    float params[4]; /* x=viewport_width, y=viewport_height, z=core_gain, w=trail_gain */
} particle_pc;

typedef struct wormhole_line_pc {
    float params[4]; /* x=viewport_width, y=viewport_height, z=intensity */
    float color[4];
    float offset[4]; /* x=offset_px_x, y=offset_px_y */
} wormhole_line_pc;

typedef struct terrain_tuning {
    float hue_shift;
    float brightness;
    float opacity;
    float normal_variation;
    float depth_fade;
} terrain_tuning;

enum acoustics_page_id {
    ACOUSTICS_PAGE_SYNTH = 0,
    ACOUSTICS_PAGE_COMBAT = 1,
    ACOUSTICS_PAGE_COUNT = 2
};

enum acoustics_combat_slider_id {
    ACOUST_COMBAT_ENEMY_LEVEL = 0,
    ACOUST_COMBAT_ENEMY_PITCH = 1,
    ACOUST_COMBAT_ENEMY_ATTACK = 2,
    ACOUST_COMBAT_ENEMY_DECAY = 3,
    ACOUST_COMBAT_ENEMY_NOISE = 4,
    ACOUST_COMBAT_ENEMY_PANW = 5,
    ACOUST_COMBAT_EXP_LEVEL = 6,
    ACOUST_COMBAT_EXP_PITCH = 7,
    ACOUST_COMBAT_EXP_ATTACK = 8,
    ACOUST_COMBAT_EXP_DECAY = 9,
    ACOUST_COMBAT_EXP_NOISE = 10,
    ACOUST_COMBAT_EXP_FM_DEPTH = 11,
    ACOUST_COMBAT_EXP_FM_RATE = 12,
    ACOUST_COMBAT_EXP_PANW = 13,
    ACOUST_COMBAT_SLIDER_COUNT = 14
};

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

typedef struct app {
    SDL_Window* window;

    VkInstance instance;
    VkSurfaceKHR surface;
    VkPhysicalDevice physical_device;
    VkDevice device;
    VkQueue graphics_queue;
    VkQueue present_queue;
    uint32_t graphics_queue_family;
    uint32_t present_queue_family;

    VkSwapchainKHR swapchain;
    VkFormat swapchain_format;
    VkExtent2D swapchain_extent;
    uint32_t swapchain_image_count;
    VkImage swapchain_images[APP_MAX_SWAPCHAIN_IMAGES];
    VkImageView swapchain_image_views[APP_MAX_SWAPCHAIN_IMAGES];

    VkRenderPass present_render_pass;
    VkFramebuffer present_framebuffers[APP_MAX_SWAPCHAIN_IMAGES];

    VkImage scene_image;
    VkDeviceMemory scene_memory;
    VkImageView scene_view;
    VkImage scene_depth_image;
    VkDeviceMemory scene_depth_memory;
    VkImageView scene_depth_view;
    VkFormat scene_depth_format;
    VkImage scene_msaa_image;
    VkDeviceMemory scene_msaa_memory;
    VkImageView scene_msaa_view;
    VkFramebuffer scene_fb;
    VkRenderPass scene_render_pass;

    VkImage bloom_image;
    VkDeviceMemory bloom_memory;
    VkImageView bloom_view;
    VkFramebuffer bloom_fb;
    VkRenderPass bloom_render_pass;

    VkSampler post_sampler;
    VkDescriptorSetLayout post_desc_layout;
    VkDescriptorPool post_desc_pool;
    VkDescriptorSet post_desc_set;
    VkPipelineLayout post_layout;
    VkPipeline bloom_pipeline;
    VkPipeline composite_pipeline;
    VkPipelineLayout terrain_layout;
    VkPipeline terrain_fill_pipeline;
    VkPipeline terrain_line_pipeline;
    VkPipelineLayout particle_layout;
    VkPipeline particle_pipeline;
    VkPipeline particle_bloom_pipeline;
    VkPipelineLayout wormhole_line_layout;
    VkPipeline wormhole_depth_pipeline;
    VkPipeline wormhole_line_pipeline;
    VkBuffer wormhole_tri_vertex_buffer;
    VkDeviceMemory wormhole_tri_vertex_memory;
    void* wormhole_tri_vertex_map;
    uint32_t wormhole_tri_vertex_count;
    VkBuffer wormhole_line_vertex_buffer;
    VkDeviceMemory wormhole_line_vertex_memory;
    void* wormhole_line_vertex_map;
    uint32_t wormhole_line_vertex_count;
    int use_gpu_wormhole;
    VkBuffer particle_instance_buffer;
    VkDeviceMemory particle_instance_memory;
    void* particle_instance_map;
    uint32_t particle_instance_count;
    VkBuffer terrain_vertex_buffer;
    VkDeviceMemory terrain_vertex_memory;
    void* terrain_vertex_map;
    VkBuffer terrain_tri_index_buffer;
    VkDeviceMemory terrain_tri_index_memory;
    VkBuffer terrain_wire_vertex_buffer;
    VkDeviceMemory terrain_wire_vertex_memory;
    void* terrain_wire_vertex_map;
    uint32_t terrain_tri_index_count;
    uint32_t terrain_wire_vertex_count;
    int terrain_wire_enabled;
    terrain_tuning terrain_tuning;
    int terrain_tuning_enabled;
    int terrain_tuning_show;
    char terrain_tuning_text[192];
    int particle_tuning_enabled; /* VTYPE_PARTICLE_TRACE */
    int particle_tuning_show;
    int particle_bloom_enabled;
    float particle_core_gain;
    float particle_trail_gain;
    float particle_heat_cooling;
    char particle_tuning_text[192];

    VkCommandPool command_pool;
    VkCommandBuffer command_buffers[APP_MAX_SWAPCHAIN_IMAGES];

    VkSemaphore image_available;
    VkSemaphore render_finished;
    VkFence in_flight;

    vg_context* vg;
    game_state game;
    vg_text_fx_typewriter wave_tty;
    vg_text_fx_marquee planetarium_marquee;
    SDL_AudioDeviceID audio_dev;
    SDL_AudioSpec audio_have;
    int audio_ready;
    char wave_tty_text[640];
    char wave_tty_visible[640];
    wtp_instrument_t weapon_synth;
    wtp_instrument_t thruster_synth;
    wtp_ringbuffer_t beep_rb;
    wtp_ringbuffer_t scope_rb;
    float* audio_mix_tmp_a;
    float* audio_mix_tmp_b;
    float* audio_mix_tmp_c;
    float* audio_mix_tmp_d;
    uint32_t audio_mix_tmp_cap;
    float scope_window[ACOUSTICS_SCOPE_SAMPLES];
    float scope_history[ACOUSTICS_SCOPE_HISTORY_SAMPLES];
    uint32_t fire_note_id;
    int32_t thruster_note_id;
    uint32_t audio_rng;
    atomic_uint pending_fire_events;
    atomic_uint pending_thruster_tests;
    atomic_uint pending_enemy_fire_tests;
    atomic_uint pending_explosion_tests;
    atomic_int thrust_gate;
    atomic_int audio_weapon_level;
    atomic_uint audio_spatial_read;
    atomic_uint audio_spatial_write;
    audio_spatial_event audio_spatial_q[AUDIO_SPATIAL_EVENT_CAP];
    audio_combat_voice combat_voices[AUDIO_COMBAT_VOICE_COUNT];
    combat_sound_params enemy_fire_sound;
    combat_sound_params explosion_sound;
    uint32_t thruster_test_frames_left;
    int force_clear_frames;
    int show_crt_ui;
    int show_fps_counter;
    int crt_ui_selected;
    int crt_ui_mouse_drag;
    int show_acoustics;
    int show_video_menu;
    int show_planetarium;
    int video_menu_selected;
    int video_menu_fullscreen;
    int palette_mode;
    int msaa_enabled;
    VkSampleCountFlagBits msaa_samples;
    float video_dial_01[VIDEO_MENU_DIAL_COUNT];
    int video_menu_dial_drag;
    float video_menu_dial_drag_start_y;
    float video_menu_dial_drag_start_value;
    int swapchain_needs_recreate;
    int acoustics_selected;
    int acoustics_page;
    int acoustics_combat_selected;
    int acoustics_fire_slot_selected;
    int acoustics_thr_slot_selected;
    int acoustics_enemy_slot_selected;
    int acoustics_exp_slot_selected;
    uint8_t acoustics_fire_slot_defined[ACOUSTICS_SLOT_COUNT];
    uint8_t acoustics_thr_slot_defined[ACOUSTICS_SLOT_COUNT];
    uint8_t acoustics_enemy_slot_defined[ACOUSTICS_SLOT_COUNT];
    uint8_t acoustics_exp_slot_defined[ACOUSTICS_SLOT_COUNT];
    float acoustics_fire_slots[ACOUSTICS_SLOT_COUNT][8];
    float acoustics_thr_slots[ACOUSTICS_SLOT_COUNT][6];
    float acoustics_enemy_slots[ACOUSTICS_SLOT_COUNT][6];
    float acoustics_exp_slots[ACOUSTICS_SLOT_COUNT][8];
    int acoustics_mouse_drag;
    float acoustics_value_01[ACOUSTICS_SLIDER_COUNT];
    float acoustics_combat_value_01[ACOUST_COMBAT_SLIDER_COUNT];
    int thruster_note_on;
    int current_system_index;
    int planetarium_selected;
    int planetarium_nodes_quelled[PLANETARIUM_MAX_SYSTEMS][PLANETARIUM_MAX_SYSTEMS];
    uint8_t* nick_rgba8;
    uint32_t nick_w;
    uint32_t nick_h;
    uint32_t nick_stride;
    vg_svg_asset* surveillance_svg_asset;
} app;

static VkSampleCountFlagBits pick_msaa_samples(app* a) {
    (void)a;
    /* DefconDraw Vulkan backend currently builds its internal line pipeline at 1x samples.
       Keep the scene pass at 1x to avoid render-pass/pipeline sample mismatches. */
    return VK_SAMPLE_COUNT_1_BIT;
}

static VkSampleCountFlagBits scene_samples(const app* a) {
    if (!a || !a->msaa_enabled) {
        return VK_SAMPLE_COUNT_1_BIT;
    }
    if (a->msaa_samples == 0) {
        return VK_SAMPLE_COUNT_1_BIT;
    }
    return a->msaa_samples;
}

static int check_vk(VkResult r, const char* what) {
    if (r != VK_SUCCESS) {
        fprintf(stderr, "%s failed (VkResult=%d)\n", what, (int)r);
        return 0;
    }
    return 1;
}

static float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static float lerpf(float a, float b, float t) {
    return a + (b - a) * t;
}

static float repeatf(float v, float period) {
    if (period <= 0.0f) {
        return v;
    }
    float x = fmodf(v, period);
    if (x < 0.0f) {
        x += period;
    }
    return x;
}

static void set_tty_message(app* a, const char* msg);

static int env_flag_enabled(const char* name) {
    const char* v = getenv(name);
    if (!v || !v[0]) {
        return 0;
    }
    if ((strcmp(v, "1") == 0) ||
        (strcmp(v, "true") == 0) || (strcmp(v, "TRUE") == 0) ||
        (strcmp(v, "yes") == 0) || (strcmp(v, "YES") == 0) ||
        (strcmp(v, "on") == 0) || (strcmp(v, "ON") == 0)) {
        return 1;
    }
    return 0;
}

static void reset_terrain_tuning(app* a) {
    if (!a) {
        return;
    }
    a->terrain_tuning.hue_shift = -0.05f;
    a->terrain_tuning.brightness = 0.50f;
    a->terrain_tuning.opacity = 1.00f;
    a->terrain_tuning.normal_variation = 0.65f;
    a->terrain_tuning.depth_fade = 0.60f;
}

static void sync_terrain_tuning_text(app* a) {
    if (!a) {
        return;
    }
    snprintf(
        a->terrain_tuning_text,
        sizeof(a->terrain_tuning_text),
        "TERRAIN TUNE hue %.3f bright %.3f alpha %.3f nvar %.3f zfade %.3f (KP Enter dump, KP . reset)",
        a->terrain_tuning.hue_shift,
        a->terrain_tuning.brightness,
        a->terrain_tuning.opacity,
        a->terrain_tuning.normal_variation,
        a->terrain_tuning.depth_fade
    );
}

static void reset_particle_tuning(app* a) {
    if (!a) {
        return;
    }
    a->particle_core_gain = 0.50f;
    a->particle_trail_gain = 1.90f;
    a->particle_heat_cooling = 2.50f;
}

static void sync_particle_tuning_text(app* a) {
    if (!a) {
        return;
    }
    snprintf(
        a->particle_tuning_text,
        sizeof(a->particle_tuning_text),
        "PARTICLE TUNE core %.3f trail %.3f cool %.3f (KP* hud, KP Enter dump, KP . reset)",
        a->particle_core_gain,
        a->particle_trail_gain,
        a->particle_heat_cooling
    );
}

static void print_particle_tuning(const app* a) {
    if (!a) {
        return;
    }
    printf(
        "[particle_tune] core_gain=%.6ff trail_gain=%.6ff heat_cooling=%.6ff\n",
        a->particle_core_gain,
        a->particle_trail_gain,
        a->particle_heat_cooling
    );
    fflush(stdout);
}

static int handle_particle_tuning_key(app* a, SDL_Keycode key) {
    if (!a || !a->particle_tuning_enabled) {
        return 0;
    }
    int handled = 1;
    switch (key) {
        case SDLK_KP_7: a->particle_core_gain += 0.10f; break;
        case SDLK_KP_4: a->particle_core_gain -= 0.10f; break;
        case SDLK_KP_8: a->particle_trail_gain += 0.10f; break;
        case SDLK_KP_5: a->particle_trail_gain -= 0.10f; break;
        case SDLK_KP_9: a->particle_heat_cooling += 0.10f; break;
        case SDLK_KP_6: a->particle_heat_cooling -= 0.10f; break;
        case SDLK_KP_MULTIPLY:
            a->particle_tuning_show = !a->particle_tuning_show;
            set_tty_message(a, a->particle_tuning_show ? "particle tune hud: on" : "particle tune hud: off");
            break;
        case SDLK_KP_PERIOD:
            reset_particle_tuning(a);
            set_tty_message(a, "particle tuning reset");
            break;
        case SDLK_KP_ENTER:
            print_particle_tuning(a);
            set_tty_message(a, "particle tuning dumped to stdout");
            break;
        default:
            handled = 0;
            break;
    }
    if (!handled) {
        return 0;
    }
    a->particle_core_gain = clampf(a->particle_core_gain, 0.50f, 3.00f);
    a->particle_trail_gain = clampf(a->particle_trail_gain, 0.00f, 3.00f);
    a->particle_heat_cooling = clampf(a->particle_heat_cooling, 0.20f, 3.00f);
    sync_particle_tuning_text(a);
    return 1;
}

static void print_terrain_tuning(const app* a) {
    if (!a) {
        return;
    }
    printf(
        "[terrain_tune] hue_shift=%.6ff brightness=%.6ff opacity=%.6ff normal_variation=%.6ff depth_fade=%.6ff\n",
        a->terrain_tuning.hue_shift,
        a->terrain_tuning.brightness,
        a->terrain_tuning.opacity,
        a->terrain_tuning.normal_variation,
        a->terrain_tuning.depth_fade
    );
    printf(
        "[terrain_tune] hardcode: pc.params[3]=%.6ff; pc.tune[0]=%.6ff; pc.tune[1]=%.6ff; pc.tune[2]=%.6ff; pc.tune[3]=%.6ff;\n",
        a->terrain_tuning.hue_shift,
        a->terrain_tuning.brightness,
        a->terrain_tuning.opacity,
        a->terrain_tuning.normal_variation,
        a->terrain_tuning.depth_fade
    );
    fflush(stdout);
}

static int handle_terrain_tuning_key(app* a, SDL_Keycode key) {
    if (!a || !a->terrain_tuning_enabled || a->game.level_style != LEVEL_STYLE_HIGH_PLAINS_DRIFTER_2) {
        return 0;
    }
    int handled = 1;
    switch (key) {
        case SDLK_KP_7: a->terrain_tuning.hue_shift += 0.010f; break;
        case SDLK_KP_4: a->terrain_tuning.hue_shift -= 0.010f; break;
        case SDLK_KP_8: a->terrain_tuning.brightness += 0.050f; break;
        case SDLK_KP_5: a->terrain_tuning.brightness -= 0.050f; break;
        case SDLK_KP_9: a->terrain_tuning.opacity += 0.050f; break;
        case SDLK_KP_6: a->terrain_tuning.opacity -= 0.050f; break;
        case SDLK_KP_1: a->terrain_tuning.normal_variation -= 0.050f; break;
        case SDLK_KP_2: a->terrain_tuning.normal_variation += 0.050f; break;
        case SDLK_KP_3: a->terrain_tuning.depth_fade += 0.050f; break;
        case SDLK_KP_0: a->terrain_tuning.depth_fade -= 0.050f; break;
        case SDLK_KP_MULTIPLY:
            a->terrain_tuning_show = !a->terrain_tuning_show;
            set_tty_message(a, a->terrain_tuning_show ? "terrain tune hud: on" : "terrain tune hud: off");
            break;
        case SDLK_KP_PERIOD:
            reset_terrain_tuning(a);
            set_tty_message(a, "terrain tuning reset");
            break;
        case SDLK_KP_ENTER:
            print_terrain_tuning(a);
            set_tty_message(a, "terrain tuning dumped to stdout");
            break;
        default:
            handled = 0;
            break;
    }

    if (!handled) {
        return 0;
    }
    a->terrain_tuning.hue_shift = clampf(a->terrain_tuning.hue_shift, -0.50f, 0.50f);
    a->terrain_tuning.brightness = clampf(a->terrain_tuning.brightness, 0.20f, 2.50f);
    a->terrain_tuning.opacity = clampf(a->terrain_tuning.opacity, 0.15f, 1.00f);
    a->terrain_tuning.normal_variation = clampf(a->terrain_tuning.normal_variation, 0.0f, 1.50f);
    a->terrain_tuning.depth_fade = clampf(a->terrain_tuning.depth_fade, 0.0f, 1.80f);
    sync_terrain_tuning_text(a);
    return 1;
}

static float perlin_fade(float t) {
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

static uint32_t hash_u32(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

static float hash01_2i(int ix, int iy) {
    const uint32_t hx = hash_u32((uint32_t)ix * 0x9e3779b9u);
    const uint32_t hy = hash_u32((uint32_t)iy * 0x85ebca6bu);
    const uint32_t h = hash_u32(hx ^ hy ^ 0x27d4eb2du);
    return (float)(h & 0x00ffffffu) / 16777215.0f;
}

static float perlin_grad_dot(int ix, int iy, float x, float y) {
    const float a = hash01_2i(ix, iy) * 6.28318530718f;
    const float gx = cosf(a);
    const float gy = sinf(a);
    return gx * (x - (float)ix) + gy * (y - (float)iy);
}

static float perlin2(float x, float y) {
    const int x0 = (int)floorf(x);
    const int y0 = (int)floorf(y);
    const int x1 = x0 + 1;
    const int y1 = y0 + 1;
    const float sx = perlin_fade(x - (float)x0);
    const float sy = perlin_fade(y - (float)y0);
    const float n00 = perlin_grad_dot(x0, y0, x, y);
    const float n10 = perlin_grad_dot(x1, y0, x, y);
    const float n01 = perlin_grad_dot(x0, y1, x, y);
    const float n11 = perlin_grad_dot(x1, y1, x, y);
    const float ix0 = lerpf(n00, n10, sx);
    const float ix1 = lerpf(n01, n11, sx);
    return lerpf(ix0, ix1, sy);
}

static float high_plains_looped_noise(float world_x, float z) {
    const float period_world = 8192.0f;
    const float u = repeatf(world_x, period_world) / period_world;
    const float a = u * 6.28318530718f;
    const float nx = cosf(a) * 2.3f + z * 0.85f + 19.7f;
    const float ny = sinf(a) * 2.3f - z * 0.55f + 7.3f;
    const float n0 = perlin2(nx, ny);
    const float n1 = perlin2(nx * 1.9f + 13.2f, ny * 1.9f - 4.6f);
    return n0 * 0.78f + n1 * 0.22f;
}

static float rand01_from_state(uint32_t* s) {
    *s = (*s * 1664525u) + 1013904223u;
    return (float)((*s >> 8) & 0x00ffffffu) / 16777215.0f;
}

static const planetary_system_def* app_planetarium_system(const app* a) {
    if (!a) {
        return NULL;
    }
    const planetary_system_def* sys = planetarium_get_system(a->current_system_index);
    if (sys) {
        return sys;
    }
    return planetarium_get_system(0);
}

static int app_planetarium_planet_count(const app* a) {
    const planetary_system_def* sys = app_planetarium_system(a);
    if (!sys || !sys->planets || sys->planet_count <= 0) {
        return 1;
    }
    if (sys->planet_count > PLANETARIUM_MAX_SYSTEMS) {
        return PLANETARIUM_MAX_SYSTEMS;
    }
    return sys->planet_count;
}

static int app_planetarium_node_quelled(const app* a, int planet_idx) {
    if (!a) {
        return 0;
    }
    if (a->current_system_index < 0 || a->current_system_index >= PLANETARIUM_MAX_SYSTEMS) {
        return 0;
    }
    if (planet_idx < 0 || planet_idx >= PLANETARIUM_MAX_SYSTEMS) {
        return 0;
    }
    return a->planetarium_nodes_quelled[a->current_system_index][planet_idx] ? 1 : 0;
}

static void app_planetarium_set_node_quelled(app* a, int planet_idx, int quelled) {
    if (!a) {
        return;
    }
    if (a->current_system_index < 0 || a->current_system_index >= PLANETARIUM_MAX_SYSTEMS) {
        return;
    }
    if (planet_idx < 0 || planet_idx >= PLANETARIUM_MAX_SYSTEMS) {
        return;
    }
    a->planetarium_nodes_quelled[a->current_system_index][planet_idx] = quelled ? 1 : 0;
}

static int planetarium_quelled_count(const app* a) {
    const int planets = app_planetarium_planet_count(a);
    int count = 0;
    for (int i = 0; i < planets; ++i) {
        if (app_planetarium_node_quelled(a, i)) {
            count++;
        }
    }
    return count;
}

static int resolution_index_from_wh(int w, int h) {
    for (int i = 0; i < VIDEO_MENU_RES_COUNT; ++i) {
        if (k_video_resolutions[i].w == w && k_video_resolutions[i].h == h) {
            return i;
        }
    }
    return -1;
}

static void map_mouse_to_scene_coords(const app* a, int mouse_x, int mouse_y, float* out_x, float* out_y);
static float drawable_scale_y(const app* a);

static float norm_range(float v, float lo, float hi) {
    if (hi <= lo) {
        return 0.0f;
    }
    float t = (v - lo) / (hi - lo);
    if (t < 0.0f) {
        return 0.0f;
    }
    if (t > 1.0f) {
        return 1.0f;
    }
    return t;
}

static void video_menu_dial_geometry(const app* a, vg_vec2 centers[VIDEO_MENU_DIAL_COUNT], float* radius_px) {
    const float w = (float)a->swapchain_extent.width;
    const float h = (float)a->swapchain_extent.height;
    const vg_rect panel = make_ui_safe_frame(w, h);
    const vg_rect lab = {panel.x + panel.w * 0.42f, panel.y + panel.h * 0.07f, panel.w * 0.54f, panel.h * 0.86f};
    *radius_px = lab.w * 0.052f;
    for (int i = 0; i < VIDEO_MENU_DIAL_COUNT; ++i) {
        const int row = i / 4;
        const int col = i % 4;
        const float cx = lab.x + lab.w * (0.12f + 0.25f * (float)col);
        const float cy = lab.y + lab.h * (0.72f - 0.29f * (float)row);
        centers[i] = (vg_vec2){cx, cy};
    }
}

static void crt_profile_from_video_dials(vg_crt_profile* crt, const float* dials) {
    if (!crt || !dials) {
        return;
    }
    crt->bloom_strength = lerpf(CRT_RANGE_BLOOM_STRENGTH_MIN, CRT_RANGE_BLOOM_STRENGTH_MAX, clampf(dials[0], 0.0f, 1.0f));
    crt->bloom_radius_px = lerpf(CRT_RANGE_BLOOM_RADIUS_MIN, CRT_RANGE_BLOOM_RADIUS_MAX, clampf(dials[1], 0.0f, 1.0f));
    crt->persistence_decay = lerpf(CRT_RANGE_PERSISTENCE_MIN, CRT_RANGE_PERSISTENCE_MAX, clampf(dials[2], 0.0f, 1.0f));
    crt->jitter_amount = lerpf(CRT_RANGE_JITTER_MIN, CRT_RANGE_JITTER_MAX, clampf(dials[3], 0.0f, 1.0f));
    crt->flicker_amount = lerpf(CRT_RANGE_FLICKER_MIN, CRT_RANGE_FLICKER_MAX, clampf(dials[4], 0.0f, 1.0f));
    crt->beam_core_width_px = lerpf(CRT_RANGE_BEAM_CORE_MIN, CRT_RANGE_BEAM_CORE_MAX, clampf(dials[5], 0.0f, 1.0f));
    crt->beam_halo_width_px = lerpf(CRT_RANGE_BEAM_HALO_MIN, CRT_RANGE_BEAM_HALO_MAX, clampf(dials[6], 0.0f, 1.0f));
    crt->beam_intensity = lerpf(CRT_RANGE_BEAM_INTENSITY_MIN, CRT_RANGE_BEAM_INTENSITY_MAX, clampf(dials[7], 0.0f, 1.0f));
    crt->scanline_strength = lerpf(CRT_RANGE_SCANLINE_MIN, CRT_RANGE_SCANLINE_MAX, clampf(dials[8], 0.0f, 1.0f));
    crt->noise_strength = lerpf(CRT_RANGE_NOISE_MIN, CRT_RANGE_NOISE_MAX, clampf(dials[9], 0.0f, 1.0f));
    crt->vignette_strength = lerpf(CRT_RANGE_VIGNETTE_MIN, CRT_RANGE_VIGNETTE_MAX, clampf(dials[10], 0.0f, 1.0f));
    crt->barrel_distortion = lerpf(CRT_RANGE_BARREL_MIN, CRT_RANGE_BARREL_MAX, clampf(dials[11], 0.0f, 1.0f));
}

static void video_dials_from_crt_profile(float* dials, const vg_crt_profile* crt) {
    if (!dials || !crt) {
        return;
    }
    dials[0] = norm_range(crt->bloom_strength, CRT_RANGE_BLOOM_STRENGTH_MIN, CRT_RANGE_BLOOM_STRENGTH_MAX);
    dials[1] = norm_range(crt->bloom_radius_px, CRT_RANGE_BLOOM_RADIUS_MIN, CRT_RANGE_BLOOM_RADIUS_MAX);
    dials[2] = norm_range(crt->persistence_decay, CRT_RANGE_PERSISTENCE_MIN, CRT_RANGE_PERSISTENCE_MAX);
    dials[3] = norm_range(crt->jitter_amount, CRT_RANGE_JITTER_MIN, CRT_RANGE_JITTER_MAX);
    dials[4] = norm_range(crt->flicker_amount, CRT_RANGE_FLICKER_MIN, CRT_RANGE_FLICKER_MAX);
    dials[5] = norm_range(crt->beam_core_width_px, CRT_RANGE_BEAM_CORE_MIN, CRT_RANGE_BEAM_CORE_MAX);
    dials[6] = norm_range(crt->beam_halo_width_px, CRT_RANGE_BEAM_HALO_MIN, CRT_RANGE_BEAM_HALO_MAX);
    dials[7] = norm_range(crt->beam_intensity, CRT_RANGE_BEAM_INTENSITY_MIN, CRT_RANGE_BEAM_INTENSITY_MAX);
    dials[8] = norm_range(crt->scanline_strength, CRT_RANGE_SCANLINE_MIN, CRT_RANGE_SCANLINE_MAX);
    dials[9] = norm_range(crt->noise_strength, CRT_RANGE_NOISE_MIN, CRT_RANGE_NOISE_MAX);
    dials[10] = norm_range(crt->vignette_strength, CRT_RANGE_VIGNETTE_MIN, CRT_RANGE_VIGNETTE_MAX);
    dials[11] = norm_range(crt->barrel_distortion, CRT_RANGE_BARREL_MIN, CRT_RANGE_BARREL_MAX);
}

static void sync_video_dials_from_live_crt(app* a) {
    if (!a || !a->vg) {
        return;
    }
    vg_crt_profile crt;
    vg_get_crt_profile(a->vg, &crt);
    video_dials_from_crt_profile(a->video_dial_01, &crt);
}

static void apply_video_lab_controls(app* a) {
    if (!a || !a->vg) {
        return;
    }
    vg_crt_profile crt;
    vg_get_crt_profile(a->vg, &crt);
    crt_profile_from_video_dials(&crt, a->video_dial_01);
    vg_set_crt_profile(a->vg, &crt);
}

static int update_video_menu_dial_drag(app* a, int mouse_x, int mouse_y) {
    if (!a || a->video_menu_dial_drag < 0 || a->video_menu_dial_drag >= VIDEO_MENU_DIAL_COUNT) {
        return 0;
    }
    float mx = 0.0f;
    float my = 0.0f;
    map_mouse_to_scene_coords(a, mouse_x, mouse_y, &mx, &my);
    const float h = fmaxf((float)a->swapchain_extent.height, 1.0f);
    const float dy = my - a->video_menu_dial_drag_start_y;
    const float t = a->video_menu_dial_drag_start_value + (dy / h) * 1.8f;
    a->video_dial_01[a->video_menu_dial_drag] = clampf(t, 0.0f, 1.0f);
    apply_video_lab_controls(a);
    a->force_clear_frames = 1;
    return 1;
}

static int ensure_dir_recursive(const char* path) {
    if (!path || path[0] == '\0') {
        return 0;
    }
    char tmp[PATH_MAX];
    size_t n = strlen(path);
    if (n >= sizeof(tmp)) {
        return 0;
    }
    memcpy(tmp, path, n + 1u);
    for (size_t i = 1; i < n; ++i) {
        if (tmp[i] == '/') {
            tmp[i] = '\0';
            if (tmp[0] != '\0') {
                if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
                    return 0;
                }
            }
            tmp[i] = '/';
        }
    }
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
        return 0;
    }
    return 1;
}

static int make_xdg_settings_path(char* out, size_t out_cap) {
    if (!out || out_cap == 0u) {
        return 0;
    }
    const char* xdg = getenv("XDG_CONFIG_HOME");
    if (xdg && xdg[0] != '\0') {
        int n = snprintf(out, out_cap, "%s/VectorSwarm/%s", xdg, SETTINGS_PATH);
        return (n > 0 && (size_t)n < out_cap) ? 1 : 0;
    }
    const char* home = getenv("HOME");
    if (!home || home[0] == '\0') {
        return 0;
    }
    int n = snprintf(out, out_cap, "%s/.config/VectorSwarm/%s", home, SETTINGS_PATH);
    return (n > 0 && (size_t)n < out_cap) ? 1 : 0;
}

static int save_settings_to_path(const app* a, const char* path) {
    if (!a || !path || path[0] == '\0') {
        return 0;
    }
    FILE* f = fopen(path, "w");
    if (!f) {
        return 0;
    }
    const int sel = a->video_menu_selected;
    int w = APP_WIDTH;
    int h = APP_HEIGHT;
    if (sel > 0 && sel <= VIDEO_MENU_RES_COUNT) {
        w = k_video_resolutions[sel - 1].w;
        h = k_video_resolutions[sel - 1].h;
    }
    fprintf(f, "fullscreen=%d\n", a->video_menu_fullscreen ? 1 : 0);
    fprintf(f, "selected=%d\n", sel);
    fprintf(f, "width=%d\n", w);
    fprintf(f, "height=%d\n", h);
    fprintf(f, "palette=%d\n", a->palette_mode);
    for (int i = 0; i < VIDEO_MENU_DIAL_COUNT; ++i) {
        fprintf(f, "dial%d=%.6f\n", i, clampf(a->video_dial_01[i], 0.0f, 1.0f));
    }
    fclose(f);
    return 1;
}

static int save_settings(const app* a) {
    char xdg_path[PATH_MAX];
    if (make_xdg_settings_path(xdg_path, sizeof(xdg_path))) {
        char dir[PATH_MAX];
        snprintf(dir, sizeof(dir), "%s", xdg_path);
        char* slash = strrchr(dir, '/');
        if (slash) {
            *slash = '\0';
            (void)ensure_dir_recursive(dir);
        }
        return save_settings_to_path(a, xdg_path);
    }
    return 0;
}

static int load_settings_from_path(app* a, const char* path) {
    if (!a || !path) {
        return 0;
    }
    FILE* f = fopen(path, "r");
    if (!f) {
        return 0;
    }
    int fullscreen = a->video_menu_fullscreen;
    int selected = a->video_menu_selected;
    int width = -1;
    int height = -1;
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        char key[64];
        char value[64];
        if (sscanf(line, "%63[^=]=%63s", key, value) != 2) {
            continue;
        }
        if (strcmp(key, "fullscreen") == 0) {
            fullscreen = (atoi(value) != 0) ? 1 : 0;
        } else if (strcmp(key, "selected") == 0) {
            selected = atoi(value);
        } else if (strcmp(key, "width") == 0) {
            width = atoi(value);
        } else if (strcmp(key, "height") == 0) {
            height = atoi(value);
        } else if (strcmp(key, "palette") == 0) {
            a->palette_mode = atoi(value);
        } else {
            int dial = -1;
            if (sscanf(key, "dial%d", &dial) == 1 && dial >= 0 && dial < VIDEO_MENU_DIAL_COUNT) {
                a->video_dial_01[dial] = clampf(strtof(value, NULL), 0.0f, 1.0f);
            }
        }
    }
    fclose(f);

    if (selected < 0 || selected > VIDEO_MENU_RES_COUNT) {
        selected = 1;
    }
    if (width > 0 && height > 0) {
        int idx = resolution_index_from_wh(width, height);
        if (idx >= 0) {
            selected = idx + 1;
        }
    }
    if (!fullscreen && selected == 0) {
        selected = 1;
    }
    if (a->palette_mode < 0 || a->palette_mode > 2) {
        a->palette_mode = 0;
    }
    a->video_menu_fullscreen = fullscreen ? 1 : 0;
    a->video_menu_selected = a->video_menu_fullscreen ? 0 : selected;
    return 1;
}

static int load_settings(app* a) {
    if (!a) {
        return 0;
    }
    char xdg_path[PATH_MAX];
    if (make_xdg_settings_path(xdg_path, sizeof(xdg_path))) {
        if (load_settings_from_path(a, xdg_path)) {
            return 1;
        }
    }
    return 0;
}

static void map_mouse_to_scene_coords(const app* a, int mouse_x, int mouse_y, float* out_x, float* out_y) {
    if (!a || !out_x || !out_y) {
        return;
    }
    const float w = (float)a->swapchain_extent.width;
    const float h = (float)a->swapchain_extent.height;
    if (w <= 1.0f || h <= 1.0f) {
        *out_x = (float)mouse_x;
        *out_y = 0.0f;
        return;
    }

    int win_w = 0;
    int win_h = 0;
    SDL_GetWindowSize(a->window, &win_w, &win_h);
    if (win_w <= 0) win_w = (int)w;
    if (win_h <= 0) win_h = (int)h;

    const float sx = w / (float)win_w;
    const float sy = h / (float)win_h;
    float x = clampf((float)mouse_x * sx, 0.0f, w);
    float y = clampf(((float)win_h - (float)mouse_y) * sy, 0.0f, h);
    if (a->show_acoustics || a->show_crt_ui || a->show_video_menu) {
        vg_crt_profile crt;
        vg_get_crt_profile(a->vg, &crt);
        const float k = clampf(crt.barrel_distortion, 0.0f, 0.30f);
        const float u = x / w;
        const float v = y / h;
        float qx = u * 2.0f - 1.0f;
        float qy = v * 2.0f - 1.0f;
        const float r2 = qx * qx + qy * qy;
        const float m = 1.0f + k * r2;
        qx *= m;
        qy *= m;
        x = clampf((qx * 0.5f + 0.5f) * w, 0.0f, w);
        y = clampf((qy * 0.5f + 0.5f) * h, 0.0f, h);
    }
    *out_x = x;
    *out_y = y;
}

static float drawable_scale_y(const app* a) {
    if (!a || !a->window) {
        return 1.0f;
    }
    int win_w = 0;
    int win_h = 0;
    SDL_GetWindowSize(a->window, &win_w, &win_h);
    if (win_h <= 0) {
        return 1.0f;
    }
    int draw_w = 0;
    int draw_h = 0;
    SDL_Vulkan_GetDrawableSize(a->window, &draw_w, &draw_h);
    if (draw_h <= 0) {
        draw_h = (int)a->swapchain_extent.height;
    }
    const float s = (float)draw_h / (float)win_h;
    return clampf(s, 1.0f, 3.0f);
}

static void set_tty_message(app* a, const char* msg) {
    if (!a || !msg) {
        return;
    }
    snprintf(a->wave_tty_text, sizeof(a->wave_tty_text), "%s", msg);
    vg_text_fx_typewriter_set_text(&a->wave_tty, a->wave_tty_text);
    vg_text_fx_typewriter_reset(&a->wave_tty);
    a->wave_tty.timer_s = 0.02f;
}

static void sync_planetarium_marquee(app* a) {
    vg_text_fx_marquee_set_text(&a->planetarium_marquee, k_planetarium_propaganda_marquee);
}

static void announce_planetarium_selection(app* a) {
    if (!a) {
        return;
    }
    const planetary_system_def* sys = app_planetarium_system(a);
    const int planet_count = app_planetarium_planet_count(a);
    if (sys && a->planetarium_selected >= 0 && a->planetarium_selected < planet_count &&
        sys->planets && a->planetarium_selected < sys->planet_count) {
        const char* title = sys->planets[a->planetarium_selected].display_name;
        if (title && title[0] != '\0') {
            set_tty_message(a, title);
            return;
        }
    }
    if (sys && sys->boss_gate_label && sys->boss_gate_label[0] != '\0') {
        set_tty_message(a, sys->boss_gate_label);
    } else {
        set_tty_message(a, "BOSS GATE");
    }
}

static void trigger_fire_test(app* a) {
    if (!a || !a->audio_ready) {
        return;
    }
    atomic_fetch_add_explicit(&a->pending_fire_events, 1u, memory_order_acq_rel);
}

static int audio_spatial_enqueue(app* a, uint8_t type, float pan, float gain) {
    if (!a || !a->audio_ready) {
        return 0;
    }
    const uint32_t cap = AUDIO_SPATIAL_EVENT_CAP;
    const uint32_t w = atomic_load_explicit(&a->audio_spatial_write, memory_order_relaxed);
    const uint32_t r = atomic_load_explicit(&a->audio_spatial_read, memory_order_acquire);
    const uint32_t next = (w + 1u) % cap;
    if (next == r) {
        return 0;
    }
    audio_spatial_event* e = &a->audio_spatial_q[w];
    e->type = type;
    e->pan = clampf(pan, -1.0f, 1.0f);
    e->gain = clampf(gain, 0.0f, 2.0f);
    atomic_store_explicit(&a->audio_spatial_write, next, memory_order_release);
    return 1;
}

static void trigger_thruster_test(app* a) {
    if (!a || !a->audio_ready) {
        return;
    }
    atomic_fetch_add_explicit(&a->pending_thruster_tests, 1u, memory_order_acq_rel);
}

static void trigger_enemy_fire_test(app* a) {
    if (!a || !a->audio_ready) {
        return;
    }
    atomic_fetch_add_explicit(&a->pending_enemy_fire_tests, 1u, memory_order_acq_rel);
}

static void trigger_explosion_test(app* a) {
    if (!a || !a->audio_ready) {
        return;
    }
    atomic_fetch_add_explicit(&a->pending_explosion_tests, 1u, memory_order_acq_rel);
}

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

static void apply_acoustics(app* a);
static void trigger_fire_test(app* a);
static void trigger_thruster_test(app* a);
static void trigger_enemy_fire_test(app* a);
static void trigger_explosion_test(app* a);
static int create_swapchain(app* a);
static int create_render_passes(app* a);
static int create_offscreen_targets(app* a);
static int create_present_framebuffers(app* a);
static int create_commands(app* a);
static int create_sync(app* a);
static int create_post_resources(app* a);
static int create_terrain_resources(app* a);
static int create_particle_resources(app* a);
static int create_wormhole_resources(app* a);
static int create_vg_context(app* a);
static void set_tty_message(app* a, const char* msg);
static void update_gpu_high_plains_vertices(app* a);
static void record_gpu_high_plains_terrain(app* a, VkCommandBuffer cmd);
static void update_gpu_particle_instances(app* a);
static void record_gpu_particles(app* a, VkCommandBuffer cmd);
static void record_gpu_particles_bloom(app* a, VkCommandBuffer cmd);
static void record_gpu_wormhole(app* a, VkCommandBuffer cmd);
static void reset_terrain_tuning(app* a);
static void sync_terrain_tuning_text(app* a);
static int handle_terrain_tuning_key(app* a, SDL_Keycode key);
static int apply_video_mode(app* a);
static void map_mouse_to_scene_coords(const app* a, int mouse_x, int mouse_y, float* out_x, float* out_y);

static int audio_spatial_enqueue(app* a, uint8_t type, float pan, float gain);

static float acoustics_value_to_display(int id, float t01) {
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

static float round_to(float v, float step) {
    if (step <= 0.0f) {
        return v;
    }
    return floorf(v / step + 0.5f) * step;
}

static float acoustics_value_to_ui_display(int id, float t01) {
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
            return round_to(v, 0.01f);
    }
}

static float acoustics_combat_value_to_display(int id, float t01) {
    const float t = clampf(t01, 0.0f, 1.0f);
    switch (id) {
        case ACOUST_COMBAT_ENEMY_LEVEL:
        case ACOUST_COMBAT_EXP_LEVEL:
            return lerpf(0.02f, 0.95f, t);
        case ACOUST_COMBAT_ENEMY_PITCH:
            return lerpf(150.0f, 1800.0f, t);
        case ACOUST_COMBAT_EXP_PITCH:
            return lerpf(40.0f, 280.0f, t);
        case ACOUST_COMBAT_ENEMY_ATTACK:
        case ACOUST_COMBAT_EXP_ATTACK:
            return lerpf(0.1f, 45.0f, t);
        case ACOUST_COMBAT_ENEMY_DECAY:
            return lerpf(14.0f, 280.0f, t);
        case ACOUST_COMBAT_EXP_DECAY:
            return lerpf(60.0f, 900.0f, t);
        case ACOUST_COMBAT_ENEMY_NOISE:
        case ACOUST_COMBAT_EXP_NOISE:
            return t;
        case ACOUST_COMBAT_EXP_FM_DEPTH:
            return lerpf(0.0f, 420.0f, t);
        case ACOUST_COMBAT_EXP_FM_RATE:
            return lerpf(8.0f, 1600.0f, t);
        case ACOUST_COMBAT_ENEMY_PANW:
        case ACOUST_COMBAT_EXP_PANW:
            return lerpf(0.25f, 1.20f, t);
        default:
            return t;
    }
}

static float acoustics_combat_value_to_ui_display(int id, float t01) {
    const float v = acoustics_combat_value_to_display(id, t01);
    switch (id) {
        case ACOUST_COMBAT_ENEMY_LEVEL:
        case ACOUST_COMBAT_EXP_LEVEL:
        case ACOUST_COMBAT_ENEMY_NOISE:
        case ACOUST_COMBAT_EXP_NOISE:
        case ACOUST_COMBAT_ENEMY_PANW:
        case ACOUST_COMBAT_EXP_PANW:
            return round_to(v, 0.01f);
        case ACOUST_COMBAT_ENEMY_ATTACK:
        case ACOUST_COMBAT_EXP_ATTACK:
        case ACOUST_COMBAT_ENEMY_DECAY:
        case ACOUST_COMBAT_EXP_DECAY:
        case ACOUST_COMBAT_EXP_FM_DEPTH:
        case ACOUST_COMBAT_EXP_FM_RATE:
            return round_to(v, 1.0f);
        case ACOUST_COMBAT_ENEMY_PITCH:
        case ACOUST_COMBAT_EXP_PITCH:
            return round_to(v, 1.0f);
        default:
            return round_to(v, 0.01f);
    }
}

static void acoustics_defaults(app* a) {
    /* Seed first-install defaults from current tuned build/acoustics_slots.cfg:
       fire slot 3 (fsel=3) and thruster slot 1 (tsel=1). */
    a->acoustics_value_01[ACOUST_FIRE_WAVE] = 0.275879592f;
    a->acoustics_value_01[ACOUST_FIRE_PITCH] = 0.602183819f;
    a->acoustics_value_01[ACOUST_FIRE_ATTACK] = 0.003753547f;
    a->acoustics_value_01[ACOUST_FIRE_DECAY] = 0.460912049f;
    a->acoustics_value_01[ACOUST_FIRE_CUTOFF] = 0.100429699f;
    a->acoustics_value_01[ACOUST_FIRE_RESONANCE] = 0.985629857f;
    a->acoustics_value_01[ACOUST_FIRE_SWEEP_ST] = 0.949483037f;
    a->acoustics_value_01[ACOUST_FIRE_SWEEP_DECAY] = 0.827205420f;
    a->acoustics_value_01[ACOUST_THR_LEVEL] = 0.570973873f;
    a->acoustics_value_01[ACOUST_THR_PITCH] = 0.997384906f;
    a->acoustics_value_01[ACOUST_THR_ATTACK] = 0.814027071f;
    a->acoustics_value_01[ACOUST_THR_RELEASE] = 0.294867337f;
    a->acoustics_value_01[ACOUST_THR_CUTOFF] = 0.035423841f;
    a->acoustics_value_01[ACOUST_THR_RESONANCE] = 0.998682797f;
}

static void acoustics_combat_defaults(app* a) {
    if (!a) {
        return;
    }
    a->acoustics_combat_value_01[ACOUST_COMBAT_ENEMY_LEVEL] = 0.40f;
    a->acoustics_combat_value_01[ACOUST_COMBAT_ENEMY_PITCH] = 0.36f;
    a->acoustics_combat_value_01[ACOUST_COMBAT_ENEMY_ATTACK] = 0.05f;
    a->acoustics_combat_value_01[ACOUST_COMBAT_ENEMY_DECAY] = 0.36f;
    a->acoustics_combat_value_01[ACOUST_COMBAT_ENEMY_NOISE] = 0.20f;
    a->acoustics_combat_value_01[ACOUST_COMBAT_ENEMY_PANW] = 0.78f;
    a->acoustics_combat_value_01[ACOUST_COMBAT_EXP_LEVEL] = 0.58f;
    a->acoustics_combat_value_01[ACOUST_COMBAT_EXP_PITCH] = 0.28f;
    a->acoustics_combat_value_01[ACOUST_COMBAT_EXP_ATTACK] = 0.07f;
    a->acoustics_combat_value_01[ACOUST_COMBAT_EXP_DECAY] = 0.54f;
    a->acoustics_combat_value_01[ACOUST_COMBAT_EXP_NOISE] = 0.64f;
    a->acoustics_combat_value_01[ACOUST_COMBAT_EXP_FM_DEPTH] = 0.28f;
    a->acoustics_combat_value_01[ACOUST_COMBAT_EXP_FM_RATE] = 0.21f;
    a->acoustics_combat_value_01[ACOUST_COMBAT_EXP_PANW] = 0.90f;
}

static void acoustics_slot_defaults(app* a) {
    if (!a) {
        return;
    }
    a->acoustics_fire_slot_selected = 0;
    a->acoustics_thr_slot_selected = 0;
    a->acoustics_enemy_slot_selected = 0;
    a->acoustics_exp_slot_selected = 0;
    memset(a->acoustics_fire_slot_defined, 0, sizeof(a->acoustics_fire_slot_defined));
    memset(a->acoustics_thr_slot_defined, 0, sizeof(a->acoustics_thr_slot_defined));
    memset(a->acoustics_enemy_slot_defined, 0, sizeof(a->acoustics_enemy_slot_defined));
    memset(a->acoustics_exp_slot_defined, 0, sizeof(a->acoustics_exp_slot_defined));
    memset(a->acoustics_fire_slots, 0, sizeof(a->acoustics_fire_slots));
    memset(a->acoustics_thr_slots, 0, sizeof(a->acoustics_thr_slots));
    memset(a->acoustics_enemy_slots, 0, sizeof(a->acoustics_enemy_slots));
    memset(a->acoustics_exp_slots, 0, sizeof(a->acoustics_exp_slots));
    for (int i = 0; i < 8; ++i) {
        a->acoustics_fire_slots[0][i] = a->acoustics_value_01[i];
    }
    for (int i = 0; i < 6; ++i) {
        a->acoustics_thr_slots[0][i] = a->acoustics_value_01[8 + i];
        a->acoustics_enemy_slots[0][i] = a->acoustics_combat_value_01[i];
    }
    for (int i = 0; i < 8; ++i) {
        a->acoustics_exp_slots[0][i] = a->acoustics_combat_value_01[6 + i];
    }
    a->acoustics_fire_slot_defined[0] = 1u;
    a->acoustics_thr_slot_defined[0] = 1u;
    a->acoustics_enemy_slot_defined[0] = 1u;
    a->acoustics_exp_slot_defined[0] = 1u;
}

static void capture_current_to_selected_slot(app* a, int is_fire) {
    if (!a) {
        return;
    }
    if (is_fire) {
        const int s = a->acoustics_fire_slot_selected;
        if (s < 0 || s >= ACOUSTICS_SLOT_COUNT) {
            return;
        }
        for (int i = 0; i < 8; ++i) {
            a->acoustics_fire_slots[s][i] = a->acoustics_value_01[i];
        }
        a->acoustics_fire_slot_defined[s] = 1u;
        return;
    }
    {
        const int s = a->acoustics_thr_slot_selected;
        if (s < 0 || s >= ACOUSTICS_SLOT_COUNT) {
            return;
        }
        for (int i = 0; i < 6; ++i) {
            a->acoustics_thr_slots[s][i] = a->acoustics_value_01[8 + i];
        }
        a->acoustics_thr_slot_defined[s] = 1u;
    }
}

static void capture_current_to_selected_slots(app* a) {
    if (!a) {
        return;
    }
    capture_current_to_selected_slot(a, 1);
    capture_current_to_selected_slot(a, 0);
}

static void capture_current_to_selected_combat_slot(app* a, int is_enemy) {
    if (!a) {
        return;
    }
    if (is_enemy) {
        const int s = a->acoustics_enemy_slot_selected;
        if (s < 0 || s >= ACOUSTICS_SLOT_COUNT) {
            return;
        }
        for (int i = 0; i < 6; ++i) {
            a->acoustics_enemy_slots[s][i] = a->acoustics_combat_value_01[i];
        }
        a->acoustics_enemy_slot_defined[s] = 1u;
    } else {
        const int s = a->acoustics_exp_slot_selected;
        if (s < 0 || s >= ACOUSTICS_SLOT_COUNT) {
            return;
        }
        for (int i = 0; i < 8; ++i) {
            a->acoustics_exp_slots[s][i] = a->acoustics_combat_value_01[6 + i];
        }
        a->acoustics_exp_slot_defined[s] = 1u;
    }
}

static void capture_current_to_selected_combat_slots(app* a) {
    if (!a) {
        return;
    }
    capture_current_to_selected_combat_slot(a, 1);
    capture_current_to_selected_combat_slot(a, 0);
}

static void load_slot_to_current(app* a, int is_fire, int slot_idx, int apply_now) {
    if (!a || slot_idx < 0 || slot_idx >= ACOUSTICS_SLOT_COUNT) {
        return;
    }
    if (is_fire) {
        if (!a->acoustics_fire_slot_defined[slot_idx]) {
            return;
        }
        for (int i = 0; i < 8; ++i) {
            a->acoustics_value_01[i] = a->acoustics_fire_slots[slot_idx][i];
        }
    } else {
        if (!a->acoustics_thr_slot_defined[slot_idx]) {
            return;
        }
        for (int i = 0; i < 6; ++i) {
            a->acoustics_value_01[8 + i] = a->acoustics_thr_slots[slot_idx][i];
        }
    }
    if (apply_now) {
        apply_acoustics(a);
    }
}

static void load_combat_slot_to_current(app* a, int is_enemy, int slot_idx, int apply_now) {
    if (!a || slot_idx < 0 || slot_idx >= ACOUSTICS_SLOT_COUNT) {
        return;
    }
    if (is_enemy) {
        if (!a->acoustics_enemy_slot_defined[slot_idx]) {
            return;
        }
        for (int i = 0; i < 6; ++i) {
            a->acoustics_combat_value_01[i] = a->acoustics_enemy_slots[slot_idx][i];
        }
    } else {
        if (!a->acoustics_exp_slot_defined[slot_idx]) {
            return;
        }
        for (int i = 0; i < 8; ++i) {
            a->acoustics_combat_value_01[6 + i] = a->acoustics_exp_slots[slot_idx][i];
        }
    }
    if (apply_now) {
        apply_acoustics(a);
    }
}

static int save_acoustics_slots(const app* a, const char* path) {
    if (!a || !path) {
        return 0;
    }
    FILE* f = fopen(path, "w");
    if (!f) {
        return 0;
    }
    fprintf(f, "version=2\n");
    fprintf(f, "fsel=%d\n", a->acoustics_fire_slot_selected);
    fprintf(f, "tsel=%d\n", a->acoustics_thr_slot_selected);
    fprintf(f, "cfsel=%d\n", a->acoustics_enemy_slot_selected);
    fprintf(f, "ctsel=%d\n", a->acoustics_exp_slot_selected);
    for (int s = 0; s < ACOUSTICS_SLOT_COUNT; ++s) {
        fprintf(f, "fd%d=%d\n", s, a->acoustics_fire_slot_defined[s] ? 1 : 0);
        fprintf(f, "td%d=%d\n", s, a->acoustics_thr_slot_defined[s] ? 1 : 0);
        fprintf(f, "cfd%d=%d\n", s, a->acoustics_enemy_slot_defined[s] ? 1 : 0);
        fprintf(f, "ctd%d=%d\n", s, a->acoustics_exp_slot_defined[s] ? 1 : 0);
        for (int i = 0; i < 8; ++i) {
            fprintf(f, "fv%d_%d=%.9f\n", s, i, a->acoustics_fire_slots[s][i]);
        }
        for (int i = 0; i < 6; ++i) {
            fprintf(f, "tv%d_%d=%.9f\n", s, i, a->acoustics_thr_slots[s][i]);
            fprintf(f, "cfv%d_%d=%.9f\n", s, i, a->acoustics_enemy_slots[s][i]);
        }
        for (int i = 0; i < 8; ++i) {
            fprintf(f, "ctv%d_%d=%.9f\n", s, i, a->acoustics_exp_slots[s][i]);
        }
    }
    for (int i = 0; i < ACOUST_COMBAT_SLIDER_COUNT; ++i) {
        fprintf(f, "cv%d=%.9f\n", i, a->acoustics_combat_value_01[i]);
    }
    fclose(f);
    return 1;
}

static int load_acoustics_slots(app* a, const char* path) {
    if (!a || !path) {
        return 0;
    }
    FILE* f = fopen(path, "r");
    if (!f) {
        return 0;
    }
    char key[64];
    float v = 0.0f;
    while (fscanf(f, "%63[^=]=%f\n", key, &v) == 2) {
        if (strcmp(key, "fsel") == 0) {
            a->acoustics_fire_slot_selected = (int)clampf(v, 0.0f, (float)(ACOUSTICS_SLOT_COUNT - 1));
            continue;
        }
        if (strcmp(key, "tsel") == 0) {
            a->acoustics_thr_slot_selected = (int)clampf(v, 0.0f, (float)(ACOUSTICS_SLOT_COUNT - 1));
            continue;
        }
        if (strcmp(key, "cfsel") == 0) {
            a->acoustics_enemy_slot_selected = (int)clampf(v, 0.0f, (float)(ACOUSTICS_SLOT_COUNT - 1));
            continue;
        }
        if (strcmp(key, "ctsel") == 0) {
            a->acoustics_exp_slot_selected = (int)clampf(v, 0.0f, (float)(ACOUSTICS_SLOT_COUNT - 1));
            continue;
        }
        int s = 0;
        int i = 0;
        if (sscanf(key, "fd%d", &s) == 1 && s >= 0 && s < ACOUSTICS_SLOT_COUNT) {
            a->acoustics_fire_slot_defined[s] = (v >= 0.5f) ? 1u : 0u;
            continue;
        }
        if (sscanf(key, "td%d", &s) == 1 && s >= 0 && s < ACOUSTICS_SLOT_COUNT) {
            a->acoustics_thr_slot_defined[s] = (v >= 0.5f) ? 1u : 0u;
            continue;
        }
        if (sscanf(key, "cfd%d", &s) == 1 && s >= 0 && s < ACOUSTICS_SLOT_COUNT) {
            a->acoustics_enemy_slot_defined[s] = (v >= 0.5f) ? 1u : 0u;
            continue;
        }
        if (sscanf(key, "ctd%d", &s) == 1 && s >= 0 && s < ACOUSTICS_SLOT_COUNT) {
            a->acoustics_exp_slot_defined[s] = (v >= 0.5f) ? 1u : 0u;
            continue;
        }
        if (sscanf(key, "fv%d_%d", &s, &i) == 2 && s >= 0 && s < ACOUSTICS_SLOT_COUNT && i >= 0 && i < 8) {
            a->acoustics_fire_slots[s][i] = clampf(v, 0.0f, 1.0f);
            continue;
        }
        if (sscanf(key, "tv%d_%d", &s, &i) == 2 && s >= 0 && s < ACOUSTICS_SLOT_COUNT && i >= 0 && i < 6) {
            a->acoustics_thr_slots[s][i] = clampf(v, 0.0f, 1.0f);
            continue;
        }
        if (sscanf(key, "cfv%d_%d", &s, &i) == 2 && s >= 0 && s < ACOUSTICS_SLOT_COUNT && i >= 0 && i < 6) {
            a->acoustics_enemy_slots[s][i] = clampf(v, 0.0f, 1.0f);
            continue;
        }
        if (sscanf(key, "ctv%d_%d", &s, &i) == 2 && s >= 0 && s < ACOUSTICS_SLOT_COUNT && i >= 0 && i < 8) {
            a->acoustics_exp_slots[s][i] = clampf(v, 0.0f, 1.0f);
            continue;
        }
        if (sscanf(key, "cv%d", &i) == 1 && i >= 0 && i < ACOUST_COMBAT_SLIDER_COUNT) {
            a->acoustics_combat_value_01[i] = clampf(v, 0.0f, 1.0f);
            continue;
        }
    }
    fclose(f);
    load_slot_to_current(a, 1, a->acoustics_fire_slot_selected, 0);
    load_slot_to_current(a, 0, a->acoustics_thr_slot_selected, 0);
    load_combat_slot_to_current(a, 1, a->acoustics_enemy_slot_selected, 0);
    load_combat_slot_to_current(a, 0, a->acoustics_exp_slot_selected, 1);
    return 1;
}

static void apply_acoustics_locked(app* a) {
    const int fire_wave_idx = (int)floorf(clampf(a->acoustics_value_01[ACOUST_FIRE_WAVE], 0.0f, 1.0f) * 4.0f + 0.5f);
    enum wtp_waveform_type fire_wave = (enum wtp_waveform_type)fire_wave_idx;
    if (fire_wave >= WTP_WT_TYPES) {
        fire_wave = WTP_WT_SAW;
    }
    wtp_set_waveform(&a->weapon_synth, fire_wave);
    wtp_set_adsr_ms(
        &a->weapon_synth,
        acoustics_value_to_display(ACOUST_FIRE_ATTACK, a->acoustics_value_01[ACOUST_FIRE_ATTACK]),
        acoustics_value_to_display(ACOUST_FIRE_DECAY, a->acoustics_value_01[ACOUST_FIRE_DECAY]),
        0.0f,
        80.0f
    );
    wtp_set_pitch_env(
        &a->weapon_synth,
        acoustics_value_to_display(ACOUST_FIRE_SWEEP_ST, a->acoustics_value_01[ACOUST_FIRE_SWEEP_ST]),
        0.0f,
        acoustics_value_to_display(ACOUST_FIRE_SWEEP_DECAY, a->acoustics_value_01[ACOUST_FIRE_SWEEP_DECAY])
    );
    wtp_set_filter(
        &a->weapon_synth,
        acoustics_value_to_display(ACOUST_FIRE_CUTOFF, a->acoustics_value_01[ACOUST_FIRE_CUTOFF]),
        acoustics_value_to_display(ACOUST_FIRE_RESONANCE, a->acoustics_value_01[ACOUST_FIRE_RESONANCE])
    );
    a->weapon_synth.gain = 0.40f;
    a->weapon_synth.clip_level = 0.92f;

    wtp_set_waveform(&a->thruster_synth, WTP_WT_NOISE);
    wtp_set_adsr_ms(
        &a->thruster_synth,
        acoustics_value_to_display(ACOUST_THR_ATTACK, a->acoustics_value_01[ACOUST_THR_ATTACK]),
        30.0f,
        0.92f,
        acoustics_value_to_display(ACOUST_THR_RELEASE, a->acoustics_value_01[ACOUST_THR_RELEASE])
    );
    wtp_set_filter(
        &a->thruster_synth,
        acoustics_value_to_display(ACOUST_THR_CUTOFF, a->acoustics_value_01[ACOUST_THR_CUTOFF]),
        acoustics_value_to_display(ACOUST_THR_RESONANCE, a->acoustics_value_01[ACOUST_THR_RESONANCE])
    );
    a->thruster_synth.gain = acoustics_value_to_display(ACOUST_THR_LEVEL, a->acoustics_value_01[ACOUST_THR_LEVEL]);
    a->thruster_synth.clip_level = 0.85f;

    a->enemy_fire_sound.level = acoustics_combat_value_to_display(ACOUST_COMBAT_ENEMY_LEVEL, a->acoustics_combat_value_01[ACOUST_COMBAT_ENEMY_LEVEL]);
    a->enemy_fire_sound.pitch_hz = acoustics_combat_value_to_display(ACOUST_COMBAT_ENEMY_PITCH, a->acoustics_combat_value_01[ACOUST_COMBAT_ENEMY_PITCH]);
    a->enemy_fire_sound.attack_ms = acoustics_combat_value_to_display(ACOUST_COMBAT_ENEMY_ATTACK, a->acoustics_combat_value_01[ACOUST_COMBAT_ENEMY_ATTACK]);
    a->enemy_fire_sound.decay_ms = acoustics_combat_value_to_display(ACOUST_COMBAT_ENEMY_DECAY, a->acoustics_combat_value_01[ACOUST_COMBAT_ENEMY_DECAY]);
    a->enemy_fire_sound.noise_mix = acoustics_combat_value_to_display(ACOUST_COMBAT_ENEMY_NOISE, a->acoustics_combat_value_01[ACOUST_COMBAT_ENEMY_NOISE]);
    a->enemy_fire_sound.fm_depth_hz = 0.0f;
    a->enemy_fire_sound.fm_rate_hz = 0.0f;
    a->enemy_fire_sound.pan_width = acoustics_combat_value_to_display(ACOUST_COMBAT_ENEMY_PANW, a->acoustics_combat_value_01[ACOUST_COMBAT_ENEMY_PANW]);

    a->explosion_sound.level = acoustics_combat_value_to_display(ACOUST_COMBAT_EXP_LEVEL, a->acoustics_combat_value_01[ACOUST_COMBAT_EXP_LEVEL]);
    a->explosion_sound.pitch_hz = acoustics_combat_value_to_display(ACOUST_COMBAT_EXP_PITCH, a->acoustics_combat_value_01[ACOUST_COMBAT_EXP_PITCH]);
    a->explosion_sound.attack_ms = acoustics_combat_value_to_display(ACOUST_COMBAT_EXP_ATTACK, a->acoustics_combat_value_01[ACOUST_COMBAT_EXP_ATTACK]);
    a->explosion_sound.decay_ms = acoustics_combat_value_to_display(ACOUST_COMBAT_EXP_DECAY, a->acoustics_combat_value_01[ACOUST_COMBAT_EXP_DECAY]);
    a->explosion_sound.noise_mix = acoustics_combat_value_to_display(ACOUST_COMBAT_EXP_NOISE, a->acoustics_combat_value_01[ACOUST_COMBAT_EXP_NOISE]);
    a->explosion_sound.fm_depth_hz = acoustics_combat_value_to_display(ACOUST_COMBAT_EXP_FM_DEPTH, a->acoustics_combat_value_01[ACOUST_COMBAT_EXP_FM_DEPTH]);
    a->explosion_sound.fm_rate_hz = acoustics_combat_value_to_display(ACOUST_COMBAT_EXP_FM_RATE, a->acoustics_combat_value_01[ACOUST_COMBAT_EXP_FM_RATE]);
    a->explosion_sound.pan_width = acoustics_combat_value_to_display(ACOUST_COMBAT_EXP_PANW, a->acoustics_combat_value_01[ACOUST_COMBAT_EXP_PANW]);
}

static void apply_acoustics(app* a) {
    if (!a->audio_ready || a->audio_dev == 0) {
        return;
    }
    SDL_LockAudioDevice(a->audio_dev);
    apply_acoustics_locked(a);
    SDL_UnlockAudioDevice(a->audio_dev);
}

static vg_ui_slider_panel_metrics make_scaled_slider_metrics(float ui, float value_col_width_px) {
    vg_ui_slider_panel_metrics m;
    vg_ui_slider_panel_default_metrics(&m);
    m.pad_left_px *= ui;
    m.pad_top_px *= ui;
    m.pad_right_px *= ui;
    m.pad_bottom_px *= ui;
    m.title_line_gap_px *= ui;
    m.rows_top_offset_px *= ui;
    m.col_gap_px *= ui;
    m.value_col_width_px = value_col_width_px;
    m.row_label_height_sub_px *= ui;
    m.row_slider_y_offset_px *= ui;
    m.row_slider_height_sub_px *= ui;
    m.value_y_offset_px *= ui;
    m.footer_y_from_bottom_px *= ui;
    m.title_sub_size_delta_px *= ui;
    m.label_size_bias_px *= ui;
    m.footer_size_bias_px *= ui;
    return m;
}

static int handle_acoustics_ui_mouse(app* a, int mouse_x, int mouse_y, int set_value) {
    const float w = (float)a->swapchain_extent.width;
    const float h = (float)a->swapchain_extent.height;
    const float ui = ui_reference_scale(w, h);
    float display_values[ACOUSTICS_SLIDER_COUNT];
    int display_count = ACOUSTICS_SLIDER_COUNT;
    if (a->acoustics_page == ACOUSTICS_PAGE_COMBAT) {
        display_count = ACOUST_COMBAT_SLIDER_COUNT;
        for (int i = 0; i < display_count; ++i) {
            display_values[i] = acoustics_combat_value_to_ui_display(i, a->acoustics_combat_value_01[i]);
        }
    } else {
        for (int i = 0; i < display_count; ++i) {
            display_values[i] = acoustics_value_to_ui_display(i, a->acoustics_value_01[i]);
        }
    }
    const float value_col_width_px = acoustics_compute_value_col_width(
        ui,
        11.5f * ui,
        display_values,
        display_count
    );
    const acoustics_ui_layout l = make_acoustics_ui_layout(w, h, value_col_width_px, (a->acoustics_page == ACOUSTICS_PAGE_COMBAT) ? 6 : 8, (a->acoustics_page == ACOUSTICS_PAGE_COMBAT) ? 8 : 6);
    const vg_rect page_btn = acoustics_page_toggle_button_rect(w, h);
    float mx = 0.0f;
    float my = 0.0f;
    map_mouse_to_scene_coords(a, mouse_x, mouse_y, &mx, &my);
    if (mx >= page_btn.x && mx <= page_btn.x + page_btn.w && my >= page_btn.y && my <= page_btn.y + page_btn.h) {
        if (set_value) {
            a->acoustics_page = (a->acoustics_page + 1) % ACOUSTICS_PAGE_COUNT;
        }
        return 1;
    }
    for (int p = 0; p < 2; ++p) {
        const vg_rect r = l.panel[p];
        if (mx < r.x || mx > r.x + r.w || my < r.y || my > r.y + r.h) {
            continue;
        }
        {
            const vg_rect btn = l.button[p];
            if (mx >= btn.x && mx <= btn.x + btn.w && my >= btn.y && my <= btn.y + btn.h) {
                if (set_value) {
                    if (a->acoustics_page == ACOUSTICS_PAGE_COMBAT) {
                        if (p == 0) {
                            trigger_enemy_fire_test(a);
                        } else {
                            trigger_explosion_test(a);
                        }
                    } else {
                        if (p == 0) {
                            trigger_fire_test(a);
                        } else {
                            trigger_thruster_test(a);
                        }
                    }
                }
                return 1;
            }
        }
        {
            const vg_rect b = l.save_button[p];
            if (mx >= b.x && mx <= b.x + b.w && my >= b.y && my <= b.y + b.h) {
                if (set_value) {
                    if (a->acoustics_page == ACOUSTICS_PAGE_SYNTH) {
                        capture_current_to_selected_slot(a, (p == 0) ? 1 : 0);
                    } else {
                        capture_current_to_selected_combat_slot(a, (p == 0) ? 1 : 0);
                    }
                    (void)save_acoustics_slots(a, ACOUSTICS_SLOTS_PATH);
                }
                return 1;
            }
        }
        for (int s = 0; s < ACOUSTICS_SLOT_COUNT; ++s) {
            const vg_rect b = l.slot_button[p][s];
            if (mx >= b.x && mx <= b.x + b.w && my >= b.y && my <= b.y + b.h) {
                if (set_value) {
                    if (a->acoustics_page == ACOUSTICS_PAGE_SYNTH) {
                        if (p == 0) {
                            a->acoustics_fire_slot_selected = s;
                            load_slot_to_current(a, 1, s, 1);
                        } else {
                            a->acoustics_thr_slot_selected = s;
                            load_slot_to_current(a, 0, s, 1);
                        }
                    } else {
                        if (p == 0) {
                            a->acoustics_enemy_slot_selected = s;
                            load_combat_slot_to_current(a, 1, s, 1);
                        } else {
                            a->acoustics_exp_slot_selected = s;
                            load_combat_slot_to_current(a, 0, s, 1);
                        }
                    }
                }
                return 1;
            }
        }
        const int row = (int)((my - l.row_y0[p]) / l.row_h);
        const int row_count = l.row_count[p];
        if (row < 0 || row >= row_count) {
            return 1;
        }
        const float sx0 = l.slider_x[p];
        const float sx1 = l.slider_x[p] + l.slider_w[p];
        if (mx >= sx0 && mx <= sx1) {
            const int idx = (p == 0) ? row : (8 + row);
            if (a->acoustics_page == ACOUSTICS_PAGE_COMBAT) {
                const int cidx = (p == 0) ? row : (6 + row);
                if (cidx >= 0 && cidx < ACOUST_COMBAT_SLIDER_COUNT) {
                    a->acoustics_combat_selected = cidx;
                    if (set_value) {
                        const float t = clampf((mx - sx0) / l.slider_w[p], 0.0f, 1.0f);
                        a->acoustics_combat_value_01[cidx] = t;
                        apply_acoustics(a);
                    }
                }
            } else {
                a->acoustics_selected = idx;
                if (set_value) {
                    const float t = clampf((mx - sx0) / l.slider_w[p], 0.0f, 1.0f);
                    a->acoustics_value_01[idx] = t;
                    apply_acoustics(a);
                }
            }
        }
        return 1;
    }
    return 0;
}

static float sample_lerp(const float* src, uint32_t n, float idx) {
    if (!src || n == 0u) {
        return 0.0f;
    }
    if (idx <= 0.0f) {
        return src[0];
    }
    const float max_i = (float)(n - 1u);
    if (idx >= max_i) {
        return src[n - 1u];
    }
    const uint32_t i0 = (uint32_t)idx;
    const uint32_t i1 = i0 + 1u;
    const float t = idx - (float)i0;
    return src[i0] + (src[i1] - src[i0]) * t;
}

static void scope_history_push(app* a, const float* src, uint32_t count) {
    if (!a || !src || count == 0u) {
        return;
    }
    if (count >= ACOUSTICS_SCOPE_HISTORY_SAMPLES) {
        const uint32_t from = count - ACOUSTICS_SCOPE_HISTORY_SAMPLES;
        memcpy(a->scope_history, src + from, sizeof(float) * ACOUSTICS_SCOPE_HISTORY_SAMPLES);
        return;
    }
    const uint32_t keep = ACOUSTICS_SCOPE_HISTORY_SAMPLES - count;
    memmove(a->scope_history, a->scope_history + count, sizeof(float) * keep);
    memcpy(a->scope_history + keep, src, sizeof(float) * count);
}

static uint32_t find_rising_trigger(const float* buf, uint32_t begin, uint32_t end, float threshold) {
    uint32_t trigger = begin;
    for (uint32_t i = begin; i + 1u < end; ++i) {
        const float a = buf[i];
        const float b = buf[i + 1u];
        if (a < threshold && b >= threshold && (b - a) > 0.002f) {
            trigger = i + 1u;
        }
    }
    return trigger;
}

static void rebuild_scope_window(app* a) {
    const uint32_t hist_n = ACOUSTICS_SCOPE_HISTORY_SAMPLES;
    if (!a || hist_n < 128u) {
        return;
    }

    const float* hist = a->scope_history;
    const uint32_t search_span = (hist_n > 6144u) ? 6144u : (hist_n - 2u);
    const uint32_t search_begin = hist_n - search_span;
    const uint32_t trigger = find_rising_trigger(hist, search_begin, hist_n - 1u, 0.02f);

    uint32_t cross[2] = {0u, 0u};
    uint32_t cross_count = 0u;
    for (uint32_t i = trigger + 1u; i + 1u < hist_n; ++i) {
        const float a0 = hist[i];
        const float a1 = hist[i + 1u];
        if (a0 < 0.02f && a1 >= 0.02f && (a1 - a0) > 0.002f) {
            cross[cross_count++] = i + 1u;
            if (cross_count >= 2u) {
                break;
            }
        }
    }

    uint32_t period = 96u;
    if (cross_count >= 2u && cross[1] > cross[0]) {
        period = cross[1] - cross[0];
    } else if (cross_count == 1u && cross[0] > trigger) {
        period = cross[0] - trigger;
    }
    if (period < 24u) {
        period = 24u;
    }
    if (period > 1536u) {
        period = 1536u;
    }

    uint32_t window_len = period * 2u;
    if (window_len < 192u) {
        window_len = 192u;
    }
    if (window_len > (hist_n - 2u)) {
        window_len = hist_n - 2u;
    }

    uint32_t window_start = trigger;
    if (window_start + window_len >= hist_n) {
        window_start = hist_n - window_len - 1u;
    }

    float peak = 0.0f;
    for (uint32_t i = 0; i < window_len; ++i) {
        const float s = hist[window_start + i];
        const float aabs = fabsf(s);
        if (aabs > peak) {
            peak = aabs;
        }
    }
    const float gain = (peak > 0.001f) ? (0.88f / peak) : 1.0f;

    const float max_src = (float)(window_len - 1u);
    for (uint32_t i = 0; i < ACOUSTICS_SCOPE_SAMPLES; ++i) {
        const float t = (float)i / (float)(ACOUSTICS_SCOPE_SAMPLES - 1);
        const float src_i = (float)window_start + t * max_src;
        float v = sample_lerp(hist, hist_n, src_i) * gain;
        if (v > 1.0f) v = 1.0f;
        if (v < -1.0f) v = -1.0f;
        a->scope_window[i] = v;
    }
}

static int audio_spatial_dequeue(app* a, audio_spatial_event* out) {
    if (!a || !out) {
        return 0;
    }
    const uint32_t cap = AUDIO_SPATIAL_EVENT_CAP;
    const uint32_t r = atomic_load_explicit(&a->audio_spatial_read, memory_order_relaxed);
    const uint32_t w = atomic_load_explicit(&a->audio_spatial_write, memory_order_acquire);
    if (r == w) {
        return 0;
    }
    *out = a->audio_spatial_q[r];
    atomic_store_explicit(&a->audio_spatial_read, (r + 1u) % cap, memory_order_release);
    return 1;
}

static void spawn_combat_voice(app* a, const audio_spatial_event* ev) {
    if (!a || !ev) {
        return;
    }
    const int type = (int)ev->type;
    const combat_sound_params* p = NULL;
    int limit = 0;
    if (type == GAME_AUDIO_EVENT_ENEMY_FIRE) {
        p = &a->enemy_fire_sound;
        limit = 14;
    } else if (type == GAME_AUDIO_EVENT_EXPLOSION) {
        p = &a->explosion_sound;
        limit = 10;
    } else {
        return;
    }

    int active_same = 0;
    int free_i = -1;
    int steal_i = 0;
    float oldest = -1.0f;
    for (int i = 0; i < AUDIO_COMBAT_VOICE_COUNT; ++i) {
        audio_combat_voice* v = &a->combat_voices[i];
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
        for (int i = 0; i < AUDIO_COMBAT_VOICE_COUNT; ++i) {
            if (a->combat_voices[i].time_s > oldest) {
                oldest = a->combat_voices[i].time_s;
                free_i = i;
            }
        }
    }
    if (free_i < 0) {
        return;
    }

    audio_combat_voice* v = &a->combat_voices[free_i];
    const float jitter = (rand01_from_state(&a->audio_rng) - 0.5f) * ((type == GAME_AUDIO_EVENT_EXPLOSION) ? 0.18f : 0.08f);
    v->active = 1;
    v->type = (uint8_t)type;
    v->pan = clampf(ev->pan * p->pan_width, -1.0f, 1.0f);
    v->gain = clampf(p->level * ev->gain, 0.0f, 1.2f);
    v->phase = rand01_from_state(&a->audio_rng) * 6.28318530718f;
    v->freq_hz = p->pitch_hz * (1.0f + jitter);
    v->attack_s = fmaxf(0.0001f, p->attack_ms * 0.001f);
    v->decay_s = fmaxf(0.005f, p->decay_ms * 0.001f);
    v->noise_mix = clampf(p->noise_mix, 0.0f, 1.0f);
    v->fm_depth_hz = (type == GAME_AUDIO_EVENT_EXPLOSION) ? fmaxf(0.0f, p->fm_depth_hz) : 0.0f;
    v->fm_rate_hz = (type == GAME_AUDIO_EVENT_EXPLOSION) ? fmaxf(0.0f, p->fm_rate_hz) : 0.0f;
    v->fm_phase = rand01_from_state(&a->audio_rng) * 6.28318530718f;
    v->time_s = 0.0f;
}

static void render_combat_voices(app* a, float* left, float* right, uint32_t n) {
    if (!a || !left || !right || n == 0u) {
        return;
    }
    const float sr = (float)a->audio_have.freq;
    for (int vi = 0; vi < AUDIO_COMBAT_VOICE_COUNT; ++vi) {
        audio_combat_voice* v = &a->combat_voices[vi];
        if (!v->active) {
            continue;
        }
        for (uint32_t i = 0; i < n; ++i) {
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
            const float step = 2.0f * 3.14159265358979323846f * freq / sr;
            const float tone = sinf(v->phase);
            const float noise = rand01_from_state(&a->audio_rng) * 2.0f - 1.0f;
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
            v->fm_phase += 2.0f * 3.14159265358979323846f * v->fm_rate_hz / sr;
            if (v->fm_phase > 6.28318530718f) {
                v->fm_phase -= 6.28318530718f;
            }
            v->time_s += 1.0f / sr;
        }
    }
}

static void audio_callback(void* userdata, Uint8* stream, int len) {
    app* a = (app*)userdata;
    if (!a || !stream || len <= 0) {
        return;
    }
    float* out = (float*)stream;
    const uint32_t channels = (a->audio_have.channels > 0) ? (uint32_t)a->audio_have.channels : 1u;
    const uint32_t frames = (uint32_t)(len / (int)(sizeof(float) * channels));
    if (frames == 0) {
        return;
    }

    uint32_t fire_events = atomic_exchange_explicit(&a->pending_fire_events, 0u, memory_order_acq_rel);
    uint32_t thruster_tests = atomic_exchange_explicit(&a->pending_thruster_tests, 0u, memory_order_acq_rel);
    uint32_t enemy_fire_tests = atomic_exchange_explicit(&a->pending_enemy_fire_tests, 0u, memory_order_acq_rel);
    uint32_t explosion_tests = atomic_exchange_explicit(&a->pending_explosion_tests, 0u, memory_order_acq_rel);
    const int weapon_level = atomic_load_explicit(&a->audio_weapon_level, memory_order_acquire);
    const int thrust_gate = atomic_load_explicit(&a->thrust_gate, memory_order_acquire);
    if (thruster_tests > 0) {
        a->thruster_test_frames_left = a->audio_have.freq / 3u;
    }
    const int thruster_effective_gate = thrust_gate || (a->thruster_test_frames_left > 0u);

    if (thruster_effective_gate && !a->thruster_note_on) {
        const float thr_hz = acoustics_value_to_display(ACOUST_THR_PITCH, a->acoustics_value_01[ACOUST_THR_PITCH]);
        wtp_note_on_hz(&a->thruster_synth, a->thruster_note_id, thr_hz);
        a->thruster_note_on = 1;
    } else if (!thruster_effective_gate && a->thruster_note_on) {
        wtp_note_off(&a->thruster_synth, a->thruster_note_id);
        a->thruster_note_on = 0;
    }

    for (uint32_t i = 0; i < fire_events; ++i) {
        const float base_hz = acoustics_value_to_display(ACOUST_FIRE_PITCH, a->acoustics_value_01[ACOUST_FIRE_PITCH]);
        const float cutoff = acoustics_value_to_display(ACOUST_FIRE_CUTOFF, a->acoustics_value_01[ACOUST_FIRE_CUTOFF]) + (float)(weapon_level - 1) * 360.0f;
        const float resonance = clampf(
            acoustics_value_to_display(ACOUST_FIRE_RESONANCE, a->acoustics_value_01[ACOUST_FIRE_RESONANCE]) + 0.05f * (float)(weapon_level - 1),
            0.0f,
            0.98f
        );
        wtp_set_filter(&a->weapon_synth, cutoff, resonance);

        float intervals[3] = {1.0f, 1.5f, 2.0f};
        int voices = 1;
        if (weapon_level >= 2) {
            voices = 2;
        }
        if (weapon_level >= 3) {
            voices = 3;
        }
        for (int v = 0; v < voices; ++v) {
            const float jitter = (rand01_from_state(&a->audio_rng) - 0.5f) * 0.012f;
            const float hz = base_hz * intervals[v] * (1.0f + jitter);
            wtp_note_on_hz(&a->weapon_synth, (int32_t)a->fire_note_id++, hz);
        }
    }

    audio_spatial_event ev;
    while (audio_spatial_dequeue(a, &ev)) {
        spawn_combat_voice(a, &ev);
    }
    for (uint32_t i = 0; i < enemy_fire_tests; ++i) {
        ev.type = (uint8_t)GAME_AUDIO_EVENT_ENEMY_FIRE;
        ev.pan = 0.0f;
        ev.gain = 1.0f;
        spawn_combat_voice(a, &ev);
    }
    for (uint32_t i = 0; i < explosion_tests; ++i) {
        ev.type = (uint8_t)GAME_AUDIO_EVENT_EXPLOSION;
        ev.pan = 0.0f;
        ev.gain = 1.0f;
        spawn_combat_voice(a, &ev);
    }

    uint32_t remaining = frames;
    uint32_t off = 0;
    while (remaining > 0) {
        uint32_t n = remaining;
        if (n > a->audio_mix_tmp_cap) {
            n = a->audio_mix_tmp_cap;
        }
        wtp_render_instrument(&a->weapon_synth, a->audio_mix_tmp_a, n);
        wtp_render_instrument(&a->thruster_synth, a->audio_mix_tmp_b, n);
        for (uint32_t i = 0; i < n; ++i) {
            a->audio_mix_tmp_a[i] += a->audio_mix_tmp_b[i];
        }
        memset(a->audio_mix_tmp_b, 0, sizeof(float) * n);
        uint32_t got = wtp_ringbuffer_read(&a->beep_rb, a->audio_mix_tmp_b, n);
        if (got < n) {
            memset(a->audio_mix_tmp_b + got, 0, sizeof(float) * (n - got));
        }
        memset(a->audio_mix_tmp_c, 0, sizeof(float) * n);
        memset(a->audio_mix_tmp_d, 0, sizeof(float) * n);
        render_combat_voices(a, a->audio_mix_tmp_c, a->audio_mix_tmp_d, n);
        for (uint32_t i = 0; i < n; ++i) {
            const float mono = a->audio_mix_tmp_a[i] + a->audio_mix_tmp_b[i];
            float l = mono + a->audio_mix_tmp_c[i];
            float r = mono + a->audio_mix_tmp_d[i];
            if (l > 1.0f) l = 1.0f;
            if (l < -1.0f) l = -1.0f;
            if (r > 1.0f) r = 1.0f;
            if (r < -1.0f) r = -1.0f;
            if (channels >= 2u) {
                out[(off + i) * channels + 0u] = l;
                out[(off + i) * channels + 1u] = r;
            } else {
                out[off + i] = 0.5f * (l + r);
            }
            a->audio_mix_tmp_a[i] = 0.5f * (l + r);
        }
        (void)wtp_ringbuffer_write(&a->scope_rb, a->audio_mix_tmp_a, n);
        off += n;
        remaining -= n;
        if (a->thruster_test_frames_left > 0u) {
            if (a->thruster_test_frames_left > n) {
                a->thruster_test_frames_left -= n;
            } else {
                a->thruster_test_frames_left = 0u;
            }
        }
    }
}

static void queue_teletype_beep(app* a, float freq_hz, float dur_s, float amp) {
    if (!a->audio_ready) {
        return;
    }
    const int sample_rate = (a->audio_have.freq > 0) ? a->audio_have.freq : 48000;
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
    (void)wtp_ringbuffer_write(&a->beep_rb, samples, (uint32_t)n);
}

static void teletype_beep_cb(void* user, char ch, float freq_hz, float dur_s, float amp) {
    (void)ch;
    app* a = (app*)user;
    if (!a) {
        return;
    }
    queue_teletype_beep(a, freq_hz, dur_s, amp);
}

static void init_teletype_audio(app* a) {
    SDL_AudioSpec want;
    SDL_zero(want);
    want.freq = 48000;
    want.format = AUDIO_F32SYS;
    want.channels = 2;
    want.samples = 512;
    want.callback = audio_callback;
    want.userdata = a;
    a->audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, &a->audio_have, 0);
    if (a->audio_dev != 0) {
        wtp_config_t cfg;
        wtp_default_config(&cfg);
        cfg.sample_rate = (uint32_t)a->audio_have.freq;
        cfg.frame_size = (uint32_t)a->audio_have.samples;
        cfg.num_voices = 14u;
        cfg.wavetable_size = 8192u;
        cfg.waveform = WTP_WT_SQUARE;
        cfg.attack_ms = 2.0f;
        cfg.decay_ms = 55.0f;
        cfg.sustain_level = 0.0f;
        cfg.release_ms = 90.0f;
        cfg.gain = 0.40f;
        cfg.clip_level = 0.92f;
        cfg.filter_cutoff_hz = 2200.0f;
        cfg.filter_resonance = 0.32f;
        cfg.filter_lowpass_mode = 0;
        if (!wtp_instrument_init_ex(&a->weapon_synth, &cfg)) {
            SDL_CloseAudioDevice(a->audio_dev);
            a->audio_dev = 0;
            a->audio_ready = 0;
            return;
        }
        cfg.waveform = WTP_WT_NOISE;
        cfg.attack_ms = 30.0f;
        cfg.decay_ms = 30.0f;
        cfg.sustain_level = 0.92f;
        cfg.release_ms = 190.0f;
        cfg.gain = 0.22f;
        cfg.clip_level = 0.85f;
        cfg.filter_cutoff_hz = 820.0f;
        cfg.filter_resonance = 0.18f;
        cfg.filter_lowpass_mode = 1;
        if (!wtp_instrument_init_ex(&a->thruster_synth, &cfg)) {
            wtp_instrument_free(&a->weapon_synth);
            SDL_CloseAudioDevice(a->audio_dev);
            a->audio_dev = 0;
            a->audio_ready = 0;
            return;
        }
        a->audio_mix_tmp_cap = cfg.frame_size;
        a->audio_mix_tmp_a = (float*)calloc(a->audio_mix_tmp_cap, sizeof(float));
        a->audio_mix_tmp_b = (float*)calloc(a->audio_mix_tmp_cap, sizeof(float));
        a->audio_mix_tmp_c = (float*)calloc(a->audio_mix_tmp_cap, sizeof(float));
        a->audio_mix_tmp_d = (float*)calloc(a->audio_mix_tmp_cap, sizeof(float));
        if (!a->audio_mix_tmp_a || !a->audio_mix_tmp_b || !a->audio_mix_tmp_c || !a->audio_mix_tmp_d) {
            free(a->audio_mix_tmp_a);
            free(a->audio_mix_tmp_b);
            free(a->audio_mix_tmp_c);
            free(a->audio_mix_tmp_d);
            a->audio_mix_tmp_a = NULL;
            a->audio_mix_tmp_b = NULL;
            a->audio_mix_tmp_c = NULL;
            a->audio_mix_tmp_d = NULL;
            wtp_instrument_free(&a->weapon_synth);
            wtp_instrument_free(&a->thruster_synth);
            SDL_CloseAudioDevice(a->audio_dev);
            a->audio_dev = 0;
            a->audio_ready = 0;
            return;
        }
        if (!wtp_ringbuffer_init(&a->beep_rb, 1u << 16)) {
            free(a->audio_mix_tmp_a);
            free(a->audio_mix_tmp_b);
            free(a->audio_mix_tmp_c);
            free(a->audio_mix_tmp_d);
            a->audio_mix_tmp_a = NULL;
            a->audio_mix_tmp_b = NULL;
            a->audio_mix_tmp_c = NULL;
            a->audio_mix_tmp_d = NULL;
            wtp_instrument_free(&a->weapon_synth);
            wtp_instrument_free(&a->thruster_synth);
            SDL_CloseAudioDevice(a->audio_dev);
            a->audio_dev = 0;
            a->audio_ready = 0;
            return;
        }
        if (!wtp_ringbuffer_init(&a->scope_rb, 1u << 15)) {
            wtp_ringbuffer_free(&a->beep_rb);
            free(a->audio_mix_tmp_a);
            free(a->audio_mix_tmp_b);
            free(a->audio_mix_tmp_c);
            free(a->audio_mix_tmp_d);
            a->audio_mix_tmp_a = NULL;
            a->audio_mix_tmp_b = NULL;
            a->audio_mix_tmp_c = NULL;
            a->audio_mix_tmp_d = NULL;
            wtp_instrument_free(&a->weapon_synth);
            wtp_instrument_free(&a->thruster_synth);
            SDL_CloseAudioDevice(a->audio_dev);
            a->audio_dev = 0;
            a->audio_ready = 0;
            return;
        }
        a->fire_note_id = 1u;
        a->thruster_note_id = 5000001;
        a->thruster_note_on = 0;
        a->thruster_test_frames_left = 0u;
        a->audio_rng = 0xC0DEF00Du;
        atomic_store_explicit(&a->pending_fire_events, 0u, memory_order_release);
        atomic_store_explicit(&a->pending_thruster_tests, 0u, memory_order_release);
        atomic_store_explicit(&a->pending_enemy_fire_tests, 0u, memory_order_release);
        atomic_store_explicit(&a->pending_explosion_tests, 0u, memory_order_release);
        atomic_store_explicit(&a->thrust_gate, 0, memory_order_release);
        atomic_store_explicit(&a->audio_weapon_level, 1, memory_order_release);
        atomic_store_explicit(&a->audio_spatial_read, 0u, memory_order_release);
        atomic_store_explicit(&a->audio_spatial_write, 0u, memory_order_release);
        memset(a->combat_voices, 0, sizeof(a->combat_voices));
        SDL_PauseAudioDevice(a->audio_dev, 0);
        a->audio_ready = 1;
    } else {
        a->audio_ready = 0;
    }
}

static void adjust_crt_profile(app* a, int selected, int dir) {
    if (!a || !a->vg) {
        return;
    }
    vg_crt_profile crt;
    vg_get_crt_profile(a->vg, &crt);
    switch (selected) {
        case 0: crt.bloom_strength = clampf(crt.bloom_strength + 0.05f * (float)dir, CRT_RANGE_BLOOM_STRENGTH_MIN, CRT_RANGE_BLOOM_STRENGTH_MAX); break;
        case 1: crt.bloom_radius_px = clampf(crt.bloom_radius_px + 0.35f * (float)dir, CRT_RANGE_BLOOM_RADIUS_MIN, CRT_RANGE_BLOOM_RADIUS_MAX); break;
        case 2: crt.persistence_decay = clampf(crt.persistence_decay + 0.005f * (float)dir, CRT_RANGE_PERSISTENCE_MIN, CRT_RANGE_PERSISTENCE_MAX); break;
        case 3: crt.jitter_amount = clampf(crt.jitter_amount + 0.02f * (float)dir, CRT_RANGE_JITTER_MIN, CRT_RANGE_JITTER_MAX); break;
        case 4: crt.flicker_amount = clampf(crt.flicker_amount + 0.02f * (float)dir, CRT_RANGE_FLICKER_MIN, CRT_RANGE_FLICKER_MAX); break;
        case 5: crt.beam_core_width_px = clampf(crt.beam_core_width_px + 0.05f * (float)dir, CRT_RANGE_BEAM_CORE_MIN, CRT_RANGE_BEAM_CORE_MAX); break;
        case 6: crt.beam_halo_width_px = clampf(crt.beam_halo_width_px + 0.12f * (float)dir, CRT_RANGE_BEAM_HALO_MIN, CRT_RANGE_BEAM_HALO_MAX); break;
        case 7: crt.beam_intensity = clampf(crt.beam_intensity + 0.05f * (float)dir, CRT_RANGE_BEAM_INTENSITY_MIN, CRT_RANGE_BEAM_INTENSITY_MAX); break;
        case 8: crt.vignette_strength = clampf(crt.vignette_strength + 0.02f * (float)dir, CRT_RANGE_VIGNETTE_MIN, CRT_RANGE_VIGNETTE_MAX); break;
        case 9: crt.barrel_distortion = clampf(crt.barrel_distortion + 0.01f * (float)dir, CRT_RANGE_BARREL_MIN, CRT_RANGE_BARREL_MAX); break;
        case 10: crt.scanline_strength = clampf(crt.scanline_strength + 0.02f * (float)dir, CRT_RANGE_SCANLINE_MIN, CRT_RANGE_SCANLINE_MAX); break;
        case 11: crt.noise_strength = clampf(crt.noise_strength + 0.01f * (float)dir, CRT_RANGE_NOISE_MIN, CRT_RANGE_NOISE_MAX); break;
        default: break;
    }
    vg_set_crt_profile(a->vg, &crt);
    sync_video_dials_from_live_crt(a);
    (void)save_settings(a);
}

static void set_crt_profile_value01(app* a, int selected, float value_01) {
    if (!a || !a->vg) {
        return;
    }
    vg_crt_profile crt;
    vg_get_crt_profile(a->vg, &crt);
    float t = clampf(value_01, 0.0f, 1.0f);
    switch (selected) {
        case 0: crt.bloom_strength = lerpf(CRT_RANGE_BLOOM_STRENGTH_MIN, CRT_RANGE_BLOOM_STRENGTH_MAX, t); break;
        case 1: crt.bloom_radius_px = lerpf(CRT_RANGE_BLOOM_RADIUS_MIN, CRT_RANGE_BLOOM_RADIUS_MAX, t); break;
        case 2: crt.persistence_decay = lerpf(CRT_RANGE_PERSISTENCE_MIN, CRT_RANGE_PERSISTENCE_MAX, t); break;
        case 3: crt.jitter_amount = lerpf(CRT_RANGE_JITTER_MIN, CRT_RANGE_JITTER_MAX, t); break;
        case 4: crt.flicker_amount = lerpf(CRT_RANGE_FLICKER_MIN, CRT_RANGE_FLICKER_MAX, t); break;
        case 5: crt.beam_core_width_px = lerpf(CRT_RANGE_BEAM_CORE_MIN, CRT_RANGE_BEAM_CORE_MAX, t); break;
        case 6: crt.beam_halo_width_px = lerpf(CRT_RANGE_BEAM_HALO_MIN, CRT_RANGE_BEAM_HALO_MAX, t); break;
        case 7: crt.beam_intensity = lerpf(CRT_RANGE_BEAM_INTENSITY_MIN, CRT_RANGE_BEAM_INTENSITY_MAX, t); break;
        case 8: crt.vignette_strength = lerpf(CRT_RANGE_VIGNETTE_MIN, CRT_RANGE_VIGNETTE_MAX, t); break;
        case 9: crt.barrel_distortion = lerpf(CRT_RANGE_BARREL_MIN, CRT_RANGE_BARREL_MAX, t); break;
        case 10: crt.scanline_strength = lerpf(CRT_RANGE_SCANLINE_MIN, CRT_RANGE_SCANLINE_MAX, t); break;
        case 11: crt.noise_strength = lerpf(CRT_RANGE_NOISE_MIN, CRT_RANGE_NOISE_MAX, t); break;
        default: break;
    }
    vg_set_crt_profile(a->vg, &crt);
    sync_video_dials_from_live_crt(a);
}

static int handle_crt_ui_mouse(app* a, int mouse_x, int mouse_y, int set_value) {
    const float w = (float)a->swapchain_extent.width;
    const float h = (float)a->swapchain_extent.height;
    const float ui = ui_reference_scale(w, h);
    const vg_rect safe = make_ui_safe_frame(w, h);
    const float px = safe.x + safe.w * 0.00f;
    const float py = safe.y + safe.h * 0.08f;
    const float pw = safe.w * 0.44f;
    const float ph = safe.h * 0.82f;
    float mx = 0.0f;
    float my = 0.0f;
    map_mouse_to_scene_coords(a, mouse_x, mouse_y, &mx, &my);
    if (mx < px || mx > px + pw || my < py || my > py + ph) {
        return 0;
    }

    const float row_h = 34.0f * ui;
    vg_ui_slider_panel_metrics sm = make_scaled_slider_metrics(ui, 70.0f * ui);
    vg_ui_slider_item dummy[12] = {0};
    vg_ui_slider_panel_desc desc = {
        .rect = {px, py, pw, ph},
        .items = dummy,
        .item_count = 12u,
        .row_height_px = row_h,
        .label_size_px = 11.0f * ui,
        .value_size_px = 11.5f * ui,
        .value_text_x_offset_px = 0.0f,
        .metrics = &sm
    };
    vg_ui_slider_panel_layout panel_layout;
    vg_ui_slider_panel_row_layout row_layout;
    if (vg_ui_slider_panel_compute_layout(&desc, &panel_layout) != VG_OK ||
        vg_ui_slider_panel_compute_row_layout(&desc, &panel_layout, 0u, &row_layout) != VG_OK) {
        return 0;
    }
    const float row_y0 = panel_layout.row_start_y;
    const float slider_x = row_layout.slider_rect.x;
    const float slider_w = row_layout.slider_rect.w;

    int row = (int)((my - row_y0) / row_h);
    if (row < 0 || row >= 12) {
        return 1;
    }
    const float sx0 = slider_x;
    const float sx1 = slider_x + slider_w;
    if (mx >= sx0 && mx <= sx1) {
        a->crt_ui_selected = row;
        if (set_value) {
            float t = (mx - slider_x) / slider_w;
            set_crt_profile_value01(a, row, t);
        }
    }
    return 1;
}

static int handle_video_menu_mouse(app* a, int mouse_x, int mouse_y, int set_value) {
    if (!a) {
        return 0;
    }
    const float w = (float)a->swapchain_extent.width;
    const float h = (float)a->swapchain_extent.height;
    float mx = 0.0f;
    float my = 0.0f;
    map_mouse_to_scene_coords(a, mouse_x, mouse_y, &mx, &my);

    const vg_rect panel = make_ui_safe_frame(w, h);
    if (mx < panel.x || mx > panel.x + panel.w || my < panel.y || my > panel.y + panel.h) {
        return 0;
    }

    {
        const float btn_h = panel.h * 0.055f;
        const float btn_w = panel.w * 0.09f;
        const float btn_gap = panel.w * 0.012f;
        const float btn_y = panel.y + panel.h - panel.h * 0.13f;
        const float btn_x0 = panel.x + panel.w - (3.0f * btn_w + 2.0f * btn_gap) - panel.w * 0.04f;
        for (int i = 0; i < 3; ++i) {
            const vg_rect b = {btn_x0 + (float)i * (btn_w + btn_gap), btn_y, btn_w, btn_h};
            if (mx >= b.x && mx <= b.x + b.w && my >= b.y && my <= b.y + b.h) {
                if (set_value) {
                    a->palette_mode = i;
                    a->force_clear_frames = 2;
                    (void)save_settings(a);
                }
                return 1;
            }
        }
    }

    {
        vg_vec2 centers[VIDEO_MENU_DIAL_COUNT];
        float r = 0.0f;
        video_menu_dial_geometry(a, centers, &r);
        for (int d = 0; d < VIDEO_MENU_DIAL_COUNT; ++d) {
            const float dx = mx - centers[d].x;
            const float dy = my - centers[d].y;
            const float dist = sqrtf(dx * dx + dy * dy);
            if (dist <= r * 1.15f) {
                if (set_value) {
                    a->video_menu_dial_drag = d;
                    a->video_menu_dial_drag_start_y = my;
                    a->video_menu_dial_drag_start_value = a->video_dial_01[d];
                }
                return 1;
            }
        }
    }

    const int item_count = VIDEO_MENU_RES_COUNT + 1;
    const float row_h = panel.h * 0.082f;
    const float row_w = panel.w * 0.36f;
    const float row_x = panel.x + panel.w * 0.05f;
    const float row_y0 = panel.y + panel.h * 0.68f;
    for (int i = 0; i < item_count; ++i) {
        const vg_rect row = {row_x, row_y0 - row_h * (float)i, row_w, row_h * 0.72f};
        if (mx >= row.x && mx <= row.x + row.w && my >= row.y && my <= row.y + row.h) {
            if (set_value) {
                a->video_menu_dial_drag = -1;
                a->video_menu_selected = i;
                if (apply_video_mode(a)) {
                    set_tty_message(a, "display mode applied");
                } else {
                    set_tty_message(a, "display mode apply failed");
                }
            }
            return 1;
        }
    }
    return 1;
}

static void planetarium_node_center(const app* a, int idx, float* out_x, float* out_y) {
    static const int k_primes[PLANETARIUM_MAX_SYSTEMS] = {2, 3, 5, 7, 11, 13, 17, 19};
    const float w = (float)a->swapchain_extent.width;
    const float h = (float)a->swapchain_extent.height;
    const vg_rect panel = make_ui_safe_frame(w, h);
    const vg_rect map = {panel.x + panel.w * 0.03f, panel.y + panel.h * 0.08f, panel.w * 0.56f, panel.h * 0.85f};
    const float cx = map.x + map.w * 0.50f;
    const float cy = map.y + map.h * 0.52f;
    const float t_s = (float)SDL_GetTicks() * 0.001f;
    if (idx < 0) {
        idx = 0;
    }
    const int planet_count = app_planetarium_planet_count(a);
    if (idx >= planet_count) {
        *out_x = cx + map.w * 0.38f;
        *out_y = cy - map.h * 0.08f;
        return;
    }
    const float orbit_t = ((float)idx + 1.0f) / ((float)planet_count + 1.0f);
    const float rx = map.w * (0.12f + orbit_t * 0.30f);
    const float ry = map.h * (0.04f + orbit_t * 0.11f);
    const float rot = 0.22f;
    const int p = k_primes[idx % PLANETARIUM_MAX_SYSTEMS];
    const int q = k_primes[(idx + 3) % PLANETARIUM_MAX_SYSTEMS];
    const float phase = t_s * (0.10f + 0.008f * (float)p) + 6.28318530718f * ((float)(q % 29) / 29.0f);
    const float c = cosf(phase);
    const float s = sinf(phase);
    *out_x = cx + c * rx * cosf(rot) - s * ry * sinf(rot);
    *out_y = cy + c * rx * sinf(rot) + s * ry * cosf(rot);
}

static int handle_planetarium_mouse(app* a, int mouse_x, int mouse_y, int set_value) {
    if (!a) {
        return 0;
    }
    const float w = (float)a->swapchain_extent.width;
    const float h = (float)a->swapchain_extent.height;
    float mx = 0.0f;
    float my = 0.0f;
    map_mouse_to_scene_coords(a, mouse_x, mouse_y, &mx, &my);
    const vg_rect panel = make_ui_safe_frame(w, h);
    if (mx < panel.x || mx > panel.x + panel.w || my < panel.y || my > panel.y + panel.h) {
        return 0;
    }
    const int boss_idx = app_planetarium_planet_count(a);
    const float r = fminf(w, h) * 0.024f;
    for (int i = 0; i <= boss_idx; ++i) {
        float cx = 0.0f;
        float cy = 0.0f;
        planetarium_node_center(a, i, &cx, &cy);
        const float dx = mx - cx;
        const float dy = my - cy;
        if (dx * dx + dy * dy <= r * r * 1.8f) {
            if (set_value) {
                if (a->planetarium_selected != i) {
                    a->planetarium_selected = i;
                    announce_planetarium_selection(a);
                }
            }
            return 1;
        }
    }
    return 1;
}

static void init_planetarium_assets(app* a) {
    if (!a) {
        return;
    }

    vg_svg_load_params sp = {
        .curve_tolerance_px = 0.75f,
        .dpi = 96.0f,
        .units = "px"
    };
    const char* svg_candidates[] = {
        "assets/images/surveillance.svg",
        "../assets/images/surveillance.svg",
        "../../assets/images/surveillance.svg"
    };
    for (size_t i = 0; i < sizeof(svg_candidates) / sizeof(svg_candidates[0]); ++i) {
        vg_svg_asset* asset = NULL;
        if (vg_svg_load_from_file(svg_candidates[i], &sp, &asset) == VG_OK && asset) {
            a->surveillance_svg_asset = asset;
            break;
        }
    }

#if V_TYPE_HAS_SDL_IMAGE
    const int img_ok = IMG_Init(IMG_INIT_JPG);
    if (img_ok & IMG_INIT_JPG) {
        const char* img_candidates[] = {
            "assets/images/nick.jpg",
            "../assets/images/nick.jpg",
            "../../assets/images/nick.jpg"
        };
        SDL_Surface* src = NULL;
        for (size_t i = 0; i < sizeof(img_candidates) / sizeof(img_candidates[0]); ++i) {
            src = IMG_Load(img_candidates[i]);
            if (src) {
                break;
            }
        }
        if (src) {
            SDL_Surface* rgba = SDL_ConvertSurfaceFormat(src, SDL_PIXELFORMAT_RGBA32, 0);
            SDL_FreeSurface(src);
            src = NULL;
            if (rgba) {
                const size_t bytes = (size_t)rgba->pitch * (size_t)rgba->h;
                a->nick_rgba8 = (uint8_t*)malloc(bytes);
                if (a->nick_rgba8) {
                    memcpy(a->nick_rgba8, rgba->pixels, bytes);
                    a->nick_w = (uint32_t)rgba->w;
                    a->nick_h = (uint32_t)rgba->h;
                    a->nick_stride = (uint32_t)rgba->pitch;
                }
                SDL_FreeSurface(rgba);
            }
        }
    }
#endif
}

static uint32_t find_memory_type(app* a, uint32_t type_bits, VkMemoryPropertyFlags required) {
    VkPhysicalDeviceMemoryProperties props;
    vkGetPhysicalDeviceMemoryProperties(a->physical_device, &props);
    for (uint32_t i = 0; i < props.memoryTypeCount; ++i) {
        if ((type_bits & (1u << i)) && ((props.memoryTypes[i].propertyFlags & required) == required)) {
            return i;
        }
    }
    return UINT32_MAX;
}

static int create_image_2d(
    app* a,
    uint32_t w,
    uint32_t h,
    VkFormat format,
    VkImageUsageFlags usage,
    VkSampleCountFlagBits samples,
    VkImage* out_image,
    VkDeviceMemory* out_mem,
    VkImageView* out_view
) {
    VkImageCreateInfo img = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = format,
        .extent = {w, h, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = samples,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
    };
    if (!check_vk(vkCreateImage(a->device, &img, NULL, out_image), "vkCreateImage")) {
        return 0;
    }
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(a->device, *out_image, &req);
    uint32_t mem_type = find_memory_type(a, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (mem_type == UINT32_MAX) {
        return 0;
    }
    VkMemoryAllocateInfo ai = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = req.size,
        .memoryTypeIndex = mem_type
    };
    if (!check_vk(vkAllocateMemory(a->device, &ai, NULL, out_mem), "vkAllocateMemory(image)")) {
        return 0;
    }
    if (!check_vk(vkBindImageMemory(a->device, *out_image, *out_mem, 0), "vkBindImageMemory")) {
        return 0;
    }
    VkImageViewCreateInfo vi = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = *out_image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = format,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1
        }
    };
    return check_vk(vkCreateImageView(a->device, &vi, NULL, out_view), "vkCreateImageView(offscreen)");
}

static int create_depth_image_2d(
    app* a,
    uint32_t w,
    uint32_t h,
    VkFormat format,
    VkSampleCountFlagBits samples,
    VkImage* out_image,
    VkDeviceMemory* out_mem,
    VkImageView* out_view
) {
    VkImageCreateInfo img = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = format,
        .extent = {w, h, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = samples,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
    };
    if (!check_vk(vkCreateImage(a->device, &img, NULL, out_image), "vkCreateImage(depth)")) {
        return 0;
    }
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(a->device, *out_image, &req);
    uint32_t mem_type = find_memory_type(a, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (mem_type == UINT32_MAX) {
        return 0;
    }
    VkMemoryAllocateInfo ai = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = req.size,
        .memoryTypeIndex = mem_type
    };
    if (!check_vk(vkAllocateMemory(a->device, &ai, NULL, out_mem), "vkAllocateMemory(depth)")) {
        return 0;
    }
    if (!check_vk(vkBindImageMemory(a->device, *out_image, *out_mem, 0), "vkBindImageMemory(depth)")) {
        return 0;
    }
    VkImageViewCreateInfo vi = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = *out_image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = format,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1
        }
    };
    return check_vk(vkCreateImageView(a->device, &vi, NULL, out_view), "vkCreateImageView(depth)");
}

static int create_buffer(
    app* a,
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VkMemoryPropertyFlags memory_flags,
    VkBuffer* out_buffer,
    VkDeviceMemory* out_memory
) {
    VkBufferCreateInfo bi = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };
    if (!check_vk(vkCreateBuffer(a->device, &bi, NULL, out_buffer), "vkCreateBuffer")) {
        return 0;
    }
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(a->device, *out_buffer, &req);
    const uint32_t mem_type = find_memory_type(a, req.memoryTypeBits, memory_flags);
    if (mem_type == UINT32_MAX) {
        return 0;
    }
    VkMemoryAllocateInfo ai = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = req.size,
        .memoryTypeIndex = mem_type
    };
    if (!check_vk(vkAllocateMemory(a->device, &ai, NULL, out_memory), "vkAllocateMemory(buffer)")) {
        return 0;
    }
    if (!check_vk(vkBindBufferMemory(a->device, *out_buffer, *out_memory, 0), "vkBindBufferMemory")) {
        return 0;
    }
    return 1;
}

static VkFormat find_depth_format(app* a) {
    const VkFormat candidates[] = {
        VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_D24_UNORM_S8_UINT,
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D16_UNORM
    };
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i) {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(a->physical_device, candidates[i], &props);
        if (props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
            return candidates[i];
        }
    }
    return VK_FORMAT_UNDEFINED;
}

static int format_has_stencil(VkFormat fmt) {
    return fmt == VK_FORMAT_D32_SFLOAT_S8_UINT || fmt == VK_FORMAT_D24_UNORM_S8_UINT || fmt == VK_FORMAT_D16_UNORM_S8_UINT;
}

static void set_viewport_scissor(VkCommandBuffer cmd, uint32_t w, uint32_t h) {
    VkViewport vp = {.x = 0.0f, .y = 0.0f, .width = (float)w, .height = (float)h, .minDepth = 0.0f, .maxDepth = 1.0f};
    VkRect2D sc = {.offset = {0, 0}, .extent = {w, h}};
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor(cmd, 0, 1, &sc);
}

static void clear_scene_depth(VkCommandBuffer cmd, VkExtent2D extent) {
    VkClearAttachment clear;
    memset(&clear, 0, sizeof(clear));
    clear.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    clear.clearValue.depthStencil.depth = 1.0f;
    clear.clearValue.depthStencil.stencil = 0;
    VkClearRect rect;
    memset(&rect, 0, sizeof(rect));
    rect.rect.extent = extent;
    rect.layerCount = 1;
    vkCmdClearAttachments(cmd, 1, &clear, 1, &rect);
}

static void clear_scene_color_depth(VkCommandBuffer cmd, VkExtent2D extent) {
    VkClearAttachment clears[2];
    memset(clears, 0, sizeof(clears));
    clears[0].aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    clears[0].colorAttachment = 0;
    clears[0].clearValue.color.float32[0] = 0.0f;
    clears[0].clearValue.color.float32[1] = 0.0f;
    clears[0].clearValue.color.float32[2] = 0.0f;
    clears[0].clearValue.color.float32[3] = 1.0f;
    clears[1].aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    clears[1].clearValue.depthStencil.depth = 1.0f;
    clears[1].clearValue.depthStencil.stencil = 0;

    VkClearRect rect;
    memset(&rect, 0, sizeof(rect));
    rect.rect.extent = extent;
    rect.layerCount = 1;
    vkCmdClearAttachments(cmd, 2, clears, 1, &rect);
}

static void cleanup(app* a) {
    if (a->device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(a->device);
    }
    if (a->audio_dev != 0) {
        SDL_PauseAudioDevice(a->audio_dev, 1);
        wtp_ringbuffer_free(&a->beep_rb);
        wtp_ringbuffer_free(&a->scope_rb);
        wtp_instrument_free(&a->weapon_synth);
        wtp_instrument_free(&a->thruster_synth);
        free(a->audio_mix_tmp_a);
        free(a->audio_mix_tmp_b);
        free(a->audio_mix_tmp_c);
        free(a->audio_mix_tmp_d);
        a->audio_mix_tmp_a = NULL;
        a->audio_mix_tmp_b = NULL;
        a->audio_mix_tmp_c = NULL;
        a->audio_mix_tmp_d = NULL;
        SDL_CloseAudioDevice(a->audio_dev);
        a->audio_dev = 0;
    }
    if (a->vg) {
        vg_context_destroy(a->vg);
        a->vg = NULL;
    }

    if (a->bloom_pipeline) vkDestroyPipeline(a->device, a->bloom_pipeline, NULL);
    if (a->composite_pipeline) vkDestroyPipeline(a->device, a->composite_pipeline, NULL);
    if (a->terrain_fill_pipeline) vkDestroyPipeline(a->device, a->terrain_fill_pipeline, NULL);
    if (a->terrain_line_pipeline) vkDestroyPipeline(a->device, a->terrain_line_pipeline, NULL);
    if (a->particle_pipeline) vkDestroyPipeline(a->device, a->particle_pipeline, NULL);
    if (a->particle_bloom_pipeline) vkDestroyPipeline(a->device, a->particle_bloom_pipeline, NULL);
    if (a->wormhole_depth_pipeline) vkDestroyPipeline(a->device, a->wormhole_depth_pipeline, NULL);
    if (a->wormhole_line_pipeline) vkDestroyPipeline(a->device, a->wormhole_line_pipeline, NULL);
    if (a->post_layout) vkDestroyPipelineLayout(a->device, a->post_layout, NULL);
    if (a->terrain_layout) vkDestroyPipelineLayout(a->device, a->terrain_layout, NULL);
    if (a->particle_layout) vkDestroyPipelineLayout(a->device, a->particle_layout, NULL);
    if (a->wormhole_line_layout) vkDestroyPipelineLayout(a->device, a->wormhole_line_layout, NULL);
    if (a->post_desc_pool) vkDestroyDescriptorPool(a->device, a->post_desc_pool, NULL);
    if (a->post_desc_layout) vkDestroyDescriptorSetLayout(a->device, a->post_desc_layout, NULL);
    if (a->post_sampler) vkDestroySampler(a->device, a->post_sampler, NULL);

    if (a->scene_fb) vkDestroyFramebuffer(a->device, a->scene_fb, NULL);
    if (a->bloom_fb) vkDestroyFramebuffer(a->device, a->bloom_fb, NULL);
    if (a->scene_view) vkDestroyImageView(a->device, a->scene_view, NULL);
    if (a->scene_depth_view) vkDestroyImageView(a->device, a->scene_depth_view, NULL);
    if (a->scene_msaa_view) vkDestroyImageView(a->device, a->scene_msaa_view, NULL);
    if (a->bloom_view) vkDestroyImageView(a->device, a->bloom_view, NULL);
    if (a->scene_image) vkDestroyImage(a->device, a->scene_image, NULL);
    if (a->scene_depth_image) vkDestroyImage(a->device, a->scene_depth_image, NULL);
    if (a->scene_msaa_image) vkDestroyImage(a->device, a->scene_msaa_image, NULL);
    if (a->bloom_image) vkDestroyImage(a->device, a->bloom_image, NULL);
    if (a->scene_memory) vkFreeMemory(a->device, a->scene_memory, NULL);
    if (a->scene_depth_memory) vkFreeMemory(a->device, a->scene_depth_memory, NULL);
    if (a->scene_msaa_memory) vkFreeMemory(a->device, a->scene_msaa_memory, NULL);
    if (a->bloom_memory) vkFreeMemory(a->device, a->bloom_memory, NULL);
    if (a->terrain_vertex_map && a->terrain_vertex_memory) {
        vkUnmapMemory(a->device, a->terrain_vertex_memory);
        a->terrain_vertex_map = NULL;
    }
    if (a->terrain_vertex_buffer) vkDestroyBuffer(a->device, a->terrain_vertex_buffer, NULL);
    if (a->terrain_tri_index_buffer) vkDestroyBuffer(a->device, a->terrain_tri_index_buffer, NULL);
    if (a->terrain_wire_vertex_map && a->terrain_wire_vertex_memory) {
        vkUnmapMemory(a->device, a->terrain_wire_vertex_memory);
        a->terrain_wire_vertex_map = NULL;
    }
    if (a->terrain_wire_vertex_buffer) vkDestroyBuffer(a->device, a->terrain_wire_vertex_buffer, NULL);
    if (a->particle_instance_map && a->particle_instance_memory) {
        vkUnmapMemory(a->device, a->particle_instance_memory);
        a->particle_instance_map = NULL;
    }
    if (a->wormhole_line_vertex_map && a->wormhole_line_vertex_memory) {
        vkUnmapMemory(a->device, a->wormhole_line_vertex_memory);
        a->wormhole_line_vertex_map = NULL;
    }
    if (a->wormhole_tri_vertex_map && a->wormhole_tri_vertex_memory) {
        vkUnmapMemory(a->device, a->wormhole_tri_vertex_memory);
        a->wormhole_tri_vertex_map = NULL;
    }
    if (a->particle_instance_buffer) vkDestroyBuffer(a->device, a->particle_instance_buffer, NULL);
    if (a->wormhole_tri_vertex_buffer) vkDestroyBuffer(a->device, a->wormhole_tri_vertex_buffer, NULL);
    if (a->wormhole_line_vertex_buffer) vkDestroyBuffer(a->device, a->wormhole_line_vertex_buffer, NULL);
    if (a->terrain_vertex_memory) vkFreeMemory(a->device, a->terrain_vertex_memory, NULL);
    if (a->terrain_tri_index_memory) vkFreeMemory(a->device, a->terrain_tri_index_memory, NULL);
    if (a->terrain_wire_vertex_memory) vkFreeMemory(a->device, a->terrain_wire_vertex_memory, NULL);
    if (a->particle_instance_memory) vkFreeMemory(a->device, a->particle_instance_memory, NULL);
    if (a->wormhole_tri_vertex_memory) vkFreeMemory(a->device, a->wormhole_tri_vertex_memory, NULL);
    if (a->wormhole_line_vertex_memory) vkFreeMemory(a->device, a->wormhole_line_vertex_memory, NULL);

    for (uint32_t i = 0; i < a->swapchain_image_count; ++i) {
        if (a->present_framebuffers[i]) vkDestroyFramebuffer(a->device, a->present_framebuffers[i], NULL);
        if (a->swapchain_image_views[i]) vkDestroyImageView(a->device, a->swapchain_image_views[i], NULL);
    }

    if (a->scene_render_pass) vkDestroyRenderPass(a->device, a->scene_render_pass, NULL);
    if (a->bloom_render_pass) vkDestroyRenderPass(a->device, a->bloom_render_pass, NULL);
    if (a->present_render_pass) vkDestroyRenderPass(a->device, a->present_render_pass, NULL);
    if (a->swapchain) vkDestroySwapchainKHR(a->device, a->swapchain, NULL);

    if (a->in_flight) vkDestroyFence(a->device, a->in_flight, NULL);
    if (a->render_finished) vkDestroySemaphore(a->device, a->render_finished, NULL);
    if (a->image_available) vkDestroySemaphore(a->device, a->image_available, NULL);
    if (a->command_pool) vkDestroyCommandPool(a->device, a->command_pool, NULL);
    if (a->device) vkDestroyDevice(a->device, NULL);
    if (a->surface) vkDestroySurfaceKHR(a->instance, a->surface, NULL);
    if (a->instance) vkDestroyInstance(a->instance, NULL);
    if (a->window) SDL_DestroyWindow(a->window);
#if V_TYPE_HAS_SDL_IMAGE
    if (a->nick_rgba8) {
        free(a->nick_rgba8);
        a->nick_rgba8 = NULL;
    }
    IMG_Quit();
#endif
    if (a->surveillance_svg_asset) {
        vg_svg_destroy(a->surveillance_svg_asset);
        a->surveillance_svg_asset = NULL;
    }
    SDL_Quit();
}

static void destroy_render_runtime(app* a) {
    if (!a || a->device == VK_NULL_HANDLE) {
        return;
    }
    vkDeviceWaitIdle(a->device);

    if (a->vg) {
        vg_context_destroy(a->vg);
        a->vg = NULL;
    }
    if (a->bloom_pipeline) {
        vkDestroyPipeline(a->device, a->bloom_pipeline, NULL);
        a->bloom_pipeline = VK_NULL_HANDLE;
    }
    if (a->composite_pipeline) {
        vkDestroyPipeline(a->device, a->composite_pipeline, NULL);
        a->composite_pipeline = VK_NULL_HANDLE;
    }
    if (a->terrain_fill_pipeline) {
        vkDestroyPipeline(a->device, a->terrain_fill_pipeline, NULL);
        a->terrain_fill_pipeline = VK_NULL_HANDLE;
    }
    if (a->terrain_line_pipeline) {
        vkDestroyPipeline(a->device, a->terrain_line_pipeline, NULL);
        a->terrain_line_pipeline = VK_NULL_HANDLE;
    }
    if (a->particle_pipeline) {
        vkDestroyPipeline(a->device, a->particle_pipeline, NULL);
        a->particle_pipeline = VK_NULL_HANDLE;
    }
    if (a->particle_bloom_pipeline) {
        vkDestroyPipeline(a->device, a->particle_bloom_pipeline, NULL);
        a->particle_bloom_pipeline = VK_NULL_HANDLE;
    }
    if (a->wormhole_depth_pipeline) {
        vkDestroyPipeline(a->device, a->wormhole_depth_pipeline, NULL);
        a->wormhole_depth_pipeline = VK_NULL_HANDLE;
    }
    if (a->wormhole_line_pipeline) {
        vkDestroyPipeline(a->device, a->wormhole_line_pipeline, NULL);
        a->wormhole_line_pipeline = VK_NULL_HANDLE;
    }
    if (a->post_layout) {
        vkDestroyPipelineLayout(a->device, a->post_layout, NULL);
        a->post_layout = VK_NULL_HANDLE;
    }
    if (a->terrain_layout) {
        vkDestroyPipelineLayout(a->device, a->terrain_layout, NULL);
        a->terrain_layout = VK_NULL_HANDLE;
    }
    if (a->particle_layout) {
        vkDestroyPipelineLayout(a->device, a->particle_layout, NULL);
        a->particle_layout = VK_NULL_HANDLE;
    }
    if (a->wormhole_line_layout) {
        vkDestroyPipelineLayout(a->device, a->wormhole_line_layout, NULL);
        a->wormhole_line_layout = VK_NULL_HANDLE;
    }
    if (a->post_desc_pool) {
        vkDestroyDescriptorPool(a->device, a->post_desc_pool, NULL);
        a->post_desc_pool = VK_NULL_HANDLE;
    }
    if (a->post_desc_layout) {
        vkDestroyDescriptorSetLayout(a->device, a->post_desc_layout, NULL);
        a->post_desc_layout = VK_NULL_HANDLE;
    }
    if (a->post_sampler) {
        vkDestroySampler(a->device, a->post_sampler, NULL);
        a->post_sampler = VK_NULL_HANDLE;
    }
    if (a->scene_fb) {
        vkDestroyFramebuffer(a->device, a->scene_fb, NULL);
        a->scene_fb = VK_NULL_HANDLE;
    }
    if (a->bloom_fb) {
        vkDestroyFramebuffer(a->device, a->bloom_fb, NULL);
        a->bloom_fb = VK_NULL_HANDLE;
    }
    if (a->scene_view) {
        vkDestroyImageView(a->device, a->scene_view, NULL);
        a->scene_view = VK_NULL_HANDLE;
    }
    if (a->scene_depth_view) {
        vkDestroyImageView(a->device, a->scene_depth_view, NULL);
        a->scene_depth_view = VK_NULL_HANDLE;
    }
    if (a->scene_msaa_view) {
        vkDestroyImageView(a->device, a->scene_msaa_view, NULL);
        a->scene_msaa_view = VK_NULL_HANDLE;
    }
    if (a->bloom_view) {
        vkDestroyImageView(a->device, a->bloom_view, NULL);
        a->bloom_view = VK_NULL_HANDLE;
    }
    if (a->scene_image) {
        vkDestroyImage(a->device, a->scene_image, NULL);
        a->scene_image = VK_NULL_HANDLE;
    }
    if (a->scene_depth_image) {
        vkDestroyImage(a->device, a->scene_depth_image, NULL);
        a->scene_depth_image = VK_NULL_HANDLE;
    }
    if (a->scene_msaa_image) {
        vkDestroyImage(a->device, a->scene_msaa_image, NULL);
        a->scene_msaa_image = VK_NULL_HANDLE;
    }
    if (a->bloom_image) {
        vkDestroyImage(a->device, a->bloom_image, NULL);
        a->bloom_image = VK_NULL_HANDLE;
    }
    if (a->scene_memory) {
        vkFreeMemory(a->device, a->scene_memory, NULL);
        a->scene_memory = VK_NULL_HANDLE;
    }
    if (a->scene_depth_memory) {
        vkFreeMemory(a->device, a->scene_depth_memory, NULL);
        a->scene_depth_memory = VK_NULL_HANDLE;
    }
    if (a->scene_msaa_memory) {
        vkFreeMemory(a->device, a->scene_msaa_memory, NULL);
        a->scene_msaa_memory = VK_NULL_HANDLE;
    }
    if (a->bloom_memory) {
        vkFreeMemory(a->device, a->bloom_memory, NULL);
        a->bloom_memory = VK_NULL_HANDLE;
    }
    if (a->terrain_vertex_map && a->terrain_vertex_memory) {
        vkUnmapMemory(a->device, a->terrain_vertex_memory);
        a->terrain_vertex_map = NULL;
    }
    if (a->terrain_vertex_buffer) {
        vkDestroyBuffer(a->device, a->terrain_vertex_buffer, NULL);
        a->terrain_vertex_buffer = VK_NULL_HANDLE;
    }
    if (a->terrain_tri_index_buffer) {
        vkDestroyBuffer(a->device, a->terrain_tri_index_buffer, NULL);
        a->terrain_tri_index_buffer = VK_NULL_HANDLE;
    }
    if (a->terrain_wire_vertex_map && a->terrain_wire_vertex_memory) {
        vkUnmapMemory(a->device, a->terrain_wire_vertex_memory);
        a->terrain_wire_vertex_map = NULL;
    }
    if (a->terrain_wire_vertex_buffer) {
        vkDestroyBuffer(a->device, a->terrain_wire_vertex_buffer, NULL);
        a->terrain_wire_vertex_buffer = VK_NULL_HANDLE;
    }
    if (a->particle_instance_map && a->particle_instance_memory) {
        vkUnmapMemory(a->device, a->particle_instance_memory);
        a->particle_instance_map = NULL;
    }
    if (a->wormhole_line_vertex_map && a->wormhole_line_vertex_memory) {
        vkUnmapMemory(a->device, a->wormhole_line_vertex_memory);
        a->wormhole_line_vertex_map = NULL;
    }
    if (a->wormhole_tri_vertex_map && a->wormhole_tri_vertex_memory) {
        vkUnmapMemory(a->device, a->wormhole_tri_vertex_memory);
        a->wormhole_tri_vertex_map = NULL;
    }
    if (a->particle_instance_buffer) {
        vkDestroyBuffer(a->device, a->particle_instance_buffer, NULL);
        a->particle_instance_buffer = VK_NULL_HANDLE;
    }
    if (a->wormhole_line_vertex_buffer) {
        vkDestroyBuffer(a->device, a->wormhole_line_vertex_buffer, NULL);
        a->wormhole_line_vertex_buffer = VK_NULL_HANDLE;
    }
    if (a->wormhole_tri_vertex_buffer) {
        vkDestroyBuffer(a->device, a->wormhole_tri_vertex_buffer, NULL);
        a->wormhole_tri_vertex_buffer = VK_NULL_HANDLE;
    }
    if (a->terrain_vertex_memory) {
        vkFreeMemory(a->device, a->terrain_vertex_memory, NULL);
        a->terrain_vertex_memory = VK_NULL_HANDLE;
    }
    if (a->terrain_tri_index_memory) {
        vkFreeMemory(a->device, a->terrain_tri_index_memory, NULL);
        a->terrain_tri_index_memory = VK_NULL_HANDLE;
    }
    if (a->terrain_wire_vertex_memory) {
        vkFreeMemory(a->device, a->terrain_wire_vertex_memory, NULL);
        a->terrain_wire_vertex_memory = VK_NULL_HANDLE;
    }
    if (a->particle_instance_memory) {
        vkFreeMemory(a->device, a->particle_instance_memory, NULL);
        a->particle_instance_memory = VK_NULL_HANDLE;
    }
    if (a->wormhole_line_vertex_memory) {
        vkFreeMemory(a->device, a->wormhole_line_vertex_memory, NULL);
        a->wormhole_line_vertex_memory = VK_NULL_HANDLE;
    }
    if (a->wormhole_tri_vertex_memory) {
        vkFreeMemory(a->device, a->wormhole_tri_vertex_memory, NULL);
        a->wormhole_tri_vertex_memory = VK_NULL_HANDLE;
    }
    for (uint32_t i = 0; i < APP_MAX_SWAPCHAIN_IMAGES; ++i) {
        if (a->present_framebuffers[i]) {
            vkDestroyFramebuffer(a->device, a->present_framebuffers[i], NULL);
            a->present_framebuffers[i] = VK_NULL_HANDLE;
        }
        if (a->swapchain_image_views[i]) {
            vkDestroyImageView(a->device, a->swapchain_image_views[i], NULL);
            a->swapchain_image_views[i] = VK_NULL_HANDLE;
        }
    }
    if (a->scene_render_pass) {
        vkDestroyRenderPass(a->device, a->scene_render_pass, NULL);
        a->scene_render_pass = VK_NULL_HANDLE;
    }
    if (a->bloom_render_pass) {
        vkDestroyRenderPass(a->device, a->bloom_render_pass, NULL);
        a->bloom_render_pass = VK_NULL_HANDLE;
    }
    if (a->present_render_pass) {
        vkDestroyRenderPass(a->device, a->present_render_pass, NULL);
        a->present_render_pass = VK_NULL_HANDLE;
    }
    if (a->swapchain) {
        vkDestroySwapchainKHR(a->device, a->swapchain, NULL);
        a->swapchain = VK_NULL_HANDLE;
    }
    if (a->in_flight) {
        vkDestroyFence(a->device, a->in_flight, NULL);
        a->in_flight = VK_NULL_HANDLE;
    }
    if (a->render_finished) {
        vkDestroySemaphore(a->device, a->render_finished, NULL);
        a->render_finished = VK_NULL_HANDLE;
    }
    if (a->image_available) {
        vkDestroySemaphore(a->device, a->image_available, NULL);
        a->image_available = VK_NULL_HANDLE;
    }
    if (a->command_pool) {
        vkDestroyCommandPool(a->device, a->command_pool, NULL);
        a->command_pool = VK_NULL_HANDLE;
    }
    memset(a->swapchain_images, 0, sizeof(a->swapchain_images));
    memset(a->command_buffers, 0, sizeof(a->command_buffers));
    a->swapchain_image_count = 0;
}

static int recreate_render_runtime(app* a) {
    if (!a) {
        return 0;
    }
    vg_crt_profile saved_crt;
    int have_crt = 0;
    if (a->vg) {
        vg_get_crt_profile(a->vg, &saved_crt);
        have_crt = 1;
    }
    destroy_render_runtime(a);
    if (!create_swapchain(a) ||
        !create_render_passes(a) ||
        !create_offscreen_targets(a) ||
        !create_present_framebuffers(a) ||
        !create_commands(a) ||
        !create_sync(a) ||
        !create_post_resources(a) ||
        !create_terrain_resources(a) ||
        !create_particle_resources(a) ||
        !create_wormhole_resources(a) ||
        !create_vg_context(a)) {
        return 0;
    }
    if (have_crt) {
        vg_set_crt_profile(a->vg, &saved_crt);
    }
    game_set_world_size(&a->game, (float)a->swapchain_extent.width, (float)a->swapchain_extent.height);
    a->force_clear_frames = 2;
    return 1;
}

static int apply_video_mode(app* a) {
    if (!a || !a->window) {
        return 0;
    }
    const int selected = a->video_menu_selected;
    if (selected <= 0) {
        a->video_menu_fullscreen = 1;
        if (SDL_SetWindowFullscreen(a->window, SDL_WINDOW_FULLSCREEN_DESKTOP) != 0) {
            return 0;
        }
    } else {
        const int idx = selected - 1;
        if (idx < 0 || idx >= VIDEO_MENU_RES_COUNT) {
            return 0;
        }
        a->video_menu_fullscreen = 0;
        if (SDL_SetWindowFullscreen(a->window, 0) != 0) {
            return 0;
        }
        SDL_SetWindowSize(a->window, k_video_resolutions[idx].w, k_video_resolutions[idx].h);
        SDL_SetWindowPosition(a->window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    }
    if (!recreate_render_runtime(a)) {
        return 0;
    }
    (void)save_settings(a);
    return 1;
}

static int create_instance(app* a) {
    uint32_t ext_count = 0;
    if (!SDL_Vulkan_GetInstanceExtensions(a->window, &ext_count, NULL) || ext_count == 0) return 0;
    const char** exts = (const char**)calloc(ext_count, sizeof(*exts));
    if (!exts) return 0;
    if (!SDL_Vulkan_GetInstanceExtensions(a->window, &ext_count, exts)) {
        free(exts);
        return 0;
    }
    VkApplicationInfo ai = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "v_type",
        .applicationVersion = VK_MAKE_VERSION(0, 1, 0),
        .pEngineName = "none",
        .engineVersion = VK_MAKE_VERSION(0, 0, 0),
        .apiVersion = VK_API_VERSION_1_0
    };
    VkInstanceCreateInfo ci = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &ai,
        .enabledExtensionCount = ext_count,
        .ppEnabledExtensionNames = exts
    };
    VkResult r = vkCreateInstance(&ci, NULL, &a->instance);
    free(exts);
    return check_vk(r, "vkCreateInstance");
}

static int create_surface(app* a) {
    return SDL_Vulkan_CreateSurface(a->window, a->instance, &a->surface) == SDL_TRUE;
}

static int pick_physical_device(app* a) {
    uint32_t count = 0;
    if (!check_vk(vkEnumeratePhysicalDevices(a->instance, &count, NULL), "vkEnumeratePhysicalDevices(count)") || count == 0) return 0;
    VkPhysicalDevice* devs = (VkPhysicalDevice*)calloc(count, sizeof(*devs));
    if (!devs) return 0;
    if (!check_vk(vkEnumeratePhysicalDevices(a->instance, &count, devs), "vkEnumeratePhysicalDevices(list)")) {
        free(devs);
        return 0;
    }
    for (uint32_t d = 0; d < count; ++d) {
        VkPhysicalDevice dev = devs[d];
        uint32_t qcount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(dev, &qcount, NULL);
        VkQueueFamilyProperties* qprops = (VkQueueFamilyProperties*)calloc(qcount, sizeof(*qprops));
        if (!qprops) continue;
        vkGetPhysicalDeviceQueueFamilyProperties(dev, &qcount, qprops);
        int g = 0, p = 0;
        uint32_t gi = 0, pi = 0;
        for (uint32_t i = 0; i < qcount; ++i) {
            if (qprops[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                g = 1;
                gi = i;
            }
            VkBool32 present = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(dev, i, a->surface, &present);
            if (present) {
                p = 1;
                pi = i;
            }
        }
        free(qprops);
        if (!g || !p) continue;
        a->physical_device = dev;
        a->graphics_queue_family = gi;
        a->present_queue_family = pi;
        a->msaa_samples = pick_msaa_samples(a);
        free(devs);
        return 1;
    }
    free(devs);
    return 0;
}

static int create_device(app* a) {
    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci[2] = {0};
    qci[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci[0].queueFamilyIndex = a->graphics_queue_family;
    qci[0].queueCount = 1;
    qci[0].pQueuePriorities = &prio;
    uint32_t qcount = 1;
    if (a->present_queue_family != a->graphics_queue_family) {
        qci[1] = qci[0];
        qci[1].queueFamilyIndex = a->present_queue_family;
        qcount = 2;
    }
    const char* dev_exts[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkPhysicalDeviceFeatures supported;
    memset(&supported, 0, sizeof(supported));
    vkGetPhysicalDeviceFeatures(a->physical_device, &supported);
    VkPhysicalDeviceFeatures enabled;
    memset(&enabled, 0, sizeof(enabled));
    if (supported.fillModeNonSolid) {
        enabled.fillModeNonSolid = VK_TRUE;
    }
    VkDeviceCreateInfo ci = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = qcount,
        .pQueueCreateInfos = qci,
        .enabledExtensionCount = 1,
        .ppEnabledExtensionNames = dev_exts,
        .pEnabledFeatures = &enabled
    };
    if (!check_vk(vkCreateDevice(a->physical_device, &ci, NULL, &a->device), "vkCreateDevice")) return 0;
    vkGetDeviceQueue(a->device, a->graphics_queue_family, 0, &a->graphics_queue);
    vkGetDeviceQueue(a->device, a->present_queue_family, 0, &a->present_queue);
    return 1;
}

static VkSurfaceFormatKHR choose_surface_format(const VkSurfaceFormatKHR* formats, uint32_t count) {
    for (uint32_t i = 0; i < count; ++i) {
        if (formats[i].format == VK_FORMAT_B8G8R8A8_UNORM && formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return formats[i];
        }
    }
    return formats[0];
}

static VkPresentModeKHR choose_present_mode(const VkPresentModeKHR* modes, uint32_t count) {
    for (uint32_t i = 0; i < count; ++i) {
        if (modes[i] == VK_PRESENT_MODE_MAILBOX_KHR) return modes[i];
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

static VkExtent2D clamp_extent_to_caps(VkExtent2D extent, const VkSurfaceCapabilitiesKHR* caps) {
    if (!caps) {
        return extent;
    }
    if (extent.width < caps->minImageExtent.width) extent.width = caps->minImageExtent.width;
    if (extent.height < caps->minImageExtent.height) extent.height = caps->minImageExtent.height;
    if (caps->maxImageExtent.width > 0 && extent.width > caps->maxImageExtent.width) extent.width = caps->maxImageExtent.width;
    if (caps->maxImageExtent.height > 0 && extent.height > caps->maxImageExtent.height) extent.height = caps->maxImageExtent.height;
    return extent;
}

static int create_swapchain(app* a) {
    VkSurfaceCapabilitiesKHR caps;
    if (!check_vk(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(a->physical_device, a->surface, &caps), "vkGetPhysicalDeviceSurfaceCapabilitiesKHR")) return 0;

    uint32_t fmt_count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(a->physical_device, a->surface, &fmt_count, NULL);
    VkSurfaceFormatKHR* formats = (VkSurfaceFormatKHR*)calloc(fmt_count, sizeof(*formats));
    if (!formats) return 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(a->physical_device, a->surface, &fmt_count, formats);
    VkSurfaceFormatKHR fmt = choose_surface_format(formats, fmt_count);
    free(formats);

    uint32_t mode_count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(a->physical_device, a->surface, &mode_count, NULL);
    VkPresentModeKHR* modes = (VkPresentModeKHR*)calloc(mode_count > 0 ? mode_count : 1u, sizeof(*modes));
    if (!modes) return 0;
    if (mode_count > 0) vkGetPhysicalDeviceSurfacePresentModesKHR(a->physical_device, a->surface, &mode_count, modes);
    VkPresentModeKHR mode = choose_present_mode(modes, mode_count);
    free(modes);

    int drawable_w = 0;
    int drawable_h = 0;
    SDL_Vulkan_GetDrawableSize(a->window, &drawable_w, &drawable_h);
    VkExtent2D drawable_extent = {
        .width = (drawable_w > 0) ? (uint32_t)drawable_w : APP_WIDTH,
        .height = (drawable_h > 0) ? (uint32_t)drawable_h : APP_HEIGHT
    };
    drawable_extent = clamp_extent_to_caps(drawable_extent, &caps);

    VkExtent2D extent = caps.currentExtent;
    if (caps.currentExtent.width == UINT32_MAX || caps.currentExtent.height == UINT32_MAX) {
        extent = drawable_extent;
    } else if (caps.currentExtent.width != drawable_extent.width || caps.currentExtent.height != drawable_extent.height) {
        /* Some WSI paths report currentExtent in logical units; prefer drawable pixels when valid. */
        extent = drawable_extent;
    }
    uint32_t image_count = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && image_count > caps.maxImageCount) image_count = caps.maxImageCount;
    if (image_count > APP_MAX_SWAPCHAIN_IMAGES) image_count = APP_MAX_SWAPCHAIN_IMAGES;

    uint32_t qidx[2] = {a->graphics_queue_family, a->present_queue_family};
    VkSwapchainCreateInfoKHR ci = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = a->surface,
        .minImageCount = image_count,
        .imageFormat = fmt.format,
        .imageColorSpace = fmt.colorSpace,
        .imageExtent = extent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .preTransform = caps.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = mode,
        .clipped = VK_TRUE
    };
    if (a->graphics_queue_family != a->present_queue_family) {
        ci.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        ci.queueFamilyIndexCount = 2;
        ci.pQueueFamilyIndices = qidx;
    } else {
        ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }
    VkResult sc_res = vkCreateSwapchainKHR(a->device, &ci, NULL, &a->swapchain);
    if (sc_res != VK_SUCCESS) {
        /* Fallback to strict surface extent if the platform rejects drawable-sized swapchains. */
        if (caps.currentExtent.width != UINT32_MAX && caps.currentExtent.height != UINT32_MAX &&
            (extent.width != caps.currentExtent.width || extent.height != caps.currentExtent.height)) {
            ci.imageExtent = caps.currentExtent;
            sc_res = vkCreateSwapchainKHR(a->device, &ci, NULL, &a->swapchain);
            extent = caps.currentExtent;
        }
    }
    if (!check_vk(sc_res, "vkCreateSwapchainKHR")) return 0;

    fprintf(
        stderr,
        "swapchain extent=%ux%u drawable=%dx%d currentExtent=%ux%u\n",
        extent.width,
        extent.height,
        drawable_w,
        drawable_h,
        caps.currentExtent.width,
        caps.currentExtent.height
    );

    a->swapchain_format = fmt.format;
    a->swapchain_extent = extent;
    a->swapchain_image_count = APP_MAX_SWAPCHAIN_IMAGES;
    if (!check_vk(vkGetSwapchainImagesKHR(a->device, a->swapchain, &a->swapchain_image_count, a->swapchain_images), "vkGetSwapchainImagesKHR")) return 0;

    for (uint32_t i = 0; i < a->swapchain_image_count; ++i) {
        VkImageViewCreateInfo vi = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = a->swapchain_images[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = a->swapchain_format,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1
            }
        };
        if (!check_vk(vkCreateImageView(a->device, &vi, NULL, &a->swapchain_image_views[i]), "vkCreateImageView(swapchain)")) return 0;
    }
    return 1;
}

static int create_render_passes(app* a) {
    a->scene_depth_format = find_depth_format(a);
    if (a->scene_depth_format == VK_FORMAT_UNDEFINED) {
        fprintf(stderr, "No suitable depth format found\n");
        return 0;
    }
    VkSampleCountFlagBits samples = scene_samples(a);
    const int has_stencil = format_has_stencil(a->scene_depth_format);
    if (samples == VK_SAMPLE_COUNT_1_BIT) {
        VkAttachmentDescription atts[2];
        atts[0] = (VkAttachmentDescription){
            .format = a->swapchain_format,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        };
        atts[1] = (VkAttachmentDescription){
            .format = a->scene_depth_format,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .stencilLoadOp = has_stencil ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = has_stencil ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
        };
        VkAttachmentReference scene_ref = {.attachment = 0, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkAttachmentReference depth_ref = {.attachment = 1, .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
        VkSubpassDescription scene_sub = {
            .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
            .colorAttachmentCount = 1,
            .pColorAttachments = &scene_ref,
            .pDepthStencilAttachment = &depth_ref
        };
        VkRenderPassCreateInfo scene_rp = {
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
            .attachmentCount = 2,
            .pAttachments = atts,
            .subpassCount = 1,
            .pSubpasses = &scene_sub
        };
        if (!check_vk(vkCreateRenderPass(a->device, &scene_rp, NULL, &a->scene_render_pass), "vkCreateRenderPass(scene)")) return 0;
    } else {
        VkAttachmentDescription atts[3];
        atts[0] = (VkAttachmentDescription){
            .format = a->swapchain_format,
            .samples = samples,
            .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
        };
        atts[1] = (VkAttachmentDescription){
            .format = a->swapchain_format,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        };
        atts[2] = (VkAttachmentDescription){
            .format = a->scene_depth_format,
            .samples = samples,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .stencilLoadOp = has_stencil ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = has_stencil ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
        };
        VkAttachmentReference color_ref = {.attachment = 0, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkAttachmentReference resolve_ref = {.attachment = 1, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkAttachmentReference depth_ref = {.attachment = 2, .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
        VkSubpassDescription scene_sub = {
            .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
            .colorAttachmentCount = 1,
            .pColorAttachments = &color_ref,
            .pResolveAttachments = &resolve_ref,
            .pDepthStencilAttachment = &depth_ref
        };
        VkRenderPassCreateInfo scene_rp = {
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
            .attachmentCount = 3,
            .pAttachments = atts,
            .subpassCount = 1,
            .pSubpasses = &scene_sub
        };
        if (!check_vk(vkCreateRenderPass(a->device, &scene_rp, NULL, &a->scene_render_pass), "vkCreateRenderPass(scene msaa)")) return 0;
    }

    VkAttachmentDescription bloom_att = {
        .format = a->swapchain_format,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    };
    VkAttachmentReference bloom_ref = {.attachment = 0, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription bloom_sub = {.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS, .colorAttachmentCount = 1, .pColorAttachments = &bloom_ref};
    VkRenderPassCreateInfo bloom_rp = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO, .attachmentCount = 1, .pAttachments = &bloom_att, .subpassCount = 1, .pSubpasses = &bloom_sub};
    if (!check_vk(vkCreateRenderPass(a->device, &bloom_rp, NULL, &a->bloom_render_pass), "vkCreateRenderPass(bloom)")) return 0;

    VkAttachmentDescription present_att = {
        .format = a->swapchain_format,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
    };
    VkAttachmentReference present_ref = {.attachment = 0, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription present_sub = {.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS, .colorAttachmentCount = 1, .pColorAttachments = &present_ref};
    VkRenderPassCreateInfo present_rp = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO, .attachmentCount = 1, .pAttachments = &present_att, .subpassCount = 1, .pSubpasses = &present_sub};
    return check_vk(vkCreateRenderPass(a->device, &present_rp, NULL, &a->present_render_pass), "vkCreateRenderPass(present)");
}

static int create_offscreen_targets(app* a) {
    uint32_t w = a->swapchain_extent.width;
    uint32_t h = a->swapchain_extent.height;
    VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    VkSampleCountFlagBits samples = scene_samples(a);

    if (!create_image_2d(a, w, h, a->swapchain_format, usage, VK_SAMPLE_COUNT_1_BIT, &a->scene_image, &a->scene_memory, &a->scene_view)) return 0;
    if (!create_image_2d(a, w, h, a->swapchain_format, usage, VK_SAMPLE_COUNT_1_BIT, &a->bloom_image, &a->bloom_memory, &a->bloom_view)) return 0;
    if (!create_depth_image_2d(a, w, h, a->scene_depth_format, samples, &a->scene_depth_image, &a->scene_depth_memory, &a->scene_depth_view)) return 0;
    if (samples != VK_SAMPLE_COUNT_1_BIT) {
        VkImageUsageFlags msaa_usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        if (!create_image_2d(a, w, h, a->swapchain_format, msaa_usage, samples, &a->scene_msaa_image, &a->scene_msaa_memory, &a->scene_msaa_view)) return 0;
    }

    VkImageView scene_att_1[] = {a->scene_view, a->scene_depth_view};
    VkImageView scene_att_2[] = {a->scene_msaa_view, a->scene_view, a->scene_depth_view};
    VkFramebufferCreateInfo scene_fb = {
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass = a->scene_render_pass,
        .attachmentCount = (samples == VK_SAMPLE_COUNT_1_BIT) ? 2u : 3u,
        .pAttachments = (samples == VK_SAMPLE_COUNT_1_BIT) ? scene_att_1 : scene_att_2,
        .width = w,
        .height = h,
        .layers = 1
    };
    if (!check_vk(vkCreateFramebuffer(a->device, &scene_fb, NULL, &a->scene_fb), "vkCreateFramebuffer(scene)")) return 0;

    VkImageView bloom_att[] = {a->bloom_view};
    VkFramebufferCreateInfo bloom_fb = {.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, .renderPass = a->bloom_render_pass, .attachmentCount = 1, .pAttachments = bloom_att, .width = w, .height = h, .layers = 1};
    return check_vk(vkCreateFramebuffer(a->device, &bloom_fb, NULL, &a->bloom_fb), "vkCreateFramebuffer(bloom)");
}

static int create_present_framebuffers(app* a) {
    for (uint32_t i = 0; i < a->swapchain_image_count; ++i) {
        VkImageView att[] = {a->swapchain_image_views[i]};
        VkFramebufferCreateInfo fb = {
            .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass = a->present_render_pass,
            .attachmentCount = 1,
            .pAttachments = att,
            .width = a->swapchain_extent.width,
            .height = a->swapchain_extent.height,
            .layers = 1
        };
        if (!check_vk(vkCreateFramebuffer(a->device, &fb, NULL, &a->present_framebuffers[i]), "vkCreateFramebuffer(present)")) return 0;
    }
    return 1;
}

static int create_commands(app* a) {
    VkCommandPoolCreateInfo pool = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, .queueFamilyIndex = a->graphics_queue_family};
    if (!check_vk(vkCreateCommandPool(a->device, &pool, NULL, &a->command_pool), "vkCreateCommandPool")) return 0;
    VkCommandBufferAllocateInfo alloc = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = a->command_pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = a->swapchain_image_count};
    return check_vk(vkAllocateCommandBuffers(a->device, &alloc, a->command_buffers), "vkAllocateCommandBuffers");
}

static int create_sync(app* a) {
    VkSemaphoreCreateInfo sem = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo fence = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .flags = VK_FENCE_CREATE_SIGNALED_BIT};
    if (!check_vk(vkCreateSemaphore(a->device, &sem, NULL, &a->image_available), "vkCreateSemaphore(image_available)")) return 0;
    if (!check_vk(vkCreateSemaphore(a->device, &sem, NULL, &a->render_finished), "vkCreateSemaphore(render_finished)")) return 0;
    return check_vk(vkCreateFence(a->device, &fence, NULL, &a->in_flight), "vkCreateFence");
}

static int create_post_resources(app* a) {
#if !V_TYPE_HAS_POST_SHADERS
    (void)a;
    fprintf(stderr, "Post shaders unavailable.\n");
    return 0;
#else
    VkSamplerCreateInfo sampler = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .maxLod = 1.0f
    };
    if (!check_vk(vkCreateSampler(a->device, &sampler, NULL, &a->post_sampler), "vkCreateSampler")) return 0;

    VkDescriptorSetLayoutBinding bindings[2] = {
        {.binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT},
        {.binding = 1, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT}
    };
    VkDescriptorSetLayoutCreateInfo dsl = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = 2, .pBindings = bindings};
    if (!check_vk(vkCreateDescriptorSetLayout(a->device, &dsl, NULL, &a->post_desc_layout), "vkCreateDescriptorSetLayout")) return 0;

    VkDescriptorPoolSize pool_size = {.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 2};
    VkDescriptorPoolCreateInfo pool = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, .poolSizeCount = 1, .pPoolSizes = &pool_size, .maxSets = 1};
    if (!check_vk(vkCreateDescriptorPool(a->device, &pool, NULL, &a->post_desc_pool), "vkCreateDescriptorPool")) return 0;

    VkDescriptorSetAllocateInfo alloc = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, .descriptorPool = a->post_desc_pool, .descriptorSetCount = 1, .pSetLayouts = &a->post_desc_layout};
    if (!check_vk(vkAllocateDescriptorSets(a->device, &alloc, &a->post_desc_set), "vkAllocateDescriptorSets")) return 0;

    VkDescriptorImageInfo scene_info = {.sampler = a->post_sampler, .imageView = a->scene_view, .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkDescriptorImageInfo bloom_info = {.sampler = a->post_sampler, .imageView = a->bloom_view, .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet writes[2] = {
        {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = a->post_desc_set, .dstBinding = 0, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .pImageInfo = &scene_info},
        {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = a->post_desc_set, .dstBinding = 1, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .pImageInfo = &bloom_info}
    };
    vkUpdateDescriptorSets(a->device, 2, writes, 0, NULL);

    VkPushConstantRange pc = {.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT, .offset = 0, .size = sizeof(post_pc)};
    VkPipelineLayoutCreateInfo pli = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &a->post_desc_layout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pc
    };
    if (!check_vk(vkCreatePipelineLayout(a->device, &pli, NULL, &a->post_layout), "vkCreatePipelineLayout(post)")) return 0;

    VkShaderModuleCreateInfo vs_ci = {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize = demo_fullscreen_vert_spv_len, .pCode = (const uint32_t*)demo_fullscreen_vert_spv};
    VkShaderModuleCreateInfo bloom_ci = {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize = demo_bloom_frag_spv_len, .pCode = (const uint32_t*)demo_bloom_frag_spv};
    VkShaderModuleCreateInfo comp_ci = {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize = demo_composite_frag_spv_len, .pCode = (const uint32_t*)demo_composite_frag_spv};

    VkShaderModule vs = VK_NULL_HANDLE, fs_bloom = VK_NULL_HANDLE, fs_comp = VK_NULL_HANDLE;
    if (!check_vk(vkCreateShaderModule(a->device, &vs_ci, NULL, &vs), "vkCreateShaderModule(vs)")) return 0;
    if (!check_vk(vkCreateShaderModule(a->device, &bloom_ci, NULL, &fs_bloom), "vkCreateShaderModule(fs bloom)")) return 0;
    if (!check_vk(vkCreateShaderModule(a->device, &comp_ci, NULL, &fs_comp), "vkCreateShaderModule(fs comp)")) return 0;

    VkPipelineShaderStageCreateInfo stages[2] = {
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vs, .pName = "main"},
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = fs_bloom, .pName = "main"}
    };
    VkPipelineVertexInputStateCreateInfo vi = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia = {.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
    VkPipelineViewportStateCreateInfo vp = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, .viewportCount = 1, .scissorCount = 1};
    VkPipelineRasterizationStateCreateInfo rs = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, .polygonMode = VK_POLYGON_MODE_FILL, .lineWidth = 1.0f, .cullMode = VK_CULL_MODE_NONE, .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE};
    VkPipelineMultisampleStateCreateInfo ms = {.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT};
    VkPipelineColorBlendAttachmentState cb_att = {
        .blendEnable = VK_TRUE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
    };
    VkPipelineColorBlendStateCreateInfo cb = {.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, .attachmentCount = 1, .pAttachments = &cb_att};
    VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo ds = {.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, .dynamicStateCount = 2, .pDynamicStates = dyn};
    VkGraphicsPipelineCreateInfo gp = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2,
        .pStages = stages,
        .pVertexInputState = &vi,
        .pInputAssemblyState = &ia,
        .pViewportState = &vp,
        .pRasterizationState = &rs,
        .pMultisampleState = &ms,
        .pColorBlendState = &cb,
        .pDynamicState = &ds,
        .layout = a->post_layout,
        .renderPass = a->bloom_render_pass,
        .subpass = 0
    };
    if (!check_vk(vkCreateGraphicsPipelines(a->device, VK_NULL_HANDLE, 1, &gp, NULL, &a->bloom_pipeline), "vkCreateGraphicsPipelines(bloom)")) return 0;

    stages[1].module = fs_comp;
    cb_att.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    cb_att.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cb_att.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    cb_att.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    gp.renderPass = a->present_render_pass;
    if (!check_vk(vkCreateGraphicsPipelines(a->device, VK_NULL_HANDLE, 1, &gp, NULL, &a->composite_pipeline), "vkCreateGraphicsPipelines(composite)")) return 0;

    vkDestroyShaderModule(a->device, fs_comp, NULL);
    vkDestroyShaderModule(a->device, fs_bloom, NULL);
    vkDestroyShaderModule(a->device, vs, NULL);
    return 1;
#endif
}

static int create_terrain_resources(app* a) {
#if !V_TYPE_HAS_TERRAIN_SHADERS
    (void)a;
    return 1;
#else
    const uint32_t vcount = (uint32_t)(TERRAIN_ROWS * TERRAIN_COLS);
    const VkDeviceSize vbuf_size = (VkDeviceSize)vcount * sizeof(terrain_vertex);
    const uint32_t wire_vcount = (uint32_t)(TERRAIN_ROWS - 1) * (uint32_t)(TERRAIN_COLS - 1) * 6u;
    const VkDeviceSize wire_vbuf_size = (VkDeviceSize)wire_vcount * sizeof(terrain_wire_vertex);
    uint16_t tri_idx[(TERRAIN_ROWS - 1) * (TERRAIN_COLS - 1) * 6];
    uint32_t tri_count = 0;

    for (int r = 0; r < TERRAIN_ROWS - 1; ++r) {
        for (int c = 0; c < TERRAIN_COLS - 1; ++c) {
            const uint16_t i00 = (uint16_t)(r * TERRAIN_COLS + c);
            const uint16_t i10 = (uint16_t)(r * TERRAIN_COLS + c + 1);
            const uint16_t i01 = (uint16_t)((r + 1) * TERRAIN_COLS + c);
            const uint16_t i11 = (uint16_t)((r + 1) * TERRAIN_COLS + c + 1);
            tri_idx[tri_count++] = i00; tri_idx[tri_count++] = i10; tri_idx[tri_count++] = i01;
            tri_idx[tri_count++] = i10; tri_idx[tri_count++] = i11; tri_idx[tri_count++] = i01;
        }
    }

    if (!create_buffer(
            a, vbuf_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &a->terrain_vertex_buffer, &a->terrain_vertex_memory)) {
        return 0;
    }
    if (!check_vk(vkMapMemory(a->device, a->terrain_vertex_memory, 0, vbuf_size, 0, &a->terrain_vertex_map), "vkMapMemory(terrain verts)")) {
        return 0;
    }
    if (!create_buffer(
            a, wire_vbuf_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &a->terrain_wire_vertex_buffer, &a->terrain_wire_vertex_memory)) {
        return 0;
    }
    if (!check_vk(vkMapMemory(a->device, a->terrain_wire_vertex_memory, 0, wire_vbuf_size, 0, &a->terrain_wire_vertex_map), "vkMapMemory(terrain wire verts)")) {
        return 0;
    }
    update_gpu_high_plains_vertices(a);

    if (!create_buffer(
            a, (VkDeviceSize)tri_count * sizeof(uint16_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &a->terrain_tri_index_buffer, &a->terrain_tri_index_memory)) {
        return 0;
    }
    {
        void* p = NULL;
        if (!check_vk(vkMapMemory(a->device, a->terrain_tri_index_memory, 0, VK_WHOLE_SIZE, 0, &p), "vkMapMemory(terrain tri idx)")) {
            return 0;
        }
        memcpy(p, tri_idx, (size_t)tri_count * sizeof(uint16_t));
        vkUnmapMemory(a->device, a->terrain_tri_index_memory);
    }
    a->terrain_tri_index_count = tri_count;
    a->terrain_wire_vertex_count = wire_vcount;

    VkPushConstantRange pc = {.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, .offset = 0, .size = sizeof(terrain_pc)};
    VkPipelineLayoutCreateInfo pli = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 0,
        .pSetLayouts = NULL,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pc
    };
    if (!check_vk(vkCreatePipelineLayout(a->device, &pli, NULL, &a->terrain_layout), "vkCreatePipelineLayout(terrain)")) {
        return 0;
    }

    VkShaderModuleCreateInfo vs_ci = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = v_type_terrain_vert_spv_len,
        .pCode = (const uint32_t*)v_type_terrain_vert_spv
    };
    VkShaderModuleCreateInfo fs_ci = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = v_type_terrain_frag_spv_len,
        .pCode = (const uint32_t*)v_type_terrain_frag_spv
    };
    VkShaderModuleCreateInfo vs_wire_ci = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = v_type_terrain_wire_vert_spv_len,
        .pCode = (const uint32_t*)v_type_terrain_wire_vert_spv
    };
    VkShaderModuleCreateInfo fs_wire_ci = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = v_type_terrain_wire_frag_spv_len,
        .pCode = (const uint32_t*)v_type_terrain_wire_frag_spv
    };
    VkShaderModule vs = VK_NULL_HANDLE;
    VkShaderModule vs_wire = VK_NULL_HANDLE;
    VkShaderModule fs = VK_NULL_HANDLE;
    VkShaderModule fs_wire = VK_NULL_HANDLE;
    if (!check_vk(vkCreateShaderModule(a->device, &vs_ci, NULL, &vs), "vkCreateShaderModule(terrain vs)")) {
        return 0;
    }
    if (!check_vk(vkCreateShaderModule(a->device, &fs_ci, NULL, &fs), "vkCreateShaderModule(terrain fs)")) {
        vkDestroyShaderModule(a->device, vs, NULL);
        return 0;
    }
    if (!check_vk(vkCreateShaderModule(a->device, &vs_wire_ci, NULL, &vs_wire), "vkCreateShaderModule(terrain wire vs)")) {
        vkDestroyShaderModule(a->device, fs, NULL);
        vkDestroyShaderModule(a->device, vs, NULL);
        return 0;
    }
    if (!check_vk(vkCreateShaderModule(a->device, &fs_wire_ci, NULL, &fs_wire), "vkCreateShaderModule(terrain wire fs)")) {
        vkDestroyShaderModule(a->device, vs_wire, NULL);
        vkDestroyShaderModule(a->device, fs, NULL);
        vkDestroyShaderModule(a->device, vs, NULL);
        return 0;
    }

    VkPipelineShaderStageCreateInfo stages_fill[2] = {
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vs, .pName = "main"},
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = fs, .pName = "main"}
    };
    VkPipelineShaderStageCreateInfo stages_line[2] = {
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vs_wire, .pName = "main"},
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = fs_wire, .pName = "main"}
    };
    VkVertexInputBindingDescription binding_fill = {.binding = 0, .stride = sizeof(terrain_vertex), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attr_fill = {.location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = 0};
    VkPipelineVertexInputStateCreateInfo vi_fill = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &binding_fill,
        .vertexAttributeDescriptionCount = 1,
        .pVertexAttributeDescriptions = &attr_fill
    };
    VkVertexInputBindingDescription binding_line = {.binding = 0, .stride = sizeof(terrain_wire_vertex), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attr_line[2];
    memset(attr_line, 0, sizeof(attr_line));
    attr_line[0].location = 0;
    attr_line[0].binding = 0;
    attr_line[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attr_line[0].offset = 0;
    attr_line[1].location = 1;
    attr_line[1].binding = 0;
    attr_line[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attr_line[1].offset = sizeof(float) * 3;
    VkPipelineVertexInputStateCreateInfo vi_line = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &binding_line,
        .vertexAttributeDescriptionCount = 2,
        .pVertexAttributeDescriptions = attr_line
    };
    VkPipelineInputAssemblyStateCreateInfo ia = {.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
    VkPipelineViewportStateCreateInfo vp = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, .viewportCount = 1, .scissorCount = 1};
    VkPipelineRasterizationStateCreateInfo rs = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .lineWidth = 1.0f,
        .cullMode = VK_CULL_MODE_BACK_BIT,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE
    };
    VkPipelineMultisampleStateCreateInfo ms = {.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, .rasterizationSamples = scene_samples(a)};
    VkPipelineColorBlendAttachmentState cb_att = {
        .blendEnable = VK_TRUE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
    };
    VkPipelineColorBlendStateCreateInfo cb = {.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, .attachmentCount = 1, .pAttachments = &cb_att};
    VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo ds = {.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, .dynamicStateCount = 2, .pDynamicStates = dyn};
    VkPipelineDepthStencilStateCreateInfo depth = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_TRUE,
        .depthWriteEnable = VK_TRUE,
        .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL
    };
    VkGraphicsPipelineCreateInfo gp = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2,
        .pStages = stages_fill,
        .pVertexInputState = &vi_fill,
        .pInputAssemblyState = &ia,
        .pViewportState = &vp,
        .pRasterizationState = &rs,
        .pMultisampleState = &ms,
        .pDepthStencilState = &depth,
        .pColorBlendState = &cb,
        .pDynamicState = &ds,
        .layout = a->terrain_layout,
        .renderPass = a->scene_render_pass,
        .subpass = 0
    };
    if (!check_vk(vkCreateGraphicsPipelines(a->device, VK_NULL_HANDLE, 1, &gp, NULL, &a->terrain_fill_pipeline), "vkCreateGraphicsPipelines(terrain fill)")) {
        vkDestroyShaderModule(a->device, vs_wire, NULL);
        vkDestroyShaderModule(a->device, fs_wire, NULL);
        vkDestroyShaderModule(a->device, fs, NULL);
        vkDestroyShaderModule(a->device, vs, NULL);
        return 0;
    }

    rs.cullMode = VK_CULL_MODE_BACK_BIT;
    rs.depthBiasEnable = VK_FALSE;
    cb_att.blendEnable = VK_TRUE;
    cb_att.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    cb_att.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cb_att.colorBlendOp = VK_BLEND_OP_ADD;
    cb_att.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    cb_att.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cb_att.alphaBlendOp = VK_BLEND_OP_ADD;
    cb_att.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    depth.depthTestEnable = VK_TRUE;
    depth.depthWriteEnable = VK_FALSE;
    gp.pStages = stages_line;
    gp.pVertexInputState = &vi_line;
    if (!check_vk(vkCreateGraphicsPipelines(a->device, VK_NULL_HANDLE, 1, &gp, NULL, &a->terrain_line_pipeline), "vkCreateGraphicsPipelines(terrain line)")) {
        vkDestroyShaderModule(a->device, vs_wire, NULL);
        vkDestroyShaderModule(a->device, fs_wire, NULL);
        vkDestroyShaderModule(a->device, fs, NULL);
        vkDestroyShaderModule(a->device, vs, NULL);
        return 0;
    }

    vkDestroyShaderModule(a->device, vs_wire, NULL);
    vkDestroyShaderModule(a->device, fs_wire, NULL);
    vkDestroyShaderModule(a->device, fs, NULL);
    vkDestroyShaderModule(a->device, vs, NULL);
    return 1;
#endif
}

static int create_particle_resources(app* a) {
#if !V_TYPE_HAS_TERRAIN_SHADERS
    (void)a;
    return 1;
#else
    const VkDeviceSize ibuf_size = (VkDeviceSize)GPU_PARTICLE_MAX_INSTANCES * sizeof(particle_instance);
    if (!create_buffer(
            a, ibuf_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &a->particle_instance_buffer, &a->particle_instance_memory)) {
        return 0;
    }
    if (!check_vk(vkMapMemory(a->device, a->particle_instance_memory, 0, ibuf_size, 0, &a->particle_instance_map), "vkMapMemory(particles)")) {
        return 0;
    }
    a->particle_instance_count = 0;

    VkPushConstantRange pc = {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = sizeof(particle_pc)
    };
    VkPipelineLayoutCreateInfo pli = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pc
    };
    if (!check_vk(vkCreatePipelineLayout(a->device, &pli, NULL, &a->particle_layout), "vkCreatePipelineLayout(particles)")) {
        return 0;
    }

    VkShaderModuleCreateInfo vs_ci = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = v_type_particle_vert_spv_len,
        .pCode = (const uint32_t*)v_type_particle_vert_spv
    };
    VkShaderModuleCreateInfo fs_ci = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = v_type_particle_frag_spv_len,
        .pCode = (const uint32_t*)v_type_particle_frag_spv
    };
    VkShaderModuleCreateInfo fs_bloom_ci = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = v_type_particle_bloom_frag_spv_len,
        .pCode = (const uint32_t*)v_type_particle_bloom_frag_spv
    };
    VkShaderModule vs = VK_NULL_HANDLE;
    VkShaderModule fs = VK_NULL_HANDLE;
    VkShaderModule fs_bloom = VK_NULL_HANDLE;
    if (!check_vk(vkCreateShaderModule(a->device, &vs_ci, NULL, &vs), "vkCreateShaderModule(particle vs)")) {
        return 0;
    }
    if (!check_vk(vkCreateShaderModule(a->device, &fs_ci, NULL, &fs), "vkCreateShaderModule(particle fs)")) {
        vkDestroyShaderModule(a->device, vs, NULL);
        return 0;
    }
    if (!check_vk(vkCreateShaderModule(a->device, &fs_bloom_ci, NULL, &fs_bloom), "vkCreateShaderModule(particle bloom fs)")) {
        vkDestroyShaderModule(a->device, fs, NULL);
        vkDestroyShaderModule(a->device, vs, NULL);
        return 0;
    }
    VkPipelineShaderStageCreateInfo stages[2] = {
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vs, .pName = "main"},
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = fs, .pName = "main"}
    };

    VkVertexInputBindingDescription binding = {
        .binding = 0,
        .stride = sizeof(particle_instance),
        .inputRate = VK_VERTEX_INPUT_RATE_INSTANCE
    };
    VkVertexInputAttributeDescription attr[2];
    memset(attr, 0, sizeof(attr));
    attr[0].location = 0;
    attr[0].binding = 0;
    attr[0].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attr[0].offset = 0;
    attr[1].location = 1;
    attr[1].binding = 0;
    attr[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attr[1].offset = sizeof(float) * 4;
    VkVertexInputAttributeDescription attr3[3];
    memset(attr3, 0, sizeof(attr3));
    attr3[0] = attr[0];
    attr3[1] = attr[1];
    attr3[2].location = 2;
    attr3[2].binding = 0;
    attr3[2].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attr3[2].offset = sizeof(float) * 8;
    VkPipelineVertexInputStateCreateInfo vi = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &binding,
        .vertexAttributeDescriptionCount = 3,
        .pVertexAttributeDescriptions = attr3
    };
    VkPipelineInputAssemblyStateCreateInfo ia = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP
    };
    VkPipelineViewportStateCreateInfo vp = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, .viewportCount = 1, .scissorCount = 1};
    VkPipelineRasterizationStateCreateInfo rs = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .lineWidth = 2.2f,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE
    };
    VkPipelineMultisampleStateCreateInfo ms = {.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, .rasterizationSamples = scene_samples(a)};
    VkPipelineColorBlendAttachmentState cb_att = {
        .blendEnable = VK_TRUE,
        /* Alpha-scaled additive: preserves glow while making p->a/lifetime meaningful. */
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
    };
    VkPipelineColorBlendStateCreateInfo cb = {.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, .attachmentCount = 1, .pAttachments = &cb_att};
    VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo ds = {.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, .dynamicStateCount = 2, .pDynamicStates = dyn};
    VkPipelineDepthStencilStateCreateInfo depth = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_FALSE,
        .depthWriteEnable = VK_FALSE,
        .depthCompareOp = VK_COMPARE_OP_ALWAYS
    };
    VkGraphicsPipelineCreateInfo gp = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2,
        .pStages = stages,
        .pVertexInputState = &vi,
        .pInputAssemblyState = &ia,
        .pViewportState = &vp,
        .pRasterizationState = &rs,
        .pMultisampleState = &ms,
        .pDepthStencilState = &depth,
        .pColorBlendState = &cb,
        .pDynamicState = &ds,
        .layout = a->particle_layout,
        .renderPass = a->scene_render_pass,
        .subpass = 0
    };
    if (!check_vk(vkCreateGraphicsPipelines(a->device, VK_NULL_HANDLE, 1, &gp, NULL, &a->particle_pipeline), "vkCreateGraphicsPipelines(particles)")) {
        vkDestroyShaderModule(a->device, fs_bloom, NULL);
        vkDestroyShaderModule(a->device, fs, NULL);
        vkDestroyShaderModule(a->device, vs, NULL);
        return 0;
    }

    stages[1].module = fs_bloom;
    cb_att.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    cb_att.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
    cb_att.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    cb_att.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    gp.renderPass = a->bloom_render_pass;
    if (!check_vk(vkCreateGraphicsPipelines(a->device, VK_NULL_HANDLE, 1, &gp, NULL, &a->particle_bloom_pipeline), "vkCreateGraphicsPipelines(particle bloom)")) {
        vkDestroyShaderModule(a->device, fs_bloom, NULL);
        vkDestroyShaderModule(a->device, fs, NULL);
        vkDestroyShaderModule(a->device, vs, NULL);
        return 0;
    }
    vkDestroyShaderModule(a->device, fs_bloom, NULL);
    vkDestroyShaderModule(a->device, fs, NULL);
    vkDestroyShaderModule(a->device, vs, NULL);
    return 1;
#endif
}

static void update_gpu_wormhole_vertices(app* a) {
    if (!a || !a->wormhole_line_vertex_map) {
        return;
    }
    wormhole_line_vertex* out = (wormhole_line_vertex*)a->wormhole_line_vertex_map;
    const size_t n = render_build_event_horizon_gpu_lines(&a->game, out, (size_t)WORMHOLE_GPU_MAX_VERTS);
    a->wormhole_line_vertex_count = (uint32_t)((n > (size_t)UINT32_MAX) ? (size_t)UINT32_MAX : n);
}

static void update_gpu_wormhole_tri_vertices(app* a) {
    if (!a || !a->wormhole_tri_vertex_map) {
        return;
    }
    wormhole_line_vertex* out = (wormhole_line_vertex*)a->wormhole_tri_vertex_map;
    const size_t n = render_build_event_horizon_gpu_tris(&a->game, out, (size_t)WORMHOLE_GPU_MAX_TRI_VERTS);
    a->wormhole_tri_vertex_count = (uint32_t)((n > (size_t)UINT32_MAX) ? (size_t)UINT32_MAX : n);
}

static int create_wormhole_resources(app* a) {
#if !V_TYPE_HAS_TERRAIN_SHADERS
    (void)a;
    return 1;
#else
    if (!a) {
        return 0;
    }
    const VkDeviceSize vbuf_size = (VkDeviceSize)WORMHOLE_GPU_MAX_VERTS * sizeof(wormhole_line_vertex);
    const VkDeviceSize tbuf_size = (VkDeviceSize)WORMHOLE_GPU_MAX_TRI_VERTS * sizeof(wormhole_line_vertex);
    if (!create_buffer(
            a, vbuf_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &a->wormhole_line_vertex_buffer, &a->wormhole_line_vertex_memory)) {
        return 0;
    }
    if (!check_vk(vkMapMemory(a->device, a->wormhole_line_vertex_memory, 0, vbuf_size, 0, &a->wormhole_line_vertex_map), "vkMapMemory(wormhole lines)")) {
        return 0;
    }
    if (!create_buffer(
            a, tbuf_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &a->wormhole_tri_vertex_buffer, &a->wormhole_tri_vertex_memory)) {
        return 0;
    }
    if (!check_vk(vkMapMemory(a->device, a->wormhole_tri_vertex_memory, 0, tbuf_size, 0, &a->wormhole_tri_vertex_map), "vkMapMemory(wormhole tris)")) {
        return 0;
    }
    a->wormhole_tri_vertex_count = 0u;
    a->wormhole_line_vertex_count = 0u;

    VkPushConstantRange pc = {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = sizeof(wormhole_line_pc)
    };
    VkPipelineLayoutCreateInfo pli = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pc
    };
    if (!check_vk(vkCreatePipelineLayout(a->device, &pli, NULL, &a->wormhole_line_layout), "vkCreatePipelineLayout(wormhole line)")) {
        return 0;
    }

    VkShaderModuleCreateInfo vs_ci = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = v_type_wormhole_line_vert_spv_len,
        .pCode = (const uint32_t*)v_type_wormhole_line_vert_spv
    };
    VkShaderModuleCreateInfo fs_ci = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = v_type_wormhole_line_frag_spv_len,
        .pCode = (const uint32_t*)v_type_wormhole_line_frag_spv
    };
    VkShaderModule vs = VK_NULL_HANDLE;
    VkShaderModule fs = VK_NULL_HANDLE;
    if (!check_vk(vkCreateShaderModule(a->device, &vs_ci, NULL, &vs), "vkCreateShaderModule(wormhole line vs)")) {
        return 0;
    }
    if (!check_vk(vkCreateShaderModule(a->device, &fs_ci, NULL, &fs), "vkCreateShaderModule(wormhole line fs)")) {
        vkDestroyShaderModule(a->device, vs, NULL);
        return 0;
    }
    VkPipelineShaderStageCreateInfo stages[2] = {
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vs, .pName = "main"},
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = fs, .pName = "main"}
    };
    VkVertexInputBindingDescription binding = {
        .binding = 0,
        .stride = sizeof(wormhole_line_vertex),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
    };
    VkVertexInputAttributeDescription attr[2];
    memset(attr, 0, sizeof(attr));
    attr[0].location = 0;
    attr[0].binding = 0;
    attr[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attr[0].offset = 0;
    attr[1].location = 1;
    attr[1].binding = 0;
    attr[1].format = VK_FORMAT_R32_SFLOAT;
    attr[1].offset = sizeof(float) * 3;
    VkPipelineVertexInputStateCreateInfo vi = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &binding,
        .vertexAttributeDescriptionCount = 2,
        .pVertexAttributeDescriptions = attr
    };
    VkPipelineInputAssemblyStateCreateInfo ia_line = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST
    };
    VkPipelineInputAssemblyStateCreateInfo ia_tri = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
    };
    VkPipelineViewportStateCreateInfo vp = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, .viewportCount = 1, .scissorCount = 1};
    const float dpi_scale = drawable_scale_y(a);
    VkPipelineRasterizationStateCreateInfo rs = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .lineWidth = 2.2f * dpi_scale,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE
    };
    VkPipelineMultisampleStateCreateInfo ms = {.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, .rasterizationSamples = scene_samples(a)};
    VkPipelineColorBlendAttachmentState cb_att = {
        .blendEnable = VK_TRUE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
    };
    VkPipelineColorBlendStateCreateInfo cb = {.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, .attachmentCount = 1, .pAttachments = &cb_att};
    VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo ds = {.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, .dynamicStateCount = 2, .pDynamicStates = dyn};
    VkPipelineDepthStencilStateCreateInfo depth = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_TRUE,
        .depthWriteEnable = VK_FALSE,
        .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL
    };
    VkGraphicsPipelineCreateInfo gp = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2,
        .pStages = stages,
        .pVertexInputState = &vi,
        .pInputAssemblyState = &ia_line,
        .pViewportState = &vp,
        .pRasterizationState = &rs,
        .pMultisampleState = &ms,
        .pDepthStencilState = &depth,
        .pColorBlendState = &cb,
        .pDynamicState = &ds,
        .layout = a->wormhole_line_layout,
        .renderPass = a->scene_render_pass,
        .subpass = 0
    };
    if (!check_vk(vkCreateGraphicsPipelines(a->device, VK_NULL_HANDLE, 1, &gp, NULL, &a->wormhole_line_pipeline), "vkCreateGraphicsPipelines(wormhole line)")) {
        vkDestroyShaderModule(a->device, fs, NULL);
        vkDestroyShaderModule(a->device, vs, NULL);
        return 0;
    }

    VkPipelineColorBlendAttachmentState cb_att_depth = {0};
    cb_att_depth.colorWriteMask = 0;
    VkPipelineColorBlendStateCreateInfo cb_depth = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &cb_att_depth
    };
    VkPipelineDepthStencilStateCreateInfo depth_only = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_TRUE,
        .depthWriteEnable = VK_TRUE,
        .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL
    };
    gp.pInputAssemblyState = &ia_tri;
    gp.pColorBlendState = &cb_depth;
    gp.pDepthStencilState = &depth_only;
    if (!check_vk(vkCreateGraphicsPipelines(a->device, VK_NULL_HANDLE, 1, &gp, NULL, &a->wormhole_depth_pipeline), "vkCreateGraphicsPipelines(wormhole depth)")) {
        vkDestroyShaderModule(a->device, fs, NULL);
        vkDestroyShaderModule(a->device, vs, NULL);
        return 0;
    }

    vkDestroyShaderModule(a->device, fs, NULL);
    vkDestroyShaderModule(a->device, vs, NULL);
    return 1;
#endif
}

static int create_vg_context(app* a) {
    vg_context_desc desc;
    memset(&desc, 0, sizeof(desc));
    desc.backend = VG_BACKEND_VULKAN;
    desc.api.vulkan.instance = (void*)a->instance;
    desc.api.vulkan.physical_device = (void*)a->physical_device;
    desc.api.vulkan.device = (void*)a->device;
    desc.api.vulkan.graphics_queue = (void*)a->graphics_queue;
    desc.api.vulkan.graphics_queue_family = a->graphics_queue_family;
    desc.api.vulkan.render_pass = (void*)a->scene_render_pass;
    desc.api.vulkan.vertex_binding = 0;
    desc.api.vulkan.max_frames_in_flight = 1;
    desc.api.vulkan.raster_samples = (uint32_t)scene_samples(a);
    desc.api.vulkan.has_stencil_attachment = format_has_stencil(a->scene_depth_format) ? 1u : 0u;
    vg_result vr = vg_context_create(&desc, &a->vg);
    if (vr != VG_OK) {
        fprintf(stderr, "vg_context_create failed: %s\n", vg_result_string(vr));
        return 0;
    }

    vg_crt_profile profile;
    vg_make_crt_profile(VG_CRT_PRESET_WOPR, &profile);
    profile.beam_core_width_px = 0.600001f;
    profile.beam_halo_width_px = 2.8f;
    profile.beam_intensity = 0.85f;
    profile.bloom_strength = 0.75f;
    profile.bloom_radius_px = 4.0f;
    profile.persistence_decay = 0.70f;
    profile.jitter_amount = 0.07f;
    profile.flicker_amount = 0.03f;
    profile.vignette_strength = 0.14f;
    profile.barrel_distortion = 0.02f;
    profile.scanline_strength = 0.12f;
    profile.noise_strength = 0.04f;
    vg_set_crt_profile(a->vg, &profile);
    return 1;
}

static void update_gpu_high_plains_vertices(app* a) {
    if (!a || !a->terrain_vertex_map) {
        return;
    }
    terrain_vertex* vtx = (terrain_vertex*)a->terrain_vertex_map;
    const float w = (float)a->swapchain_extent.width;
    const float h = (float)a->swapchain_extent.height;
    const float y_near = h * 0.04f;
    const float y_far = h * 0.34f;
    const float cam = a->game.camera_x * 1.10f;
    const float center_x = w * 0.50f;
    const float col_spacing = w * 0.050f;
    const float col_span = col_spacing * (float)(TERRAIN_COLS - 1);
    const int x0 = (int)floorf((cam - col_span * 0.5f) / col_spacing) - 2;
    const float y_quant_step = h * 0.010f;
    for (int r = 0; r < TERRAIN_ROWS; ++r) {
        const float z = (float)r / (float)(TERRAIN_ROWS - 1);
        const float p = powf(z, 0.82f);
        const float zw = lerpf(360.0f, 4200.0f, p);
        const float y_base = lerpf(y_near, y_far, p);
        const float row_scale = lerpf(1.04f, 0.23f, p);
        const float amp = lerpf(h * 0.21f, h * 0.08f, p);
        for (int c = 0; c < TERRAIN_COLS; ++c) {
            const float xw = (float)(x0 + c) * col_spacing;
            const float dx = xw - cam;
            const float x = center_x + dx * row_scale;
            const float n = high_plains_looped_noise(xw * 0.72f, zw * 0.0021f) * 1.95f;
            float y = y_base + n * amp;
            y = floorf(y / y_quant_step + 0.5f) * y_quant_step;
            const uint32_t idx = (uint32_t)r * TERRAIN_COLS + (uint32_t)c;
            vtx[idx].x = x;
            vtx[idx].y = y;
            vtx[idx].z = z;
        }
    }

    if (a->terrain_wire_vertex_map) {
        terrain_wire_vertex* wv = (terrain_wire_vertex*)a->terrain_wire_vertex_map;
        uint32_t wi = 0;
        for (int r = 0; r < TERRAIN_ROWS - 1; ++r) {
            for (int c = 0; c < TERRAIN_COLS - 1; ++c) {
                const uint32_t i00 = (uint32_t)r * TERRAIN_COLS + (uint32_t)c;
                const uint32_t i10 = (uint32_t)r * TERRAIN_COLS + (uint32_t)c + 1u;
                const uint32_t i01 = (uint32_t)(r + 1) * TERRAIN_COLS + (uint32_t)c;
                const uint32_t i11 = (uint32_t)(r + 1) * TERRAIN_COLS + (uint32_t)c + 1u;
                const terrain_vertex p00 = vtx[i00];
                const terrain_vertex p10 = vtx[i10];
                const terrain_vertex p01 = vtx[i01];
                const terrain_vertex p11 = vtx[i11];

                wv[wi++] = (terrain_wire_vertex){p00.x, p00.y, p00.z, 1.0f, 0.0f, 0.0f};
                wv[wi++] = (terrain_wire_vertex){p10.x, p10.y, p10.z, 0.0f, 1.0f, 0.0f};
                wv[wi++] = (terrain_wire_vertex){p01.x, p01.y, p01.z, 0.0f, 0.0f, 1.0f};

                wv[wi++] = (terrain_wire_vertex){p10.x, p10.y, p10.z, 1.0f, 0.0f, 0.0f};
                wv[wi++] = (terrain_wire_vertex){p11.x, p11.y, p11.z, 0.0f, 1.0f, 0.0f};
                wv[wi++] = (terrain_wire_vertex){p01.x, p01.y, p01.z, 0.0f, 0.0f, 1.0f};
            }
        }
    }
}

static int level_uses_cylinder_gpu(int level_style) {
    return level_style == LEVEL_STYLE_ENEMY_RADAR ||
           level_style == LEVEL_STYLE_EVENT_HORIZON ||
           level_style == LEVEL_STYLE_EVENT_HORIZON_LEGACY;
}

static float cylinder_period_gpu(const game_state* g) {
    return fmaxf(g->world_w * 2.4f, 1.0f);
}

static void project_cylinder_point_gpu(const game_state* g, float x, float y, float* out_x, float* out_y, float* out_depth01) {
    const float w = g->world_w;
    const float h = g->world_h;
    const float cx = w * 0.5f;
    const float cy = h * 0.50f;
    const float period = cylinder_period_gpu(g);
    const float theta = (x - g->camera_x) / period * 6.28318530718f;
    const float depth = cosf(theta) * 0.5f + 0.5f;
    const float radius = w * 0.485f;
    const float y_scale = 0.44f + depth * 0.62f;
    *out_x = cx + sinf(theta) * radius;
    *out_y = cy + (y - cy) * y_scale;
    if (out_depth01) {
        *out_depth01 = depth;
    }
}

static void update_gpu_particle_instances(app* a) {
    if (!a || !a->particle_instance_map) {
        return;
    }
    static float trace_last_t = -1000.0f;
    const int trace_enabled = a->particle_tuning_enabled;
    particle_instance* out = (particle_instance*)a->particle_instance_map;
    uint32_t n = 0;
    const game_state* g = &a->game;
    const int use_cyl = level_uses_cylinder_gpu(g->level_style);
    float r_sum = 0.0f;
    float r_min = 1e9f;
    float r_max = 0.0f;
    for (size_t i = 0; i < MAX_PARTICLES; ++i) {
        const particle* p = &g->particles[i];
        if (!p->active || p->a <= 0.01f || p->size <= 0.10f) {
            continue;
        }
        if (n >= GPU_PARTICLE_MAX_INSTANCES) {
            break;
        }
        float sx = p->b.x;
        float sy = p->b.y;
        float depth = 1.0f;
        float radius = p->size;
        if (use_cyl) {
            project_cylinder_point_gpu(g, p->b.x, p->b.y, &sx, &sy, &depth);
            radius *= (0.35f + 0.9f * depth);
        } else {
            /* Match vg foreground world->screen transform:
               translate(world by -camera, then center in viewport). */
            sx = p->b.x + g->world_w * 0.5f - g->camera_x;
            sy = p->b.y + g->world_h * 0.5f - g->camera_y;
        }
        if (sx < -24.0f || sx > g->world_w + 24.0f || sy < -24.0f || sy > g->world_h + 24.0f) {
            continue;
        }
        if (radius <= 0.10f) {
            continue;
        }
        if (radius < r_min) r_min = radius;
        if (radius > r_max) r_max = radius;
        r_sum += radius;
        out[n].x = sx;
        out[n].y = sy;
        out[n].radius_px = radius;
        if (p->type == PARTICLE_POINT) {
            out[n].kind = 0.0f;
        } else if (p->type == PARTICLE_FLASH) {
            out[n].kind = 2.0f;
        } else {
            out[n].kind = 1.0f;
        }
        {
            float emission_boost = 1.0f;
            /* Explosion particles live longer than thruster particles;
               give them a short spawn-time brightness kick. */
            if (p->life_s > 0.30f) {
                const float life_t = clampf(p->age_s / fmaxf(p->life_s, 1e-5f), 0.0f, 1.0f);
                const float spawn_t = 1.0f - life_t;
                emission_boost += 0.55f * spawn_t * spawn_t;
            }
            out[n].r = clampf(p->r * emission_boost, 0.0f, 1.0f);
            out[n].g = clampf(p->g * emission_boost, 0.0f, 1.0f);
            out[n].b = clampf(p->bcol * emission_boost, 0.0f, 1.0f);
            out[n].a = p->a;
        }
        {
            const float spd = sqrtf(p->b.vx * p->b.vx + p->b.vy * p->b.vy);
            float dx = 1.0f;
            float dy = 0.0f;
            if (spd > 1e-3f) {
                dx = p->b.vx / spd;
                dy = p->b.vy / spd;
            }
            float life_t = clampf(p->age_s / fmaxf(p->life_s, 1e-5f), 0.0f, 1.0f);
            float trail = 0.0f;
            float heat = 0.0f;
            if (p->type == PARTICLE_FLASH) {
                heat = 2.0f;
            } else if (p->life_s > 0.30f) {
                /* Explosion sparks: hot at spawn, with short phosphor streak. */
                const float speed01 = clampf(spd / 520.0f, 0.0f, 1.0f);
                trail = speed01 * (1.0f - life_t) * 0.95f * a->particle_trail_gain;
                heat = powf(1.0f - life_t, a->particle_heat_cooling);
            }
            out[n].dir_x = dx;
            out[n].dir_y = dy;
            out[n].trail = trail;
            out[n].heat = heat;
        }
        ++n;
    }
    a->particle_instance_count = n;
    if (trace_enabled && n > 0 && (g->t - trace_last_t) >= 0.5f) {
        const float r_avg = r_sum / (float)n;
        fprintf(
            stderr,
            "[particles] gpu=1 lvl=%d n=%u radius_px[min=%.2f avg=%.2f max=%.2f] active=%d\n",
            g->level_style, n, r_min, r_avg, r_max, g->active_particles
        );
        trace_last_t = g->t;
    }
}

static void record_gpu_particles(app* a, VkCommandBuffer cmd) {
#if !V_TYPE_HAS_TERRAIN_SHADERS
    (void)a;
    (void)cmd;
#else
    if (!a || !cmd || !a->particle_pipeline || !a->particle_instance_buffer) {
        return;
    }
    update_gpu_particle_instances(a);
    if (a->particle_instance_count == 0) {
        return;
    }
    set_viewport_scissor(cmd, a->swapchain_extent.width, a->swapchain_extent.height);
    particle_pc pc;
    memset(&pc, 0, sizeof(pc));
    pc.params[0] = (float)a->swapchain_extent.width;
    pc.params[1] = (float)a->swapchain_extent.height;
    pc.params[2] = a->particle_core_gain;
    pc.params[3] = a->particle_trail_gain;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, a->particle_pipeline);
    vkCmdPushConstants(cmd, a->particle_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &a->particle_instance_buffer, &off);
    vkCmdDraw(cmd, 4, a->particle_instance_count, 0, 0);
#endif
}

static void record_gpu_particles_bloom(app* a, VkCommandBuffer cmd) {
#if !V_TYPE_HAS_TERRAIN_SHADERS
    (void)a;
    (void)cmd;
#else
    if (!a || !cmd || !a->particle_bloom_pipeline || !a->particle_instance_buffer) {
        return;
    }
    update_gpu_particle_instances(a);
    if (a->particle_instance_count == 0) {
        return;
    }
    set_viewport_scissor(cmd, a->swapchain_extent.width, a->swapchain_extent.height);
    particle_pc pc;
    memset(&pc, 0, sizeof(pc));
    pc.params[0] = (float)a->swapchain_extent.width;
    pc.params[1] = (float)a->swapchain_extent.height;
    pc.params[2] = a->particle_core_gain;
    pc.params[3] = a->particle_trail_gain;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, a->particle_bloom_pipeline);
    vkCmdPushConstants(cmd, a->particle_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &a->particle_instance_buffer, &off);
    vkCmdDraw(cmd, 4, a->particle_instance_count, 0, 0);
#endif
}

static void record_gpu_wormhole(app* a, VkCommandBuffer cmd) {
#if !V_TYPE_HAS_TERRAIN_SHADERS
    (void)a;
    (void)cmd;
#else
    if (!a || !cmd || !a->use_gpu_wormhole ||
        a->game.level_style != LEVEL_STYLE_EVENT_HORIZON ||
        !a->wormhole_line_pipeline || !a->wormhole_depth_pipeline ||
        !a->wormhole_line_vertex_buffer || !a->wormhole_tri_vertex_buffer) {
        return;
    }
    update_gpu_wormhole_vertices(a);
    update_gpu_wormhole_tri_vertices(a);
    if (a->wormhole_line_vertex_count < 2u) {
        return;
    }
    if (a->wormhole_tri_vertex_count < 3u) {
        return;
    }
    set_viewport_scissor(cmd, a->swapchain_extent.width, a->swapchain_extent.height);
    VkDeviceSize off = 0;

    wormhole_line_pc pc;
    memset(&pc, 0, sizeof(pc));
    pc.params[0] = (float)a->swapchain_extent.width;
    pc.params[1] = (float)a->swapchain_extent.height;
    float primary[3];
    float primary_dim[3];
    if (a->palette_mode == 1) {
        primary[0] = 1.00f; primary[1] = 0.68f; primary[2] = 0.24f;
        primary_dim[0] = 0.85f; primary_dim[1] = 0.52f; primary_dim[2] = 0.16f;
    } else if (a->palette_mode == 2) {
        primary[0] = 0.40f; primary[1] = 0.95f; primary[2] = 1.00f;
        primary_dim[0] = 0.26f; primary_dim[1] = 0.72f; primary_dim[2] = 0.92f;
    } else {
        primary[0] = 0.08f; primary[1] = 0.66f; primary[2] = 0.18f;
        primary_dim[0] = 0.03f; primary_dim[1] = 0.52f; primary_dim[2] = 0.12f;
    }

    const float dpi_scale = drawable_scale_y(a);
    const float px = 0.55f * dpi_scale;
    static const float taps[5][2] = {
        {0.0f, 0.0f},
        {1.0f, 0.0f},
        {-1.0f, 0.0f},
        {0.0f, 1.0f},
        {0.0f, -1.0f}
    };

    /* Depth prepass for hidden-line removal. */
    VkDeviceSize toff = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &a->wormhole_tri_vertex_buffer, &toff);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, a->wormhole_depth_pipeline);
    pc.color[0] = 0.0f;
    pc.color[1] = 0.0f;
    pc.color[2] = 0.0f;
    pc.color[3] = 0.0f;
    pc.params[2] = 1.0f;
    pc.offset[0] = 0.0f;
    pc.offset[1] = 0.0f;
    vkCmdPushConstants(cmd, a->wormhole_line_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
    vkCmdDraw(cmd, a->wormhole_tri_vertex_count, 1, 0, 0);

    vkCmdBindVertexBuffers(cmd, 0, 1, &a->wormhole_line_vertex_buffer, &off);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, a->wormhole_line_pipeline);
    pc.color[0] = primary_dim[0];
    pc.color[1] = primary_dim[1];
    pc.color[2] = primary_dim[2];
    pc.params[2] = 0.72f;
    pc.color[3] = 0.26f;
    for (int i = 0; i < 5; ++i) {
        pc.offset[0] = taps[i][0] * px;
        pc.offset[1] = taps[i][1] * px;
        vkCmdPushConstants(cmd, a->wormhole_line_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
        vkCmdDraw(cmd, a->wormhole_line_vertex_count, 1, 0, 0);
    }

    pc.color[0] = primary[0];
    pc.color[1] = primary[1];
    pc.color[2] = primary[2];
    pc.params[2] = 0.90f;
    pc.color[3] = 0.74f;
    pc.offset[0] = 0.0f;
    pc.offset[1] = 0.0f;
    vkCmdPushConstants(cmd, a->wormhole_line_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
    vkCmdDraw(cmd, a->wormhole_line_vertex_count, 1, 0, 0);
#endif
}

static void record_gpu_high_plains_terrain(app* a, VkCommandBuffer cmd) {
    const int enable_gpu_terrain = 1;
    if (!enable_gpu_terrain) {
        return;
    }
    if (!a || !cmd ||
        (a->game.level_style != LEVEL_STYLE_HIGH_PLAINS_DRIFTER_2 &&
         a->game.level_style != LEVEL_STYLE_HIGH_PLAINS_DRIFTER)) {
        return;
    }
    if (!a->terrain_fill_pipeline || !a->terrain_line_pipeline || !a->terrain_vertex_buffer) {
        return;
    }
    update_gpu_high_plains_vertices(a);
    set_viewport_scissor(cmd, a->swapchain_extent.width, a->swapchain_extent.height);
    VkDeviceSize vb_off = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &a->terrain_vertex_buffer, &vb_off);

    terrain_pc pc;
    memset(&pc, 0, sizeof(pc));
    if (a->palette_mode == 1) {
        pc.color[0] = 1.0f; pc.color[1] = 0.73f; pc.color[2] = 0.34f; pc.color[3] = 1.0f;
    } else if (a->palette_mode == 2) {
        pc.color[0] = 0.60f; pc.color[1] = 0.88f; pc.color[2] = 1.0f; pc.color[3] = 1.0f;
    } else {
        pc.color[0] = 0.20f; pc.color[1] = 0.90f; pc.color[2] = 0.34f; pc.color[3] = 1.0f;
    }
    pc.params[0] = (float)a->swapchain_extent.width;
    pc.params[1] = (float)a->swapchain_extent.height;
    pc.params[2] = 1.0f;
    pc.params[3] = a->terrain_tuning.hue_shift;
    pc.tune[0] = a->terrain_tuning.brightness;
    pc.tune[1] = a->terrain_tuning.opacity;
    pc.tune[2] = a->terrain_tuning.normal_variation;
    pc.tune[3] = a->terrain_tuning.depth_fade;

    const int draw_fill = (a->game.level_style == LEVEL_STYLE_HIGH_PLAINS_DRIFTER_2);
    if (draw_fill) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, a->terrain_fill_pipeline);
        vkCmdPushConstants(cmd, a->terrain_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
        vkCmdBindIndexBuffer(cmd, a->terrain_tri_index_buffer, 0, VK_INDEX_TYPE_UINT16);
        vkCmdDrawIndexed(cmd, a->terrain_tri_index_count, 1, 0, 0, 0);
    }

    const int draw_wire = (a->game.level_style == LEVEL_STYLE_HIGH_PLAINS_DRIFTER) ? 1 : a->terrain_wire_enabled;
    if (draw_wire) {
        const float wire_boost = 1.28f;
        pc.color[0] = clampf(pc.color[0] * wire_boost, 0.0f, 1.0f);
        pc.color[1] = clampf(pc.color[1] * wire_boost, 0.0f, 1.0f);
        pc.color[2] = clampf(pc.color[2] * wire_boost, 0.0f, 1.0f);
        pc.color[3] = 0.82f;
        pc.params[2] = 0.96f;
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, a->terrain_line_pipeline);
        vkCmdPushConstants(cmd, a->terrain_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);
        if (a->terrain_wire_vertex_buffer && a->terrain_wire_vertex_count > 0) {
            VkDeviceSize wb_off = 0;
            vkCmdBindVertexBuffers(cmd, 0, 1, &a->terrain_wire_vertex_buffer, &wb_off);
            vkCmdDraw(cmd, a->terrain_wire_vertex_count, 1, 0, 0);
            vkCmdBindVertexBuffers(cmd, 0, 1, &a->terrain_vertex_buffer, &vb_off);
        }
    }
}

static int record_submit_present(app* a, uint32_t image_index, float t, float dt, float fps) {
    VkCommandBuffer cmd = a->command_buffers[image_index];
    if (!check_vk(vkResetCommandBuffer(cmd, 0), "vkResetCommandBuffer")) return 0;
    VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    if (!check_vk(vkBeginCommandBuffer(cmd, &begin), "vkBeginCommandBuffer")) return 0;

    VkClearValue scene_clear[3];
    memset(scene_clear, 0, sizeof(scene_clear));
    const uint32_t scene_clear_count = (scene_samples(a) == VK_SAMPLE_COUNT_1_BIT) ? 2u : 3u;
    /* Attachment order:
       - no MSAA:  [0]=color, [1]=depth
       - with MSAA:[0]=color_msaa, [1]=resolve_color, [2]=depth
    */
    scene_clear[0].color.float32[3] = 1.0f;
    if (scene_samples(a) == VK_SAMPLE_COUNT_1_BIT) {
        scene_clear[1].depthStencil.depth = 1.0f;
    } else {
        scene_clear[1].color.float32[3] = 1.0f;
        scene_clear[2].depthStencil.depth = 1.0f;
    }
    VkRenderPassBeginInfo scene_rp = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = a->scene_render_pass,
        .framebuffer = a->scene_fb,
        .renderArea = {.offset = {0, 0}, .extent = a->swapchain_extent},
        .clearValueCount = scene_clear_count,
        .pClearValues = scene_clear
    };
    vkCmdBeginRenderPass(cmd, &scene_rp, VK_SUBPASS_CONTENTS_INLINE);

    vg_frame_desc frame = {.width = a->swapchain_extent.width, .height = a->swapchain_extent.height, .delta_time_s = dt, .command_buffer = (void*)cmd};
    vg_result vr = vg_begin_frame(a->vg, &frame);
    if (vr != VG_OK) {
        fprintf(stderr, "VG failure: vg_begin_frame -> %s (%d)\n", vg_result_string(vr), (int)vr);
        return 0;
    }
    render_metrics metrics = {
        .fps = fps,
        .dt = dt,
        .show_fps = a->show_fps_counter,
        .ui_time_s = (float)SDL_GetTicks() * 0.001f,
        .force_clear = (a->force_clear_frames > 0) ? 1 : 0,
        .show_crt_ui = a->show_crt_ui,
        .crt_ui_selected = a->crt_ui_selected,
        .teletype_text = a->wave_tty_visible,
        .planetarium_marquee_text = a->planetarium_marquee.text,
        .planetarium_marquee_offset_px = a->planetarium_marquee.offset_px,
        .show_acoustics = a->show_acoustics,
        .show_video_menu = a->show_video_menu,
        .show_planetarium = a->show_planetarium,
        .video_menu_selected = a->video_menu_selected,
        .video_menu_fullscreen = a->video_menu_fullscreen,
        .palette_mode = a->palette_mode,
        .acoustics_selected = a->acoustics_selected,
        .acoustics_page = a->acoustics_page,
        .acoustics_combat_selected = a->acoustics_combat_selected,
        .acoustics_fire_slot_selected = a->acoustics_fire_slot_selected,
        .acoustics_thr_slot_selected = a->acoustics_thr_slot_selected,
        .planetarium_system = app_planetarium_system(a),
        .planetarium_selected = a->planetarium_selected,
        .planetarium_system_count = app_planetarium_planet_count(a),
        .planetarium_systems_quelled = planetarium_quelled_count(a),
        .nick_rgba8 = a->nick_rgba8,
        .nick_w = a->nick_w,
        .nick_h = a->nick_h,
        .nick_stride = a->nick_stride,
        .surveillance_svg_asset = a->surveillance_svg_asset,
        .terrain_tuning_text = (a->particle_tuning_enabled &&
                                a->particle_tuning_show &&
                                !a->show_acoustics && !a->show_video_menu && !a->show_planetarium)
            ? a->particle_tuning_text
            : ((a->terrain_tuning_enabled &&
                a->terrain_tuning_show &&
                a->game.level_style == LEVEL_STYLE_HIGH_PLAINS_DRIFTER_2)
                ? a->terrain_tuning_text : NULL),
        .use_gpu_particles = 0,
        .use_gpu_terrain = 0,
        .use_gpu_wormhole = 0,
        .scene_phase = 0
    };
    {
        int mx = 0;
        int my = 0;
        (void)SDL_GetMouseState(&mx, &my);
        const uint32_t wf = SDL_GetWindowFlags(a->window);
        metrics.mouse_in_window = ((wf & SDL_WINDOW_MOUSE_FOCUS) != 0u) ? 1 : 0;
        map_mouse_to_scene_coords(a, mx, my, &metrics.mouse_x, &metrics.mouse_y);
    }
    for (int i = 0; i < ACOUSTICS_SLOT_COUNT; ++i) {
        if (a->acoustics_page == ACOUSTICS_PAGE_COMBAT) {
            metrics.acoustics_fire_slot_defined[i] = a->acoustics_enemy_slot_defined[i] ? 1 : 0;
            metrics.acoustics_thr_slot_defined[i] = a->acoustics_exp_slot_defined[i] ? 1 : 0;
        } else {
            metrics.acoustics_fire_slot_defined[i] = a->acoustics_fire_slot_defined[i] ? 1 : 0;
            metrics.acoustics_thr_slot_defined[i] = a->acoustics_thr_slot_defined[i] ? 1 : 0;
        }
    }
    for (int i = 0; i < VIDEO_MENU_RES_COUNT; ++i) {
        metrics.video_res_w[i] = k_video_resolutions[i].w;
        metrics.video_res_h[i] = k_video_resolutions[i].h;
    }
    for (int i = 0; i < PLANETARIUM_MAX_SYSTEMS; ++i) {
        metrics.planetarium_nodes_quelled[i] = app_planetarium_node_quelled(a, i);
    }
    for (int i = 0; i < VIDEO_MENU_DIAL_COUNT; ++i) {
        metrics.video_dial_01[i] = a->video_dial_01[i];
    }
    if (a->acoustics_page == ACOUSTICS_PAGE_COMBAT) {
        metrics.acoustics_fire_slot_selected = a->acoustics_enemy_slot_selected;
        metrics.acoustics_thr_slot_selected = a->acoustics_exp_slot_selected;
    } else {
        metrics.acoustics_fire_slot_selected = a->acoustics_fire_slot_selected;
        metrics.acoustics_thr_slot_selected = a->acoustics_thr_slot_selected;
    }
    for (int i = 0; i < ACOUSTICS_SLIDER_COUNT; ++i) {
        metrics.acoustics_value_01[i] = a->acoustics_value_01[i];
        metrics.acoustics_display[i] = acoustics_value_to_ui_display(i, a->acoustics_value_01[i]);
    }
    for (int i = 0; i < ACOUSTICS_COMBAT_SLIDER_COUNT; ++i) {
        metrics.acoustics_combat_value_01[i] = a->acoustics_combat_value_01[i];
        metrics.acoustics_combat_display[i] = acoustics_combat_value_to_ui_display(i, a->acoustics_combat_value_01[i]);
    }
    for (int i = 0; i < ACOUSTICS_SCOPE_SAMPLES; ++i) {
        metrics.acoustics_scope[i] = a->scope_window[i];
    }
    {
        const int split_scene =
            !a->show_acoustics &&
            !a->show_video_menu &&
            !a->show_planetarium;
        const int use_gpu_terrain =
            (a->game.level_style == LEVEL_STYLE_HIGH_PLAINS_DRIFTER_2 ||
             a->game.level_style == LEVEL_STYLE_HIGH_PLAINS_DRIFTER);
        const int use_gpu_wormhole =
            a->use_gpu_wormhole &&
            (a->game.level_style == LEVEL_STYLE_EVENT_HORIZON);
        if (split_scene) {
            metrics.use_gpu_particles = 1;
            metrics.use_gpu_terrain = use_gpu_terrain ? 1 : 0;
            metrics.use_gpu_wormhole = use_gpu_wormhole ? 1 : 0;

            metrics.scene_phase = 1; /* background-only */
            vr = render_frame(a->vg, &a->game, &metrics);
            if (vr != VG_OK) {
                fprintf(stderr, "VG failure: render_frame(background) -> %s (%d)\n", vg_result_string(vr), (int)vr);
                return 0;
            }
            if (use_gpu_terrain) {
                record_gpu_high_plains_terrain(a, cmd);
                clear_scene_depth(cmd, a->swapchain_extent);
            }
            if (use_gpu_wormhole) {
                record_gpu_wormhole(a, cmd);
            }
            record_gpu_particles(a, cmd);

            metrics.scene_phase = 2; /* foreground-only */
            vr = render_frame(a->vg, &a->game, &metrics);
            if (vr != VG_OK) {
                fprintf(stderr, "VG failure: render_frame(foreground) -> %s (%d)\n", vg_result_string(vr), (int)vr);
                return 0;
            }
        } else {
            metrics.scene_phase = 0;
            vr = render_frame(a->vg, &a->game, &metrics);
            if (vr != VG_OK) {
                fprintf(stderr, "VG failure: render_frame -> %s (%d)\n", vg_result_string(vr), (int)vr);
                return 0;
            }
        }
    }
    if (a->force_clear_frames > 0) {
        a->force_clear_frames--;
    }
    vr = vg_end_frame(a->vg);
    if (vr != VG_OK) {
        fprintf(stderr, "VG failure: vg_end_frame -> %s (%d)\n", vg_result_string(vr), (int)vr);
        return 0;
    }
    vkCmdEndRenderPass(cmd);

    VkClearValue bloom_clear = {.color = {{0.0f, 0.0f, 0.0f, 1.0f}}};
    VkRenderPassBeginInfo bloom_rp = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = a->bloom_render_pass,
        .framebuffer = a->bloom_fb,
        .renderArea = {.offset = {0, 0}, .extent = a->swapchain_extent},
        .clearValueCount = 1,
        .pClearValues = &bloom_clear
    };
    vkCmdBeginRenderPass(cmd, &bloom_rp, VK_SUBPASS_CONTENTS_INLINE);
    set_viewport_scissor(cmd, a->swapchain_extent.width, a->swapchain_extent.height);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, a->bloom_pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, a->post_layout, 0, 1, &a->post_desc_set, 0, NULL);

    vg_crt_profile crt;
    vg_get_crt_profile(a->vg, &crt);
    post_pc pc = {0};
    pc.p0[0] = 1.0f / (float)a->swapchain_extent.width;
    pc.p0[1] = 1.0f / (float)a->swapchain_extent.height;
    pc.p0[2] = crt.bloom_strength;
    pc.p0[3] = crt.bloom_radius_px;
    pc.p1[0] = crt.vignette_strength;
    pc.p1[1] = crt.barrel_distortion;
    pc.p1[2] = crt.scanline_strength;
    pc.p1[3] = crt.noise_strength;
    pc.p2[0] = t;
    pc.p2[1] = a->show_crt_ui ? 1.0f : 0.0f;
    pc.p2[2] = 24.0f / (float)a->swapchain_extent.width;
    pc.p2[3] = 0.12f;
    pc.p3[0] = 0.44f;
    pc.p3[1] = 0.76f;
    vkCmdPushConstants(cmd, a->post_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    if (a->particle_bloom_enabled &&
        !a->show_acoustics && !a->show_video_menu && !a->show_planetarium) {
        record_gpu_particles_bloom(a, cmd);
    }
    vkCmdEndRenderPass(cmd);

    VkClearValue present_clear = {.color = {{0.0f, 0.0f, 0.0f, 1.0f}}};
    VkRenderPassBeginInfo present_rp = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = a->present_render_pass,
        .framebuffer = a->present_framebuffers[image_index],
        .renderArea = {.offset = {0, 0}, .extent = a->swapchain_extent},
        .clearValueCount = 1,
        .pClearValues = &present_clear
    };
    vkCmdBeginRenderPass(cmd, &present_rp, VK_SUBPASS_CONTENTS_INLINE);
    set_viewport_scissor(cmd, a->swapchain_extent.width, a->swapchain_extent.height);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, a->composite_pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, a->post_layout, 0, 1, &a->post_desc_set, 0, NULL);
    vkCmdPushConstants(cmd, a->post_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRenderPass(cmd);

    if (!check_vk(vkEndCommandBuffer(cmd), "vkEndCommandBuffer")) return 0;
    if (!check_vk(vkResetFences(a->device, 1, &a->in_flight), "vkResetFences")) return 0;

    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &a->image_available,
        .pWaitDstStageMask = &wait_stage,
        .commandBufferCount = 1,
        .pCommandBuffers = &cmd,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &a->render_finished
    };
    if (!check_vk(vkQueueSubmit(a->graphics_queue, 1, &submit, a->in_flight), "vkQueueSubmit")) return 0;

    VkPresentInfoKHR present = {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &a->render_finished,
        .swapchainCount = 1,
        .pSwapchains = &a->swapchain,
        .pImageIndices = &image_index
    };
    VkResult pr = vkQueuePresentKHR(a->present_queue, &present);
    if (pr == VK_ERROR_OUT_OF_DATE_KHR || pr == VK_SUBOPTIMAL_KHR) {
        a->swapchain_needs_recreate = 1;
        return 0;
    }
    return check_vk(pr, "vkQueuePresentKHR");
}

int main(void) {
    app a;
    memset(&a, 0, sizeof(a));
    a.force_clear_frames = 2;
    a.crt_ui_selected = 0;
    a.crt_ui_mouse_drag = 0;
    a.show_fps_counter = 0;
    a.show_acoustics = 0;
	    a.show_video_menu = 0;
    a.show_planetarium = 0;
    a.terrain_wire_enabled = 1;
    a.terrain_tuning_enabled = env_flag_enabled("VTYPE_TERRAIN_TUNING");
    a.terrain_tuning_show = 1;
    a.particle_tuning_enabled = env_flag_enabled("VTYPE_PARTICLE_TRACE");
    a.particle_tuning_show = 1;
    a.particle_bloom_enabled = 1;
    a.use_gpu_wormhole = 1;
    reset_terrain_tuning(&a);
    sync_terrain_tuning_text(&a);
    reset_particle_tuning(&a);
    sync_particle_tuning_text(&a);
    a.video_menu_selected = 1;
    a.video_menu_fullscreen = 0;
    a.palette_mode = 0;
    a.msaa_enabled = 1;
    a.video_dial_01[0] = 0.472223f; /* bloom strength */
    a.video_dial_01[1] = 0.475000f; /* bloom radius */
    a.video_dial_01[2] = 0.369918f; /* persistence */
    a.video_dial_01[3] = 0.000000f; /* jitter */
    a.video_dial_01[4] = 0.000000f; /* flicker */
    a.video_dial_01[5] = 0.348039f; /* beam core */
    a.video_dial_01[6] = 0.185656f; /* beam halo */
    a.video_dial_01[7] = 0.303458f; /* beam intensity */
    a.video_dial_01[8] = 0.000000f; /* scanline */
    a.video_dial_01[9] = 0.000000f; /* noise */
    a.video_dial_01[10] = 0.191667f; /* vignette */
    a.video_dial_01[11] = 0.100000f; /* barrel */
    a.video_menu_dial_drag = -1;
    a.video_menu_dial_drag_start_y = 0.0f;
    a.video_menu_dial_drag_start_value = 0.0f;
    a.acoustics_selected = 0;
    a.acoustics_page = ACOUSTICS_PAGE_SYNTH;
    a.acoustics_combat_selected = 0;
    a.acoustics_mouse_drag = 0;
    a.wave_tty_text[0] = '\0';
    a.wave_tty_visible[0] = '\0';
    a.current_system_index = 0;
    a.planetarium_selected = 0;
    memset(a.planetarium_nodes_quelled, 0, sizeof(a.planetarium_nodes_quelled));
    acoustics_defaults(&a);
    acoustics_combat_defaults(&a);
    acoustics_slot_defaults(&a);

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    srand((unsigned int)SDL_GetTicks());
    init_teletype_audio(&a);
    (void)load_acoustics_slots(&a, ACOUSTICS_SLOTS_PATH);
    (void)load_settings(&a);
    apply_acoustics(&a);

    int start_w = APP_WIDTH;
    int start_h = APP_HEIGHT;
    if (a.video_menu_selected > 0 && a.video_menu_selected <= VIDEO_MENU_RES_COUNT) {
        start_w = k_video_resolutions[a.video_menu_selected - 1].w;
        start_h = k_video_resolutions[a.video_menu_selected - 1].h;
    }
    Uint32 win_flags = SDL_WINDOW_VULKAN | SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI;
    if (a.video_menu_fullscreen) {
        win_flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    }
    a.window = SDL_CreateWindow("v-type (vulkan + post)", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, start_w, start_h, win_flags);
    if (!a.window) {
        cleanup(&a);
        return 1;
    }
    SDL_ShowCursor(SDL_DISABLE);

    if (!create_instance(&a) ||
        !create_surface(&a) ||
        !pick_physical_device(&a) ||
        !create_device(&a) ||
        !create_swapchain(&a) ||
        !create_render_passes(&a) ||
        !create_offscreen_targets(&a) ||
        !create_present_framebuffers(&a) ||
        !create_commands(&a) ||
        !create_sync(&a) ||
        !create_post_resources(&a) ||
        !create_terrain_resources(&a) ||
        !create_particle_resources(&a) ||
        !create_wormhole_resources(&a) ||
        !create_vg_context(&a)) {
        cleanup(&a);
        return 1;
    }

    init_planetarium_assets(&a);
    if (!planetarium_validate_registry(stderr)) {
        fprintf(stderr, "planetarium validation failed; continuing with best-effort defaults\n");
    }

    game_init(&a.game, (float)a.swapchain_extent.width, (float)a.swapchain_extent.height);
    apply_video_lab_controls(&a);
    vg_text_fx_typewriter_set_rate(&a.wave_tty, 0.038f);
    vg_text_fx_typewriter_set_beep(&a.wave_tty, teletype_beep_cb, &a);
    vg_text_fx_typewriter_set_beep_profile(&a.wave_tty, 900.0f, 55.0f, 0.028f, 0.14f);
    vg_text_fx_typewriter_enable_beep(&a.wave_tty, 1);
    set_tty_message(&a, "TACTICAL UPLINK READY");
    sync_planetarium_marquee(&a);
    vg_text_fx_marquee_set_speed(&a.planetarium_marquee, 70.0f);
    vg_text_fx_marquee_set_gap(&a.planetarium_marquee, 48.0f);

    uint64_t last = SDL_GetPerformanceCounter();
    float freq = (float)SDL_GetPerformanceFrequency();
    float fps_smoothed = 60.0f;
    int running = 1;

    while (running) {
        int restart_pressed = 0;
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) {
                running = 0;
            } else if (ev.type == SDL_KEYDOWN && !ev.key.repeat) {
                if (ev.key.keysym.sym == SDLK_ESCAPE && a.show_video_menu) {
                    a.show_video_menu = 0;
                } else if (ev.key.keysym.sym == SDLK_ESCAPE && a.show_planetarium) {
                    a.show_planetarium = 0;
                } else if (ev.key.keysym.sym == SDLK_ESCAPE) {
                    running = 0;
                } else if (ev.key.keysym.sym == SDLK_n) {
                    game_cycle_level(&a.game);
                    a.force_clear_frames = 2;
                    if (a.game.level_style == LEVEL_STYLE_ENEMY_RADAR) {
                        set_tty_message(&a, "level mode: cylinder run");
                    } else if (a.game.level_style == LEVEL_STYLE_EVENT_HORIZON) {
                        set_tty_message(&a, "level mode: event horizon");
                    } else if (a.game.level_style == LEVEL_STYLE_EVENT_HORIZON_LEGACY) {
                        set_tty_message(&a, "level mode: event horizon legacy");
                    } else if (a.game.level_style == LEVEL_STYLE_HIGH_PLAINS_DRIFTER) {
                        set_tty_message(&a, "level mode: high plains drifter");
                    } else if (a.game.level_style == LEVEL_STYLE_HIGH_PLAINS_DRIFTER_2) {
                        set_tty_message(&a, "level mode: high plains drifter 2");
                    } else {
                        set_tty_message(&a, "level mode: defender");
                    }
                } else if (ev.key.keysym.sym == SDLK_2) {
                    a.show_video_menu = !a.show_video_menu;
                    if (a.show_video_menu) {
                        a.show_acoustics = 0;
                        a.show_planetarium = 0;
                        a.show_crt_ui = 0;
                        a.video_menu_dial_drag = -1;
                        sync_video_dials_from_live_crt(&a);
                    }
                } else if (ev.key.keysym.sym == SDLK_3) {
                    a.show_planetarium = !a.show_planetarium;
                    if (a.show_planetarium) {
                        a.show_video_menu = 0;
                        a.show_acoustics = 0;
                        a.show_crt_ui = 0;
                        a.video_menu_dial_drag = -1;
                        sync_planetarium_marquee(&a);
                        announce_planetarium_selection(&a);
                    }
                } else if (a.show_video_menu && ev.key.keysym.sym == SDLK_a) {
                    a.msaa_enabled = !a.msaa_enabled;
                    if (a.msaa_enabled && a.msaa_samples == VK_SAMPLE_COUNT_1_BIT) {
                        a.msaa_enabled = 0;
                        set_tty_message(&a, "msaa unavailable");
                    } else {
                        set_tty_message(&a, a.msaa_enabled ? "msaa enabled" : "msaa disabled");
                        if (!recreate_render_runtime(&a)) {
                            fprintf(stderr, "msaa toggle recreate failed\n");
                            running = 0;
                        }
                    }
                } else if (a.show_video_menu && ev.key.keysym.sym == SDLK_UP) {
                    const int count = VIDEO_MENU_RES_COUNT + 1;
                    a.video_menu_selected = (a.video_menu_selected + count - 1) % count;
                } else if (a.show_video_menu && ev.key.keysym.sym == SDLK_DOWN) {
                    const int count = VIDEO_MENU_RES_COUNT + 1;
                    a.video_menu_selected = (a.video_menu_selected + 1) % count;
                } else if (a.show_video_menu && (ev.key.keysym.sym == SDLK_RETURN || ev.key.keysym.sym == SDLK_KP_ENTER || ev.key.keysym.sym == SDLK_SPACE)) {
                    if (apply_video_mode(&a)) {
                        set_tty_message(&a, "display mode applied");
                    } else {
                        set_tty_message(&a, "display mode apply failed");
                    }
                } else if (ev.key.keysym.sym == SDLK_1) {
                    a.show_acoustics = !a.show_acoustics;
                    if (a.show_acoustics) {
                        a.show_planetarium = 0;
                        a.show_video_menu = 0;
                    }
                } else if (ev.key.keysym.sym == SDLK_4) {
                    a.terrain_wire_enabled = !a.terrain_wire_enabled;
                    set_tty_message(&a, a.terrain_wire_enabled ? "terrain wire: on" : "terrain wire: off");
                } else if (ev.key.keysym.sym == SDLK_b) {
                    a.particle_bloom_enabled = !a.particle_bloom_enabled;
                    set_tty_message(&a, a.particle_bloom_enabled ? "particle bloom: on" : "particle bloom: off");
                } else if (a.show_planetarium && ev.key.keysym.sym == SDLK_LEFT) {
                    const int max_idx = app_planetarium_planet_count(&a);
                    a.planetarium_selected = (a.planetarium_selected + max_idx) % (max_idx + 1);
                    announce_planetarium_selection(&a);
                } else if (a.show_planetarium && ev.key.keysym.sym == SDLK_RIGHT) {
                    const int max_idx = app_planetarium_planet_count(&a);
                    a.planetarium_selected = (a.planetarium_selected + 1) % (max_idx + 1);
                    announce_planetarium_selection(&a);
                } else if (a.show_planetarium &&
                           (ev.key.keysym.sym == SDLK_RETURN || ev.key.keysym.sym == SDLK_KP_ENTER || ev.key.keysym.sym == SDLK_SPACE)) {
                    const planetary_system_def* sys = app_planetarium_system(&a);
                    const int boss_idx = app_planetarium_planet_count(&a);
                    const int quelled = planetarium_quelled_count(&a);
                    if (a.planetarium_selected < boss_idx) {
                        if (!app_planetarium_node_quelled(&a, a.planetarium_selected)) {
                            app_planetarium_set_node_quelled(&a, a.planetarium_selected, 1);
                            set_tty_message(&a, "contract accepted: system marked quelled");
                        } else {
                            set_tty_message(&a, "system already quelled");
                        }
                    } else if (quelled >= boss_idx) {
                        a.show_planetarium = 0;
                        if (sys && sys->boss_gate_ready_text && sys->boss_gate_ready_text[0] != '\0') {
                            set_tty_message(&a, sys->boss_gate_ready_text);
                        } else {
                            set_tty_message(&a, "boss contract armed: launching sortie");
                        }
                        if (a.current_system_index + 1 < planetarium_get_system_count()) {
                            a.current_system_index++;
                            a.planetarium_selected = 0;
                            sync_planetarium_marquee(&a);
                        }
                    } else {
                        if (sys && sys->boss_gate_locked_text && sys->boss_gate_locked_text[0] != '\0') {
                            set_tty_message(&a, sys->boss_gate_locked_text);
                        } else {
                            set_tty_message(&a, "boss locked: quell all systems first");
                        }
                    }
                } else if (a.show_acoustics && ev.key.keysym.sym == SDLK_s) {
                    if (a.acoustics_page == ACOUSTICS_PAGE_SYNTH) {
                        capture_current_to_selected_slots(&a);
                    }
                    if (save_acoustics_slots(&a, ACOUSTICS_SLOTS_PATH)) {
                        set_tty_message(&a, "acoustics slots saved");
                    } else {
                        set_tty_message(&a, "acoustics slots save failed");
                    }
                } else if (ev.key.keysym.sym == SDLK_f) {
                    if (a.show_acoustics) {
                        if (a.acoustics_page == ACOUSTICS_PAGE_COMBAT) {
                            trigger_enemy_fire_test(&a);
                        } else {
                            trigger_fire_test(&a);
                        }
                    } else {
                        a.show_fps_counter = !a.show_fps_counter;
                        set_tty_message(&a, a.show_fps_counter ? "fps counter: on" : "fps counter: off");
                    }
                } else if (a.show_acoustics && ev.key.keysym.sym == SDLK_g) {
                    if (a.acoustics_page == ACOUSTICS_PAGE_COMBAT) {
                        trigger_explosion_test(&a);
                    } else {
                        trigger_thruster_test(&a);
                    }
                } else if (a.show_acoustics && (ev.key.keysym.sym == SDLK_q || ev.key.keysym.sym == SDLK_e)) {
                    if (ev.key.keysym.sym == SDLK_q) {
                        a.acoustics_page = (a.acoustics_page + ACOUSTICS_PAGE_COUNT - 1) % ACOUSTICS_PAGE_COUNT;
                    } else {
                        a.acoustics_page = (a.acoustics_page + 1) % ACOUSTICS_PAGE_COUNT;
                    }
                } else if (ev.key.keysym.sym == SDLK_TAB) {
                    a.show_crt_ui = !a.show_crt_ui;
                } else if (ev.key.keysym.sym == SDLK_r) {
                    restart_pressed = 1;
                } else if (a.particle_tuning_enabled &&
                           !a.show_acoustics && !a.show_video_menu && !a.show_planetarium &&
                           handle_particle_tuning_key(&a, ev.key.keysym.sym)) {
                    /* handled by particle tuning controls */
                } else if (a.terrain_tuning_enabled &&
                           !a.show_acoustics && !a.show_video_menu && !a.show_planetarium &&
                           handle_terrain_tuning_key(&a, ev.key.keysym.sym)) {
                    /* handled by terrain tuning controls */
                } else if (a.show_acoustics && ev.key.keysym.sym == SDLK_UP) {
                    if (a.acoustics_page == ACOUSTICS_PAGE_COMBAT) {
                        a.acoustics_combat_selected =
                            (a.acoustics_combat_selected + ACOUST_COMBAT_SLIDER_COUNT - 1) % ACOUST_COMBAT_SLIDER_COUNT;
                    } else {
                        a.acoustics_selected = (a.acoustics_selected + ACOUSTICS_SLIDER_COUNT - 1) % ACOUSTICS_SLIDER_COUNT;
                    }
                } else if (a.show_acoustics && ev.key.keysym.sym == SDLK_DOWN) {
                    if (a.acoustics_page == ACOUSTICS_PAGE_COMBAT) {
                        a.acoustics_combat_selected = (a.acoustics_combat_selected + 1) % ACOUST_COMBAT_SLIDER_COUNT;
                    } else {
                        a.acoustics_selected = (a.acoustics_selected + 1) % ACOUSTICS_SLIDER_COUNT;
                    }
                } else if (a.show_acoustics && ev.key.keysym.sym == SDLK_LEFT) {
                    float* v = (a.acoustics_page == ACOUSTICS_PAGE_COMBAT) ?
                        &a.acoustics_combat_value_01[a.acoustics_combat_selected] :
                        &a.acoustics_value_01[a.acoustics_selected];
                    *v = clampf(*v - 0.02f, 0.0f, 1.0f);
                    apply_acoustics(&a);
                } else if (a.show_acoustics && ev.key.keysym.sym == SDLK_RIGHT) {
                    float* v = (a.acoustics_page == ACOUSTICS_PAGE_COMBAT) ?
                        &a.acoustics_combat_value_01[a.acoustics_combat_selected] :
                        &a.acoustics_value_01[a.acoustics_selected];
                    *v = clampf(*v + 0.02f, 0.0f, 1.0f);
                    apply_acoustics(&a);
                } else if (a.show_crt_ui && ev.key.keysym.sym == SDLK_UP) {
                    a.crt_ui_selected = (a.crt_ui_selected + 11) % 12;
                } else if (a.show_crt_ui && ev.key.keysym.sym == SDLK_DOWN) {
                    a.crt_ui_selected = (a.crt_ui_selected + 1) % 12;
                } else if (a.show_crt_ui && ev.key.keysym.sym == SDLK_LEFT) {
                    adjust_crt_profile(&a, a.crt_ui_selected, -1);
                } else if (a.show_crt_ui && ev.key.keysym.sym == SDLK_RIGHT) {
                    adjust_crt_profile(&a, a.crt_ui_selected, +1);
                }
            } else if (ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_LEFT) {
                if (a.show_video_menu && handle_video_menu_mouse(&a, ev.button.x, ev.button.y, 1)) {
                    a.acoustics_mouse_drag = 0;
                    a.crt_ui_mouse_drag = 0;
                } else if (a.show_planetarium && handle_planetarium_mouse(&a, ev.button.x, ev.button.y, 1)) {
                    a.acoustics_mouse_drag = 0;
                    a.crt_ui_mouse_drag = 0;
                } else if (a.show_acoustics && handle_acoustics_ui_mouse(&a, ev.button.x, ev.button.y, 1)) {
                    a.acoustics_mouse_drag = 1;
                } else if (a.show_crt_ui && handle_crt_ui_mouse(&a, ev.button.x, ev.button.y, 1)) {
                    a.crt_ui_mouse_drag = 1;
                }
            } else if (ev.type == SDL_MOUSEBUTTONUP && ev.button.button == SDL_BUTTON_LEFT) {
                if (a.show_video_menu) {
                    if (a.video_menu_dial_drag >= 0) {
                        (void)save_settings(&a);
                    }
                    a.video_menu_dial_drag = -1;
                }
                if (a.show_crt_ui && a.crt_ui_mouse_drag) {
                    (void)save_settings(&a);
                }
                a.crt_ui_mouse_drag = 0;
                a.acoustics_mouse_drag = 0;
            } else if (ev.type == SDL_MOUSEMOTION) {
                if (a.show_video_menu && a.video_menu_dial_drag >= 0) {
                    (void)update_video_menu_dial_drag(&a, ev.motion.x, ev.motion.y);
                } else if (a.show_acoustics && a.acoustics_mouse_drag) {
                    (void)handle_acoustics_ui_mouse(&a, ev.motion.x, ev.motion.y, 1);
                } else if (a.show_crt_ui && a.crt_ui_mouse_drag) {
                    (void)handle_crt_ui_mouse(&a, ev.motion.x, ev.motion.y, 1);
                }
            }
        }

        const uint8_t* keys = SDL_GetKeyboardState(NULL);
        game_input in;
        memset(&in, 0, sizeof(in));
        if (!a.show_acoustics && !a.show_video_menu && !a.show_planetarium) {
            in.left = keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT];
            in.right = keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT];
            in.up = keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP];
            in.down = keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN];
            in.fire = keys[SDL_SCANCODE_SPACE] || keys[SDL_SCANCODE_LCTRL];
            in.restart = restart_pressed;
        }

        uint64_t now = SDL_GetPerformanceCounter();
        float dt_raw = (float)(now - last) / freq;
        last = now;
        if (dt_raw <= 0.0f) dt_raw = 1.0f / 60.0f;
        float dt_sim = dt_raw;
        if (dt_sim > (1.0f / 15.0f)) dt_sim = 1.0f / 15.0f;
        if (!a.show_acoustics && !a.show_video_menu && !a.show_planetarium) {
            game_update(&a.game, dt_sim, &in);
        }
        if (a.audio_ready) {
            const int thrust_on = (!a.show_acoustics && !a.show_video_menu && !a.show_planetarium) &&
                                  (in.left || in.right || in.up || in.down) && (a.game.lives > 0);
            atomic_store_explicit(&a.thrust_gate, thrust_on ? 1 : 0, memory_order_release);
        }
        if (a.audio_ready) {
            const int fire_events = game_pop_fire_sfx_count(&a.game);
            if (fire_events > 0) {
                atomic_fetch_add_explicit(&a.pending_fire_events, (unsigned int)fire_events, memory_order_acq_rel);
            }
            game_audio_event game_events[MAX_AUDIO_EVENTS];
            const int game_event_count = game_pop_audio_events(&a.game, game_events, MAX_AUDIO_EVENTS);
            for (int i = 0; i < game_event_count; ++i) {
                const float dx = game_events[i].x - a.game.camera_x;
                const float pan = clampf(dx / (a.game.world_w * 0.5f), -1.0f, 1.0f);
                (void)audio_spatial_enqueue(&a, (uint8_t)game_events[i].type, pan, 1.0f);
            }
            atomic_store_explicit(&a.audio_weapon_level, a.game.weapon_level, memory_order_release);
        } else {
            (void)game_pop_fire_sfx_count(&a.game);
            {
                game_audio_event sink[MAX_AUDIO_EVENTS];
                (void)game_pop_audio_events(&a.game, sink, MAX_AUDIO_EVENTS);
            }
        }
        {
            char wave_msg[160];
            if (game_pop_wave_announcement(&a.game, wave_msg, sizeof(wave_msg))) {
                set_tty_message(&a, wave_msg);
            }
            (void)vg_text_fx_typewriter_update(&a.wave_tty, dt_sim);
            (void)vg_text_fx_typewriter_copy_visible(&a.wave_tty, a.wave_tty_visible, sizeof(a.wave_tty_visible));
        }
        vg_text_fx_marquee_update(&a.planetarium_marquee, dt_raw);
        if (a.audio_ready) {
            float rb_tmp[256];
            uint32_t got = 0u;
            int scope_updated = 0;
            do {
                got = wtp_ringbuffer_read(&a.scope_rb, rb_tmp, (uint32_t)(sizeof(rb_tmp) / sizeof(rb_tmp[0])));
                if (got > 0u) {
                    scope_history_push(&a, rb_tmp, got);
                    scope_updated = 1;
                }
            } while (got > 0u);
            if (scope_updated) {
                rebuild_scope_window(&a);
            }
        }

        float fps_inst = 1.0f / dt_raw;
        fps_smoothed += (fps_inst - fps_smoothed) * 0.10f;

        if (!check_vk(vkWaitForFences(a.device, 1, &a.in_flight, VK_TRUE, UINT64_MAX), "vkWaitForFences")) break;

        uint32_t image_index = 0;
        VkResult ar = vkAcquireNextImageKHR(a.device, a.swapchain, UINT64_MAX, a.image_available, VK_NULL_HANDLE, &image_index);
        if (ar != VK_SUCCESS) {
            if (ar == VK_ERROR_OUT_OF_DATE_KHR || ar == VK_SUBOPTIMAL_KHR) {
                if (!recreate_render_runtime(&a)) {
                    fprintf(stderr, "swapchain recreate failed after out-of-date/suboptimal\n");
                    break;
                }
                continue;
            } else {
                check_vk(ar, "vkAcquireNextImageKHR");
            }
            break;
        }

        float t = (float)SDL_GetTicks() * 0.001f;
        a.swapchain_needs_recreate = 0;
        if (!record_submit_present(&a, image_index, t, dt_sim, fps_smoothed)) {
            if (a.swapchain_needs_recreate) {
                if (!recreate_render_runtime(&a)) {
                    fprintf(stderr, "render failure: swapchain flagged for recreate, but recreate failed\n");
                    break;
                }
                continue;
            }
            fprintf(stderr, "render failure: record_submit_present returned 0\n");
            break;
        }
    }

    cleanup(&a);
    return 0;
}
