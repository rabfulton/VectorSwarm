#include "vg.h"
#include "vg_image.h"
#include "vg_palette.h"
#include "vg_pointer.h"
#include "vg_svg.h"
#include "vg_text_fx.h"
#include "vg_text_layout.h"
#include "vg_ui.h"
#include "vg_ui_ext.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
#if VG_DEMO_HAS_SDL_IMAGE
#include <SDL2/SDL_image.h>
#endif
#include <vulkan/vulkan.h>

#include <math.h>
#include <dirent.h>
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef VG_DEMO_HAS_POST_SHADERS
#define VG_DEMO_HAS_POST_SHADERS 0
#endif
#ifndef VG_DEMO_HAS_SDL_IMAGE
#define VG_DEMO_HAS_SDL_IMAGE 0
#endif

#if VG_DEMO_HAS_POST_SHADERS
#include "demo_bloom_frag_spv.h"
#include "demo_composite_frag_spv.h"
#include "demo_fullscreen_vert_spv.h"
#endif

#define APP_WIDTH 1440
#define APP_HEIGHT 900
#define APP_MAX_SWAPCHAIN_IMAGES 8
#define APP_MAX_SVG_FILES 128

typedef struct post_pc {
    float p0[4]; /* texel.x, texel.y, bloom_strength, bloom_radius */
    float p1[4]; /* vignette, barrel, scanline, noise */
    float p2[4]; /* time_s, ui_enable, ui_x, ui_y */
    float p3[4]; /* ui_w, ui_h, pad0, pad1 */
} post_pc;

typedef struct star3 {
    float x;
    float y;
    float z;
} star3;

typedef enum frame_result {
    FRAME_OK = 0,
    FRAME_RECREATE = 1,
    FRAME_FAIL = 2
} frame_result;

typedef enum cursor_mode {
    CURSOR_MODE_VECTOR_ASTEROIDS = 0,
    CURSOR_MODE_VECTOR_CROSSHAIR = 1,
    CURSOR_MODE_NONE = 2,
    CURSOR_MODE_SYSTEM = 3
} cursor_mode;

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
    VkFramebuffer scene_fb;
    VkRenderPass scene_render_pass;
    int scene_initialized;

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

    VkCommandPool command_pool;
    VkCommandBuffer command_buffers[APP_MAX_SWAPCHAIN_IMAGES];

    VkSemaphore image_available;
    VkSemaphore render_finished;
    VkFence in_flight;

    vg_context* vg;
    vg_path* wave_path;

    int show_ui;
    int selected_param;
    int selected_image_param;
    int selected_text_param;
    float main_line_width;
    float fps_smoothed;
    int prev_adjust_dir;
    int prev_nav_dir;
    float adjust_repeat_timer;
    float nav_repeat_timer;

    int scene_mode;
    int cursor_mode;
    int mouse_x;
    int mouse_y;
    int mouse_in_window;
    int ui_drag_active;
    int ui_drag_kind;
    int ui_drag_param;
    star3 stars[320];
    int stars_initialized;

    vg_text_fx_typewriter tty_fx;

    SDL_AudioDeviceID audio_dev;
    int audio_ready;

    vg_crt_profile crt_profile;
    int crt_profile_valid;
    char profile_path[512];
    float boxed_font_weight;
    int force_clear_frames;
    vg_ui_history cpu_hist;
    vg_ui_history net_hist;
    float cpu_hist_buf[180];
    float net_hist_buf[180];
    float fft_bins[48];
    uint8_t* image_rgba;
    uint32_t image_w;
    uint32_t image_h;
    uint32_t image_stride;
    vg_svg_asset* svg_asset;
    char svg_asset_name[128];
    char svg_dir_path[256];
    char svg_files[APP_MAX_SVG_FILES][128];
    int svg_file_count;
    int svg_file_index;
    float image_threshold;
    float image_contrast;
    float image_pitch_px;
    float image_min_width_px;
    float image_max_width_px;
    float image_jitter_px;
    float image_block_cell_w_px;
    float image_block_cell_h_px;
    int image_block_levels;
    int image_invert;
    vg_text_fx_marquee scene7_marquee;
} app;

enum {
    UI_PARAM_BLOOM_STRENGTH = 0,
    UI_PARAM_BLOOM_RADIUS = 1,
    UI_PARAM_PERSISTENCE = 2,
    UI_PARAM_JITTER = 3,
    UI_PARAM_FLICKER = 4,
    UI_PARAM_BEAM_CORE = 5,
    UI_PARAM_BEAM_HALO = 6,
    UI_PARAM_BEAM_INTENSITY = 7,
    UI_PARAM_VIGNETTE = 8,
    UI_PARAM_BARREL = 9,
    UI_PARAM_SCANLINE = 10,
    UI_PARAM_NOISE = 11,
    UI_PARAM_LINE_WIDTH = 12,
    UI_PARAM_COUNT = 13
};

enum {
    IMAGE_UI_PARAM_THRESHOLD = 0,
    IMAGE_UI_PARAM_CONTRAST = 1,
    IMAGE_UI_PARAM_SCAN_PITCH = 2,
    IMAGE_UI_PARAM_MIN_WIDTH = 3,
    IMAGE_UI_PARAM_MAX_WIDTH = 4,
    IMAGE_UI_PARAM_JITTER = 5,
    IMAGE_UI_PARAM_BLOCK_W = 6,
    IMAGE_UI_PARAM_BLOCK_H = 7,
    IMAGE_UI_PARAM_BLOCK_LEVELS = 8,
    IMAGE_UI_PARAM_INVERT = 9,
    IMAGE_UI_PARAM_COUNT = 10
};

enum {
    TEXT_UI_PARAM_BOX_WEIGHT = 0,
    TEXT_UI_PARAM_COUNT = 1
};

static const float k_ui_x = 24.0f;
static const float k_ui_y = 24.0f;
static const float k_ui_w = 560.0f;
static const float k_ui_row_step = 40.0f;
static const float k_ui_h = 70.0f + (float)UI_PARAM_COUNT * 40.0f + 56.0f;
static const float k_ui_image_h = 70.0f + (float)IMAGE_UI_PARAM_COUNT * 40.0f + 56.0f;
static const float k_ui_text_h = 70.0f + (float)TEXT_UI_PARAM_COUNT * 40.0f + 56.0f;

enum {
    SCENE_CLASSIC = 0,
    SCENE_WIREFRAME_CUBE = 1,
    SCENE_STARFIELD = 2,
    SCENE_SURFACE_PLOT = 3,
    SCENE_SYNTHWAVE = 4,
    SCENE_FILL_PRIMS = 5,
    SCENE_TITLE_CRAWL = 6,
    SCENE_IMAGE_FX = 7,
    SCENE_COUNT = 8
};

static int check_vk(VkResult res, const char* what) {
    if (res != VK_SUCCESS) {
        fprintf(stderr, "%s failed (VkResult=%d)\n", what, (int)res);
        return 0;
    }
    return 1;
}

static uint32_t hash_u32(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

static float rand_signed(uint32_t seed) {
    uint32_t h = hash_u32(seed);
    float t = (float)(h & 0x00ffffffu) / 8388607.5f;
    return t - 1.0f;
}

static float clampf(float v, float lo, float hi) {
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

static float norm_range(float v, float lo, float hi) {
    if (hi <= lo) {
        return 0.0f;
    }
    return clampf((v - lo) / (hi - lo), 0.0f, 1.0f);
}

static float lerpf(float a, float b, float t) {
    return a + (b - a) * t;
}

static vg_vec2 project_3d(float x, float y, float z, float w, float h, float fov_px, float cam_z) {
    float zz = z + cam_z;
    if (zz < 0.10f) {
        zz = 0.10f;
    }
    float s = fov_px / zz;
    vg_vec2 p = {w * 0.5f + x * s, h * 0.55f - y * s};
    return p;
}

static void queue_teletype_beep(app* a, float freq_hz, float dur_s, float amp) {
    if (!a->audio_ready) {
        return;
    }
    const int sample_rate = 48000;
    int n = (int)(dur_s * (float)sample_rate);
    if (n < 64) {
        n = 64;
    }
    if (n > 4096) {
        n = 4096;
    }
    float* samples = (float*)malloc((size_t)n * sizeof(float));
    if (!samples) {
        return;
    }
    float phase = 0.0f;
    float step = 2.0f * 3.14159265358979323846f * freq_hz / (float)sample_rate;
    for (int i = 0; i < n; ++i) {
        float t = (float)i / (float)(n - 1);
        float env = (1.0f - t) * (1.0f - t);
        samples[i] = sinf(phase) * amp * env;
        phase += step;
    }
    SDL_QueueAudio(a->audio_dev, samples, (uint32_t)((size_t)n * sizeof(float)));
    free(samples);
}

static void teletype_beep_cb(void* user, char ch, float freq_hz, float dur_s, float amp) {
    (void)ch;
    app* a = (app*)user;
    if (!a) {
        return;
    }
    queue_teletype_beep(a, freq_hz, dur_s, amp);
}

static void reset_teletype(app* a) {
    vg_text_fx_typewriter_reset(&a->tty_fx);
    a->tty_fx.timer_s = 0.02f;
}

static void set_scene(app* a, int mode) {
    static const char* k_scene_text[SCENE_COUNT] = {
        "STATUS READY\nMODE 1 METERS PANEL\nLINEAR + RADIAL TEST",
        "STATUS READY\nMODE 2 WIREFRAME CUBE\nROTATION + PERSPECTIVE TEST",
        "STATUS READY\nMODE 3 STARFIELD\nDEPTH MOTION + STREAK TEST",
        "STATUS READY\nMODE 4 SURFACE PLOT\n3D FUNCTION GRID TEST",
        "STATUS READY\nMODE 5 SVG IMPORTER\nVECTOR ASSET PREVIEW",
        "STATUS READY\nMODE 6 SOLAR INFOGRAPHIC\nFILLS + ORBITS + CALLOUTS",
        "STATUS READY\nMODE 7 TITLE CRAWL\nBOXED FONT + ROTARY TEST",
        "STATUS READY\nMODE 8 IMAGE FX TEST\nMONO + BLOCK + SVG"
    };
    if (mode < 0 || mode >= SCENE_COUNT) {
        return;
    }
    a->scene_mode = mode;
    vg_text_fx_typewriter_set_text(&a->tty_fx, k_scene_text[mode]);
    reset_teletype(a);
    if (mode == SCENE_TITLE_CRAWL) {
        vg_text_fx_marquee_set_text(&a->scene7_marquee, "MARQUEE HELPER: LONG TEXT SCROLLS WITHIN BOX   ");
        vg_text_fx_marquee_set_speed(&a->scene7_marquee, 70.0f);
        vg_text_fx_marquee_set_gap(&a->scene7_marquee, 48.0f);
        vg_text_fx_marquee_reset(&a->scene7_marquee);
    }
}

static void init_teletype_audio(app* a) {
    SDL_AudioSpec want;
    SDL_zero(want);
    want.freq = 48000;
    want.format = AUDIO_F32SYS;
    want.channels = 1;
    want.samples = 512;
    want.callback = NULL;
    a->audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, NULL, 0);
    if (a->audio_dev != 0) {
        SDL_PauseAudioDevice(a->audio_dev, 0);
        a->audio_ready = 1;
    } else {
        a->audio_ready = 0;
    }
}

static void init_image_asset(app* a) {
#if VG_DEMO_HAS_SDL_IMAGE
    static const char* k_candidates[] = {
        "assets/nick.jpg",
        "../assets/nick.jpg",
        "../../assets/nick.jpg"
    };
    SDL_Surface* src = NULL;
    const char* loaded_path = NULL;
    for (size_t i = 0; i < sizeof(k_candidates) / sizeof(k_candidates[0]); ++i) {
        src = IMG_Load(k_candidates[i]);
        if (src) {
            loaded_path = k_candidates[i];
            break;
        }
    }
    if (!src) {
        fprintf(stderr, "IMG_Load nick.jpg failed: %s\n", IMG_GetError());
        return;
    }
    fprintf(stderr, "image loaded: %s\n", loaded_path);
    SDL_Surface* rgba = SDL_ConvertSurfaceFormat(src, SDL_PIXELFORMAT_RGBA32, 0);
    SDL_FreeSurface(src);
    if (!rgba) {
        fprintf(stderr, "SDL_ConvertSurfaceFormat RGBA32 failed: %s\n", SDL_GetError());
        return;
    }
    size_t bytes = (size_t)rgba->pitch * (size_t)rgba->h;
    a->image_rgba = (uint8_t*)malloc(bytes);
    if (!a->image_rgba) {
        SDL_FreeSurface(rgba);
        return;
    }
    memcpy(a->image_rgba, rgba->pixels, bytes);
    a->image_w = (uint32_t)rgba->w;
    a->image_h = (uint32_t)rgba->h;
    a->image_stride = (uint32_t)rgba->pitch;
    SDL_FreeSurface(rgba);
#else
    (void)a;
#endif
}

static int has_svg_ext(const char* name) {
    if (!name) {
        return 0;
    }
    const char* dot = strrchr(name, '.');
    if (!dot) {
        return 0;
    }
    return tolower((unsigned char)dot[1]) == 's' &&
           tolower((unsigned char)dot[2]) == 'v' &&
           tolower((unsigned char)dot[3]) == 'g' &&
           dot[4] == '\0';
}

static int cmp_svg_name(const void* a, const void* b) {
    const char* sa = (const char*)a;
    const char* sb = (const char*)b;
    return strcmp(sa, sb);
}

static void load_svg_asset_at_index(app* a, int index) {
    if (!a || a->svg_file_count <= 0 || a->svg_dir_path[0] == '\0') {
        return;
    }
    if (index < 0) {
        index = a->svg_file_count - 1;
    } else if (index >= a->svg_file_count) {
        index = 0;
    }

    char full_path[512];
    snprintf(full_path, sizeof(full_path), "%s/%s", a->svg_dir_path, a->svg_files[index]);

    vg_svg_load_params lp = {
        .curve_tolerance_px = 1.0f,
        .dpi = 96.0f,
        .units = "px"
    };

    vg_svg_asset* next_asset = NULL;
    vg_result r = vg_svg_load_from_file(full_path, &lp, &next_asset);
    if (r != VG_OK) {
        fprintf(stderr, "vg_svg_load_from_file failed for %s (%d)\n", full_path, (int)r);
        return;
    }

    if (a->svg_asset) {
        vg_svg_destroy(a->svg_asset);
    }
    a->svg_asset = next_asset;
    a->svg_file_index = index;
    snprintf(a->svg_asset_name, sizeof(a->svg_asset_name), "%s", a->svg_files[index]);
    fprintf(stderr, "svg loaded [%d/%d]: %s\n", index + 1, a->svg_file_count, full_path);
}

static void cycle_svg_asset(app* a, int dir) {
    if (!a || a->svg_file_count <= 0) {
        return;
    }
    load_svg_asset_at_index(a, a->svg_file_index + dir);
}

static void init_svg_asset(app* a) {
    static const char* k_dirs[] = {"assets", "../assets", "../../assets"};
    a->svg_file_count = 0;
    a->svg_file_index = 0;
    a->svg_dir_path[0] = '\0';
    a->svg_asset_name[0] = '\0';

    for (size_t d = 0; d < sizeof(k_dirs) / sizeof(k_dirs[0]); ++d) {
        DIR* dir = opendir(k_dirs[d]);
        if (!dir) {
            continue;
        }
        struct dirent* ent;
        while ((ent = readdir(dir)) != NULL) {
            if (!has_svg_ext(ent->d_name) || a->svg_file_count >= APP_MAX_SVG_FILES) {
                continue;
            }
            snprintf(a->svg_files[a->svg_file_count], sizeof(a->svg_files[a->svg_file_count]), "%s", ent->d_name);
            a->svg_file_count++;
        }
        closedir(dir);
        if (a->svg_file_count > 0) {
            snprintf(a->svg_dir_path, sizeof(a->svg_dir_path), "%s", k_dirs[d]);
            qsort(a->svg_files, (size_t)a->svg_file_count, sizeof(a->svg_files[0]), cmp_svg_name);
            break;
        }
    }

    if (a->svg_file_count <= 0) {
        return;
    }
    load_svg_asset_at_index(a, 0);
}

static void init_starfield(app* a) {
    for (size_t i = 0; i < sizeof(a->stars) / sizeof(a->stars[0]); ++i) {
        a->stars[i].x = rand_signed((uint32_t)(i * 31u + 7u)) * 2.2f;
        a->stars[i].y = rand_signed((uint32_t)(i * 71u + 13u)) * 1.2f;
        a->stars[i].z = 0.2f + (float)i / (float)(sizeof(a->stars) / sizeof(a->stars[0])) * 1.8f;
    }
    a->stars_initialized = 1;
}

static void clamp_crt_profile(vg_crt_profile* crt) {
    crt->bloom_strength = clampf(crt->bloom_strength, 0.0f, 3.0f);
    crt->bloom_radius_px = clampf(crt->bloom_radius_px, 0.0f, 14.0f);
    crt->persistence_decay = clampf(crt->persistence_decay, 0.70f, 0.985f);
    crt->jitter_amount = clampf(crt->jitter_amount, 0.0f, 1.5f);
    crt->flicker_amount = clampf(crt->flicker_amount, 0.0f, 1.0f);
    crt->beam_core_width_px = clampf(crt->beam_core_width_px, 0.5f, 3.5f);
    crt->beam_halo_width_px = clampf(crt->beam_halo_width_px, 0.0f, 10.0f);
    crt->beam_intensity = clampf(crt->beam_intensity, 0.2f, 3.0f);
    crt->vignette_strength = clampf(crt->vignette_strength, 0.0f, 1.0f);
    crt->barrel_distortion = clampf(crt->barrel_distortion, 0.0f, 0.30f);
    crt->scanline_strength = clampf(crt->scanline_strength, 0.0f, 1.0f);
    crt->noise_strength = clampf(crt->noise_strength, 0.0f, 0.30f);
}

static void init_profile_path(app* a) {
    const char* fallback = "./vg_demo_vk_profile.cfg";
    snprintf(a->profile_path, sizeof(a->profile_path), "%s", fallback);
    char* pref = SDL_GetPrefPath("vectorgfx", "vk_demo");
    if (pref && pref[0] != '\0') {
        snprintf(a->profile_path, sizeof(a->profile_path), "%svg_demo_vk_profile.cfg", pref);
    }
    if (pref) {
        SDL_free(pref);
    }
}

static int save_profile(const app* a) {
    FILE* f = fopen(a->profile_path, "wb");
    if (!f) {
        fprintf(stderr, "profile save failed: %s\n", a->profile_path);
        return 0;
    }
    fprintf(f, "line_width=%.6f\n", a->main_line_width);
    fprintf(f, "box_weight=%.6f\n", a->boxed_font_weight);
    fprintf(f, "scene_mode=%d\n", a->scene_mode);
    fprintf(f, "show_ui=%d\n", a->show_ui);
    fprintf(f, "beam_core_width_px=%.6f\n", a->crt_profile.beam_core_width_px);
    fprintf(f, "beam_halo_width_px=%.6f\n", a->crt_profile.beam_halo_width_px);
    fprintf(f, "beam_intensity=%.6f\n", a->crt_profile.beam_intensity);
    fprintf(f, "bloom_strength=%.6f\n", a->crt_profile.bloom_strength);
    fprintf(f, "bloom_radius_px=%.6f\n", a->crt_profile.bloom_radius_px);
    fprintf(f, "persistence_decay=%.6f\n", a->crt_profile.persistence_decay);
    fprintf(f, "jitter_amount=%.6f\n", a->crt_profile.jitter_amount);
    fprintf(f, "flicker_amount=%.6f\n", a->crt_profile.flicker_amount);
    fprintf(f, "vignette_strength=%.6f\n", a->crt_profile.vignette_strength);
    fprintf(f, "barrel_distortion=%.6f\n", a->crt_profile.barrel_distortion);
    fprintf(f, "scanline_strength=%.6f\n", a->crt_profile.scanline_strength);
    fprintf(f, "noise_strength=%.6f\n", a->crt_profile.noise_strength);
    fclose(f);
    fprintf(stderr, "profile saved: %s\n", a->profile_path);
    return 1;
}

static int load_profile(app* a) {
    FILE* f = fopen(a->profile_path, "rb");
    if (!f) {
        fprintf(stderr, "profile load skipped (missing): %s\n", a->profile_path);
        return 0;
    }
    vg_crt_profile crt = a->crt_profile;
    float line_width = a->main_line_width;
    float box_weight = a->boxed_font_weight;
    int scene_mode = a->scene_mode;
    int show_ui = a->show_ui;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char key[64];
        float val = 0.0f;
        if (sscanf(line, " %63[^=]=%f", key, &val) != 2) {
            continue;
        }
        if (strcmp(key, "line_width") == 0) line_width = val;
        else if (strcmp(key, "box_weight") == 0) box_weight = val;
        else if (strcmp(key, "scene_mode") == 0) scene_mode = (int)val;
        else if (strcmp(key, "show_ui") == 0) show_ui = (int)val;
        else if (strcmp(key, "beam_core_width_px") == 0) crt.beam_core_width_px = val;
        else if (strcmp(key, "beam_halo_width_px") == 0) crt.beam_halo_width_px = val;
        else if (strcmp(key, "beam_intensity") == 0) crt.beam_intensity = val;
        else if (strcmp(key, "bloom_strength") == 0) crt.bloom_strength = val;
        else if (strcmp(key, "bloom_radius_px") == 0) crt.bloom_radius_px = val;
        else if (strcmp(key, "persistence_decay") == 0) crt.persistence_decay = val;
        else if (strcmp(key, "jitter_amount") == 0) crt.jitter_amount = val;
        else if (strcmp(key, "flicker_amount") == 0) crt.flicker_amount = val;
        else if (strcmp(key, "vignette_strength") == 0) crt.vignette_strength = val;
        else if (strcmp(key, "barrel_distortion") == 0) crt.barrel_distortion = val;
        else if (strcmp(key, "scanline_strength") == 0) crt.scanline_strength = val;
        else if (strcmp(key, "noise_strength") == 0) crt.noise_strength = val;
    }
    fclose(f);

    clamp_crt_profile(&crt);
    a->crt_profile = crt;
    a->crt_profile_valid = 1;
    a->main_line_width = clampf(line_width, 1.0f, 16.0f);
    a->boxed_font_weight = clampf(box_weight, 0.25f, 3.0f);
    a->show_ui = show_ui ? 1 : 0;
    if (scene_mode < 0 || scene_mode >= SCENE_COUNT) {
        scene_mode = SCENE_CLASSIC;
    }
    set_scene(a, scene_mode);
    if (a->vg) {
        vg_set_crt_profile(a->vg, &a->crt_profile);
    }
    fprintf(stderr, "profile loaded: %s\n", a->profile_path);
    return 1;
}

