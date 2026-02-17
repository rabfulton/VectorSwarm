#include "game.h"
#include "leveldef.h"

#include <dirent.h>
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

static leveldef_db g_leveldef;
static int g_leveldef_ready = 0;
static char g_level_dir[256];

#define MAX_DISCOVERED_LEVELS 128
typedef struct discovered_level {
    char name[64];
    int style_hint;
    leveldef_level level;
} discovered_level;
static discovered_level g_levels[MAX_DISCOVERED_LEVELS];
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

static int has_prefix(const char* s, const char* prefix) {
    if (!s || !prefix) return 0;
    while (*prefix) {
        if (*s++ != *prefix++) return 0;
    }
    return 1;
}

static int has_suffix(const char* s, const char* suffix) {
    size_t ls, lx;
    if (!s || !suffix) return 0;
    ls = strlen(s);
    lx = strlen(suffix);
    if (ls < lx) return 0;
    return strcmp(s + ls - lx, suffix) == 0;
}

static int cmp_strptr(const void* a, const void* b) {
    const char* sa = *(const char* const*)a;
    const char* sb = *(const char* const*)b;
    return strcmp(sa, sb);
}

static int discover_levels_from_dir(const char* dir_path) {
    DIR* d = NULL;
    struct dirent* de = NULL;
    char names[MAX_DISCOVERED_LEVELS][64];
    const char* ordered[MAX_DISCOVERED_LEVELS];
    int file_n = 0;
    int out_n = 0;
    if (!dir_path || !dir_path[0]) {
        return 0;
    }
    d = opendir(dir_path);
    if (!d) {
        return 0;
    }
    while ((de = readdir(d)) != NULL) {
        const char* fn = de->d_name;
        if (!has_prefix(fn, "level_") || !has_suffix(fn, ".cfg")) {
            continue;
        }
        if (file_n >= MAX_DISCOVERED_LEVELS) {
            break;
        }
        snprintf(names[file_n], sizeof(names[file_n]), "%s", fn);
        ordered[file_n] = names[file_n];
        ++file_n;
    }
    closedir(d);
    if (file_n <= 0) {
        return 0;
    }
    qsort(ordered, (size_t)file_n, sizeof(ordered[0]), cmp_strptr);
    for (int i = 0; i < file_n && out_n < MAX_DISCOVERED_LEVELS; ++i) {
        char path[512];
        char base_name[64];
        int style = -1;
        leveldef_level lvl;
        snprintf(path, sizeof(path), "%s/%s", dir_path, ordered[i]);
        if (!leveldef_load_level_file_with_base(&g_leveldef, path, &lvl, &style, stderr)) {
            continue;
        }
        snprintf(base_name, sizeof(base_name), "%s", ordered[i]);
        {
            const size_t n = strlen(base_name);
            if (n > 4 && strcmp(base_name + n - 4, ".cfg") == 0) {
                base_name[n - 4] = '\0';
            }
        }
        snprintf(g_levels[out_n].name, sizeof(g_levels[out_n].name), "%s", base_name);
        g_levels[out_n].style_hint = style;
        g_levels[out_n].level = lvl;
        ++out_n;
    }
    g_level_count = out_n;
    return out_n > 0;
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
    if (!discover_levels_from_dir(chosen_dir)) {
        fprintf(stderr, "FATAL: no levels discovered in %s\n", chosen_dir);
        exit(1);
    }
    g_leveldef_ready = 1;
}

static float gameplay_ui_scale(const game_state* g);
static void apply_player_hit(game_state* g, float impact_x, float impact_y, float impact_vx, float impact_vy);
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

static float dist_sq_level(const game_state* g, float ax, float ay, float bx, float by) {
    if (!g || !level_uses_cylinder(g)) {
        return dist_sq(ax, ay, bx, by);
    }
    const float dx = wrap_delta(ax, bx, cylinder_period(g));
    const float dy = ay - by;
    return dx * dx + dy * dy;
}

static float enemy_progression01(const game_state* g) {
    const leveldef_combat_tuning* c;
    if (!g) {
        return 0.0f;
    }
    ensure_leveldef_loaded();
    c = &g_leveldef.combat;
    float progression = (float)g->wave_index * c->progression_wave_weight +
                        (float)g->score * c->progression_score_weight;
    progression += (float)g->level_style * c->progression_level_weight;
    return clampf(progression, 0.0f, 1.0f);
}

