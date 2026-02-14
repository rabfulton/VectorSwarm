#include "game.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum enemy_archetype {
    ENEMY_ARCH_FORMATION = 0,
    ENEMY_ARCH_SWARM = 1,
    ENEMY_ARCH_KAMIKAZE = 2
};

enum enemy_state {
    ENEMY_STATE_FORMATION = 0,
    ENEMY_STATE_BREAK_ATTACK = 1,
    ENEMY_STATE_SWARM = 2,
    ENEMY_STATE_KAMIKAZE = 3
};

enum enemy_weapon_id {
    ENEMY_WEAPON_PULSE = 0,
    ENEMY_WEAPON_SPREAD = 1,
    ENEMY_WEAPON_BURST = 2,
    ENEMY_WEAPON_COUNT = 3
};

typedef struct enemy_weapon_def {
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
} enemy_weapon_def;

typedef struct enemy_fire_tuning {
    float armed_probability[3];
    float fire_range_min;
    float fire_range_max;
    float aim_error_deg;
    float cooldown_scale;
    float projectile_speed_scale;
    float spread_scale;
} enemy_fire_tuning;

typedef struct enemy_combat_config {
    enemy_weapon_def weapon_defs[ENEMY_WEAPON_COUNT];
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
} enemy_combat_config;

static float frand01(void) {
    return (float)rand() / (float)RAND_MAX;
}

static float frands1(void) {
    return frand01() * 2.0f - 1.0f;
}

static float frand_range(float lo, float hi) {
    return lo + (hi - lo) * frand01();
}

