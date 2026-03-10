#include "game.h"
#include "boss.h"
#include "enemy.h"
#include "leveldef.h"
#include "death_teletype_messages.h"

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

static float smoothstepf(float edge0, float edge1, float x) {
    const float t = clampf((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
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
    if (!g || g_level_count <= 0) {
        return NULL;
    }
    if (g->level_index >= 0 && g->level_index < g_level_count) {
        return &g_levels[g->level_index].level;
    }
    fprintf(stderr, "game: invalid level_index=%d (count=%d); falling back to first discovered level\n", g->level_index, g_level_count);
    return &g_levels[0].level;
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
static int level_uses_cylinder(const game_state* g);
static float cylinder_period(const game_state* g);
static float wrap_delta(float a, float b, float period);
static float length2(float x, float y);
static void normalize2(float* x, float* y);
static float wrap_angle_rad(float a);
static void steer_to_velocity(body* b, float target_vx, float target_vy, float accel, float damping);
static void integrate_body(body* b, float dt);
static particle* alloc_particle(game_state* g);
static void explode_mine(game_state* g, mine* m, float impact_vx, float impact_vy);
static void emit_player_asteroid_explosion(game_state* g);
static void trigger_emp_visual_at(game_state* g, float x, float y, float radius);
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

static void configure_minefields_for_level(game_state* g);
static void update_minefields(game_state* g, float dt);
static void minefield_apply_emp(game_state* g, float radius);
static void configure_missile_launchers_for_level(game_state* g);
static void update_missile_system(game_state* g, float dt);
static void configure_arc_nodes_for_level(game_state* g);
static void update_arc_nodes(game_state* g, float dt);
static void game_reset_powerup_state(game_state* g, int clear_pickups);
static void game_set_player_dead(game_state* g, int queue_message);
static void game_update_powerups(game_state* g, float dt);
static void game_try_spawn_powerup_drop(game_state* g, float x, float y, float vx, float vy);

static void game_queue_death_message(game_state* g) {
    if (!g || g->lives > 0) {
        return;
    }
    const size_t n = death_teletype_message_count();
    if (n <= 0) {
        return;
    }
    const size_t idx = (size_t)(rand() % (int)n);
    const char* msg = death_teletype_message_at(idx);
    if (!msg || !msg[0]) {
        return;
    }
    g->wave_announce_pending = 1;
    snprintf(g->wave_announce_text, sizeof(g->wave_announce_text), "%s", msg);
}

static void game_reset_powerup_state(game_state* g, int clear_pickups) {
    if (!g) {
        return;
    }
    g->weapon_level = 1;
    g->powerup_magnet_active = 0;
    g->powerup_drop_credit = 0.0f;
    if (!clear_pickups) {
        return;
    }
    memset(g->powerups, 0, sizeof(g->powerups));
    g->powerup_count = 0;
}

static powerup_pickup* alloc_powerup_pickup(game_state* g) {
    if (!g) {
        return NULL;
    }
    for (int i = 0; i < MAX_POWERUPS; ++i) {
        if (g->powerups[i].active) {
            continue;
        }
        powerup_pickup* p = &g->powerups[i];
        memset(p, 0, sizeof(*p));
        p->active = 1;
        g->powerup_count += 1;
        return p;
    }
    return NULL;
}

static void kill_powerup_pickup(game_state* g, powerup_pickup* p) {
    if (!g || !p || !p->active) {
        return;
    }
    p->active = 0;
    if (g->powerup_count > 0) {
        g->powerup_count -= 1;
    }
}

static int powerup_pick_drop_type(const game_state* g) {
    const float r = frand01();
    if (level_uses_cylinder(g)) {
        if (r < 0.27f) return POWERUP_DOUBLE_SHOT;
        if (r < 0.48f) return POWERUP_TRIPLE_SHOT;
        if (r < 0.68f) return POWERUP_VITALITY;
        if (r < 0.84f) return POWERUP_ORBITAL_BOOST;
        return POWERUP_MAGNET;
    }
    if (r < 0.34f) return POWERUP_DOUBLE_SHOT;
    if (r < 0.56f) return POWERUP_TRIPLE_SHOT;
    if (r < 0.80f) return POWERUP_VITALITY;
    return POWERUP_MAGNET;
}

static void game_apply_powerup(game_state* g, int type) {
    if (!g || g->lives <= 0) {
        return;
    }
    switch (type) {
        case POWERUP_DOUBLE_SHOT:
            if (g->weapon_level < 2) {
                g->weapon_level = 2;
            }
            break;
        case POWERUP_TRIPLE_SHOT:
            g->weapon_level = 3;
            break;
        case POWERUP_VITALITY:
            g->lives = clampi(g->lives + 1, 0, 3);
            break;
        case POWERUP_ORBITAL_BOOST:
            if (level_uses_cylinder(g)) {
                g->level_time_remaining_s = fmaxf(g->level_time_remaining_s, 0.0f) + 15.0f;
            }
            break;
        case POWERUP_MAGNET:
            g->powerup_magnet_active = 1;
            break;
        default:
            break;
    }
}

static void game_try_spawn_powerup_drop(game_state* g, float x, float y, float vx, float vy) {
    const leveldef_level* lvl;
    powerup_pickup* p;
    const float su = gameplay_ui_scale(g);
    float drop_p;
    if (!g || g->lives <= 0) {
        return;
    }
    lvl = current_leveldef(g);
    if (!lvl) {
        return;
    }
    drop_p = clampf(lvl->powerup_drop_chance, 0.0f, 1.0f);
    if (drop_p <= 0.0f) {
        g->powerup_drop_credit = 0.0f;
        return;
    }
    /* Credit carry avoids long droughts/clumps while honoring level drop chance long-term. */
    g->powerup_drop_credit = fminf(g->powerup_drop_credit + drop_p, 2.0f);
    if (g->powerup_drop_credit < 1.0f && frand01() > g->powerup_drop_credit) {
        return;
    }
    p = alloc_powerup_pickup(g);
    if (!p) {
        return;
    }
    g->powerup_drop_credit = fmaxf(g->powerup_drop_credit - 1.0f, 0.0f);
    p->type = powerup_pick_drop_type(g);
    p->b.x = x;
    p->b.y = y;
    p->b.vx = vx * 0.08f + frands1() * 42.0f * su;
    p->b.vy = vy * 0.08f + frands1() * 36.0f * su;
    p->b.ax = 0.0f;
    p->b.ay = 0.0f;
    p->ttl_s = 13.0f;
    p->radius = 14.0f * su;
    p->spin = frand01() * 6.2831853f;
    p->spin_rate = frands1() * 2.5f;
    if (fabsf(p->spin_rate) < 0.8f) {
        p->spin_rate = (p->spin_rate < 0.0f) ? -0.8f : 0.8f;
    }
    p->bob_phase = frand01() * 6.2831853f;
}

void game_on_enemy_destroyed(game_state* g, float x, float y, float vx, float vy, int score_delta) {
    if (!g) {
        return;
    }
    g->kills += 1;
    g->score += score_delta;
    game_try_spawn_powerup_drop(g, x, y, vx, vy);
}

static void game_set_player_dead(game_state* g, int queue_message) {
    const int was_alive = (g && g->lives > 0) ? 1 : 0;
    if (!g || !was_alive) {
        return;
    }
    g->lives = 0;
    game_reset_powerup_state(g, 0);
    if (queue_message) {
        game_queue_death_message(g);
    }
}

void game_on_player_life_lost(game_state* g) {
    if (!g || g->lives <= 0) {
        return;
    }
    g->lives -= 1;
    if (g->lives < 0) {
        g->lives = 0;
    }
    game_reset_powerup_state(g, 0);
    if (g->lives == 0) {
        game_queue_death_message(g);
    }
}

static float dist_sq(float ax, float ay, float bx, float by) {
    const float dx = ax - bx;
    const float dy = ay - by;
    return dx * dx + dy * dy;
}

static float point_segment_dist_sq(
    float px,
    float py,
    float ax,
    float ay,
    float bx,
    float by,
    float* out_cx,
    float* out_cy
) {
    const float abx = bx - ax;
    const float aby = by - ay;
    const float apx = px - ax;
    const float apy = py - ay;
    const float denom = abx * abx + aby * aby;
    float t = 0.0f;
    float cx = ax;
    float cy = ay;
    if (denom > 1.0e-6f) {
        t = (apx * abx + apy * aby) / denom;
        t = clampf(t, 0.0f, 1.0f);
        cx = ax + abx * t;
        cy = ay + aby * t;
    }
    if (out_cx) {
        *out_cx = cx;
    }
    if (out_cy) {
        *out_cy = cy;
    }
    return dist_sq(px, py, cx, cy);
}

static void structure_prefab_dims_world(int prefab_id, int* out_w, int* out_h) {
    int w = 1;
    int h = 1;
    if (prefab_id == 4) {
        h = 3;
    }
    if (out_w) {
        *out_w = w;
    }
    if (out_h) {
        *out_h = h;
    }
}

static void structure_aabb_world(
    const game_state* g,
    const leveldef_structure_instance* st,
    float* out_min_x,
    float* out_min_y,
    float* out_max_x,
    float* out_max_y
) {
    int w_units = 1;
    int h_units = 1;
    int q;
    float unit_w;
    float unit_h;
    float bx;
    float by;
    float bw;
    float bh;

    if (!g || !st || !out_min_x || !out_min_y || !out_max_x || !out_max_y) {
        return;
    }
    w_units = (st->w_units > 0) ? st->w_units : 1;
    h_units = (st->h_units > 0) ? st->h_units : 1;
    unit_w = g->world_w * (float)LEVELDEF_STRUCTURE_GRID_SCALE / (float)(LEVELDEF_STRUCTURE_GRID_W - 1);
    unit_h = g->world_h / (float)((LEVELDEF_STRUCTURE_GRID_H - 1) / LEVELDEF_STRUCTURE_GRID_SCALE);
    bx = (float)st->grid_x * unit_w;
    by = (float)st->grid_y * unit_h;
    bw = unit_w * (float)w_units;
    bh = unit_h * (float)h_units;
    q = ((st->rotation_quadrants % 4) + 4) % 4;
    if ((q & 1) != 0) {
        const float tmp = bw;
        bw = bh;
        bh = tmp;
    }
    *out_min_x = bx;
    *out_min_y = by;
    *out_max_x = bx + bw;
    *out_max_y = by + bh;
}

static int segment_intersects_aabb(
    float x0,
    float y0,
    float x1,
    float y1,
    float min_x,
    float min_y,
    float max_x,
    float max_y
) {
    const float dx = x1 - x0;
    const float dy = y1 - y0;
    float tmin = 0.0f;
    float tmax = 1.0f;

    if (fabsf(dx) < 1.0e-6f) {
        if (x0 < min_x || x0 > max_x) {
            return 0;
        }
    } else {
        const float inv_dx = 1.0f / dx;
        float tx1 = (min_x - x0) * inv_dx;
        float tx2 = (max_x - x0) * inv_dx;
        if (tx1 > tx2) {
            const float tmp = tx1;
            tx1 = tx2;
            tx2 = tmp;
        }
        tmin = fmaxf(tmin, tx1);
        tmax = fminf(tmax, tx2);
        if (tmin > tmax) {
            return 0;
        }
    }

    if (fabsf(dy) < 1.0e-6f) {
        if (y0 < min_y || y0 > max_y) {
            return 0;
        }
    } else {
        const float inv_dy = 1.0f / dy;
        float ty1 = (min_y - y0) * inv_dy;
        float ty2 = (max_y - y0) * inv_dy;
        if (ty1 > ty2) {
            const float tmp = ty1;
            ty1 = ty2;
            ty2 = tmp;
        }
        tmin = fmaxf(tmin, ty1);
        tmax = fminf(tmax, ty2);
        if (tmin > tmax) {
            return 0;
        }
    }
    return 1;
}

int game_structure_circle_overlap(const game_state* g, float x, float y, float radius) {
    const leveldef_level* lvl;
    if (!g) {
        return 0;
    }
    if (g->render_style == LEVEL_RENDER_CYLINDER) {
        return 0;
    }
    lvl = current_leveldef(g);
    if (!lvl || lvl->structure_count <= 0) {
        return 0;
    }
    for (int i = 0; i < lvl->structure_count && i < LEVELDEF_MAX_STRUCTURES; ++i) {
        const leveldef_structure_instance* st = &lvl->structures[i];
        float min_x;
        float min_y;
        float max_x;
        float max_y;
        float nx;
        float ny;
        float dx;
        float dy;
        if (st->layer != 0) {
            continue;
        }
        structure_aabb_world(g, st, &min_x, &min_y, &max_x, &max_y);
        nx = fmaxf(min_x, fminf(x, max_x));
        ny = fmaxf(min_y, fminf(y, max_y));
        dx = x - nx;
        dy = y - ny;
        if (dx * dx + dy * dy <= radius * radius) {
            return 1;
        }
    }
    return 0;
}

int game_find_noncolliding_spawn(
    const game_state* g,
    float* io_x,
    float* io_y,
    float radius,
    float search_step,
    float max_search_radius
) {
    const float pi = 3.14159265359f;
    if (!g || !io_x || !io_y) {
        return 0;
    }
    if (!game_structure_circle_overlap(g, *io_x, *io_y, radius)) {
        return 1;
    }
    if (search_step <= 0.0f) {
        search_step = fmaxf(4.0f, radius * 0.5f);
    }
    if (max_search_radius <= search_step) {
        max_search_radius = search_step * 6.0f;
    }
    for (float r = search_step; r <= max_search_radius; r += search_step) {
        const int samples = 24;
        for (int i = 0; i < samples; ++i) {
            const float a = (2.0f * pi * (float)i) / (float)samples;
            float tx = *io_x + cosf(a) * r;
            float ty = *io_y + sinf(a) * r;
            if (ty < 0.0f) ty = 0.0f;
            if (ty > g->world_h) ty = g->world_h;
            if (!game_structure_circle_overlap(g, tx, ty, radius)) {
                *io_x = tx;
                *io_y = ty;
                return 1;
            }
        }
    }
    return 0;
}

void game_structure_avoidance_vector(
    const game_state* g,
    float x,
    float y,
    float probe_radius,
    float probe_distance,
    float* out_x,
    float* out_y
) {
    float ax = 0.0f;
    float ay = 0.0f;
    const float dirs[8][2] = {
        { 1.0f,  0.0f},
        {-1.0f,  0.0f},
        { 0.0f,  1.0f},
        { 0.0f, -1.0f},
        { 0.70710677f,  0.70710677f},
        {-0.70710677f,  0.70710677f},
        { 0.70710677f, -0.70710677f},
        {-0.70710677f, -0.70710677f}
    };
    if (out_x) {
        *out_x = 0.0f;
    }
    if (out_y) {
        *out_y = 0.0f;
    }
    if (!g || probe_radius <= 0.0f || probe_distance <= 0.0f) {
        return;
    }
    if (!game_structure_circle_overlap(g, x, y, probe_radius + probe_distance)) {
        return;
    }
    for (int i = 0; i < 8; ++i) {
        const float sx = x + dirs[i][0] * probe_distance;
        const float sy = y + dirs[i][1] * probe_distance;
        if (game_structure_circle_overlap(g, sx, sy, probe_radius)) {
            ax -= dirs[i][0];
            ay -= dirs[i][1];
        }
    }
    if (out_x) {
        *out_x = ax;
    }
    if (out_y) {
        *out_y = ay;
    }
}

int game_line_of_sight_clear(const game_state* g, float x0, float y0, float x1, float y1, float radius) {
    const leveldef_level* lvl;
    if (!g) {
        return 1;
    }
    if (g->render_style == LEVEL_RENDER_CYLINDER) {
        return 1;
    }
    lvl = current_leveldef(g);
    if (!lvl || lvl->structure_count <= 0) {
        return 1;
    }
    if (radius < 0.0f) {
        radius = 0.0f;
    }
    for (int i = 0; i < lvl->structure_count && i < LEVELDEF_MAX_STRUCTURES; ++i) {
        const leveldef_structure_instance* st = &lvl->structures[i];
        float min_x;
        float min_y;
        float max_x;
        float max_y;
        if (st->layer != 0) {
            continue;
        }
        structure_aabb_world(g, st, &min_x, &min_y, &max_x, &max_y);
        min_x -= radius;
        min_y -= radius;
        max_x += radius;
        max_y += radius;
        if (segment_intersects_aabb(x0, y0, x1, y1, min_x, min_y, max_x, max_y)) {
            return 0;
        }
    }
    return 1;
}

int game_structure_segment_blocked(const game_state* g, float x0, float y0, float x1, float y1, float pad_radius) {
    const leveldef_level* lvl;
    float unit_w;
    float unit_h;
    float min_x;
    float min_y;
    float max_x;
    float max_y;
    int qmin_x;
    int qmin_y;
    int qmax_x;
    int qmax_y;
    if (!g) {
        return 0;
    }
    if (g->render_style == LEVEL_RENDER_CYLINDER) {
        return 0;
    }
    lvl = current_leveldef(g);
    if (!lvl || lvl->structure_count <= 0) {
        return 0;
    }
    if (pad_radius < 0.0f) {
        pad_radius = 0.0f;
    }
    unit_w = g->world_w * (float)LEVELDEF_STRUCTURE_GRID_SCALE / (float)(LEVELDEF_STRUCTURE_GRID_W - 1);
    unit_h = g->world_h / (float)((LEVELDEF_STRUCTURE_GRID_H - 1) / LEVELDEF_STRUCTURE_GRID_SCALE);
    min_x = fminf(x0, x1) - pad_radius;
    min_y = fminf(y0, y1) - pad_radius;
    max_x = fmaxf(x0, x1) + pad_radius;
    max_y = fmaxf(y0, y1) + pad_radius;
    qmin_x = clampi((int)floorf(min_x / fmaxf(unit_w, 1.0e-5f)), 0, LEVELDEF_STRUCTURE_GRID_W - 1);
    qmin_y = clampi((int)floorf(min_y / fmaxf(unit_h, 1.0e-5f)), 0, LEVELDEF_STRUCTURE_GRID_H - 1);
    qmax_x = clampi((int)ceilf(max_x / fmaxf(unit_w, 1.0e-5f)), 0, LEVELDEF_STRUCTURE_GRID_W - 1);
    qmax_y = clampi((int)ceilf(max_y / fmaxf(unit_h, 1.0e-5f)), 0, LEVELDEF_STRUCTURE_GRID_H - 1);

    for (int i = 0; i < lvl->structure_count && i < LEVELDEF_MAX_STRUCTURES; ++i) {
        const leveldef_structure_instance* st = &lvl->structures[i];
        int w_units = 1;
        int h_units = 1;
        int q;
        int gx0, gy0, gx1, gy1;
        if (st->layer != 0) {
            continue;
        }
        w_units = (st->w_units > 0) ? st->w_units : 1;
        h_units = (st->h_units > 0) ? st->h_units : 1;
        q = ((st->rotation_quadrants % 4) + 4) % 4;
        if ((q & 1) != 0) {
            const int tmp = w_units;
            w_units = h_units;
            h_units = tmp;
        }
        gx0 = st->grid_x;
        gy0 = st->grid_y;
        gx1 = gx0 + w_units;
        gy1 = gy0 + h_units;
        if (gx1 < qmin_x || gx0 > qmax_x || gy1 < qmin_y || gy0 > qmax_y) {
            continue;
        }
        structure_aabb_world(g, st, &min_x, &min_y, &max_x, &max_y);
        if (segment_intersects_aabb(
                x0,
                y0,
                x1,
                y1,
                min_x - pad_radius,
                min_y - pad_radius,
                max_x + pad_radius,
                max_y + pad_radius)) {
            return 1;
        }
    }
    return 0;
}

typedef struct mine_tuning {
    float activation_distance;
    float detonation_distance;
    float size;
    float drift_speed;
    float drift_accel;
    float max_speed;
    float push_impulse;
    float push_accel;
    float push_duration_s;
    float emp_fx_radius;
} mine_tuning;

static mine_tuning mine_tuning_for(const game_state* g) {
    const float screen_ref = fminf(g->world_w, g->world_h);
    const float su = gameplay_ui_scale(g);
    mine_tuning t = {
        .activation_distance = screen_ref * 0.50f,
        .detonation_distance = screen_ref * 0.052f,
        .size = 16.0f * su,
        .drift_speed = 180.0f * su,
        .drift_accel = 7.2f,
        .max_speed = 260.0f * su,
        .push_impulse = 980.0f * su,
        .push_accel = 4200.0f * su,
        .push_duration_s = 0.42f,
        .emp_fx_radius = screen_ref * 0.09f
    };
    return t;
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
        float sweep_center_deg;
        memset(sl, 0, sizeof(*sl));
        sl->active = 1;
        sl->origin_x = g->world_w * d->anchor_x01;
        sl->origin_y = g->world_h * d->anchor_y01;
        sl->length = g->world_h * d->length_h01;
        sl->half_angle_rad = deg_to_rad(d->half_angle_deg);
        sweep_center_deg = d->sweep_center_deg;
        if (fabsf(sweep_center_deg) < 1.0e-3f) {
            /* Legacy/editor-authored searchlights defaulted center to 0deg,
               which produced vertical sweeps for 180deg span. */
            sweep_center_deg = 90.0f;
        }
        sl->sweep_center_rad = deg_to_rad(sweep_center_deg);
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
        (void)game_find_noncolliding_spawn(
            g,
            &sl->origin_x,
            &sl->origin_y,
            fmaxf(sl->source_radius, 10.0f),
            fmaxf(8.0f, sl->source_radius * 0.45f),
            g->world_h * 0.35f
        );
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

static float asteroid_storm_speed_px(const game_state* g, float su) {
    if (!g) {
        return 0.0f;
    }
    return fmaxf(g->asteroid_storm_speed, 0.0f) * su;
}

static int asteroid_target_count(const game_state* g);

static void asteroid_storm_velocity_local(const game_state* g, float* out_vx, float* out_vy) {
    if (out_vx) *out_vx = 0.0f;
    if (out_vy) *out_vy = 0.0f;
    if (!g) {
        return;
    }
    /* Angle is the tilt from vertical-down. Positive means drift left (from top-right). */
    const float ang = g->asteroid_storm_angle_rad;
    float dx = -sinf(ang);
    float dy = -cosf(ang);
    normalize2(&dx, &dy);
    const float su = gameplay_ui_scale(g);
    const float speed = asteroid_storm_speed_px(g, su);
    if (out_vx) *out_vx = dx * speed;
    if (out_vy) *out_vy = dy * speed;
}

static int asteroid_overlap_candidate_world(const game_state* g, const asteroid_body* skip, float x, float y, float r) {
    if (!g) {
        return 0;
    }
    for (int i = 0; i < g->asteroid_count && i < MAX_ASTEROIDS; ++i) {
        const asteroid_body* o = &g->asteroids[i];
        if (!o->active || o == skip) {
            continue;
        }
        const float rr = r + o->radius + 6.0f;
        if (dist_sq(x, y, o->b.x, o->b.y) < rr * rr) {
            return 1;
        }
    }
    return 0;
}

static void asteroid_clear_bodies(game_state* g) {
    if (!g) {
        return;
    }
    for (int i = 0; i < MAX_ASTEROIDS; ++i) {
        g->asteroids[i].active = 0;
    }
}

static int asteroid_active_count(const game_state* g) {
    int n = 0;
    if (!g) {
        return 0;
    }
    for (int i = 0; i < g->asteroid_count && i < MAX_ASTEROIDS; ++i) {
        if (g->asteroids[i].active) {
            ++n;
        }
    }
    return n;
}

static asteroid_body* asteroid_find_inactive_slot(game_state* g) {
    if (!g) {
        return NULL;
    }
    for (int i = 0; i < g->asteroid_count && i < MAX_ASTEROIDS; ++i) {
        if (!g->asteroids[i].active) {
            return &g->asteroids[i];
        }
    }
    return NULL;
}

static void asteroid_storm_reset_emitters(game_state* g) {
    if (!g) {
        return;
    }
    const float su = gameplay_ui_scale(g);
    const float pad = 72.0f * su;
    float vx = 0.0f, vy = 0.0f;
    asteroid_storm_velocity_local(g, &vx, &vy);
    const float vy_abs = fmaxf(fabsf(vy), 1.0f);
    const float lifetime_s = (g->world_h + 2.0f * pad) / vy_abs;
    const float target_visible = (float)asteroid_target_count(g);
    const float spawn_rate_total = target_visible / fmaxf(lifetime_s, 0.25f);
    const float spawn_rate_per = spawn_rate_total / (float)ASTEROID_EMITTERS;
    const float base_interval = 1.0f / fmaxf(spawn_rate_per, 0.001f);

    g->asteroid_storm_emitter_cursor = (int)(frand01() * (float)ASTEROID_EMITTERS) % ASTEROID_EMITTERS;
    for (int i = 0; i < ASTEROID_EMITTERS; ++i) {
        /* Randomize initial phases so we don't get visible spawn rows. */
        g->asteroid_storm_emitter_cd[i] = frand01() * base_interval;
    }
}

static int asteroid_try_spawn_inflow_world(game_state* g, asteroid_body* a) {
    if (!g || !a) {
        return 0;
    }
    const float su = gameplay_ui_scale(g);
    const float half_w = g->world_w * 0.5f;
    const float pad = 72.0f * su;
    const int emitter_n = ASTEROID_EMITTERS;
    const float x_min = g->camera_x - half_w - pad;
    const float x_max = g->camera_x + half_w + pad;
    const float y_top = g->world_h + pad;
    float vx = 0.0f, vy = 0.0f;
    asteroid_storm_velocity_local(g, &vx, &vy);
    if (vy >= -1e-3f) {
        return 0;
    }

    for (int tries = 0; tries < 12; ++tries) {
        const float sz = (8.0f + frand01() * 36.0f) * su;
        const float rr = sz * 0.90f;
        const int cursor = (g->asteroid_storm_emitter_cursor + tries) % emitter_n;
        const float cell_w = (x_max - x_min) / (float)emitter_n;
        const float sx = (x_min + (cursor + 0.5f) * cell_w) + frands1() * (cell_w * 0.45f);
        const float sy = y_top + frand01() * (pad * 0.85f);
        if (asteroid_overlap_candidate_world(g, a, sx, sy, rr)) {
            continue;
        }
        g->asteroid_storm_emitter_cursor = (cursor + 1) % emitter_n;
        a->active = 1;
        a->b.x = sx;
        a->b.y = sy;
        /* Small per-asteroid speed variation avoids visible "lanes" while keeping direction consistent. */
        const float spd = (0.90f + 0.20f * frand01());
        a->b.vx = vx * spd;
        a->b.vy = vy * spd;
        a->b.ax = 0.0f;
        a->b.ay = 0.0f;
        a->size = sz;
        a->radius = rr;
        a->angle = frand01() * 6.2831853f;
        a->spin_rate = frands1() * (0.85f + frand01() * 4.80f);
        return 1;
    }
    return 0;
}

static int asteroid_target_count(const game_state* g) {
    if (!g || g->asteroid_storm_density <= 0.0f) {
        return 0;
    }
    const float dens = clampf(g->asteroid_storm_density, 0.02f, 6.0f);
    int n = (int)lroundf(dens * 34.0f);
    if (n < 6) {
        n = 6;
    }
    if (n > MAX_ASTEROIDS) {
        n = MAX_ASTEROIDS;
    }
    return n;
}

static void configure_asteroid_storm_for_level(game_state* g) {
    const leveldef_level* lvl;
    if (!g) {
        return;
    }
    memset(g->asteroids, 0, sizeof(g->asteroids));
    g->asteroid_count = 0;
    g->asteroid_storm_active = 0;
    g->asteroid_storm_completed = 0;
    g->asteroid_storm_announced = 0;
    g->asteroid_storm_timer_s = 0.0f;
    g->asteroid_storm_enabled = 0;
    g->asteroid_storm_start_x = 0.0f;
    g->asteroid_storm_angle_rad = 0.0f;
    g->asteroid_storm_speed = 0.0f;
    g->asteroid_storm_duration_s = 0.0f;
    g->asteroid_storm_density = 0.0f;
    g->asteroid_storm_emitter_cursor = 0;
    memset(g->asteroid_storm_emitter_cd, 0, sizeof(g->asteroid_storm_emitter_cd));
    if (level_uses_cylinder(g)) {
        return;
    }
    lvl = current_leveldef(g);
    if (!lvl || !lvl->asteroid_storm_enabled) {
        return;
    }
    g->asteroid_storm_enabled = 1;
    g->asteroid_storm_start_x = fmaxf(0.0f, lvl->asteroid_storm_start_x01) * g->world_w;
    g->asteroid_storm_angle_rad = deg_to_rad(lvl->asteroid_storm_angle_deg);
    g->asteroid_storm_speed = fmaxf(lvl->asteroid_storm_speed, 0.0f);
    g->asteroid_storm_duration_s = fmaxf(lvl->asteroid_storm_duration_s, 0.01f);
    g->asteroid_storm_density = fmaxf(lvl->asteroid_storm_density, 0.01f);
    g->asteroid_count = asteroid_target_count(g);
    g->asteroid_storm_active = (g->asteroid_storm_start_x <= g->camera_x);
    g->asteroid_storm_completed = 0;
    g->asteroid_storm_timer_s = g->asteroid_storm_duration_s;
    g->asteroid_storm_cooldown_s = 0.0f; /* spawn accumulator */
    asteroid_storm_reset_emitters(g);
}

static void update_asteroid_storm(game_state* g, float dt) {
    if (!g || !g->asteroid_storm_enabled || level_uses_cylinder(g)) {
        return;
    }
    if (g->asteroid_count <= 0) {
        g->asteroid_count = asteroid_target_count(g);
    }
    if (!g->auto_event_mode &&
        !g->asteroid_storm_active &&
        !g->asteroid_storm_completed &&
        g->camera_x >= g->asteroid_storm_start_x) {
        asteroid_clear_bodies(g);
        g->asteroid_storm_active = 1;
        g->asteroid_storm_timer_s = g->asteroid_storm_duration_s;
        g->asteroid_storm_announced = 0;
        g->asteroid_storm_cooldown_s = 0.0f;
        asteroid_storm_reset_emitters(g);
    }
    if (g->asteroid_storm_active) {
        g->asteroid_storm_timer_s -= dt;
        if (!g->asteroid_storm_announced) {
            if (!g->wave_announce_pending) {
                g->wave_announce_pending = 1;
                snprintf(g->wave_announce_text, sizeof(g->wave_announce_text), "hazard alert\nasteroid storm");
            }
            g->asteroid_storm_announced = 1;
        }
        if (g->asteroid_storm_timer_s <= 0.0f) {
            g->asteroid_storm_active = 0;
            g->asteroid_storm_timer_s = 0.0f;
            g->asteroid_storm_completed = 1;
        }
    }
    const float su = gameplay_ui_scale(g);
    const float player_hit_r = 18.0f * su;
    const float half_w = g->world_w * 0.5f;
    const float pad = 72.0f * su;
    const float x_min = g->camera_x - half_w - pad;
    const float x_max = g->camera_x + half_w + pad;
    const float y_min = -pad;
    const float y_max = g->world_h + pad;
    const float screen_l = g->camera_x - half_w;
    const float screen_r = g->camera_x + half_w;
    float vx = 0.0f, vy = 0.0f;
    asteroid_storm_velocity_local(g, &vx, &vy);

    /* Independent top-edge emitters: randomized cooldown per emitter. */
    if (g->asteroid_storm_active && vy < -1e-3f) {
        const int target = g->asteroid_count;
        int active_now = asteroid_active_count(g);
        int spawn_budget = 5;
        const int start = g->asteroid_storm_emitter_cursor % ASTEROID_EMITTERS;
        for (int step = 0; step < ASTEROID_EMITTERS && spawn_budget > 0 && active_now < target; ++step) {
            const int ei = (start + step) % ASTEROID_EMITTERS;
            g->asteroid_storm_emitter_cd[ei] -= dt;
            if (g->asteroid_storm_emitter_cd[ei] > 0.0f) {
                continue;
            }
            asteroid_body* slot = asteroid_find_inactive_slot(g);
            if (!slot) {
                break;
            }
            if (asteroid_try_spawn_inflow_world(g, slot)) {
                active_now += 1;
                spawn_budget -= 1;
                /* New randomized cooldown: each emitter is its own Poisson-ish source. */
                const float base = 0.10f + 0.40f / fmaxf(g->asteroid_storm_density, 0.05f);
                g->asteroid_storm_emitter_cd[ei] = base * (0.6f + 1.2f * frand01());
            } else {
                /* Try again soon. */
                g->asteroid_storm_emitter_cd[ei] = 0.02f;
            }
        }
        g->asteroid_storm_emitter_cursor = (start + 1) % ASTEROID_EMITTERS;
    }
    int player_hit_this_tick = 0;
    for (int i = 0; i < g->asteroid_count && i < MAX_ASTEROIDS; ++i) {
        asteroid_body* a = &g->asteroids[i];
        if (!a->active) {
            continue;
        }
        integrate_body(&a->b, dt);
        a->angle += a->spin_rate * dt;

        /* Horizontal wrap to keep storm density consistent even when drift is steep. */
        if (a->b.x < screen_l - a->radius) {
            a->b.x = screen_r + a->radius;
        } else if (a->b.x > screen_r + a->radius) {
            a->b.x = screen_l - a->radius;
        }

        if (a->b.y > y_max || a->b.y < y_min) {
            a->active = 0;
            continue;
        }
        if (g->lives > 0 && !g->shield_active && !player_hit_this_tick) {
            const float rr = a->radius + player_hit_r;
            if (dist_sq(a->b.x, a->b.y, g->player.b.x, g->player.b.y) <= rr * rr) {
                emit_player_asteroid_explosion(g);
                game_set_player_dead(g, 1);
                player_hit_this_tick = 1;
                a->active = 0;
            }
        }
    }
}

static homing_missile* alloc_missile(game_state* g) {
    if (!g) {
        return NULL;
    }
    for (int i = 0; i < MAX_MISSILES; ++i) {
        if (g->missiles[i].active) {
            continue;
        }
        homing_missile* m = &g->missiles[i];
        memset(m, 0, sizeof(*m));
        m->active = 1;
        g->missile_count += 1;
        return m;
    }
    return NULL;
}

static void kill_missile(game_state* g, homing_missile* m) {
    if (!g || !m || !m->active) {
        return;
    }
    m->active = 0;
    if (g->missile_count > 0) {
        g->missile_count -= 1;
    }
}

static void emit_missile_explosion(game_state* g, float x, float y, float vx, float vy) {
    if (!g) {
        return;
    }
    const float su = gameplay_ui_scale(g);
    game_push_audio_event(g, GAME_AUDIO_EVENT_EMP, x, y);
    trigger_emp_visual_at(g, x, y, fminf(g->world_w, g->world_h) * 0.08f);
    for (int i = 0; i < 26; ++i) {
        particle* p = alloc_particle(g);
        if (!p) {
            break;
        }
        const float a = frand01() * 6.2831853f;
        const float spd = (90.0f + frand01() * 320.0f) * su;
        p->type = (frand01() < 0.70f) ? PARTICLE_POINT : PARTICLE_GEOM;
        p->b.x = x + frands1() * 4.0f * su;
        p->b.y = y + frands1() * 4.0f * su;
        p->b.vx = cosf(a) * spd + vx * 0.2f;
        p->b.vy = sinf(a) * spd + vy * 0.2f;
        p->age_s = 0.0f;
        p->life_s = 0.32f + frand01() * 0.56f;
        p->size = (2.0f + frand01() * 4.2f) * su;
        p->spin = frand01() * 6.2831853f;
        p->spin_rate = frands1() * 9.0f;
        p->r = 1.0f;
        p->g = 0.72f + frand01() * 0.26f;
        p->bcol = 0.30f + frand01() * 0.34f;
        p->a = 1.0f;
    }
}

static int find_player_missile_target(
    const game_state* g,
    const homing_missile* m,
    float half_angle_deg,
    float* out_tx,
    float* out_ty
) {
    if (!g || !m || !out_tx || !out_ty) {
        return 0;
    }
    const int uses_cylinder = level_uses_cylinder(g);
    const float period = cylinder_period(g);
    float fwd_x = m->forward_x;
    float fwd_y = m->forward_y;
    if (fabsf(fwd_x) < 1.0e-5f && fabsf(fwd_y) < 1.0e-5f) {
        fwd_x = cosf(m->heading_rad);
        fwd_y = sinf(m->heading_rad);
    }
    normalize2(&fwd_x, &fwd_y);
    const float cone_cos = cosf(deg_to_rad(clampf(half_angle_deg, 1.0f, 179.0f)));
    float best_d2 = 1.0e20f;
    int found = 0;
    for (size_t j = 0; j < MAX_ENEMIES; ++j) {
        const enemy* e = &g->enemies[j];
        if (!e->active) {
            continue;
        }
        const float dx = uses_cylinder ? wrap_delta(e->b.x, m->b.x, period) : (e->b.x - m->b.x);
        const float dy = e->b.y - m->b.y;
        const float d2 = dx * dx + dy * dy;
        if (d2 <= 1.0e-6f) {
            continue;
        }
        const float inv_d = 1.0f / sqrtf(d2);
        const float nx = dx * inv_d;
        const float ny = dy * inv_d;
        const float dp = nx * fwd_x + ny * fwd_y;
        if (dp < cone_cos) {
            continue;
        }
        if (d2 < best_d2) {
            best_d2 = d2;
            *out_tx = m->b.x + dx;
            *out_ty = m->b.y + dy;
            found = 1;
        }
    }
    return found;
}

static void explode_missile(game_state* g, homing_missile* m, int direct_hit) {
    if (!g || !m || !m->active) {
        return;
    }
    const float su = gameplay_ui_scale(g);
    const int uses_cylinder = level_uses_cylinder(g);
    const float period = cylinder_period(g);
    const float x = m->b.x;
    const float y = m->b.y;
    const float hit_r = fmaxf(m->hit_radius, 1.0f);
    const float blast_r = fmaxf(m->blast_radius, hit_r);
    const float hit2 = hit_r * hit_r;
    const float blast2 = blast_r * blast_r;

    if (m->owner == MISSILE_OWNER_ENEMY) {
        float dx = uses_cylinder ? wrap_delta(g->player.b.x, x, period) : (g->player.b.x - x);
        float dy = g->player.b.y - y;
        const float d2 = dx * dx + dy * dy;
        if (d2 <= blast2) {
            const float d = sqrtf(fmaxf(d2, 1.0e-6f));
            float nx = dx / d;
            float ny = dy / d;
            if (d <= 1.0e-3f) {
                nx = (frand01() < 0.5f) ? -1.0f : 1.0f;
                ny = frands1() * 0.25f;
            }
            const float blast_push = 1250.0f * su;
            g->player.b.vx += nx * blast_push;
            g->player.b.vy += ny * blast_push;
            g->mine_push_ax = nx * (blast_push * 3.4f);
            g->mine_push_ay = ny * (blast_push * 3.4f);
            if (g->mine_push_time_s < 0.35f) {
                g->mine_push_time_s = 0.35f;
            }
        }
        if (direct_hit && d2 <= hit2 && !g->shield_active && g->lives > 0) {
            game_on_player_life_lost(g);
        }
    } else {
        const float blast_accel = 2200.0f * su;
        const float push_duration_s = 1.10f;
        for (size_t i = 0; i < MAX_ENEMIES; ++i) {
            enemy* e = &g->enemies[i];
            if (!e->active) {
                continue;
            }
            const float dx = uses_cylinder ? wrap_delta(e->b.x, x, period) : (e->b.x - x);
            const float dy = e->b.y - y;
            const float d2 = dx * dx + dy * dy;
            if (d2 <= hit2) {
                const int hp_max = (e->hp > 0) ? e->hp : 1;
                e->hp = hp_max - 1;
                if (e->hp <= 0) {
                    e->active = 0;
                    game_on_enemy_destroyed(g, e->b.x, e->b.y, e->b.vx, e->b.vy, 100);
                }
                continue;
            }
            if (d2 <= blast2) {
                const float d = sqrtf(fmaxf(d2, 1.0e-6f));
                const float nx = dx / d;
                const float ny = dy / d;
                const float t = 1.0f - clampf((d - hit_r) / fmaxf(blast_r - hit_r, 1.0f), 0.0f, 1.0f);
                const float impulse = blast_accel * (0.72f + 0.90f * t);
                e->b.vx += nx * (impulse * 0.48f);
                e->b.vy += ny * (impulse * 0.48f);
                e->emp_push_ax += nx * (impulse * 3.0f);
                e->emp_push_ay += ny * (impulse * 3.0f);
                if (e->emp_push_time_s < push_duration_s) {
                    e->emp_push_time_s = push_duration_s;
                }
            }
        }
    }

    emit_missile_explosion(g, x, y, m->b.vx, m->b.vy);
    kill_missile(g, m);
}

static void configure_missile_launchers_for_level(game_state* g) {
    if (!g) {
        return;
    }
    memset(g->missile_launchers, 0, sizeof(g->missile_launchers));
    memset(g->missiles, 0, sizeof(g->missiles));
    g->missile_launcher_count = 0;
    g->missile_count = 0;
    if (level_uses_cylinder(g)) {
        return;
    }
    const leveldef_level* lvl = current_leveldef(g);
    if (!lvl || lvl->missile_launcher_count <= 0) {
        return;
    }
    const float su = gameplay_ui_scale(g);
    const int n = clampi(lvl->missile_launcher_count, 0, MAX_MISSILE_LAUNCHERS);
    for (int i = 0; i < n; ++i) {
        const leveldef_missile_launcher* d = &lvl->missile_launchers[i];
        missile_launcher* ml = &g->missile_launchers[g->missile_launcher_count++];
        memset(ml, 0, sizeof(*ml));
        ml->active = 1;
        ml->triggered = 0;
        ml->fired = 0;
        ml->launched_count = 0;
        ml->anchor_x = d->anchor_x01 * g->world_w;
        ml->anchor_y = d->anchor_y01 * g->world_h;
        ml->count = clampi(d->count, 1, 24);
        ml->spacing = fmaxf(d->spacing, 0.0f) * su;
        ml->activation_range = fmaxf(d->activation_range, 20.0f) * su;
        ml->launch_interval_s = 0.5f;
        ml->launch_timer_s = 0.0f;
        ml->missile_speed = fmaxf(d->missile_speed, 10.0f);
        ml->missile_turn_rate_deg = fmaxf(d->missile_turn_rate_deg, 1.0f);
        ml->missile_ttl_s = fmaxf(d->missile_ttl_s, 0.1f);
        ml->hit_radius = fmaxf(d->hit_radius, 1.0f);
        ml->blast_radius = fmaxf(d->blast_radius, ml->hit_radius);
        (void)game_find_noncolliding_spawn(
            g,
            &ml->anchor_x,
            &ml->anchor_y,
            fmaxf(ml->hit_radius * su, 16.0f * su),
            fmaxf(8.0f * su, ml->hit_radius * su * 0.5f),
            g->world_h * 0.35f
        );
    }
}

static void configure_arc_nodes_for_level(game_state* g) {
    if (!g) {
        return;
    }
    memset(g->arc_nodes, 0, sizeof(g->arc_nodes));
    g->arc_node_count = 0;
    if (level_uses_cylinder(g)) {
        return;
    }
    const leveldef_level* lvl = current_leveldef(g);
    if (!lvl || lvl->arc_node_count <= 0) {
        return;
    }
    const float su = gameplay_ui_scale(g);
    const int n = clampi(lvl->arc_node_count, 0, MAX_ARC_NODES);
    for (int i = 0; i < n; ++i) {
        const leveldef_arc_node* src = &lvl->arc_nodes[i];
        arc_node_runtime* an = &g->arc_nodes[g->arc_node_count++];
        memset(an, 0, sizeof(*an));
        an->active = 1;
        an->x = src->anchor_x01 * g->world_w;
        an->y = src->anchor_y01 * g->world_h;
        an->period_s = fmaxf(src->period_s, 0.10f);
        an->on_s = clampf(src->on_s, 0.0f, an->period_s);
        an->radius = fmaxf(src->radius, 4.0f) * su;
        an->push_accel = fmaxf(src->push_accel, 0.0f) * su;
        an->damage_interval_s = fmaxf(src->damage_interval_s, 0.02f);
        an->phase_s = (float)(i % 8) * 0.17f;
        an->damage_timer_s = 0.0f;
        an->sound_timer_s = 0.0f;
        an->energized_prev = 0;
    }
}

static void update_arc_nodes(game_state* g, float dt) {
    if (!g || g->arc_node_count < 2 || dt <= 0.0f || g->lives <= 0) {
        return;
    }
    g->lightning_active = 0;
    g->lightning_audio_gain = 0.0f;
    g->lightning_audio_pan = 0.0f;
    for (int i = 0; i + 1 < g->arc_node_count && i + 1 < MAX_ARC_NODES; i += 2) {
        arc_node_runtime* a = &g->arc_nodes[i];
        arc_node_runtime* b = &g->arc_nodes[i + 1];
        if (!a->active || !b->active) {
            continue;
        }
        const float period = fmaxf(a->period_s, 0.10f);
        const float on_s = clampf(a->on_s, 0.0f, period);
        if (on_s <= 0.0f) {
            continue;
        }
        const float t = fmodf(g->t + a->phase_s, period);
        const int energized = (t <= on_s);
        if (!energized) {
            a->damage_timer_s = 0.0f;
            a->sound_timer_s = 0.0f;
            a->energized_prev = 0;
            continue;
        }
        float cx = 0.0f;
        float cy = 0.0f;
        const float d2 = point_segment_dist_sq(
            g->player.b.x,
            g->player.b.y,
            a->x,
            a->y,
            b->x,
            b->y,
            &cx,
            &cy
        );
        {
            float sx = 0.0f;
            float sy = 0.0f;
            const float d2_screen = point_segment_dist_sq(
                g->camera_x,
                g->camera_y,
                a->x,
                a->y,
                b->x,
                b->y,
                &sx,
                &sy
            );
        const float hear_radius = fmaxf(a->radius * 2.2f, g->world_w * 1.20f);
        const float full_radius = fmaxf(a->radius * 0.65f, g->world_w * 0.08f);
            const float d = sqrtf(fmaxf(d2_screen, 0.0f));
        const float near01 = 1.0f - smoothstepf(full_radius, hear_radius, d);
        const int audible = (near01 > 0.0f);
        if (audible) {
            g->lightning_active = 1;
            if (near01 > g->lightning_audio_gain) {
                const float gate_x = 0.5f * (a->x + b->x);
                const float pan = clampf((gate_x - g->player.b.x) / (g->world_w * 0.45f), -1.0f, 1.0f);
                g->lightning_audio_gain = near01;
                g->lightning_audio_pan = pan;
            }
            if (!a->energized_prev) {
                game_push_audio_event(g, GAME_AUDIO_EVENT_LIGHTNING, 0.5f * (a->x + b->x), 0.5f * (a->y + b->y));
            }
        }
        }
        a->energized_prev = 1;
        if (d2 > a->radius * a->radius) {
            continue;
        }
        emit_player_asteroid_explosion(g);
        game_set_player_dead(g, 1);
        return;
    }
}

static void spawn_enemy_missile_one(game_state* g, missile_launcher* ml) {
    if (!g || !ml || !ml->active || ml->fired) {
        return;
    }
    if (ml->launched_count < 0 || ml->launched_count >= ml->count) {
        ml->fired = 1;
        return;
    }
    const float su = gameplay_ui_scale(g);
    const float half = ((float)ml->count - 1.0f) * 0.5f;
    homing_missile* m = alloc_missile(g);
    if (!m) {
        return;
    }
    const float slot = ((float)ml->launched_count - half);
    m->owner = MISSILE_OWNER_ENEMY;
    m->b.x = ml->anchor_x + slot * ml->spacing;
    m->b.y = ml->anchor_y;
    m->heading_rad = 1.57079632679f;
    m->speed = ml->missile_speed * su;
    m->turn_rate_rad_s = deg_to_rad(ml->missile_turn_rate_deg);
    m->ttl_s = ml->missile_ttl_s;
    m->hit_radius = ml->hit_radius * su;
    m->blast_radius = ml->blast_radius * su;
    m->radius = 10.0f * su;
    m->trail_emit_accum = 0.0f;
    m->b.vx = cosf(m->heading_rad) * m->speed;
    m->b.vy = sinf(m->heading_rad) * m->speed;
    ml->launched_count += 1;
    if (ml->launched_count >= ml->count) {
        ml->fired = 1;
    }
}

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
) {
    if (!g || g->lives <= 0) {
        return 0;
    }
    homing_missile* m = alloc_missile(g);
    if (!m) {
        return 0;
    }
    const float su = gameplay_ui_scale(g);
    const float v = sqrtf(dir_x * dir_x + dir_y * dir_y);
    if (v <= 1.0e-5f) {
        dir_x = 1.0f;
        dir_y = 0.0f;
    } else {
        dir_x /= v;
        dir_y /= v;
    }
    m->owner = MISSILE_OWNER_ENEMY;
    m->b.x = x;
    m->b.y = y;
    m->heading_rad = atan2f(dir_y, dir_x);
    m->speed = fmaxf(speed, 10.0f);
    m->turn_rate_rad_s = deg_to_rad(fmaxf(turn_rate_deg, 1.0f));
    m->ttl_s = fmaxf(ttl_s, 0.1f);
    m->hit_radius = fmaxf(hit_radius, 1.0f);
    m->blast_radius = fmaxf(blast_radius, m->hit_radius);
    m->radius = 10.0f * su;
    m->trail_emit_accum = 0.0f;
    m->b.vx = dir_x * m->speed;
    m->b.vy = dir_y * m->speed;
    m->b.ax = 0.0f;
    m->b.ay = 0.0f;
    return 1;
}

static void spawn_player_missile(game_state* g) {
    if (!g || g->lives <= 0) {
        return;
    }
    if (g->alt_weapon_equipped != PLAYER_ALT_WEAPON_MISSILE) {
        return;
    }
    if (g->alt_weapon_ammo[PLAYER_ALT_WEAPON_MISSILE] <= 0) {
        return;
    }
    const float su = gameplay_ui_scale(g);
    const float screen_ref = fminf(g->world_w, g->world_h);
    const float forward_x = (g->player.facing_x < 0.0f) ? -1.0f : 1.0f;
    const float forward_y = 0.0f;
    homing_missile* m = alloc_missile(g);
    if (!m) {
        return;
    }
    m->owner = MISSILE_OWNER_PLAYER;
    m->b.x = g->player.b.x - forward_x * (14.0f * su);
    m->b.y = g->player.b.y - (20.0f * su);
    m->heading_rad = atan2f(forward_y, forward_x);
    m->speed = 560.0f * su;
    m->turn_rate_rad_s = deg_to_rad(360.0f);
    m->ttl_s = 4.6f;
    m->hit_radius = screen_ref * 0.15f;
    m->blast_radius = screen_ref * 0.30f;
    m->radius = 9.5f * su;
    m->arm_delay_s = 1.0f;
    m->forward_x = forward_x;
    m->forward_y = forward_y;
    m->trail_emit_accum = 0.0f;
    m->b.vx = forward_x * (130.0f * su);
    m->b.vy = 0.0f;
    g->alt_weapon_ammo[PLAYER_ALT_WEAPON_MISSILE] -= 1;
    g->secondary_fire_cooldown_s = 0.18f;
    g->fire_sfx_pending += 1;
}

static void update_missile_system(game_state* g, float dt) {
    if (!g || dt <= 0.0f) {
        return;
    }
    if (g->missile_launcher_count > 0 && g->lives > 0) {
        for (int i = 0; i < g->missile_launcher_count && i < MAX_MISSILE_LAUNCHERS; ++i) {
            missile_launcher* ml = &g->missile_launchers[i];
            if (!ml->active || ml->fired) {
                continue;
            }
            if (!ml->triggered) {
                if (dist_sq(g->player.b.x, g->player.b.y, ml->anchor_x, ml->anchor_y) <= ml->activation_range * ml->activation_range &&
                    game_line_of_sight_clear(
                        g,
                        ml->anchor_x,
                        ml->anchor_y,
                        g->player.b.x,
                        g->player.b.y,
                        5.0f * gameplay_ui_scale(g))) {
                    ml->triggered = 1;
                    ml->launch_timer_s = 0.0f;
                }
            }
            if (ml->triggered && !ml->fired) {
                ml->launch_timer_s -= dt;
                if (ml->launch_timer_s <= 0.0f) {
                    spawn_enemy_missile_one(g, ml);
                    ml->launch_timer_s += fmaxf(0.05f, ml->launch_interval_s);
                }
            }
        }
    }

    for (int i = 0; i < MAX_MISSILES; ++i) {
        homing_missile* m = &g->missiles[i];
        if (!m->active) {
            continue;
        }

        if (m->owner == MISSILE_OWNER_PLAYER && m->arm_delay_s > 0.0f) {
            m->arm_delay_s -= dt;
            m->heading_rad = atan2f(m->forward_y, m->forward_x);
            m->b.ax = 0.0f;
            m->b.ay = 0.0f;
            m->b.vx = m->forward_x * (130.0f * gameplay_ui_scale(g));
            m->b.vy = m->forward_y * (130.0f * gameplay_ui_scale(g));
            integrate_body(&m->b, dt);
            if (m->arm_delay_s > 0.0f) {
                continue;
            }
            m->heading_rad = atan2f(m->forward_y, m->forward_x);
            m->b.vx = cosf(m->heading_rad) * m->speed;
            m->b.vy = sinf(m->heading_rad) * m->speed;
        }

        float tx = g->player.b.x;
        float ty = g->player.b.y;
        if (m->owner == MISSILE_OWNER_PLAYER) {
            if (!find_player_missile_target(g, m, 70.0f, &tx, &ty)) {
                tx = m->b.x + m->forward_x * g->world_w;
                ty = m->b.y + m->forward_y * g->world_h * 0.02f;
            }
        }

        {
            float dx = tx - m->b.x;
            float dy = ty - m->b.y;
            if (level_uses_cylinder(g)) {
                dx = wrap_delta(tx, m->b.x, cylinder_period(g));
            }
            const float desired = atan2f(dy, dx);
            const float delta = wrap_angle_rad(desired - m->heading_rad);
            const float max_step = m->turn_rate_rad_s * dt;
            m->heading_rad += clampf(delta, -max_step, max_step);
            m->heading_rad = wrap_angle_rad(m->heading_rad);
        }

        m->b.ax = 0.0f;
        m->b.ay = 0.0f;
        m->b.vx = cosf(m->heading_rad) * m->speed;
        m->b.vy = sinf(m->heading_rad) * m->speed;
        integrate_body(&m->b, dt);
        m->ttl_s -= dt;

        {
            const float su = gameplay_ui_scale(g);
            const float trail_rate = 65.0f;
            m->trail_emit_accum += trail_rate * dt;
            int emit_n = (int)m->trail_emit_accum;
            if (emit_n > 5) {
                emit_n = 5;
            }
            m->trail_emit_accum -= (float)emit_n;
            for (int p = 0; p < emit_n; ++p) {
                particle* pr = alloc_particle(g);
                if (!pr) {
                    break;
                }
                const float tx = -cosf(m->heading_rad);
                const float ty = -sinf(m->heading_rad);
                float jx = frands1() * 0.30f;
                float jy = frands1() * 0.30f;
                normalize2(&jx, &jy);
                pr->type = PARTICLE_POINT;
                pr->b.x = m->b.x + tx * (m->radius * 0.95f);
                pr->b.y = m->b.y + ty * (m->radius * 0.95f);
                pr->b.vx = tx * (180.0f + frand01() * 120.0f) * su + jx * 40.0f * su;
                pr->b.vy = ty * (180.0f + frand01() * 120.0f) * su + jy * 40.0f * su;
                pr->age_s = 0.0f;
                pr->life_s = 0.10f + frand01() * 0.20f;
                pr->size = (1.1f + frand01() * 2.0f) * su;
                pr->spin = 0.0f;
                pr->spin_rate = 0.0f;
                pr->r = 1.0f;
                pr->g = 0.70f + frand01() * 0.24f;
                pr->bcol = 0.20f + frand01() * 0.20f;
                pr->a = 1.0f;
            }
        }

        if (m->owner == MISSILE_OWNER_ENEMY) {
            if (dist_sq(m->b.x, m->b.y, g->player.b.x, g->player.b.y) <= m->hit_radius * m->hit_radius) {
                explode_missile(g, m, 1);
                continue;
            }
        } else {
            int direct = 0;
            const float su = gameplay_ui_scale(g);
            const int uses_cylinder = level_uses_cylinder(g);
            const float period = cylinder_period(g);
            for (size_t j = 0; j < MAX_ENEMIES; ++j) {
                const enemy* e = &g->enemies[j];
                if (!e->active) {
                    continue;
                }
                const float dx = uses_cylinder ? wrap_delta(e->b.x, m->b.x, period) : (e->b.x - m->b.x);
                const float dy = e->b.y - m->b.y;
                const float enemy_r = fmaxf(8.0f * su, e->radius);
                const float rr = m->radius + enemy_r;
                if ((dx * dx + dy * dy) <= rr * rr) {
                    direct = 1;
                    break;
                }
            }
            if (direct) {
                explode_missile(g, m, 1);
                continue;
            }
        }

        if (m->ttl_s <= 0.0f) {
            explode_missile(g, m, 0);
            continue;
        }
    }
}

static void configure_minefields_for_level(game_state* g) {
    if (!g) {
        return;
    }
    memset(g->mines, 0, sizeof(g->mines));
    g->mine_count = 0;
    if (level_uses_cylinder(g)) {
        return;
    }
    const leveldef_level* lvl = current_leveldef(g);
    if (!lvl || lvl->minefield_count <= 0) {
        return;
    }
    const float su = gameplay_ui_scale(g);
    const float field_w = g->world_w;
    const float field_h = g->world_h * 0.62f;
    const mine_tuning tune = mine_tuning_for(g);
    for (int fi = 0; fi < lvl->minefield_count; ++fi) {
        const leveldef_minefield* mf = &lvl->minefields[fi];
        const float cx = mf->anchor_x01 * g->world_w;
        const float cy = mf->anchor_y01 * g->world_h;
        const int count = clampi(mf->count, 1, MAX_MINES);
        for (int k = 0; k < count && g->mine_count < MAX_MINES; ++k) {
            mine* m = &g->mines[g->mine_count++];
            int placed = 0;
            for (int tries = 0; tries < 20; ++tries) {
                const float rr = tune.size;
                const float px = cx + (frands1() * 0.5f) * field_w;
                const float py = cy + (frands1() * 0.5f) * field_h;
                int overlap = 0;
                for (int j = 0; j < g->mine_count - 1; ++j) {
                    const mine* o = &g->mines[j];
                    if (!o->active) {
                        continue;
                    }
                    const float dr = rr + o->radius + 4.0f * su;
                    if (dist_sq(px, py, o->b.x, o->b.y) < dr * dr) {
                        overlap = 1;
                        break;
                    }
                }
                if (overlap) {
                    continue;
                }
                memset(m, 0, sizeof(*m));
                m->active = 1;
                m->b.x = px;
                m->b.y = py;
                m->radius = rr;
                m->angle = frand01() * 6.2831853f;
                m->spin_rate = frands1() * (0.8f + frand01() * 2.4f);
                m->hp = 10;
                m->style = clampi(mf->style, MINE_STYLE_CLASSIC, MINE_STYLE_ANEMONE);
                placed = 1;
                break;
            }
            if (!placed) {
                memset(m, 0, sizeof(*m));
            }
        }
    }
}

static void minefield_apply_emp(game_state* g, float radius) {
    if (!g || radius <= 0.0f) {
        return;
    }
    const float rr2 = radius * radius;
    for (int i = 0; i < g->mine_count && i < MAX_MINES; ++i) {
        mine* m = &g->mines[i];
        if (!m->active) {
            continue;
        }
        if (dist_sq(m->b.x, m->b.y, g->player.b.x, g->player.b.y) <= rr2) {
            explode_mine(g, m, 0.0f, 0.0f);
        }
    }
}

static void update_minefields(game_state* g, float dt) {
    if (!g || g->mine_count <= 0) {
        return;
    }
    const mine_tuning t = mine_tuning_for(g);
    const float act2 = t.activation_distance * t.activation_distance;
    const float det2 = t.detonation_distance * t.detonation_distance;
    const float su = gameplay_ui_scale(g);

    for (int i = 0; i < g->mine_count && i < MAX_MINES; ++i) {
        mine* m = &g->mines[i];
        if (!m->active) {
            continue;
        }
        float dx = g->player.b.x - m->b.x;
        float dy = g->player.b.y - m->b.y;
        const float d2 = dx * dx + dy * dy;
        if (d2 <= act2) {
            normalize2(&dx, &dy);
            steer_to_velocity(&m->b, dx * t.drift_speed, dy * t.drift_speed, t.drift_accel, 1.05f);
        } else {
            steer_to_velocity(&m->b, 0.0f, 0.0f, t.drift_accel, 2.25f);
        }
        integrate_body(&m->b, dt);
        {
            const float speed = length2(m->b.vx, m->b.vy);
            if (speed > t.max_speed) {
                const float s = t.max_speed / speed;
                m->b.vx *= s;
                m->b.vy *= s;
            }
        }
        m->angle += m->spin_rate * dt;

        /* Bullet impacts: mine takes 10 hits. */
        for (size_t bi = 0; bi < MAX_BULLETS; ++bi) {
            bullet* b = &g->bullets[bi];
            if (!b->active) {
                continue;
            }
            if (dist_sq(b->b.x, b->b.y, m->b.x, m->b.y) <= m->radius * m->radius) {
                b->active = 0;
                m->hp -= 1;
                if (m->hp <= 0) {
                    const float kill_x = m->b.x;
                    const float kill_y = m->b.y;
                    const float kill_vx = m->b.vx;
                    const float kill_vy = m->b.vy;
                    explode_mine(g, m, b->b.vx, b->b.vy);
                    game_on_enemy_destroyed(g, kill_x, kill_y, kill_vx, kill_vy, 120);
                }
                break;
            }
        }
        if (!m->active) {
            continue;
        }

        if (d2 <= det2) {
            float nx = g->player.b.x - m->b.x;
            float ny = g->player.b.y - m->b.y;
            if (nx * nx + ny * ny < 1.0e-6f) {
                nx = (frand01() < 0.5f) ? -1.0f : 1.0f;
                ny = frands1() * 0.25f;
            }
            normalize2(&nx, &ny);
            g->player.b.vx += nx * t.push_impulse;
            g->player.b.vy += ny * t.push_impulse;
            g->mine_push_ax = nx * t.push_accel;
            g->mine_push_ay = ny * t.push_accel;
            if (g->mine_push_time_s < t.push_duration_s) {
                g->mine_push_time_s = t.push_duration_s;
            }
            if (!g->shield_active && g->lives > 0) {
                game_on_player_life_lost(g);
            }
            explode_mine(g, m, g->player.b.vx, g->player.b.vy);
        }
    }
}

static float cylinder_period(const game_state* g) {
    return fmaxf(g->world_w * 2.4f, 1.0f);
}

static int level_uses_cylinder(const game_state* g) {
    return g && g->render_style == LEVEL_RENDER_CYLINDER;
}

static int level_count_gating_boss_markers(const leveldef_level* lvl) {
    int count = 0;
    if (!lvl) {
        return 0;
    }
    for (int i = 0; i < lvl->curated_count; ++i) {
        const leveldef_curated_enemy* ce = &lvl->curated[i];
        if (ce->kind == 20 && ce->e > 0.5f) {
            count += 1;
        }
    }
    return count;
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
    g->exit_requires_boss_defeated = 0;
    g->gating_bosses_remaining = 0;
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
    g->exit_requires_boss_defeated = lvl->exit_requires_boss_defeated ? 1 : 0;
    g->gating_bosses_remaining = g->exit_requires_boss_defeated ? level_count_gating_boss_markers(lvl) : 0;
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
    g->level_theme_palette = lvl->theme_palette;
    g->wave_cooldown_s = lvl->wave_cooldown_initial_s;
    if (lvl->wave_mode != LEVELDEF_WAVES_CURATED) {
        g->wave_cooldown_s = fmaxf(g->wave_cooldown_s, 2.5f);
    }
    g->auto_event_mode = (lvl->event_count > 0) ? 1 : 0;
    g->auto_event_index = 0;
    g->auto_event_running = 0;
    g->auto_event_running_kind = -1;
    g->auto_event_delay_s = (lvl->event_count > 0) ? fmaxf(lvl->events[0].delay_s, 0.0f) : 0.0f;
    if (lvl->wave_mode != LEVELDEF_WAVES_CURATED && lvl->event_count > 0) {
        g->auto_event_delay_s = fmaxf(g->auto_event_delay_s, 2.5f);
    }
    g->curated_spawned_count = 0;
    memset(g->curated_spawned, 0, sizeof(g->curated_spawned));
    configure_searchlights_for_level(g);
    configure_minefields_for_level(g);
    configure_missile_launchers_for_level(g);
    configure_arc_nodes_for_level(g);
    configure_asteroid_storm_for_level(g);
    configure_exit_portal_for_level(g);
}

static int set_level_index(game_state* g, int index) {
    const float su = gameplay_ui_scale(g);
    if (!g || g_level_count <= 0 || index < 0 || index >= g_level_count) {
        return 0;
    }
    g->level_index = index;
    g->level_style = g_levels[index].style_hint;
    snprintf(g->current_level_name, sizeof(g->current_level_name), "%s", g_levels[index].name);
    memset(g->bullets, 0, sizeof(g->bullets));
    memset(g->enemy_bullets, 0, sizeof(g->enemy_bullets));
    memset(g->enemies, 0, sizeof(g->enemies));
    memset(g->particles, 0, sizeof(g->particles));
    memset(g->debris, 0, sizeof(g->debris));
    memset(g->asteroids, 0, sizeof(g->asteroids));
    memset(g->mines, 0, sizeof(g->mines));
    memset(g->missiles, 0, sizeof(g->missiles));
    memset(g->missile_launchers, 0, sizeof(g->missile_launchers));
    memset(g->arc_nodes, 0, sizeof(g->arc_nodes));
    memset(g->eel_arcs, 0, sizeof(g->eel_arcs));
    memset(g->powerups, 0, sizeof(g->powerups));
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
    g->mine_push_ax = 0.0f;
    g->mine_push_ay = 0.0f;
    g->mine_push_time_s = 0.0f;
    g->shield_active = 0;
    g->lightning_active = 0;
    g->lightning_audio_gain = 0.0f;
    g->lightning_audio_pan = 0.0f;
    g->emp_effect_active = 0;
    g->emp_effect_t = 0.0f;
    g->emp_effect_duration_s = 0.0f;
    g->emp_primary_radius = 0.0f;
    g->emp_blast_radius = 0.0f;
    g->asteroid_count = 0;
    g->mine_count = 0;
    g->missile_count = 0;
    g->missile_launcher_count = 0;
    g->arc_node_count = 0;
    g->eel_arc_count = 0;
    g->powerup_count = 0;
    g->powerup_drop_credit = 0.0f;
    g->asteroid_storm_active = 0;
    g->asteroid_storm_completed = 0;
    g->asteroid_storm_announced = 0;
    g->orbit_decay_timeout = 0;
    apply_level_runtime_config(g);
    g->level_time_remaining_s = level_uses_cylinder(g) ? 120.0f : 0.0f;
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

static float wrap_angle_rad(float a) {
    while (a > 3.14159265359f) {
        a -= 6.28318530718f;
    }
    while (a < -3.14159265359f) {
        a += 6.28318530718f;
    }
    return a;
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

static void emit_player_asteroid_explosion(game_state* g) {
    if (!g) {
        return;
    }
    const float su = gameplay_ui_scale(g);
    game_push_audio_event(g, GAME_AUDIO_EVENT_EXPLOSION, g->player.b.x, g->player.b.y);
    {
        particle* f = alloc_particle(g);
        if (f) {
            f->type = PARTICLE_FLASH;
            f->b.x = g->player.b.x;
            f->b.y = g->player.b.y;
            f->age_s = 0.0f;
            f->life_s = 0.22f;
            f->size = 18.0f * su;
            f->r = 1.0f;
            f->g = 0.96f;
            f->bcol = 0.72f;
            f->a = 1.0f;
        }
    }
    for (int i = 0; i < 56; ++i) {
        particle* p = alloc_particle(g);
        if (!p) {
            break;
        }
        const float a = frand01() * 6.2831853f;
        const float spd = (120.0f + frand01() * 420.0f) * su;
        p->type = (frand01() < 0.7f) ? PARTICLE_POINT : PARTICLE_GEOM;
        p->b.x = g->player.b.x + frands1() * 5.0f * su;
        p->b.y = g->player.b.y + frands1() * 5.0f * su;
        p->b.vx = cosf(a) * spd + g->player.b.vx * 0.25f;
        p->b.vy = sinf(a) * spd + g->player.b.vy * 0.25f;
        p->age_s = 0.0f;
        p->life_s = 0.55f + frand01() * 0.65f;
        p->size = (2.5f + frand01() * 5.2f) * su;
        p->spin = frand01() * 6.2831853f;
        p->spin_rate = frands1() * 10.0f;
        p->r = 1.0f;
        p->g = 0.56f + frand01() * 0.40f;
        p->bcol = 0.22f + frand01() * 0.32f;
        p->a = 1.0f;
    }
}

static void emit_mine_debris(game_state* g, float x, float y, float radius, float impact_vx, float impact_vy) {
    if (!g) {
        return;
    }
    const float su = gameplay_ui_scale(g);
    for (int seg = 0; seg < 12; ++seg) {
        for (size_t i = 0; i < MAX_ENEMY_DEBRIS; ++i) {
            enemy_debris* d = &g->debris[i];
            if (d->active) {
                continue;
            }
            const float a = ((float)seg / 12.0f) * 6.2831853f;
            d->active = 1;
            d->half_len = radius * 0.36f;
            d->angle = a;
            d->spin_rate = frands1() * (5.0f + 7.0f * frand01());
            d->b.x = x + cosf(a) * radius * 0.30f;
            d->b.y = y + sinf(a) * radius * 0.30f;
            d->b.vx = cosf(a) * (82.0f + frand01() * 190.0f) * su + impact_vx * 0.18f;
            d->b.vy = sinf(a) * (82.0f + frand01() * 190.0f) * su + impact_vy * 0.18f;
            d->b.ax = -d->b.vx * 0.16f;
            d->b.ay = -220.0f;
            d->age_s = 0.0f;
            d->life_s = 1.5f + frand01() * 0.95f;
            d->alpha = 1.0f;
            break;
        }
    }
}

static void trigger_emp_visual_at(game_state* g, float x, float y, float radius) {
    if (!g) {
        return;
    }
    g->emp_effect_active = 1;
    g->emp_effect_t = 0.0f;
    g->emp_effect_duration_s = 0.24f;
    g->emp_effect_x = x;
    g->emp_effect_y = y;
    g->emp_primary_radius = radius * 0.52f;
    g->emp_blast_radius = radius;
}

static void explode_mine(game_state* g, mine* m, float impact_vx, float impact_vy) {
    if (!g || !m || !m->active) {
        return;
    }
    const float su = gameplay_ui_scale(g);
    const float x = m->b.x;
    const float y = m->b.y;
    game_push_audio_event(g, GAME_AUDIO_EVENT_EMP, x, y);
    trigger_emp_visual_at(g, x, y, mine_tuning_for(g).emp_fx_radius);
    emit_mine_debris(g, x, y, m->radius, impact_vx, impact_vy);
    for (int i = 0; i < 22; ++i) {
        particle* p = alloc_particle(g);
        if (!p) {
            break;
        }
        const float a = frand01() * 6.2831853f;
        const float spd = (75.0f + frand01() * 220.0f) * su;
        p->type = (frand01() < 0.65f) ? PARTICLE_POINT : PARTICLE_GEOM;
        p->b.x = x + frands1() * 5.0f * su;
        p->b.y = y + frands1() * 5.0f * su;
        p->b.vx = cosf(a) * spd + impact_vx * 0.2f;
        p->b.vy = sinf(a) * spd + impact_vy * 0.2f;
        p->age_s = 0.0f;
        p->life_s = 0.45f + frand01() * 0.55f;
        p->size = (2.2f + frand01() * 4.0f) * su;
        p->spin = frand01() * 6.2831853f;
        p->spin_rate = frands1() * 9.0f;
        p->r = 1.0f;
        p->g = 0.82f + frand01() * 0.18f;
        p->bcol = 0.38f + frand01() * 0.28f;
        p->a = 1.0f;
    }
    m->active = 0;
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

static void trigger_emp_blast(game_state* g) {
    if (!g || g->lives <= 0) {
        return;
    }
    if (g->alt_weapon_equipped != PLAYER_ALT_WEAPON_EMP) {
        return;
    }
    if (g->alt_weapon_ammo[PLAYER_ALT_WEAPON_EMP] <= 0) {
        return;
    }
    {
        const float screen_ref = fminf(g->world_w, g->world_h);
        const float su = gameplay_ui_scale(g);
        const float primary_radius = screen_ref * 0.15f;
        const float blast_radius = screen_ref * 0.30f;
        const float blast_impulse = 2200.0f * su;
        enemy_apply_emp(
            g,
            primary_radius,
            blast_radius,
            blast_impulse,
            su,
            level_uses_cylinder(g),
            cylinder_period(g)
        );
        minefield_apply_emp(g, blast_radius);
        g->alt_weapon_ammo[PLAYER_ALT_WEAPON_EMP] -= 1;
        g->secondary_fire_cooldown_s = 0.85f;
        g->emp_effect_active = 1;
        g->emp_effect_t = 0.0f;
        g->emp_effect_duration_s = 0.34f;
        g->emp_effect_x = g->player.b.x;
        g->emp_effect_y = g->player.b.y;
        g->emp_primary_radius = primary_radius;
        g->emp_blast_radius = blast_radius;
        game_push_audio_event(g, GAME_AUDIO_EVENT_EMP, g->emp_effect_x, g->emp_effect_y);
    }
}

static void use_secondary_weapon(game_state* g) {
    if (!g) {
        return;
    }
    switch (g->alt_weapon_equipped) {
        case PLAYER_ALT_WEAPON_REAR_GUN:
            spawn_secondary_bullet(g);
            break;
        case PLAYER_ALT_WEAPON_MISSILE:
            spawn_player_missile(g);
            break;
        case PLAYER_ALT_WEAPON_EMP:
            trigger_emp_blast(g);
            break;
        default:
            break;
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

int game_spawn_enemy_bullet(
    game_state* g,
    float x,
    float y,
    float dir_x,
    float dir_y,
    float speed,
    float ttl_s,
    float radius
) {
    enemy_bullet* b;
    const float d2 = dir_x * dir_x + dir_y * dir_y;
    if (!g || d2 <= 1.0e-6f || speed <= 0.0f || ttl_s <= 0.0f || radius <= 0.0f) {
        return 0;
    }
    {
        const float inv_d = 1.0f / sqrtf(d2);
        dir_x *= inv_d;
        dir_y *= inv_d;
    }
    b = spawn_enemy_bullet_at(g, x, y, dir_x, dir_y, speed, ttl_s, radius);
    if (!b) {
        return 0;
    }
    g->fire_sfx_pending += 1;
    return 1;
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
    g->lightning_active = 0;
    g->lightning_audio_gain = 0.0f;
    g->lightning_audio_pan = 0.0f;
    g->emp_effect_active = 0;
    g->emp_effect_t = 0.0f;
    g->emp_effect_duration_s = 0.0f;
    g->emp_effect_x = 0.0f;
    g->emp_effect_y = 0.0f;
    g->emp_primary_radius = 0.0f;
    g->emp_blast_radius = 0.0f;
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
    g->current_level_name[0] = '\0';
    g->orbit_decay_timeout = 0;
    g->wave_index = 0;
    g->wave_id_alloc = 0;
    ensure_leveldef_loaded();
    for (int i = 0; i < g_level_count; ++i) {
        if (g_levels[i].style_hint == LEVEL_STYLE_DEFENDER) {
            g->level_index = i;
            break;
        }
    }
    g->level_style = g_levels[g->level_index].style_hint;
    snprintf(g->current_level_name, sizeof(g->current_level_name), "%s", g_levels[g->level_index].name);
    apply_level_runtime_config(g);
    g->level_time_remaining_s = level_uses_cylinder(g) ? 120.0f : 0.0f;

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
    const int next = (g->level_index + 1) % g_level_count;
    (void)set_level_index(g, next);
}

static void game_handle_restart(game_state* g, const game_input* in) {
    if (g->orbit_decay_timeout && in->fire) {
        game_cycle_level(g);
        return;
    }
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
    if (g->mine_push_time_s > 0.0f) {
        const float decay = clampf(g->mine_push_time_s / 0.42f, 0.0f, 1.0f);
        g->player.b.ax += g->mine_push_ax * decay;
        g->player.b.ay += g->mine_push_ay * decay;
        g->mine_push_time_s -= dt;
        if (g->mine_push_time_s <= 0.0f) {
            g->mine_push_time_s = 0.0f;
            g->mine_push_ax = 0.0f;
            g->mine_push_ay = 0.0f;
        }
    }
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
    if (game_structure_circle_overlap(g, g->player.b.x, g->player.b.y, 14.0f * su)) {
        emit_player_asteroid_explosion(g);
        game_set_player_dead(g, 1);
        return 0;
    }
    if (g->exit_portal_active &&
        dist_sq(g->player.b.x, g->player.b.y, g->exit_portal_x, g->exit_portal_y) <=
            g->exit_portal_radius * g->exit_portal_radius) {
        if (g->exit_requires_boss_defeated && g->gating_bosses_remaining > 0) {
            return 0;
        }
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
    if (g->emp_effect_active) {
        g->emp_effect_t += dt;
        if (g->emp_effect_t >= g->emp_effect_duration_s) {
            g->emp_effect_active = 0;
            g->emp_effect_t = 0.0f;
        }
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
        use_secondary_weapon(g);
    }
}

static void game_update_powerups(game_state* g, float dt) {
    const float su = gameplay_ui_scale(g);
    const int uses_cylinder = level_uses_cylinder(g);
    const float period = cylinder_period(g);
    for (int i = 0; i < MAX_POWERUPS; ++i) {
        powerup_pickup* p = &g->powerups[i];
        float px;
        float py;
        float dx;
        float dy;
        float rr;
        if (!p->active) {
            continue;
        }
        p->ttl_s -= dt;
        if (p->ttl_s <= 0.0f) {
            kill_powerup_pickup(g, p);
            continue;
        }
        p->spin += p->spin_rate * dt;
        p->bob_phase += dt * 2.8f;
        p->b.vx *= fmaxf(0.0f, 1.0f - dt * 1.6f);
        p->b.vy *= fmaxf(0.0f, 1.0f - dt * 1.6f);
        if (g->powerup_magnet_active && g->lives > 0) {
            const float to_x = uses_cylinder ? wrap_delta(g->player.b.x, p->b.x, period) : (g->player.b.x - p->b.x);
            const float to_y = g->player.b.y - p->b.y;
            const float d2 = to_x * to_x + to_y * to_y;
            if (d2 > 1.0e-6f) {
                const float d = sqrtf(d2);
                const float nx = to_x / d;
                const float ny = to_y / d;
                const float near_t = 1.0f - clampf((d - 28.0f * su) / (420.0f * su), 0.0f, 1.0f);
                const float accel = (90.0f + 560.0f * near_t) * su;
                p->b.vx += nx * accel * dt;
                p->b.vy += ny * accel * dt;
            }
        }
        integrate_body(&p->b, dt);
        if (uses_cylinder) {
            p->b.x = g->camera_x + wrap_delta(p->b.x, g->camera_x, period);
            p->b.y = clampf(p->b.y, 20.0f * su, g->world_h - 20.0f * su);
        } else {
            const float x_margin = g->world_w * 1.35f;
            if (fabsf(p->b.x - g->camera_x) > x_margin ||
                p->b.y < -56.0f * su ||
                p->b.y > g->world_h + 56.0f * su) {
                kill_powerup_pickup(g, p);
                continue;
            }
        }
        if (g->lives <= 0) {
            continue;
        }
        px = p->b.x;
        py = p->b.y + sinf(p->bob_phase) * (4.2f * su);
        dx = uses_cylinder ? wrap_delta(px, g->player.b.x, period) : (px - g->player.b.x);
        dy = py - g->player.b.y;
        rr = p->radius + 20.0f * su;
        if (dx * dx + dy * dy <= rr * rr) {
            game_apply_powerup(g, p->type);
            game_push_audio_event(g, GAME_AUDIO_EVENT_FX2, px, py);
            kill_powerup_pickup(g, p);
        }
    }
}

static void game_update_player_bullets(game_state* g, float dt) {
    const float su = gameplay_ui_scale(g);
    for (size_t i = 0; i < MAX_BULLETS; ++i) {
        if (!g->bullets[i].active) {
            continue;
        }
        bullet* b = &g->bullets[i];
        const float prev_x = b->b.x;
        const float prev_y = b->b.y;
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
        if (game_structure_segment_blocked(g, prev_x, prev_y, b->b.x, b->b.y, 2.4f * su)) {
            b->active = 0;
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
    if (lvl->wave_mode == LEVELDEF_WAVES_CURATED) {
        if (boss_any_controller_alive(g)) {
            return;
        }
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
    if (boss_any_controller_alive(g)) {
        return;
    }
    if (g->auto_event_mode && lvl->event_count > 0) {
        if (g->auto_event_running) {
            if (g->auto_event_running_kind == LEVELDEF_EVENT_ASTEROID_STORM) {
                if (!g->asteroid_storm_active) {
                    g->auto_event_running = 0;
                    g->auto_event_index = (g->auto_event_index + 1) % lvl->event_count;
                    g->auto_event_delay_s = fmaxf(lvl->events[g->auto_event_index].delay_s, 0.0f);
                }
            } else {
                if (game_enemy_count(g) <= 0) {
                    g->auto_event_running = 0;
                    g->auto_event_index = (g->auto_event_index + 1) % lvl->event_count;
                    g->auto_event_delay_s = fmaxf(lvl->events[g->auto_event_index].delay_s, 0.0f);
                }
            }
            return;
        }
        if (g->auto_event_delay_s > 0.0f) {
            g->auto_event_delay_s -= dt;
            if (g->auto_event_delay_s > 0.0f) {
                return;
            }
        }
        {
            const int ev_kind = lvl->events[g->auto_event_index].kind;
            if (ev_kind == LEVELDEF_EVENT_ASTEROID_STORM) {
                /* Event-lane storms must initialize their own runtime state even when
                   spatial storm triggering is disabled for this level. */
                asteroid_clear_bodies(g);
                g->asteroid_storm_enabled = 1;
                g->asteroid_storm_angle_rad = deg_to_rad(lvl->asteroid_storm_angle_deg);
                g->asteroid_storm_speed = fmaxf(lvl->asteroid_storm_speed, 0.0f);
                g->asteroid_storm_duration_s = fmaxf(lvl->asteroid_storm_duration_s, 0.01f);
                g->asteroid_storm_density = fmaxf(lvl->asteroid_storm_density, 0.01f);
                if (g->asteroid_count <= 0) {
                    g->asteroid_count = asteroid_target_count(g);
                }
                g->asteroid_storm_completed = 0;
                g->asteroid_storm_active = 1;
                g->asteroid_storm_timer_s = g->asteroid_storm_duration_s;
                g->asteroid_storm_announced = 0;
                g->asteroid_storm_cooldown_s = 0.0f;
                asteroid_storm_reset_emitters(g);
                g->auto_event_running = 1;
                g->auto_event_running_kind = ev_kind;
                return;
            } else {
                leveldef_level one = *lvl;
                one.wave_mode = LEVELDEF_WAVES_NORMAL;
                one.wave_cycle_count = 1;
                if (ev_kind == LEVELDEF_EVENT_WAVE_SINE) {
                    one.wave_cycle[0] = LEVELDEF_WAVE_SINE_SNAKE;
                } else if (ev_kind == LEVELDEF_EVENT_WAVE_V) {
                    one.wave_cycle[0] = LEVELDEF_WAVE_V_FORMATION;
                } else if (ev_kind == LEVELDEF_EVENT_WAVE_SWARM) {
                    one.wave_cycle[0] = LEVELDEF_WAVE_SWARM;
                } else if (ev_kind == LEVELDEF_EVENT_WAVE_SWARM_FISH) {
                    one.wave_cycle[0] = LEVELDEF_WAVE_SWARM_FISH;
                } else if (ev_kind == LEVELDEF_EVENT_WAVE_SWARM_FIREFLY) {
                    one.wave_cycle[0] = LEVELDEF_WAVE_SWARM_FIREFLY;
                } else if (ev_kind == LEVELDEF_EVENT_WAVE_SWARM_BIRD) {
                    one.wave_cycle[0] = LEVELDEF_WAVE_SWARM_BIRD;
                } else {
                    one.wave_cycle[0] = LEVELDEF_WAVE_KAMIKAZE;
                }
                enemy_spawn_next_wave(
                    g,
                    &g_leveldef,
                    &one,
                    gameplay_ui_scale(g),
                    level_uses_cylinder(g),
                    cylinder_period(g)
                );
                g->auto_event_running = 1;
                g->auto_event_running_kind = ev_kind;
                return;
            }
        }
    }
    /* Asteroid storm is an auto-spawn event lane: while active, pause auto wave spawning. */
    if (g->asteroid_storm_enabled && g->asteroid_storm_active) {
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
    if (!g->orbit_decay_timeout && g->lives > 0 && level_uses_cylinder(g) && g->level_time_remaining_s > 0.0f) {
        g->level_time_remaining_s -= dt;
        if (g->level_time_remaining_s <= 0.0f) {
            g->level_time_remaining_s = 0.0f;
            g->orbit_decay_timeout = 1;
            game_set_player_dead(g, 0);
            return;
        }
    }
    if (game_update_player(g, dt, in, su)) {
        return;
    }
    game_update_powerups(g, dt);
    game_update_player_weapons(g, dt, in);
    game_update_player_bullets(g, dt);
    game_update_wave_spawning(g, dt);
    update_asteroid_storm(g, dt);
    if (g->searchlight_count > 0) {
        update_searchlights(g, dt);
    }
    g->lightning_active = 0;
    g->lightning_audio_gain = 0.0f;
    g->lightning_audio_pan = 0.0f;
    if (g->arc_node_count > 1) {
        update_arc_nodes(g, dt);
    }
    enemy_update_system(g, &g_leveldef, dt, su, level_uses_cylinder(g), cylinder_period(g));
    update_minefields(g, dt);
    update_missile_system(g, dt);
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

const struct leveldef_level* game_current_leveldef(const game_state* g) {
    ensure_leveldef_loaded();
    return current_leveldef(g);
}

const char* game_current_level_name(const game_state* g) {
    ensure_leveldef_loaded();
    if (g && g->current_level_name[0] != '\0') {
        return g->current_level_name;
    }
    if (g && g->level_index >= 0 && g->level_index < g_level_count) {
        return g_levels[g->level_index].name;
    }
    if (g_level_count > 0) {
        return g_levels[0].name;
    }
    return "";
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

int game_refresh_levels(game_state* g) {
    char keep_name[64];
    keep_name[0] = '\0';
    ensure_leveldef_loaded();
    if (g) {
        const char* cur = game_current_level_name(g);
        if (cur && cur[0]) {
            snprintf(keep_name, sizeof(keep_name), "%s", cur);
        }
    }
    if (!leveldef_load_project_layout(&g_leveldef, g_level_dir, stderr)) {
        return 0;
    }
    g_level_count = leveldef_discover_levels_from_dir(
        &g_leveldef,
        g_level_dir,
        g_levels,
        LEVELDEF_MAX_DISCOVERED_LEVELS,
        stderr
    );
    if (g_level_count <= 0) {
        return 0;
    }
    if (g && keep_name[0]) {
        if (game_set_level_by_name(g, keep_name)) {
            return 1;
        }
    }
    if (g) {
        return set_level_index(g, 0);
    }
    return 1;
}

int game_apply_level_override(game_state* g, const struct leveldef_level* level, const char* level_name) {
    if (!g || !level) {
        return 0;
    }
    ensure_leveldef_loaded();
    if (g_level_count <= 0) {
        return 0;
    }
    if (g->level_index < 0 || g->level_index >= g_level_count) {
        return 0;
    }
    g_levels[g->level_index].level = *level;
    (void)level_name;
    /* Keep discovered filename identity stable. Runtime level overrides are
       data-only and must not rename the active discovered level entry. */
    if (g_levels[g->level_index].name[0] != '\0') {
        snprintf(g->current_level_name, sizeof(g->current_level_name), "%s", g_levels[g->level_index].name);
    }
    apply_level_runtime_config(g);
    return 1;
}

void game_set_alt_weapon(game_state* g, int weapon_id) {
    if (!g) {
        return;
    }
    g->alt_weapon_equipped = clampi(weapon_id, 0, PLAYER_ALT_WEAPON_COUNT - 1);
    if (g->alt_weapon_equipped != PLAYER_ALT_WEAPON_SHIELD) {
        g->shield_active = 0;
        g->lightning_active = 0;
        g->lightning_audio_gain = 0.0f;
        g->lightning_audio_pan = 0.0f;
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