static enemy_fire_tuning enemy_fire_tuning_for(const game_state* g) {
    const leveldef_combat_tuning* c;
    ensure_leveldef_loaded();
    c = &g_leveldef.combat;
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
    const float progression = enemy_progression01(g);
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

static void emit_enemy_debris(game_state* g, const enemy* e, float impact_vx, float impact_vy) {
    const float su = gameplay_ui_scale(g);
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
            d->b.vx = e->b.vx * 0.18f + impact_vx * (0.10f + 0.08f * frand01()) + frands1() * (46.0f * su);
            d->b.vy = e->b.vy * 0.10f + impact_vy * 0.08f + frands1() * (34.0f * su) + (22.0f * su);
            d->b.ax = -d->b.vx * 0.16f;
            d->b.ay = -260.0f * su;
            d->age_s = 0.0f;
            d->life_s = 2.2f + frand01() * 1.0f;
            d->alpha = 1.0f;
            break;
        }
    }
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
    const leveldef_combat_tuning* combat;
    ensure_leveldef_loaded();
    combat = &g_leveldef.combat;
    int arch = e->archetype;
    if (arch < 0 || arch > 2) {
        arch = 0;
    }
    if (e->archetype == ENEMY_ARCH_SWARM) {
        const float progression = enemy_progression01(g);
        const float armed_p = clampf(
            lerpf(combat->swarm_armed_prob_start, combat->swarm_armed_prob_end, progression),
            0.0f, 1.0f
        );
        const float spread_p = clampf(
            lerpf(combat->swarm_spread_prob_start, combat->swarm_spread_prob_end, progression),
            0.0f, 1.0f
        );
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
        enemy_reset_fire_cooldown(g, e, &w, &t);
    }
}

static void enemy_fire_projectiles(game_state* g, const enemy* e, const enemy_weapon_def* w, const enemy_fire_tuning* t) {
    const float aim_lead = w->aim_lead_s;
    const float tx = g->player.b.x + g->player.b.vx * aim_lead;
    const float ty = g->player.b.y + g->player.b.vy * aim_lead;
    float dx = level_uses_cylinder(g) ? wrap_delta(tx, e->b.x, cylinder_period(g)) : (tx - e->b.x);
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
    enemy_weapon_def w_local;
    const leveldef_combat_tuning* combat;
    ensure_leveldef_loaded();
    combat = &g_leveldef.combat;
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
    const enemy_weapon_def* w = &w_local;
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

    const float dx = level_uses_cylinder(g) ? wrap_delta(g->player.b.x, e->b.x, cylinder_period(g)) : (g->player.b.x - e->b.x);
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
        e->lane_dir = -1.0f;
        return e;
    }
    return NULL;
}

static void announce_wave(game_state* g, const char* wave_name) {
    g->wave_announce_pending = 1;
    snprintf(g->wave_announce_text, sizeof(g->wave_announce_text), "inbound enemy wave %02d\n%s", g->wave_index + 1, wave_name);
}

static void spawn_wave_sine_snake(game_state* g, int wave_id) {
    const leveldef_level* lvl;
    const leveldef_wave_sine_tuning* w;
    const float su = gameplay_ui_scale(g);
    ensure_leveldef_loaded();
    lvl = current_leveldef(g);
    if (!lvl) {
        return;
    }
    w = &lvl->sine;
    if (w->count <= 0) {
        return;
    }
    const float spawn_side = level_uses_cylinder(g) ? ((frand01() < 0.5f) ? -1.0f : 1.0f) : 1.0f;
    const int count = w->count;
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
        e->b.x = g->camera_x + spawn_side * (g->world_w * w->start_x01 + (float)i * w->spacing_x * su);
        e->home_y = g->world_h * w->home_y01;
        e->b.y = e->home_y;
        e->lane_dir = -spawn_side;
        e->form_phase = (float)i * w->phase_step;
        e->form_amp = w->form_amp * su;
        e->form_freq = w->form_freq;
        e->break_delay_s = w->break_delay_base + w->break_delay_step * (float)i;
        e->max_speed = w->max_speed * su;
        e->accel = w->accel;
    }
}

