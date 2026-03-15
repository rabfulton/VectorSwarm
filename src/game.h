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
#define MAX_ASTEROIDS 192
#define ASTEROID_EMITTERS 64
#define MAX_MINES 256
#define MAX_MISSILE_LAUNCHERS 64
#define MAX_MISSILES 256
#define MAX_ARC_NODES 64
#define MAX_POWERUPS 64
#define MAX_EEL_ARCS 384
#define EEL_ARC_MAX_POINTS 10
#define EEL_SPINE_POINTS 28
#define EEL_ARC_PULSE_PERIOD_S 0.60f
#define EEL_ARC_PULSE_ON_S 0.20f
#define PLAYER_ALT_WEAPON_COUNT 4

struct leveldef_db;
struct leveldef_level;

enum level_style_id {
    LEVEL_STYLE_DEFENDER = 0,
    LEVEL_STYLE_ENEMY_RADAR = 1,
    LEVEL_STYLE_EVENT_HORIZON = 2,
    LEVEL_STYLE_EVENT_HORIZON_LEGACY = 3,
    LEVEL_STYLE_HIGH_PLAINS_DRIFTER = 4,
    LEVEL_STYLE_HIGH_PLAINS_DRIFTER_2 = 5,
    LEVEL_STYLE_FOG_OF_WAR = 6,
    LEVEL_STYLE_BLANK = 7,
    LEVEL_STYLE_REVOLVER = 8,
    LEVEL_STYLE_COUNT = 9
};

enum level_render_style_id {
    LEVEL_RENDER_DEFENDER = 0,
    LEVEL_RENDER_CYLINDER = 1,
    LEVEL_RENDER_DRIFTER = 2,
    LEVEL_RENDER_DRIFTER_SHADED = 3,
    LEVEL_RENDER_FOG = 4,
    LEVEL_RENDER_BLANK = 5
};

enum enemy_visual_kind {
    ENEMY_VISUAL_DEFAULT = 0,
    ENEMY_VISUAL_JELLY = 1,
    ENEMY_VISUAL_MANTA = 2,
    ENEMY_VISUAL_EEL = 3,
    ENEMY_VISUAL_PHOENIX = 4,
    ENEMY_VISUAL_BOID_RAZOR = 5,
    ENEMY_VISUAL_BOID_LANTERN = 6,
    ENEMY_VISUAL_BOID_SHARD = 7,
    ENEMY_VISUAL_BOID_WRAITH = 8,
    ENEMY_VISUAL_BOSS_CORE = 9,
    ENEMY_VISUAL_BOSS_PLATE = 10,
    ENEMY_VISUAL_BOSS_JOINT = 11,
    ENEMY_VISUAL_BOSS_TURRET = 12,
    ENEMY_VISUAL_BOSS_VENT = 13
};

enum boss_part_role {
    BOSS_PART_ROLE_NONE = 0,
    BOSS_PART_ROLE_CONTROLLER = 1,
    BOSS_PART_ROLE_CORE = 2,
    BOSS_PART_ROLE_ARMOR = 3,
    BOSS_PART_ROLE_JOINT = 4,
    BOSS_PART_ROLE_TURRET = 5,
    BOSS_PART_ROLE_VENT = 6
};

enum boss_enemy_flags {
    BOSS_ENEMY_FLAG_INVULNERABLE = 1 << 0,
    BOSS_ENEMY_FLAG_NO_CONTACT_DAMAGE = 1 << 1,
    BOSS_ENEMY_FLAG_WEAKPOINT = 1 << 2
};

typedef enum kamikaze_style_id {
    KAMIKAZE_STYLE_CLASSIC = 0,
    KAMIKAZE_STYLE_PHOENIX = 1
} kamikaze_style_id;

typedef enum boid_style_id {
    BOID_STYLE_CLASSIC = 0,
    BOID_STYLE_RAZOR = 1,
    BOID_STYLE_LANTERN = 2,
    BOID_STYLE_SHARD = 3,
    BOID_STYLE_WRAITH = 4
} boid_style_id;

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

