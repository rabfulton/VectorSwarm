#ifndef V_TYPE_GAME_H
#define V_TYPE_GAME_H

#include <stddef.h>

#define MAX_STARS 64
#define MAX_BULLETS 128
#define MAX_ENEMIES 64
#define MAX_PARTICLES 1024

enum level_style_id {
    LEVEL_STYLE_DEFENDER = 0,
    LEVEL_STYLE_ENEMY_RADAR = 1,
    LEVEL_STYLE_EVENT_HORIZON = 2,
    LEVEL_STYLE_EVENT_HORIZON_LEGACY = 3,
    LEVEL_STYLE_HIGH_PLAINS_DRIFTER = 4,
    LEVEL_STYLE_COUNT = 5
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
    float home_y;
} enemy;

typedef enum particle_type {
    PARTICLE_POINT = 0,
    PARTICLE_GEOM = 1
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
    int wave_announce_pending;
    int fire_sfx_pending;
    char wave_announce_text[160];
    float weapon_heat;
    int weapon_level;
    int active_particles;
    float thruster_emit_accum;
    float camera_x;
    float camera_y;
    float camera_vx;
    float camera_vy;
    int level_style; /* enum level_style_id */
    player_state player;
    star stars[MAX_STARS];
    bullet bullets[MAX_BULLETS];
    enemy enemies[MAX_ENEMIES];
    particle particles[MAX_PARTICLES];
} game_state;

void game_init(game_state* g, float world_w, float world_h);
void game_update(game_state* g, float dt, const game_input* in);
void game_cycle_level(game_state* g);
int game_enemy_count(const game_state* g);
float game_player_speed01(const game_state* g);
float game_weapon_heat01(const game_state* g);
float game_threat01(const game_state* g);
int game_pop_wave_announcement(game_state* g, char* out, size_t out_cap);
int game_pop_fire_sfx_count(game_state* g);

#endif