static void spawn_wave_v_formation(game_state* g, int wave_id) {
    const leveldef_level* lvl;
    const leveldef_wave_v_tuning* w;
    const float su = gameplay_ui_scale(g);
    ensure_leveldef_loaded();
    lvl = current_leveldef(g);
    if (!lvl) {
        return;
    }
    w = &lvl->v;
    if (w->count <= 0) {
        return;
    }
    const float spawn_side = level_uses_cylinder(g) ? ((frand01() < 0.5f) ? -1.0f : 1.0f) : 1.0f;
    const int count = w->count;
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
        e->b.x = g->camera_x + spawn_side * (g->world_w * w->start_x01 + (float)(abs(off)) * w->spacing_x * su);
        e->home_y = g->world_h * w->home_y01 + (float)off * w->home_y_step * su;
        e->b.y = e->home_y;
        e->lane_dir = -spawn_side;
        e->form_phase = (float)i * w->phase_step;
        e->form_amp = w->form_amp * su;
        e->form_freq = w->form_freq;
        e->break_delay_s = w->break_delay_min + frand01() * w->break_delay_rand;
        e->max_speed = w->max_speed * su;
        e->accel = w->accel;
    }
}

static void spawn_wave_swarm_profile(game_state* g, int wave_id, int profile_id, float goal_dir) {
    const float su = gameplay_ui_scale(g);
    const leveldef_boid_profile* p;
    ensure_leveldef_loaded();
    p = leveldef_get_boid_profile(&g_leveldef, profile_id);
    if (!p) {
        return;
    }
    const int count = p->count;
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
        if (level_uses_cylinder(g)) {
            const float spawn_side = (goal_dir < 0.0f) ? 1.0f : -1.0f;
            e->b.x = g->camera_x + spawn_side * (g->world_w * p->spawn_x01 + frand01() * p->spawn_x_span * su);
        } else {
            e->b.x = g->camera_x + g->world_w * p->spawn_x01 + frand01() * p->spawn_x_span * su;
        }
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

static void spawn_wave_kamikaze(game_state* g, int wave_id) {
    const leveldef_level* lvl;
    const leveldef_wave_kamikaze_tuning* w;
    const float su = gameplay_ui_scale(g);
    ensure_leveldef_loaded();
    lvl = current_leveldef(g);
    if (!lvl) {
        return;
    }
    w = &lvl->kamikaze;
    if (w->count <= 0) {
        return;
    }
    const float spawn_side = level_uses_cylinder(g) ? ((frand01() < 0.5f) ? -1.0f : 1.0f) : 1.0f;
    const int count = w->count;
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
        e->b.x = g->camera_x + spawn_side * (g->world_w * w->start_x01 + (float)i * w->spacing_x * su);
        {
            const float margin = w->y_margin * su;
            e->b.y = margin + frand01() * fmaxf(g->world_h - 2.0f * margin, 1.0f);
        }
        e->max_speed = w->max_speed * su;
        e->accel = w->accel;
        {
            const float rmin = w->radius_min;
            const float rmax = w->radius_max;
            e->radius = (rmin + frand01() * fmaxf(rmax - rmin, 0.0f)) * su;
        }
    }
}

