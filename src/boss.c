#include "boss.h"
#include "enemy.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static float clampf(float v, float lo, float hi) {
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

static int clampi(int v, int lo, int hi) {
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

static float lerpf(float a, float b, float t) {
    return a + (b - a) * t;
}

static float hash01_u32(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return (float)(x & 0x00ffffffU) / 16777215.0f;
}

static void normalize2(float* x, float* y) {
    const float d2 = (*x) * (*x) + (*y) * (*y);
    if (d2 <= 1.0e-6f) {
        *x = -1.0f;
        *y = 0.0f;
        return;
    }
    {
        const float inv_d = 1.0f / sqrtf(d2);
        *x *= inv_d;
        *y *= inv_d;
    }
}

static float boss_ui_scale(const game_state* g) {
    const float sx = g->world_w / 1920.0f;
    const float sy = g->world_h / 1080.0f;
    return fmaxf(0.5f, fminf(sx, sy));
}

#if defined(V_TYPE_BOSS_NO_RUNTIME_WEAPONS)
static int boss_game_spawn_enemy_bullet(
    game_state* g,
    float x,
    float y,
    float dir_x,
    float dir_y,
    float speed,
    float ttl_s,
    float radius
) {
    (void)g;
    (void)x;
    (void)y;
    (void)dir_x;
    (void)dir_y;
    (void)speed;
    (void)ttl_s;
    (void)radius;
    return 0;
}

static int boss_game_spawn_enemy_missile(
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
) {
    (void)g;
    (void)x;
    (void)y;
    (void)dir_x;
    (void)dir_y;
    (void)speed;
    (void)turn_rate_deg;
    (void)ttl_s;
    (void)hit_radius;
    (void)blast_radius;
    return 0;
}

static void boss_enemy_emit_generic_destruction_fx(
    game_state* g,
    float x,
    float y,
    float vx,
    float vy,
    float radius,
    float ui_scale,
    int explosion_count
) {
    (void)g;
    (void)x;
    (void)y;
    (void)vx;
    (void)vy;
    (void)radius;
    (void)ui_scale;
    (void)explosion_count;
}

static void boss_enemy_spawn_eel_arc_burst(
    game_state* g,
    const enemy* e,
    int owner_index,
    int omnidirectional,
    int ray_count,
    float angle_offset
) {
    (void)g;
    (void)e;
    (void)owner_index;
    (void)omnidirectional;
    (void)ray_count;
    (void)angle_offset;
}

static void boss_enemy_spawn_support_wave(game_state* g, int owner_index, int phase, uint32_t seed) {
    (void)g;
    (void)owner_index;
    (void)phase;
    (void)seed;
}

static void boss_game_on_enemy_destroyed(game_state* g, float x, float y, float vx, float vy, int score_delta) {
    (void)g;
    (void)x;
    (void)y;
    (void)vx;
    (void)vy;
    (void)score_delta;
}
#else
#define boss_game_spawn_enemy_bullet game_spawn_enemy_bullet
#define boss_game_spawn_enemy_missile game_spawn_enemy_missile
#define boss_enemy_emit_generic_destruction_fx enemy_emit_generic_destruction_fx
#define boss_enemy_spawn_eel_arc_burst enemy_spawn_eel_arc_burst
#define boss_enemy_spawn_support_wave enemy_spawn_boss_support_wave
#define boss_game_on_enemy_destroyed game_on_enemy_destroyed
#endif

static const boss_blueprint k_boss_blueprints[] = {
    {
        .boss_id = 1,
        .name = "PROTO HEART",
        .controller_visual_kind = ENEMY_VISUAL_BOSS_CORE,
        .controller_flags = BOSS_ENEMY_FLAG_INVULNERABLE | BOSS_ENEMY_FLAG_NO_CONTACT_DAMAGE,
        .controller_radius = 132.0f,
        .sway_amp_x = 84.0f,
        .sway_amp_y = 138.0f,
        .sway_speed = 0.82f,
        .roll_amp_rad = 0.10f,
        .bounds_radius = 780.0f,
        .max_parts = 13
    }
};

static const boss_part_blueprint k_module_core_heart = {
    .role = BOSS_PART_ROLE_CORE,
    .flags = BOSS_ENEMY_FLAG_WEAKPOINT,
    .visual_kind = ENEMY_VISUAL_BOSS_CORE,
    .hp = 12,
    .radius = 96.0f,
    .local_x = 0.0f,
    .local_y = 0.0f,
    .local_rot = 0.0f,
    .emitter_count = 2,
    .emitter_local_x = { 42.0f, 42.0f },
    .emitter_local_y = { 30.0f, -30.0f }
};

static const boss_part_blueprint k_module_armor_plate = {
    .role = BOSS_PART_ROLE_ARMOR,
    .flags = 0,
    .visual_kind = ENEMY_VISUAL_BOSS_PLATE,
    .hp = 6,
    .radius = 90.0f,
    .local_x = 0.0f,
    .local_y = 0.0f,
    .local_rot = 0.0f,
    .emitter_count = 0
};

static const boss_part_blueprint k_module_turret = {
    .role = BOSS_PART_ROLE_TURRET,
    .flags = 0,
    .visual_kind = ENEMY_VISUAL_BOSS_TURRET,
    .hp = 8,
    .radius = 72.0f,
    .local_x = 0.0f,
    .local_y = 0.0f,
    .local_rot = 0.0f,
    .emitter_count = 1,
    .emitter_local_x = { 84.0f, 0.0f },
    .emitter_local_y = { 0.0f, 0.0f }
};

static const boss_part_blueprint k_module_joint = {
    .role = BOSS_PART_ROLE_JOINT,
    .flags = 0,
    .visual_kind = ENEMY_VISUAL_BOSS_JOINT,
    .hp = 6,
    .radius = 66.0f,
    .local_x = 0.0f,
    .local_y = 0.0f,
    .local_rot = 0.0f,
    .emitter_count = 1,
    .emitter_local_x = { 0.0f, 0.0f },
    .emitter_local_y = { 0.0f, 0.0f }
};

static const boss_part_blueprint k_module_vent = {
    .role = BOSS_PART_ROLE_VENT,
    .flags = BOSS_ENEMY_FLAG_WEAKPOINT,
    .visual_kind = ENEMY_VISUAL_BOSS_VENT,
    .hp = 6,
    .radius = 60.0f,
    .local_x = 0.0f,
    .local_y = 0.0f,
    .local_rot = 0.0f,
    .emitter_count = 1,
    .emitter_local_x = { 0.0f, 0.0f },
    .emitter_local_y = { 42.0f, 0.0f }
};

static int boss_append_part(
    boss_part_blueprint* out_parts,
    int out_cap,
    int count,
    const boss_part_blueprint* module,
    float local_x,
    float local_y,
    float local_rot
) {
    if (!out_parts || !module || count < 0 || count >= out_cap) {
        return count;
    }
    out_parts[count] = *module;
    out_parts[count].local_x = local_x;
    out_parts[count].local_y = local_y;
    out_parts[count].local_rot = local_rot;
    return count + 1;
}

static int boss_append_mirrored_pair(
    boss_part_blueprint* out_parts,
    int out_cap,
    int count,
    const boss_part_blueprint* module,
    float local_x,
    float local_y,
    float left_rot,
    float right_rot
) {
    count = boss_append_part(out_parts, out_cap, count, module, -fabsf(local_x), local_y, left_rot);
    count = boss_append_part(out_parts, out_cap, count, module, fabsf(local_x), local_y, right_rot);
    return count;
}

static float boss_controller_roll(const boss_controller_runtime* ctrl) {
    if (!ctrl) {
        return 0.0f;
    }
    return ctrl->roll_amp_rad * sinf(ctrl->sway_phase_s * 0.93f + hash01_u32(ctrl->seed ^ 0x6517u) * 6.2831853f);
}

typedef struct boss_emitter_point {
    int part_index;
    int emitter_index;
    float x;
    float y;
} boss_emitter_point;

enum boss_telegraph_kind {
    BOSS_TELEGRAPH_NONE = 0,
    BOSS_TELEGRAPH_TURRET_BURST = 1,
    BOSS_TELEGRAPH_JOINT_SPREAD = 2,
    BOSS_TELEGRAPH_VENT_BURST = 3,
    BOSS_TELEGRAPH_VENT_MISSILE = 4,
    BOSS_TELEGRAPH_CORE_BURST = 5,
    BOSS_TELEGRAPH_CORE_MISSILE = 6,
    BOSS_TELEGRAPH_LIGHTNING_FIELD = 7,
    BOSS_TELEGRAPH_LIGHTNING_PROX = 8,
    BOSS_TELEGRAPH_DRONE_RELEASE = 9
};

enum boss_movement_mode {
    BOSS_MOVEMENT_CRUISE = 0,
    BOSS_MOVEMENT_CHARGE = 1,
    BOSS_MOVEMENT_REVERSE = 2
};

int boss_build_parts(
    const boss_blueprint* blueprint,
    uint32_t seed,
    int variant_id,
    boss_part_blueprint* out_parts,
    int out_cap
) {
    const float pi = 3.14159265359f;
    uint32_t variant_seed;
    int count = 0;
    int add_outer_armor;
    int add_spine_pair;
    float arm_joint_x;
    float arm_y;
    float inner_armor_x;
    float inner_armor_y;
    float outer_armor_x;
    float outer_armor_y;
    float turret_x;
    float turret_y;
    float vent_x;
    float vent_y;
    float spine_y;

    if (!blueprint || !out_parts || out_cap <= 0) {
        return 0;
    }
    if (blueprint->max_parts > 0 && out_cap < blueprint->max_parts) {
        return 0;
    }
    if (variant_id < 0) {
        variant_id = -variant_id;
    }
    variant_seed = seed ^ ((uint32_t)variant_id * 0x9e3779b9U) ^ 0x52dce729U;

    if (blueprint->boss_id != 1) {
        return 0;
    }

    add_outer_armor = 1;
    add_spine_pair = 1;
    arm_joint_x = lerpf(136.0f, 164.0f, hash01_u32(variant_seed ^ 0x3111U));
    arm_y = lerpf(-34.0f, 34.0f, hash01_u32(variant_seed ^ 0x3222U));
    inner_armor_x = arm_joint_x + lerpf(86.0f, 112.0f, hash01_u32(variant_seed ^ 0x3333U));
    inner_armor_y = arm_y + lerpf(-16.0f, 14.0f, hash01_u32(variant_seed ^ 0x3444U));
    outer_armor_x = inner_armor_x + lerpf(74.0f, 102.0f, hash01_u32(variant_seed ^ 0x3555U));
    outer_armor_y = arm_y + lerpf(-26.0f, 22.0f, hash01_u32(variant_seed ^ 0x3666U));
    turret_x = (add_outer_armor ? outer_armor_x : inner_armor_x) + lerpf(20.0f, 34.0f, hash01_u32(variant_seed ^ 0x3777U));
    turret_y = arm_y + lerpf(-20.0f, 18.0f, hash01_u32(variant_seed ^ 0x3888U));
    vent_x = lerpf(58.0f, 78.0f, hash01_u32(variant_seed ^ 0x3999U));
    vent_y = lerpf(196.0f, 236.0f, hash01_u32(variant_seed ^ 0x3aaaU));
    spine_y = lerpf(146.0f, 182.0f, hash01_u32(variant_seed ^ 0x3bbbU));

    count = boss_append_part(out_parts, out_cap, count, &k_module_core_heart, 0.0f, 0.0f, 0.0f);
    count = boss_append_mirrored_pair(out_parts, out_cap, count, &k_module_joint, arm_joint_x, arm_y, pi, 0.0f);
    count = boss_append_mirrored_pair(out_parts, out_cap, count, &k_module_armor_plate, inner_armor_x, inner_armor_y, pi, 0.0f);
    if (add_outer_armor) {
        count = boss_append_mirrored_pair(out_parts, out_cap, count, &k_module_armor_plate, outer_armor_x, outer_armor_y, pi, 0.0f);
    }
    count = boss_append_mirrored_pair(out_parts, out_cap, count, &k_module_turret, turret_x, turret_y, pi, 0.0f);
    count = boss_append_mirrored_pair(out_parts, out_cap, count, &k_module_vent, vent_x, vent_y, pi * 0.5f, pi * 0.5f);
    if (add_spine_pair) {
        count = boss_append_part(out_parts, out_cap, count, &k_module_joint, 0.0f, spine_y, pi * 0.5f);
        count = boss_append_part(out_parts, out_cap, count, &k_module_joint, 0.0f, -spine_y, -pi * 0.5f);
    }
    return count;
}

static int boss_count_alive_parts_by_role(const game_state* g, int owner_index, int role) {
    int count = 0;
    if (!g || owner_index < 0 || owner_index >= MAX_ENEMIES) {
        return 0;
    }
    for (int i = 0; i < MAX_ENEMIES; ++i) {
        if (!g->enemies[i].active || !g->boss_attachments[i].active) {
            continue;
        }
        if (g->boss_attachments[i].owner_index != owner_index || g->boss_attachments[i].role != role) {
            continue;
        }
        count += 1;
    }
    return count;
}

static int boss_find_alive_child(const game_state* g, int owner_index) {
    if (!g || owner_index < 0 || owner_index >= MAX_ENEMIES) {
        return -1;
    }
    for (int i = 0; i < MAX_ENEMIES; ++i) {
        if (!g->enemies[i].active || !g->boss_attachments[i].active) {
            continue;
        }
        if (g->boss_attachments[i].owner_index == owner_index) {
            return i;
        }
    }
    return -1;
}

static int boss_collect_emitters(
    const game_state* g,
    int owner_index,
    int role,
    boss_emitter_point* out_emitters,
    int out_cap
) {
    int count = 0;
    if (!g || !out_emitters || out_cap <= 0 || owner_index < 0 || owner_index >= MAX_ENEMIES) {
        return 0;
    }
    for (int i = 0; i < MAX_ENEMIES; ++i) {
        const boss_enemy_attachment* attachment = &g->boss_attachments[i];
        const boss_controller_runtime* ctrl;
        const enemy* part;
        float world_rot;
        float c;
        float s;
        if (!g->enemies[i].active || !attachment->active) {
            continue;
        }
        if (attachment->owner_index != owner_index || attachment->role != role || attachment->emitter_count <= 0) {
            continue;
        }
        ctrl = &g->boss_controllers[owner_index];
        part = &g->enemies[i];
        world_rot = attachment->local_rot + boss_controller_roll(ctrl);
        c = cosf(world_rot);
        s = sinf(world_rot);
        for (int emitter_index = 0; emitter_index < attachment->emitter_count && count < out_cap; ++emitter_index) {
            out_emitters[count].part_index = i;
            out_emitters[count].emitter_index = emitter_index;
            out_emitters[count].x = part->b.x + attachment->emitter_local_x[emitter_index] * c -
                                    attachment->emitter_local_y[emitter_index] * s;
            out_emitters[count].y = part->b.y + attachment->emitter_local_x[emitter_index] * s +
                                    attachment->emitter_local_y[emitter_index] * c;
            count += 1;
        }
    }
    return count;
}

static void boss_tick_attachment_state(game_state* g, int owner_index, float dt) {
    if (!g || owner_index < 0 || owner_index >= MAX_ENEMIES) {
        return;
    }
    for (int i = 0; i < MAX_ENEMIES; ++i) {
        boss_enemy_attachment* attachment = &g->boss_attachments[i];
        enemy* part = &g->enemies[i];
        if (!attachment->active) {
            continue;
        }
        if (i == owner_index || attachment->owner_index == owner_index) {
            part->boss_telegraph = fmaxf(0.0f, part->boss_telegraph - dt * 3.8f);
        }
        if (attachment->owner_index != owner_index) {
            continue;
        }
        for (int emitter_index = 0; emitter_index < attachment->emitter_count; ++emitter_index) {
            attachment->emitter_cooldown_s[emitter_index] =
                fmaxf(0.0f, attachment->emitter_cooldown_s[emitter_index] - dt);
        }
    }
}

static int boss_get_emitter_point(
    const game_state* g,
    int owner_index,
    int part_index,
    int emitter_index,
    boss_emitter_point* out_emitter
) {
    const boss_enemy_attachment* attachment;
    const boss_controller_runtime* ctrl;
    const enemy* part;
    float world_rot;
    float c;
    float s;
    if (!g || !out_emitter || owner_index < 0 || owner_index >= MAX_ENEMIES || part_index < 0 || part_index >= MAX_ENEMIES) {
        return 0;
    }
    attachment = &g->boss_attachments[part_index];
    ctrl = &g->boss_controllers[owner_index];
    part = &g->enemies[part_index];
    if (!attachment->active || !part->active || attachment->owner_index != owner_index) {
        return 0;
    }
    if (emitter_index < 0 || emitter_index >= attachment->emitter_count) {
        return 0;
    }
    world_rot = attachment->local_rot + boss_controller_roll(ctrl);
    c = cosf(world_rot);
    s = sinf(world_rot);
    out_emitter->part_index = part_index;
    out_emitter->emitter_index = emitter_index;
    out_emitter->x = part->b.x + attachment->emitter_local_x[emitter_index] * c -
                     attachment->emitter_local_y[emitter_index] * s;
    out_emitter->y = part->b.y + attachment->emitter_local_x[emitter_index] * s +
                     attachment->emitter_local_y[emitter_index] * c;
    return 1;
}

static int boss_select_emitter(
    const game_state* g,
    int owner_index,
    int role,
    int selector,
    boss_emitter_point* out_emitter
) {
    boss_emitter_point emitters[8];
    const int emitter_count = boss_collect_emitters(g, owner_index, role, emitters, (int)(sizeof(emitters) / sizeof(emitters[0])));
    if (!g || !out_emitter || emitter_count <= 0) {
        return 0;
    }
    for (int offset = 0; offset < emitter_count; ++offset) {
        const boss_emitter_point* candidate = &emitters[(selector + offset) % emitter_count];
        const boss_enemy_attachment* attachment = &g->boss_attachments[candidate->part_index];
        if (candidate->emitter_index < 0 || candidate->emitter_index >= attachment->emitter_count) {
            continue;
        }
        if (attachment->emitter_cooldown_s[candidate->emitter_index] > 0.0f) {
            continue;
        }
        *out_emitter = *candidate;
        return 1;
    }
    return 0;
}

static void boss_mark_telegraph(game_state* g, int owner_index, int part_index, float strength) {
    if (!g || owner_index < 0 || owner_index >= MAX_ENEMIES) {
        return;
    }
    if (g->enemies[owner_index].active) {
        g->enemies[owner_index].boss_telegraph = fmaxf(g->enemies[owner_index].boss_telegraph, strength * 0.35f);
    }
    if (part_index >= 0 && part_index < MAX_ENEMIES && g->enemies[part_index].active) {
        g->enemies[part_index].boss_telegraph = fmaxf(g->enemies[part_index].boss_telegraph, strength);
    }
}

static void boss_mark_whole_boss(game_state* g, int owner_index, float owner_strength, float child_strength) {
    if (!g || owner_index < 0 || owner_index >= MAX_ENEMIES) {
        return;
    }
    if (g->enemies[owner_index].active) {
        g->enemies[owner_index].boss_telegraph =
            fmaxf(g->enemies[owner_index].boss_telegraph, owner_strength);
    }
    for (int i = 0; i < MAX_ENEMIES; ++i) {
        if (!g->boss_attachments[i].active || !g->enemies[i].active) {
            continue;
        }
        if (g->boss_attachments[i].owner_index != owner_index) {
            continue;
        }
        g->enemies[i].boss_telegraph =
            fmaxf(g->enemies[i].boss_telegraph, child_strength);
    }
}

static int boss_telegraph_role(int telegraph_kind) {
    switch (telegraph_kind) {
        case BOSS_TELEGRAPH_TURRET_BURST:
            return BOSS_PART_ROLE_TURRET;
        case BOSS_TELEGRAPH_JOINT_SPREAD:
            return BOSS_PART_ROLE_JOINT;
        case BOSS_TELEGRAPH_VENT_BURST:
        case BOSS_TELEGRAPH_VENT_MISSILE:
            return BOSS_PART_ROLE_VENT;
        case BOSS_TELEGRAPH_CORE_BURST:
        case BOSS_TELEGRAPH_CORE_MISSILE:
            return BOSS_PART_ROLE_CORE;
        case BOSS_TELEGRAPH_LIGHTNING_FIELD:
        case BOSS_TELEGRAPH_LIGHTNING_PROX:
        case BOSS_TELEGRAPH_DRONE_RELEASE:
            return BOSS_PART_ROLE_NONE;
        default:
            return BOSS_PART_ROLE_NONE;
    }
}

static float boss_telegraph_duration_s(int telegraph_kind) {
    switch (telegraph_kind) {
        case BOSS_TELEGRAPH_TURRET_BURST:
            return 0.22f;
        case BOSS_TELEGRAPH_JOINT_SPREAD:
            return 0.30f;
        case BOSS_TELEGRAPH_VENT_BURST:
            return 0.26f;
        case BOSS_TELEGRAPH_VENT_MISSILE:
            return 0.42f;
        case BOSS_TELEGRAPH_CORE_BURST:
            return 0.18f;
        case BOSS_TELEGRAPH_CORE_MISSILE:
            return 0.38f;
        case BOSS_TELEGRAPH_LIGHTNING_FIELD:
            return 0.58f;
        case BOSS_TELEGRAPH_LIGHTNING_PROX:
            return 0.42f;
        case BOSS_TELEGRAPH_DRONE_RELEASE:
            return 0.64f;
        default:
            return 0.0f;
    }
}

static float boss_emitter_cooldown_s(int telegraph_kind) {
    switch (telegraph_kind) {
        case BOSS_TELEGRAPH_TURRET_BURST:
            return 0.44f;
        case BOSS_TELEGRAPH_JOINT_SPREAD:
            return 0.60f;
        case BOSS_TELEGRAPH_VENT_BURST:
            return 0.54f;
        case BOSS_TELEGRAPH_VENT_MISSILE:
            return 1.30f;
        case BOSS_TELEGRAPH_CORE_BURST:
            return 0.30f;
        case BOSS_TELEGRAPH_CORE_MISSILE:
            return 1.10f;
        case BOSS_TELEGRAPH_LIGHTNING_FIELD:
            return 2.20f;
        case BOSS_TELEGRAPH_LIGHTNING_PROX:
            return 1.00f;
        case BOSS_TELEGRAPH_DRONE_RELEASE:
            return 3.20f;
        default:
            return 0.0f;
    }
}

static int boss_telegraph_consumes_burst(int telegraph_kind) {
    return telegraph_kind == BOSS_TELEGRAPH_TURRET_BURST ||
           telegraph_kind == BOSS_TELEGRAPH_JOINT_SPREAD ||
           telegraph_kind == BOSS_TELEGRAPH_VENT_BURST ||
           telegraph_kind == BOSS_TELEGRAPH_CORE_BURST;
}

static float boss_support_cooldown_s(const boss_controller_runtime* ctrl);

static int boss_begin_telegraph(game_state* g, int owner_index, int telegraph_kind, int selector) {
    boss_controller_runtime* ctrl;
    boss_emitter_point emitter;
    const int role = boss_telegraph_role(telegraph_kind);
    if (!g || owner_index < 0 || owner_index >= MAX_ENEMIES) {
        return 0;
    }
    ctrl = &g->boss_controllers[owner_index];
    if (!ctrl->active || ctrl->telegraph_active) {
        return 0;
    }
    if (role == BOSS_PART_ROLE_NONE) {
        ctrl->telegraph_active = 1;
        ctrl->telegraph_kind = telegraph_kind;
        ctrl->telegraph_part_index = owner_index;
        ctrl->telegraph_emitter_index = -1;
        ctrl->telegraph_total_s = boss_telegraph_duration_s(telegraph_kind);
        ctrl->telegraph_time_s = ctrl->telegraph_total_s;
        boss_mark_whole_boss(g, owner_index, 0.60f, 0.24f);
        boss_mark_telegraph(g, owner_index, owner_index, 0.55f);
        return 1;
    }
    if (!boss_select_emitter(g, owner_index, role, selector, &emitter)) {
        return 0;
    }
    ctrl->telegraph_active = 1;
    ctrl->telegraph_kind = telegraph_kind;
    ctrl->telegraph_part_index = emitter.part_index;
    ctrl->telegraph_emitter_index = emitter.emitter_index;
    ctrl->telegraph_total_s = boss_telegraph_duration_s(telegraph_kind);
    ctrl->telegraph_time_s = ctrl->telegraph_total_s;
    boss_mark_whole_boss(g, owner_index, 0.46f, 0.16f);
    boss_mark_telegraph(g, owner_index, emitter.part_index, 0.45f);
    return 1;
}

static int boss_count_phase1_shell_parts(const game_state* g, int owner_index) {
    return boss_count_alive_parts_by_role(g, owner_index, BOSS_PART_ROLE_ARMOR) +
           boss_count_alive_parts_by_role(g, owner_index, BOSS_PART_ROLE_TURRET);
}

static int boss_count_phase2_inner_parts(const game_state* g, int owner_index) {
    return boss_count_alive_parts_by_role(g, owner_index, BOSS_PART_ROLE_JOINT) +
           boss_count_alive_parts_by_role(g, owner_index, BOSS_PART_ROLE_VENT);
}

static void boss_queue_status(game_state* g, const char* title, const char* detail) {
    if (!g || !title || !title[0]) {
        return;
    }
    g->wave_announce_pending = 1;
    if (detail && detail[0]) {
        snprintf(g->wave_announce_text, sizeof(g->wave_announce_text), "%s\n%s", title, detail);
    } else {
        snprintf(g->wave_announce_text, sizeof(g->wave_announce_text), "%s", title);
    }
}

static void boss_emit_phase_cue(game_state* g, int owner_index, int phase) {
    int target_role_a = BOSS_PART_ROLE_NONE;
    int target_role_b = BOSS_PART_ROLE_NONE;
    if (!g || owner_index < 0 || owner_index >= MAX_ENEMIES) {
        return;
    }
    if (phase == 1) {
        target_role_a = BOSS_PART_ROLE_JOINT;
        target_role_b = BOSS_PART_ROLE_VENT;
    } else if (phase == 2) {
        target_role_a = BOSS_PART_ROLE_CORE;
    } else {
        return;
    }
    if (g->enemies[owner_index].active) {
        g->enemies[owner_index].boss_telegraph = 1.0f;
    }
    boss_mark_whole_boss(g, owner_index, 1.0f, 0.30f);
    for (int i = 0; i < MAX_ENEMIES; ++i) {
        boss_enemy_attachment* attachment = &g->boss_attachments[i];
        enemy* part = &g->enemies[i];
        if (!attachment->active || !part->active || attachment->owner_index != owner_index) {
            continue;
        }
        if (attachment->role != target_role_a && attachment->role != target_role_b) {
            continue;
        }
        part->boss_telegraph = 1.0f;
        boss_enemy_emit_generic_destruction_fx(
            g,
            part->b.x,
            part->b.y,
            part->b.vx,
            part->b.vy,
            fmaxf(part->radius * 0.55f, 7.0f * boss_ui_scale(g)),
            boss_ui_scale(g),
            10
        );
    }
}

static void boss_apply_phase(game_state* g, int owner_index, int phase) {
    boss_controller_runtime* ctrl;
    if (!g || owner_index < 0 || owner_index >= MAX_ENEMIES) {
        return;
    }
    ctrl = &g->boss_controllers[owner_index];
    if (!ctrl->active) {
        return;
    }
    ctrl->phase = phase;
    ctrl->phase_time_s = 0.0f;
    ctrl->attack_cooldown_s = (phase > 0) ? 1.30f : 0.80f;
    ctrl->burst_gap_s = 0.0f;
    ctrl->burst_shots_left = 0;
    ctrl->attack_index = 0;
    ctrl->telegraph_active = 0;
    ctrl->telegraph_kind = BOSS_TELEGRAPH_NONE;
    ctrl->telegraph_part_index = -1;
    ctrl->telegraph_emitter_index = -1;
    ctrl->telegraph_time_s = 0.0f;
    ctrl->telegraph_total_s = 0.0f;
    ctrl->movement_mode = BOSS_MOVEMENT_CRUISE;
    ctrl->movement_timer_s = 0.0f;
    ctrl->behind_timer_s = 0.0f;
    ctrl->lightning_cooldown_s = 1.2f;
    ctrl->support_cooldown_s = boss_support_cooldown_s(ctrl) * 0.7f;
    ctrl->movement_target_x = ctrl->anchor_x;
    ctrl->movement_target_y = ctrl->anchor_y;
    for (int i = 0; i < MAX_ENEMIES; ++i) {
        boss_enemy_attachment* attachment = &g->boss_attachments[i];
        if (!attachment->active || attachment->owner_index != owner_index) {
            continue;
        }
        attachment->flags &= ~BOSS_ENEMY_FLAG_INVULNERABLE;
        if (attachment->role == BOSS_PART_ROLE_ARMOR || attachment->role == BOSS_PART_ROLE_TURRET) {
            if (phase != 0) {
                attachment->flags |= BOSS_ENEMY_FLAG_INVULNERABLE;
            }
        } else if (attachment->role == BOSS_PART_ROLE_JOINT || attachment->role == BOSS_PART_ROLE_VENT) {
            if (phase != 1) {
                attachment->flags |= BOSS_ENEMY_FLAG_INVULNERABLE;
            }
        } else if (attachment->role == BOSS_PART_ROLE_CORE) {
            if (phase != 2) {
                attachment->flags |= BOSS_ENEMY_FLAG_INVULNERABLE;
            }
        }
    }
    if (phase == 1) {
        boss_queue_status(g, "BOSS PHASE 2", "VENTS AND JOINTS EXPOSED");
        boss_emit_phase_cue(g, owner_index, phase);
    } else if (phase == 2) {
        boss_queue_status(g, "BOSS PHASE 3", "CORE EXPOSED");
        boss_emit_phase_cue(g, owner_index, phase);
    }
}

static void boss_reconcile_phase(game_state* g, int owner_index) {
    const int shell_alive = boss_count_phase1_shell_parts(g, owner_index);
    const int inner_alive = boss_count_phase2_inner_parts(g, owner_index);
    boss_controller_runtime* ctrl;
    int target_phase = 0;
    if (!g || owner_index < 0 || owner_index >= MAX_ENEMIES) {
        return;
    }
    ctrl = &g->boss_controllers[owner_index];
    if (!ctrl->active) {
        return;
    }
    if (shell_alive <= 0) {
        target_phase = 1;
    }
    if (inner_alive <= 0) {
        target_phase = 2;
    }
    if (target_phase != ctrl->phase) {
        boss_apply_phase(g, owner_index, target_phase);
    }
}

static int boss_fire_bullet_from(
    game_state* g,
    float x,
    float y,
    float dir_x,
    float dir_y,
    float speed,
    float ttl_s,
    float radius
) {
    return boss_game_spawn_enemy_bullet(g, x, y, dir_x, dir_y, speed, ttl_s, radius);
}

static void boss_fire_aimed_from_point(game_state* g, float x, float y, float speed, float ttl_s, float radius, float aim_error_rad) {
    float dir_x;
    float dir_y;
    float c;
    float s;
    if (!g) {
        return;
    }
    dir_x = g->player.b.x - x;
    dir_y = g->player.b.y - y;
    normalize2(&dir_x, &dir_y);
    c = cosf(aim_error_rad);
    s = sinf(aim_error_rad);
    boss_fire_bullet_from(
        g,
        x,
        y,
        dir_x * c - dir_y * s,
        dir_x * s + dir_y * c,
        speed,
        ttl_s,
        radius
    );
}

static void boss_fire_spread_from_point(
    game_state* g,
    float x,
    float y,
    int shot_count,
    float spread_rad,
    float speed,
    float ttl_s,
    float radius
) {
    float base_x;
    float base_y;
    if (!g || shot_count <= 0) {
        return;
    }
    base_x = g->player.b.x - x;
    base_y = g->player.b.y - y;
    normalize2(&base_x, &base_y);
    for (int i = 0; i < shot_count; ++i) {
        const float t = (shot_count <= 1) ? 0.5f : (float)i / (float)(shot_count - 1);
        const float a = lerpf(-spread_rad * 0.5f, spread_rad * 0.5f, t);
        const float c = cosf(a);
        const float s = sinf(a);
        boss_fire_bullet_from(
            g,
            x,
            y,
            base_x * c - base_y * s,
            base_x * s + base_y * c,
            speed,
            ttl_s,
            radius
        );
    }
}

static float boss_phase_charge_cooldown_s(const boss_controller_runtime* ctrl) {
    if (!ctrl) {
        return 2.2f;
    }
    if (ctrl->phase >= 2) {
        return 1.65f;
    }
    if (ctrl->phase == 1) {
        return 2.00f;
    }
    return 2.50f;
}

static float boss_phase_charge_duration_s(const boss_controller_runtime* ctrl, int movement_mode) {
    if (!ctrl) {
        return 0.85f;
    }
    if (movement_mode == BOSS_MOVEMENT_REVERSE) {
        return (ctrl->phase >= 2) ? 1.00f : 0.84f;
    }
    if (ctrl->phase >= 2) {
        return 1.10f;
    }
    if (ctrl->phase == 1) {
        return 0.92f;
    }
    return 0.76f;
}

static float boss_lightning_cooldown_s(const boss_controller_runtime* ctrl, int proximity_trigger) {
    if (!ctrl) {
        return proximity_trigger ? 2.0f : 3.5f;
    }
    if (proximity_trigger) {
        if (ctrl->phase >= 2) return 2.10f;
        if (ctrl->phase == 1) return 2.60f;
        return 3.10f;
    }
    if (ctrl->phase >= 2) return 4.40f;
    if (ctrl->phase == 1) return 5.20f;
    return 6.20f;
}

static float boss_support_cooldown_s(const boss_controller_runtime* ctrl) {
    if (!ctrl) {
        return 4.8f;
    }
    if (ctrl->phase >= 2) return 4.4f;
    if (ctrl->phase == 1) return 5.0f;
    return 4.8f;
}

static void boss_spawn_lightning_field(game_state* g, int owner_index, int proximity_trigger) {
    boss_controller_runtime* ctrl;
    enemy* controller;
    int ray_count;
    float angle_offset;
    if (!g || owner_index < 0 || owner_index >= MAX_ENEMIES) {
        return;
    }
    ctrl = &g->boss_controllers[owner_index];
    controller = &g->enemies[owner_index];
    if (!ctrl->active || !controller->active) {
        return;
    }
    ray_count = proximity_trigger ? 12 : ((ctrl->phase >= 2) ? 11 : 9);
    angle_offset = ctrl->sway_phase_s * (0.75f + 0.10f * (float)ctrl->phase);
    boss_enemy_spawn_eel_arc_burst(g, controller, owner_index, 1, ray_count, angle_offset);
    ctrl->lightning_cooldown_s = boss_lightning_cooldown_s(ctrl, proximity_trigger);
    boss_mark_telegraph(g, owner_index, owner_index, proximity_trigger ? 1.0f : 0.78f);
}

static void boss_begin_movement_pattern(game_state* g, int owner_index, int movement_mode) {
    boss_controller_runtime* ctrl;
    enemy* e;
    float offset_x;
    float y_clamp;
    if (!g || owner_index < 0 || owner_index >= MAX_ENEMIES) {
        return;
    }
    ctrl = &g->boss_controllers[owner_index];
    e = &g->enemies[owner_index];
    if (!ctrl->active || !e->active) {
        return;
    }
    ctrl->movement_mode = movement_mode;
    ctrl->movement_timer_s = boss_phase_charge_duration_s(ctrl, movement_mode);
    ctrl->movement_cooldown_s = boss_phase_charge_cooldown_s(ctrl);
    ctrl->behind_timer_s = 0.0f;
    y_clamp = fminf(ctrl->bounds_radius * 0.42f, g->world_h * 0.34f);
    ctrl->movement_target_y = clampf(g->player.b.y, y_clamp, g->world_h - y_clamp);
    offset_x = ctrl->bounds_radius * ((movement_mode == BOSS_MOVEMENT_REVERSE) ? 0.34f : -0.24f);
    ctrl->movement_target_x = g->player.b.x + offset_x;
}

static void boss_update_movement_pattern(game_state* g, int owner_index, float dt) {
    boss_controller_runtime* ctrl;
    enemy* e;
    float behind_threshold;
    float ahead_threshold;
    float player_dx;
    if (!g || owner_index < 0 || owner_index >= MAX_ENEMIES) {
        return;
    }
    ctrl = &g->boss_controllers[owner_index];
    e = &g->enemies[owner_index];
    if (!ctrl->active || !e->active || ctrl->destruction_active) {
        return;
    }
    if (ctrl->movement_cooldown_s > 0.0f) {
        ctrl->movement_cooldown_s -= dt;
    }
    if (ctrl->movement_timer_s > 0.0f) {
        ctrl->movement_timer_s -= dt;
        if (ctrl->movement_timer_s <= 0.0f) {
            ctrl->movement_mode = BOSS_MOVEMENT_CRUISE;
        }
        return;
    }
    ctrl->movement_mode = BOSS_MOVEMENT_CRUISE;
    player_dx = g->player.b.x - e->b.x;
    behind_threshold = ctrl->bounds_radius * 0.12f;
    ahead_threshold = ctrl->bounds_radius * 0.18f;
    if (player_dx > behind_threshold) {
        ctrl->behind_timer_s += dt;
    } else {
        ctrl->behind_timer_s = fmaxf(0.0f, ctrl->behind_timer_s - dt * 1.4f);
    }
    if (ctrl->movement_cooldown_s > 0.0f) {
        return;
    }
    if (ctrl->behind_timer_s >= 0.40f) {
        boss_begin_movement_pattern(g, owner_index, BOSS_MOVEMENT_REVERSE);
        return;
    }
    if (player_dx < -ahead_threshold) {
        boss_begin_movement_pattern(g, owner_index, BOSS_MOVEMENT_CHARGE);
    }
}

static void boss_execute_telegraph(game_state* g, int owner_index) {
    boss_controller_runtime* ctrl;
    boss_emitter_point emitter;
    boss_enemy_attachment* attachment;
    int used_emitter = 0;
    if (!g || owner_index < 0 || owner_index >= MAX_ENEMIES) {
        return;
    }
    ctrl = &g->boss_controllers[owner_index];
    if (!ctrl->active || !ctrl->telegraph_active) {
        return;
    }
    attachment = &g->boss_attachments[ctrl->telegraph_part_index];
    if (boss_telegraph_role(ctrl->telegraph_kind) != BOSS_PART_ROLE_NONE) {
        if (!boss_get_emitter_point(g, owner_index, ctrl->telegraph_part_index, ctrl->telegraph_emitter_index, &emitter)) {
            ctrl->telegraph_active = 0;
            ctrl->telegraph_kind = BOSS_TELEGRAPH_NONE;
            return;
        }
        used_emitter = 1;
    }
    switch (ctrl->telegraph_kind) {
        case BOSS_TELEGRAPH_TURRET_BURST: {
            const float error = (ctrl->attack_index & 1) ? 0.06f : -0.06f;
            boss_fire_aimed_from_point(g, emitter.x, emitter.y, 720.0f, 6.8f, 11.0f, error);
        } break;
        case BOSS_TELEGRAPH_JOINT_SPREAD:
            boss_fire_spread_from_point(g, emitter.x, emitter.y, 5, 1.10f, 620.0f, 5.8f, 10.0f);
            break;
        case BOSS_TELEGRAPH_VENT_BURST: {
            const float error = (ctrl->attack_index & 1) ? 0.12f : -0.12f;
            boss_fire_aimed_from_point(g, emitter.x, emitter.y, 540.0f, 5.2f, 9.0f, error);
        } break;
        case BOSS_TELEGRAPH_VENT_MISSILE: {
            float dir_x = g->player.b.x - emitter.x;
            float dir_y = g->player.b.y - emitter.y;
            normalize2(&dir_x, &dir_y);
            (void)boss_game_spawn_enemy_missile(g, emitter.x, emitter.y, dir_x, dir_y, 460.0f, 260.0f, 10.0f, 18.0f, 36.0f);
        } break;
        case BOSS_TELEGRAPH_CORE_BURST: {
            const float error = lerpf(-0.04f, 0.04f, hash01_u32(ctrl->seed ^ (uint32_t)(ctrl->attack_index * 0x9e37u)));
            boss_fire_aimed_from_point(g, emitter.x, emitter.y, 860.0f, 7.2f, 12.0f, error);
        } break;
        case BOSS_TELEGRAPH_CORE_MISSILE: {
            float dir_x = g->player.b.x - emitter.x;
            float dir_y = g->player.b.y - emitter.y;
            normalize2(&dir_x, &dir_y);
            (void)boss_game_spawn_enemy_missile(g, emitter.x, emitter.y, dir_x, dir_y, 500.0f, 280.0f, 10.5f, 20.0f, 40.0f);
        } break;
        case BOSS_TELEGRAPH_LIGHTNING_FIELD:
            boss_spawn_lightning_field(g, owner_index, 0);
            ctrl->attack_cooldown_s = fmaxf(ctrl->attack_cooldown_s, 0.70f);
            break;
        case BOSS_TELEGRAPH_LIGHTNING_PROX:
            boss_spawn_lightning_field(g, owner_index, 1);
            ctrl->attack_cooldown_s = fmaxf(ctrl->attack_cooldown_s, 0.45f);
            break;
        case BOSS_TELEGRAPH_DRONE_RELEASE:
            boss_enemy_spawn_support_wave(g, owner_index, ctrl->phase, ctrl->seed ^ (uint32_t)(ctrl->attack_index * 0x9e37u));
            ctrl->support_cooldown_s = boss_support_cooldown_s(ctrl);
            ctrl->attack_cooldown_s = fmaxf(ctrl->attack_cooldown_s, (ctrl->phase >= 2) ? 1.35f : 1.60f);
            ctrl->burst_gap_s = fmaxf(ctrl->burst_gap_s, 0.16f);
            boss_queue_status(g, "DRONE BAY OPEN", "SUPPORT SWARM DEPLOYED");
            break;
        default:
            break;
    }
    if (used_emitter &&
        ctrl->telegraph_emitter_index >= 0 &&
        ctrl->telegraph_emitter_index < attachment->emitter_count) {
        attachment->emitter_cooldown_s[ctrl->telegraph_emitter_index] =
            fmaxf(attachment->emitter_cooldown_s[ctrl->telegraph_emitter_index], boss_emitter_cooldown_s(ctrl->telegraph_kind));
    }
    boss_mark_telegraph(g, owner_index, ctrl->telegraph_part_index, 1.0f);
    ctrl->telegraph_active = 0;
    ctrl->telegraph_kind = BOSS_TELEGRAPH_NONE;
    ctrl->telegraph_part_index = -1;
    ctrl->telegraph_emitter_index = -1;
    ctrl->telegraph_time_s = 0.0f;
    ctrl->telegraph_total_s = 0.0f;
}

static void boss_update_attacks(game_state* g, int owner_index, float dt) {
    boss_controller_runtime* ctrl;
    enemy* controller;
    float player_dx;
    float player_dy;
    float player_dist;
    if (!g || owner_index < 0 || owner_index >= MAX_ENEMIES) {
        return;
    }
    ctrl = &g->boss_controllers[owner_index];
    controller = &g->enemies[owner_index];
    if (!ctrl->active || !controller->active || g->lives <= 0) {
        return;
    }

    boss_update_movement_pattern(g, owner_index, dt);
    ctrl->phase_time_s += dt;
    if (ctrl->lightning_cooldown_s > 0.0f) {
        ctrl->lightning_cooldown_s -= dt;
    }
    if (ctrl->support_cooldown_s > 0.0f) {
        ctrl->support_cooldown_s -= dt;
    }
    if (ctrl->attack_cooldown_s > 0.0f) {
        ctrl->attack_cooldown_s -= dt;
    }
    if (ctrl->burst_gap_s > 0.0f) {
        ctrl->burst_gap_s -= dt;
    }
    player_dx = g->player.b.x - controller->b.x;
    player_dy = g->player.b.y - controller->b.y;
    player_dist = sqrtf(player_dx * player_dx + player_dy * player_dy);

    if (ctrl->lightning_cooldown_s <= 0.0f && player_dist <= ctrl->bounds_radius * 0.56f) {
        (void)boss_begin_telegraph(g, owner_index, BOSS_TELEGRAPH_LIGHTNING_PROX, ctrl->attack_index);
    }

    if (ctrl->support_cooldown_s <= 0.0f &&
        !ctrl->telegraph_active &&
        ctrl->burst_shots_left <= 0 &&
        ctrl->attack_cooldown_s <= 0.70f) {
        if (boss_begin_telegraph(g, owner_index, BOSS_TELEGRAPH_DRONE_RELEASE, ctrl->attack_index)) {
            boss_begin_movement_pattern(g, owner_index, BOSS_MOVEMENT_REVERSE);
        }
    }

    if (ctrl->phase >= 1 &&
        ctrl->lightning_cooldown_s <= 0.0f &&
        !ctrl->telegraph_active &&
        ctrl->burst_shots_left <= 0 &&
        ctrl->attack_cooldown_s <= 0.18f &&
        ((ctrl->attack_index + ctrl->phase) % 4) == 0) {
        (void)boss_begin_telegraph(g, owner_index, BOSS_TELEGRAPH_LIGHTNING_FIELD, ctrl->attack_index);
    }

    

    if (ctrl->telegraph_active) {
        const int telegraph_kind = ctrl->telegraph_kind;
        const float telegraph_progress = 1.0f - (ctrl->telegraph_time_s / fmaxf(ctrl->telegraph_total_s, 1.0e-5f));
        boss_mark_telegraph(
            g,
            owner_index,
            ctrl->telegraph_part_index,
            clampf(0.45f + telegraph_progress * 0.80f, 0.0f, 1.0f)
        );
        ctrl->telegraph_time_s -= dt;
        if (ctrl->telegraph_time_s <= 0.0f) {
            boss_execute_telegraph(g, owner_index);
            if (boss_telegraph_consumes_burst(telegraph_kind) && ctrl->burst_shots_left > 0) {
                ctrl->attack_index += 1;
                ctrl->burst_shots_left -= 1;
                ctrl->burst_gap_s = (ctrl->phase == 2) ? 0.10f : ((ctrl->phase == 1) ? 0.14f : 0.16f);
            }
        }
        return;
    }

    if (ctrl->burst_shots_left > 0 && ctrl->burst_gap_s <= 0.0f) {
        int started = 0;
        if (ctrl->phase == 0) {
            started = boss_begin_telegraph(g, owner_index, BOSS_TELEGRAPH_TURRET_BURST, ctrl->attack_index);
        } else if (ctrl->phase == 1) {
            if ((ctrl->attack_index & 1) == 0) {
                started = boss_begin_telegraph(g, owner_index, BOSS_TELEGRAPH_JOINT_SPREAD, ctrl->attack_index);
                if (!started) {
                    started = boss_begin_telegraph(g, owner_index, BOSS_TELEGRAPH_VENT_BURST, ctrl->attack_index);
                }
            } else {
                started = boss_begin_telegraph(g, owner_index, BOSS_TELEGRAPH_VENT_BURST, ctrl->attack_index);
                if (!started) {
                    started = boss_begin_telegraph(g, owner_index, BOSS_TELEGRAPH_JOINT_SPREAD, ctrl->attack_index);
                }
            }
        } else {
            started = boss_begin_telegraph(g, owner_index, BOSS_TELEGRAPH_CORE_BURST, ctrl->attack_index);
        }
        if (!started) {
            ctrl->burst_gap_s = 0.08f;
        }
    }

    if (ctrl->burst_shots_left <= 0 && ctrl->attack_cooldown_s <= 0.0f) {
        if (ctrl->phase == 0) {
            ctrl->burst_shots_left = 3;
            ctrl->attack_cooldown_s = 1.35f;
        } else if (ctrl->phase == 1) {
            ctrl->burst_shots_left = 3;
            ctrl->attack_cooldown_s = 1.65f;
            (void)boss_begin_telegraph(g, owner_index, BOSS_TELEGRAPH_VENT_MISSILE, ctrl->attack_index / 2);
        } else {
            ctrl->burst_shots_left = 4;
            ctrl->attack_cooldown_s = 1.25f;
            (void)boss_begin_telegraph(g, owner_index, BOSS_TELEGRAPH_CORE_MISSILE, ctrl->attack_index / 2);
        }
        ctrl->burst_gap_s = 0.08f;
    }
}

static void boss_begin_destruction(game_state* g, int owner_index) {
    boss_controller_runtime* ctrl;
    if (!g || owner_index < 0 || owner_index >= MAX_ENEMIES) {
        return;
    }
    ctrl = &g->boss_controllers[owner_index];
    if (!ctrl->active || ctrl->destruction_active) {
        return;
    }
    ctrl->destruction_active = 1;
    ctrl->telegraph_active = 0;
    ctrl->telegraph_kind = BOSS_TELEGRAPH_NONE;
    ctrl->telegraph_part_index = -1;
    ctrl->telegraph_emitter_index = -1;
    ctrl->destruction_time_s = 0.0f;
    ctrl->detonation_timer_s = 0.18f;
    ctrl->burst_shots_left = 0;
    ctrl->attack_cooldown_s = 999.0f;
    ctrl->burst_gap_s = 999.0f;
    ctrl->telegraph_time_s = 0.0f;
    ctrl->telegraph_total_s = 0.0f;
    for (int i = 0; i < MAX_ENEMIES; ++i) {
        boss_enemy_attachment* attachment = &g->boss_attachments[i];
        if (!attachment->active || attachment->owner_index != owner_index) {
            continue;
        }
        attachment->flags |= BOSS_ENEMY_FLAG_INVULNERABLE | BOSS_ENEMY_FLAG_NO_CONTACT_DAMAGE;
    }
    boss_queue_status(g, "BOSS DESTABILIZED", "REACTOR MELTDOWN");
}

static void boss_finish_destruction(game_state* g, int owner_index) {
    enemy* controller;
    if (!g || owner_index < 0 || owner_index >= MAX_ENEMIES) {
        return;
    }
    controller = &g->enemies[owner_index];
    if (!controller->active || !g->boss_controllers[owner_index].active) {
        return;
    }
    boss_enemy_emit_generic_destruction_fx(
        g,
        controller->b.x,
        controller->b.y,
        controller->b.vx,
        controller->b.vy,
        fmaxf(controller->radius * 2.6f, 22.0f * boss_ui_scale(g)),
        boss_ui_scale(g),
        56
    );
    boss_game_on_enemy_destroyed(g, controller->b.x, controller->b.y, controller->b.vx, controller->b.vy, 500);
    boss_queue_status(g, "BOSS DESTROYED", boss_name_for_id(g->boss_controllers[owner_index].boss_id));
    controller->active = 0;
    boss_on_enemy_destroyed(g, owner_index);
}

static void boss_update_destruction(game_state* g, int owner_index, float dt) {
    boss_controller_runtime* ctrl;
    enemy* controller;
    if (!g || owner_index < 0 || owner_index >= MAX_ENEMIES) {
        return;
    }
    ctrl = &g->boss_controllers[owner_index];
    controller = &g->enemies[owner_index];
    if (!ctrl->active || !ctrl->destruction_active || !controller->active) {
        return;
    }

    ctrl->destruction_time_s += dt;
    ctrl->detonation_timer_s -= dt;
    if (ctrl->detonation_timer_s <= 0.0f) {
        const int part_index = boss_find_alive_child(g, owner_index);
        if (part_index >= 0) {
            enemy* part = &g->enemies[part_index];
            boss_enemy_emit_generic_destruction_fx(
                g,
                part->b.x,
                part->b.y,
                part->b.vx,
                part->b.vy,
                fmaxf(part->radius, 10.0f * boss_ui_scale(g)),
                boss_ui_scale(g),
                22
            );
            part->active = 0;
            boss_reset_enemy_runtime(g, part_index);
            if (ctrl->live_part_count > 0) {
                ctrl->live_part_count -= 1;
            }
        } else {
            boss_enemy_emit_generic_destruction_fx(
                g,
                controller->b.x + cosf(ctrl->destruction_time_s * 9.0f) * controller->radius,
                controller->b.y + sinf(ctrl->destruction_time_s * 12.0f) * controller->radius,
                controller->b.vx,
                controller->b.vy,
                fmaxf(controller->radius * 0.8f, 8.0f * boss_ui_scale(g)),
                boss_ui_scale(g),
                14
            );
        }
        ctrl->detonation_timer_s = fmaxf(0.08f, 0.34f - ctrl->destruction_time_s * 0.05f);
    }

    if ((ctrl->live_part_count <= 0 && ctrl->destruction_time_s >= 1.6f) || ctrl->destruction_time_s >= 3.6f) {
        boss_finish_destruction(g, owner_index);
    }
}

static void boss_snap_part_to_owner(game_state* g, int enemy_index, float dt) {
    const boss_enemy_attachment* attachment;
    enemy* part;
    enemy* owner;
    const boss_controller_runtime* ctrl;
    float prev_x;
    float prev_y;
    float roll;
    float c;
    float s;
    float ox;
    float oy;

    if (!g || enemy_index < 0 || enemy_index >= MAX_ENEMIES) {
        return;
    }
    attachment = &g->boss_attachments[enemy_index];
    if (!attachment->active || attachment->owner_index < 0 || attachment->owner_index >= MAX_ENEMIES) {
        return;
    }
    part = &g->enemies[enemy_index];
    owner = &g->enemies[attachment->owner_index];
    ctrl = &g->boss_controllers[attachment->owner_index];
    if (!part->active || !owner->active || !ctrl->active) {
        part->active = 0;
        boss_reset_enemy_runtime(g, enemy_index);
        return;
    }

    prev_x = part->b.x;
    prev_y = part->b.y;
    roll = boss_controller_roll(ctrl);
    c = cosf(roll + attachment->local_rot);
    s = sinf(roll + attachment->local_rot);
    ox = attachment->local_x * c - attachment->local_y * s;
    oy = attachment->local_x * s + attachment->local_y * c;

    part->b.x = owner->b.x + ox;
    part->b.y = owner->b.y + oy;
    part->b.vx = (dt > 1.0e-5f) ? (part->b.x - prev_x) / dt : owner->b.vx;
    part->b.vy = (dt > 1.0e-5f) ? (part->b.y - prev_y) / dt : owner->b.vy;
    part->b.ax = 0.0f;
    part->b.ay = 0.0f;
    part->facing_x = cosf(attachment->local_rot);
    part->facing_y = sinf(attachment->local_rot);
}

const boss_blueprint* boss_lookup_blueprint(int boss_id) {
    for (size_t i = 0; i < sizeof(k_boss_blueprints) / sizeof(k_boss_blueprints[0]); ++i) {
        if (k_boss_blueprints[i].boss_id == boss_id) {
            return &k_boss_blueprints[i];
        }
    }
    return NULL;
}

int boss_is_valid_blueprint_id(int boss_id) {
    return boss_lookup_blueprint(boss_id) != NULL;
}

const char* boss_name_for_id(int boss_id) {
    const boss_blueprint* blueprint = boss_lookup_blueprint(boss_id);
    return blueprint ? blueprint->name : NULL;
}

void boss_reset_enemy_runtime(game_state* g, int enemy_index) {
    if (!g || enemy_index < 0 || enemy_index >= MAX_ENEMIES) {
        return;
    }
    memset(&g->boss_attachments[enemy_index], 0, sizeof(g->boss_attachments[enemy_index]));
    memset(&g->boss_controllers[enemy_index], 0, sizeof(g->boss_controllers[enemy_index]));
    g->boss_attachments[enemy_index].owner_index = -1;
}

void boss_configure_controller(
    game_state* g,
    int enemy_index,
    const boss_blueprint* blueprint,
    uint32_t seed,
    float anchor_x,
    float anchor_y,
    float difficulty_scale,
    int gates_exit
) {
    boss_controller_runtime* ctrl;
    boss_enemy_attachment* attachment;
    if (!g || !blueprint || enemy_index < 0 || enemy_index >= MAX_ENEMIES) {
        return;
    }
    ctrl = &g->boss_controllers[enemy_index];
    attachment = &g->boss_attachments[enemy_index];
    boss_reset_enemy_runtime(g, enemy_index);
    ctrl->active = 1;
    ctrl->boss_id = blueprint->boss_id;
    ctrl->seed = seed;
    ctrl->phase = 0;
    ctrl->live_part_count = 0;
    ctrl->burst_shots_left = 0;
    ctrl->attack_index = 0;
    ctrl->gates_exit = gates_exit ? 1 : 0;
    ctrl->destruction_active = 0;
    ctrl->telegraph_active = 0;
    ctrl->telegraph_kind = BOSS_TELEGRAPH_NONE;
    ctrl->telegraph_part_index = -1;
    ctrl->telegraph_emitter_index = -1;
    ctrl->anchor_x = anchor_x;
    ctrl->anchor_y = anchor_y;
    ctrl->sway_phase_s = hash01_u32(seed ^ 0xA713u) * 6.2831853f;
    ctrl->sway_amp_x = blueprint->sway_amp_x;
    ctrl->sway_amp_y = blueprint->sway_amp_y;
    ctrl->sway_speed = blueprint->sway_speed;
    ctrl->roll_amp_rad = blueprint->roll_amp_rad;
    ctrl->bounds_radius = blueprint->bounds_radius;
    ctrl->difficulty_scale = difficulty_scale;
    ctrl->phase_time_s = 0.0f;
    ctrl->attack_cooldown_s = 0.80f;
    ctrl->burst_gap_s = 0.0f;
    ctrl->telegraph_time_s = 0.0f;
    ctrl->telegraph_total_s = 0.0f;
    ctrl->movement_mode = BOSS_MOVEMENT_CRUISE;
    ctrl->movement_timer_s = 0.0f;
    ctrl->movement_cooldown_s = 1.8f;
    ctrl->behind_timer_s = 0.0f;
    ctrl->lightning_cooldown_s = 1.6f;
    ctrl->support_cooldown_s = 2.8f;
    ctrl->movement_target_x = anchor_x;
    ctrl->movement_target_y = anchor_y;
    ctrl->destruction_time_s = 0.0f;
    ctrl->detonation_timer_s = 0.0f;

    attachment->active = 1;
    attachment->owner_index = -1;
    attachment->role = BOSS_PART_ROLE_CONTROLLER;
    attachment->flags = blueprint->controller_flags;
    attachment->hp_max = 1;
    attachment->emitter_count = 0;
    attachment->local_x = 0.0f;
    attachment->local_y = 0.0f;
    attachment->local_rot = 0.0f;

    g->enemies[enemy_index].eel_weapon_range = blueprint->bounds_radius * 0.56f;
    g->enemies[enemy_index].eel_weapon_duration_s = 1.55f;
    g->enemies[enemy_index].eel_weapon_damage_interval_s = 0.11f;
    g->enemies[enemy_index].eel_heading_rad = 3.14159265359f;
}

void boss_configure_part(
    game_state* g,
    int enemy_index,
    int owner_index,
    const boss_part_blueprint* part,
    int hp_max
) {
    boss_enemy_attachment* attachment;
    if (!g || !part || enemy_index < 0 || enemy_index >= MAX_ENEMIES || owner_index < 0 || owner_index >= MAX_ENEMIES) {
        return;
    }
    attachment = &g->boss_attachments[enemy_index];
    boss_reset_enemy_runtime(g, enemy_index);
    attachment->active = 1;
    attachment->owner_index = owner_index;
    attachment->role = part->role;
    attachment->flags = part->flags;
    if (part->role != BOSS_PART_ROLE_ARMOR && part->role != BOSS_PART_ROLE_TURRET) {
        attachment->flags |= BOSS_ENEMY_FLAG_INVULNERABLE;
    }
    attachment->hp_max = hp_max;
    attachment->emitter_count = clampi(part->emitter_count, 0, BOSS_PART_EMITTERS_MAX);
    attachment->local_x = part->local_x;
    attachment->local_y = part->local_y;
    attachment->local_rot = part->local_rot;
    for (int i = 0; i < attachment->emitter_count; ++i) {
        attachment->emitter_local_x[i] = part->emitter_local_x[i];
        attachment->emitter_local_y[i] = part->emitter_local_y[i];
        attachment->emitter_cooldown_s[i] = 0.0f;
    }
    if (g->boss_controllers[owner_index].active) {
        g->boss_controllers[owner_index].live_part_count += 1;
    }
}

int boss_update_enemy(game_state* g, int enemy_index, float dt) {
    enemy* e;
    boss_controller_runtime* ctrl;
    float desired_x;
    float desired_y;
    float gain;
    float prev_x;
    float prev_y;
    float min_visible_y;
    float max_visible_y;
    float player_dx;
    float player_dy;
    float pressure_x;
    float pressure_y;
    float pressure_mul_x;
    float pressure_mul_y;
    float lunge_x;
    float lunge_y;
    float lunge_mag;
    float y_radius;
    float move_gain;
    float move_dx;
    float move_dy;
    float move_len;
    float move_strength;
    if (!g || enemy_index < 0 || enemy_index >= MAX_ENEMIES) {
        return 0;
    }
    e = &g->enemies[enemy_index];
    if (!e->active || !g->boss_attachments[enemy_index].active) {
        return 0;
    }
    ctrl = &g->boss_controllers[enemy_index];
    if (!ctrl->active) {
        boss_snap_part_to_owner(g, enemy_index, dt);
        return 1;
    }

    prev_x = e->b.x;
    prev_y = e->b.y;
    ctrl->sway_phase_s += dt * fmaxf(ctrl->sway_speed, 0.01f);
    player_dx = g->player.b.x - ctrl->anchor_x;
    player_dy = g->player.b.y - ctrl->anchor_y;
    pressure_mul_x = (ctrl->phase <= 0) ? 0.10f : ((ctrl->phase == 1) ? 0.16f : 0.22f);
    pressure_mul_y = (ctrl->phase <= 0) ? 0.12f : ((ctrl->phase == 1) ? 0.18f : 0.24f);
    pressure_x = clampf(player_dx, -ctrl->bounds_radius * 0.30f, ctrl->bounds_radius * 0.30f) * pressure_mul_x;
    pressure_y = clampf(player_dy, -ctrl->bounds_radius * 0.22f, ctrl->bounds_radius * 0.22f) * pressure_mul_y;
    desired_x = ctrl->anchor_x;
    desired_x += cosf(ctrl->sway_phase_s * 0.61f + hash01_u32(ctrl->seed ^ 0x1389u) * 6.2831853f) * ctrl->sway_amp_x;
    desired_x += pressure_x;
    desired_y = ctrl->anchor_y;
    desired_y += sinf(ctrl->sway_phase_s + hash01_u32(ctrl->seed ^ 0x51d2u) * 6.2831853f) * ctrl->sway_amp_y;
    desired_y += cosf(ctrl->sway_phase_s * 0.57f + hash01_u32(ctrl->seed ^ 0x7b41u) * 6.2831853f) * ctrl->sway_amp_y * 0.34f;
    desired_y += pressure_y;
    if (ctrl->telegraph_active || ctrl->burst_shots_left > 0) {
        lunge_x = g->player.b.x - e->b.x;
        lunge_y = g->player.b.y - e->b.y;
        normalize2(&lunge_x, &lunge_y);
        lunge_mag = ctrl->bounds_radius * ((ctrl->phase == 2) ? 0.12f : ((ctrl->phase == 1) ? 0.09f : 0.06f));
        desired_x += lunge_x * lunge_mag;
        desired_y += lunge_y * lunge_mag * 0.42f;
    }
    if (ctrl->movement_mode != BOSS_MOVEMENT_CRUISE && ctrl->movement_timer_s > 0.0f) {
        move_dx = ctrl->movement_target_x - e->b.x;
        move_dy = ctrl->movement_target_y - e->b.y;
        move_len = sqrtf(move_dx * move_dx + move_dy * move_dy);
        if (move_len > 1.0e-4f) {
            move_dx /= move_len;
            move_dy /= move_len;
            move_strength = ctrl->bounds_radius * ((ctrl->movement_mode == BOSS_MOVEMENT_REVERSE) ? 0.72f : 0.56f);
            desired_x += move_dx * move_strength;
            desired_y += move_dy * move_strength * 0.72f;
        }
    }
    y_radius = fminf(ctrl->bounds_radius * 0.42f, g->world_h * 0.34f);
    min_visible_y = y_radius + 20.0f * boss_ui_scale(g);
    max_visible_y = g->world_h - y_radius - 20.0f * boss_ui_scale(g);
    if (min_visible_y > max_visible_y) {
        min_visible_y = max_visible_y = g->world_h * 0.5f;
    }
    desired_y = clampf(desired_y, min_visible_y, max_visible_y);
    move_gain = 2.4f;
    if (ctrl->movement_mode == BOSS_MOVEMENT_REVERSE) {
        move_gain = 5.8f;
    } else if (ctrl->movement_mode == BOSS_MOVEMENT_CHARGE) {
        move_gain = 4.9f;
    } else if (ctrl->telegraph_active || ctrl->burst_shots_left > 0) {
        move_gain = 3.2f;
    }
    gain = (dt > 1.0e-5f) ? clampf(dt * move_gain, 0.0f, 1.0f) : 1.0f;
    e->b.x = lerpf(e->b.x, desired_x, gain);
    e->b.y = lerpf(e->b.y, desired_y, gain);
    e->b.vx = (dt > 1.0e-5f) ? (e->b.x - prev_x) / dt : 0.0f;
    e->b.vy = (dt > 1.0e-5f) ? (e->b.y - prev_y) / dt : 0.0f;
    e->b.ax = 0.0f;
    e->b.ay = 0.0f;
    e->facing_x = (ctrl->movement_mode == BOSS_MOVEMENT_REVERSE) ? 1.0f : -1.0f;
    e->facing_y = 0.10f * sinf(ctrl->sway_phase_s * 0.75f);
    e->ai_timer_s += dt;
    boss_tick_attachment_state(g, enemy_index, dt);
    if (ctrl->destruction_active) {
        boss_update_destruction(g, enemy_index, dt);
    } else {
        boss_reconcile_phase(g, enemy_index);
        boss_update_attacks(g, enemy_index, dt);
    }
    for (int i = 0; i < MAX_ENEMIES; ++i) {
        if (g->boss_attachments[i].active && g->boss_attachments[i].owner_index == enemy_index && g->enemies[i].active) {
            boss_snap_part_to_owner(g, i, dt);
        }
    }
    return 1;
}

int boss_any_controller_alive(const game_state* g) {
    if (!g) {
        return 0;
    }
    for (int i = 0; i < MAX_ENEMIES; ++i) {
        if (g->boss_controllers[i].active && g->enemies[i].active) {
            return 1;
        }
    }
    return 0;
}

int boss_enemy_is_managed(const game_state* g, int enemy_index) {
    if (!g || enemy_index < 0 || enemy_index >= MAX_ENEMIES) {
        return 0;
    }
    return g->boss_attachments[enemy_index].active != 0;
}

int boss_enemy_is_damageable(const game_state* g, int enemy_index) {
    if (!boss_enemy_is_managed(g, enemy_index)) {
        return 1;
    }
    return (g->boss_attachments[enemy_index].flags & BOSS_ENEMY_FLAG_INVULNERABLE) == 0;
}

int boss_enemy_damage_per_hit(const game_state* g, int enemy_index) {
    if (!boss_enemy_is_managed(g, enemy_index)) {
        return 1;
    }
    return (g->boss_attachments[enemy_index].flags & BOSS_ENEMY_FLAG_WEAKPOINT) ? 2 : 1;
}

int boss_enemy_has_contact_damage(const game_state* g, int enemy_index) {
    if (!boss_enemy_is_managed(g, enemy_index)) {
        return 1;
    }
    return (g->boss_attachments[enemy_index].flags & BOSS_ENEMY_FLAG_NO_CONTACT_DAMAGE) == 0;
}

void boss_on_enemy_damaged(game_state* g, int enemy_index, int damage_applied, int destroyed) {
    boss_enemy_attachment* attachment;
    enemy* hit;
    const float su = boss_ui_scale(g);
    if (!g || enemy_index < 0 || enemy_index >= MAX_ENEMIES || !g->enemies[enemy_index].active) {
        return;
    }
    if (!boss_enemy_is_managed(g, enemy_index)) {
        return;
    }
    attachment = &g->boss_attachments[enemy_index];
    hit = &g->enemies[enemy_index];
    boss_mark_whole_boss(g, attachment->owner_index >= 0 ? attachment->owner_index : enemy_index, destroyed ? 0.92f : 0.58f, destroyed ? 0.34f : 0.16f);
    g->enemies[enemy_index].boss_telegraph =
        fmaxf(g->enemies[enemy_index].boss_telegraph, destroyed ? 1.0f : 0.88f);
    if (attachment->owner_index >= 0 && attachment->owner_index < MAX_ENEMIES && g->enemies[attachment->owner_index].active) {
        g->enemies[attachment->owner_index].boss_telegraph =
            fmaxf(g->enemies[attachment->owner_index].boss_telegraph, destroyed ? 0.85f : 0.60f);
        for (int i = 0; i < MAX_ENEMIES; ++i) {
            if (!g->boss_attachments[i].active || !g->enemies[i].active) {
                continue;
            }
            if (g->boss_attachments[i].owner_index != attachment->owner_index) {
                continue;
            }
            g->enemies[i].boss_telegraph = fmaxf(g->enemies[i].boss_telegraph, destroyed ? 0.34f : 0.22f);
        }
    }
    boss_enemy_emit_generic_destruction_fx(
        g,
        hit->b.x,
        hit->b.y,
        hit->b.vx,
        hit->b.vy,
        fmaxf(hit->radius * 0.24f, 9.0f * su),
        su,
        destroyed ? 9 : clampi(2 + damage_applied, 2, 5)
    );
    if (!destroyed) {
        const float spark_jitter_x = (hash01_u32((uint32_t)(enemy_index * 0x9e37u + damage_applied * 0x85ebu)) - 0.5f) * hit->radius * 0.36f;
        const float spark_jitter_y = (hash01_u32((uint32_t)(enemy_index * 0x7f4au + damage_applied * 0x51d2u)) - 0.5f) * hit->radius * 0.36f;
        boss_enemy_emit_generic_destruction_fx(
            g,
            hit->b.x + spark_jitter_x,
            hit->b.y + spark_jitter_y,
            hit->b.vx * 0.7f,
            hit->b.vy * 0.7f,
            fmaxf(hit->radius * 0.14f, 6.0f * su),
            su,
            1
        );
    }
}

void boss_on_enemy_destroyed(game_state* g, int enemy_index) {
    int owner_index;
    int role;
    if (!g || enemy_index < 0 || enemy_index >= MAX_ENEMIES) {
        return;
    }

    if (g->boss_controllers[enemy_index].active) {
        if (g->boss_controllers[enemy_index].gates_exit && g->gating_bosses_remaining > 0) {
            g->gating_bosses_remaining -= 1;
        }
        for (int i = 0; i < MAX_ENEMIES; ++i) {
            if (!g->boss_attachments[i].active || g->boss_attachments[i].owner_index != enemy_index) {
                continue;
            }
            g->enemies[i].active = 0;
            boss_reset_enemy_runtime(g, i);
        }
        boss_reset_enemy_runtime(g, enemy_index);
        return;
    }

    owner_index = g->boss_attachments[enemy_index].owner_index;
    role = g->boss_attachments[enemy_index].role;
    boss_reset_enemy_runtime(g, enemy_index);
    if (owner_index < 0 || owner_index >= MAX_ENEMIES || !g->boss_controllers[owner_index].active) {
        return;
    }
    if (g->boss_controllers[owner_index].live_part_count > 0) {
        g->boss_controllers[owner_index].live_part_count -= 1;
    }
    if (role == BOSS_PART_ROLE_CORE && g->enemies[owner_index].active) {
        boss_begin_destruction(g, owner_index);
        return;
    }
    boss_reconcile_phase(g, owner_index);
    if (g->boss_controllers[owner_index].live_part_count <= 0 && g->enemies[owner_index].active) {
        g->enemies[owner_index].active = 0;
        boss_reset_enemy_runtime(g, owner_index);
    }
}
