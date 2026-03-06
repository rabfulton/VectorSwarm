#ifndef V_TYPE_BOSS_H
#define V_TYPE_BOSS_H

#include "game.h"

#define BOSS_PART_EMITTERS_MAX 2

typedef struct boss_part_blueprint {
    int role;
    int flags;
    int visual_kind;
    int hp;
    float radius;
    float local_x;
    float local_y;
    float local_rot;
    int emitter_count;
    float emitter_local_x[BOSS_PART_EMITTERS_MAX];
    float emitter_local_y[BOSS_PART_EMITTERS_MAX];
} boss_part_blueprint;

typedef struct boss_blueprint {
    int boss_id;
    const char* name;
    int controller_visual_kind;
    int controller_flags;
    float controller_radius;
    float sway_amp_x;
    float sway_amp_y;
    float sway_speed;
    float roll_amp_rad;
    float bounds_radius;
    int max_parts;
} boss_blueprint;

const boss_blueprint* boss_lookup_blueprint(int boss_id);
int boss_is_valid_blueprint_id(int boss_id);
const char* boss_name_for_id(int boss_id);
int boss_build_parts(
    const boss_blueprint* blueprint,
    uint32_t seed,
    int variant_id,
    boss_part_blueprint* out_parts,
    int out_cap
);

void boss_reset_enemy_runtime(game_state* g, int enemy_index);
void boss_configure_controller(
    game_state* g,
    int enemy_index,
    const boss_blueprint* blueprint,
    uint32_t seed,
    float anchor_x,
    float anchor_y,
    float difficulty_scale,
    int gates_exit
);
void boss_configure_part(
    game_state* g,
    int enemy_index,
    int owner_index,
    const boss_part_blueprint* part,
    int hp_max
);
int boss_update_enemy(game_state* g, int enemy_index, float dt);
int boss_any_controller_alive(const game_state* g);
int boss_enemy_is_managed(const game_state* g, int enemy_index);
int boss_enemy_is_damageable(const game_state* g, int enemy_index);
int boss_enemy_damage_per_hit(const game_state* g, int enemy_index);
int boss_enemy_has_contact_damage(const game_state* g, int enemy_index);
void boss_on_enemy_damaged(game_state* g, int enemy_index, int damage_applied, int destroyed);
void boss_on_enemy_destroyed(game_state* g, int enemy_index);

#endif