static void spawn_curated_enemy(game_state* g, int wave_id, const leveldef_curated_enemy* ce) {
    const leveldef_level* lvl;
    const float su = gameplay_ui_scale(g);
    int count;
    if (!g || !ce) {
        return;
    }
    ensure_leveldef_loaded();
    lvl = current_leveldef(g);
    if (!lvl) {
        return;
    }

    count = (int)lroundf(fmaxf(1.0f, ce->a));
    if (count > 24) {
        count = 24;
    }

    if (ce->kind == 5) {
        const int profile_id = lvl->default_boid_profile;
        const leveldef_boid_profile* p = leveldef_get_boid_profile(&g_leveldef, profile_id);
        if (!p) {
            return;
        }
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
        enemy* e = spawn_enemy_common(g);
        if (!e) {
            break;
        }
        e->wave_id = wave_id;
        e->slot_index = i;
        e->b.x = g->world_w * ce->x01 + (float)i * 18.0f * su;
        e->b.y = g->world_h * ce->y01 + frands1() * 10.0f * su;
        enemy_assign_combat_loadout(g, e);

        if (ce->kind == 4) {
            e->archetype = ENEMY_ARCH_KAMIKAZE;
            e->state = ENEMY_STATE_KAMIKAZE;
            e->max_speed = ((ce->b > 0.0f) ? ce->b : lvl->kamikaze.max_speed) * su;
            e->accel = (ce->c > 0.0f) ? ce->c : lvl->kamikaze.accel;
            e->radius = (lvl->kamikaze.radius_min + frand01() * fmaxf(lvl->kamikaze.radius_max - lvl->kamikaze.radius_min, 0.0f)) * su;
        } else {
            e->archetype = ENEMY_ARCH_FORMATION;
            e->state = ENEMY_STATE_FORMATION;
            e->home_y = g->world_h * ce->y01;
            e->b.y = e->home_y;
            e->form_phase = (float)i * 0.4f;
            e->form_amp = fmaxf(0.0f, ce->b) * su;
            e->form_freq = (ce->kind == 3) ? lvl->v.form_freq : lvl->sine.form_freq;
            e->break_delay_s = 0.8f + 0.14f * (float)i;
            e->max_speed = ((ce->c > 0.0f) ? ce->c : ((ce->kind == 3) ? lvl->v.max_speed : lvl->sine.max_speed)) * su;
            e->accel = (ce->kind == 3) ? lvl->v.accel : lvl->sine.accel;
        }
    }
}

static void spawn_next_wave(game_state* g) {
    const int wave_id = ++g->wave_id_alloc;
    const leveldef_level* lvl;
    ensure_leveldef_loaded();
    lvl = current_leveldef(g);
    if (!lvl) {
        return;
    }
    if (lvl && lvl->wave_mode == LEVELDEF_WAVES_BOID_ONLY) {
        int profile_id;
        if (lvl->boid_cycle_count <= 0) {
            return;
        }
        profile_id = lvl->boid_cycle[g->wave_index % lvl->boid_cycle_count];
        {
            const leveldef_boid_profile* p = leveldef_get_boid_profile(&g_leveldef, profile_id);
            if (!p) {
                return;
            }
            announce_wave(g, p->wave_name);
        }
        {
            const float dir = level_uses_cylinder(g) ? ((frand01() < 0.5f) ? -1.0f : 1.0f) : 1.0f;
            spawn_wave_swarm_profile(g, wave_id, profile_id, dir);
        }
        g->wave_index += 1;
        g->wave_cooldown_s = lvl->wave_cooldown_between_s;
        return;
    }
    if (lvl && lvl->wave_mode == LEVELDEF_WAVES_CURATED) {
        const leveldef_curated_enemy* ce;
        if (lvl->curated_count <= 0) {
            return;
        }
        ce = &lvl->curated[g->wave_index % lvl->curated_count];
        if (ce->kind == 5) {
            announce_wave(g, "curated boid contact");
        } else if (ce->kind == 4) {
            announce_wave(g, "curated kamikaze contact");
        } else if (ce->kind == 3) {
            announce_wave(g, "curated v wing");
        } else {
            announce_wave(g, "curated sine wing");
        }
        spawn_curated_enemy(g, wave_id, ce);
        g->wave_index += 1;
        g->wave_cooldown_s = lvl->wave_cooldown_between_s;
        return;
    }
    if (lvl->wave_cycle_count <= 0) {
        return;
    }
    int pattern = lvl->wave_cycle[g->wave_index % lvl->wave_cycle_count];
    if (pattern == LEVELDEF_WAVE_SINE_SNAKE) {
        announce_wave(g, "sine snake formation");
        spawn_wave_sine_snake(g, wave_id);
    } else if (pattern == LEVELDEF_WAVE_V_FORMATION) {
        announce_wave(g, "galaxian break v formation");
        spawn_wave_v_formation(g, wave_id);
    } else if (pattern == LEVELDEF_WAVE_SWARM) {
        const int profile_id = lvl->default_boid_profile;
        if (profile_id < 0) {
            return;
        }
        announce_wave(g, "boid swarm cluster");
        {
            const int is_cylinder = level_uses_cylinder(g);
            const float dir = is_cylinder ? ((frand01() < 0.5f) ? -1.0f : 1.0f) : 1.0f;
            spawn_wave_swarm_profile(g, wave_id, profile_id, dir);
            if (is_cylinder && g->wave_index >= 4 && frand01() < 0.38f) {
                const int wave_id_2 = ++g->wave_id_alloc;
                spawn_wave_swarm_profile(g, wave_id_2, profile_id, -dir);
            }
        }
    } else {
        announce_wave(g, "kamikaze crash wing");
        spawn_wave_kamikaze(g, wave_id);
    }
    g->wave_index += 1;
    g->wave_cooldown_s = lvl->wave_cooldown_between_s;
}