static void update_teletype(app* a, float dt) {
    if (!a->tty_fx.text) {
        return;
    }
    (void)vg_text_fx_typewriter_update(&a->tty_fx, dt);
    vg_text_fx_marquee_update(&a->scene7_marquee, dt);
}

static uint32_t find_memory_type(app* a, uint32_t type_bits, VkMemoryPropertyFlags required) {
    VkPhysicalDeviceMemoryProperties props = {0};
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
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
    };
    if (!check_vk(vkCreateImage(a->device, &img, NULL, out_image), "vkCreateImage")) {
        return 0;
    }

    VkMemoryRequirements req = {0};
    vkGetImageMemoryRequirements(a->device, *out_image, &req);

    uint32_t mem_type = find_memory_type(a, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (mem_type == UINT32_MAX) {
        fprintf(stderr, "No device local memory type for image\n");
        return 0;
    }

    VkMemoryAllocateInfo alloc = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = req.size,
        .memoryTypeIndex = mem_type
    };
    if (!check_vk(vkAllocateMemory(a->device, &alloc, NULL, out_mem), "vkAllocateMemory(image)")) {
        return 0;
    }

    if (!check_vk(vkBindImageMemory(a->device, *out_image, *out_mem, 0), "vkBindImageMemory")) {
        return 0;
    }

    VkImageViewCreateInfo view = {
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
    if (!check_vk(vkCreateImageView(a->device, &view, NULL, out_view), "vkCreateImageView(offscreen)")) {
        return 0;
    }

    return 1;
}

static int create_instance(app* a) {
    unsigned int ext_count = 0;
    if (!SDL_Vulkan_GetInstanceExtensions(a->window, &ext_count, NULL)) {
        fprintf(stderr, "SDL_Vulkan_GetInstanceExtensions(count) failed: %s\n", SDL_GetError());
        return 0;
    }

    const char** exts = (const char**)calloc(ext_count, sizeof(*exts));
    if (!exts) {
        return 0;
    }

    int ok = 0;
    if (!SDL_Vulkan_GetInstanceExtensions(a->window, &ext_count, exts)) {
        fprintf(stderr, "SDL_Vulkan_GetInstanceExtensions(list) failed: %s\n", SDL_GetError());
        goto cleanup;
    }

    VkApplicationInfo app_info = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "vectorgfx Vulkan SDL demo",
        .applicationVersion = VK_MAKE_VERSION(0, 1, 0),
        .pEngineName = "vectorgfx",
        .engineVersion = VK_MAKE_VERSION(0, 1, 0),
        .apiVersion = VK_API_VERSION_1_1
    };

    VkInstanceCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &app_info,
        .enabledExtensionCount = ext_count,
        .ppEnabledExtensionNames = exts
    };

    ok = check_vk(vkCreateInstance(&create_info, NULL, &a->instance), "vkCreateInstance");

cleanup:
    free(exts);
    return ok;
}

static int create_surface(app* a) {
    if (!SDL_Vulkan_CreateSurface(a->window, a->instance, &a->surface)) {
        fprintf(stderr, "SDL_Vulkan_CreateSurface failed: %s\n", SDL_GetError());
        return 0;
    }
    return 1;
}

static int pick_physical_device(app* a) {
    uint32_t count = 0;
    if (!check_vk(vkEnumeratePhysicalDevices(a->instance, &count, NULL), "vkEnumeratePhysicalDevices(count)")) {
        return 0;
    }
    if (count == 0) {
        fprintf(stderr, "No Vulkan physical devices found\n");
        return 0;
    }

    VkPhysicalDevice* devices = (VkPhysicalDevice*)calloc(count, sizeof(*devices));
    if (!devices) {
        return 0;
    }

    int ok = 0;
    if (!check_vk(vkEnumeratePhysicalDevices(a->instance, &count, devices), "vkEnumeratePhysicalDevices(list)")) {
        goto cleanup;
    }

    for (uint32_t d = 0; d < count && !ok; ++d) {
        VkPhysicalDevice dev = devices[d];

        uint32_t qcount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(dev, &qcount, NULL);
        if (qcount == 0) {
            continue;
        }

        VkQueueFamilyProperties* qprops = (VkQueueFamilyProperties*)calloc(qcount, sizeof(*qprops));
        if (!qprops) {
            continue;
        }
        vkGetPhysicalDeviceQueueFamilyProperties(dev, &qcount, qprops);

        int have_graphics = 0;
        int have_present = 0;
        uint32_t gq = 0;
        uint32_t pq = 0;

        for (uint32_t i = 0; i < qcount; ++i) {
            if ((qprops[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && !have_graphics) {
                gq = i;
                have_graphics = 1;
            }
            VkBool32 present = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(dev, i, a->surface, &present);
            if (present && !have_present) {
                pq = i;
                have_present = 1;
            }
        }

        free(qprops);

        if (have_graphics && have_present) {
            a->physical_device = dev;
            a->graphics_queue_family = gq;
            a->present_queue_family = pq;
            ok = 1;
        }
    }

cleanup:
    free(devices);
    if (!ok) {
        fprintf(stderr, "Failed to find suitable physical device\n");
    }
    return ok;
}

static int create_device(app* a) {
    float priority = 1.0f;

    VkDeviceQueueCreateInfo queue_infos[2] = {0};
    uint32_t queue_info_count = 0;

    queue_infos[queue_info_count].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_infos[queue_info_count].queueFamilyIndex = a->graphics_queue_family;
    queue_infos[queue_info_count].queueCount = 1;
    queue_infos[queue_info_count].pQueuePriorities = &priority;
    queue_info_count++;

    if (a->present_queue_family != a->graphics_queue_family) {
        queue_infos[queue_info_count].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue_infos[queue_info_count].queueFamilyIndex = a->present_queue_family;
        queue_infos[queue_info_count].queueCount = 1;
        queue_infos[queue_info_count].pQueuePriorities = &priority;
        queue_info_count++;
    }

    const char* dev_exts[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

    VkDeviceCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = queue_info_count,
        .pQueueCreateInfos = queue_infos,
        .enabledExtensionCount = 1,
        .ppEnabledExtensionNames = dev_exts
    };

    if (!check_vk(vkCreateDevice(a->physical_device, &create_info, NULL, &a->device), "vkCreateDevice")) {
        return 0;
    }

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
        if (modes[i] == VK_PRESENT_MODE_MAILBOX_KHR) {
            return modes[i];
        }
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

static VkExtent2D choose_extent(const VkSurfaceCapabilitiesKHR* caps) {
    if (caps->currentExtent.width != UINT32_MAX) {
        return caps->currentExtent;
    }
    VkExtent2D out = {APP_WIDTH, APP_HEIGHT};
    if (out.width < caps->minImageExtent.width) {
        out.width = caps->minImageExtent.width;
    }
    if (out.height < caps->minImageExtent.height) {
        out.height = caps->minImageExtent.height;
    }
    if (out.width > caps->maxImageExtent.width) {
        out.width = caps->maxImageExtent.width;
    }
    if (out.height > caps->maxImageExtent.height) {
        out.height = caps->maxImageExtent.height;
    }
    return out;
}

static int create_swapchain(app* a) {
    VkSurfaceCapabilitiesKHR caps = {0};
    if (!check_vk(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(a->physical_device, a->surface, &caps), "vkGetPhysicalDeviceSurfaceCapabilitiesKHR")) {
        return 0;
    }

    uint32_t format_count = 0;
    if (!check_vk(vkGetPhysicalDeviceSurfaceFormatsKHR(a->physical_device, a->surface, &format_count, NULL), "vkGetPhysicalDeviceSurfaceFormatsKHR(count)")) {
        return 0;
    }
    if (format_count == 0) {
        return 0;
    }
    VkSurfaceFormatKHR* formats = (VkSurfaceFormatKHR*)calloc(format_count, sizeof(*formats));
    if (!formats) {
        return 0;
    }
    if (!check_vk(vkGetPhysicalDeviceSurfaceFormatsKHR(a->physical_device, a->surface, &format_count, formats), "vkGetPhysicalDeviceSurfaceFormatsKHR(list)")) {
        free(formats);
        return 0;
    }

    uint32_t mode_count = 0;
    if (!check_vk(vkGetPhysicalDeviceSurfacePresentModesKHR(a->physical_device, a->surface, &mode_count, NULL), "vkGetPhysicalDeviceSurfacePresentModesKHR(count)")) {
        free(formats);
        return 0;
    }
    VkPresentModeKHR* modes = (VkPresentModeKHR*)calloc(mode_count > 0 ? mode_count : 1u, sizeof(*modes));
    if (!modes) {
        free(formats);
        return 0;
    }
    if (mode_count > 0 && !check_vk(vkGetPhysicalDeviceSurfacePresentModesKHR(a->physical_device, a->surface, &mode_count, modes), "vkGetPhysicalDeviceSurfacePresentModesKHR(list)")) {
        free(modes);
        free(formats);
        return 0;
    }

    VkSurfaceFormatKHR fmt = choose_surface_format(formats, format_count);
    VkPresentModeKHR mode = choose_present_mode(modes, mode_count);
    VkExtent2D extent = choose_extent(&caps);

    free(modes);
    free(formats);

    uint32_t image_count = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && image_count > caps.maxImageCount) {
        image_count = caps.maxImageCount;
    }
    if (image_count > APP_MAX_SWAPCHAIN_IMAGES) {
        image_count = APP_MAX_SWAPCHAIN_IMAGES;
    }

    uint32_t queue_indices[2] = {a->graphics_queue_family, a->present_queue_family};

    VkSwapchainCreateInfoKHR create_info = {
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
        .clipped = VK_TRUE,
        .oldSwapchain = VK_NULL_HANDLE
    };

    if (a->graphics_queue_family != a->present_queue_family) {
        create_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        create_info.queueFamilyIndexCount = 2;
        create_info.pQueueFamilyIndices = queue_indices;
    } else {
        create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    if (!check_vk(vkCreateSwapchainKHR(a->device, &create_info, NULL, &a->swapchain), "vkCreateSwapchainKHR")) {
        return 0;
    }

    a->swapchain_format = fmt.format;
    a->swapchain_extent = extent;

    if (!check_vk(vkGetSwapchainImagesKHR(a->device, a->swapchain, &a->swapchain_image_count, NULL), "vkGetSwapchainImagesKHR(count)")) {
        return 0;
    }
    if (a->swapchain_image_count > APP_MAX_SWAPCHAIN_IMAGES) {
        fprintf(stderr, "swapchain images exceed APP_MAX_SWAPCHAIN_IMAGES\n");
        return 0;
    }
    if (!check_vk(vkGetSwapchainImagesKHR(a->device, a->swapchain, &a->swapchain_image_count, a->swapchain_images), "vkGetSwapchainImagesKHR(list)")) {
        return 0;
    }

    return 1;
}

static int create_swapchain_image_views(app* a) {
    for (uint32_t i = 0; i < a->swapchain_image_count; ++i) {
        VkImageViewCreateInfo info = {
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
        if (!check_vk(vkCreateImageView(a->device, &info, NULL, &a->swapchain_image_views[i]), "vkCreateImageView(swapchain)")) {
            return 0;
        }
    }
    return 1;
}

static int create_render_passes(app* a) {
    VkAttachmentDescription scene_att = {
        .format = a->swapchain_format,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    };
    VkAttachmentReference scene_ref = {.attachment = 0, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription scene_sub = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments = &scene_ref
    };
    VkRenderPassCreateInfo scene_rp = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &scene_att,
        .subpassCount = 1,
        .pSubpasses = &scene_sub
    };
    if (!check_vk(vkCreateRenderPass(a->device, &scene_rp, NULL, &a->scene_render_pass), "vkCreateRenderPass(scene)")) {
        return 0;
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
    VkSubpassDescription bloom_sub = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments = &bloom_ref
    };
    VkRenderPassCreateInfo bloom_rp = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &bloom_att,
        .subpassCount = 1,
        .pSubpasses = &bloom_sub
    };
    if (!check_vk(vkCreateRenderPass(a->device, &bloom_rp, NULL, &a->bloom_render_pass), "vkCreateRenderPass(bloom)")) {
        return 0;
    }

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
    VkSubpassDescription present_sub = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments = &present_ref
    };
    VkRenderPassCreateInfo present_rp = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &present_att,
        .subpassCount = 1,
        .pSubpasses = &present_sub
    };
    if (!check_vk(vkCreateRenderPass(a->device, &present_rp, NULL, &a->present_render_pass), "vkCreateRenderPass(present)")) {
        return 0;
    }

    return 1;
}

