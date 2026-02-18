#include "enemy.h"

#include <math.h>
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

enum enemy_formation_kind {
    ENEMY_FORMATION_NONE = 0,
    ENEMY_FORMATION_SINE = 1,
    ENEMY_FORMATION_V = 2
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

static float wrap_delta(float a, float b, float period) {
    float d = fmodf(a - b, period);
    if (d > period * 0.5f) {
        d -= period;
    } else if (d < -period * 0.5f) {
        d += period;
    }
    return d;
}

static float dist_sq(float ax, float ay, float bx, float by) {
    const float dx = ax - bx;
    const float dy = ay - by;
    return dx * dx + dy * dy;
}

static float dist_sq_level(int uses_cylinder, float period, float ax, float ay, float bx, float by) {
    if (!uses_cylinder) {
        return dist_sq(ax, ay, bx, by);
    }
    const float dx = wrap_delta(ax, bx, period);
    const float dy = ay - by;
    return dx * dx + dy * dy;
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

static void emit_explosion(game_state* g, float x, float y, float bias_vx, float bias_vy, int count, float su) {
    game_push_audio_event(g, GAME_AUDIO_EVENT_EXPLOSION, x, y);
    {
        particle* f = alloc_particle(g);
        if (f) {
            f->type = PARTICLE_FLASH;
            f->b.x = x;
            f->b.y = y;
            f->age_s = 0.0f;
            f->life_s = 0.20f + frand01() * 0.08f;
            f->size = (10.0f + frand01() * 7.0f) * su;
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

static void apply_player_hit(game_state* g, float impact_x, float impact_y, float impact_vx, float impact_vy, float su) {
    if (!g || g->lives <= 0) {
        return;
    }
    emit_explosion(g, impact_x, impact_y, impact_vx, impact_vy, 48, su);
    g->lives -= 1;
    if (g->lives < 0) {
        g->lives = 0;
    }
}

static void emit_enemy_debris(game_state* g, const enemy* e, float impact_vx, float impact_vy) {
    static const float nx[4] = {-0.60f, 0.40f, 0.40f, -0.60f};
    static const float ny[4] = {0.00f, -0.80f, 0.00f, 0.80f};
    static const float tx[4] = {0.40f, 0.60f, -0.60f, -0.60f};
    static const float ty[4] = {-0.80f, 0.00f, 0.80f, 0.00f};
    if (!g || !e) {
        return;
    }
    for (int seg = 0; seg < 4; ++seg) {
        for (size_t i = 0; i < MAX_ENEMY_DEBRIS; ++i) {
            enemy_debris* d = &g->debris[i];
            if (d->active) {
                continue;
            }
            d->active = 1;
            d->half_len = e->radius * 0.52f;
            d->angle = atan2f(ty[seg] - ny[seg], tx[seg] - nx[seg]);
            d->spin_rate = frands1() * (6.0f + 6.0f * frand01());
            d->b.x = e->b.x + (nx[seg] + tx[seg]) * 0.5f * e->radius;
            d->b.y = e->b.y + (ny[seg] + ty[seg]) * 0.5f * e->radius;
            d->b.vx = e->b.vx * 0.18f + impact_vx * (0.10f + 0.08f * frand01()) + frands1() * 46.0f;
            d->b.vy = e->b.vy * 0.10f + impact_vy * 0.08f + frands1() * 34.0f + 22.0f;
            d->b.ax = -d->b.vx * 0.16f;
            d->b.ay = -260.0f;
            d->age_s = 0.0f;
            d->life_s = 2.2f + frand01() * 1.0f;
            d->alpha = 1.0f;
            break;
        }
    }
}

static float enemy_progression01(const game_state* g, const leveldef_db* db) {
    const leveldef_combat_tuning* c;
    if (!g || !db) {
        return 0.0f;
    }
    c = &db->combat;
    {
        float progression = (float)g->wave_index * c->progression_wave_weight +
                            (float)g->score * c->progression_score_weight;
        progression += (float)g->level_style * c->progression_level_weight;
        return clampf(progression, 0.0f, 1.0f);
    }
}

static enemy_fire_tuning enemy_fire_tuning_for(const game_state* g, const leveldef_db* db) {
    const leveldef_combat_tuning* c;
    enemy_fire_tuning t;
    c = &db->combat;
    t = (enemy_fire_tuning){
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
    {
        const float progression = enemy_progression01(g, db);
        t.armed_probability[0] = clampf(t.armed_probability[0] + progression * c->armed_probability_progression_bonus[0], 0.0f, 1.0f);
        t.armed_probability[1] = clampf(t.armed_probability[1] + progression * c->armed_probability_progression_bonus[1], 0.0f, 1.0f);
        t.armed_probability[2] = clampf(t.armed_probability[2] + progression * c->armed_probability_progression_bonus[2], 0.0f, 1.0f);
        t.fire_range_max += progression * c->fire_range_max_progression_bonus;
        t.aim_error_deg = lerpf(c->aim_error_deg_start, c->aim_error_deg_end, progression);
        t.cooldown_scale = lerpf(c->cooldown_scale_start, c->cooldown_scale_end, progression);
        t.projectile_speed_scale = lerpf(c->projectile_speed_scale_start, c->projectile_speed_scale_end, progression);
        t.spread_scale = lerpf(c->spread_scale_start, c->spread_scale_end, progression);
    }
    return t;
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

static void enemy_reset_fire_cooldown(const enemy_weapon_def* w, const enemy_fire_tuning* t, enemy* e) {
    const float scale = t ? t->cooldown_scale : 1.0f;
    e->fire_cooldown_s = frand_range(w->cooldown_min_s, w->cooldown_max_s) * scale * frand_range(0.92f, 1.08f);
    if (e->fire_cooldown_s < 0.04f) {
        e->fire_cooldown_s = 0.04f;
    }
}

static void enemy_assign_combat_loadout(game_state* g, enemy* e, const leveldef_db* db) {
    const enemy_fire_tuning t = enemy_fire_tuning_for(g, db);
    const leveldef_combat_tuning* combat = &db->combat;
    int arch = e->archetype;
    if (arch < 0 || arch > 2) {
        arch = 0;
    }
    if (e->archetype == ENEMY_ARCH_SWARM) {
        const float progression = enemy_progression01(g, db);
        const float armed_p = clampf(lerpf(combat->swarm_armed_prob_start, combat->swarm_armed_prob_end, progression), 0.0f, 1.0f);
        const float spread_p = clampf(lerpf(combat->swarm_spread_prob_start, combat->swarm_spread_prob_end, progression), 0.0f, 1.0f);
        e->armed = (frand01() < armed_p) ? 1 : 0;
        e->weapon_id = (frand01() < spread_p) ? ENEMY_WEAPON_SPREAD : ENEMY_WEAPON_PULSE;
    } else if (e->archetype == ENEMY_ARCH_KAMIKAZE) {
        e->armed = (frand01() < t.armed_probability[arch]) ? 1 : 0;
        e->weapon_id = ENEMY_WEAPON_BURST;
    } else {
        e->armed = (frand01() < t.armed_probability[arch]) ? 1 : 0;
        e->weapon_id = ENEMY_WEAPON_PULSE;
    }
    e->burst_shots_left = 0;
    e->burst_gap_timer_s = 0.0f;
    {
        enemy_weapon_def w = {
            .cooldown_min_s = combat->weapon[e->weapon_id].cooldown_min_s,
            .cooldown_max_s = combat->weapon[e->weapon_id].cooldown_max_s,
            .burst_count = combat->weapon[e->weapon_id].burst_count,
            .burst_gap_s = combat->weapon[e->weapon_id].burst_gap_s,
            .projectiles_per_shot = combat->weapon[e->weapon_id].projectiles_per_shot,
            .spread_deg = combat->weapon[e->weapon_id].spread_deg,
            .projectile_speed = combat->weapon[e->weapon_id].projectile_speed,
            .projectile_ttl_s = combat->weapon[e->weapon_id].projectile_ttl_s,
            .projectile_radius = combat->weapon[e->weapon_id].projectile_radius,
            .aim_lead_s = combat->weapon[e->weapon_id].aim_lead_s
        };
        enemy_reset_fire_cooldown(&w, &t, e);
    }
}

static enemy* spawn_enemy_common(game_state* g, float su) {
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
        e->lane_dir = -1.0f;
        return e;
    }
    return NULL;
}

static void enforce_auto_spawn_side(game_state* g, enemy* e, int bidirectional_spawns) {
    if (!g || !e || bidirectional_spawns) {
        return;
    }
    {
        const float min_x = g->camera_x + g->world_w * 0.56f;
        if (e->b.x < min_x) {
            e->b.x = min_x + frand01() * (g->world_w * 0.16f);
        }
    }
}

static float lane_dir_toward_player_x(const game_state* g, float enemy_x, int uses_cylinder, float period) {
    if (!g) {
        return -1.0f;
    }
    {
        const float dx = uses_cylinder ? wrap_delta(g->player.b.x, enemy_x, period) : (g->player.b.x - enemy_x);
        return (dx < 0.0f) ? -1.0f : 1.0f;
    }
}

static void announce_wave(game_state* g, const char* wave_name) {
    g->wave_announce_pending = 1;
    snprintf(g->wave_announce_text, sizeof(g->wave_announce_text), "inbound enemy wave %02d\n%s", g->wave_index + 1, wave_name);
}

static void spawn_wave_sine_snake(game_state* g, const leveldef_db* db, const leveldef_level* lvl, int wave_id, int bidirectional_spawns, float su, int uses_cylinder, float period) {
    const leveldef_wave_sine_tuning* w = &lvl->sine;
    if (w->count <= 0) {
        return;
    }
    const float spawn_side = bidirectional_spawns ? ((frand01() < 0.5f) ? -1.0f : 1.0f) : 1.0f;
    for (int i = 0; i < w->count; ++i) {
        enemy* e = spawn_enemy_common(g, su);
        if (!e) {
            break;
        }
        e->archetype = ENEMY_ARCH_FORMATION;
        e->state = ENEMY_STATE_FORMATION;
        e->formation_kind = ENEMY_FORMATION_SINE;
        enemy_assign_combat_loadout(g, e, db);
        e->wave_id = wave_id;
        e->slot_index = i;
        e->b.x = g->camera_x + spawn_side * (g->world_w * w->start_x01 + (float)i * w->spacing_x * su);
        enforce_auto_spawn_side(g, e, bidirectional_spawns);
        e->home_y = g->world_h * w->home_y01;
        e->b.y = e->home_y;
        e->lane_dir = lane_dir_toward_player_x(g, e->b.x, uses_cylinder, period);
        e->form_phase = (float)i * w->phase_step;
        e->form_amp = w->form_amp * su;
        e->form_freq = w->form_freq;
        e->break_delay_s = w->break_delay_base + w->break_delay_step * (float)i;
        e->max_speed = w->max_speed * su;
        e->accel = w->accel;
    }
}

static void spawn_wave_v_formation(game_state* g, const leveldef_db* db, const leveldef_level* lvl, int wave_id, int bidirectional_spawns, float su, int uses_cylinder, float period) {
    const leveldef_wave_v_tuning* w = &lvl->v;
    if (w->count <= 0) {
        return;
    }
    const float spawn_side = bidirectional_spawns ? ((frand01() < 0.5f) ? -1.0f : 1.0f) : 1.0f;
    const int mid = w->count / 2;
    for (int i = 0; i < w->count; ++i) {
        enemy* e = spawn_enemy_common(g, su);
        const int off = i - mid;
        if (!e) {
            break;
        }
        e->archetype = ENEMY_ARCH_FORMATION;
        e->state = ENEMY_STATE_FORMATION;
        e->formation_kind = ENEMY_FORMATION_V;
        enemy_assign_combat_loadout(g, e, db);
        e->wave_id = wave_id;
        e->slot_index = i;
        e->b.x = g->camera_x + spawn_side * (g->world_w * w->start_x01 + (float)abs(off) * w->spacing_x * su);
        enforce_auto_spawn_side(g, e, bidirectional_spawns);
        e->home_y = g->world_h * w->home_y01 + (float)off * w->home_y_step * su;
        e->b.y = e->home_y;
        e->lane_dir = lane_dir_toward_player_x(g, e->b.x, uses_cylinder, period);
        e->form_phase = (float)i * w->phase_step;
        e->form_amp = w->form_amp * su;
        e->form_freq = w->form_freq;
        e->break_delay_s = w->break_delay_min + frand01() * w->break_delay_rand;
        e->max_speed = w->max_speed * su;
        e->accel = w->accel;
    }
}

static void spawn_wave_swarm_profile(game_state* g, const leveldef_db* db, int wave_id, int profile_id, float goal_dir, int bidirectional_spawns, float su) {
    const leveldef_boid_profile* p = leveldef_get_boid_profile(db, profile_id);
    if (!p) {
        return;
    }
    for (int i = 0; i < p->count; ++i) {
        enemy* e = spawn_enemy_common(g, su);
        if (!e) {
            break;
        }
        e->archetype = ENEMY_ARCH_SWARM;
        e->state = ENEMY_STATE_SWARM;
        enemy_assign_combat_loadout(g, e, db);
        e->wave_id = wave_id;
        e->slot_index = i;
        if (bidirectional_spawns) {
            const float spawn_side = (goal_dir < 0.0f) ? 1.0f : -1.0f;
            e->b.x = g->camera_x + spawn_side * (g->world_w * p->spawn_x01 + frand01() * p->spawn_x_span * su);
        } else {
            e->b.x = g->camera_x + g->world_w * p->spawn_x01 + frand01() * p->spawn_x_span * su;
        }
        enforce_auto_spawn_side(g, e, bidirectional_spawns);
        e->b.y = g->world_h * p->spawn_y01 + frands1() * p->spawn_y_span * su;
        e->home_y = g->world_h * p->spawn_y01;
        e->max_speed = p->max_speed * su;
        e->accel = p->accel;
        e->radius = (p->radius_min + frand01() * (p->radius_max - p->radius_min)) * su;
        e->swarm_sep_w = p->sep_w;
        e->swarm_ali_w = p->ali_w;
        e->swarm_coh_w = p->coh_w;
        e->swarm_avoid_w = p->avoid_w;
        e->swarm_goal_w = p->goal_w;
        e->swarm_sep_r = p->sep_r * su;
        e->swarm_ali_r = p->ali_r * su;
        e->swarm_coh_r = p->coh_r * su;
        e->swarm_goal_amp = p->goal_amp * su;
        e->swarm_goal_freq = p->goal_freq;
        e->swarm_goal_dir = (goal_dir < 0.0f) ? -1.0f : 1.0f;
        e->swarm_wander_w = p->wander_w;
        e->swarm_wander_freq = p->wander_freq;
        e->swarm_drag = p->steer_drag;
    }
}

static void spawn_wave_kamikaze(game_state* g, const leveldef_db* db, const leveldef_level* lvl, int wave_id, int bidirectional_spawns, float su) {
    const leveldef_wave_kamikaze_tuning* w = &lvl->kamikaze;
    (void)db;
    if (w->count <= 0) {
        return;
    }
    {
        const float spawn_side = bidirectional_spawns ? ((frand01() < 0.5f) ? -1.0f : 1.0f) : 1.0f;
        for (int i = 0; i < w->count; ++i) {
            enemy* e = spawn_enemy_common(g, su);
            if (!e) {
                break;
            }
            e->archetype = ENEMY_ARCH_KAMIKAZE;
            e->state = ENEMY_STATE_KAMIKAZE;
            enemy_assign_combat_loadout(g, e, db);
            e->wave_id = wave_id;
            e->slot_index = i;
            e->b.x = g->camera_x + spawn_side * (g->world_w * w->start_x01 + (float)i * w->spacing_x * su);
            enforce_auto_spawn_side(g, e, bidirectional_spawns);
            {
                const float margin = w->y_margin * su;
                e->b.y = margin + frand01() * fmaxf(g->world_h - 2.0f * margin, 1.0f);
            }
            e->max_speed = w->max_speed * su;
            e->accel = w->accel;
            e->radius = (w->radius_min + frand01() * fmaxf(w->radius_max - w->radius_min, 0.0f)) * su;
        }
    }
}

void enemy_spawn_curated_enemy(
    game_state* g,
    const leveldef_db* db,
    const leveldef_level* lvl,
    int wave_id,
    const leveldef_curated_enemy* ce,
    float su,
    int uses_cylinder,
    float period
) {
    int count;
    if (!g || !db || !lvl || !ce) {
        return;
    }
    count = (int)lroundf(fmaxf(1.0f, ce->a));
    if (count > 24) {
        count = 24;
    }
    if (ce->kind == 5) {
        const int profile_id = lvl->default_boid_profile;
        const leveldef_boid_profile* p = leveldef_get_boid_profile(db, profile_id);
        if (!p) {
            return;
        }
        for (int i = 0; i < count; ++i) {
            enemy* e = spawn_enemy_common(g, su);
            if (!e) {
                break;
            }
            e->archetype = ENEMY_ARCH_SWARM;
            e->state = ENEMY_STATE_SWARM;
            enemy_assign_combat_loadout(g, e, db);
            e->wave_id = wave_id;
            e->slot_index = i;
            e->b.x = g->world_w * ce->x01 + frands1() * 14.0f * su;
            e->b.y = g->world_h * ce->y01 + frands1() * 20.0f * su;
            e->home_y = g->world_h * ce->y01;
            e->max_speed = ((ce->b > 0.0f) ? ce->b : p->max_speed) * su;
            e->accel = (ce->c > 0.0f) ? ce->c : p->accel;
            e->radius = (p->radius_min + frand01() * fmaxf(p->radius_max - p->radius_min, 0.0f)) * su;
            e->swarm_sep_w = p->sep_w;
            e->swarm_ali_w = p->ali_w;
            e->swarm_coh_w = p->coh_w;
            e->swarm_avoid_w = p->avoid_w;
            e->swarm_goal_w = p->goal_w;
            e->swarm_sep_r = p->sep_r * su;
            e->swarm_ali_r = p->ali_r * su;
            e->swarm_coh_r = p->coh_r * su;
            e->swarm_goal_amp = p->goal_amp * su;
            e->swarm_goal_freq = p->goal_freq;
            e->swarm_goal_dir = 1.0f;
            e->swarm_wander_w = p->wander_w;
            e->swarm_wander_freq = p->wander_freq;
            e->swarm_drag = p->steer_drag;
        }
        return;
    }

    for (int i = 0; i < count; ++i) {
        enemy* e = spawn_enemy_common(g, su);
        if (!e) {
            break;
        }
        e->wave_id = wave_id;
        e->slot_index = i;
        e->b.x = g->world_w * ce->x01 + (float)i * 18.0f * su;
        e->b.y = g->world_h * ce->y01 + frands1() * 10.0f * su;
        enemy_assign_combat_loadout(g, e, db);

        if (ce->kind == 4) {
            e->archetype = ENEMY_ARCH_KAMIKAZE;
            e->state = ENEMY_STATE_KAMIKAZE;
            e->max_speed = ((ce->b > 0.0f) ? ce->b : lvl->kamikaze.max_speed) * su;
            e->accel = (ce->c > 0.0f) ? ce->c : lvl->kamikaze.accel;
            e->radius = (lvl->kamikaze.radius_min + frand01() * fmaxf(lvl->kamikaze.radius_max - lvl->kamikaze.radius_min, 0.0f)) * su;
        } else {
            e->archetype = ENEMY_ARCH_FORMATION;
            e->state = ENEMY_STATE_FORMATION;
            e->formation_kind = (ce->kind == 3) ? ENEMY_FORMATION_V : ENEMY_FORMATION_SINE;
            e->home_y = g->world_h * ce->y01;
            e->b.y = e->home_y;
            e->form_phase = (float)i * 0.4f;
            e->form_amp = fmaxf(0.0f, ce->b) * su;
            e->form_freq = (ce->kind == 3) ? lvl->v.form_freq : lvl->sine.form_freq;
            e->break_delay_s = 0.8f + 0.14f * (float)i;
            e->max_speed = ((ce->c > 0.0f) ? ce->c : ((ce->kind == 3) ? lvl->v.max_speed : lvl->sine.max_speed)) * su;
            e->accel = (ce->kind == 3) ? lvl->v.accel : lvl->sine.accel;
            e->lane_dir = lane_dir_toward_player_x(g, e->b.x, uses_cylinder, period);
        }
    }
}

void enemy_spawn_next_wave(
    game_state* g,
    const leveldef_db* db,
    const leveldef_level* lvl,
    float su,
    int uses_cylinder,
    float period
) {
    const int wave_id = ++g->wave_id_alloc;
    int bidirectional_spawns;
    if (!g || !db || !lvl) {
        return;
    }
    bidirectional_spawns = (lvl->render_style == LEVEL_RENDER_CYLINDER && lvl->bidirectional_spawns != 0) ? 1 : 0;

    if (lvl->wave_mode == LEVELDEF_WAVES_BOID_ONLY) {
        if (lvl->boid_cycle_count <= 0) {
            return;
        }
        {
            const int profile_id = lvl->boid_cycle[g->wave_index % lvl->boid_cycle_count];
            const leveldef_boid_profile* p = leveldef_get_boid_profile(db, profile_id);
            if (!p) {
                return;
            }
            announce_wave(g, p->wave_name);
            {
                const float dir = bidirectional_spawns ? ((frand01() < 0.5f) ? -1.0f : 1.0f) : 1.0f;
                spawn_wave_swarm_profile(g, db, wave_id, profile_id, dir, bidirectional_spawns, su);
                if (bidirectional_spawns && g->wave_index >= 4 && frand01() < lvl->cylinder_double_swarm_chance) {
                    const int wave_id_2 = ++g->wave_id_alloc;
                    spawn_wave_swarm_profile(g, db, wave_id_2, profile_id, -dir, bidirectional_spawns, su);
                }
            }
        }
        g->wave_index += 1;
        g->wave_cooldown_s = lvl->wave_cooldown_between_s;
        return;
    }

    if (lvl->wave_mode == LEVELDEF_WAVES_CURATED) {
        if (lvl->curated_count <= 0) {
            return;
        }
        {
            const leveldef_curated_enemy* ce = &lvl->curated[g->wave_index % lvl->curated_count];
            if (ce->kind == 5) {
                announce_wave(g, "curated boid contact");
            } else if (ce->kind == 4) {
                announce_wave(g, "curated kamikaze contact");
            } else if (ce->kind == 3) {
                announce_wave(g, "curated v wing");
            } else {
                announce_wave(g, "curated sine wing");
            }
            enemy_spawn_curated_enemy(g, db, lvl, wave_id, ce, su, uses_cylinder, period);
        }
        g->wave_index += 1;
        g->wave_cooldown_s = lvl->wave_cooldown_between_s;
        return;
    }

    if (lvl->wave_cycle_count <= 0) {
        return;
    }

    {
        const int pattern = lvl->wave_cycle[g->wave_index % lvl->wave_cycle_count];
        if (pattern == LEVELDEF_WAVE_SINE_SNAKE) {
            announce_wave(g, "sine snake formation");
            spawn_wave_sine_snake(g, db, lvl, wave_id, bidirectional_spawns, su, uses_cylinder, period);
        } else if (pattern == LEVELDEF_WAVE_V_FORMATION) {
            announce_wave(g, "galaxian break v formation");
            spawn_wave_v_formation(g, db, lvl, wave_id, bidirectional_spawns, su, uses_cylinder, period);
        } else if (pattern == LEVELDEF_WAVE_SWARM) {
            const int profile_id = lvl->default_boid_profile;
            announce_wave(g, "boid swarm cluster");
            {
                const float dir = bidirectional_spawns ? ((frand01() < 0.5f) ? -1.0f : 1.0f) : 1.0f;
                spawn_wave_swarm_profile(g, db, wave_id, profile_id, dir, bidirectional_spawns, su);
                if (bidirectional_spawns && g->wave_index >= 4 && frand01() < lvl->cylinder_double_swarm_chance) {
                    const int wave_id_2 = ++g->wave_id_alloc;
                    spawn_wave_swarm_profile(g, db, wave_id_2, profile_id, -dir, bidirectional_spawns, su);
                }
            }
        } else {
            announce_wave(g, "kamikaze crash wing");
            spawn_wave_kamikaze(g, db, lvl, wave_id, bidirectional_spawns, su);
        }
    }
    g->wave_index += 1;
    g->wave_cooldown_s = lvl->wave_cooldown_between_s;
}

static void enemy_fire_projectiles(game_state* g, const enemy* e, const enemy_weapon_def* w, const enemy_fire_tuning* t, int uses_cylinder, float period) {
    const float aim_lead = w->aim_lead_s;
    const float tx = g->player.b.x + g->player.b.vx * aim_lead;
    const float ty = g->player.b.y + g->player.b.vy * aim_lead;
    float dx = uses_cylinder ? wrap_delta(tx, e->b.x, period) : (tx - e->b.x);
    float dy = ty - e->b.y;
    normalize2(&dx, &dy);
    {
        const float err_rad = frands1() * t->aim_error_deg * (3.14159265359f / 180.0f);
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
            {
                const float c = cosf(offset);
                const float s = sinf(offset);
                float dir_x = base_x * c - base_y * s;
                float dir_y = base_x * s + base_y * c;
                normalize2(&dir_x, &dir_y);
                if (spawn_enemy_bullet(g, e, dir_x, dir_y, w->projectile_speed * t->projectile_speed_scale, w->projectile_ttl_s, w->projectile_radius)) {
                    spawned = 1;
                }
            }
        }
        if (spawned) {
            game_push_audio_event(g, GAME_AUDIO_EVENT_ENEMY_FIRE, e->b.x, e->b.y);
        }
    }
}

static void enemy_try_fire(game_state* g, enemy* e, float dt, const leveldef_db* db, int uses_cylinder, float period) {
    const leveldef_combat_tuning* combat;
    enemy_weapon_def w_local;
    const enemy_weapon_def* w;
    enemy_fire_tuning t;

    if (!g || !e || !e->active || !e->armed || g->lives <= 0) {
        return;
    }
    if (e->weapon_id < 0 || e->weapon_id >= ENEMY_WEAPON_COUNT) {
        e->weapon_id = ENEMY_WEAPON_PULSE;
    }

    combat = &db->combat;
    w_local = (enemy_weapon_def){
        .cooldown_min_s = combat->weapon[e->weapon_id].cooldown_min_s,
        .cooldown_max_s = combat->weapon[e->weapon_id].cooldown_max_s,
        .burst_count = combat->weapon[e->weapon_id].burst_count,
        .burst_gap_s = combat->weapon[e->weapon_id].burst_gap_s,
        .projectiles_per_shot = combat->weapon[e->weapon_id].projectiles_per_shot,
        .spread_deg = combat->weapon[e->weapon_id].spread_deg,
        .projectile_speed = combat->weapon[e->weapon_id].projectile_speed,
        .projectile_ttl_s = combat->weapon[e->weapon_id].projectile_ttl_s,
        .projectile_radius = combat->weapon[e->weapon_id].projectile_radius,
        .aim_lead_s = combat->weapon[e->weapon_id].aim_lead_s
    };
    w = &w_local;
    t = enemy_fire_tuning_for(g, db);

    if (e->burst_gap_timer_s > 0.0f) {
        e->burst_gap_timer_s -= dt;
    }
    if (e->fire_cooldown_s > 0.0f) {
        e->fire_cooldown_s -= dt;
    }

    if (e->burst_shots_left > 0 && e->burst_gap_timer_s <= 0.0f) {
        enemy_fire_projectiles(g, e, w, &t, uses_cylinder, period);
        e->burst_shots_left -= 1;
        if (e->burst_shots_left > 0) {
            e->burst_gap_timer_s = w->burst_gap_s;
        }
        return;
    }
    if (e->fire_cooldown_s > 0.0f) {
        return;
    }

    {
        const float dx_player = uses_cylinder ? wrap_delta(g->player.b.x, e->b.x, period) : (e->b.x - g->camera_x);
        const float dy_player = g->player.b.y - e->b.y;
        int in_fire_region = 0;
        if (uses_cylinder) {
            /* Same visible side of cylinder: +/- 90 degrees (half the circumference). */
            in_fire_region = (fabsf(dx_player) <= period * 0.25f) ? 1 : 0;
        } else {
            /* Same screen on defender-style levels. */
            in_fire_region =
                (fabsf(dx_player) <= g->world_w * 0.5f) &&
                (fabsf(dy_player) <= g->world_h * 0.5f);
        }
        if (!in_fire_region) {
            enemy_reset_fire_cooldown(w, &t, e);
            return;
        }
    }

    {
        const float dx = uses_cylinder ? wrap_delta(g->player.b.x, e->b.x, period) : (g->player.b.x - e->b.x);
        const float dy = g->player.b.y - e->b.y;
        const float d2 = dx * dx + dy * dy;
        const float rmin = t.fire_range_min;
        if (d2 < rmin * rmin) {
            enemy_reset_fire_cooldown(w, &t, e);
            return;
        }
    }

    enemy_fire_projectiles(g, e, w, &t, uses_cylinder, period);
    e->burst_shots_left = w->burst_count - 1;
    e->burst_gap_timer_s = (e->burst_shots_left > 0) ? w->burst_gap_s : 0.0f;
    enemy_reset_fire_cooldown(w, &t, e);
}

static void update_enemy_formation(game_state* g, enemy* e, float dt, float su, int uses_cylinder, float period) {
    const float dx_player = uses_cylinder ? wrap_delta(g->player.b.x, e->b.x, period) : (g->player.b.x - e->b.x);
    const float dy_player = g->player.b.y - e->b.y;
    const int same_screen =
        (fabsf(dx_player) <= g->world_w * 0.52f) &&
        (fabsf(dy_player) <= g->world_h * 0.52f);

    e->ai_timer_s += dt;
    if (e->state == ENEMY_STATE_FORMATION) {
        switch (e->formation_kind) {
            case ENEMY_FORMATION_SINE: {
                const float dx_to_player = uses_cylinder ? wrap_delta(g->player.b.x, e->b.x, period) : (g->player.b.x - e->b.x);
                if (fabsf(dx_to_player) > g->world_w * 0.10f) {
                    e->lane_dir = (dx_to_player < 0.0f) ? -1.0f : 1.0f;
                }
                {
                    const float lane_dir = (e->lane_dir < 0.0f) ? -1.0f : 1.0f;
                    const float target_vx = lane_dir * 165.0f * su;
                    const float desired_y = e->home_y + sinf(g->t * e->form_freq + e->form_phase) * e->form_amp;
                    const float target_vy = (desired_y - e->b.y) * 2.4f;
                    steer_to_velocity(&e->b, target_vx, target_vy, e->accel, 1.2f);
                }
            } break;
            case ENEMY_FORMATION_V:
            default: {
                const float desired_y = e->home_y + sinf(g->t * e->form_freq + e->form_phase) * e->form_amp;
                const float lane_dir = (e->lane_dir < 0.0f) ? -1.0f : 1.0f;
                const float target_vx = lane_dir * 165.0f * su;
                const float target_vy = (desired_y - e->b.y) * 2.4f;
                steer_to_velocity(&e->b, target_vx, target_vy, e->accel, 1.2f);
                if (same_screen) {
                    const float warmup_s = 0.9f;
                    const float mean_interval_s = 2.7f;
                    if (e->ai_timer_s > warmup_s) {
                        const float p_dt = 1.0f - expf(-fmaxf(dt, 0.0f) / mean_interval_s);
                        if (frand01() < p_dt) {
                            e->state = ENEMY_STATE_BREAK_ATTACK;
                            e->ai_timer_s = 0.0f;
                            e->break_delay_s = 1.6f + frand01() * 1.1f;
                        }
                    }
                }
            } break;
        }
    } else {
        const float lead = 0.45f;
        const float tx = g->player.b.x + g->player.b.vx * lead;
        const float ty = g->player.b.y + g->player.b.vy * lead;
        float to_x = uses_cylinder ? wrap_delta(tx, e->b.x, period) : (tx - e->b.x);
        float to_y = ty - e->b.y;
        float dir_x, dir_y;

        normalize2(&to_x, &to_y);
        if (e->formation_kind == ENEMY_FORMATION_V) {
            const float turn_sign = ((e->slot_index ^ e->wave_id) & 1) ? -1.0f : 1.0f;
            const float arc_t = clampf(e->ai_timer_s / fmaxf(e->break_delay_s, 0.1f), 0.0f, 1.0f);
            const float arc_w = (1.0f - arc_t) * 1.05f;
            const float px = -to_y * turn_sign;
            const float py = to_x * turn_sign;
            dir_x = to_x + px * arc_w;
            dir_y = to_y + py * arc_w;
            normalize2(&dir_x, &dir_y);
            steer_to_velocity(
                &e->b,
                dir_x * (e->max_speed * 1.62f),
                dir_y * (e->max_speed * 1.62f),
                e->accel * 1.35f,
                0.92f
            );
        } else {
            dir_x = to_x;
            dir_y = to_y;
            steer_to_velocity(
                &e->b,
                dir_x * (e->max_speed * 1.18f),
                dir_y * (e->max_speed * 1.18f),
                e->accel * 1.25f,
                1.0f
            );
        }
        if (e->ai_timer_s > fmaxf(e->break_delay_s, 1.4f)) {
            e->state = ENEMY_STATE_FORMATION;
            e->ai_timer_s = 0.0f;
            e->break_delay_s = 0.0f;
        }
    }
}

static void update_enemy_kamikaze(game_state* g, enemy* e, int uses_cylinder, float period) {
    const float lead = 0.25f;
    const float tx = g->player.b.x + g->player.b.vx * lead;
    const float ty = g->player.b.y + g->player.b.vy * lead;
    float dir_x = uses_cylinder ? wrap_delta(tx, e->b.x, period) : (tx - e->b.x);
    float dir_y = ty - e->b.y;
    normalize2(&dir_x, &dir_y);
    steer_to_velocity(&e->b, dir_x * e->max_speed, dir_y * e->max_speed, e->accel * 1.35f, 0.8f);
}

static void update_enemy_swarm(game_state* g, enemy* e, float dt, int uses_cylinder, float period, float su) {
    (void)dt;
    float sep_x = 0.0f, sep_y = 0.0f, ali_x = 0.0f, ali_y = 0.0f, coh_x = 0.0f, coh_y = 0.0f;
    int ali_n = 0, coh_n = 0;
    const float sep_r = (e->swarm_sep_r > 1.0f) ? e->swarm_sep_r : (70.0f * su);
    const float ali_r = (e->swarm_ali_r > 1.0f) ? e->swarm_ali_r : (180.0f * su);
    const float coh_r = (e->swarm_coh_r > 1.0f) ? e->swarm_coh_r : (220.0f * su);
    const float sep_r2 = sep_r * sep_r;
    const float ali_r2 = ali_r * ali_r;
    const float coh_r2 = coh_r * coh_r;
    for (size_t i = 0; i < MAX_ENEMIES; ++i) {
        const enemy* o = &g->enemies[i];
        if (!o->active || o == e || o->archetype != ENEMY_ARCH_SWARM) {
            continue;
        }
        const float dx = uses_cylinder ? wrap_delta(o->b.x, e->b.x, period) : (o->b.x - e->b.x);
        const float dy = o->b.y - e->b.y;
        const float d2 = dx * dx + dy * dy;
        if (d2 < 1e-4f) {
            continue;
        }
        if (d2 < sep_r2) {
            sep_x -= dx / d2;
            sep_y -= dy / d2;
        }
        if (d2 < ali_r2) {
            ali_x += o->b.vx;
            ali_y += o->b.vy;
            ali_n += 1;
        }
        if (d2 < coh_r2) {
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
    float player_avoid_boost = 0.0f;
    {
        const float lead = 0.22f;
        const float px = g->player.b.x + g->player.b.vx * lead;
        const float py = g->player.b.y + g->player.b.vy * lead;
        float dx = uses_cylinder ? wrap_delta(e->b.x, px, period) : (e->b.x - px);
        float dy = e->b.y - py;
        float d2 = dx * dx + dy * dy;
        const float aware_r = 300.0f * su;
        const float aware_r2 = aware_r * aware_r;
        const float personal_r = 120.0f * su;
        const float personal_r2 = personal_r * personal_r;
        if (d2 < aware_r2) {
            if (d2 < 1e-4f) {
                d2 = 1e-4f;
            }
            {
                const float far_falloff = 1.0f - (d2 / aware_r2);
                avoid_x += (dx / d2) * far_falloff;
                avoid_y += (dy / d2) * far_falloff;
                if (far_falloff > player_avoid_boost) {
                    player_avoid_boost = far_falloff;
                }
            }
            if (d2 < personal_r2) {
                const float near_falloff = 1.0f - (d2 / personal_r2);
                avoid_x += (dx / d2) * (1.75f * near_falloff);
                avoid_y += (dy / d2) * (1.75f * near_falloff);
                if (near_falloff > player_avoid_boost) {
                    player_avoid_boost = near_falloff;
                }
            }
        }
    }
    if (g->searchlight_count > 0) {
        for (int i = 0; i < g->searchlight_count && i < MAX_SEARCHLIGHTS; ++i) {
            const searchlight* sl = &g->searchlights[i];
            if (!sl->active) {
                continue;
            }
            float dx = uses_cylinder ? wrap_delta(e->b.x, sl->origin_x, period) : (e->b.x - sl->origin_x);
            float dy = e->b.y - sl->origin_y;
            float d2 = dx * dx + dy * dy;
            const float avoid_r = fmaxf(sl->source_radius + 64.0f * su, 28.0f * su);
            const float avoid_r2 = avoid_r * avoid_r;
            if (d2 >= avoid_r2) {
                continue;
            }
            if (d2 < 1e-4f) {
                d2 = 1e-4f;
            }
            {
                const float falloff = 1.0f - (d2 / avoid_r2);
                avoid_x += (dx / d2) * falloff;
                avoid_y += (dy / d2) * falloff;
            }
        }
    }

    {
        const float goal_dir = (e->swarm_goal_dir < 0.0f) ? -1.0f : 1.0f;
        float goal_x = (g->player.b.x + goal_dir * 280.0f * su) - e->b.x;
        float goal_y;
        float wander_x = 0.0f;
        float wander_y = 0.0f;
        float sep_w = (e->swarm_sep_w > 0.01f) ? e->swarm_sep_w : 1.85f;
        float ali_w = (e->swarm_ali_w > 0.01f) ? e->swarm_ali_w : 0.60f;
        float coh_w = (e->swarm_coh_w > 0.01f) ? e->swarm_coh_w : 0.55f;
        float avoid_w = (e->swarm_avoid_w > 0.01f) ? e->swarm_avoid_w : 2.70f;
        float goal_w = (e->swarm_goal_w > 0.01f) ? e->swarm_goal_w : 0.95f;
        const float wander_w = (e->swarm_wander_w > 0.01f) ? e->swarm_wander_w : 0.0f;
        const float wander_freq = (e->swarm_wander_freq > 0.01f) ? e->swarm_wander_freq : 0.9f;
        const float steer_drag = (e->swarm_drag > 0.01f) ? e->swarm_drag : 1.3f;

        if (uses_cylinder) {
            goal_x = wrap_delta(g->player.b.x + goal_dir * 280.0f * su, e->b.x, period);
        }
        {
            const float goal_amp = (e->swarm_goal_amp > 1.0f) ? e->swarm_goal_amp : (80.0f * su);
            const float goal_freq = (e->swarm_goal_freq > 0.01f) ? e->swarm_goal_freq : 0.70f;
            goal_y = (g->player.b.y + sinf(g->t * goal_freq + (float)e->slot_index * 0.35f) * goal_amp) - e->b.y;
        }

        {
            const float phase = (float)(e->wave_id & 31) * 0.61f;
            const float breathe = 0.5f + 0.5f * sinf(g->t * 0.85f + phase);
            const float tightness = 0.80f + 0.40f * breathe;
            sep_w *= (1.20f - 0.28f * tightness);
            ali_w *= (0.90f + 0.25f * tightness);
            coh_w *= tightness;
            goal_w *= (0.92f + 0.18f * tightness);
            avoid_w *= (1.0f + 2.4f * player_avoid_boost);
            goal_w *= (1.0f - 0.45f * player_avoid_boost);
        }

        {
            const float wp = g->t * wander_freq + (float)e->slot_index * 0.73f + (float)(e->wave_id & 31) * 0.29f;
            wander_x = cosf(wp) + 0.35f * sinf(wp * 0.57f + 1.3f);
            wander_y = sinf(wp * 1.11f + 0.8f) + 0.28f * cosf(wp * 0.49f + 0.4f);
        }

        normalize2(&sep_x, &sep_y);
        normalize2(&ali_x, &ali_y);
        normalize2(&coh_x, &coh_y);
        normalize2(&avoid_x, &avoid_y);
        normalize2(&goal_x, &goal_y);
        normalize2(&wander_x, &wander_y);

        {
            const float fx = sep_x * sep_w + ali_x * ali_w + coh_x * coh_w + avoid_x * avoid_w + goal_x * goal_w + wander_x * wander_w;
            const float fy = sep_y * sep_w + ali_y * ali_w + coh_y * coh_w + avoid_y * avoid_w + goal_y * goal_w + wander_y * wander_w;
            e->b.ax = fx * (e->accel * 135.0f) - e->b.vx * steer_drag;
            e->b.ay = fy * (e->accel * 135.0f) - e->b.vy * steer_drag;
        }
    }
}

void enemy_update_system(
    game_state* g,
    const leveldef_db* db,
    float dt,
    float su,
    int uses_cylinder,
    float period
) {
    int player_hit_this_frame = 0;
    if (!g || !db) {
        return;
    }

    for (size_t i = 0; i < MAX_ENEMIES; ++i) {
        enemy* e = &g->enemies[i];
        if (!e->active) {
            continue;
        }
        if (e->archetype == ENEMY_ARCH_SWARM) {
            update_enemy_swarm(g, e, dt, uses_cylinder, period, su);
        } else if (e->archetype == ENEMY_ARCH_KAMIKAZE) {
            update_enemy_kamikaze(g, e, uses_cylinder, period);
        } else {
            update_enemy_formation(g, e, dt, su, uses_cylinder, period);
        }
        integrate_body(&e->b, dt);
        {
            const float v = length2(e->b.vx, e->b.vy);
            if (v > e->max_speed) {
                const float s = e->max_speed / v;
                e->b.vx *= s;
                e->b.vy *= s;
            }
        }
        if (!uses_cylinder && e->b.x < g->camera_x - g->world_w * 0.72f) {
            if (e->archetype == ENEMY_ARCH_FORMATION) {
                e->state = ENEMY_STATE_BREAK_ATTACK;
                e->ai_timer_s = 0.0f;
                e->break_delay_s = 1.0f + frand01() * 1.3f;
            }
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
                dist_sq_level(uses_cylinder, period, e->b.x, e->b.y, g->player.b.x, g->player.b.y) <= hit_r * hit_r) {
                emit_enemy_debris(g, e, g->player.b.vx, g->player.b.vy);
                e->active = 0;
                apply_player_hit(g, g->player.b.x, g->player.b.y, g->player.b.vx, g->player.b.vy, su);
                player_hit_this_frame = 1;
            }
        }
        enemy_try_fire(g, e, dt, db, uses_cylinder, period);
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
        if (uses_cylinder) {
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
            if (dist_sq_level(uses_cylinder, period, b->b.x, b->b.y, g->player.b.x, g->player.b.y) <= hit_r * hit_r) {
                b->active = 0;
                apply_player_hit(g, g->player.b.x, g->player.b.y, b->b.vx, b->b.vy, su);
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
            if (dist_sq_level(uses_cylinder, period, g->bullets[bi].b.x, g->bullets[bi].b.y, g->enemies[ei].b.x, g->enemies[ei].b.y) <=
                g->enemies[ei].radius * g->enemies[ei].radius) {
                g->bullets[bi].active = 0;
                emit_enemy_debris(g, &g->enemies[ei], g->bullets[bi].b.vx, g->bullets[bi].b.vy);
                g->enemies[ei].active = 0;
                emit_explosion(g, g->enemies[ei].b.x, g->enemies[ei].b.y, g->enemies[ei].b.vx, g->enemies[ei].b.vy, 26, su);
                g->kills += 1;
                g->score += 100;
                break;
            }
        }
    }

    for (size_t i = 0; i < MAX_ENEMY_DEBRIS; ++i) {
        enemy_debris* d = &g->debris[i];
        if (!d->active) {
            continue;
        }
        d->age_s += dt;
        if (d->age_s >= d->life_s) {
            d->active = 0;
            continue;
        }
        integrate_body(&d->b, dt);
        d->angle += d->spin_rate * dt;
        d->alpha = clampf(1.0f - (d->age_s / d->life_s), 0.0f, 1.0f);
        if (d->b.y < -48.0f * su) {
            d->active = 0;
            continue;
        }
        if (!uses_cylinder && fabsf(d->b.x - g->camera_x) > g->world_w * 1.4f) {
            d->active = 0;
            continue;
        }
    }
}
