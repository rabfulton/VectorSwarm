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

#endif
