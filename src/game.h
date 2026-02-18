#ifndef V_TYPE_GAME_H
#define V_TYPE_GAME_H

#include <stddef.h>
#include <stdint.h>

#define MAX_STARS 64
#define MAX_BULLETS 128
#define MAX_ENEMY_BULLETS 512
#define MAX_ENEMIES 64
#define MAX_PARTICLES 1024
#define MAX_ENEMY_DEBRIS 512
#define MAX_AUDIO_EVENTS 64
#define MAX_SEARCHLIGHTS 4
#define MAX_CURATED_RUNTIME 128

struct leveldef_db;

enum level_style_id {
    LEVEL_STYLE_DEFENDER = 0,
    LEVEL_STYLE_ENEMY_RADAR = 1,
    LEVEL_STYLE_EVENT_HORIZON = 2,
    LEVEL_STYLE_EVENT_HORIZON_LEGACY = 3,
    LEVEL_STYLE_HIGH_PLAINS_DRIFTER = 4,
    LEVEL_STYLE_HIGH_PLAINS_DRIFTER_2 = 5,
    LEVEL_STYLE_FOG_OF_WAR = 6,
    LEVEL_STYLE_COUNT = 7
};

enum level_render_style_id {
    LEVEL_RENDER_DEFENDER = 0,
    LEVEL_RENDER_CYLINDER = 1,
    LEVEL_RENDER_DRIFTER = 2,
    LEVEL_RENDER_DRIFTER_SHADED = 3,
    LEVEL_RENDER_FOG = 4
};

typedef struct star {
    float x;
    float y;
    float prev_x;
    float prev_y;
    float speed;
    float size;
} star;

typedef struct body {
    float x;
    float y;
    float vx;
    float vy;
    float ax;
    float ay;
} body;

typedef struct player_state {
    body b;
    float thrust;
    float drag;
    float max_speed;
    float facing_x;
} player_state;

typedef struct bullet {
    int active;
    body b;
    float spawn_x;
    float ttl_s;
} bullet;

typedef struct enemy {
    int active;
    body b;
    float radius;
    int archetype;
    int state;
    int wave_id;
    int slot_index;
    float ai_timer_s;
    float break_delay_s;
    float max_speed;
    float accel;
    float form_phase;
    float form_amp;
    float form_freq;
    float swarm_sep_w;
    float swarm_ali_w;
    float swarm_coh_w;
    float swarm_avoid_w;
    float swarm_goal_w;
    float swarm_sep_r;
    float swarm_ali_r;
    float swarm_coh_r;
    float swarm_goal_amp;
    float swarm_goal_freq;
    float swarm_goal_dir;
    float swarm_wander_w;
    float swarm_wander_freq;
    float swarm_drag;
    float lane_dir;
    float home_y;
    int formation_kind;
    int armed;
    int weapon_id;
    float fire_cooldown_s;
    int burst_shots_left;
    float burst_gap_timer_s;
} enemy;

typedef struct enemy_bullet {
    int active;
    body b;
    float ttl_s;
    float radius;
} enemy_bullet;

typedef enum searchlight_motion_type {
    SEARCHLIGHT_MOTION_LINEAR = 0,
    SEARCHLIGHT_MOTION_PENDULUM = 1,
    SEARCHLIGHT_MOTION_SPIN = 2
} searchlight_motion_type;

typedef enum searchlight_source_type {
    SEARCHLIGHT_SOURCE_DOME = 0,
    SEARCHLIGHT_SOURCE_ORB = 1
} searchlight_source_type;

typedef struct searchlight {
    int active;
    float origin_x;
    float origin_y;
    float length;
    float half_angle_rad;
    float sweep_center_rad;
    float sweep_amplitude_rad;
    float sweep_speed;
    float sweep_phase;
    int sweep_motion; /* enum searchlight_motion_type */
    int source_type; /* enum searchlight_source_type */
    float source_radius;
    float clear_grace_s;
    float damage_interval_s;
    float projectile_speed;
    float projectile_ttl_s;
    float projectile_radius;
    float aim_jitter_rad;
    float damage_timer_s;
    float alert_timer_s;
    float current_angle_rad;
} searchlight;

typedef enum game_audio_event_type {
    GAME_AUDIO_EVENT_ENEMY_FIRE = 1,
    GAME_AUDIO_EVENT_EXPLOSION = 2,
    GAME_AUDIO_EVENT_SEARCHLIGHT_FIRE = 3
} game_audio_event_type;

typedef struct game_audio_event {
    game_audio_event_type type;
    float x;
    float y;
} game_audio_event;

typedef enum particle_type {
    PARTICLE_POINT = 0,
    PARTICLE_GEOM = 1,
    PARTICLE_FLASH = 2
} particle_type;

typedef struct particle {
    int active;
    particle_type type;
    body b;
    float age_s;
    float life_s;
    float size;
    float spin;
    float spin_rate;
    float r;
    float g;
    float bcol;
    float a;
} particle;

typedef struct enemy_debris {
    int active;
    body b;
    float half_len;
    float angle;
    float spin_rate;
    float age_s;
    float life_s;
    float alpha;
} enemy_debris;

typedef struct game_input {
    int left;
    int right;
    int up;
    int down;
    int fire;
    int restart;
} game_input;

typedef struct game_state {
    float world_w;
    float world_h;
    float t;
    int lives;
    int kills;
    int score;
    float fire_cooldown_s;
    float enemy_spawn_timer_s;
    float wave_cooldown_s;
    int wave_index;
    int wave_id_alloc;
    int curated_spawned_count;
    uint8_t curated_spawned[MAX_CURATED_RUNTIME];
    int wave_announce_pending;
    int fire_sfx_pending;
    char wave_announce_text[160];
    float weapon_heat;
    int weapon_level;
    int active_particles;
    int audio_event_count;
    float thruster_emit_accum;
    float camera_x;
    float camera_y;
    float camera_vx;
    float camera_vy;
    int level_style; /* enum level_style_id */
    int level_index;
    int render_style; /* enum level_render_style_id */
    player_state player;
    star stars[MAX_STARS];
    bullet bullets[MAX_BULLETS];
    enemy_bullet enemy_bullets[MAX_ENEMY_BULLETS];
    enemy enemies[MAX_ENEMIES];
    particle particles[MAX_PARTICLES];
    enemy_debris debris[MAX_ENEMY_DEBRIS];
    game_audio_event audio_events[MAX_AUDIO_EVENTS];
    searchlight searchlights[MAX_SEARCHLIGHTS];
    int searchlight_count;
    int exit_portal_active;
    float exit_portal_x;
    float exit_portal_y;
    float exit_portal_radius;
} game_state;

void game_init(game_state* g, float world_w, float world_h);
void game_set_world_size(game_state* g, float world_w, float world_h);
void game_update(game_state* g, float dt, const game_input* in);
void game_cycle_level(game_state* g);
int game_enemy_count(const game_state* g);
float game_player_speed01(const game_state* g);
float game_weapon_heat01(const game_state* g);
float game_threat01(const game_state* g);
int game_pop_wave_announcement(game_state* g, char* out, size_t out_cap);
int game_pop_fire_sfx_count(game_state* g);
int game_pop_audio_events(game_state* g, game_audio_event* out, int out_cap);
const struct leveldef_db* game_leveldef_get(void);
const char* game_current_level_name(const game_state* g);
int game_set_level_by_name(game_state* g, const char* name);

#endif