static int create_offscreen_targets(app* a) {
    uint32_t w = a->swapchain_extent.width;
    uint32_t h = a->swapchain_extent.height;
    VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    if (!create_image_2d(a, w, h, a->swapchain_format, usage, &a->scene_image, &a->scene_memory, &a->scene_view)) {
        return 0;
    }
    if (!create_image_2d(a, w, h, a->swapchain_format, usage, &a->bloom_image, &a->bloom_memory, &a->bloom_view)) {
        return 0;
    }

    VkImageView scene_att[] = {a->scene_view};
    VkFramebufferCreateInfo scene_fb = {
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass = a->scene_render_pass,
        .attachmentCount = 1,
        .pAttachments = scene_att,
        .width = w,
        .height = h,
        .layers = 1
    };
    if (!check_vk(vkCreateFramebuffer(a->device, &scene_fb, NULL, &a->scene_fb), "vkCreateFramebuffer(scene)")) {
        return 0;
    }

    VkImageView bloom_att[] = {a->bloom_view};
    VkFramebufferCreateInfo bloom_fb = {
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass = a->bloom_render_pass,
        .attachmentCount = 1,
        .pAttachments = bloom_att,
        .width = w,
        .height = h,
        .layers = 1
    };
    if (!check_vk(vkCreateFramebuffer(a->device, &bloom_fb, NULL, &a->bloom_fb), "vkCreateFramebuffer(bloom)")) {
        return 0;
    }

    return 1;
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
        if (!check_vk(vkCreateFramebuffer(a->device, &fb, NULL, &a->present_framebuffers[i]), "vkCreateFramebuffer(present)")) {
            return 0;
        }
    }
    return 1;
}

static int create_command_pool_and_buffers(app* a) {
    VkCommandPoolCreateInfo pool = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = a->graphics_queue_family
    };
    if (!check_vk(vkCreateCommandPool(a->device, &pool, NULL, &a->command_pool), "vkCreateCommandPool")) {
        return 0;
    }

    VkCommandBufferAllocateInfo alloc = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = a->command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = a->swapchain_image_count
    };
    return check_vk(vkAllocateCommandBuffers(a->device, &alloc, a->command_buffers), "vkAllocateCommandBuffers");
}

static int create_sync(app* a) {
    VkSemaphoreCreateInfo sem = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo fence = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .flags = VK_FENCE_CREATE_SIGNALED_BIT};

    if (!check_vk(vkCreateSemaphore(a->device, &sem, NULL, &a->image_available), "vkCreateSemaphore(image_available)")) {
        return 0;
    }
    if (!check_vk(vkCreateSemaphore(a->device, &sem, NULL, &a->render_finished), "vkCreateSemaphore(render_finished)")) {
        return 0;
    }
    return check_vk(vkCreateFence(a->device, &fence, NULL, &a->in_flight), "vkCreateFence");
}

static int create_post_resources(app* a) {
#if !VG_DEMO_HAS_POST_SHADERS
    fprintf(stderr, "Demo post shaders were not generated.\n");
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
    if (!check_vk(vkCreateSampler(a->device, &sampler, NULL, &a->post_sampler), "vkCreateSampler")) {
        return 0;
    }

    VkDescriptorSetLayoutBinding bindings[2] = {
        {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT
        },
        {
            .binding = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT
        }
    };
    VkDescriptorSetLayoutCreateInfo dsl = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 2,
        .pBindings = bindings
    };
    if (!check_vk(vkCreateDescriptorSetLayout(a->device, &dsl, NULL, &a->post_desc_layout), "vkCreateDescriptorSetLayout")) {
        return 0;
    }

    VkDescriptorPoolSize pool_size = {
        .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 2
    };
    VkDescriptorPoolCreateInfo pool = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .poolSizeCount = 1,
        .pPoolSizes = &pool_size,
        .maxSets = 1
    };
    if (!check_vk(vkCreateDescriptorPool(a->device, &pool, NULL, &a->post_desc_pool), "vkCreateDescriptorPool")) {
        return 0;
    }

    VkDescriptorSetAllocateInfo alloc = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = a->post_desc_pool,
        .descriptorSetCount = 1,
        .pSetLayouts = &a->post_desc_layout
    };
    if (!check_vk(vkAllocateDescriptorSets(a->device, &alloc, &a->post_desc_set), "vkAllocateDescriptorSets")) {
        return 0;
    }

    VkDescriptorImageInfo scene_info = {
        .sampler = a->post_sampler,
        .imageView = a->scene_view,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    };
    VkDescriptorImageInfo bloom_info = {
        .sampler = a->post_sampler,
        .imageView = a->bloom_view,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    };

    VkWriteDescriptorSet writes[2] = {
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = a->post_desc_set,
            .dstBinding = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = &scene_info
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = a->post_desc_set,
            .dstBinding = 1,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = &bloom_info
        }
    };
    vkUpdateDescriptorSets(a->device, 2, writes, 0, NULL);

    VkPushConstantRange pc = {
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = sizeof(post_pc)
    };
    VkPipelineLayoutCreateInfo pli = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &a->post_desc_layout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pc
    };
    if (!check_vk(vkCreatePipelineLayout(a->device, &pli, NULL, &a->post_layout), "vkCreatePipelineLayout(post)")) {
        return 0;
    }

    VkShaderModuleCreateInfo vs_ci = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = demo_fullscreen_vert_spv_len,
        .pCode = (const uint32_t*)demo_fullscreen_vert_spv
    };
    VkShaderModuleCreateInfo bloom_ci = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = demo_bloom_frag_spv_len,
        .pCode = (const uint32_t*)demo_bloom_frag_spv
    };
    VkShaderModuleCreateInfo comp_ci = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = demo_composite_frag_spv_len,
        .pCode = (const uint32_t*)demo_composite_frag_spv
    };

    VkShaderModule vs = VK_NULL_HANDLE;
    VkShaderModule fs_bloom = VK_NULL_HANDLE;
    VkShaderModule fs_comp = VK_NULL_HANDLE;
    if (!check_vk(vkCreateShaderModule(a->device, &vs_ci, NULL, &vs), "vkCreateShaderModule(vs)")) {
        return 0;
    }
    if (!check_vk(vkCreateShaderModule(a->device, &bloom_ci, NULL, &fs_bloom), "vkCreateShaderModule(fs bloom)")) {
        vkDestroyShaderModule(a->device, vs, NULL);
        return 0;
    }
    if (!check_vk(vkCreateShaderModule(a->device, &comp_ci, NULL, &fs_comp), "vkCreateShaderModule(fs comp)")) {
        vkDestroyShaderModule(a->device, fs_bloom, NULL);
        vkDestroyShaderModule(a->device, vs, NULL);
        return 0;
    }

    VkPipelineShaderStageCreateInfo stages[2] = {
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = vs,
            .pName = "main"
        },
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = fs_bloom,
            .pName = "main"
        }
    };

    VkPipelineVertexInputStateCreateInfo vi = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
    };
    VkPipelineViewportStateCreateInfo vp = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1
    };
    VkPipelineRasterizationStateCreateInfo rs = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .lineWidth = 1.0f,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE
    };
    VkPipelineMultisampleStateCreateInfo ms = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT
    };
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
    VkPipelineColorBlendStateCreateInfo cb = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &cb_att
    };
    VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo ds = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 2,
        .pDynamicStates = dyn
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
        .pColorBlendState = &cb,
        .pDynamicState = &ds,
        .layout = a->post_layout,
        .renderPass = a->bloom_render_pass,
        .subpass = 0
    };
    if (!check_vk(vkCreateGraphicsPipelines(a->device, VK_NULL_HANDLE, 1, &gp, NULL, &a->bloom_pipeline), "vkCreateGraphicsPipelines(bloom)")) {
        vkDestroyShaderModule(a->device, fs_comp, NULL);
        vkDestroyShaderModule(a->device, fs_bloom, NULL);
        vkDestroyShaderModule(a->device, vs, NULL);
        return 0;
    }

    stages[1].module = fs_comp;
    cb_att.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    cb_att.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cb_att.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    cb_att.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    gp.renderPass = a->present_render_pass;
    if (!check_vk(vkCreateGraphicsPipelines(a->device, VK_NULL_HANDLE, 1, &gp, NULL, &a->composite_pipeline), "vkCreateGraphicsPipelines(composite)")) {
        vkDestroyShaderModule(a->device, fs_comp, NULL);
        vkDestroyShaderModule(a->device, fs_bloom, NULL);
        vkDestroyShaderModule(a->device, vs, NULL);
        return 0;
    }

    vkDestroyShaderModule(a->device, fs_comp, NULL);
    vkDestroyShaderModule(a->device, fs_bloom, NULL);
    vkDestroyShaderModule(a->device, vs, NULL);
    return 1;
#endif
}

static int init_scene_image_layout(app* a) {
    VkCommandBufferAllocateInfo alloc = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = a->command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1
    };
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (!check_vk(vkAllocateCommandBuffers(a->device, &alloc, &cmd), "vkAllocateCommandBuffers(init)")) {
        return 0;
    }

    VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    if (!check_vk(vkBeginCommandBuffer(cmd, &begin), "vkBeginCommandBuffer(init)")) {
        return 0;
    }

    VkImageMemoryBarrier to_transfer = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = a->scene_image,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1
        },
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT
    };
    vkCmdPipelineBarrier(
        cmd,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,
        0,
        NULL,
        0,
        NULL,
        1,
        &to_transfer
    );

    VkClearColorValue clear = {{0.0f, 0.0f, 0.0f, 1.0f}};
    VkImageSubresourceRange range = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0,
        .levelCount = 1,
        .baseArrayLayer = 0,
        .layerCount = 1
    };
    vkCmdClearColorImage(cmd, a->scene_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear, 1, &range);

    VkImageMemoryBarrier to_sample = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = a->scene_image,
        .subresourceRange = range,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT
    };
    vkCmdPipelineBarrier(
        cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0,
        0,
        NULL,
        0,
        NULL,
        1,
        &to_sample
    );

    if (!check_vk(vkEndCommandBuffer(cmd), "vkEndCommandBuffer(init)")) {
        return 0;
    }

    VkSubmitInfo submit = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &cmd
    };
    if (!check_vk(vkQueueSubmit(a->graphics_queue, 1, &submit, VK_NULL_HANDLE), "vkQueueSubmit(init)")) {
        return 0;
    }
    if (!check_vk(vkQueueWaitIdle(a->graphics_queue), "vkQueueWaitIdle(init)")) {
        return 0;
    }

    vkFreeCommandBuffers(a->device, a->command_pool, 1, &cmd);
    a->scene_initialized = 1;
    return 1;
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
    desc.api.vulkan.max_frames_in_flight = 2;
    desc.api.vulkan.raster_samples = 1;

    vg_result r = vg_context_create(&desc, &a->vg);
    if (r != VG_OK) {
        fprintf(stderr, "vg_context_create failed: %s\n", vg_result_string(r));
        return 0;
    }

    r = vg_path_create(a->vg, &a->wave_path);
    if (r != VG_OK) {
        fprintf(stderr, "vg_path_create failed: %s\n", vg_result_string(r));
        return 0;
    }

    vg_crt_profile crt = {0};
    if (a->crt_profile_valid) {
        crt = a->crt_profile;
    } else {
        vg_make_crt_profile(VG_CRT_PRESET_WOPR, &crt);
        crt.beam_core_width_px = 0.600001f;
        crt.beam_halo_width_px = 2.8f;
        crt.beam_intensity = 0.85f;
        crt.bloom_strength = 0.75f;
        crt.bloom_radius_px = 4.0f;
        crt.persistence_decay = 0.70f;
        crt.jitter_amount = 0.15f;
        crt.flicker_amount = 0.10f;
        crt.vignette_strength = 0.14f;
        crt.barrel_distortion = 0.02f;
        crt.scanline_strength = 0.12f;
        crt.noise_strength = 0.04f;
    }
    clamp_crt_profile(&crt);
    vg_set_crt_profile(a->vg, &crt);
    a->crt_profile = crt;
    a->crt_profile_valid = 1;
    return 1;
}

static void destroy_vg_context(app* a) {
    if (a->wave_path) {
        vg_path_destroy(a->wave_path);
        a->wave_path = NULL;
    }
    if (a->vg) {
        vg_context_destroy(a->vg);
        a->vg = NULL;
    }
}

static void destroy_swapchain_resources(app* a) {
    destroy_vg_context(a);

    if (a->bloom_pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(a->device, a->bloom_pipeline, NULL);
        a->bloom_pipeline = VK_NULL_HANDLE;
    }
    if (a->composite_pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(a->device, a->composite_pipeline, NULL);
        a->composite_pipeline = VK_NULL_HANDLE;
    }
    if (a->post_layout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(a->device, a->post_layout, NULL);
        a->post_layout = VK_NULL_HANDLE;
    }
    if (a->post_desc_pool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(a->device, a->post_desc_pool, NULL);
        a->post_desc_pool = VK_NULL_HANDLE;
    }
    if (a->post_desc_layout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(a->device, a->post_desc_layout, NULL);
        a->post_desc_layout = VK_NULL_HANDLE;
    }
    if (a->post_sampler != VK_NULL_HANDLE) {
        vkDestroySampler(a->device, a->post_sampler, NULL);
        a->post_sampler = VK_NULL_HANDLE;
    }

    if (a->scene_fb != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(a->device, a->scene_fb, NULL);
        a->scene_fb = VK_NULL_HANDLE;
    }
    if (a->bloom_fb != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(a->device, a->bloom_fb, NULL);
        a->bloom_fb = VK_NULL_HANDLE;
    }
    if (a->scene_render_pass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(a->device, a->scene_render_pass, NULL);
        a->scene_render_pass = VK_NULL_HANDLE;
    }
    if (a->bloom_render_pass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(a->device, a->bloom_render_pass, NULL);
        a->bloom_render_pass = VK_NULL_HANDLE;
    }

    if (a->scene_view != VK_NULL_HANDLE) {
        vkDestroyImageView(a->device, a->scene_view, NULL);
        a->scene_view = VK_NULL_HANDLE;
    }
    if (a->bloom_view != VK_NULL_HANDLE) {
        vkDestroyImageView(a->device, a->bloom_view, NULL);
        a->bloom_view = VK_NULL_HANDLE;
    }
    if (a->scene_image != VK_NULL_HANDLE) {
        vkDestroyImage(a->device, a->scene_image, NULL);
        a->scene_image = VK_NULL_HANDLE;
    }
    if (a->bloom_image != VK_NULL_HANDLE) {
        vkDestroyImage(a->device, a->bloom_image, NULL);
        a->bloom_image = VK_NULL_HANDLE;
    }
    if (a->scene_memory != VK_NULL_HANDLE) {
        vkFreeMemory(a->device, a->scene_memory, NULL);
        a->scene_memory = VK_NULL_HANDLE;
    }
    if (a->bloom_memory != VK_NULL_HANDLE) {
        vkFreeMemory(a->device, a->bloom_memory, NULL);
        a->bloom_memory = VK_NULL_HANDLE;
    }

    if (a->command_pool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(a->device, a->command_pool, NULL);
        a->command_pool = VK_NULL_HANDLE;
    }

    for (uint32_t i = 0; i < a->swapchain_image_count; ++i) {
        if (a->present_framebuffers[i] != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(a->device, a->present_framebuffers[i], NULL);
            a->present_framebuffers[i] = VK_NULL_HANDLE;
        }
    }
    if (a->present_render_pass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(a->device, a->present_render_pass, NULL);
        a->present_render_pass = VK_NULL_HANDLE;
    }

    for (uint32_t i = 0; i < a->swapchain_image_count; ++i) {
        if (a->swapchain_image_views[i] != VK_NULL_HANDLE) {
            vkDestroyImageView(a->device, a->swapchain_image_views[i], NULL);
            a->swapchain_image_views[i] = VK_NULL_HANDLE;
        }
    }
    if (a->swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(a->device, a->swapchain, NULL);
        a->swapchain = VK_NULL_HANDLE;
    }

    a->swapchain_image_count = 0;
    a->scene_initialized = 0;
}

static int create_swapchain_resources(app* a) {
    if (!create_swapchain(a) ||
        !create_swapchain_image_views(a) ||
        !create_render_passes(a) ||
        !create_offscreen_targets(a) ||
        !create_present_framebuffers(a) ||
        !create_command_pool_and_buffers(a) ||
        !create_post_resources(a) ||
        !init_scene_image_layout(a) ||
        !create_vg_context(a)) {
        return 0;
    }
    a->force_clear_frames = 3;
    return 1;
}

static int recreate_swapchain_resources(app* a) {
    int w = 0;
    int h = 0;
    SDL_Vulkan_GetDrawableSize(a->window, &w, &h);
    if (w == 0 || h == 0) {
        return 1;
    }

    if (!check_vk(vkDeviceWaitIdle(a->device), "vkDeviceWaitIdle(recreate)")) {
        return 0;
    }
    destroy_swapchain_resources(a);
    return create_swapchain_resources(a);
}

