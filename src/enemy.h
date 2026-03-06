#ifndef V_TYPE_ENEMY_H
#define V_TYPE_ENEMY_H

#include "game.h"
#include "leveldef.h"

void enemy_spawn_curated_enemy(
    game_state* g,
    const leveldef_db* db,
    const leveldef_level* lvl,
    int wave_id,
    const leveldef_curated_enemy* ce,
    float ui_scale,
    int uses_cylinder,
    float cylinder_period
);

void enemy_spawn_next_wave(
    game_state* g,
    const leveldef_db* db,
    const leveldef_level* lvl,
    float ui_scale,
    int uses_cylinder,
    float cylinder_period
);

void enemy_update_system(
    game_state* g,
    const leveldef_db* db,
    float dt,
    float ui_scale,
    int uses_cylinder,
    float cylinder_period
);

void enemy_apply_emp(
    game_state* g,
    float primary_radius,
    float blast_radius,
    float blast_accel,
    float ui_scale,
    int uses_cylinder,
    float cylinder_period
);

void enemy_emit_generic_destruction_fx(
    game_state* g,
    float x,
    float y,
    float vx,
    float vy,
    float radius,
    float ui_scale,
    int explosion_count
);

void enemy_spawn_eel_arc_burst(
    game_state* g,
    const enemy* e,
    int owner_index,
    int omnidirectional,
    int ray_count,
    float angle_offset
);

void enemy_spawn_boss_support_wave(
    game_state* g,
    int owner_index,
    int phase,
    uint32_t seed
);

#endif