static float clampf(float v, float lo, float hi) {
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

static const enemy_combat_config k_enemy_combat_config = {
    .weapon_defs = {
        [ENEMY_WEAPON_PULSE] = {
            .cooldown_min_s = 1.10f,
            .cooldown_max_s = 1.90f,
            .burst_count = 1,
            .burst_gap_s = 0.0f,
            .projectiles_per_shot = 1,
            .spread_deg = 0.0f,
            .projectile_speed = 500.0f,
            .projectile_ttl_s = 2.35f,
            .projectile_radius = 4.0f,
            .aim_lead_s = 0.20f
        },
        [ENEMY_WEAPON_SPREAD] = {
            .cooldown_min_s = 1.30f,
            .cooldown_max_s = 2.20f,
            .burst_count = 1,
            .burst_gap_s = 0.0f,
            .projectiles_per_shot = 3,
            .spread_deg = 11.0f,
            .projectile_speed = 440.0f,
            .projectile_ttl_s = 2.15f,
            .projectile_radius = 3.6f,
            .aim_lead_s = 0.17f
        },
        [ENEMY_WEAPON_BURST] = {
            .cooldown_min_s = 1.90f,
            .cooldown_max_s = 2.70f,
            .burst_count = 3,
            .burst_gap_s = 0.085f,
            .projectiles_per_shot = 1,
            .spread_deg = 0.0f,
            .projectile_speed = 560.0f,
            .projectile_ttl_s = 2.00f,
            .projectile_radius = 3.4f,
            .aim_lead_s = 0.14f
        }
    },
    .progression_wave_weight = 0.045f,
    .progression_score_weight = (1.0f / 22000.0f),
    .progression_level_weight = 0.0f,
    .armed_probability_base = {0.32f, 0.48f, 0.24f},
    .armed_probability_progression_bonus = {0.40f, 0.35f, 0.45f},
    .fire_range_min = 110.0f,
    .fire_range_max_base = 560.0f,
    .fire_range_max_progression_bonus = 180.0f,
    .aim_error_deg_start = 8.0f,
    .aim_error_deg_end = 2.2f,
    .cooldown_scale_start = 1.0f,
    .cooldown_scale_end = 0.62f,
    .projectile_speed_scale_start = 1.0f,
    .projectile_speed_scale_end = 1.28f,
    .spread_scale_start = 1.0f,
    .spread_scale_end = 0.70f
};

static float dist_sq(float ax, float ay, float bx, float by) {
    const float dx = ax - bx;
    const float dy = ay - by;
    return dx * dx + dy * dy;
}

static float cylinder_period(const game_state* g) {
    return fmaxf(g->world_w * 2.4f, 1.0f);
}

static int level_uses_cylinder(int level_style) {
    return level_style == LEVEL_STYLE_ENEMY_RADAR ||
           level_style == LEVEL_STYLE_EVENT_HORIZON ||
           level_style == LEVEL_STYLE_EVENT_HORIZON_LEGACY;
}

static float gameplay_ui_scale(const game_state* g) {
    const float sx = g->world_w / 1920.0f;
    const float sy = g->world_h / 1080.0f;
    return fmaxf(0.5f, fminf(sx, sy));
}

static float wrap_delta(float a, float b, float period) {
    float d = fmodf(a - b, period);
    if (d > period * 0.5f) {
        d -= period;
    } else if (d < -period * 0.5f) {
        d += period;
    }
    return d;
}

static float dist_sq_level(const game_state* g, float ax, float ay, float bx, float by) {
    if (!g || !level_uses_cylinder(g->level_style)) {
        return dist_sq(ax, ay, bx, by);
    }
    const float dx = wrap_delta(ax, bx, cylinder_period(g));
    const float dy = ay - by;
    return dx * dx + dy * dy;
}

static enemy_fire_tuning enemy_fire_tuning_for(const game_state* g) {
    const enemy_combat_config* c = &k_enemy_combat_config;
    enemy_fire_tuning t = {
        .armed_probability = {
            c->armed_probability_base[0],
            c->armed_probability_base[1],
            c->armed_probability_base[2]
        },
        .fire_range_min = c->fire_range_min,
        .fire_range_max = c->fire_range_max_base,
        .aim_error_deg = c->aim_error_deg_start,
        .cooldown_scale = c->cooldown_scale_start,
        .projectile_speed_scale = c->projectile_speed_scale_start,
        .spread_scale = c->spread_scale_start
    };
    if (!g) {
        return t;
    }
    float progression = (float)g->wave_index * c->progression_wave_weight +
                        (float)g->score * c->progression_score_weight;
    progression += (float)g->level_style * c->progression_level_weight;
    progression = clampf(progression, 0.0f, 1.0f);
    t.armed_probability[0] = clampf(t.armed_probability[0] + progression * c->armed_probability_progression_bonus[0], 0.0f, 1.0f);
    t.armed_probability[1] = clampf(t.armed_probability[1] + progression * c->armed_probability_progression_bonus[1], 0.0f, 1.0f);
    t.armed_probability[2] = clampf(t.armed_probability[2] + progression * c->armed_probability_progression_bonus[2], 0.0f, 1.0f);
    t.fire_range_max += progression * c->fire_range_max_progression_bonus;
    t.aim_error_deg = lerpf(c->aim_error_deg_start, c->aim_error_deg_end, progression);
    t.cooldown_scale = lerpf(c->cooldown_scale_start, c->cooldown_scale_end, progression);
    t.projectile_speed_scale = lerpf(c->projectile_speed_scale_start, c->projectile_speed_scale_end, progression);
    t.spread_scale = lerpf(c->spread_scale_start, c->spread_scale_end, progression);
    return t;
}

static float length2(float x, float y) {
    return sqrtf(x * x + y * y);
}

static void normalize2(float* x, float* y) {
    float l = length2(*x, *y);
    if (l > 1e-5f) {
        *x /= l;
        *y /= l;
    }
}

static void steer_to_velocity(body* b, float target_vx, float target_vy, float accel, float damping) {
    b->ax = (target_vx - b->vx) * accel - b->vx * damping;
    b->ay = (target_vy - b->vy) * accel - b->vy * damping;
}

static void integrate_body(body* b, float dt) {
    b->vx += b->ax * dt;
    b->vy += b->ay * dt;
    b->x += b->vx * dt;
    b->y += b->vy * dt;
}

static particle* alloc_particle(game_state* g) {
    for (size_t i = 0; i < MAX_PARTICLES; ++i) {
        if (!g->particles[i].active) {
            particle* p = &g->particles[i];
            memset(p, 0, sizeof(*p));
            p->active = 1;
            g->active_particles += 1;
            return p;
        }
    }
    return NULL;
}

static void kill_particle(game_state* g, particle* p) {
    if (!p || !p->active) {
        return;
    }
    p->active = 0;
    if (g->active_particles > 0) {
        g->active_particles -= 1;
    }
}

static void game_push_audio_event(game_state* g, game_audio_event_type type, float x, float y) {
    if (!g || g->audio_event_count < 0 || g->audio_event_count >= MAX_AUDIO_EVENTS) {
        return;
    }
    game_audio_event* e = &g->audio_events[g->audio_event_count++];
    e->type = type;
    e->x = x;
    e->y = y;
}

static void emit_explosion(game_state* g, float x, float y, float bias_vx, float bias_vy, int count) {
    game_push_audio_event(g, GAME_AUDIO_EVENT_EXPLOSION, x, y);
    const float su = gameplay_ui_scale(g);
    {
        particle* f = alloc_particle(g);
        if (f) {
            f->type = PARTICLE_FLASH;
            f->b.x = x;
            f->b.y = y;
            f->b.vx = 0.0f;
            f->b.vy = 0.0f;
            f->b.ax = 0.0f;
            f->b.ay = 0.0f;
            f->age_s = 0.0f;
            f->life_s = 0.20f + frand01() * 0.08f;
            f->size = (10.0f + frand01() * 7.0f) * su;
            f->spin = 0.0f;
            f->spin_rate = 0.0f;
            f->r = 1.0f;
            f->g = 0.96f;
            f->bcol = 0.72f;
            f->a = 1.0f;
        }
    }
    for (int i = 0; i < count; ++i) {
        particle* p = alloc_particle(g);
        if (!p) {
            return;
        }
        const float a = frand01() * 6.2831853f;
        const float spd = (70.0f + frand01() * 300.0f) * su;
        p->type = (frand01() < 0.65f) ? PARTICLE_POINT : PARTICLE_GEOM;
        p->b.x = x + frands1() * 6.0f * su;
        p->b.y = y + frands1() * 6.0f * su;
        p->b.vx = cosf(a) * spd + bias_vx * 0.4f;
        p->b.vy = sinf(a) * spd + bias_vy * 0.4f;
        /* Keep explosion motion expanding outward for full lifetime. */
        p->b.ax = 0.0f;
        p->b.ay = 0.0f;
        p->age_s = 0.0f;
        p->life_s = 0.55f + frand01() * 0.85f;
        p->size = (2.7f + frand01() * 6.2f) * su;
        p->spin = frand01() * 6.2831853f;
        p->spin_rate = frands1() * 9.0f;
        p->r = 0.95f + frand01() * 0.05f;
        p->g = 0.55f + frand01() * 0.45f;
        p->bcol = 0.25f + frand01() * 0.40f;
        p->a = 1.0f;
    }
}

static void apply_player_hit(game_state* g, float impact_x, float impact_y, float impact_vx, float impact_vy) {
    if (!g || g->lives <= 0) {
        return;
    }
    emit_explosion(g, impact_x, impact_y, impact_vx, impact_vy, 48);
    g->lives -= 1;
    if (g->lives < 0) {
        g->lives = 0;
    }
}

static void emit_thruster(game_state* g, float dt) {
    if (g->lives <= 0) {
        g->thruster_emit_accum = 0.0f;
        return;
    }
    const float dir = (g->player.facing_x < 0.0f) ? -1.0f : 1.0f;
    const float su = gameplay_ui_scale(g);
    const float speed = length2(g->player.b.vx, g->player.b.vy);
    const float emit_rate = 55.0f + clampf(speed / fmaxf(g->player.max_speed, 1.0f), 0.0f, 1.0f) * 45.0f;
    g->thruster_emit_accum += emit_rate * dt;
    int emit_count = (int)g->thruster_emit_accum;
    if (emit_count > 8) {
        emit_count = 8;
    }
    g->thruster_emit_accum -= (float)emit_count;
    for (int i = 0; i < emit_count; ++i) {
        particle* p = alloc_particle(g);
        if (!p) {
            return;
        }
        p->type = (frand01() < 0.75f) ? PARTICLE_POINT : PARTICLE_GEOM;
        p->b.x = g->player.b.x - dir * (40.0f + frand01() * 4.0f) * su;
        p->b.y = g->player.b.y + frands1() * 4.5f * su;
        p->b.vx = -dir * (220.0f + frand01() * 220.0f) * su + g->player.b.vx * 0.25f;
        p->b.vy = frands1() * 30.0f * su + g->player.b.vy * 0.15f;
        p->b.ax = -p->b.vx * 1.9f;
        p->b.ay = -p->b.vy * 1.6f;
        p->age_s = 0.0f;
        p->life_s = 0.10f + frand01() * 0.15f;
        p->size = (2.1f + frand01() * 3.6f) * su;
        p->spin = frand01() * 6.2831853f;
        p->spin_rate = frands1() * 15.0f;
        p->r = 0.35f;
        p->g = 1.0f;
        p->bcol = 0.75f;
        p->a = 0.95f;
    }
}

static void spawn_bullet_single(game_state* g, float y_offset, float muzzle_speed) {
    const float dir = (g->player.facing_x < 0.0f) ? -1.0f : 1.0f;
    const float su = gameplay_ui_scale(g);
    const float vertical_inherit = 0.18f;
    for (size_t i = 0; i < MAX_BULLETS; ++i) {
        if (g->bullets[i].active) {
            continue;
        }
        bullet* b = &g->bullets[i];
        b->active = 1;
        b->b.x = g->player.b.x + dir * 36.0f * su;
        b->b.y = g->player.b.y + y_offset;
        b->spawn_x = b->b.x;
        b->b.vx = dir * muzzle_speed + g->player.b.vx;
        b->b.vy = g->player.b.vy * vertical_inherit;
        b->b.ax = 0.0f;
        b->b.ay = 0.0f;
        b->ttl_s = 2.0f;
        return;
    }
}

static void spawn_bullet(game_state* g) {
    const float su = gameplay_ui_scale(g);
    g->fire_sfx_pending += 1;
    if (g->weapon_level <= 1) {
        spawn_bullet_single(g, 0.0f, 760.0f * su);
        return;
    }
    if (g->weapon_level == 2) {
        spawn_bullet_single(g, -12.0f * su, 800.0f * su);
        spawn_bullet_single(g, 12.0f * su, 800.0f * su);
        return;
    }
    spawn_bullet_single(g, 0.0f, 860.0f * su);
    spawn_bullet_single(g, -15.0f * su, 860.0f * su);
    spawn_bullet_single(g, 15.0f * su, 860.0f * su);
}

static enemy_bullet* spawn_enemy_bullet(
    game_state* g,
    const enemy* e,
    float dir_x,
    float dir_y,
    float speed,
    float ttl_s,
    float radius
) {
    for (size_t i = 0; i < MAX_ENEMY_BULLETS; ++i) {
        if (g->enemy_bullets[i].active) {
            continue;
        }
        enemy_bullet* b = &g->enemy_bullets[i];
        b->active = 1;
        b->ttl_s = ttl_s;
        b->radius = radius;
        b->b.x = e->b.x + dir_x * (e->radius + 8.0f);
        b->b.y = e->b.y + dir_y * (e->radius + 8.0f);
        b->b.vx = dir_x * speed + e->b.vx * 0.22f;
        b->b.vy = dir_y * speed + e->b.vy * 0.22f;
        b->b.ax = 0.0f;
        b->b.ay = 0.0f;
        return b;
    }
    return NULL;
}

static void enemy_reset_fire_cooldown(game_state* g, enemy* e, const enemy_weapon_def* w, const enemy_fire_tuning* t) {
    const float scale = t ? t->cooldown_scale : 1.0f;
    e->fire_cooldown_s = frand_range(w->cooldown_min_s, w->cooldown_max_s) * scale * frand_range(0.92f, 1.08f);
    if (e->fire_cooldown_s < 0.04f) {
        e->fire_cooldown_s = 0.04f;
    }
}

static void enemy_assign_combat_loadout(game_state* g, enemy* e) {
    if (!g || !e) {
        return;
    }
    const enemy_fire_tuning t = enemy_fire_tuning_for(g);
    int arch = e->archetype;
    if (arch < 0 || arch > 2) {
        arch = 0;
    }
    e->armed = (frand01() < t.armed_probability[arch]) ? 1 : 0;
    if (e->archetype == ENEMY_ARCH_SWARM) {
        e->weapon_id = ENEMY_WEAPON_SPREAD;
    } else if (e->archetype == ENEMY_ARCH_KAMIKAZE) {
        e->weapon_id = ENEMY_WEAPON_BURST;
    } else {
        e->weapon_id = ENEMY_WEAPON_PULSE;
    }
    e->burst_shots_left = 0;
    e->burst_gap_timer_s = 0.0f;
    enemy_reset_fire_cooldown(g, e, &k_enemy_combat_config.weapon_defs[e->weapon_id], &t);
}

static void enemy_fire_projectiles(game_state* g, const enemy* e, const enemy_weapon_def* w, const enemy_fire_tuning* t) {
    const float aim_lead = w->aim_lead_s;
    const float tx = g->player.b.x + g->player.b.vx * aim_lead;
    const float ty = g->player.b.y + g->player.b.vy * aim_lead;
    float dx = level_uses_cylinder(g->level_style) ? wrap_delta(tx, e->b.x, cylinder_period(g)) : (tx - e->b.x);
    float dy = ty - e->b.y;
    normalize2(&dx, &dy);
    const float err_deg = t->aim_error_deg;
    const float err_rad = frands1() * err_deg * (3.14159265359f / 180.0f);
    const float c0 = cosf(err_rad);
    const float s0 = sinf(err_rad);
    const float base_x = dx * c0 - dy * s0;
    const float base_y = dx * s0 + dy * c0;
    const int count = (w->projectiles_per_shot < 1) ? 1 : w->projectiles_per_shot;
    const float spread_rad = (w->spread_deg * t->spread_scale) * (3.14159265359f / 180.0f);
    int spawned = 0;
    for (int i = 0; i < count; ++i) {
        float offset = 0.0f;
        if (count > 1) {
            const float u = (float)i / (float)(count - 1);
            offset = (u - 0.5f) * spread_rad;
        }
        const float c = cosf(offset);
        const float s = sinf(offset);
        float dir_x = base_x * c - base_y * s;
        float dir_y = base_x * s + base_y * c;
        normalize2(&dir_x, &dir_y);
        const float speed = w->projectile_speed * t->projectile_speed_scale;
        if (spawn_enemy_bullet(g, e, dir_x, dir_y, speed, w->projectile_ttl_s, w->projectile_radius)) {
            spawned = 1;
        }
    }
    if (spawned) {
        game_push_audio_event(g, GAME_AUDIO_EVENT_ENEMY_FIRE, e->b.x, e->b.y);
    }
}

static void enemy_try_fire(game_state* g, enemy* e, float dt) {
    if (!g || !e || !e->active || !e->armed || g->lives <= 0) {
        return;
    }
    if (e->weapon_id < 0 || e->weapon_id >= ENEMY_WEAPON_COUNT) {
        e->weapon_id = ENEMY_WEAPON_PULSE;
    }
    const enemy_weapon_def* w = &k_enemy_combat_config.weapon_defs[e->weapon_id];
    const enemy_fire_tuning t = enemy_fire_tuning_for(g);

    if (e->burst_gap_timer_s > 0.0f) {
        e->burst_gap_timer_s -= dt;
    }
    if (e->fire_cooldown_s > 0.0f) {
        e->fire_cooldown_s -= dt;
    }

    if (e->burst_shots_left > 0 && e->burst_gap_timer_s <= 0.0f) {
        enemy_fire_projectiles(g, e, w, &t);
        e->burst_shots_left -= 1;
        if (e->burst_shots_left > 0) {
            e->burst_gap_timer_s = w->burst_gap_s;
        }
        return;
    }
    if (e->fire_cooldown_s > 0.0f) {
        return;
    }

    const float dx = level_uses_cylinder(g->level_style) ? wrap_delta(g->player.b.x, e->b.x, cylinder_period(g)) : (g->player.b.x - e->b.x);
    const float dy = g->player.b.y - e->b.y;
    const float d2 = dx * dx + dy * dy;
    const float rmin = t.fire_range_min;
    const float rmax = t.fire_range_max;
    if (d2 < rmin * rmin || d2 > rmax * rmax) {
        enemy_reset_fire_cooldown(g, e, w, &t);
        return;
    }

    enemy_fire_projectiles(g, e, w, &t);
    e->burst_shots_left = w->burst_count - 1;
    e->burst_gap_timer_s = (e->burst_shots_left > 0) ? w->burst_gap_s : 0.0f;
    enemy_reset_fire_cooldown(g, e, w, &t);
}

static enemy* spawn_enemy_common(game_state* g) {
    const float su = gameplay_ui_scale(g);
    for (size_t i = 0; i < MAX_ENEMIES; ++i) {
        if (g->enemies[i].active) {
            continue;
        }
        enemy* e = &g->enemies[i];
        memset(e, 0, sizeof(*e));
        e->active = 1;
        e->radius = (12.0f + frand01() * 8.0f) * su;
        e->max_speed = 270.0f * su;
        e->accel = 6.0f;
        return e;
    }
    return NULL;
}

static void announce_wave(game_state* g, const char* wave_name) {
    g->wave_announce_pending = 1;
    snprintf(g->wave_announce_text, sizeof(g->wave_announce_text), "inbound enemy wave %02d\n%s", g->wave_index + 1, wave_name);
}

static void spawn_wave_sine_snake(game_state* g, int wave_id) {
    const float su = gameplay_ui_scale(g);
    const int count = 10;
    for (int i = 0; i < count; ++i) {
        enemy* e = spawn_enemy_common(g);
        if (!e) {
            break;
        }
        e->archetype = ENEMY_ARCH_FORMATION;
        e->state = ENEMY_STATE_FORMATION;
        enemy_assign_combat_loadout(g, e);
        e->wave_id = wave_id;
        e->slot_index = i;
        e->b.x = g->camera_x + g->world_w * 0.70f + (float)i * 44.0f * su;
        e->home_y = g->world_h * 0.52f;
        e->b.y = e->home_y;
        e->form_phase = (float)i * 0.55f;
        e->form_amp = 92.0f * su;
        e->form_freq = 1.8f;
        e->break_delay_s = 1.1f + 0.16f * (float)i;
        e->max_speed = 285.0f * su;
        e->accel = 6.8f;
    }
}

static void spawn_wave_v_formation(game_state* g, int wave_id) {
    const float su = gameplay_ui_scale(g);
    const int count = 11;
    const int mid = count / 2;
    for (int i = 0; i < count; ++i) {
        enemy* e = spawn_enemy_common(g);
        if (!e) {
            break;
        }
        const int off = i - mid;
        e->archetype = ENEMY_ARCH_FORMATION;
        e->state = ENEMY_STATE_FORMATION;
        enemy_assign_combat_loadout(g, e);
        e->wave_id = wave_id;
        e->slot_index = i;
        e->b.x = g->camera_x + g->world_w * 0.74f + (float)(abs(off)) * 32.0f * su;
        e->home_y = g->world_h * 0.55f + (float)off * 18.0f * su;
        e->b.y = e->home_y;
        e->form_phase = (float)i * 0.35f;
        e->form_amp = 10.0f * su;
        e->form_freq = 1.2f;
        e->break_delay_s = 0.9f + frand01() * 1.8f;
        e->max_speed = 295.0f * su;
        e->accel = 7.5f;
    }
}

static void spawn_wave_swarm(game_state* g, int wave_id) {
    const float su = gameplay_ui_scale(g);
    const int count = 15;
    for (int i = 0; i < count; ++i) {
        enemy* e = spawn_enemy_common(g);
        if (!e) {
            break;
        }
        e->archetype = ENEMY_ARCH_SWARM;
        e->state = ENEMY_STATE_SWARM;
        enemy_assign_combat_loadout(g, e);
        e->wave_id = wave_id;
        e->slot_index = i;
        e->b.x = g->camera_x + g->world_w * 0.62f + frand01() * 260.0f * su;
        e->b.y = g->world_h * 0.50f + frands1() * 140.0f * su;
        e->home_y = g->world_h * 0.50f;
        e->max_speed = 255.0f * su;
        e->accel = 7.8f;
        e->radius = (10.0f + frand01() * 6.0f) * su;
    }
}

static void spawn_wave_kamikaze(game_state* g, int wave_id) {
    const float su = gameplay_ui_scale(g);
    const int count = 9;
    for (int i = 0; i < count; ++i) {
        enemy* e = spawn_enemy_common(g);
        if (!e) {
            break;
        }
        e->archetype = ENEMY_ARCH_KAMIKAZE;
        e->state = ENEMY_STATE_KAMIKAZE;
        enemy_assign_combat_loadout(g, e);
        e->wave_id = wave_id;
        e->slot_index = i;
        e->b.x = g->camera_x + g->world_w * 0.65f + (float)i * 34.0f * su;
        e->b.y = 64.0f * su + frand01() * (g->world_h - 128.0f * su);
        e->max_speed = 360.0f * su;
        e->accel = 9.0f;
        e->radius = (11.0f + frand01() * 6.0f) * su;
    }
}

static void spawn_next_wave(game_state* g) {
    const int wave_id = ++g->wave_id_alloc;
    const int pattern = g->wave_index % 4;
    if (pattern == 0) {
        announce_wave(g, "sine snake formation");
        spawn_wave_sine_snake(g, wave_id);
    } else if (pattern == 1) {
        announce_wave(g, "galaxian break v formation");
        spawn_wave_v_formation(g, wave_id);
    } else if (pattern == 2) {
        announce_wave(g, "boid swarm cluster");
        spawn_wave_swarm(g, wave_id);
    } else {
        announce_wave(g, "kamikaze crash wing");
        spawn_wave_kamikaze(g, wave_id);
    }
    g->wave_index += 1;
    g->wave_cooldown_s = 2.0f;
}

static void update_enemy_formation(game_state* g, enemy* e, float dt) {
    e->ai_timer_s += dt;
    if (e->state == ENEMY_STATE_FORMATION) {
        const float su = gameplay_ui_scale(g);
        const float desired_y = e->home_y + sinf(g->t * e->form_freq + e->form_phase) * e->form_amp;
        const float target_vx = -165.0f * su;
        const float target_vy = (desired_y - e->b.y) * 2.4f;
        steer_to_velocity(&e->b, target_vx, target_vy, e->accel, 1.2f);

        if (e->ai_timer_s > e->break_delay_s) {
            /* Keep break-attack transition rate stable across frame rates. */
            const float legacy_p_per_frame = 0.014f; /* tuned at ~60 fps */
            const float lambda = -logf(1.0f - legacy_p_per_frame) * 60.0f;
            const float p_dt = 1.0f - expf(-lambda * fmaxf(dt, 0.0f));
            if (frand01() < p_dt) {
                e->state = ENEMY_STATE_BREAK_ATTACK;
                e->ai_timer_s = 0.0f;
                e->break_delay_s = 1.0f + frand01() * 2.0f;
            }
        }
    } else {
        const float lead = 0.45f;
        const float tx = g->player.b.x + g->player.b.vx * lead;
        const float ty = g->player.b.y + g->player.b.vy * lead;
        float dir_x = level_uses_cylinder(g->level_style) ? wrap_delta(tx, e->b.x, cylinder_period(g)) : (tx - e->b.x);
        float dir_y = ty - e->b.y;
        normalize2(&dir_x, &dir_y);
        steer_to_velocity(&e->b, dir_x * (e->max_speed * 1.18f), dir_y * (e->max_speed * 1.18f), e->accel * 1.25f, 1.0f);

        if (e->ai_timer_s > 1.6f) {
            e->state = ENEMY_STATE_FORMATION;
            e->ai_timer_s = 0.0f;
        }
    }
}

static void update_enemy_kamikaze(game_state* g, enemy* e, float dt) {
    (void)dt;
    const float lead = 0.25f;
    const float tx = g->player.b.x + g->player.b.vx * lead;
    const float ty = g->player.b.y + g->player.b.vy * lead;
    float dir_x = level_uses_cylinder(g->level_style) ? wrap_delta(tx, e->b.x, cylinder_period(g)) : (tx - e->b.x);
    float dir_y = ty - e->b.y;
    normalize2(&dir_x, &dir_y);
    steer_to_velocity(&e->b, dir_x * e->max_speed, dir_y * e->max_speed, e->accel * 1.35f, 0.8f);
}

static void update_enemy_swarm(game_state* g, enemy* e, float dt) {
    (void)dt;
    const float su = gameplay_ui_scale(g);
    float sep_x = 0.0f;
    float sep_y = 0.0f;
    float ali_x = 0.0f;
    float ali_y = 0.0f;
    float coh_x = 0.0f;
    float coh_y = 0.0f;
    int ali_n = 0;
    int coh_n = 0;

    for (size_t i = 0; i < MAX_ENEMIES; ++i) {
        const enemy* o = &g->enemies[i];
        if (!o->active || o == e || o->archetype != ENEMY_ARCH_SWARM) {
            continue;
        }
        const float dx = level_uses_cylinder(g->level_style) ? wrap_delta(o->b.x, e->b.x, cylinder_period(g)) : (o->b.x - e->b.x);
        const float dy = o->b.y - e->b.y;
        const float d2 = dx * dx + dy * dy;
        if (d2 < 1e-4f) {
            continue;
        }
        if (d2 < (70.0f * su) * (70.0f * su)) {
            sep_x -= dx / d2;
            sep_y -= dy / d2;
        }
        if (d2 < (180.0f * su) * (180.0f * su)) {
            ali_x += o->b.vx;
            ali_y += o->b.vy;
            ali_n += 1;
        }
        if (d2 < (220.0f * su) * (220.0f * su)) {
            coh_x += o->b.x;
            coh_y += o->b.y;
            coh_n += 1;
        }
    }

    if (ali_n > 0) {
        ali_x = ali_x / (float)ali_n - e->b.vx;
        ali_y = ali_y / (float)ali_n - e->b.vy;
    }
    if (coh_n > 0) {
        coh_x = coh_x / (float)coh_n - e->b.x;
        coh_y = coh_y / (float)coh_n - e->b.y;
    }

    float avoid_x = 0.0f;
    float avoid_y = 0.0f;
    {
        float dx = level_uses_cylinder(g->level_style) ? wrap_delta(e->b.x, g->player.b.x, cylinder_period(g)) : (e->b.x - g->player.b.x);
        float dy = e->b.y - g->player.b.y;
        float d2 = dx * dx + dy * dy;
        if (d2 < (185.0f * su) * (185.0f * su)) {
            if (d2 < 1e-4f) {
                d2 = 1e-4f;
            }
            avoid_x += dx / d2;
            avoid_y += dy / d2;
        }
    }

    float goal_x = (g->player.b.x + 280.0f * su) - e->b.x;
    if (level_uses_cylinder(g->level_style)) {
        goal_x = wrap_delta(g->player.b.x + 280.0f * su, e->b.x, cylinder_period(g));
    }
    float goal_y = (g->player.b.y + sinf(g->t * 0.7f + (float)e->slot_index * 0.35f) * 80.0f * su) - e->b.y;

    normalize2(&sep_x, &sep_y);
    normalize2(&ali_x, &ali_y);
    normalize2(&coh_x, &coh_y);
    normalize2(&avoid_x, &avoid_y);
    normalize2(&goal_x, &goal_y);

    const float fx =
        sep_x * 1.85f +
        ali_x * 0.60f +
        coh_x * 0.55f +
        avoid_x * 2.30f +
        goal_x * 0.95f;
    const float fy =
        sep_y * 1.85f +
        ali_y * 0.60f +
        coh_y * 0.55f +
        avoid_y * 2.30f +
        goal_y * 0.95f;

    e->b.ax = fx * (e->accel * 135.0f) - e->b.vx * 1.3f;
    e->b.ay = fy * (e->accel * 135.0f) - e->b.vy * 1.3f;
}

void game_init(game_state* g, float world_w, float world_h) {
    memset(g, 0, sizeof(*g));
    g->world_w = world_w;
    g->world_h = world_h;
    g->lives = 3;
    g->weapon_level = 1;
    const float su = gameplay_ui_scale(g);
    g->player.b.x = 170.0f * su;
    g->player.b.y = world_h * 0.5f;
    g->player.thrust = 3300.0f * su;
    g->player.drag = 4.1f;
    g->player.max_speed = 760.0f * su;
    g->player.facing_x = 1.0f;
    g->camera_x = g->player.b.x;
    g->camera_y = world_h * 0.5f;
    g->level_style = LEVEL_STYLE_DEFENDER;
    g->wave_cooldown_s = 0.65f;
    g->wave_index = 0;
    g->wave_id_alloc = 0;

    for (size_t i = 0; i < MAX_STARS; ++i) {
        g->stars[i].x = frand01() * world_w;
        g->stars[i].y = frand01() * world_h;
        g->stars[i].prev_x = g->stars[i].x;
        g->stars[i].prev_y = g->stars[i].y;
        g->stars[i].speed = 50.0f + frand01() * 190.0f;
        g->stars[i].size = 0.9f + frand01() * 1.5f;
    }
}

void game_cycle_level(game_state* g) {
    if (!g) {
        return;
    }
    g->level_style = (g->level_style + 1) % LEVEL_STYLE_COUNT;
    memset(g->bullets, 0, sizeof(g->bullets));
    memset(g->enemy_bullets, 0, sizeof(g->enemy_bullets));
    memset(g->enemies, 0, sizeof(g->enemies));
    memset(g->particles, 0, sizeof(g->particles));
    g->active_particles = 0;
    g->wave_cooldown_s = 0.6f;
    g->camera_vx = 0.0f;
    g->camera_x = g->player.b.x;
}

void game_update(game_state* g, float dt, const game_input* in) {
    g->t += dt;
    const float su = gameplay_ui_scale(g);

    if (in->restart && g->lives <= 0) {
        const int level_style = g->level_style;
        game_init(g, g->world_w, g->world_h);
        g->level_style = level_style;
    }

    for (size_t i = 0; i < MAX_STARS; ++i) {
        g->stars[i].prev_x = g->stars[i].x;
        g->stars[i].prev_y = g->stars[i].y;
        g->stars[i].x -= g->stars[i].speed * dt;
        if (g->stars[i].x < -6.0f) {
            g->stars[i].x = g->world_w + 6.0f;
            g->stars[i].y = frand01() * g->world_h;
            g->stars[i].prev_x = g->stars[i].x;
            g->stars[i].prev_y = g->stars[i].y;
            g->stars[i].speed = 50.0f + frand01() * 190.0f;
            g->stars[i].size = 0.9f + frand01() * 1.5f;
        }
    }

    if (g->lives > 0) {
        float input_x = 0.0f;
        float input_y = 0.0f;
        if (in->left) {
            input_x -= 1.0f;
        }
        if (in->right) {
            input_x += 1.0f;
        }
        if (in->up) {
            input_y += 1.0f;
        }
        if (in->down) {
            input_y -= 1.0f;
        }

        const float input_len = length2(input_x, input_y);
        if (input_len > 1.0f) {
            input_x /= input_len;
            input_y /= input_len;
        }
        if (input_x < -0.1f) {
            g->player.facing_x = -1.0f;
        } else if (input_x > 0.1f) {
            g->player.facing_x = 1.0f;
        }

        g->player.b.ax = input_x * g->player.thrust - g->player.b.vx * g->player.drag;
        g->player.b.ay = input_y * g->player.thrust - g->player.b.vy * g->player.drag;
        integrate_body(&g->player.b, dt);

        const float speed = length2(g->player.b.vx, g->player.b.vy);
        if (speed > g->player.max_speed) {
            const float s = g->player.max_speed / speed;
            g->player.b.vx *= s;
            g->player.b.vy *= s;
        }

        if (g->player.b.y < 38.0f * su) {
            g->player.b.y = 38.0f * su;
            if (g->player.b.vy < 0.0f) {
                g->player.b.vy = 0.0f;
            }
        }
        if (g->player.b.y > g->world_h - 38.0f * su) {
            g->player.b.y = g->world_h - 38.0f * su;
            if (g->player.b.vy > 0.0f) {
                g->player.b.vy = 0.0f;
            }
        }
    }

    emit_thruster(g, dt);

    if (g->fire_cooldown_s > 0.0f) {
        g->fire_cooldown_s -= dt;
    }
    if (g->score >= 3000) {
        g->weapon_level = 3;
    } else if (g->score >= 1200) {
        g->weapon_level = 2;
    } else {
        g->weapon_level = 1;
    }
    g->weapon_heat = clampf(g->weapon_heat - dt * 0.58f, 0.0f, 1.0f);

    if (g->lives > 0 && in->fire && g->fire_cooldown_s <= 0.0f) {
        spawn_bullet(g);
        g->fire_cooldown_s = 0.095f;
        g->weapon_heat = clampf(g->weapon_heat + 0.09f, 0.0f, 1.0f);
    }

    for (size_t i = 0; i < MAX_BULLETS; ++i) {
        if (!g->bullets[i].active) {
            continue;
        }
        bullet* b = &g->bullets[i];
        integrate_body(&b->b, dt);
        b->ttl_s -= dt;
        if (level_uses_cylinder(g->level_style)) {
            const float period = cylinder_period(g);
            const float travel = fabsf(wrap_delta(b->b.x, b->spawn_x, period));
            if (b->ttl_s <= 0.0f || travel >= period * (1.0f / 3.0f)) {
                b->active = 0;
            }
            continue;
        }
        if (b->ttl_s <= 0.0f || fabsf(b->b.x - g->camera_x) > g->world_w * 1.2f) {
            b->active = 0;
        }
    }

    if (g->lives > 0) {
        g->wave_cooldown_s -= dt;
        if (game_enemy_count(g) <= 0 && g->wave_cooldown_s <= 0.0f) {
            spawn_next_wave(g);
        }
    }
    int player_hit_this_frame = 0;

    for (size_t i = 0; i < MAX_ENEMIES; ++i) {
        if (!g->enemies[i].active) {
            continue;
        }
        enemy* e = &g->enemies[i];
        if (e->archetype == ENEMY_ARCH_SWARM) {
            update_enemy_swarm(g, e, dt);
        } else if (e->archetype == ENEMY_ARCH_KAMIKAZE) {
            update_enemy_kamikaze(g, e, dt);
        } else {
            update_enemy_formation(g, e, dt);
        }

        integrate_body(&e->b, dt);

        const float v = length2(e->b.vx, e->b.vy);
        if (v > e->max_speed) {
            const float s = e->max_speed / v;
            e->b.vx *= s;
            e->b.vy *= s;
        }

        if (!level_uses_cylinder(g->level_style) && e->b.x < g->camera_x - g->world_w * 0.72f) {
            e->active = 0;
            continue;
        }
        if (e->b.y < 26.0f * su) {
            e->b.y = 26.0f * su;
            if (e->b.vy < 0.0f) {
                e->b.vy = 0.0f;
            }
        }
        if (e->b.y > g->world_h - 26.0f * su) {
            e->b.y = g->world_h - 26.0f * su;
            if (e->b.vy > 0.0f) {
                e->b.vy = 0.0f;
            }
        }

        if (g->lives > 0) {
            const float hit_r = e->radius + 14.0f * su;
            if (!player_hit_this_frame &&
                dist_sq_level(g, e->b.x, e->b.y, g->player.b.x, g->player.b.y) <= hit_r * hit_r) {
                e->active = 0;
                apply_player_hit(g, g->player.b.x, g->player.b.y, g->player.b.vx, g->player.b.vy);
                player_hit_this_frame = 1;
            }
        }
        enemy_try_fire(g, e, dt);
    }

    for (size_t i = 0; i < MAX_ENEMY_BULLETS; ++i) {
        enemy_bullet* b = &g->enemy_bullets[i];
        if (!b->active) {
            continue;
        }
        integrate_body(&b->b, dt);
        b->ttl_s -= dt;
        if (b->ttl_s <= 0.0f) {
            b->active = 0;
            continue;
        }
        if (level_uses_cylinder(g->level_style)) {
            const float period = cylinder_period(g);
            if (fabsf(wrap_delta(b->b.x, g->player.b.x, period)) > period * 0.55f) {
                b->active = 0;
                continue;
            }
        } else if (fabsf(b->b.x - g->camera_x) > g->world_w * 1.35f) {
            b->active = 0;
            continue;
        }
        if (g->lives > 0 && !player_hit_this_frame) {
            const float hit_r = b->radius + 12.0f * su;
            if (dist_sq_level(g, b->b.x, b->b.y, g->player.b.x, g->player.b.y) <= hit_r * hit_r) {
                b->active = 0;
                apply_player_hit(g, g->player.b.x, g->player.b.y, b->b.vx, b->b.vy);
                player_hit_this_frame = 1;
            }
        }
    }

    for (size_t bi = 0; bi < MAX_BULLETS; ++bi) {
        if (!g->bullets[bi].active) {
            continue;
        }
        for (size_t ei = 0; ei < MAX_ENEMIES; ++ei) {
            if (!g->enemies[ei].active) {
                continue;
            }
            if (dist_sq_level(g, g->bullets[bi].b.x, g->bullets[bi].b.y, g->enemies[ei].b.x, g->enemies[ei].b.y) <=
                g->enemies[ei].radius * g->enemies[ei].radius) {
                g->bullets[bi].active = 0;
                g->enemies[ei].active = 0;
                emit_explosion(g, g->enemies[ei].b.x, g->enemies[ei].b.y, g->enemies[ei].b.vx, g->enemies[ei].b.vy, 26);
                g->kills += 1;
                g->score += 100;
                break;
            }
        }
    }

    for (size_t i = 0; i < MAX_PARTICLES; ++i) {
        particle* p = &g->particles[i];
        if (!p->active) {
            continue;
        }
        p->age_s += dt;
        if (p->age_s >= p->life_s) {
            kill_particle(g, p);
            continue;
        }
        p->spin += p->spin_rate * dt;
        integrate_body(&p->b, dt);
        {
            const float t01 = p->age_s / p->life_s;
            const float inv = 1.0f - t01;
            if (p->type == PARTICLE_FLASH) {
                p->a = inv * inv * inv;
            } else if (p->life_s > 0.30f) {
                /* Explosion particles should hold brightness longer, then fall off. */
                p->a = powf(inv, 1.35f);
            } else {
                p->a = inv * inv;
            }
        }
    }

    {
        /* Camera follows only on X; cylindrical mode keeps ship tighter to center. */
        float rear_bias = 0.25f;
        float spring_k = 18.0f;
        float damping = 8.2f;
        if (level_uses_cylinder(g->level_style)) {
            rear_bias = 0.08f;
            spring_k = 26.0f;
            damping = 10.2f;
        }
        const float target_x = g->player.b.x + g->player.facing_x * (g->world_w * rear_bias);
        const float cam_ax = (target_x - g->camera_x) * spring_k - g->camera_vx * damping;
        g->camera_vx += cam_ax * dt;
        g->camera_x += g->camera_vx * dt;
        g->camera_vy = 0.0f;
        g->camera_y = g->world_h * 0.5f;
    }
}

int game_enemy_count(const game_state* g) {
    int n = 0;
    for (size_t i = 0; i < MAX_ENEMIES; ++i) {
        if (g->enemies[i].active) {
            ++n;
        }
    }
    return n;
}

float game_player_speed01(const game_state* g) {
    return clampf(length2(g->player.b.vx, g->player.b.vy) / g->player.max_speed, 0.0f, 1.0f);
}

float game_weapon_heat01(const game_state* g) {
    return clampf(g->weapon_heat, 0.0f, 1.0f);
}

float game_threat01(const game_state* g) {
    return clampf((float)game_enemy_count(g) / (float)MAX_ENEMIES, 0.0f, 1.0f);
}

int game_pop_wave_announcement(game_state* g, char* out, size_t out_cap) {
    if (!g || !out || out_cap == 0 || !g->wave_announce_pending) {
        return 0;
    }
    snprintf(out, out_cap, "%s", g->wave_announce_text);
    g->wave_announce_pending = 0;
    g->wave_announce_text[0] = '\0';
    return 1;
}

int game_pop_fire_sfx_count(game_state* g) {
    if (!g) {
        return 0;
    }
    int n = g->fire_sfx_pending;
    g->fire_sfx_pending = 0;
    return n;
}

int game_pop_audio_events(game_state* g, game_audio_event* out, int out_cap) {
    if (!g || !out || out_cap <= 0) {
        return 0;
    }
    int n = g->audio_event_count;
    if (n < 0) {
        n = 0;
    }
    if (n > out_cap) {
        n = out_cap;
    }
    if (n > 0) {
        memcpy(out, g->audio_events, (size_t)n * sizeof(*out));
    }
    g->audio_event_count = 0;
    return n;
}