static void set_viewport_scissor(VkCommandBuffer cmd, uint32_t w, uint32_t h) {
    VkViewport vp = {.x = 0.0f, .y = 0.0f, .width = (float)w, .height = (float)h, .minDepth = 0.0f, .maxDepth = 1.0f};
    VkRect2D sc = {.offset = {0, 0}, .extent = {w, h}};
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor(cmd, 0, 1, &sc);
}

static void apply_selected_tweak(app* a, int dir) {
    vg_crt_profile crt = a->crt_profile_valid ? a->crt_profile : (vg_crt_profile){0};
    if (!a->crt_profile_valid) {
        vg_get_crt_profile(a->vg, &crt);
    }

    switch (a->selected_param) {
        case UI_PARAM_BLOOM_STRENGTH:
            crt.bloom_strength = clampf(crt.bloom_strength + 0.05f * (float)dir, 0.0f, 3.0f);
            break;
        case UI_PARAM_BLOOM_RADIUS:
            crt.bloom_radius_px = clampf(crt.bloom_radius_px + 0.35f * (float)dir, 0.0f, 14.0f);
            break;
        case UI_PARAM_PERSISTENCE:
            crt.persistence_decay = clampf(crt.persistence_decay + 0.005f * (float)dir, 0.70f, 0.985f);
            break;
        case UI_PARAM_JITTER:
            crt.jitter_amount = clampf(crt.jitter_amount + 0.02f * (float)dir, 0.0f, 1.5f);
            break;
        case UI_PARAM_FLICKER:
            crt.flicker_amount = clampf(crt.flicker_amount + 0.02f * (float)dir, 0.0f, 1.0f);
            break;
        case UI_PARAM_BEAM_CORE:
            crt.beam_core_width_px = clampf(crt.beam_core_width_px + 0.05f * (float)dir, 0.5f, 3.5f);
            break;
        case UI_PARAM_BEAM_HALO:
            crt.beam_halo_width_px = clampf(crt.beam_halo_width_px + 0.12f * (float)dir, 0.0f, 10.0f);
            break;
        case UI_PARAM_BEAM_INTENSITY:
            crt.beam_intensity = clampf(crt.beam_intensity + 0.05f * (float)dir, 0.2f, 3.0f);
            break;
        case UI_PARAM_VIGNETTE:
            crt.vignette_strength = clampf(crt.vignette_strength + 0.02f * (float)dir, 0.0f, 1.0f);
            break;
        case UI_PARAM_BARREL:
            crt.barrel_distortion = clampf(crt.barrel_distortion + 0.01f * (float)dir, 0.0f, 0.30f);
            break;
        case UI_PARAM_SCANLINE:
            crt.scanline_strength = clampf(crt.scanline_strength + 0.02f * (float)dir, 0.0f, 1.0f);
            break;
        case UI_PARAM_NOISE:
            crt.noise_strength = clampf(crt.noise_strength + 0.01f * (float)dir, 0.0f, 0.30f);
            break;
        case UI_PARAM_LINE_WIDTH:
            a->main_line_width = clampf(a->main_line_width + 0.25f * (float)dir, 1.0f, 16.0f);
            break;
        default:
            break;
    }
    clamp_crt_profile(&crt);
    vg_set_crt_profile(a->vg, &crt);
    a->crt_profile = crt;
    a->crt_profile_valid = 1;
}

static void step_selected_param(app* a, int dir) {
    if (dir > 0) {
        a->selected_param = (a->selected_param + 1) % UI_PARAM_COUNT;
    } else if (dir < 0) {
        a->selected_param = (a->selected_param + UI_PARAM_COUNT - 1) % UI_PARAM_COUNT;
    }
}

static void apply_selected_image_tweak(app* a, int dir) {
    switch (a->selected_image_param) {
        case IMAGE_UI_PARAM_THRESHOLD:
            a->image_threshold = clampf(a->image_threshold + 0.02f * (float)dir, 0.0f, 1.0f);
            break;
        case IMAGE_UI_PARAM_CONTRAST:
            a->image_contrast = clampf(a->image_contrast + 0.08f * (float)dir, 0.25f, 4.0f);
            break;
        case IMAGE_UI_PARAM_SCAN_PITCH:
            a->image_pitch_px = clampf(a->image_pitch_px + 0.10f * (float)dir, 1.0f, 10.0f);
            break;
        case IMAGE_UI_PARAM_MIN_WIDTH:
            a->image_min_width_px = clampf(a->image_min_width_px + 0.05f * (float)dir, 0.2f, 8.0f);
            if (a->image_max_width_px < a->image_min_width_px) {
                a->image_max_width_px = a->image_min_width_px;
            }
            break;
        case IMAGE_UI_PARAM_MAX_WIDTH:
            a->image_max_width_px = clampf(a->image_max_width_px + 0.06f * (float)dir, a->image_min_width_px, 12.0f);
            break;
        case IMAGE_UI_PARAM_JITTER:
            a->image_jitter_px = clampf(a->image_jitter_px + 0.05f * (float)dir, 0.0f, 3.0f);
            break;
        case IMAGE_UI_PARAM_BLOCK_W:
            a->image_block_cell_w_px = clampf(a->image_block_cell_w_px + 1.0f * (float)dir, 2.0f, 40.0f);
            break;
        case IMAGE_UI_PARAM_BLOCK_H:
            a->image_block_cell_h_px = clampf(a->image_block_cell_h_px + 1.0f * (float)dir, 2.0f, 48.0f);
            break;
        case IMAGE_UI_PARAM_BLOCK_LEVELS:
            a->image_block_levels += dir;
            if (a->image_block_levels < 2) a->image_block_levels = 2;
            if (a->image_block_levels > 32) a->image_block_levels = 32;
            break;
        case IMAGE_UI_PARAM_INVERT:
            if (dir != 0) {
                a->image_invert = !a->image_invert;
            }
            break;
        default:
            break;
    }
}

static void step_selected_image_param(app* a, int dir) {
    if (dir > 0) {
        a->selected_image_param = (a->selected_image_param + 1) % IMAGE_UI_PARAM_COUNT;
    } else if (dir < 0) {
        a->selected_image_param = (a->selected_image_param + IMAGE_UI_PARAM_COUNT - 1) % IMAGE_UI_PARAM_COUNT;
    }
}

static void apply_selected_text_tweak(app* a, int dir) {
    switch (a->selected_text_param) {
        case TEXT_UI_PARAM_BOX_WEIGHT:
            a->boxed_font_weight = clampf(a->boxed_font_weight + 0.06f * (float)dir, 0.25f, 3.0f);
            return;
        default:
            return;
    }
}

static void step_selected_text_param(app* a, int dir) {
    if (dir > 0) {
        a->selected_text_param = (a->selected_text_param + 1) % TEXT_UI_PARAM_COUNT;
    } else if (dir < 0) {
        a->selected_text_param = (a->selected_text_param + TEXT_UI_PARAM_COUNT - 1) % TEXT_UI_PARAM_COUNT;
    }
}

static void apply_selected_tweak_value01(app* a, int param, float value_01) {
    value_01 = clampf(value_01, 0.0f, 1.0f);
    vg_crt_profile crt = a->crt_profile_valid ? a->crt_profile : (vg_crt_profile){0};
    if (!a->crt_profile_valid) {
        vg_get_crt_profile(a->vg, &crt);
    }

    switch (param) {
        case UI_PARAM_BLOOM_STRENGTH: crt.bloom_strength = lerpf(0.0f, 3.0f, value_01); break;
        case UI_PARAM_BLOOM_RADIUS: crt.bloom_radius_px = lerpf(0.0f, 14.0f, value_01); break;
        case UI_PARAM_PERSISTENCE: crt.persistence_decay = lerpf(0.70f, 0.985f, value_01); break;
        case UI_PARAM_JITTER: crt.jitter_amount = lerpf(0.0f, 1.5f, value_01); break;
        case UI_PARAM_FLICKER: crt.flicker_amount = lerpf(0.0f, 1.0f, value_01); break;
        case UI_PARAM_BEAM_CORE: crt.beam_core_width_px = lerpf(0.5f, 3.5f, value_01); break;
        case UI_PARAM_BEAM_HALO: crt.beam_halo_width_px = lerpf(0.0f, 10.0f, value_01); break;
        case UI_PARAM_BEAM_INTENSITY: crt.beam_intensity = lerpf(0.2f, 3.0f, value_01); break;
        case UI_PARAM_VIGNETTE: crt.vignette_strength = lerpf(0.0f, 1.0f, value_01); break;
        case UI_PARAM_BARREL: crt.barrel_distortion = lerpf(0.0f, 0.30f, value_01); break;
        case UI_PARAM_SCANLINE: crt.scanline_strength = lerpf(0.0f, 1.0f, value_01); break;
        case UI_PARAM_NOISE: crt.noise_strength = lerpf(0.0f, 0.30f, value_01); break;
        case UI_PARAM_LINE_WIDTH: a->main_line_width = lerpf(1.0f, 16.0f, value_01); break;
        default: break;
    }
    clamp_crt_profile(&crt);
    vg_set_crt_profile(a->vg, &crt);
    a->crt_profile = crt;
    a->crt_profile_valid = 1;
}

static void apply_selected_image_tweak_value01(app* a, int param, float value_01) {
    value_01 = clampf(value_01, 0.0f, 1.0f);
    switch (param) {
        case IMAGE_UI_PARAM_THRESHOLD: a->image_threshold = lerpf(0.0f, 1.0f, value_01); break;
        case IMAGE_UI_PARAM_CONTRAST: a->image_contrast = lerpf(0.25f, 4.0f, value_01); break;
        case IMAGE_UI_PARAM_SCAN_PITCH: a->image_pitch_px = lerpf(1.0f, 10.0f, value_01); break;
        case IMAGE_UI_PARAM_MIN_WIDTH:
            a->image_min_width_px = lerpf(0.2f, 8.0f, value_01);
            if (a->image_max_width_px < a->image_min_width_px) a->image_max_width_px = a->image_min_width_px;
            break;
        case IMAGE_UI_PARAM_MAX_WIDTH:
            a->image_max_width_px = lerpf(0.2f, 12.0f, value_01);
            if (a->image_max_width_px < a->image_min_width_px) a->image_max_width_px = a->image_min_width_px;
            break;
        case IMAGE_UI_PARAM_JITTER: a->image_jitter_px = lerpf(0.0f, 3.0f, value_01); break;
        case IMAGE_UI_PARAM_BLOCK_W: a->image_block_cell_w_px = lerpf(2.0f, 40.0f, value_01); break;
        case IMAGE_UI_PARAM_BLOCK_H: a->image_block_cell_h_px = lerpf(2.0f, 48.0f, value_01); break;
        case IMAGE_UI_PARAM_BLOCK_LEVELS:
            a->image_block_levels = (int)lroundf(lerpf(2.0f, 32.0f, value_01));
            if (a->image_block_levels < 2) a->image_block_levels = 2;
            if (a->image_block_levels > 32) a->image_block_levels = 32;
            break;
        case IMAGE_UI_PARAM_INVERT:
            a->image_invert = value_01 >= 0.5f ? 1 : 0;
            break;
        default:
            break;
    }
}

static void apply_selected_text_tweak_value01(app* a, int param, float value_01) {
    value_01 = clampf(value_01, 0.0f, 1.0f);
    if (param == TEXT_UI_PARAM_BOX_WEIGHT) {
        a->boxed_font_weight = lerpf(0.25f, 3.0f, value_01);
    }
}

static int ui_kind_for_scene(const app* a) {
    if (a->scene_mode == SCENE_IMAGE_FX) {
        return 1;
    }
    if (a->scene_mode == SCENE_TITLE_CRAWL) {
        return 2;
    }
    return 0;
}

static int ui_kind_item_count(int ui_kind) {
    if (ui_kind == 1) return IMAGE_UI_PARAM_COUNT;
    if (ui_kind == 2) return TEXT_UI_PARAM_COUNT;
    return UI_PARAM_COUNT;
}

static float ui_kind_height(int ui_kind) {
    if (ui_kind == 1) return k_ui_image_h;
    if (ui_kind == 2) return k_ui_text_h;
    return k_ui_h;
}

static int point_in_rectf(float x, float y, vg_rect r) {
    return x >= r.x && y >= r.y && x <= (r.x + r.w) && y <= (r.y + r.h);
}

static void update_cursor_visibility(app* a) {
    if (!a) {
        return;
    }
    int show_system = (a->cursor_mode == CURSOR_MODE_SYSTEM) && a->mouse_in_window && !a->ui_drag_active;
    SDL_ShowCursor(show_system ? SDL_ENABLE : SDL_DISABLE);
}

static int handle_ui_mouse(app* a, float mouse_x, float mouse_y_vg, int pressed) {
    if (!a->show_ui) {
        return 0;
    }
    int ui_kind = ui_kind_for_scene(a);
    int item_count = ui_kind_item_count(ui_kind);
    vg_rect panel_rect = {k_ui_x, k_ui_y, k_ui_w, ui_kind_height(ui_kind)};
    if (!point_in_rectf(mouse_x, mouse_y_vg, panel_rect)) {
        return 0;
    }

    float left = panel_rect.x + 16.0f;
    float label_w = panel_rect.w * 0.40f;
    float slider_x = left + label_w + 16.0f;
    float slider_w = panel_rect.w - (slider_x - panel_rect.x) - 76.0f;
    float row_y = panel_rect.y + 70.0f;
    int hit = 0;
    for (int i = 0; i < item_count; ++i) {
        vg_rect row_rect = {left, row_y, panel_rect.w - 32.0f, k_ui_row_step - 10.0f};
        vg_rect slider_rect = {slider_x, row_y + 2.0f, slider_w, k_ui_row_step - 14.0f};
        if (point_in_rectf(mouse_x, mouse_y_vg, row_rect)) {
            hit = 1;
            if (ui_kind == 1) a->selected_image_param = i;
            else if (ui_kind == 2) a->selected_text_param = i;
            else a->selected_param = i;

            if (pressed && point_in_rectf(mouse_x, mouse_y_vg, slider_rect)) {
                float v01 = (mouse_x - slider_rect.x) / slider_rect.w;
                if (ui_kind == 1) apply_selected_image_tweak_value01(a, i, v01);
                else if (ui_kind == 2) apply_selected_text_tweak_value01(a, i, v01);
                else apply_selected_tweak_value01(a, i, v01);
                a->ui_drag_active = 1;
                a->ui_drag_kind = ui_kind;
                a->ui_drag_param = i;
                SDL_CaptureMouse(SDL_TRUE);
            }
            break;
        }
        row_y += k_ui_row_step;
    }
    return hit;
}

static void handle_ui_mouse_drag(app* a, float mouse_x, float mouse_y_vg) {
    (void)mouse_y_vg;
    if (!a->ui_drag_active) {
        return;
    }
    float panel_h = ui_kind_height(a->ui_drag_kind);
    float left = k_ui_x + 16.0f;
    float label_w = k_ui_w * 0.40f;
    float slider_x = left + label_w + 16.0f;
    float slider_w = k_ui_w - (slider_x - k_ui_x) - 76.0f;
    float row_y = k_ui_y + 70.0f + (float)a->ui_drag_param * k_ui_row_step;
    vg_rect slider_rect = {slider_x, row_y + 2.0f, slider_w, k_ui_row_step - 14.0f};
    if (panel_h <= 0.0f || slider_rect.w <= 0.0f) {
        return;
    }
    float v01 = (mouse_x - slider_rect.x) / slider_rect.w;
    if (a->ui_drag_kind == 1) apply_selected_image_tweak_value01(a, a->ui_drag_param, v01);
    else if (a->ui_drag_kind == 2) apply_selected_text_tweak_value01(a, a->ui_drag_param, v01);
    else apply_selected_tweak_value01(a, a->ui_drag_param, v01);
}

static void handle_ui_hold(app* a, float dt) {
    const Uint8* ks = SDL_GetKeyboardState(NULL);
    int adjust_dir = (ks[SDL_SCANCODE_RIGHT] ? 1 : 0) - (ks[SDL_SCANCODE_LEFT] ? 1 : 0);
    int nav_dir = (ks[SDL_SCANCODE_UP] ? 1 : 0) - (ks[SDL_SCANCODE_DOWN] ? 1 : 0);
    int image_ui = (a->scene_mode == SCENE_IMAGE_FX);
    int text_ui = (a->scene_mode == SCENE_TITLE_CRAWL);

    if (adjust_dir != 0) {
        if (adjust_dir != a->prev_adjust_dir) {
            if (image_ui) {
                apply_selected_image_tweak(a, adjust_dir);
            } else if (text_ui) {
                apply_selected_text_tweak(a, adjust_dir);
            } else {
                apply_selected_tweak(a, adjust_dir);
            }
            a->adjust_repeat_timer = 0.24f;
        } else {
            a->adjust_repeat_timer -= dt;
            while (a->adjust_repeat_timer <= 0.0f) {
                if (image_ui) {
                    apply_selected_image_tweak(a, adjust_dir);
                } else if (text_ui) {
                    apply_selected_text_tweak(a, adjust_dir);
                } else {
                    apply_selected_tweak(a, adjust_dir);
                }
                a->adjust_repeat_timer += 0.06f;
            }
        }
    } else {
        a->adjust_repeat_timer = 0.0f;
    }
    a->prev_adjust_dir = adjust_dir;

    if (nav_dir != 0) {
        if (nav_dir != a->prev_nav_dir) {
            if (image_ui) {
                step_selected_image_param(a, nav_dir);
            } else if (text_ui) {
                step_selected_text_param(a, nav_dir);
            } else {
                step_selected_param(a, nav_dir);
            }
            a->nav_repeat_timer = 0.24f;
        } else {
            a->nav_repeat_timer -= dt;
            while (a->nav_repeat_timer <= 0.0f) {
                if (image_ui) {
                    step_selected_image_param(a, nav_dir);
                } else if (text_ui) {
                    step_selected_text_param(a, nav_dir);
                } else {
                    step_selected_param(a, nav_dir);
                }
                a->nav_repeat_timer += 0.09f;
            }
        }
    } else {
        a->nav_repeat_timer = 0.0f;
    }
    a->prev_nav_dir = nav_dir;
}

