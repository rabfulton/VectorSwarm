#ifndef V_TYPE_LEVELDEF_H
#define V_TYPE_LEVELDEF_H

#include <stdio.h>

#include "game.h"

#define LEVELDEF_MAX_BOID_PROFILES 16
#define LEVELDEF_MAX_BOID_CYCLE 8
#define LEVELDEF_WEAPON_COUNT 3
#define LEVELDEF_MAX_DISCOVERED_LEVELS 128
#define LEVELDEF_MAX_EVENTS 64
#define LEVELDEF_MAX_MINEFIELDS 32
#define LEVELDEF_MAX_MISSILE_LAUNCHERS 32
#define LEVELDEF_MAX_ARC_NODES 64
#define LEVELDEF_MAX_WINDOW_MASKS 128
#define LEVELDEF_MAX_STRUCTURES 512
#define LEVELDEF_STRUCTURE_GRID_W 64
#define LEVELDEF_STRUCTURE_GRID_H 36
#define LEVELDEF_STRUCTURE_GRID_SCALE 2

enum leveldef_wave_mode {
    LEVELDEF_WAVES_NORMAL = 0,
    LEVELDEF_WAVES_BOID_ONLY = 1,
    LEVELDEF_WAVES_CURATED = 2
};

enum leveldef_spawn_mode {
    LEVELDEF_SPAWN_SEQUENCED_CLEAR = 0,
    LEVELDEF_SPAWN_TIMED = 1,
    LEVELDEF_SPAWN_TIMED_SEQUENCED = 2
};

enum leveldef_background_style {
    LEVELDEF_BACKGROUND_STARS = 0,
    LEVELDEF_BACKGROUND_NONE = 1,
    LEVELDEF_BACKGROUND_NEBULA = 2,
    LEVELDEF_BACKGROUND_GRID = 3,
    LEVELDEF_BACKGROUND_SOLID = 4,
    LEVELDEF_BACKGROUND_UNDERWATER = 5,
    LEVELDEF_BACKGROUND_FIRE = 6
};

enum leveldef_background_mask_style {
    LEVELDEF_BG_MASK_NONE = 0,
    LEVELDEF_BG_MASK_TERRAIN = 1,
    LEVELDEF_BG_MASK_WINDOWS = 2
};

enum leveldef_wave_pattern_id {
    LEVELDEF_WAVE_SINE_SNAKE = 0,
    LEVELDEF_WAVE_V_FORMATION = 1,
    LEVELDEF_WAVE_SWARM = 2,
    LEVELDEF_WAVE_KAMIKAZE = 3,
    LEVELDEF_WAVE_ASTEROID_STORM = 4,
    LEVELDEF_WAVE_SWARM_FISH = 5,
    LEVELDEF_WAVE_SWARM_FIREFLY = 6,
    LEVELDEF_WAVE_SWARM_BIRD = 7
};

enum leveldef_event_kind {
    LEVELDEF_EVENT_WAVE_SINE = 0,
    LEVELDEF_EVENT_WAVE_V = 1,
    LEVELDEF_EVENT_WAVE_SWARM = 2,
    LEVELDEF_EVENT_WAVE_KAMIKAZE = 3,
    LEVELDEF_EVENT_ASTEROID_STORM = 4,
    LEVELDEF_EVENT_WAVE_SWARM_FISH = 5,
    LEVELDEF_EVENT_WAVE_SWARM_FIREFLY = 6,
    LEVELDEF_EVENT_WAVE_SWARM_BIRD = 7
};

typedef struct leveldef_event_entry {
    int kind;      /* enum leveldef_event_kind */
    int order;     /* 1-based event order */
    float delay_s; /* delay after previous event ends */
} leveldef_event_entry;

typedef struct leveldef_boid_profile {
    char name[32];
    char wave_name[64];
    int count;
    float sep_w;
    float ali_w;
    float coh_w;
    float avoid_w;
    float goal_w;
    float sep_r;
    float ali_r;
    float coh_r;
    float goal_amp;
    float goal_freq;
    float wander_w;
    float wander_freq;
    float steer_drag;
    float max_turn_rate_deg;
    float max_speed;
    float min_speed;
    float accel;
    float radius_min;
    float radius_max;
    float spawn_x01;
    float spawn_x_span;
    float spawn_y01;
    float spawn_y_span;
} leveldef_boid_profile;

