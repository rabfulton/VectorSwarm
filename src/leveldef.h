#ifndef V_TYPE_LEVELDEF_H
#define V_TYPE_LEVELDEF_H

#include <stdio.h>

#include "game.h"

#define LEVELDEF_MAX_BOID_PROFILES 16
#define LEVELDEF_MAX_BOID_CYCLE 8
#define LEVELDEF_WEAPON_COUNT 3

enum leveldef_wave_mode {
    LEVELDEF_WAVES_NORMAL = 0,
    LEVELDEF_WAVES_BOID_ONLY = 1
};

enum leveldef_spawn_mode {
    LEVELDEF_SPAWN_SEQUENCED_CLEAR = 0,
    LEVELDEF_SPAWN_TIMED = 1,
    LEVELDEF_SPAWN_TIMED_SEQUENCED = 2
};

enum leveldef_wave_pattern_id {
    LEVELDEF_WAVE_SINE_SNAKE = 0,
    LEVELDEF_WAVE_V_FORMATION = 1,
    LEVELDEF_WAVE_SWARM = 2,
    LEVELDEF_WAVE_KAMIKAZE = 3
};

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
    float max_speed;
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

typedef struct leveldef_level {
    int render_style; /* enum level_render_style_id */
    int wave_mode;
    int spawn_mode; /* enum leveldef_spawn_mode */
    float spawn_interval_s;
    int default_boid_profile;
    float wave_cooldown_initial_s;
    float wave_cooldown_between_s;
    int exit_enabled;
    float exit_x01;
    float exit_y01;
    int boid_cycle_count;
    int boid_cycle[LEVELDEF_MAX_BOID_CYCLE];
    int wave_cycle_count;
    int wave_cycle[LEVELDEF_MAX_BOID_CYCLE];
    leveldef_wave_sine_tuning sine;
    leveldef_wave_v_tuning v;
    leveldef_wave_kamikaze_tuning kamikaze;
    int searchlight_count;
    leveldef_searchlight searchlights[MAX_SEARCHLIGHTS];
} leveldef_level;

typedef struct leveldef_db {
    int profile_count;
    leveldef_boid_profile profiles[LEVELDEF_MAX_BOID_PROFILES];
    leveldef_combat_tuning combat;
    leveldef_level levels[LEVEL_STYLE_COUNT];
} leveldef_db;

void leveldef_init_defaults(leveldef_db* db);
int leveldef_load_with_defaults(leveldef_db* db, const char* path, FILE* log_out);
int leveldef_load_project_layout(leveldef_db* db, const char* dir_path, FILE* log_out);
int leveldef_find_boid_profile(const leveldef_db* db, const char* name);
const leveldef_boid_profile* leveldef_get_boid_profile(const leveldef_db* db, int profile_id);
const leveldef_level* leveldef_get_level(const leveldef_db* db, int level_style);

#endif
