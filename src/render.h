#ifndef V_TYPE_RENDER_H
#define V_TYPE_RENDER_H

#include "game.h"
#include "planetarium/planetarium_types.h"
#include "vg.h"
#include <stddef.h>
#include <stdint.h>

#define ACOUSTICS_SLIDER_COUNT 14
#define ACOUSTICS_COMBAT_SLIDER_COUNT 16
#define ACOUSTICS_EQUIPMENT_SLIDER_COUNT 16
#define ACOUSTICS_EFFECTS_SLIDER_COUNT 16
#define ACOUSTICS_SCOPE_SAMPLES 192
#define ACOUSTICS_SLOT_COUNT 5
#define MIXTAPE_MAX_TRACKS 128
#define MIXTAPE_LABEL_CAP 96
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
    int video_menu_high_quality;
    int palette_mode;
    float video_dial_01[VIDEO_MENU_DIAL_COUNT];
    int acoustics_selected;
    int acoustics_page;
    int acoustics_combat_selected;
    int acoustics_equipment_selected;
    int acoustics_effects_selected;
    int acoustics_mixtape_selected;
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
    float acoustics_equipment_value_01[ACOUSTICS_EQUIPMENT_SLIDER_COUNT];
    float acoustics_equipment_display[ACOUSTICS_EQUIPMENT_SLIDER_COUNT];
    float acoustics_effects_value_01[ACOUSTICS_EFFECTS_SLIDER_COUNT];
    float acoustics_effects_display[ACOUSTICS_EFFECTS_SLIDER_COUNT];
    float acoustics_mixtape_volume_01;
    float acoustics_mixtape_volume_display;
    int acoustics_mixtape_track_index;
    int acoustics_mixtape_track_count;
    int acoustics_mixtape_playing;
    const char* acoustics_mixtape_track_name;
    int acoustics_mixtape_focus; /* 0=library(left), 1=playlist(right) */
    int acoustics_mixtape_playlist_selected;
    int acoustics_mixtape_playlist_count;
    int acoustics_mixtape_randomize;
    int acoustics_mixtape_drag_active;
    int acoustics_mixtape_drag_source;
    int acoustics_mixtape_drag_target;
    int acoustics_mixtape_playlist_indices[MIXTAPE_MAX_TRACKS];
    char acoustics_mixtape_track_labels[MIXTAPE_MAX_TRACKS][MIXTAPE_LABEL_CAP];
    const void* acoustics_tape_svg_asset;
    float acoustics_scope[ACOUSTICS_SCOPE_SAMPLES];
    int video_res_w[VIDEO_MENU_RES_COUNT];
    int video_res_h[VIDEO_MENU_RES_COUNT];
    const uint8_t* nick_rgba8;
    uint32_t nick_w;
    uint32_t nick_h;
    uint32_t nick_stride;
    float nick_threshold;
    float nick_contrast;
    float nick_scanline_pitch_px;
    float nick_min_line_width_px;
    float nick_max_line_width_px;
    float nick_intensity;
    int nick_invert;
    int shipyard_weapon_selected;
    int shipyard_nav_column; /* 0=left links, 1=weapon column */
    int shipyard_link_selected; /* 0..3 top-to-bottom */
    int shipyard_weapon_ammo[PLAYER_ALT_WEAPON_COUNT];
    const void* shipyard_ship_svg_asset;
    const void* shipyard_weapon_svg_assets[4];
    const void* surveillance_svg_asset;
    int opening_menu_selected;
    const float* opening_ship_positions_xyz;
    uint32_t opening_ship_vertex_count;
    const uint32_t* opening_ship_edges;
    uint32_t opening_ship_edge_count;
    float opening_ship_yaw_deg;
    float opening_ship_pitch_deg;
    float opening_ship_roll_deg;
    float opening_ship_scale;
    float opening_ship_spin_rate;
    int opening_ship_spin_axis; /* 0=yaw, 1=pitch, 2=roll */
    const char* terrain_tuning_text;
    int use_gpu_terrain;
    int use_gpu_particles;
    int use_gpu_wormhole;
    int use_gpu_radar;
    int use_gpu_arc;
    int use_gpu_industry;
    int scene_phase; /* 0=full, 1=background-only, 2=foreground-only, 3=overlay-no-clear */
    const char* level_editor_level_name;
    const char* level_editor_status_text;
    float level_editor_timeline_01;
    float level_editor_level_length_screens;
    int level_editor_wave_mode;
    int level_editor_render_style;
    int level_editor_theme_palette;
    int level_editor_enemy_palette;
    int level_editor_background_style;
    int level_editor_background_mask_style;
    int level_editor_asteroid_storm_enabled;
    float level_editor_asteroid_storm_angle_deg;
    float level_editor_asteroid_storm_speed;
    float level_editor_asteroid_storm_duration_s;
    float level_editor_asteroid_storm_density;
    float level_editor_powerup_drop_chance;
    float level_editor_kamikaze_radius_min;
    float level_editor_kamikaze_radius_max;
    int level_editor_kamikaze_style;
    float level_editor_curated_formation_fire_prob_mul;
    float level_editor_curated_formation_cooldown_mul;
    int level_editor_curated_formation_shot_count;
    float level_editor_curated_formation_aim_error_mul;
    float level_editor_curated_formation_projectile_speed_mul;
    float level_editor_curated_formation_spread_mul;
    float level_editor_curated_swarm_fire_prob_mul;
    float level_editor_curated_swarm_spread_prob_mul;
    float level_editor_curated_swarm_cooldown_mul;
    int level_editor_curated_swarm_shot_count;
    float level_editor_curated_swarm_aim_error_mul;
    float level_editor_curated_swarm_projectile_speed_mul;
    float level_editor_curated_swarm_spread_mul;
    float level_editor_curated_kamikaze_fire_prob_mul;
    float level_editor_curated_kamikaze_speed_mul;
    float level_editor_curated_kamikaze_accel_mul;
    float level_editor_curated_manta_fire_prob_mul;
    int level_editor_curated_manta_missile_count_bonus;
    float level_editor_curated_manta_missile_cooldown_mul;
    float level_editor_curated_manta_missile_charge_mul;
    float level_editor_curated_eel_fire_prob_mul;
    float level_editor_curated_eel_arc_fire_rate_mul;
    float level_editor_curated_eel_arc_duration_mul;
    float level_editor_curated_eel_arc_range_mul;
    float level_editor_curated_eel_arc_damage_interval_mul;
    int level_editor_selected_marker;
    int level_editor_selected_property;
    int level_editor_tool_selected;
    int level_editor_structure_tool_selected;
    int level_editor_drag_active;
    int level_editor_drag_kind;
    float level_editor_drag_x;
    float level_editor_drag_y;
    int level_editor_marker_count;
    float level_editor_marker_x01[LEVEL_EDITOR_MAX_MARKERS];
    float level_editor_marker_y01[LEVEL_EDITOR_MAX_MARKERS];
    int level_editor_marker_kind[LEVEL_EDITOR_MAX_MARKERS];
    int level_editor_marker_track[LEVEL_EDITOR_MAX_MARKERS];
    int level_editor_marker_order[LEVEL_EDITOR_MAX_MARKERS];
    float level_editor_marker_delay_s[LEVEL_EDITOR_MAX_MARKERS];
    float level_editor_marker_a[LEVEL_EDITOR_MAX_MARKERS];
    float level_editor_marker_b[LEVEL_EDITOR_MAX_MARKERS];
    float level_editor_marker_c[LEVEL_EDITOR_MAX_MARKERS];
    float level_editor_marker_d[LEVEL_EDITOR_MAX_MARKERS];
    float level_editor_marker_e[LEVEL_EDITOR_MAX_MARKERS];
    float level_editor_marker_f[LEVEL_EDITOR_MAX_MARKERS];
    float level_editor_marker_g[LEVEL_EDITOR_MAX_MARKERS];
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
size_t render_build_enemy_radar_gpu_lines(const game_state* g, wormhole_line_vertex* out, size_t out_cap);
size_t render_build_enemy_radar_gpu_tris(const game_state* g, wormhole_line_vertex* out, size_t out_cap);

vg_result render_frame(vg_context* ctx, const game_state* g, const render_metrics* metrics);

#endif