typedef struct leveldef_searchlight {
    float anchor_x01;
    float anchor_y01;
    float length_h01;
    float half_angle_deg;
    float sweep_center_deg;
    float sweep_amplitude_deg;
    float sweep_speed;
    float sweep_phase_deg;
    int sweep_motion;
    int source_type;
    float source_radius;
    float clear_grace_s;
    float fire_interval_s;
    float projectile_speed;
    float projectile_ttl_s;
    float projectile_radius;
    float aim_jitter_deg;
} leveldef_searchlight;

typedef struct leveldef_minefield {
    float anchor_x01;
    float anchor_y01;
    int count;
} leveldef_minefield;

typedef struct leveldef_missile_launcher {
    float anchor_x01;
    float anchor_y01;
    int count;
    float spacing;
    float activation_range;
    float missile_speed;
    float missile_turn_rate_deg;
    float missile_ttl_s;
    float hit_radius;
    float blast_radius;
} leveldef_missile_launcher;

typedef struct leveldef_arc_node {
    float anchor_x01;
    float anchor_y01;
    float period_s;
    float on_s;
    float radius;
    float push_accel;
    float damage_interval_s;
} leveldef_arc_node;

typedef struct leveldef_window_mask {
    float anchor_x01;
    float anchor_y01;
    float width_h01;
    float height_v01;
    int flip_vertical;
} leveldef_window_mask;

typedef struct leveldef_structure_instance {
    int prefab_id;
    int layer; /* 0=base, 1=feature */
    int grid_x;
    int grid_y;
    int rotation_quadrants;
    int flip_x;
    int flip_y;
    int w_units;
    int h_units;
    int variant;
    /* Optional vent tuning (used by vent prefabs). */
    float vent_density;
    float vent_opacity;
    float vent_plume_height;
} leveldef_structure_instance;

typedef struct leveldef_combat_tuning {
    struct {
        float cooldown_min_s;
        float cooldown_max_s;
        int burst_count;
        float burst_gap_s;
        int projectiles_per_shot;
        float spread_deg;
        float projectile_speed;
        float projectile_ttl_s;
        float projectile_radius;
        float aim_lead_s;
    } weapon[LEVELDEF_WEAPON_COUNT];
    float progression_wave_weight;
    float progression_score_weight;
    float progression_level_weight;
    float armed_probability_base[3];
    float armed_probability_progression_bonus[3];
    float fire_range_min;
    float fire_range_max_base;
    float fire_range_max_progression_bonus;
    float aim_error_deg_start;
    float aim_error_deg_end;
    float cooldown_scale_start;
    float cooldown_scale_end;
    float projectile_speed_scale_start;
    float projectile_speed_scale_end;
    float spread_scale_start;
    float spread_scale_end;
    float swarm_armed_prob_start;
    float swarm_armed_prob_end;
    float swarm_spread_prob_start;
    float swarm_spread_prob_end;
} leveldef_combat_tuning;

typedef struct leveldef_wave_sine_tuning {
    int count;
    float start_x01;
    float spacing_x;
    float home_y01;
    float phase_step;
    float form_amp;
    float form_freq;
    float break_delay_base;
    float break_delay_step;
    float max_speed;
    float accel;
} leveldef_wave_sine_tuning;

typedef struct leveldef_wave_v_tuning {
    int count;
    float start_x01;
    float spacing_x;
    float home_y01;
    float home_y_step;
    float phase_step;
    float form_amp;
    float form_freq;
    float break_delay_min;
    float break_delay_rand;
    float max_speed;
    float accel;
} leveldef_wave_v_tuning;

typedef struct leveldef_wave_kamikaze_tuning {
    int count;
    float start_x01;
    float spacing_x;
    float y_margin;
    float max_speed;
    float accel;
    float radius_min;
    float radius_max;
} leveldef_wave_kamikaze_tuning;

typedef struct leveldef_curated_enemy {
    int kind; /* level editor marker kind: 2=sine,3=v,4=kamikaze,5=boid,10/11/12=swarm variants,15=jelly_swarm,16=manta_wing */
    float x01; /* in screens (same unit as exit_x01) */
    float y01; /* 0..1 on screen */
    float a;
    float b;
    float c;
    float d; /* optional extra parameter (jelly_swarm uses this as size scale) */
} leveldef_curated_enemy;