static void update_enemy_formation(game_state* g, enemy* e, float dt) {
    e->ai_timer_s += dt;
    if (e->state == ENEMY_STATE_FORMATION) {
        const float su = gameplay_ui_scale(g);
        const float desired_y = e->home_y + sinf(g->t * e->form_freq + e->form_phase) * e->form_amp;
        const float lane_dir = (e->lane_dir < 0.0f) ? -1.0f : 1.0f;
        const float target_vx = lane_dir * 165.0f * su;
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
        float dir_x = level_uses_cylinder(g) ? wrap_delta(tx, e->b.x, cylinder_period(g)) : (tx - e->b.x);
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
    float dir_x = level_uses_cylinder(g) ? wrap_delta(tx, e->b.x, cylinder_period(g)) : (tx - e->b.x);
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
        const float dx = level_uses_cylinder(g) ? wrap_delta(o->b.x, e->b.x, cylinder_period(g)) : (o->b.x - e->b.x);
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
        /* Use a short prediction lead so swarms react before collisions happen. */
        const float lead = 0.22f;
        const float px = g->player.b.x + g->player.b.vx * lead;
        const float py = g->player.b.y + g->player.b.vy * lead;
        float dx = level_uses_cylinder(g) ? wrap_delta(e->b.x, px, cylinder_period(g)) : (e->b.x - px);
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
                /* Extra hard push near the player to avoid collisions aggressively. */
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
            float dx = level_uses_cylinder(g) ?
                wrap_delta(e->b.x, sl->origin_x, cylinder_period(g)) :
                (e->b.x - sl->origin_x);
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
            /* Repel from the source entity itself (not the beam). */
            const float falloff = 1.0f - (d2 / avoid_r2);
            avoid_x += (dx / d2) * falloff;
            avoid_y += (dy / d2) * falloff;
        }
    }

    const float goal_dir = (e->swarm_goal_dir < 0.0f) ? -1.0f : 1.0f;
    float goal_x = (g->player.b.x + goal_dir * 280.0f * su) - e->b.x;
    if (level_uses_cylinder(g)) {
        goal_x = wrap_delta(g->player.b.x + goal_dir * 280.0f * su, e->b.x, cylinder_period(g));
    }
    const float goal_amp = (e->swarm_goal_amp > 1.0f) ? e->swarm_goal_amp : (80.0f * su);
    const float goal_freq = (e->swarm_goal_freq > 0.01f) ? e->swarm_goal_freq : 0.70f;
    float goal_y = (g->player.b.y + sinf(g->t * goal_freq + (float)e->slot_index * 0.35f) * goal_amp) - e->b.y;
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
    /* Flock-level "breathing": periodically loosen/tighten spacing for more organic motion. */
    const float phase = (float)(e->wave_id & 31) * 0.61f;
    const float breathe = 0.5f + 0.5f * sinf(g->t * 0.85f + phase);
    const float tightness = 0.80f + 0.40f * breathe; /* 0.8..1.2 */
    sep_w *= (1.20f - 0.28f * tightness); /* tighter -> less separation */
    ali_w *= (0.90f + 0.25f * tightness); /* tighter -> stronger alignment */
    coh_w *= tightness;                    /* tighter -> stronger cohesion */
    goal_w *= (0.92f + 0.18f * tightness);
    avoid_w *= (1.0f + 2.4f * player_avoid_boost);
    goal_w *= (1.0f - 0.45f * player_avoid_boost);
    {
        const float wp = g->t * wander_freq +
                         (float)e->slot_index * 0.73f +
                         (float)(e->wave_id & 31) * 0.29f;
        wander_x = cosf(wp) + 0.35f * sinf(wp * 0.57f + 1.3f);
        wander_y = sinf(wp * 1.11f + 0.8f) + 0.28f * cosf(wp * 0.49f + 0.4f);
    }

    normalize2(&sep_x, &sep_y);
    normalize2(&ali_x, &ali_y);
    normalize2(&coh_x, &coh_y);
    normalize2(&avoid_x, &avoid_y);
    normalize2(&goal_x, &goal_y);
    normalize2(&wander_x, &wander_y);

    const float fx =
        sep_x * sep_w +
        ali_x * ali_w +
        coh_x * coh_w +
        avoid_x * avoid_w +
        goal_x * goal_w +
        wander_x * wander_w;
    const float fy =
        sep_y * sep_w +
        ali_y * ali_w +
        coh_y * coh_w +
        avoid_y * avoid_w +
        goal_y * goal_w +
        wander_y * wander_w;

    e->b.ax = fx * (e->accel * 135.0f) - e->b.vx * steer_drag;
    e->b.ay = fy * (e->accel * 135.0f) - e->b.vy * steer_drag;
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