static vg_result draw_debug_ui(app* a, const vg_crt_profile* crt, float fps) {
    vg_stroke_style panel = {
        .width_px = 2.0f,
        .intensity = 0.98f,
        .color = {1.0f, 0.56f, 0.12f, 0.98f},
        .cap = VG_LINE_CAP_BUTT,
        .join = VG_LINE_JOIN_BEVEL,
        .miter_limit = 2.0f,
        .blend = VG_BLEND_ALPHA
    };
    vg_stroke_style text = panel;
    text.width_px = 1.7f;
    text.intensity = 1.05f;
    text.cap = VG_LINE_CAP_ROUND;
    text.join = VG_LINE_JOIN_ROUND;
    text.blend = VG_BLEND_ALPHA;

    static const char* labels[UI_PARAM_COUNT] = {
        "BLOOM STR",
        "BLOOM RAD",
        "PERSISTENCE",
        "JITTER",
        "FLICKER",
        "BEAM CORE",
        "BEAM HALO",
        "BEAM INTENSITY",
        "VIGNETTE",
        "BARREL DISTORT",
        "SCANLINE",
        "NOISE",
        "LINE WIDTH PX"
    };
    float values[UI_PARAM_COUNT] = {
        crt->bloom_strength,
        crt->bloom_radius_px,
        crt->persistence_decay,
        crt->jitter_amount,
        crt->flicker_amount,
        crt->beam_core_width_px,
        crt->beam_halo_width_px,
        crt->beam_intensity,
        crt->vignette_strength,
        crt->barrel_distortion,
        crt->scanline_strength,
        crt->noise_strength,
        a->main_line_width
    };
    float values_norm[UI_PARAM_COUNT] = {
        norm_range(crt->bloom_strength, 0.0f, 3.0f),
        norm_range(crt->bloom_radius_px, 0.0f, 14.0f),
        norm_range(crt->persistence_decay, 0.70f, 0.985f),
        norm_range(crt->jitter_amount, 0.0f, 1.5f),
        norm_range(crt->flicker_amount, 0.0f, 1.0f),
        norm_range(crt->beam_core_width_px, 0.5f, 3.5f),
        norm_range(crt->beam_halo_width_px, 0.0f, 10.0f),
        norm_range(crt->beam_intensity, 0.2f, 3.0f),
        norm_range(crt->vignette_strength, 0.0f, 1.0f),
        norm_range(crt->barrel_distortion, 0.0f, 0.30f),
        norm_range(crt->scanline_strength, 0.0f, 1.0f),
        norm_range(crt->noise_strength, 0.0f, 0.30f),
        norm_range(a->main_line_width, 1.0f, 16.0f)
    };
    vg_ui_slider_item items[UI_PARAM_COUNT];
    for (int i = 0; i < UI_PARAM_COUNT; ++i) {
        items[i].label = labels[i];
        items[i].value_01 = values_norm[i];
        items[i].value_display = values[i];
        items[i].selected = (i == a->selected_param);
    }
    char footer[64];
    snprintf(footer, sizeof(footer), "FPS %.1f", fps);
    vg_ui_slider_panel_desc ui = {
        .rect = {k_ui_x, k_ui_y, k_ui_w, k_ui_h},
        .title_line_0 = "TAB UI  UP DOWN SELECT  LEFT RIGHT ADJUST",
        .title_line_1 = "1..8 SCENE  R REPLAY TTY  F5 SAVE  F9 LOAD",
        .footer_line = footer,
        .items = items,
        .item_count = UI_PARAM_COUNT,
        .row_height_px = k_ui_row_step,
        .label_size_px = 11.0f,
        .value_size_px = 11.5f,
        .border_style = panel,
        .text_style = text,
        .track_style = text,
        .knob_style = text
    };
    return vg_ui_draw_slider_panel(a->vg, &ui);
}

static vg_result draw_image_debug_ui(app* a, float fps) {
    vg_stroke_style panel = {
        .width_px = 2.0f,
        .intensity = 0.98f,
        .color = {1.0f, 0.56f, 0.12f, 0.98f},
        .cap = VG_LINE_CAP_BUTT,
        .join = VG_LINE_JOIN_BEVEL,
        .miter_limit = 2.0f,
        .blend = VG_BLEND_ALPHA
    };
    vg_stroke_style text = panel;
    text.width_px = 1.7f;
    text.intensity = 1.05f;
    text.cap = VG_LINE_CAP_ROUND;
    text.join = VG_LINE_JOIN_ROUND;
    text.blend = VG_BLEND_ALPHA;

    static const char* labels[IMAGE_UI_PARAM_COUNT] = {
        "THRESHOLD",
        "CONTRAST",
        "SCAN PITCH",
        "LINE MIN",
        "LINE MAX",
        "JITTER",
        "BLOCK CELL W",
        "BLOCK CELL H",
        "BLOCK LEVELS",
        "INVERT"
    };
    float values[IMAGE_UI_PARAM_COUNT] = {
        a->image_threshold,
        a->image_contrast,
        a->image_pitch_px,
        a->image_min_width_px,
        a->image_max_width_px,
        a->image_jitter_px,
        a->image_block_cell_w_px,
        a->image_block_cell_h_px,
        (float)a->image_block_levels,
        a->image_invert ? 1.0f : 0.0f
    };
    float values_norm[IMAGE_UI_PARAM_COUNT] = {
        norm_range(a->image_threshold, 0.0f, 1.0f),
        norm_range(a->image_contrast, 0.25f, 4.0f),
        norm_range(a->image_pitch_px, 1.0f, 10.0f),
        norm_range(a->image_min_width_px, 0.2f, 8.0f),
        norm_range(a->image_max_width_px, 0.2f, 12.0f),
        norm_range(a->image_jitter_px, 0.0f, 3.0f),
        norm_range(a->image_block_cell_w_px, 2.0f, 40.0f),
        norm_range(a->image_block_cell_h_px, 2.0f, 48.0f),
        norm_range((float)a->image_block_levels, 2.0f, 32.0f),
        a->image_invert ? 1.0f : 0.0f
    };
    vg_ui_slider_item items[IMAGE_UI_PARAM_COUNT];
    for (int i = 0; i < IMAGE_UI_PARAM_COUNT; ++i) {
        items[i].label = labels[i];
        items[i].value_01 = values_norm[i];
        items[i].value_display = values[i];
        items[i].selected = (i == a->selected_image_param);
    }
    char footer[64];
    snprintf(footer, sizeof(footer), "FPS %.1f", fps);
    vg_ui_slider_panel_desc ui = {
        .rect = {k_ui_x, k_ui_y, k_ui_w, k_ui_image_h},
        .title_line_0 = "IMAGE UI  UP DOWN SELECT  LEFT RIGHT ADJUST",
        .title_line_1 = "SCENE 8 IMAGE  TAB TOGGLE UI",
        .footer_line = footer,
        .items = items,
        .item_count = IMAGE_UI_PARAM_COUNT,
        .row_height_px = k_ui_row_step,
        .label_size_px = 11.0f,
        .value_size_px = 11.5f,
        .border_style = panel,
        .text_style = text,
        .track_style = text,
        .knob_style = text
    };
    return vg_ui_draw_slider_panel(a->vg, &ui);
}

static vg_result draw_text_debug_ui(app* a, float fps) {
    vg_stroke_style panel = {
        .width_px = 2.0f,
        .intensity = 0.98f,
        .color = {1.0f, 0.56f, 0.12f, 0.98f},
        .cap = VG_LINE_CAP_BUTT,
        .join = VG_LINE_JOIN_BEVEL,
        .miter_limit = 2.0f,
        .blend = VG_BLEND_ALPHA
    };
    vg_stroke_style text = panel;
    text.width_px = 1.7f;
    text.intensity = 1.05f;
    text.cap = VG_LINE_CAP_ROUND;
    text.join = VG_LINE_JOIN_ROUND;
    text.blend = VG_BLEND_ALPHA;

    static const char* labels[TEXT_UI_PARAM_COUNT] = {
        "BOX WEIGHT"
    };
    float values[TEXT_UI_PARAM_COUNT] = {
        a->boxed_font_weight
    };
    float values_norm[TEXT_UI_PARAM_COUNT] = {
        norm_range(a->boxed_font_weight, 0.25f, 3.0f)
    };
    vg_ui_slider_item items[TEXT_UI_PARAM_COUNT];
    for (int i = 0; i < TEXT_UI_PARAM_COUNT; ++i) {
        items[i].label = labels[i];
        items[i].value_01 = values_norm[i];
        items[i].value_display = values[i];
        items[i].selected = (i == a->selected_text_param);
    }
    char footer[64];
    snprintf(footer, sizeof(footer), "FPS %.1f", fps);
    vg_ui_slider_panel_desc ui = {
        .rect = {k_ui_x, k_ui_y, k_ui_w, k_ui_text_h},
        .title_line_0 = "TEXT UI  UP DOWN SELECT  LEFT RIGHT ADJUST",
        .title_line_1 = "SCENE 7 TEXT  TAB TOGGLE UI",
        .footer_line = footer,
        .items = items,
        .item_count = TEXT_UI_PARAM_COUNT,
        .row_height_px = k_ui_row_step,
        .label_size_px = 11.0f,
        .value_size_px = 11.5f,
        .border_style = panel,
        .text_style = text,
        .track_style = text,
        .knob_style = text
    };
    return vg_ui_draw_slider_panel(a->vg, &ui);
}

static vg_result draw_scene_classic(app* a, const vg_stroke_style* halo_s, const vg_stroke_style* main_s, float t, float cx, float cy, float jx, float jy) {
    (void)cx;
    (void)cy;
    (void)jx;
    (void)jy;

    float w = (float)a->swapchain_extent.width;
    float h = (float)a->swapchain_extent.height;
    float cpu = 52.0f + 42.0f * sinf(t * 0.92f);
    float mem = 60.0f + 22.0f * sinf(t * 0.43f + 1.3f);
    float net = 100.0f * (0.5f + 0.5f * sinf(t * 1.85f + 0.5f));
    float therm = 35.0f + 60.0f * (0.5f + 0.5f * sinf(t * 0.34f + 2.1f));
    float batt = 50.0f + 50.0f * sinf(t * 0.18f + 0.8f);

    vg_ui_meter_style ms;
    ms.frame = *main_s;
    ms.frame.blend = VG_BLEND_ALPHA;
    ms.frame.intensity = main_s->intensity * 0.85f;
    ms.bg = *halo_s;
    ms.bg.blend = VG_BLEND_ALPHA;
    ms.bg.intensity = halo_s->intensity * 0.45f;
    ms.fill = *main_s;
    ms.fill.blend = VG_BLEND_ADDITIVE;
    ms.fill.intensity = main_s->intensity * 1.15f;
    ms.tick = *main_s;
    ms.tick.blend = VG_BLEND_ALPHA;
    ms.tick.width_px = 1.0f;
    ms.tick.intensity = 0.9f;
    ms.text = ms.tick;
    ms.text.width_px = 1.25f;

    vg_ui_meter_desc d;
    d.min_value = 0.0f;
    d.max_value = 100.0f;
    d.mode = VG_UI_METER_SEGMENTED;
    d.segments = 18;
    d.segment_gap_px = 2.0f;
    d.value_fmt = "%5.1f";
    d.show_value = 1;
    d.show_ticks = 1;

    vg_result vr;
    d.rect = (vg_rect){w * 0.05f, h * 0.64f, w * 0.36f, 32.0f};
    d.label = "CPU %";
    d.value = cpu;
    vr = vg_ui_meter_linear(a->vg, &d, &ms);
    if (vr != VG_OK) return vr;

    d.rect = (vg_rect){w * 0.05f, h * 0.55f, w * 0.36f, 32.0f};
    d.label = "MEM %";
    d.value = mem;
    vr = vg_ui_meter_linear(a->vg, &d, &ms);
    if (vr != VG_OK) return vr;

    d.mode = VG_UI_METER_CONTINUOUS;
    d.rect = (vg_rect){w * 0.05f, h * 0.46f, w * 0.36f, 32.0f};
    d.label = "NET IN";
    d.value = net;
    vr = vg_ui_meter_linear(a->vg, &d, &ms);
    if (vr != VG_OK) return vr;

    d.mode = VG_UI_METER_SEGMENTED;
    d.segments = 12;
    d.segment_gap_px = 3.0f;
    d.label = "THERM";
    d.value = therm;
    vr = vg_ui_meter_radial(a->vg, (vg_vec2){w * 0.70f, h * 0.74f}, 106.0f, &d, &ms);
    if (vr != VG_OK) return vr;

    d.mode = VG_UI_METER_CONTINUOUS;
    d.label = "BATTERY";
    d.value = batt;
    vr = vg_ui_meter_radial(a->vg, (vg_vec2){w * 0.86f, h * 0.74f}, 80.0f, &d, &ms);
    if (vr != VG_OK) return vr;

    vg_ui_history_push(&a->cpu_hist, cpu);
    vg_ui_history_push(&a->net_hist, net);
    for (size_t i = 0; i < sizeof(a->fft_bins) / sizeof(a->fft_bins[0]); ++i) {
        float u = (float)i / (float)(sizeof(a->fft_bins) / sizeof(a->fft_bins[0]) - 1u);
        float env = 1.0f - fabsf(u * 2.0f - 1.0f) * 0.55f;
        float wob = sinf(t * (1.2f + u * 3.1f) + u * 9.0f) * 0.5f + 0.5f;
        a->fft_bins[i] = clampf(wob * env * 100.0f, 0.0f, 100.0f);
    }

    vg_ui_graph_style gs;
    gs.frame = ms.frame;
    gs.line = ms.fill;
    gs.line.width_px = 2.0f;
    gs.bar = ms.fill;
    gs.grid = ms.tick;
    gs.grid.intensity = 0.45f;
    gs.text = ms.text;

    float cpu_line[180];
    float net_line[180];
    size_t cpu_n = vg_ui_history_linearize(&a->cpu_hist, cpu_line, sizeof(cpu_line) / sizeof(cpu_line[0]));
    size_t net_n = vg_ui_history_linearize(&a->net_hist, net_line, sizeof(net_line) / sizeof(net_line[0]));

    vg_ui_graph_desc gd;
    gd.min_value = 0.0f;
    gd.max_value = 100.0f;
    gd.show_grid = 1;
    gd.show_minmax_labels = 0;

    gd.rect = (vg_rect){w * 0.05f, h * 0.16f, w * 0.36f, h * 0.20f};
    gd.samples = cpu_line;
    gd.sample_count = cpu_n > 0u ? cpu_n : 1u;
    gd.label = "CPU TREND";
    vr = vg_ui_graph_line(a->vg, &gd, &gs);
    if (vr != VG_OK) return vr;

    gd.rect = (vg_rect){w * 0.05f, h * 0.01f, w * 0.36f, h * 0.12f};
    gd.samples = net_line;
    gd.sample_count = net_n > 0u ? net_n : 1u;
    gd.label = "NET TREND";
    gd.show_minmax_labels = 0;
    vr = vg_ui_graph_line(a->vg, &gd, &gs);
    if (vr != VG_OK) return vr;

    gd.rect = (vg_rect){w * 0.52f, h * 0.08f, w * 0.40f, h * 0.18f};
    gd.samples = a->fft_bins;
    gd.sample_count = sizeof(a->fft_bins) / sizeof(a->fft_bins[0]);
    gd.label = "SPECTRUM";
    gd.show_grid = 0;
    gd.show_minmax_labels = 0;
    vr = vg_ui_graph_bars(a->vg, &gd, &gs);
    if (vr != VG_OK) return vr;

    float hist_bins[12];
    for (size_t i = 0; i < sizeof(hist_bins) / sizeof(hist_bins[0]); ++i) {
        float u = (float)i / (float)(sizeof(hist_bins) / sizeof(hist_bins[0]) - 1u);
        float wave = 0.55f + 0.45f * sinf(t * (0.9f + u * 1.5f) + u * 5.0f);
        float bump_a = expf(-16.0f * (u - 0.22f) * (u - 0.22f));
        float bump_b = expf(-18.0f * (u - 0.73f) * (u - 0.73f));
        hist_bins[i] = clampf((wave * 0.6f + (bump_a + bump_b) * 0.55f) * 100.0f, 2.0f, 100.0f);
    }
    vg_ui_histogram_desc hd;
    hd.rect = (vg_rect){w * 0.52f, h * 0.30f, w * 0.40f, h * 0.16f};
    hd.bins = hist_bins;
    hd.bin_count = sizeof(hist_bins) / sizeof(hist_bins[0]);
    hd.min_value = 0.0f;
    hd.max_value = 100.0f;
    hd.label = "BIN HISTOGRAM";
    hd.x_label = "FREQ";
    hd.y_label = "AMP";
    hd.show_grid = 1;
    hd.show_axes = 1;
    vr = vg_ui_histogram(a->vg, &hd, &gs);
    if (vr != VG_OK) return vr;

    float pie_values[5] = {
        12.0f + 7.0f * (sinf(t * 0.52f) * 0.5f + 0.5f),
        18.0f + 8.0f * (sinf(t * 0.77f + 0.7f) * 0.5f + 0.5f),
        22.0f + 6.0f * (sinf(t * 0.63f + 2.3f) * 0.5f + 0.5f),
        10.0f + 5.0f * (sinf(t * 1.13f + 1.2f) * 0.5f + 0.5f),
        14.0f + 4.0f * (sinf(t * 0.91f + 2.7f) * 0.5f + 0.5f)
    };
    vg_color pie_colors[5] = {
        {0.20f, 1.00f, 0.42f, 0.78f},
        {0.25f, 0.95f, 0.90f, 0.78f},
        {0.70f, 1.00f, 0.45f, 0.76f},
        {0.15f, 0.75f, 0.30f, 0.80f},
        {0.85f, 1.00f, 0.55f, 0.72f}
    };
    vg_ui_pie_desc pd;
    pd.center = (vg_vec2){w * 0.50f, h * 0.79f};
    pd.radius_px = 72.0f;
    static const char* pie_labels[5] = {"CPU", "GPU", "IO", "NET", "AUX"};
    pd.values = pie_values;
    pd.value_count = sizeof(pie_values) / sizeof(pie_values[0]);
    pd.colors = pie_colors;
    pd.labels = pie_labels;
    pd.label = NULL;
    pd.show_percent_labels = 1;
    vr = vg_ui_pie_chart(a->vg, &pd, &ms.frame, &ms.text);
    if (vr != VG_OK) return vr;

    vg_stroke_style ttl = ms.text;
    ttl.width_px = 1.5f;
    ttl.intensity = 1.2f;
    return vg_draw_text(a->vg, "INSTRUMENT BUS ACTIVE", (vg_vec2){w * 0.06f, h * 0.76f}, 17.0f, 1.0f, &ttl, NULL);
}

