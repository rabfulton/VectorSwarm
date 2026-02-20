#ifndef V_TYPE_RENDER_H
#define V_TYPE_RENDER_H

#include "game.h"
#include "planetarium/planetarium_types.h"
#include "vg.h"
#include <stddef.h>
#include <stdint.h>

#define ACOUSTICS_SLIDER_COUNT 14
#define ACOUSTICS_COMBAT_SLIDER_COUNT 16
#define ACOUSTICS_SCOPE_SAMPLES 192
#define ACOUSTICS_SLOT_COUNT 5
#define VIDEO_MENU_RES_COUNT 6
#define VIDEO_MENU_DIAL_COUNT 12
#define CONTROL_ACTION_COUNT_RENDER 6
#ifndef LEVEL_EDITOR_MAX_MARKERS
#define LEVEL_EDITOR_MAX_MARKERS 256
#endif
#define WORMHOLE_GPU_MAX_VERTS 8192u
#define WORMHOLE_GPU_MAX_TRI_VERTS 6144u

#define CRT_RANGE_BLOOM_STRENGTH_MIN 0.0f
#define CRT_RANGE_BLOOM_STRENGTH_MAX 1.8f
#define CRT_RANGE_BLOOM_RADIUS_MIN 0.0f
#define CRT_RANGE_BLOOM_RADIUS_MAX 8.0f
#define CRT_RANGE_PERSISTENCE_MIN 0.10f
#define CRT_RANGE_PERSISTENCE_MAX 0.92f
#define CRT_RANGE_JITTER_MIN 0.0f
#define CRT_RANGE_JITTER_MAX 0.40f
#define CRT_RANGE_FLICKER_MIN 0.0f
#define CRT_RANGE_FLICKER_MAX 1.0f
#define CRT_RANGE_BEAM_CORE_MIN 0.5f
#define CRT_RANGE_BEAM_CORE_MAX 2.2f
#define CRT_RANGE_BEAM_HALO_MIN 0.0f
#define CRT_RANGE_BEAM_HALO_MAX 5.0f
#define CRT_RANGE_BEAM_INTENSITY_MIN 0.4f
#define CRT_RANGE_BEAM_INTENSITY_MAX 1.6f
#define CRT_RANGE_SCANLINE_MIN 0.0f
#define CRT_RANGE_SCANLINE_MAX 0.45f
#define CRT_RANGE_NOISE_MIN 0.0f
#define CRT_RANGE_NOISE_MAX 0.14f
#define CRT_RANGE_VIGNETTE_MIN 0.0f
#define CRT_RANGE_VIGNETTE_MAX 0.45f
#define CRT_RANGE_BARREL_MIN 0.0f
#define CRT_RANGE_BARREL_MAX 0.10f
typedef struct render_metrics {
    float fps;
    float dt;
    int show_fps;
    float ui_time_s;
    int force_clear;
    int show_crt_ui;
    int crt_ui_selected;
    const char* teletype_text;
    const char* planetarium_marquee_text;
    float planetarium_marquee_offset_px;
    int menu_screen;
    int video_menu_selected;
    int video_menu_fullscreen;
    int palette_mode;
    float video_dial_01[VIDEO_MENU_DIAL_COUNT];
    int acoustics_selected;
    int acoustics_page;
    int acoustics_combat_selected;
    int acoustics_fire_slot_selected;
    int acoustics_thr_slot_selected;
    const planetary_system_def* planetarium_system;
    int planetarium_selected;
    int planetarium_system_count;
    int planetarium_systems_quelled;
    int planetarium_nodes_quelled[PLANETARIUM_MAX_SYSTEMS];
    int mouse_in_window;
    float mouse_x;
    float mouse_y;
    int acoustics_fire_slot_defined[ACOUSTICS_SLOT_COUNT];
    int acoustics_thr_slot_defined[ACOUSTICS_SLOT_COUNT];
    float acoustics_value_01[ACOUSTICS_SLIDER_COUNT];
    float acoustics_display[ACOUSTICS_SLIDER_COUNT];
    float acoustics_combat_value_01[ACOUSTICS_COMBAT_SLIDER_COUNT];
    float acoustics_combat_display[ACOUSTICS_COMBAT_SLIDER_COUNT];
    float acoustics_scope[ACOUSTICS_SCOPE_SAMPLES];
    int video_res_w[VIDEO_MENU_RES_COUNT];
    int video_res_h[VIDEO_MENU_RES_COUNT];
    const uint8_t* nick_rgba8;
    uint32_t nick_w;
    uint32_t nick_h;
    uint32_t nick_stride;
    int shipyard_weapon_selected;
    const void* shipyard_ship_svg_asset;
    const void* shipyard_weapon_svg_assets[4];
    const void* surveillance_svg_asset;
    const char* terrain_tuning_text;
    int use_gpu_terrain;
    int use_gpu_particles;
    int use_gpu_wormhole;
    int scene_phase; /* 0=full, 1=background-only, 2=foreground-only, 3=overlay-no-clear */
    const char* level_editor_level_name;
    const char* level_editor_status_text;
    float level_editor_timeline_01;
    float level_editor_level_length_screens;
    int level_editor_wave_mode;
    int level_editor_render_style;
    int level_editor_selected_marker;
    int level_editor_selected_property;
    int level_editor_tool_selected;
    int level_editor_drag_active;
    int level_editor_drag_kind;
    float level_editor_drag_x;
    float level_editor_drag_y;
    int level_editor_marker_count;
    float level_editor_marker_x01[LEVEL_EDITOR_MAX_MARKERS];
    float level_editor_marker_y01[LEVEL_EDITOR_MAX_MARKERS];
    int level_editor_marker_kind[LEVEL_EDITOR_MAX_MARKERS];
    float level_editor_marker_a[LEVEL_EDITOR_MAX_MARKERS];
    float level_editor_marker_b[LEVEL_EDITOR_MAX_MARKERS];
    float level_editor_marker_c[LEVEL_EDITOR_MAX_MARKERS];
    float level_editor_marker_d[LEVEL_EDITOR_MAX_MARKERS];
    int controls_selected;
    int controls_selected_column;
    int controls_rebinding_action;
    int controls_rebinding_column;
    int controls_use_gamepad;
    const char* controls_pad_name;
    const char* controls_action_label[CONTROL_ACTION_COUNT_RENDER];
    const char* controls_key_label[CONTROL_ACTION_COUNT_RENDER];
    const char* controls_pad_label[CONTROL_ACTION_COUNT_RENDER];
} render_metrics;

typedef struct wormhole_line_vertex {
    float x;
    float y;
    float z;
    float fade;
} wormhole_line_vertex;

size_t render_build_event_horizon_gpu_lines(const game_state* g, wormhole_line_vertex* out, size_t out_cap);
size_t render_build_event_horizon_gpu_tris(const game_state* g, wormhole_line_vertex* out, size_t out_cap);

vg_result render_frame(vg_context* ctx, const game_state* g, const render_metrics* metrics);

#endif