void game_update(game_state* g, float dt, const game_input* in) {
    g->t += dt;
    const float su = gameplay_ui_scale(g);

    if (in->restart && g->lives <= 0) {
        const int restart_level_index = g->level_index;
        game_init(g, g->world_w, g->world_h);
        if (!set_level_index(g, restart_level_index)) {
            apply_level_runtime_config(g);
        }
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
        if (g->exit_portal_active &&
            dist_sq(g->player.b.x, g->player.b.y, g->exit_portal_x, g->exit_portal_y) <=
                g->exit_portal_radius * g->exit_portal_radius) {
            game_cycle_level(g);
            return;
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

    if (g->lives > 0) {
        const leveldef_level* lvl = current_leveldef(g);
        if (lvl) {
            if (lvl->wave_mode == LEVELDEF_WAVES_CURATED &&
                lvl->render_style == LEVEL_RENDER_DEFENDER) {
                const float activate_x = g->camera_x + g->world_w * 1.18f;
                for (int i = 0; i < lvl->curated_count && i < MAX_CURATED_RUNTIME; ++i) {
                    const leveldef_curated_enemy* ce = &lvl->curated[i];
                    const float world_x = ce->x01 * g->world_w;
                    if (g->curated_spawned[i]) {
                        continue;
                    }
                    if (world_x <= activate_x) {
                        const int wave_id = ++g->wave_id_alloc;
                        spawn_curated_enemy(g, wave_id, ce);
                        g->curated_spawned[i] = 1u;
                        g->curated_spawned_count += 1;
                    }
                }
            } else {
                g->wave_cooldown_s -= dt;
                if (lvl->spawn_mode == LEVELDEF_SPAWN_SEQUENCED_CLEAR) {
                    if (game_enemy_count(g) <= 0 && g->wave_cooldown_s <= 0.0f) {
                        spawn_next_wave(g);
                    }
                } else if (lvl->spawn_mode == LEVELDEF_SPAWN_TIMED) {
                    if (g->wave_cooldown_s <= 0.0f) {
                        spawn_next_wave(g);
                        g->wave_cooldown_s = lvl->spawn_interval_s;
                    }
                } else if (lvl->spawn_mode == LEVELDEF_SPAWN_TIMED_SEQUENCED) {
                    if (g->wave_cooldown_s <= 0.0f && game_enemy_count(g) <= 0) {
                        spawn_next_wave(g);
                        g->wave_cooldown_s = lvl->spawn_interval_s;
                    }
                }
            }
        }
    }
    int player_hit_this_frame = 0;
    if (g->searchlight_count > 0) {
        update_searchlights(g, dt);
    }

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

        if (!level_uses_cylinder(g) && e->b.x < g->camera_x - g->world_w * 0.72f) {
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
                emit_enemy_debris(g, e, g->player.b.vx, g->player.b.vy);
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
        if (level_uses_cylinder(g)) {
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
                emit_enemy_debris(g, &g->enemies[ei], g->bullets[bi].b.vx, g->bullets[bi].b.vy);
                g->enemies[ei].active = 0;
                emit_explosion(g, g->enemies[ei].b.x, g->enemies[ei].b.y, g->enemies[ei].b.vx, g->enemies[ei].b.vy, 26);
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
        if (!level_uses_cylinder(g) && fabsf(d->b.x - g->camera_x) > g->world_w * 1.4f) {
            d->active = 0;
            continue;
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