static vg_result draw_scene_wire_cube(app* a, const vg_stroke_style* halo_s, const vg_stroke_style* main_s, float t, float w, float h, float jx, float jy) {
    float rx = t * 0.7f;
    float ry = t * 1.1f;
    float crx = cosf(rx), srx = sinf(rx);
    float cry = cosf(ry), sry = sinf(ry);
    float s = 1.1f;

    float v[8][3] = {
        {-s, -s, -s}, {s, -s, -s}, {s, s, -s}, {-s, s, -s},
        {-s, -s, s},  {s, -s, s},  {s, s, s},  {-s, s, s}
    };
    vg_vec2 p[8];
    for (int i = 0; i < 8; ++i) {
        float x = v[i][0], y = v[i][1], z = v[i][2];
        float xz = x * cry - z * sry;
        float zz = x * sry + z * cry;
        float yz = y * crx - zz * srx;
        float zz2 = y * srx + zz * crx;
        p[i] = project_3d(xz, yz, zz2, w, h, h * 0.95f, 3.8f);
        p[i].x += jx;
        p[i].y += jy;
    }
    static const int edges[12][2] = {
        {0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}
    };
    for (int i = 0; i < 12; ++i) {
        vg_vec2 seg[2] = {p[edges[i][0]], p[edges[i][1]]};
        vg_result vr = vg_draw_polyline(a->vg, seg, 2, halo_s, 0);
        if (vr != VG_OK) return vr;
        vr = vg_draw_polyline(a->vg, seg, 2, main_s, 0);
        if (vr != VG_OK) return vr;
    }
    return VG_OK;
}

static vg_result draw_scene_starfield(app* a, const vg_stroke_style* halo_s, const vg_stroke_style* main_s, float dt, float w, float h) {
    if (!a->stars_initialized) {
        init_starfield(a);
    }
    vg_fill_style star_fill = {
        .intensity = 1.1f,
        .color = {0.30f, 1.0f, 0.45f, 0.85f},
        .blend = VG_BLEND_ADDITIVE
    };
    float speed = 1.45f;
    for (size_t i = 0; i < sizeof(a->stars) / sizeof(a->stars[0]); ++i) {
        star3* s = &a->stars[i];
        float z_prev = s->z;
        s->z -= dt * speed;
        if (s->z <= 0.08f) {
            s->x = rand_signed((uint32_t)(i * 211u + SDL_GetTicks())) * 2.5f;
            s->y = rand_signed((uint32_t)(i * 97u + SDL_GetTicks() * 3u)) * 1.4f;
            s->z = 2.0f;
            z_prev = s->z;
        }
        vg_vec2 p0 = project_3d(s->x, s->y, z_prev, w, h, h * 0.75f, 0.3f);
        vg_vec2 p1 = project_3d(s->x, s->y, s->z, w, h, h * 0.75f, 0.3f);
        vg_vec2 seg[2] = {p0, p1};
        vg_result vr = vg_draw_polyline(a->vg, seg, 2, halo_s, 0);
        if (vr != VG_OK) return vr;
        vr = vg_draw_polyline(a->vg, seg, 2, main_s, 0);
        if (vr != VG_OK) return vr;
        if (s->z < 0.35f) {
            vr = vg_fill_circle(a->vg, p1, 1.8f + (0.35f - s->z) * 4.0f, &star_fill, 14);
            if (vr != VG_OK) return vr;
        }
    }
    return VG_OK;
}

static vg_result draw_scene_surface(app* a, const vg_stroke_style* halo_s, const vg_stroke_style* main_s, float t, float w, float h) {
    const int n = 16;
    const float pitch = 0.62f;
    const float cp = cosf(pitch);
    const float sp = sinf(pitch);
    for (int pass = 0; pass < 2; ++pass) {
        for (int li = -n; li <= n; ++li) {
            vg_vec2 line[2 * n + 1];
            for (int si = -n; si <= n; ++si) {
                float x = pass == 0 ? (float)si * 0.24f : (float)li * 0.24f;
                float z = pass == 0 ? (float)li * 0.24f : (float)si * 0.24f;
                float y = 0.42f * sinf(2.1f * x + t * 0.85f) * cosf(1.7f * z + t * 0.62f);
                float yr = y * cp - z * sp;
                float zr = y * sp + z * cp + 2.8f;
                line[si + n] = project_3d(x, yr, zr, w, h, h * 0.92f, 2.9f);
            }
            vg_result vr = vg_draw_polyline(a->vg, line, 2u * n + 1u, halo_s, 0);
            if (vr != VG_OK) return vr;
            vr = vg_draw_polyline(a->vg, line, 2u * n + 1u, main_s, 0);
            if (vr != VG_OK) return vr;
        }
    }
    return VG_OK;
}

static vg_result draw_scene_synthwave(app* a, const vg_stroke_style* halo_s, const vg_stroke_style* main_s, float t, float w, float h) {
    vg_stroke_style frame = *main_s;
    frame.blend = VG_BLEND_ALPHA;
    frame.width_px = 1.4f;
    frame.intensity = 0.92f;

    vg_rect panel = {w * 0.08f, h * 0.10f, w * 0.84f, h * 0.74f};
    vg_result vr = vg_draw_rect(a->vg, panel, &frame);
    if (vr != VG_OK) {
        return vr;
    }

    if (!a->svg_asset) {
        vr = vg_draw_text(a->vg, "NO SVG FOUND IN ASSETS", (vg_vec2){w * 0.27f, h * 0.50f}, 20.0f, 1.1f, main_s, NULL);
        if (vr != VG_OK) {
            return vr;
        }
        return vg_draw_text(a->vg, "ADD SVG FILE AND RESTART DEMO", (vg_vec2){w * 0.20f, h * 0.44f}, 16.0f, 1.0f, &frame, NULL);
    }

    vg_stroke_style svg_halo = *halo_s;
    svg_halo.blend = VG_BLEND_ALPHA;
    svg_halo.intensity = halo_s->intensity * 0.60f;
    svg_halo.width_px = main_s->width_px * 2.2f;

    vg_stroke_style svg_main = *main_s;
    svg_main.blend = VG_BLEND_ADDITIVE;
    svg_main.intensity = main_s->intensity * 1.08f;
    svg_main.width_px = clampf(main_s->width_px * 0.95f, 0.9f, 2.6f);
    vg_palette ctx_pal;
    vg_get_palette(a->vg, &ctx_pal);
    vg_color bright_pal[3] = {
        {0.18f, 0.72f, 0.22f, 1.0f},
        {0.26f, 0.88f, 0.30f, 1.0f},
        {0.82f, 1.00f, 0.86f, 1.0f}
    };
    if (ctx_pal.count >= 5u) {
        bright_pal[0] = ctx_pal.entries[2].color;
        bright_pal[1] = ctx_pal.entries[3].color;
        bright_pal[2] = ctx_pal.entries[4].color;
    }
    float pulse = 0.96f + 0.08f * sinf(t * 0.9f);
    vg_svg_draw_params sp = {
        .dst = {
            panel.x + panel.w * 0.06f,
            panel.y + panel.h * 0.07f,
            panel.w * 0.88f * pulse,
            panel.h * 0.86f * pulse
        },
        .preserve_aspect = 1,
        .flip_y = 1,
        .fill_closed_paths = 1,
        .use_source_colors = 1,
        .fill_intensity = 1.10f,
        .stroke_intensity = 1.25f,
        .use_context_palette = 0,
        .palette = bright_pal,
        .palette_count = 3u
    };
    sp.dst.x += (panel.w * 0.88f - sp.dst.w) * 0.5f;
    sp.dst.y += (panel.h * 0.86f - sp.dst.h) * 0.5f;

    vr = vg_svg_draw(a->vg, a->svg_asset, &sp, &svg_halo);
    if (vr != VG_OK) {
        return vr;
    }
    vr = vg_svg_draw(a->vg, a->svg_asset, &sp, &svg_main);
    if (vr != VG_OK) {
        return vr;
    }

    vg_rect src_bounds = {0};
    (void)vg_svg_get_bounds(a->svg_asset, &src_bounds);

    char info[256];
    snprintf(
        info,
        sizeof(info),
        "SVG %d OF %d  SRC %.0fx%.0f  FIT %.0fx%.0f",
        a->svg_file_index + 1,
        a->svg_file_count,
        src_bounds.w,
        src_bounds.h,
        sp.dst.w,
        sp.dst.h
    );
    vr = vg_draw_text(a->vg, info, (vg_vec2){w * 0.10f, h * 0.06f}, 12.0f, 0.8f, &frame, NULL);
    if (vr != VG_OK) {
        return vr;
    }

    snprintf(
        info,
        sizeof(info),
        "FILE %s   SPACE NEXT SVG",
        a->svg_asset_name[0] ? a->svg_asset_name : "(unnamed)"
    );
    vr = vg_draw_text(a->vg, info, (vg_vec2){w * 0.10f, h * 0.03f}, 11.0f, 0.7f, &frame, NULL);
    if (vr != VG_OK) {
        return vr;
    }
    return vg_draw_text(a->vg, "MODE 5 SVG IMPORT PREVIEW", (vg_vec2){w * 0.10f, h * 0.84f}, 14.0f, 0.9f, &frame, NULL);
}

static vg_result draw_scene_fill_prims(app* a, float t, float w, float h) {
    vg_fill_style panel_fill = {.intensity = 0.75f, .color = {0.04f, 0.13f, 0.08f, 0.35f}, .blend = VG_BLEND_ALPHA};
    vg_fill_style sun_fill = {.intensity = 1.1f, .color = {0.95f, 1.00f, 0.42f, 0.56f}, .blend = VG_BLEND_ADDITIVE};
    vg_fill_style sun_core = {.intensity = 1.0f, .color = {1.00f, 0.90f, 0.22f, 0.72f}, .blend = VG_BLEND_ALPHA};
    vg_fill_style orbit_marker_fill = {.intensity = 1.0f, .color = {0.25f, 1.0f, 0.52f, 0.55f}, .blend = VG_BLEND_ADDITIVE};

    vg_stroke_style edge = {
        .width_px = 2.0f,
        .intensity = 1.05f,
        .color = {0.24f, 1.0f, 0.52f, 0.92f},
        .cap = VG_LINE_CAP_ROUND,
        .join = VG_LINE_JOIN_ROUND,
        .miter_limit = 2.0f,
        .blend = VG_BLEND_ADDITIVE
    };
    vg_stroke_style orbit = edge;
    orbit.width_px = 1.15f;
    orbit.intensity = 0.55f;
    orbit.blend = VG_BLEND_ALPHA;
    vg_stroke_style label = edge;
    label.width_px = 1.35f;
    label.intensity = 0.9f;
    label.blend = VG_BLEND_ALPHA;

    vg_result vr = vg_fill_rect(a->vg, (vg_rect){w * 0.04f, h * 0.10f, w * 0.64f, h * 0.78f}, &panel_fill);
    if (vr != VG_OK) return vr;
    vr = vg_draw_rect(a->vg, (vg_rect){w * 0.04f, h * 0.10f, w * 0.64f, h * 0.78f}, &orbit);
    if (vr != VG_OK) return vr;

    vg_rect side = {w * 0.72f, h * 0.16f, w * 0.24f, h * 0.62f};
    vr = vg_fill_rect(a->vg, side, &panel_fill);
    if (vr != VG_OK) return vr;
    vr = vg_draw_rect(a->vg, side, &orbit);
    if (vr != VG_OK) return vr;

    vg_vec2 c = {w * 0.36f, h * 0.49f};
    float base = h * 0.062f;
    vr = vg_fill_circle(a->vg, c, base * 1.45f, &sun_fill, 48);
    if (vr != VG_OK) return vr;
    vr = vg_fill_circle(a->vg, c, base, &sun_core, 42);
    if (vr != VG_OK) return vr;

    const char* names[5] = {"MERCURY", "VENUS", "EARTH", "MARS", "JUPITER"};
    float orbit_r[5] = {h * 0.11f, h * 0.16f, h * 0.22f, h * 0.29f, h * 0.37f};
    float planet_r[5] = {4.5f, 6.5f, 7.5f, 5.8f, 12.0f};
    float speed[5] = {1.5f, 1.15f, 0.95f, 0.78f, 0.55f};
    float phase[5] = {0.7f, 1.8f, 3.2f, 5.0f, 2.4f};
    vg_color pcol[5] = {
        {0.96f, 0.84f, 0.52f, 0.95f},
        {0.95f, 0.70f, 0.38f, 0.95f},
        {0.35f, 0.95f, 1.00f, 0.95f},
        {1.00f, 0.58f, 0.40f, 0.95f},
        {0.82f, 0.90f, 0.55f, 0.95f}
    };

    for (int i = 0; i < 5; ++i) {
        vr = vg_draw_polyline(
            a->vg,
            (vg_vec2[]){ {c.x - orbit_r[i], c.y}, {c.x + orbit_r[i], c.y} },
            2u,
            &orbit,
            0
        );
        if (vr != VG_OK) return vr;
        vr = vg_draw_polyline(
            a->vg,
            (vg_vec2[]){ {c.x, c.y - orbit_r[i]}, {c.x, c.y + orbit_r[i]} },
            2u,
            &orbit,
            0
        );
        if (vr != VG_OK) return vr;

        float a0 = t * speed[i] + phase[i];
        vg_vec2 p = {c.x + cosf(a0) * orbit_r[i], c.y + sinf(a0) * orbit_r[i]};
        vg_fill_style pf = {.intensity = 1.0f, .color = pcol[i], .blend = VG_BLEND_ADDITIVE};
        vr = vg_fill_circle(a->vg, p, planet_r[i] * 1.8f, &orbit_marker_fill, 22);
        if (vr != VG_OK) return vr;
        vr = vg_fill_circle(a->vg, p, planet_r[i], &pf, 22);
        if (vr != VG_OK) return vr;

        vg_vec2 label_anchor = {w * 0.73f, h * (0.24f + 0.10f * (float)i)};
        vr = vg_draw_polyline(a->vg, (vg_vec2[]){p, label_anchor}, 2u, &orbit, 0);
        if (vr != VG_OK) return vr;
        vr = vg_draw_text(a->vg, names[i], (vg_vec2){label_anchor.x + 8.0f, label_anchor.y - 5.0f}, 12.0f, 0.8f, &label, NULL);
        if (vr != VG_OK) return vr;

        char km[64];
        snprintf(km, sizeof(km), "R %.0f M KM", orbit_r[i] * 9.0f);
        vr = vg_draw_text(a->vg, km, (vg_vec2){side.x + 16.0f, label_anchor.y - 20.0f}, 10.0f, 0.6f, &orbit, NULL);
        if (vr != VG_OK) return vr;
    }

    vr = vg_draw_text(a->vg, "SOLAR DATA LINK", (vg_vec2){side.x + 16.0f, side.y + side.h - 28.0f}, 14.0f, 0.9f, &label, NULL);
    if (vr != VG_OK) return vr;
    vr = vg_draw_text(a->vg, "FILL + CIRCLE + CALLOUT DEMO", (vg_vec2){w * 0.06f, h * 0.83f}, 13.0f, 0.8f, &orbit, NULL);
    if (vr != VG_OK) return vr;
    return vg_draw_text(a->vg, "MODE 6", (vg_vec2){w * 0.06f, h * 0.79f}, 18.0f, 1.2f, &label, NULL);
}