typedef enum player_alt_weapon_id {
    PLAYER_ALT_WEAPON_SHIELD = 0,
    PLAYER_ALT_WEAPON_MISSILE = 1,
    PLAYER_ALT_WEAPON_EMP = 2,
    PLAYER_ALT_WEAPON_REAR_GUN = 3
} player_alt_weapon_id;

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
    float swarm_min_speed;
    float swarm_turn_rate_rad;
    float facing_x;
    float facing_y;
    float kamikaze_tail;
    float kamikaze_thrust;
    float kamikaze_tail_start;
    float kamikaze_thrust_scale;
    float kamikaze_glide_scale;
    float kamikaze_strike_x;
    float kamikaze_strike_y;
    int kamikaze_is_turning;
    int visual_kind;
    uint32_t visual_seed;
    float visual_phase;
    float visual_param_a;
    float visual_param_b;
    float boss_telegraph;
    float emp_push_ax;
    float emp_push_ay;
    float emp_push_time_s;
    float lane_dir;
    float home_y;
    int formation_kind;
    int hp;
    int missile_ammo;
    int armed;
    float fire_prob;
    int weapon_id;
    float fire_cooldown_s;
    int burst_shots_left;
    float burst_gap_timer_s;
    float missile_cooldown_s;
    float missile_charge_s;
    float missile_charge_duration_s;
    float eel_heading_rad;
    float eel_wave_freq;
    float eel_wave_amp;
    float eel_body_length;
    float eel_min_speed;
    float eel_turn_rate_rad;
    float eel_weapon_range;
    float eel_weapon_fire_rate;
    float eel_weapon_duration_s;
    float eel_weapon_damage_interval_s;
    int eel_spine_count;
    float eel_spine_x[EEL_SPINE_POINTS];
    float eel_spine_y[EEL_SPINE_POINTS];
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
    SEARCHLIGHT_MOTION_SPIN = 2,
    SEARCHLIGHT_MOTION_PENDULUM_INV = 3
} searchlight_motion_type;

typedef enum searchlight_source_type {
    SEARCHLIGHT_SOURCE_DOME = 0,
    SEARCHLIGHT_SOURCE_ORB = 1
} searchlight_source_type;

typedef enum mine_style_id {
    MINE_STYLE_CLASSIC = 0,
    MINE_STYLE_ANEMONE = 1
} mine_style_id;

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
    GAME_AUDIO_EVENT_SEARCHLIGHT_FIRE = 3,
    GAME_AUDIO_EVENT_EMP = 4,
    GAME_AUDIO_EVENT_LIGHTNING = 5,
    GAME_AUDIO_EVENT_FX2 = 6
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

typedef struct asteroid_body {
    int active;
    body b;
    float size;
    float angle;
    float spin_rate;
    float radius;
} asteroid_body;

typedef struct mine {
    int active;
    body b;
    float radius;
    float angle;
    float spin_rate;
    int hp;
    int style; /* enum mine_style_id */
} mine;

typedef enum missile_owner_type {
    MISSILE_OWNER_ENEMY = 0,
    MISSILE_OWNER_PLAYER = 1
} missile_owner_type;

typedef struct missile_launcher {
    int active;
    int triggered;
    int fired;
    int launched_count;
    float anchor_x;
    float anchor_y;
    int count;
    float spacing;
    float activation_range;
    float launch_interval_s;
    float launch_timer_s;
    float missile_speed;
    float missile_turn_rate_deg;
    float missile_ttl_s;
    float hit_radius;
    float blast_radius;
} missile_launcher;

typedef struct homing_missile {
    int active;
    int owner; /* enum missile_owner_type */
    body b;
    float heading_rad;
    float speed;
    float turn_rate_rad_s;
    float ttl_s;
    float hit_radius;
    float blast_radius;
    float radius;
    float arm_delay_s;
    float forward_x;
    float forward_y;
    float trail_emit_accum;
} homing_missile;

typedef enum powerup_type {
    POWERUP_DOUBLE_SHOT = 0,
    POWERUP_TRIPLE_SHOT = 1,
    POWERUP_VITALITY = 2,
    POWERUP_ORBITAL_BOOST = 3,
    POWERUP_MAGNET = 4,
    POWERUP_COUNT = 5
} powerup_type;

