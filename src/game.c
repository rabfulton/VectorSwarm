#include "game.h"
#include "enemy.h"
#include "leveldef.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static float frand01(void) {
    return (float)rand() / (float)RAND_MAX;
}

static float frands1(void) {
    return frand01() * 2.0f - 1.0f;
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

static float clampf(float v, float lo, float hi) {
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

static leveldef_db g_leveldef;
static int g_leveldef_ready = 0;
static char g_level_dir[256];

static leveldef_discovered_level g_levels[LEVELDEF_MAX_DISCOVERED_LEVELS];
static int g_level_count = 0;

static int strieq(const char* a, const char* b) {
    if (!a || !b) {
        return 0;
    }
    while (*a && *b) {
        char ca = *a;
        char cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
        if (ca != cb) {
            return 0;
        }
        ++a;
        ++b;
    }
    return (*a == '\0' && *b == '\0');
}

static const leveldef_level* current_leveldef(const game_state* g) {
    if (g && g_level_count > 0 && g->level_index >= 0 && g->level_index < g_level_count) {
        return &g_levels[g->level_index].level;
    }
    if (!g) {
        return NULL;
    }
    return leveldef_get_level(&g_leveldef, g->level_style);
}

static void ensure_leveldef_loaded(void) {
    static const char* level_dirs[] = {
        "../data/levels",
        "data/levels"
    };
    const char* chosen_dir = NULL;
    if (g_leveldef_ready) {
        return;
    }
    for (int i = 0; i < (int)(sizeof(level_dirs) / sizeof(level_dirs[0])); ++i) {
        char probe[256];
        FILE* f = NULL;
        if (snprintf(probe, sizeof(probe), "%s/combat.cfg", level_dirs[i]) >= (int)sizeof(probe)) {
            continue;
        }
        f = fopen(probe, "r");
        if (f) {
            fclose(f);
            chosen_dir = level_dirs[i];
            break;
        }
    }
    if (!chosen_dir) {
        fprintf(
            stderr,
            "FATAL: could not locate level config directory. Expected ../data/levels or data/levels from cwd.\n"
        );
        exit(1);
    }
    if (!leveldef_load_project_layout(&g_leveldef, chosen_dir, stderr)) {
        fprintf(stderr, "FATAL: failed to load LevelDef config from %s\n", chosen_dir);
        exit(1);
    }
    snprintf(g_level_dir, sizeof(g_level_dir), "%s", chosen_dir);
    g_level_count = leveldef_discover_levels_from_dir(
        &g_leveldef,
        chosen_dir,
        g_levels,
        LEVELDEF_MAX_DISCOVERED_LEVELS,
        stderr
    );
    if (g_level_count <= 0) {
        fprintf(stderr, "FATAL: no levels discovered in %s\n", chosen_dir);
        exit(1);
    }
    g_leveldef_ready = 1;
}

static float gameplay_ui_scale(const game_state* g);
static void normalize2(float* x, float* y);
static void game_push_audio_event(game_state* g, game_audio_event_type type, float x, float y);
static enemy_bullet* spawn_enemy_bullet_at(
    game_state* g,
    float ox,
    float oy,
    float dir_x,
    float dir_y,
    float speed,
    float ttl_s,
    float radius
);
static int set_level_index(game_state* g, int index);

static float dist_sq(float ax, float ay, float bx, float by) {
    const float dx = ax - bx;
    const float dy = ay - by;
    return dx * dx + dy * dy;
}

static float deg_to_rad(float d) {
    return d * (3.14159265359f / 180.0f);
}

static float cross2(float ax, float ay, float bx, float by, float px, float py) {
    return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
}

static int point_in_triangle(float px, float py, float ax, float ay, float bx, float by, float cx, float cy) {
    const float c0 = cross2(ax, ay, bx, by, px, py);
    const float c1 = cross2(bx, by, cx, cy, px, py);
    const float c2 = cross2(cx, cy, ax, ay, px, py);
    const int has_neg = (c0 < 0.0f) || (c1 < 0.0f) || (c2 < 0.0f);
    const int has_pos = (c0 > 0.0f) || (c1 > 0.0f) || (c2 > 0.0f);
    return !(has_neg && has_pos);
}

static float searchlight_angle(const game_state* g, const searchlight* sl) {
    if (!g || !sl) {
        return 0.0f;
    }
    const float phase = g->t * sl->sweep_speed + sl->sweep_phase;
    float q = 0.0f;
    if (sl->sweep_motion == SEARCHLIGHT_MOTION_SPIN) {
        return sl->sweep_center_rad + phase;
    }
    if (sl->sweep_motion == SEARCHLIGHT_MOTION_LINEAR) {
        const float tri = (2.0f / 3.14159265359f) * asinf(sinf(phase));
        q = clampf(tri, -1.0f, 1.0f);
    } else {
        /* Smooth pendulum-like sweep (sinusoid). */
        q = sinf(phase);
    }
    return sl->sweep_center_rad + sl->sweep_amplitude_rad * q;
}

static void searchlight_build_triangle(
    const searchlight* sl,
    float* ax,
    float* ay,
    float* bx,
    float* by,
    float* cx,
    float* cy
) {
    const float a0 = sl->current_angle_rad - sl->half_angle_rad;
    const float a1 = sl->current_angle_rad + sl->half_angle_rad;
    const float d0x = cosf(a0);
    const float d0y = sinf(a0);
    const float d1x = cosf(a1);
    const float d1y = sinf(a1);
    *ax = sl->origin_x;
    *ay = sl->origin_y;
    *bx = sl->origin_x + d0x * sl->length;
    *by = sl->origin_y + d0y * sl->length;
    *cx = sl->origin_x + d1x * sl->length;
    *cy = sl->origin_y + d1y * sl->length;
}

static void configure_searchlights_for_level(game_state* g) {
    const leveldef_level* lvl;
    if (!g) {
        return;
    }
    ensure_leveldef_loaded();
    memset(g->searchlights, 0, sizeof(g->searchlights));
    g->searchlight_count = 0;
    lvl = current_leveldef(g);
    if (!lvl) {
        return;
    }
    for (int i = 0; i < lvl->searchlight_count && g->searchlight_count < MAX_SEARCHLIGHTS; ++i) {
        const leveldef_searchlight* d = &lvl->searchlights[i];
        searchlight* sl = &g->searchlights[g->searchlight_count++];
        memset(sl, 0, sizeof(*sl));
        sl->active = 1;
        sl->origin_x = g->world_w * d->anchor_x01;
        sl->origin_y = g->world_h * d->anchor_y01;
        sl->length = g->world_h * d->length_h01;
        sl->half_angle_rad = deg_to_rad(d->half_angle_deg);
        sl->sweep_center_rad = deg_to_rad(d->sweep_center_deg);
        sl->sweep_amplitude_rad = deg_to_rad(d->sweep_amplitude_deg);
        sl->sweep_speed = d->sweep_speed;
        sl->sweep_phase = deg_to_rad(d->sweep_phase_deg);
        sl->sweep_motion = d->sweep_motion;
        sl->source_type = d->source_type;
        sl->source_radius = d->source_radius;
        sl->clear_grace_s = d->clear_grace_s;
        sl->damage_interval_s = fmaxf(d->fire_interval_s, 0.005f);
        sl->projectile_speed = d->projectile_speed;
        sl->projectile_ttl_s = d->projectile_ttl_s;
        sl->projectile_radius = d->projectile_radius;
        sl->aim_jitter_rad = deg_to_rad(d->aim_jitter_deg);
        sl->damage_timer_s = sl->damage_interval_s;
        sl->alert_timer_s = 0.0f;
        sl->current_angle_rad = sl->sweep_center_rad;
    }
}

static void update_searchlights(game_state* g, float dt) {
    if (!g || g->searchlight_count <= 0) {
        return;
    }
    const float su = gameplay_ui_scale(g);
    for (int i = 0; i < g->searchlight_count && i < MAX_SEARCHLIGHTS; ++i) {
        searchlight* sl = &g->searchlights[i];
        if (!sl->active) {
            continue;
        }
        const float sweep_angle = searchlight_angle(g, sl);
        sl->current_angle_rad = sweep_angle;
        float ax = 0.0f, ay = 0.0f, bx = 0.0f, by = 0.0f, cx = 0.0f, cy = 0.0f;
        searchlight_build_triangle(sl, &ax, &ay, &bx, &by, &cx, &cy);
        const int spotted_by_sweep = point_in_triangle(g->player.b.x, g->player.b.y, ax, ay, bx, by, cx, cy);
        if (spotted_by_sweep) {
            sl->alert_timer_s = sl->clear_grace_s;
        } else if (sl->alert_timer_s > 0.0f) {
            sl->alert_timer_s -= dt;
            if (sl->alert_timer_s < 0.0f) {
                sl->alert_timer_s = 0.0f;
            }
        }
        if (sl->alert_timer_s > 0.0f) {
            sl->current_angle_rad = atan2f(g->player.b.y - sl->origin_y, g->player.b.x - sl->origin_x);
        }
        if (g->lives <= 0) {
            continue;
        }
        if (sl->alert_timer_s <= 0.0f) {
            sl->damage_timer_s = sl->damage_interval_s;
            continue;
        }
        sl->damage_timer_s -= dt;
        while (sl->damage_timer_s <= 0.0f) {
            sl->damage_timer_s += sl->damage_interval_s;
            float dx = g->player.b.x - sl->origin_x;
            float dy = g->player.b.y - sl->origin_y;
            normalize2(&dx, &dy);
            /* Slight spread so beam-fire feels synthetic but still targeted. */
            const float err = frands1() * sl->aim_jitter_rad;
            const float c = cosf(err);
            const float s = sinf(err);
            float dir_x = dx * c - dy * s;
            float dir_y = dx * s + dy * c;
            normalize2(&dir_x, &dir_y);
            if (spawn_enemy_bullet_at(
                    g,
                    sl->origin_x,
                    sl->origin_y,
                    dir_x,
                    dir_y,
                    sl->projectile_speed * su,
                    sl->projectile_ttl_s,
                    sl->projectile_radius)) {
                game_push_audio_event(g, GAME_AUDIO_EVENT_SEARCHLIGHT_FIRE, sl->origin_x, sl->origin_y);
            }
        }
    }
}

static float cylinder_period(const game_state* g) {
    return fmaxf(g->world_w * 2.4f, 1.0f);
}

static int level_uses_cylinder(const game_state* g) {
    return g && g->render_style == LEVEL_RENDER_CYLINDER;
}

static void configure_exit_portal_for_level(game_state* g) {
    const leveldef_level* lvl;
    const float su = gameplay_ui_scale(g);
    if (!g) {
        return;
    }
    g->exit_portal_active = 0;
    g->exit_portal_x = 0.0f;
    g->exit_portal_y = 0.0f;
    g->exit_portal_radius = 26.0f * su;
    if (level_uses_cylinder(g)) {
        return;
    }
    ensure_leveldef_loaded();
    lvl = current_leveldef(g);
    if (!lvl || !lvl->exit_enabled) {
        return;
    }
    g->exit_portal_active = 1;
    g->exit_portal_x = g->world_w * lvl->exit_x01;
    g->exit_portal_y = g->world_h * lvl->exit_y01;
    g->exit_portal_radius = 28.0f * su;
}

static void apply_level_runtime_config(game_state* g) {
    const leveldef_level* lvl;
    if (!g) {
        return;
    }
    ensure_leveldef_loaded();
    lvl = current_leveldef(g);
    if (!lvl) {
        fprintf(stderr, "FATAL: missing LevelDef for level style %d\n", g->level_style);
        exit(1);
    }
    g->render_style = lvl->render_style;
    g->wave_cooldown_s = lvl->wave_cooldown_initial_s;
    g->curated_spawned_count = 0;
    memset(g->curated_spawned, 0, sizeof(g->curated_spawned));
    configure_searchlights_for_level(g);
    configure_exit_portal_for_level(g);
}

static int set_level_index(game_state* g, int index) {
    const float su = gameplay_ui_scale(g);
    if (!g || g_level_count <= 0 || index < 0 || index >= g_level_count) {
        return 0;
    }
    g->level_index = index;
    g->level_style = g_levels[index].style_hint;
    memset(g->bullets, 0, sizeof(g->bullets));
    memset(g->enemy_bullets, 0, sizeof(g->enemy_bullets));
    memset(g->enemies, 0, sizeof(g->enemies));
    memset(g->particles, 0, sizeof(g->particles));
    memset(g->debris, 0, sizeof(g->debris));
    g->active_particles = 0;
    g->wave_index = 0;
    g->wave_id_alloc = 0;
    g->wave_announce_pending = 0;
    g->fire_sfx_pending = 0;
    g->player.b.x = 170.0f * su;
    g->player.b.y = g->world_h * 0.5f;
    g->player.b.vx = 0.0f;
    g->player.b.vy = 0.0f;
    g->camera_vx = 0.0f;
    g->camera_x = g->player.b.x;
    g->camera_y = g->world_h * 0.5f;
    g->shield_radius = 52.0f * su;
    g->shield_active = 0;
    apply_level_runtime_config(g);
    return 1;
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

static int spawn_bullet_single(game_state* g, float y_offset, float muzzle_speed, float dir, float muzzle_offset) {
    const float su = gameplay_ui_scale(g);
    const float vertical_inherit = 0.18f;
    for (size_t i = 0; i < MAX_BULLETS; ++i) {
        if (g->bullets[i].active) {
            continue;
        }
        bullet* b = &g->bullets[i];
        b->active = 1;
        b->b.x = g->player.b.x + dir * muzzle_offset * su;
        b->b.y = g->player.b.y + y_offset;
        b->spawn_x = b->b.x;
        b->b.vx = dir * muzzle_speed + g->player.b.vx;
        b->b.vy = g->player.b.vy * vertical_inherit;
        b->b.ax = 0.0f;
        b->b.ay = 0.0f;
        b->ttl_s = 2.0f;
        return 1;
    }
    return 0;
}

static void spawn_bullet(game_state* g) {
    const float su = gameplay_ui_scale(g);
    const float dir = (g->player.facing_x < 0.0f) ? -1.0f : 1.0f;
    g->fire_sfx_pending += 1;
    if (g->weapon_level <= 1) {
        (void)spawn_bullet_single(g, 0.0f, 760.0f * su, dir, 36.0f);
        return;
    }
    if (g->weapon_level == 2) {
        (void)spawn_bullet_single(g, -12.0f * su, 800.0f * su, dir, 36.0f);
        (void)spawn_bullet_single(g, 12.0f * su, 800.0f * su, dir, 36.0f);
        return;
    }
    (void)spawn_bullet_single(g, 0.0f, 860.0f * su, dir, 36.0f);
    (void)spawn_bullet_single(g, -15.0f * su, 860.0f * su, dir, 36.0f);
    (void)spawn_bullet_single(g, 15.0f * su, 860.0f * su, dir, 36.0f);
}

static void spawn_secondary_bullet(game_state* g) {
    if (!g || g->lives <= 0) {
        return;
    }
    if (g->alt_weapon_equipped != PLAYER_ALT_WEAPON_REAR_GUN) {
        return;
    }
    if (g->alt_weapon_ammo[PLAYER_ALT_WEAPON_REAR_GUN] <= 0) {
        return;
    }
    {
        const float su = gameplay_ui_scale(g);
        const float dir = (g->player.facing_x < 0.0f) ? 1.0f : -1.0f;
        if (spawn_bullet_single(g, 0.0f, 720.0f * su, dir, 30.0f)) {
            g->alt_weapon_ammo[PLAYER_ALT_WEAPON_REAR_GUN] -= 1;
            g->secondary_fire_cooldown_s = 0.085f;
            g->fire_sfx_pending += 1;
        }
    }
}

static enemy_bullet* spawn_enemy_bullet_at(
    game_state* g,
    float ox,
    float oy,
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
        b->b.x = ox + dir_x * (radius + 8.0f);
        b->b.y = oy + dir_y * (radius + 8.0f);
        b->b.vx = dir_x * speed;
        b->b.vy = dir_y * speed;
        b->b.ax = 0.0f;
        b->b.ay = 0.0f;
        return b;
    }
    return NULL;
}

void game_init(game_state* g, float world_w, float world_h) {
    memset(g, 0, sizeof(*g));
    g->world_w = world_w;
    g->world_h = world_h;
    g->lives = 3;
    g->weapon_level = 1;
    g->alt_weapon_equipped = PLAYER_ALT_WEAPON_SHIELD;
    g->alt_weapon_ammo[PLAYER_ALT_WEAPON_SHIELD] = 20;
    g->alt_weapon_ammo[PLAYER_ALT_WEAPON_MISSILE] = 8;
    g->alt_weapon_ammo[PLAYER_ALT_WEAPON_EMP] = 5;
    g->alt_weapon_ammo[PLAYER_ALT_WEAPON_REAR_GUN] = 180;
    g->shield_time_remaining_s = 20.0f;
    g->shield_active = 0;
    const float su = gameplay_ui_scale(g);
    g->player.b.x = 170.0f * su;
    g->player.b.y = world_h * 0.5f;
    g->player.thrust = 3300.0f * su;
    g->player.drag = 4.1f;
    g->player.max_speed = 760.0f * su;
    g->player.facing_x = 1.0f;
    g->shield_radius = 52.0f * su;
    g->camera_x = g->player.b.x;
    g->camera_y = world_h * 0.5f;
    g->level_style = LEVEL_STYLE_DEFENDER;
    g->level_index = 0;
    g->wave_index = 0;
    g->wave_id_alloc = 0;
    ensure_leveldef_loaded();
    for (int i = 0; i < g_level_count; ++i) {
        if (g_levels[i].style_hint == LEVEL_STYLE_DEFENDER) {
            g->level_index = i;
            break;
        }
    }
    if (g_level_count > 0) {
        g->level_style = g_levels[g->level_index].style_hint;
    }
    apply_level_runtime_config(g);

    for (size_t i = 0; i < MAX_STARS; ++i) {
        g->stars[i].x = frand01() * world_w;
        g->stars[i].y = frand01() * world_h;
        g->stars[i].prev_x = g->stars[i].x;
        g->stars[i].prev_y = g->stars[i].y;
        g->stars[i].speed = 50.0f + frand01() * 190.0f;
        g->stars[i].size = 0.9f + frand01() * 1.5f;
    }
}

void game_set_world_size(game_state* g, float world_w, float world_h) {
    if (!g || world_w <= 0.0f || world_h <= 0.0f) {
        return;
    }
    g->world_w = world_w;
    g->world_h = world_h;
    if (g->player.b.y < 0.0f) {
        g->player.b.y = 0.0f;
    }
    if (g->player.b.y > g->world_h) {
        g->player.b.y = g->world_h;
    }
    g->camera_y = g->world_h * 0.5f;
    g->shield_radius = 52.0f * gameplay_ui_scale(g);
    apply_level_runtime_config(g);
}

void game_cycle_level(game_state* g) {
    if (!g) {
        return;
    }
    ensure_leveldef_loaded();
    if (g_level_count > 0) {
        const int next = (g->level_index + 1) % g_level_count;
        (void)set_level_index(g, next);
    } else {
        g->level_style = (g->level_style + 1) % LEVEL_STYLE_COUNT;
        apply_level_runtime_config(g);
    }
}

static void game_handle_restart(game_state* g, const game_input* in) {
    if (in->restart && g->lives <= 0) {
        const int restart_level_index = g->level_index;
        game_init(g, g->world_w, g->world_h);
        if (!set_level_index(g, restart_level_index)) {
            apply_level_runtime_config(g);
        }
    }
}

static void game_update_stars(game_state* g, float dt) {
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
}

static int game_update_player(game_state* g, float dt, const game_input* in, float su) {
    if (g->lives <= 0) {
        return 0;
    }
    float input_x = 0.0f;
    float input_y = 0.0f;
    if (in->left) input_x -= 1.0f;
    if (in->right) input_x += 1.0f;
    if (in->up) input_y += 1.0f;
    if (in->down) input_y -= 1.0f;

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
    if (g->exit_portal_active &&
        dist_sq(g->player.b.x, g->player.b.y, g->exit_portal_x, g->exit_portal_y) <=
            g->exit_portal_radius * g->exit_portal_radius) {
        game_cycle_level(g);
        return 1;
    }
    return 0;
}

static void game_update_player_weapons(game_state* g, float dt, const game_input* in) {
    const int shield_equipped = (g->alt_weapon_equipped == PLAYER_ALT_WEAPON_SHIELD);
    const int shield_held = shield_equipped && in->secondary_fire && (g->shield_time_remaining_s > 0.0f);
    emit_thruster(g, dt);
    if (g->fire_cooldown_s > 0.0f) {
        g->fire_cooldown_s -= dt;
    }
    if (g->secondary_fire_cooldown_s > 0.0f) {
        g->secondary_fire_cooldown_s -= dt;
    }
    g->shield_active = shield_held ? 1 : 0;
    if (g->shield_active) {
        g->shield_time_remaining_s -= dt;
        if (g->shield_time_remaining_s <= 0.0f) {
            g->shield_time_remaining_s = 0.0f;
            g->shield_active = 0;
        }
    }
    if (g->score >= 3000) {
        g->weapon_level = 3;
    } else if (g->score >= 1200) {
        g->weapon_level = 2;
    } else {
        g->weapon_level = 1;
    }
    g->weapon_heat = clampf(g->weapon_heat - dt * 0.58f, 0.0f, 1.0f);
    if (g->lives > 0 && !g->shield_active && in->fire && g->fire_cooldown_s <= 0.0f) {
        spawn_bullet(g);
        g->fire_cooldown_s = 0.095f;
        g->weapon_heat = clampf(g->weapon_heat + 0.09f, 0.0f, 1.0f);
    }
    if (g->lives > 0 &&
        !shield_equipped &&
        in->secondary_fire &&
        g->secondary_fire_cooldown_s <= 0.0f) {
        spawn_secondary_bullet(g);
    }
}

static void game_update_player_bullets(game_state* g, float dt) {
    for (size_t i = 0; i < MAX_BULLETS; ++i) {
        if (!g->bullets[i].active) {
            continue;
        }
        bullet* b = &g->bullets[i];
        integrate_body(&b->b, dt);
        b->ttl_s -= dt;
        if (level_uses_cylinder(g)) {
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
}

static void game_update_wave_spawning(game_state* g, float dt) {
    if (g->lives <= 0) {
        return;
    }
    const leveldef_level* lvl = current_leveldef(g);
    if (!lvl) {
        return;
    }
    if (lvl->wave_mode == LEVELDEF_WAVES_CURATED &&
        lvl->render_style == LEVEL_RENDER_DEFENDER) {
        const float activate_min_x = g->camera_x + g->world_w * 0.05f;
        const float activate_x = g->camera_x + g->world_w * 1.18f;
        for (int i = 0; i < lvl->curated_count && i < MAX_CURATED_RUNTIME; ++i) {
            const leveldef_curated_enemy* ce = &lvl->curated[i];
            const float world_x = ce->x01 * g->world_w;
            if (g->curated_spawned[i]) {
                continue;
            }
            if (world_x >= activate_min_x && world_x <= activate_x) {
                const int wave_id = ++g->wave_id_alloc;
                enemy_spawn_curated_enemy(
                    g,
                    &g_leveldef,
                    lvl,
                    wave_id,
                    ce,
                    gameplay_ui_scale(g),
                    level_uses_cylinder(g),
                    cylinder_period(g)
                );
                g->curated_spawned[i] = 1u;
                g->curated_spawned_count += 1;
            }
        }
        return;
    }
    g->wave_cooldown_s -= dt;
    if (lvl->spawn_mode == LEVELDEF_SPAWN_SEQUENCED_CLEAR) {
        if (game_enemy_count(g) <= 0 && g->wave_cooldown_s <= 0.0f) {
            enemy_spawn_next_wave(
                g,
                &g_leveldef,
                lvl,
                gameplay_ui_scale(g),
                level_uses_cylinder(g),
                cylinder_period(g)
            );
        }
    } else if (lvl->spawn_mode == LEVELDEF_SPAWN_TIMED) {
        if (g->wave_cooldown_s <= 0.0f) {
            enemy_spawn_next_wave(
                g,
                &g_leveldef,
                lvl,
                gameplay_ui_scale(g),
                level_uses_cylinder(g),
                cylinder_period(g)
            );
            g->wave_cooldown_s = lvl->spawn_interval_s;
        }
    } else if (lvl->spawn_mode == LEVELDEF_SPAWN_TIMED_SEQUENCED) {
        if (g->wave_cooldown_s <= 0.0f && game_enemy_count(g) <= 0) {
            enemy_spawn_next_wave(
                g,
                &g_leveldef,
                lvl,
                gameplay_ui_scale(g),
                level_uses_cylinder(g),
                cylinder_period(g)
            );
            g->wave_cooldown_s = lvl->spawn_interval_s;
        }
    }
}

static void game_update_particles(game_state* g, float dt) {
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
                p->a = powf(inv, 1.35f);
            } else {
                p->a = inv * inv;
            }
        }
    }
}

static void game_update_camera(game_state* g, float dt) {
    float rear_bias = 0.25f;
    float spring_k = 18.0f;
    float damping = 8.2f;
    if (level_uses_cylinder(g)) {
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

void game_update(game_state* g, float dt, const game_input* in) {
    g->t += dt;
    const float su = gameplay_ui_scale(g);

    game_handle_restart(g, in);
    game_update_stars(g, dt);
    if (game_update_player(g, dt, in, su)) {
        return;
    }
    game_update_player_weapons(g, dt, in);
    game_update_player_bullets(g, dt);
    game_update_wave_spawning(g, dt);
    if (g->searchlight_count > 0) {
        update_searchlights(g, dt);
    }
    enemy_update_system(g, &g_leveldef, dt, su, level_uses_cylinder(g), cylinder_period(g));
    game_update_particles(g, dt);
    game_update_camera(g, dt);
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

const struct leveldef_db* game_leveldef_get(void) {
    ensure_leveldef_loaded();
    return &g_leveldef;
}

const char* game_current_level_name(const game_state* g) {
    ensure_leveldef_loaded();
    if (g && g_level_count > 0 && g->level_index >= 0 && g->level_index < g_level_count) {
        return g_levels[g->level_index].name;
    }
    if (!g) {
        return "level_defender";
    }
    switch (g->level_style) {
        case LEVEL_STYLE_ENEMY_RADAR: return "level_enemy_radar";
        case LEVEL_STYLE_EVENT_HORIZON: return "level_event_horizon";
        case LEVEL_STYLE_EVENT_HORIZON_LEGACY: return "level_event_horizon_legacy";
        case LEVEL_STYLE_HIGH_PLAINS_DRIFTER: return "level_high_plains_drifter";
        case LEVEL_STYLE_HIGH_PLAINS_DRIFTER_2: return "level_high_plains_drifter_2";
        case LEVEL_STYLE_FOG_OF_WAR: return "level_fog_of_war";
        default: return "level_defender";
    }
}

int game_set_level_by_name(game_state* g, const char* name) {
    if (!g || !name || !name[0]) {
        return 0;
    }
    ensure_leveldef_loaded();
    if (g_level_count <= 0) {
        return 0;
    }
    for (int i = 0; i < g_level_count; ++i) {
        if (strieq(g_levels[i].name, name)) {
            return set_level_index(g, i);
        }
    }
    return 0;
}

void game_set_alt_weapon(game_state* g, int weapon_id) {
    if (!g) {
        return;
    }
    g->alt_weapon_equipped = clampi(weapon_id, 0, PLAYER_ALT_WEAPON_COUNT - 1);
    if (g->alt_weapon_equipped != PLAYER_ALT_WEAPON_SHIELD) {
        g->shield_active = 0;
    }
}

int game_get_alt_weapon(const game_state* g) {
    if (!g) {
        return 0;
    }
    return clampi(g->alt_weapon_equipped, 0, PLAYER_ALT_WEAPON_COUNT - 1);
}

int game_get_alt_weapon_ammo(const game_state* g, int weapon_id) {
    if (!g) {
        return 0;
    }
    weapon_id = clampi(weapon_id, 0, PLAYER_ALT_WEAPON_COUNT - 1);
    if (weapon_id == PLAYER_ALT_WEAPON_SHIELD) {
        return (int)ceilf(fmaxf(g->shield_time_remaining_s, 0.0f));
    }
    return g->alt_weapon_ammo[weapon_id];
}