static vg_result draw_scene_title_crawl(app* a, const vg_stroke_style* halo_s, const vg_stroke_style* main_s, float t, float w, float h) {
    vg_result vr;
    vg_stroke_style title_s = *main_s;
    title_s.width_px = main_s->width_px * 0.85f;
    title_s.intensity = main_s->intensity * 1.20f;
    title_s.blend = VG_BLEND_ADDITIVE;

    const char* title = "VECTOR WARS";
    float title_w = vg_measure_text_boxed(title, 52.0f, 4.0f);
    vr = vg_draw_text_boxed_weighted(a->vg, title, (vg_vec2){(w - title_w) * 0.5f, h * 0.83f}, 52.0f, 4.0f, &title_s, a->boxed_font_weight, NULL);
    if (vr != VG_OK) return vr;

    const char* rot = "RETRO";
    float rot_w = vg_measure_text_boxed(rot, 26.0f, 2.0f);
    float rot_h = 26.0f * 1.35f;
    vr = vg_transform_push(a->vg);
    if (vr != VG_OK) return vr;
    vg_transform_translate(a->vg, w * 0.5f, h * 0.66f);
    vg_transform_rotate(a->vg, t * 1.65f);
    vr = vg_draw_text_boxed_weighted(a->vg, rot, (vg_vec2){-rot_w * 0.5f, -rot_h * 0.5f}, 26.0f, 2.0f, &title_s, a->boxed_font_weight, NULL);
    if (vr != VG_OK) {
        (void)vg_transform_pop(a->vg);
        return vr;
    }
    vr = vg_transform_pop(a->vg);
    if (vr != VG_OK) return vr;

    static const char* crawl_lines[] = {
        "EPISODE VII",
        "THE VECTOR AWAKENS",
        "A SMALL GRAPHICS LIBRARY",
        "HAS EMBRACED VULKAN",
        "TO RECREATE GLOWING",
        "RETRO DISPLAY MAGIC.",
        "BLOOM SCANLINES AND",
        "PERSISTENCE FLICKER",
        "NOW POWER NEW DEMOS",
        "FOR GAMES AND UI."
    };
    const size_t crawl_count = sizeof(crawl_lines) / sizeof(crawl_lines[0]);
    float phase = fmodf(t * 0.12f, 1.0f);
    float y_base = h * 0.20f;
    float y_span = h * 0.38f;

    for (size_t i = 0; i < crawl_count; ++i) {
        float u = ((float)i + phase * (float)crawl_count) / (float)crawl_count;
        if (u > 1.0f) {
            u -= 1.0f;
        }
        float y = y_base + y_span * u * u;
        float size = 24.0f * (1.0f - u) + 10.0f * u;
        float tracking = 1.2f * (1.0f - u) + 0.55f * u;
        float line_w = vg_measure_text_boxed(crawl_lines[i], size, tracking);
        float center_x = w * 0.5f + (u - 0.5f) * 20.0f;
        vg_stroke_style crawl_s = *main_s;
        crawl_s.width_px = main_s->width_px * (0.80f - 0.32f * u);
        if (crawl_s.width_px < 0.9f) crawl_s.width_px = 0.9f;
        crawl_s.intensity = main_s->intensity * (1.12f - 0.36f * u);
        crawl_s.blend = VG_BLEND_ADDITIVE;

        vr = vg_draw_text_boxed_weighted(a->vg, crawl_lines[i], (vg_vec2){center_x - line_w * 0.5f, y}, size, tracking, &crawl_s, a->boxed_font_weight, NULL);
        if (vr != VG_OK) return vr;
    }

    vg_vec2 beam[2] = {{w * 0.18f, h * 0.64f}, {w * 0.82f, h * 0.64f}};
    vr = vg_draw_polyline(a->vg, beam, 2, halo_s, 0);
    if (vr != VG_OK) return vr;
    vr = vg_draw_polyline(a->vg, beam, 2, main_s, 0);
    if (vr != VG_OK) return vr;

    vg_stroke_style cmp = *main_s;
    cmp.blend = VG_BLEND_ALPHA;
    cmp.width_px = 1.4f;
    cmp.intensity = 1.0f;
    vg_fill_style cmp_panel_fill = {
        .intensity = 0.95f,
        .color = {0.14f, 0.68f, 0.30f, 0.72f},
        .blend = VG_BLEND_ALPHA
    };
    vg_stroke_style cmp_panel_border = cmp;
    cmp_panel_border.width_px = 1.1f;
    cmp_panel_border.intensity = 0.9f;

    float x = w * 0.06f;
    float y = h * 0.18f;
    vr = vg_draw_text(a->vg, "TEXT MODE 1", (vg_vec2){x, y}, 22.0f, 1.2f, &cmp, NULL);
    if (vr != VG_OK) return vr;
    vr = vg_draw_text_boxed(a->vg, "TEXT MODE 2", (vg_vec2){x, y + 34.0f}, 22.0f, 1.2f, &cmp, NULL);
    if (vr != VG_OK) return vr;
    vr = vg_draw_text_vector_fill(a->vg, "TEXT MODE 3", (vg_vec2){x, y + 68.0f}, 22.0f, 1.2f, &cmp, NULL);
    if (vr != VG_OK) return vr;
    vr = vg_draw_text_stencil_cutout(
        a->vg,
        "TEXT MODE 4",
        (vg_vec2){x, y + 102.0f},
        22.0f,
        1.2f,
        &cmp_panel_fill,
        &cmp_panel_border,
        &cmp,
        NULL
    );
    if (vr != VG_OK) return vr;

    vg_fill_style marq_bg = {.intensity = 1.0f, .color = {0.02f, 0.10f, 0.06f, 0.92f}, .blend = VG_BLEND_ALPHA};
    vg_stroke_style marq_bd = cmp;
    marq_bd.width_px = 1.2f;
    marq_bd.intensity = 0.85f;
    return vg_text_fx_marquee_draw(
        a->vg,
        &a->scene7_marquee,
        (vg_rect){w * 0.06f, h * 0.05f, w * 0.52f, 28.0f},
        14.0f,
        0.8f,
        VG_TEXT_DRAW_MODE_STROKE,
        &cmp,
        1.0f,
        &marq_bg,
        &marq_bd
    );
}

static vg_result draw_scene_image_fx(app* a, const vg_stroke_style* main_s, float w, float h) {
    if (!a->image_rgba || a->image_w == 0u || a->image_h == 0u) {
        return vg_draw_text(a->vg, "NICK.JPG NOT LOADED", (vg_vec2){w * 0.30f, h * 0.52f}, 20.0f, 1.2f, main_s, NULL);
    }

    vg_image_desc img = {
        .pixels_rgba8 = a->image_rgba,
        .width = a->image_w,
        .height = a->image_h,
        .stride_bytes = a->image_stride
    };
    vg_image_style s = {
        .kind = VG_IMAGE_STYLE_MONO_SCANLINE,
        .threshold = a->image_threshold,
        .contrast = a->image_contrast,
        .scanline_pitch_px = a->image_pitch_px,
        .min_line_width_px = a->image_min_width_px,
        .max_line_width_px = a->image_max_width_px,
        .line_jitter_px = a->image_jitter_px,
        .intensity = 1.0f,
        .tint_color = {0.22f, 1.0f, 0.40f, 1.0f},
        .blend = VG_BLEND_ADDITIVE,
        .use_crt_palette = 1,
        .invert = a->image_invert
    };

    vg_result vr = vg_draw_image_stylized(a->vg, &img, (vg_rect){w * 0.06f, h * 0.14f, w * 0.27f, h * 0.72f}, &s);
    if (vr != VG_OK) return vr;

    vg_image_style s_hard = s;
    s_hard.threshold = clampf(a->image_threshold + 0.08f, 0.0f, 1.0f);
    s_hard.contrast = a->image_contrast * 1.25f;
    s_hard.scanline_pitch_px = a->image_pitch_px + 0.7f;
    s_hard.min_line_width_px = a->image_min_width_px * 0.85f;
    s_hard.max_line_width_px = a->image_max_width_px * 0.92f;

    vg_rect high_contrast_rect = {w * 0.37f, h * 0.14f, w * 0.27f, h * 0.72f};
    if (a->svg_asset) {
        high_contrast_rect = (vg_rect){w * 0.37f, h * 0.44f, w * 0.27f, h * 0.42f};
    }
    vr = vg_draw_image_stylized(a->vg, &img, high_contrast_rect, &s_hard);
    if (vr != VG_OK) return vr;

    if (a->svg_asset) {
        vg_stroke_style svg_s = *main_s;
        svg_s.blend = VG_BLEND_ALPHA;
        svg_s.width_px = clampf(main_s->width_px * 0.9f, 0.8f, 2.0f);
        svg_s.intensity = 1.0f;
        vg_svg_draw_params sp = {
            .dst = {w * 0.37f, h * 0.14f, w * 0.27f, h * 0.25f},
            .preserve_aspect = 1,
            .flip_y = 1
        };
        vr = vg_svg_draw(a->vg, a->svg_asset, &sp, &svg_s);
        if (vr != VG_OK) return vr;
    }

    vg_image_style s_char = s;
    s_char.kind = VG_IMAGE_STYLE_BLOCK_GRAPHICS;
    s_char.threshold = clampf(a->image_threshold - 0.04f, 0.0f, 1.0f);
    s_char.contrast = a->image_contrast * 1.05f;
    s_char.cell_width_px = a->image_block_cell_w_px;
    s_char.cell_height_px = a->image_block_cell_h_px;
    s_char.block_levels = a->image_block_levels;
    s_char.intensity = 0.95f;
    s_char.blend = VG_BLEND_ALPHA;
    s_char.use_crt_palette = 0;
    s_char.use_context_palette = 1;
    s_char.palette_index = 3;
    s_char.use_boxed_glyphs = 0;
    vr = vg_draw_image_stylized(a->vg, &img, (vg_rect){w * 0.68f, h * 0.14f, w * 0.27f, h * 0.72f}, &s_char);
    if (vr != VG_OK) return vr;

    vg_stroke_style label = *main_s;
    label.blend = VG_BLEND_ALPHA;
    label.width_px = 1.2f;
    label.intensity = 1.0f;
    vr = vg_draw_text(a->vg, "BASE", (vg_vec2){w * 0.17f, h * 0.11f}, 12.0f, 0.8f, &label, NULL);
    if (vr != VG_OK) return vr;
    if (a->svg_asset) {
        vr = vg_draw_text(a->vg, "SVG PREVIEW", (vg_vec2){w * 0.44f, h * 0.11f}, 12.0f, 0.8f, &label, NULL);
    } else {
        vr = vg_draw_text(a->vg, "HIGH CONTRAST", (vg_vec2){w * 0.44f, h * 0.11f}, 12.0f, 0.8f, &label, NULL);
    }
    if (vr != VG_OK) return vr;
    vr = vg_draw_text(a->vg, "BLOCK GRAPH", (vg_vec2){w * 0.76f, h * 0.11f}, 12.0f, 0.8f, &label, NULL);
    if (vr != VG_OK) return vr;
    if (a->svg_asset) {
        vr = vg_draw_text(a->vg, "HIGH CONTRAST", (vg_vec2){w * 0.43f, h * 0.41f}, 12.0f, 0.8f, &label, NULL);
        if (vr != VG_OK) return vr;
        if (a->svg_asset_name[0] != '\0') {
            vr = vg_draw_text(a->vg, a->svg_asset_name, (vg_vec2){w * 0.42f, h * 0.36f}, 10.0f, 0.7f, &label, NULL);
            if (vr != VG_OK) return vr;
        }
    }

    char txt[256];
    snprintf(
        txt,
        sizeof(txt),
        "THR %.2f CTR %.2f PITCH %.2f MIN %.2f MAX %.2f BW %.0f BH %.0f LVL %d INV %s\nTAB UI  UP/DOWN SELECT  LEFT/RIGHT ADJUST",
        a->image_threshold,
        a->image_contrast,
        a->image_pitch_px,
        a->image_min_width_px,
        a->image_max_width_px,
        a->image_block_cell_w_px,
        a->image_block_cell_h_px,
        a->image_block_levels,
        a->image_invert ? "ON" : "OFF"
    );
    vg_text_layout layout;
    memset(&layout, 0, sizeof(layout));
    vg_text_layout_params lp = {
        .bounds = {w * 0.08f, h * 0.04f, w * 0.84f, 40.0f},
        .size_px = 12.0f,
        .letter_spacing_px = 0.8f,
        .line_height_px = 15.5f,
        .align = VG_TEXT_ALIGN_LEFT
    };
    vr = vg_text_layout_build(txt, &lp, &layout);
    if (vr != VG_OK) {
        return vr;
    }
    vr = vg_text_layout_draw(a->vg, &layout, VG_TEXT_DRAW_MODE_STROKE, &label, 1.0f, NULL, NULL);
    vg_text_layout_reset(&layout);
    return vr;
}

static vg_result draw_scene_mode(app* a, const vg_stroke_style* halo_s, const vg_stroke_style* main_s, float t, float dt, float w, float h, float cx, float cy, float jx, float jy) {
    switch (a->scene_mode) {
        case SCENE_WIREFRAME_CUBE:
            return draw_scene_wire_cube(a, halo_s, main_s, t, w, h, jx, jy);
        case SCENE_STARFIELD:
            return draw_scene_starfield(a, halo_s, main_s, dt, w, h);
        case SCENE_SURFACE_PLOT:
            return draw_scene_surface(a, halo_s, main_s, t, w, h);
        case SCENE_SYNTHWAVE:
            return draw_scene_synthwave(a, halo_s, main_s, t, w, h);
        case SCENE_FILL_PRIMS:
            return draw_scene_fill_prims(a, t, w, h);
        case SCENE_TITLE_CRAWL:
            return draw_scene_title_crawl(a, halo_s, main_s, t, w, h);
        case SCENE_IMAGE_FX:
            return draw_scene_image_fx(a, main_s, w, h);
        case SCENE_CLASSIC:
        default:
            return draw_scene_classic(a, halo_s, main_s, t, cx, cy, jx, jy);
    }
}

static vg_result draw_teletype_overlay(app* a, float w, float h) {
    (void)w;
    vg_stroke_style tty = {
        .width_px = 1.2f,
        .intensity = 0.95f,
        .color = {0.35f, 1.0f, 0.52f, 0.95f},
        .cap = VG_LINE_CAP_ROUND,
        .join = VG_LINE_JOIN_ROUND,
        .miter_limit = 2.0f,
        .blend = VG_BLEND_ALPHA
    };
    if (!a->tty_fx.text) {
        return VG_OK;
    }
    char buf[640];
    (void)vg_text_fx_typewriter_copy_visible(&a->tty_fx, buf, sizeof(buf));

    float x0 = 40.0f;
    float y0 = h - 44.0f;
    float lh = 18.0f;
    char line[256];
    size_t li = 0;
    int row = 0;

    for (size_t i = 0;; ++i) {
        char c = buf[i];
        if (c == '\n' || c == '\0') {
            line[li] = '\0';
            vg_result r;
            if (row == 0) {
                r = vg_draw_text_boxed_weighted(a->vg, line, (vg_vec2){x0, y0 - lh * (float)row}, 13.0f, 0.8f, &tty, a->boxed_font_weight, NULL);
            } else {
                r = vg_draw_text(a->vg, line, (vg_vec2){x0, y0 - lh * (float)row}, 13.0f, 0.8f, &tty, NULL);
            }
            if (r != VG_OK) {
                return r;
            }
            row++;
            li = 0;
            if (c == '\0') {
                break;
            }
            continue;
        }
        if (li + 1u < sizeof(line)) {
            line[li++] = c;
        }
    }
    return VG_OK;
}

static vg_result draw_pointer_overlay(app* a, const vg_stroke_style* main_s, float t) {
    if (!a->mouse_in_window) {
        return VG_OK;
    }
    if (a->cursor_mode == CURSOR_MODE_NONE || a->cursor_mode == CURSOR_MODE_SYSTEM) {
        return VG_OK;
    }
    (void)t;
    float h = (float)a->swapchain_extent.height;
    float py = h - (float)a->mouse_y;
    vg_stroke_style ps = *main_s;
    ps.blend = VG_BLEND_ALPHA;
    ps.width_px = main_s->width_px * 0.95f;
    ps.intensity = main_s->intensity * 1.05f;
    vg_fill_style pf = {
        .intensity = 1.0f,
        .color = {0.9f, 1.0f, 0.92f, 0.95f},
        .blend = VG_BLEND_ALPHA
    };
    float pointer_angle = 2.0943951f;
    vg_pointer_style style = VG_POINTER_ASTEROIDS;
    if (a->cursor_mode == CURSOR_MODE_VECTOR_CROSSHAIR) {
        pointer_angle = 0.0f;
        style = VG_POINTER_CROSSHAIR;
    }
    vg_pointer_desc pd = {
        .position = {(float)a->mouse_x, py},
        .size_px = 34.0f,
        .angle_rad = pointer_angle,
        .phase = 0.0f,
        .stroke = ps,
        .fill = pf,
        .use_fill = 1
    };
    return vg_draw_pointer(a->vg, style, &pd);
}