typedef struct powerup_pickup {
    int active;
    int type; /* enum powerup_type */
    body b;
    float ttl_s;
    float radius;
    float spin;
    float spin_rate;
    float bob_phase;
} powerup_pickup;

typedef struct eel_arc_effect {
    int active;
    int owner_index;
    int owner_wave_id;
    int owner_slot_index;
    int omnidirectional;
    uint32_t seed;
    float start_u;
    float base_angle;
    float range;
    float age_s;
    float life_s;
    float damage_timer_s;
    int pulse_prev_on;
    int pulse_emit_on;
    int pulse_sound_anchor;
    int strike_slot;
    float focus_dir_x;
    float focus_dir_y;
    float focus_range;
    int point_count;
    float point_x[EEL_ARC_MAX_POINTS];
    float point_y[EEL_ARC_MAX_POINTS];
} eel_arc_effect;

typedef struct arc_node_runtime {
    int active;
    float x;
    float y;
    float period_s;
    float on_s;
    float radius;
    float push_accel;
    float damage_interval_s;
    float phase_s;
    float damage_timer_s;
    float sound_timer_s;
    int energized_prev;
} arc_node_runtime;

typedef struct boss_enemy_attachment {
    int active;
    int owner_index;
    int role;
    int flags;
    int hp_max;
    int emitter_count;
    float local_x;
    float local_y;
    float local_rot;
    float emitter_local_x[2];
    float emitter_local_y[2];
    float emitter_cooldown_s[2];
} boss_enemy_attachment;

typedef struct boss_controller_runtime {
    int active;
    int boss_id;
    uint32_t seed;
    int phase;
    int live_part_count;
    int burst_shots_left;
    int attack_index;
    int gates_exit;
    int destruction_active;
    int telegraph_active;
    int telegraph_kind;
    int telegraph_part_index;
    int telegraph_emitter_index;
    int movement_mode;
    float anchor_x;
    float anchor_y;
    float sway_phase_s;
    float sway_amp_x;
    float sway_amp_y;
    float sway_speed;
    float roll_amp_rad;
    float bounds_radius;
    float difficulty_scale;
    float phase_time_s;
    float attack_cooldown_s;
    float burst_gap_s;
    float telegraph_time_s;
    float telegraph_total_s;
    float movement_timer_s;
    float movement_cooldown_s;
    float behind_timer_s;
    float lightning_cooldown_s;
    float support_cooldown_s;
    float movement_target_x;
    float movement_target_y;
    float destruction_time_s;
    float detonation_timer_s;
} boss_controller_runtime;