typedef struct leveldef_level {
    float editor_length_screens; /* editor/runtime normalization basis for x01-authored objects */
    int theme_palette; /* 0=green, 1=amber, 2=ice */
    int background_style; /* enum leveldef_background_style */
    int background_mask_style; /* enum leveldef_background_mask_style */
    float underwater_density;
    float underwater_caustic_strength;
    float underwater_caustic_scale;
    float underwater_bubble_rate;
    float underwater_haze_alpha;
    float underwater_current_speed;
    float underwater_palette_shift;
    float underwater_kelp_density;
    float underwater_kelp_sway_amp;
    float underwater_kelp_sway_speed;
    float underwater_kelp_height;
    float underwater_kelp_parallax_strength;
    float underwater_kelp_tint_r;
    float underwater_kelp_tint_g;
    float underwater_kelp_tint_b;
    float underwater_kelp_tint_strength;
    float fire_magma_scale;
    float fire_warp_amp;
    float fire_pulse_freq;
    float fire_plume_height;
    float fire_rise_speed;
    float fire_distortion_amp;
    float fire_smoke_alpha_cap;
    float fire_ember_spawn_rate;
    int render_style; /* enum level_render_style_id */
    int wave_mode;
    int spawn_mode; /* enum leveldef_spawn_mode */
    float spawn_interval_s;
    int default_boid_profile;
    float wave_cooldown_initial_s;
    float wave_cooldown_between_s;
    int bidirectional_spawns;
    float cylinder_double_swarm_chance;
    float powerup_drop_chance;
    float powerup_double_shot_r;
    float powerup_double_shot_g;
    float powerup_double_shot_b;
    float powerup_triple_shot_r;
    float powerup_triple_shot_g;
    float powerup_triple_shot_b;
    float powerup_vitality_r;
    float powerup_vitality_g;
    float powerup_vitality_b;
    float powerup_orbital_boost_r;
    float powerup_orbital_boost_g;
    float powerup_orbital_boost_b;
    float powerup_magnet_r;
    float powerup_magnet_g;
    float powerup_magnet_b;
    int exit_enabled;
    float exit_x01;
    float exit_y01;
    int asteroid_storm_enabled;
    float asteroid_storm_start_x01;
    float asteroid_storm_angle_deg;
    float asteroid_storm_speed;
    float asteroid_storm_duration_s;
    float asteroid_storm_density;
    int boid_cycle_count;
    int boid_cycle[LEVELDEF_MAX_BOID_CYCLE];
    int wave_cycle_count;
    int wave_cycle[LEVELDEF_MAX_BOID_CYCLE];
    int event_count;
    leveldef_event_entry events[LEVELDEF_MAX_EVENTS];
    leveldef_wave_sine_tuning sine;
    leveldef_wave_v_tuning v;
    leveldef_wave_kamikaze_tuning kamikaze;
    int curated_count;
    leveldef_curated_enemy curated[LEVELDEF_MAX_BOID_CYCLE * 8];
    int searchlight_count;
    leveldef_searchlight searchlights[MAX_SEARCHLIGHTS];
    int minefield_count;
    leveldef_minefield minefields[LEVELDEF_MAX_MINEFIELDS];
    int missile_launcher_count;
    leveldef_missile_launcher missile_launchers[LEVELDEF_MAX_MISSILE_LAUNCHERS];
    int arc_node_count;
    leveldef_arc_node arc_nodes[LEVELDEF_MAX_ARC_NODES];
    int window_mask_count;
    leveldef_window_mask window_masks[LEVELDEF_MAX_WINDOW_MASKS];
    int structure_count;
    leveldef_structure_instance structures[LEVELDEF_MAX_STRUCTURES];
} leveldef_level;

typedef struct leveldef_db {
    int profile_count;
    leveldef_boid_profile profiles[LEVELDEF_MAX_BOID_PROFILES];
    leveldef_combat_tuning combat;
    int level_present[LEVEL_STYLE_COUNT];
    leveldef_level levels[LEVEL_STYLE_COUNT];
} leveldef_db;

typedef struct leveldef_discovered_level {
    char name[64];
    int style_hint;
    leveldef_level level;
} leveldef_discovered_level;

void leveldef_init_defaults(leveldef_db* db);
int leveldef_load_with_defaults(leveldef_db* db, const char* path, FILE* log_out);
int leveldef_load_project_layout(leveldef_db* db, const char* dir_path, FILE* log_out);
int leveldef_load_level_file_with_base(
    const leveldef_db* base_db,
    const char* level_path,
    leveldef_level* out_level,
    int* out_style,
    FILE* log_out
);
int leveldef_discover_levels_from_dir(
    const leveldef_db* base_db,
    const char* dir_path,
    leveldef_discovered_level* out_levels,
    int out_cap,
    FILE* log_out
);
int leveldef_find_boid_profile(const leveldef_db* db, const char* name);
const leveldef_boid_profile* leveldef_get_boid_profile(const leveldef_db* db, int profile_id);
const leveldef_level* leveldef_get_level(const leveldef_db* db, int level_style);

#endif