static frame_result record_and_submit(app* a, uint32_t image_index, float t, float dt, float fps) {
    VkCommandBuffer cmd = a->command_buffers[image_index];

    if (!check_vk(vkResetCommandBuffer(cmd, 0), "vkResetCommandBuffer")) {
        return FRAME_FAIL;
    }
    VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    if (!check_vk(vkBeginCommandBuffer(cmd, &begin), "vkBeginCommandBuffer")) {
        return FRAME_FAIL;
    }

    VkClearValue scene_clear = {.color = {{0.0f, 0.0f, 0.0f, 1.0f}}};
    VkRenderPassBeginInfo scene_rp = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = a->scene_render_pass,
        .framebuffer = a->scene_fb,
        .renderArea = {.offset = {0, 0}, .extent = a->swapchain_extent},
        .clearValueCount = 1,
        .pClearValues = &scene_clear
    };
    vkCmdBeginRenderPass(cmd, &scene_rp, VK_SUBPASS_CONTENTS_INLINE);

    vg_frame_desc frame = {
        .width = a->swapchain_extent.width,
        .height = a->swapchain_extent.height,
        .delta_time_s = dt,
        .command_buffer = (void*)cmd
    };
    vg_result vr = vg_begin_frame(a->vg, &frame);
    if (vr != VG_OK) {
        fprintf(stderr, "vg_begin_frame failed: %s\n", vg_result_string(vr));
        return FRAME_FAIL;
    }

    vg_crt_profile crt;
    vg_get_crt_profile(a->vg, &crt);
    float persistence = crt.persistence_decay;
    if (persistence < 0.0f) {
        persistence = 0.0f;
    }
    if (persistence > 1.0f) {
        persistence = 1.0f;
    }
    float frame_decay = powf(persistence, dt * 95.0f);
    float fade_alpha = 1.0f - frame_decay;
    if (fade_alpha < 0.025f) {
        fade_alpha = 0.025f;
    }
    if (a->force_clear_frames > 0) {
        fade_alpha = 1.0f;
        a->force_clear_frames--;
    }

    float flicker_n = rand_signed((uint32_t)(t * 1000.0f));
    float intensity_scale = 1.0f + crt.flicker_amount * flicker_n;
    if (intensity_scale < 0.0f) {
        intensity_scale = 0.0f;
    }
    float jx = crt.jitter_amount * 2.0f * rand_signed((uint32_t)(t * 1300.0f));
    float jy = crt.jitter_amount * 2.0f * rand_signed((uint32_t)(t * 1700.0f));

    vg_fill_style fade_fill = {
        .intensity = 1.0f,
        .color = {0.0f, 0.0f, 0.0f, fade_alpha},
        .blend = VG_BLEND_ALPHA
    };
    vr = vg_fill_rect(a->vg, (vg_rect){0.0f, 0.0f, (float)a->swapchain_extent.width, (float)a->swapchain_extent.height}, &fade_fill);
    if (vr != VG_OK) {
        fprintf(stderr, "vg_fill_rect(fade) failed: %s\n", vg_result_string(vr));
        return FRAME_FAIL;
    }

    float cx = (float)a->swapchain_extent.width * 0.5f;
    float cy = (float)a->swapchain_extent.height * 0.5f;

    vg_stroke_style halo_s = {
        .width_px = a->main_line_width * crt.beam_core_width_px + crt.beam_halo_width_px,
        .intensity = 0.42f * crt.beam_intensity * intensity_scale,
        .color = {0.2f, 1.0f, 0.35f, 0.45f},
        .cap = VG_LINE_CAP_ROUND,
        .join = VG_LINE_JOIN_ROUND,
        .miter_limit = 4.0f,
        .blend = VG_BLEND_ADDITIVE
    };
    vg_stroke_style main_s = {
        .width_px = a->main_line_width * crt.beam_core_width_px,
        .intensity = 1.2f * crt.beam_intensity * intensity_scale,
        .color = {0.2f, 1.0f, 0.35f, 1.0f},
        .cap = VG_LINE_CAP_ROUND,
        .join = VG_LINE_JOIN_ROUND,
        .miter_limit = 4.0f,
        .blend = VG_BLEND_ADDITIVE
    };

    vr = draw_scene_mode(
        a,
        &halo_s,
        &main_s,
        t,
        dt,
        (float)a->swapchain_extent.width,
        (float)a->swapchain_extent.height,
        cx,
        cy,
        jx,
        jy
    );
    if (vr != VG_OK) {
        fprintf(stderr, "draw_scene_mode failed: %s\n", vg_result_string(vr));
        return FRAME_FAIL;
    }

    update_teletype(a, dt);
    vr = draw_teletype_overlay(a, (float)a->swapchain_extent.width, (float)a->swapchain_extent.height);
    if (vr != VG_OK) {
        fprintf(stderr, "draw_teletype_overlay failed: %s\n", vg_result_string(vr));
        return FRAME_FAIL;
    }

    if (a->show_ui) {
        if (a->scene_mode == SCENE_IMAGE_FX) {
            vr = draw_image_debug_ui(a, fps);
        } else if (a->scene_mode == SCENE_TITLE_CRAWL) {
            vr = draw_text_debug_ui(a, fps);
        } else {
            vr = draw_debug_ui(a, &crt, fps);
        }
        if (vr != VG_OK) {
            fprintf(stderr, "draw_debug_ui failed: %s\n", vg_result_string(vr));
            return FRAME_FAIL;
        }
    }

    vr = draw_pointer_overlay(a, &main_s, t);
    if (vr != VG_OK) {
        fprintf(stderr, "draw_pointer_overlay failed: %s\n", vg_result_string(vr));
        return FRAME_FAIL;
    }

    vr = vg_end_frame(a->vg);
    if (vr != VG_OK) {
        fprintf(stderr, "vg_end_frame failed: %s\n", vg_result_string(vr));
        return FRAME_FAIL;
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

    post_pc pc = {0};
    pc.p0[0] = 1.0f / (float)a->swapchain_extent.width;
    pc.p0[1] = 1.0f / (float)a->swapchain_extent.height;
    pc.p0[2] = crt.bloom_strength;
    pc.p0[3] = crt.bloom_radius_px;
    pc.p1[0] = crt.vignette_strength;
    pc.p1[1] = crt.barrel_distortion;
    pc.p1[2] = (a->scene_mode == SCENE_IMAGE_FX) ? 0.0f : crt.scanline_strength;
    pc.p1[3] = crt.noise_strength;
    pc.p2[0] = t;
    pc.p2[1] = a->show_ui ? 1.0f : 0.0f;
    pc.p2[2] = k_ui_x / (float)a->swapchain_extent.width;
    float ui_h = k_ui_h;
    if (a->scene_mode == SCENE_IMAGE_FX) {
        ui_h = k_ui_image_h;
    } else if (a->scene_mode == SCENE_TITLE_CRAWL) {
        ui_h = k_ui_text_h;
    }
    pc.p3[0] = k_ui_w / (float)a->swapchain_extent.width;
    pc.p3[1] = ui_h / (float)a->swapchain_extent.height;
    /* UI drawing uses bottom-origin coordinates; composite UV mask expects top-origin. */
    pc.p2[3] = 1.0f - ((k_ui_y + ui_h) / (float)a->swapchain_extent.height);
    if (pc.p2[3] < 0.0f) {
        pc.p2[3] = 0.0f;
    }
    vkCmdPushConstants(cmd, a->post_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
    vkCmdDraw(cmd, 3, 1, 0, 0);
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

    if (!check_vk(vkEndCommandBuffer(cmd), "vkEndCommandBuffer")) {
        return FRAME_FAIL;
    }

    if (!check_vk(vkResetFences(a->device, 1, &a->in_flight), "vkResetFences")) {
        return FRAME_FAIL;
    }

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

    if (!check_vk(vkQueueSubmit(a->graphics_queue, 1, &submit, a->in_flight), "vkQueueSubmit")) {
        return FRAME_FAIL;
    }

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
        return FRAME_RECREATE;
    }
    if (!check_vk(pr, "vkQueuePresentKHR")) {
        return FRAME_FAIL;
    }
    return FRAME_OK;
}

static void cleanup(app* a) {
    if (a->image_rgba) {
        free(a->image_rgba);
        a->image_rgba = NULL;
    }
    if (a->svg_asset) {
        vg_svg_destroy(a->svg_asset);
        a->svg_asset = NULL;
    }
#if VG_DEMO_HAS_SDL_IMAGE
    IMG_Quit();
#endif
    if (a->audio_dev != 0) {
        SDL_CloseAudioDevice(a->audio_dev);
        a->audio_dev = 0;
    }

    if (a->device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(a->device);
        destroy_swapchain_resources(a);
    }

    if (a->in_flight != VK_NULL_HANDLE) {
        vkDestroyFence(a->device, a->in_flight, NULL);
    }
    if (a->render_finished != VK_NULL_HANDLE) {
        vkDestroySemaphore(a->device, a->render_finished, NULL);
    }
    if (a->image_available != VK_NULL_HANDLE) {
        vkDestroySemaphore(a->device, a->image_available, NULL);
    }

    if (a->device != VK_NULL_HANDLE) {
        vkDestroyDevice(a->device, NULL);
    }
    if (a->surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(a->instance, a->surface, NULL);
    }
    if (a->instance != VK_NULL_HANDLE) {
        vkDestroyInstance(a->instance, NULL);
    }

    if (a->window) {
        SDL_DestroyWindow(a->window);
    }
    SDL_Quit();
}

int main(void) {
    app a;
    memset(&a, 0, sizeof(a));
    a.show_ui = 0;
    a.selected_param = 0;
    a.selected_image_param = 0;
    a.main_line_width = 1.5f;
    a.boxed_font_weight = 0.25f;
    a.cursor_mode = CURSOR_MODE_VECTOR_CROSSHAIR;
    vg_text_fx_typewriter_set_rate(&a.tty_fx, 0.050f);
    a.cpu_hist.data = a.cpu_hist_buf;
    a.cpu_hist.capacity = sizeof(a.cpu_hist_buf) / sizeof(a.cpu_hist_buf[0]);
    vg_ui_history_reset(&a.cpu_hist);
    a.net_hist.data = a.net_hist_buf;
    a.net_hist.capacity = sizeof(a.net_hist_buf) / sizeof(a.net_hist_buf[0]);
    vg_ui_history_reset(&a.net_hist);
    a.image_threshold = 0.47f;
    a.image_contrast = 1.45f;
    a.image_pitch_px = 2.6f;
    a.image_min_width_px = 0.55f;
    a.image_max_width_px = 2.35f;
    a.image_jitter_px = 0.0f;
    a.image_block_cell_w_px = 8.0f;
    a.image_block_cell_h_px = 6.0f;
    a.image_block_levels = 16;
    a.image_invert = 0;
    a.mouse_in_window = 1;
    vg_text_fx_typewriter_set_beep(&a.tty_fx, teletype_beep_cb, &a);
    vg_text_fx_typewriter_set_beep_profile(&a.tty_fx, 900.0f, 55.0f, 0.028f, 0.17f);
    vg_text_fx_typewriter_enable_beep(&a.tty_fx, 1);
    vg_text_fx_marquee_set_text(&a.scene7_marquee, "MARQUEE HELPER: LONG TEXT SCROLLS WITHIN BOX   ");
    vg_text_fx_marquee_set_speed(&a.scene7_marquee, 70.0f);
    vg_text_fx_marquee_set_gap(&a.scene7_marquee, 48.0f);
    set_scene(&a, SCENE_WIREFRAME_CUBE);

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
#if VG_DEMO_HAS_SDL_IMAGE
    if ((IMG_Init(IMG_INIT_JPG | IMG_INIT_PNG) & (IMG_INIT_JPG | IMG_INIT_PNG)) == 0) {
        fprintf(stderr, "IMG_Init failed: %s\n", IMG_GetError());
    }
#endif
    init_profile_path(&a);
    init_teletype_audio(&a);
    init_image_asset(&a);
    init_svg_asset(&a);

    a.window = SDL_CreateWindow(
        "vectorgfx Vulkan example",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        APP_WIDTH,
        APP_HEIGHT,
        SDL_WINDOW_VULKAN | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
    );
    if (!a.window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        cleanup(&a);
        return 1;
    }
    update_cursor_visibility(&a);

    if (!create_instance(&a) ||
        !create_surface(&a) ||
        !pick_physical_device(&a) ||
        !create_device(&a) ||
        !create_sync(&a) ||
        !create_swapchain_resources(&a)) {
        cleanup(&a);
        return 1;
    }
    load_profile(&a);

    int running = 1;
    int need_recreate = 0;
    uint64_t last = SDL_GetPerformanceCounter();
    float freq = (float)SDL_GetPerformanceFrequency();

    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) {
                running = 0;
            } else if (ev.type == SDL_WINDOWEVENT) {
                if (ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED || ev.window.event == SDL_WINDOWEVENT_RESIZED) {
                    need_recreate = 1;
                } else if (ev.window.event == SDL_WINDOWEVENT_ENTER) {
                    a.mouse_in_window = 1;
                    update_cursor_visibility(&a);
                } else if (ev.window.event == SDL_WINDOWEVENT_LEAVE) {
                    a.mouse_in_window = 0;
                    update_cursor_visibility(&a);
                } else if (ev.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                    a.mouse_in_window = 0;
                    a.ui_drag_active = 0;
                    SDL_CaptureMouse(SDL_FALSE);
                    update_cursor_visibility(&a);
                }
            } else if (ev.type == SDL_MOUSEMOTION) {
                a.mouse_x = ev.motion.x;
                a.mouse_y = ev.motion.y;
                float my_vg = (float)a.swapchain_extent.height - (float)a.mouse_y;
                if (a.ui_drag_active) {
                    handle_ui_mouse_drag(&a, (float)a.mouse_x, my_vg);
                }
            } else if (ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_LEFT) {
                a.mouse_x = ev.button.x;
                a.mouse_y = ev.button.y;
                float my_vg = (float)a.swapchain_extent.height - (float)a.mouse_y;
                (void)handle_ui_mouse(&a, (float)a.mouse_x, my_vg, 1);
            } else if (ev.type == SDL_MOUSEBUTTONUP && ev.button.button == SDL_BUTTON_LEFT) {
                a.ui_drag_active = 0;
                SDL_CaptureMouse(SDL_FALSE);
                update_cursor_visibility(&a);
            } else if (ev.type == SDL_KEYDOWN && !ev.key.repeat) {
                if (ev.key.keysym.sym == SDLK_TAB) {
                    a.show_ui = !a.show_ui;
                } else if (ev.key.keysym.sym == SDLK_1) {
                    set_scene(&a, SCENE_CLASSIC);
                } else if (ev.key.keysym.sym == SDLK_2) {
                    set_scene(&a, SCENE_WIREFRAME_CUBE);
                } else if (ev.key.keysym.sym == SDLK_3) {
                    set_scene(&a, SCENE_STARFIELD);
                } else if (ev.key.keysym.sym == SDLK_4) {
                    set_scene(&a, SCENE_SURFACE_PLOT);
                } else if (ev.key.keysym.sym == SDLK_5) {
                    set_scene(&a, SCENE_SYNTHWAVE);
                } else if (ev.key.keysym.sym == SDLK_6) {
                    set_scene(&a, SCENE_FILL_PRIMS);
                } else if (ev.key.keysym.sym == SDLK_7) {
                    set_scene(&a, SCENE_TITLE_CRAWL);
                } else if (ev.key.keysym.sym == SDLK_8) {
                    set_scene(&a, SCENE_IMAGE_FX);
                } else if (ev.key.keysym.sym == SDLK_SPACE) {
                    cycle_svg_asset(&a, +1);
                } else if (ev.key.keysym.sym == SDLK_p) {
                    a.cursor_mode = (a.cursor_mode + 1) % 4;
                    update_cursor_visibility(&a);
                } else if (ev.key.keysym.sym == SDLK_F5) {
                    save_profile(&a);
                } else if (ev.key.keysym.sym == SDLK_F9) {
                    load_profile(&a);
                } else if (ev.key.keysym.sym == SDLK_r) {
                    reset_teletype(&a);
                }
            }
        }

        uint64_t now = SDL_GetPerformanceCounter();
        float dt = (float)(now - last) / freq;
        last = now;
        if (dt <= 0.0f) {
            dt = 1.0f / 60.0f;
        }
        if (!a.ui_drag_active) {
            SDL_GetMouseState(&a.mouse_x, &a.mouse_y);
        }
        if (a.show_ui) {
            handle_ui_hold(&a, dt);
        } else {
            a.prev_adjust_dir = 0;
            a.prev_nav_dir = 0;
            a.adjust_repeat_timer = 0.0f;
            a.nav_repeat_timer = 0.0f;
        }
        float fps_inst = 1.0f / dt;
        if (a.fps_smoothed <= 0.0f) {
            a.fps_smoothed = fps_inst;
        } else {
            a.fps_smoothed += (fps_inst - a.fps_smoothed) * 0.10f;
        }

        if (need_recreate) {
            if (!recreate_swapchain_resources(&a)) {
                break;
            }
            need_recreate = 0;
            continue;
        }

        if (!check_vk(vkWaitForFences(a.device, 1, &a.in_flight, VK_TRUE, UINT64_MAX), "vkWaitForFences")) {
            break;
        }

        uint32_t image_index = 0;
        VkResult ar = vkAcquireNextImageKHR(a.device, a.swapchain, UINT64_MAX, a.image_available, VK_NULL_HANDLE, &image_index);
        if (ar == VK_ERROR_OUT_OF_DATE_KHR) {
            need_recreate = 1;
            continue;
        }
        if (ar == VK_SUBOPTIMAL_KHR) {
            need_recreate = 1;
        }
        if (!check_vk(ar, "vkAcquireNextImageKHR")) {
            break;
        }

        float t = (float)SDL_GetTicks() * 0.001f;
        frame_result fr = record_and_submit(&a, image_index, t, dt, a.fps_smoothed);
        if (fr == FRAME_RECREATE) {
            need_recreate = 1;
            continue;
        }
        if (fr == FRAME_FAIL) {
            break;
        }
    }

    cleanup(&a);
    return 0;
}