typedef struct game_input {
    int left;
    int right;
    int up;
    int down;
    int fire;
    int secondary_fire;
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
    float secondary_fire_cooldown_s;
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
    float prev_camera_x;
    float camera_vx;
    float camera_vy;
    float camera_bias_x;
    int level_style; /* enum level_style_id */
    int level_index;
    char current_level_name[64];
    float level_time_remaining_s;
    int orbit_decay_timeout;
    int render_style; /* enum level_render_style_id */
    int level_theme_palette; /* 0=green,1=amber,2=ice from level config */
    player_state player;
    player_state prev_player;
    star stars[MAX_STARS];
    bullet bullets[MAX_BULLETS];
    bullet prev_bullets[MAX_BULLETS];
    enemy_bullet enemy_bullets[MAX_ENEMY_BULLETS];
    enemy_bullet prev_enemy_bullets[MAX_ENEMY_BULLETS];
    enemy enemies[MAX_ENEMIES];
    enemy prev_enemies[MAX_ENEMIES];
    particle particles[MAX_PARTICLES];
    enemy_debris debris[MAX_ENEMY_DEBRIS];
    game_audio_event audio_events[MAX_AUDIO_EVENTS];
    searchlight searchlights[MAX_SEARCHLIGHTS];
    int searchlight_count;
    int auto_event_mode;
    int auto_event_index;
    int auto_event_running;
    int auto_event_running_kind;
    float auto_event_delay_s;
    float auto_event_running_timeout_s;
    asteroid_body asteroids[MAX_ASTEROIDS];
    int asteroid_count;
    int asteroid_storm_enabled;
    int asteroid_storm_active;
    int asteroid_storm_completed;
    int asteroid_storm_announced;
    float asteroid_storm_start_x;
    float asteroid_storm_angle_rad;
    float asteroid_storm_speed;
    float asteroid_storm_duration_s;
    float asteroid_storm_density;
    float asteroid_storm_timer_s;
    float asteroid_storm_cooldown_s;
    int asteroid_storm_emitter_cursor;
    float asteroid_storm_emitter_cd[ASTEROID_EMITTERS];
    mine mines[MAX_MINES];
    int mine_count;
    missile_launcher missile_launchers[MAX_MISSILE_LAUNCHERS];
    int missile_launcher_count;
    homing_missile missiles[MAX_MISSILES];
    homing_missile prev_missiles[MAX_MISSILES];
    int missile_count;
    arc_node_runtime arc_nodes[MAX_ARC_NODES];
    int arc_node_count;
    powerup_pickup powerups[MAX_POWERUPS];
    int powerup_count;
    eel_arc_effect eel_arcs[MAX_EEL_ARCS];
    int eel_arc_count;
    boss_enemy_attachment boss_attachments[MAX_ENEMIES];
    boss_controller_runtime boss_controllers[MAX_ENEMIES];
    int powerup_magnet_active;
    float powerup_drop_credit; /* Smooths drop cadence while preserving average drop chance. */
    int exit_portal_active;
    float exit_portal_x;
    float exit_portal_y;
    float exit_portal_radius;
    int exit_requires_boss_defeated;
    int gating_bosses_remaining;
    float shield_time_remaining_s;
    int shield_active;
    int lightning_active;
    float lightning_audio_gain;
    float lightning_audio_pan;
    float shield_radius;
    float mine_push_ax;
    float mine_push_ay;
    float mine_push_time_s;
    int emp_effect_active;
    float emp_effect_t;
    float emp_effect_duration_s;
    float emp_effect_x;
    float emp_effect_y;
    float emp_primary_radius;
    float emp_blast_radius;
    int alt_weapon_equipped; /* enum player_alt_weapon_id */
    int alt_weapon_ammo[PLAYER_ALT_WEAPON_COUNT];
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
const struct leveldef_level* game_current_leveldef(const game_state* g);
const char* game_current_level_name(const game_state* g);
int game_set_level_by_name(game_state* g, const char* name);
int game_refresh_levels(game_state* g);
int game_apply_level_override(game_state* g, const struct leveldef_level* level, const char* level_name);
void game_set_alt_weapon(game_state* g, int weapon_id);
int game_get_alt_weapon(const game_state* g);
int game_get_alt_weapon_ammo(const game_state* g, int weapon_id);
void game_on_enemy_destroyed(game_state* g, float x, float y, float vx, float vy, int score_delta);
void game_on_player_life_lost(game_state* g);
int game_structure_circle_overlap(const game_state* g, float x, float y, float radius);
int game_find_noncolliding_spawn(
    const game_state* g,
    float* io_x,
    float* io_y,
    float radius,
    float search_step,
    float max_search_radius
);
void game_structure_avoidance_vector(
    const game_state* g,
    float x,
    float y,
    float probe_radius,
    float probe_distance,
    float* out_x,
    float* out_y
);
int game_line_of_sight_clear(const game_state* g, float x0, float y0, float x1, float y1, float radius);
int game_structure_segment_blocked(const game_state* g, float x0, float y0, float x1, float y1, float pad_radius);
int game_spawn_enemy_bullet(
    game_state* g,
    float x,
    float y,
    float dir_x,
    float dir_y,
    float speed,
    float ttl_s,
    float radius
);
int game_spawn_enemy_missile(
    game_state* g,
    float x,
    float y,
    float dir_x,
    float dir_y,
    float speed,
    float turn_rate_deg,
    float ttl_s,
    float hit_radius,
    float blast_radius
);

#endif
