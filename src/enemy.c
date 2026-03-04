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
    ENEMY_STATE_KAMIKAZE_COIL = 3,
    ENEMY_STATE_KAMIKAZE_THRUST = 4,
    ENEMY_STATE_KAMIKAZE_STRIKE = 5
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

static float hash01_u32(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return (float)(x & 0x00ffffffU) / 16777215.0f;
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

static float smoothstepf(float edge0, float edge1, float x) {
    if (edge1 <= edge0) {
        return (x >= edge1) ? 1.0f : 0.0f;
    }
    {
        const float t = clampf((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }
}

static uint32_t enemy_visual_seed_from_ids(int wave_id, int slot_index) {
    return (uint32_t)((uint32_t)wave_id * 73856093u) ^ (uint32_t)((uint32_t)slot_index * 19349663u);
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

static float wrap_position_near(float x, float ref, float period) {
    if (period <= 1.0e-5f) {
        return x;
    }
    return ref + wrap_delta(x, ref, period);
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

static float wrap_angle_pi(float a) {
    const float pi = 3.14159265359f;
    while (a > pi) a -= 2.0f * pi;
    while (a < -pi) a += 2.0f * pi;
    return a;
}

static float point_segment_dist_sq(float px, float py, float ax, float ay, float bx, float by) {
    const float abx = bx - ax;
    const float aby = by - ay;
    const float apx = px - ax;
    const float apy = py - ay;
    const float denom = abx * abx + aby * aby;
    float t = 0.0f;
    float qx;
    float qy;
    if (denom > 1.0e-6f) {
        t = (apx * abx + apy * aby) / denom;
    }
    t = clampf(t, 0.0f, 1.0f);
    qx = ax + abx * t;
    qy = ay + aby * t;
    return dist_sq(px, py, qx, qy);
}

static void eel_basis(const enemy* e, float* out_fx, float* out_fy, float* out_nx, float* out_ny) {
    float fx = 1.0f;
    float fy = 0.0f;
    float v;
    if (!e) {
        if (out_fx) *out_fx = fx;
        if (out_fy) *out_fy = fy;
        if (out_nx) *out_nx = -fy;
        if (out_ny) *out_ny = fx;
        return;
    }

    v = length2(e->facing_x, e->facing_y);
    if (v > 1.0e-5f) {
        fx = e->facing_x / v;
        fy = e->facing_y / v;
    } else {
        const float h = e->eel_heading_rad;
        fx = cosf(h);
        fy = sinf(h);
        v = length2(fx, fy);
        if (v <= 1.0e-5f) {
            fx = (e->b.vx < 0.0f) ? -1.0f : 1.0f;
            fy = 0.0f;
        }
    }
    if (out_fx) *out_fx = fx;
    if (out_fy) *out_fy = fy;
    if (out_nx) *out_nx = -fy;
    if (out_ny) *out_ny = fx;
}

static void eel_tail_anchor(const enemy* e, float* out_x, float* out_y) {
    float fx = 1.0f;
    float fy = 0.0f;
    float head_back;
    if (!e || !out_x || !out_y) {
        return;
    }
    eel_basis(e, &fx, &fy, NULL, NULL);
    head_back = fmaxf(1.0f, e->radius * 0.58f);
    *out_x = e->b.x - fx * head_back;
    *out_y = e->b.y - fy * head_back;
}

static void eel_seed_spine(enemy* e, int uses_cylinder, float period) {
    float anchor_x;
    float anchor_y;
    float fx = 1.0f;
    float fy = 0.0f;
    float body_len;
    float seg_len;
    int n;
    if (!e) {
        return;
    }
    n = EEL_SPINE_POINTS;
    if (n < 2) {
        n = 2;
    }
    eel_tail_anchor(e, &anchor_x, &anchor_y);
    eel_basis(e, &fx, &fy, NULL, NULL);
    body_len = (e->eel_body_length > 1.0f) ? e->eel_body_length : fmaxf(e->radius * 6.0f, 24.0f);
    seg_len = body_len / (float)(n - 1);
    e->eel_spine_count = n;
    for (int i = 0; i < n; ++i) {
        const float along = (float)i * seg_len;
        e->eel_spine_x[i] = anchor_x - fx * along;
        e->eel_spine_y[i] = anchor_y - fy * along;
    }
    if (uses_cylinder && period > 1.0e-5f) {
        for (int i = 0; i < n; ++i) {
            e->eel_spine_x[i] = wrap_position_near(e->eel_spine_x[i], anchor_x, period);
        }
    }
}

static void eel_update_spine(enemy* e, float dt, int uses_cylinder, float period) {
    float anchor_x;
    float anchor_y;
    float body_len;
    float seg_len;
    int n;
    if (!e) {
        return;
    }
    n = clampi(e->eel_spine_count, 0, EEL_SPINE_POINTS);
    if (n < 2) {
        eel_seed_spine(e, uses_cylinder, period);
        n = clampi(e->eel_spine_count, 0, EEL_SPINE_POINTS);
        if (n < 2) {
            return;
        }
    }
    eel_tail_anchor(e, &anchor_x, &anchor_y);
    if (uses_cylinder && period > 1.0e-5f) {
        anchor_x = wrap_position_near(anchor_x, e->eel_spine_x[0], period);
    }
    e->eel_spine_x[0] = anchor_x;
    e->eel_spine_y[0] = anchor_y;

    body_len = (e->eel_body_length > 1.0f) ? e->eel_body_length : fmaxf(e->radius * 6.0f, 24.0f);
    seg_len = body_len / (float)(n - 1);

    for (int i = 1; i < n; ++i) {
        float prev_x = e->eel_spine_x[i - 1];
        float prev_y = e->eel_spine_y[i - 1];
        float cur_x = e->eel_spine_x[i];
        float cur_y = e->eel_spine_y[i];
        float dx;
        float dy;
        float d;
        float pull;
        if (uses_cylinder && period > 1.0e-5f) {
            cur_x = wrap_position_near(cur_x, prev_x, period);
        }
        dx = cur_x - prev_x;
        dy = cur_y - prev_y;
        d = length2(dx, dy);
        if (d > 1.0e-5f) {
            const float overshoot = d - seg_len;
            if (overshoot > 0.0f) {
                pull = clampf(dt * 16.0f, 0.15f, 1.0f);
                cur_x -= (dx / d) * overshoot * pull;
                cur_y -= (dy / d) * overshoot * pull;
            } else {
                const float compress = (-overshoot) / fmaxf(seg_len, 1.0f);
                if (compress > 0.55f) {
                    pull = clampf(dt * 8.5f, 0.0f, 1.0f) * (compress - 0.55f);
                    cur_x += (dx / d) * seg_len * pull;
                    cur_y += (dy / d) * seg_len * pull;
                }
            }
        } else {
            cur_x = prev_x - seg_len;
            cur_y = prev_y;
        }
        e->eel_spine_x[i] = cur_x;
        e->eel_spine_y[i] = cur_y;
    }
    if (uses_cylinder && period > 1.0e-5f) {
        for (int i = 0; i < n; ++i) {
            e->eel_spine_x[i] = wrap_position_near(e->eel_spine_x[i], e->b.x, period);
        }
    }
}

static void eel_sample_body_point(
    const enemy* e,
    float u01,
    float* out_x,
    float* out_y
) {
    float fx = 1.0f;
    float fy = 0.0f;
    float nx = 0.0f;
    float ny = 1.0f;
    float u;
    float body_len;
    float wave_freq;
    float wave_amp;
    float t;
    float along;
    float env;
    float side;
    int spine_n;
    if (!e || !out_x || !out_y) {
        return;
    }
    spine_n = clampi(e->eel_spine_count, 0, EEL_SPINE_POINTS);
    if (spine_n >= 2) {
        const float u_spine = clampf(u01, 0.0f, 1.0f) * (float)(spine_n - 1);
        const int i0 = clampi((int)floorf(u_spine), 0, spine_n - 1);
        const int i1 = (i0 < spine_n - 1) ? (i0 + 1) : i0;
        const float t_spine = u_spine - (float)i0;
        *out_x = lerpf(e->eel_spine_x[i0], e->eel_spine_x[i1], t_spine);
        *out_y = lerpf(e->eel_spine_y[i0], e->eel_spine_y[i1], t_spine);
        return;
    }
    eel_basis(e, &fx, &fy, &nx, &ny);
    u = clampf(u01, 0.0f, 1.0f);
    body_len = (e->eel_body_length > 1.0f) ? e->eel_body_length : fmaxf(e->radius * 6.0f, 24.0f);
    wave_freq = (e->eel_wave_freq > 0.05f) ? e->eel_wave_freq : 2.2f;
    wave_amp = (e->eel_wave_amp > 0.01f) ? e->eel_wave_amp : fmaxf(e->radius * 0.55f, 4.0f);
    t = e->ai_timer_s * (1.8f + wave_freq) + e->visual_phase;
    along = -u * body_len;
    env = 0.18f + 0.82f * u;
    side = sinf(t * 1.95f - u * 12.2f) * wave_amp * env;
    {
        float base_x;
        float base_y;
        eel_tail_anchor(e, &base_x, &base_y);
        *out_x = base_x + fx * along + nx * side;
        *out_y = base_y + fy * along + ny * side;
    }
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

static void structure_aabb_world_enemy(
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
    structure_prefab_dims_world(st->prefab_id, &w_units, &h_units);
    unit_w = g->world_w * (float)LEVELDEF_STRUCTURE_GRID_SCALE / (float)(LEVELDEF_STRUCTURE_GRID_W - 1);
    unit_h = g->world_h * (float)LEVELDEF_STRUCTURE_GRID_SCALE / (float)(LEVELDEF_STRUCTURE_GRID_H - 1);
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

static int swarm_structure_collision_response(
    const game_state* g,
    float x,
    float y,
    float radius,
    float* out_nx,
    float* out_ny,
    float* out_penetration
) {
    const leveldef_level* lvl;
    float best_pen = 0.0f;
    float best_nx = 0.0f;
    float best_ny = 0.0f;
    int hit = 0;
    if (!g || !out_nx || !out_ny || !out_penetration || radius <= 0.0f) {
        return 0;
    }
    if (g->render_style == LEVEL_RENDER_CYLINDER) {
        return 0;
    }
    lvl = game_current_leveldef(g);
    if (!lvl || lvl->structure_count <= 0) {
        return 0;
    }
    for (int i = 0; i < lvl->structure_count && i < LEVELDEF_MAX_STRUCTURES; ++i) {
        const leveldef_structure_instance* st = &lvl->structures[i];
        float min_x;
        float min_y;
        float max_x;
        float max_y;
        float nearest_x;
        float nearest_y;
        float dx;
        float dy;
        float d2;
        float nx;
        float ny;
        float pen;
        if (st->layer != 0) {
            continue;
        }
        structure_aabb_world_enemy(g, st, &min_x, &min_y, &max_x, &max_y);
        nearest_x = fmaxf(min_x, fminf(x, max_x));
        nearest_y = fmaxf(min_y, fminf(y, max_y));
        dx = x - nearest_x;
        dy = y - nearest_y;
        d2 = dx * dx + dy * dy;
        if (d2 > radius * radius) {
            continue;
        }
        if (d2 > 1.0e-6f) {
            const float d = sqrtf(d2);
            nx = dx / d;
            ny = dy / d;
            pen = radius - d;
        } else {
            const float left = fabsf(x - min_x);
            const float right = fabsf(max_x - x);
            const float bottom = fabsf(y - min_y);
            const float top = fabsf(max_y - y);
            nx = -1.0f;
            ny = 0.0f;
            pen = radius + left;
            if (right < left && right <= bottom && right <= top) {
                nx = 1.0f;
                ny = 0.0f;
                pen = radius + right;
            } else if (bottom < left && bottom <= right && bottom <= top) {
                nx = 0.0f;
                ny = -1.0f;
                pen = radius + bottom;
            } else if (top < left && top <= right && top <= bottom) {
                nx = 0.0f;
                ny = 1.0f;
                pen = radius + top;
            }
        }
        if (!hit || pen > best_pen) {
            best_pen = pen;
            best_nx = nx;
            best_ny = ny;
            hit = 1;
        }
    }
    if (!hit) {
        return 0;
    }
    *out_nx = best_nx;
    *out_ny = best_ny;
    *out_penetration = best_pen;
    return 1;
}

static float add_inverse_avoid_force(
    float self_x,
    float self_y,
    float source_x,
    float source_y,
    float aware_r2,
    float personal_r2,
    float far_scale,
    float near_scale,
    float* io_avoid_x,
    float* io_avoid_y
) {
    float dx = self_x - source_x;
    float dy = self_y - source_y;
    float d2 = dx * dx + dy * dy;
    float boost = 0.0f;
    if (d2 >= aware_r2) {
        return 0.0f;
    }
    if (d2 < 1.0e-4f) {
        d2 = 1.0e-4f;
        dx = 1.0f;
        dy = 0.0f;
    }
    {
        const float far_falloff = 1.0f - (d2 / aware_r2);
        *io_avoid_x += (dx / d2) * (far_scale * far_falloff);
        *io_avoid_y += (dy / d2) * (far_scale * far_falloff);
        if (far_falloff > boost) {
            boost = far_falloff;
        }
    }
    if (d2 < personal_r2) {
        const float near_falloff = 1.0f - (d2 / personal_r2);
        *io_avoid_x += (dx / d2) * (near_scale * near_falloff);
        *io_avoid_y += (dy / d2) * (near_scale * near_falloff);
        if (near_falloff > boost) {
            boost = near_falloff;
        }
    }
    return boost;
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

static void emit_manta_turbulence(game_state* g, const enemy* e, float dt, float su, int uses_cylinder, float period) {
    if (!g || !e || !e->active) {
        return;
    }
    const leveldef_level* lvl = game_current_leveldef(g);
    if (!lvl || lvl->background_style != LEVELDEF_BACKGROUND_UNDERWATER) {
        return;
    }
    /* Underwater is currently a flat background; avoid wasting particles if we're far offscreen. */
    if (!uses_cylinder) {
        if (fabsf(e->b.x - g->camera_x) > g->world_w * 0.80f) {
            return;
        }
    } else {
        /* On cylinder there is no true offscreen; keep it very quiet. */
        if (fabsf(wrap_delta(e->b.x, g->camera_x, period)) > period * 0.45f) {
            return;
        }
    }

    float lane = (e->lane_dir != 0.0f) ? e->lane_dir : 0.0f;
    if (lane > -0.01f && lane < 0.01f) {
        lane = (e->b.vx != 0.0f) ? e->b.vx : e->facing_x;
    }
    const float fx = (lane < 0.0f) ? -1.0f : 1.0f;

    const float rr = fmaxf(e->radius, 6.0f * su);
    const float flap_speed = (e->visual_param_a > 0.01f) ? e->visual_param_a : 1.5f;
    const float flap_amp = clampf((e->visual_param_b > 0.01f) ? e->visual_param_b : 0.16f, 0.06f, 0.32f);
    const float flap_t = e->ai_timer_s * (2.2f + flap_speed) + e->visual_phase;
    const float flap_s1 = sinf(flap_t);
    const float flap_s2 = sinf(flap_t * 2.0f + 0.65f);
    float flap_pos = flap_s1 + 0.22f * flap_s2;
    flap_pos = clampf(flap_pos, -1.0f, 1.0f);
    float flap_vel = cosf(flap_t) + 0.44f * cosf(flap_t * 2.0f + 0.65f);
    flap_vel = clampf(flap_vel, -1.0f, 1.0f);

    const float spd = fabsf(e->b.vx);
    const float spd01 = clampf(spd / fmaxf(e->max_speed, 1.0f), 0.0f, 1.0f);
    const float beat01 = clampf(fabsf(flap_vel), 0.0f, 1.0f);
    const float strength = (0.35f + 0.65f * beat01) * (0.35f + 0.65f * spd01);

    /* Small bursty rate that follows the wingbeat, without needing per-enemy accumulators. */
    float emit_rate = (10.0f + 48.0f * strength) * (rr / fmaxf(18.0f * su, 1.0f));
    if (uses_cylinder) {
        emit_rate *= 0.35f;
    }
    float want = emit_rate * fmaxf(dt, 0.0f);
    int emit_count = (int)want;
    want -= (float)emit_count;
    if (frand01() < want) {
        emit_count += 1;
    }
    if (emit_count > 3) {
        emit_count = 3;
    }
    if (emit_count <= 0) {
        return;
    }

    const float wing_base_front_f = 1.06f * rr;
    const float wing_base_back_f = -1.76f * rr;
    const float wing_beat_amp = rr * (0.54f + 2.05f * flap_amp);
    const float wing_twist_amp = rr * (0.10f + 0.24f * flap_amp);
    const float wing_rest_amp = rr * (0.18f + 0.12f * flap_amp);
    const float wing_ripple_amp = rr * (0.06f + 0.20f * flap_amp);

    for (int pi = 0; pi < emit_count; ++pi) {
        /* Bias toward the trailing half, where the traveling wave reads strongest. */
        const float r = frand01();
        float u = 0.12f + 0.84f * (1.0f - powf(r, 1.7f));
        u = clampf(u, 0.06f, 0.94f);

        const float prof0 = sinf(u * 3.14159265f); /* 0..1..0 */
        float prof = prof0 * (1.10f - 0.55f * u);
        if (prof < 0.0f) {
            prof = 0.0f;
        }
        const float sweep_u = clampf(u + 0.10f * sinf(u * 3.14159265f), 0.0f, 1.0f);
        const float f = lerpf(wing_base_front_f, wing_base_back_f, sweep_u);

        const float rest_n = wing_rest_amp * (0.35f + 0.65f * (1.0f - u)) * prof;
        const float beat_n = wing_beat_amp * flap_pos * prof;
        const float twist_n = wing_twist_amp * flap_vel * prof * (0.85f - 0.65f * u);

        const float trailing_w = u * (0.35f + 0.65f * u);
        const float ripple_phase = flap_t * 2.25f - u * 14.5f + e->visual_phase * 0.7f;
        const float wave = sinf(ripple_phase);
        const float wave_env = (0.10f + 0.90f * prof0) * trailing_w;
        const float wave_n = wave * wave_env;
        const float ripple = wing_ripple_amp * wave_n;

        float outer_n = rest_n + beat_n + twist_n + ripple;
        if (fabsf(outer_n) < rr * 0.02f) {
            outer_n = (outer_n < 0.0f) ? (-rr * 0.02f) : (rr * 0.02f);
        }

        /* Skip weak wave regions so the turbulence clusters into a readable ripple. */
        const float wave_abs = fabsf(wave_n);
        if (frand01() > clampf(wave_abs * 1.35f, 0.15f, 1.0f)) {
            continue;
        }

        const float edge_x = e->b.x + fx * f;
        const float edge_y = e->b.y + outer_n;

        particle* p = alloc_particle(g);
        if (!p) {
            return;
        }

        /* Turbulence: short-lived, quickly damped, drifting behind the wing edge. */
        const float back_spd = (38.0f + 160.0f * strength) * su;
        const float up_spd = (20.0f + 90.0f * strength) * su * ((flap_vel >= 0.0f) ? 1.0f : -1.0f);
        /* Use FLASH so GPU heat stays "hot" (avoids the global orange heat-tint that point/geom get at heat=0). */
        p->type = PARTICLE_FLASH;
        p->b.x = edge_x - fx * (3.0f + frand01() * 3.5f) * su + frands1() * 2.0f * su;
        p->b.y = edge_y + frands1() * 2.5f * su;
        p->b.vx = -fx * back_spd + frands1() * 40.0f * su + e->b.vx * 0.30f;
        p->b.vy = up_spd + frands1() * 40.0f * su;
        p->b.ax = -p->b.vx * 4.2f;
        p->b.ay = -p->b.vy * 4.2f;
        p->age_s = 0.0f;
        p->life_s = 0.11f + frand01() * 0.12f;
        p->size = (1.2f + frand01() * 1.9f) * su;
        p->spin = frand01() * 6.2831853f;
        p->spin_rate = frands1() * 10.0f;
        /* Blue-green water shimmer; shader heat tint warms slightly. */
        p->r = 0.30f;
        p->g = 0.92f;
        p->bcol = 1.00f;
        p->a = 0.08f + 0.12f * strength;
    }
}

static void apply_player_hit(game_state* g, float impact_x, float impact_y, float impact_vx, float impact_vy, float su) {
    if (!g || g->lives <= 0 || g->shield_active) {
        return;
    }
    emit_explosion(g, impact_x, impact_y, impact_vx, impact_vy, 48, su);
    game_on_player_life_lost(g);
}

static int shield_deflect_body(
    const game_state* g,
    body* b,
    float obj_radius,
    float impulse,
    int uses_cylinder,
    float period
) {
    float dx, dy, dist_sq_v, hit_r, dist, nx, ny, vn;
    if (!g || !b || !g->shield_active) {
        return 0;
    }
    dx = uses_cylinder ? wrap_delta(b->x, g->player.b.x, period) : (b->x - g->player.b.x);
    dy = b->y - g->player.b.y;
    hit_r = fmaxf(g->shield_radius, 1.0f) + fmaxf(obj_radius, 0.0f);
    dist_sq_v = dx * dx + dy * dy;
    if (dist_sq_v > hit_r * hit_r) {
        return 0;
    }
    dist = sqrtf(fmaxf(dist_sq_v, 1.0e-8f));
    nx = (dist > 1.0e-4f) ? (dx / dist) : 1.0f;
    ny = (dist > 1.0e-4f) ? (dy / dist) : 0.0f;

    /* Push outside the shield surface immediately so collisions feel snappy. */
    if (uses_cylinder) {
        b->x = g->player.b.x + nx * (hit_r + 2.0f);
        b->y = g->player.b.y + ny * (hit_r + 2.0f);
    } else {
        b->x = g->player.b.x + nx * (hit_r + 2.0f);
        b->y = g->player.b.y + ny * (hit_r + 2.0f);
    }

    vn = b->vx * nx + b->vy * ny;
    if (vn < 0.0f) {
        b->vx -= 2.0f * vn * nx;
        b->vy -= 2.0f * vn * ny;
    }
    b->vx += nx * impulse;
    b->vy += ny * impulse;
    return 1;
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
        const float kamikaze_fire_p = clampf(t.armed_probability[arch] * 0.02f, 0.0f, 1.0f);
        e->armed = (frand01() < kamikaze_fire_p) ? 1 : 0;
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
        e->facing_x = -1.0f;
        e->facing_y = 0.0f;
        e->kamikaze_tail = 0.0f;
        e->kamikaze_thrust = 0.0f;
        e->kamikaze_tail_start = 0.0f;
        e->kamikaze_thrust_scale = 1.0f;
        e->kamikaze_glide_scale = 1.0f;
        e->kamikaze_strike_x = 0.0f;
        e->kamikaze_strike_y = 0.0f;
        e->kamikaze_is_turning = 0;
        e->visual_kind = ENEMY_VISUAL_DEFAULT;
        e->visual_seed = 0u;
        e->visual_phase = 0.0f;
        e->visual_param_a = 0.0f;
        e->visual_param_b = 0.0f;
        e->hp = 1;
        e->missile_ammo = 0;
        e->missile_cooldown_s = 0.0f;
        e->missile_charge_s = 0.0f;
        e->missile_charge_duration_s = 0.0f;
        e->eel_heading_rad = 0.0f;
        e->eel_wave_freq = 0.0f;
        e->eel_wave_amp = 0.0f;
        e->eel_body_length = 0.0f;
        e->eel_min_speed = 0.0f;
        e->eel_turn_rate_rad = 0.0f;
        e->eel_weapon_range = 0.0f;
        e->eel_weapon_fire_rate = 0.0f;
        e->eel_weapon_duration_s = 0.0f;
        e->eel_weapon_damage_interval_s = 0.0f;
        e->eel_spine_count = 0;
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

static void enemy_adjust_spawn_clear(game_state* g, enemy* e, float su) {
    if (!g || !e) {
        return;
    }
    (void)game_find_noncolliding_spawn(
        g,
        &e->b.x,
        &e->b.y,
        fmaxf(e->radius, 10.0f * su),
        fmaxf(8.0f * su, e->radius * 0.55f),
        g->world_h * 0.40f
    );
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
        enemy_adjust_spawn_clear(g, e, su);
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
        enemy_adjust_spawn_clear(g, e, su);
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
        e->swarm_min_speed = p->min_speed * su;
        e->swarm_turn_rate_rad = p->max_turn_rate_deg * (3.14159265359f / 180.0f);
        enemy_adjust_spawn_clear(g, e, su);
    }
}

static int resolve_boid_profile_by_variant(const leveldef_db* db, const leveldef_level* lvl, int variant_kind_or_pattern) {
    int pid = -1;
    if (!db || !lvl) {
        return 0;
    }
    if (variant_kind_or_pattern == 10 || variant_kind_or_pattern == LEVELDEF_WAVE_SWARM_FISH) {
        pid = leveldef_find_boid_profile(db, "FISH");
    } else if (variant_kind_or_pattern == 11 || variant_kind_or_pattern == LEVELDEF_WAVE_SWARM_FIREFLY) {
        pid = leveldef_find_boid_profile(db, "FIREFLY");
    } else if (variant_kind_or_pattern == 12 || variant_kind_or_pattern == LEVELDEF_WAVE_SWARM_BIRD) {
        pid = leveldef_find_boid_profile(db, "BIRD");
    } else if (variant_kind_or_pattern == 15) {
        pid = leveldef_find_boid_profile(db, "JELLY");
    } else if (variant_kind_or_pattern == 17) {
        pid = leveldef_find_boid_profile(db, "EEL");
    }
    if (pid < 0) {
        pid = lvl->default_boid_profile;
    }
    if (pid < 0) {
        pid = 0;
    }
    return pid;
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
            e->state = ENEMY_STATE_KAMIKAZE_COIL;
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
            {
                const float r_span = fmaxf(w->radius_max - w->radius_min, 0.0f);
                const float r01 = (r_span > 1e-4f) ? frand01() : 0.5f;
                e->radius = (w->radius_min + r01 * r_span) * su;
                e->kamikaze_thrust_scale = lerpf(1.00f, 1.50f, r01);
                e->kamikaze_glide_scale = lerpf(1.00f, 1.70f, r01);
            }
            e->ai_timer_s = 0.0f;
            e->break_delay_s = 0.90f + frand01() * 0.65f;
            e->facing_x = lane_dir_toward_player_x(g, e->b.x, 0, 0.0f);
            e->facing_y = 0.0f;
            e->kamikaze_tail = 0.18f;
            e->kamikaze_thrust = 0.0f;
            e->kamikaze_tail_start = e->kamikaze_tail;
            e->kamikaze_strike_x = e->b.x;
            e->kamikaze_strike_y = e->b.y;
            e->kamikaze_is_turning = 0;
            enemy_adjust_spawn_clear(g, e, su);
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
    if (ce->kind == 5 || ce->kind == 10 || ce->kind == 11 || ce->kind == 12 || ce->kind == 15 || ce->kind == 17) {
        const int profile_id = resolve_boid_profile_by_variant(db, lvl, ce->kind);
        const leveldef_boid_profile* p = leveldef_get_boid_profile(db, profile_id);
        if (!p) {
            return;
        }
        for (int i = 0; i < count; ++i) {
            enemy* e = spawn_enemy_common(g, su);
            const int legacy_default_override =
                (ce->b > 0.0f && ce->c > 0.0f &&
                 fabsf(ce->b - 190.0f) < 0.001f &&
                 fabsf(ce->c - 90.0f) < 0.001f);
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
            e->max_speed = ((ce->b > 0.0f && !legacy_default_override) ? ce->b : p->max_speed) * su;
            e->accel = ((ce->c > 0.0f && !legacy_default_override) ? ce->c : p->accel);
            e->radius = (p->radius_min + frand01() * fmaxf(p->radius_max - p->radius_min, 0.0f)) * su;
            {
                float jelly_sep_scale = 1.0f;
                if (ce->kind == 15) {
                    const float size_scale = (ce->d > 0.0f) ? ce->d : 1.0f;
                    const float size_scale_clamped = clampf(size_scale, 0.20f, 4.00f);
                    e->radius *= size_scale_clamped;
                    jelly_sep_scale = lerpf(0.75f, 1.90f, (size_scale_clamped - 0.20f) / (4.00f - 0.20f));
                }
                e->swarm_sep_r = p->sep_r * su * jelly_sep_scale;
            }
            e->swarm_ali_r = p->ali_r * su;
            e->swarm_coh_r = p->coh_r * su;
            if (ce->kind == 15) {
                const float size_scale = clampf((ce->d > 0.0f) ? ce->d : 1.0f, 0.20f, 4.00f);
                const float neighborhood_scale = lerpf(0.95f, 1.35f, (size_scale - 0.20f) / (4.00f - 0.20f));
                e->swarm_ali_r *= neighborhood_scale;
                e->swarm_coh_r *= neighborhood_scale;
            }
            e->swarm_sep_w = p->sep_w;
            e->swarm_ali_w = p->ali_w;
            e->swarm_coh_w = p->coh_w;
            e->swarm_avoid_w = p->avoid_w;
            e->swarm_goal_w = p->goal_w;
            e->swarm_goal_amp = p->goal_amp * su;
            e->swarm_goal_freq = p->goal_freq;
            e->swarm_goal_dir = 1.0f;
            e->swarm_wander_w = p->wander_w;
            e->swarm_wander_freq = p->wander_freq;
            e->swarm_drag = p->steer_drag;
            e->swarm_min_speed = p->min_speed * su;
            e->swarm_turn_rate_rad = p->max_turn_rate_deg * (3.14159265359f / 180.0f);
            if (ce->kind == 15) {
                const uint32_t seed = enemy_visual_seed_from_ids(wave_id, i);
                e->visual_kind = ENEMY_VISUAL_JELLY;
                e->visual_seed = seed;
                e->visual_phase = hash01_u32(seed) * 6.2831853f;
                e->visual_param_a = lerpf(1.6f, 2.4f, hash01_u32(seed ^ 0xA53u));
                e->visual_param_b = lerpf(0.08f, 0.18f, hash01_u32(seed ^ 0xB71u));
            }
            if (ce->kind == 17) {
                const uint32_t seed = enemy_visual_seed_from_ids(wave_id, i);
                const float size_scale = clampf((ce->d > 0.0f) ? ce->d : 1.0f, 0.55f, 2.40f);
                const float eel_speed = ((ce->b > 0.0f && !legacy_default_override) ? ce->b : 250.0f) * su;
                const float eel_accel = (ce->c > 0.0f && !legacy_default_override) ? ce->c : 6.4f;
                e->visual_kind = ENEMY_VISUAL_EEL;
                e->visual_seed = seed;
                e->visual_phase = hash01_u32(seed ^ 0x51du) * 6.2831853f;
                e->visual_param_a = lerpf(1.7f, 2.9f, hash01_u32(seed ^ 0x913u));
                e->visual_param_b = lerpf(0.20f, 0.36f, hash01_u32(seed ^ 0x347u));
                e->radius = fmaxf(8.0f * su, e->radius * 0.82f * size_scale);
                e->hp = 2;
                e->max_speed = fmaxf(120.0f * su, eel_speed);
                e->accel = fmaxf(1.4f, eel_accel);
                e->eel_wave_freq = e->visual_param_a;
                e->eel_wave_amp = fmaxf(4.0f * su, e->radius * (0.54f + 0.18f * e->visual_param_b));
                e->eel_body_length = fmaxf(e->radius * 6.2f, e->radius * (7.2f + 1.6f * size_scale));
                e->eel_min_speed = fmaxf(92.0f * su, e->max_speed * 0.42f);
                e->eel_turn_rate_rad = (360.0f + 200.0f * hash01_u32(seed ^ 0xA73u)) * (3.14159265359f / 180.0f);
                e->eel_weapon_range = fmaxf(112.0f * su, g->world_w * 0.24f);
                e->eel_weapon_fire_rate = 1.25f + 1.10f * hash01_u32(seed ^ 0xCC1u);
                e->eel_weapon_duration_s = 1.7f + 0.9f * hash01_u32(seed ^ 0x1B5u);
                e->eel_weapon_damage_interval_s = 0.30f;
                e->swarm_min_speed = e->eel_min_speed;
                e->swarm_turn_rate_rad = e->eel_turn_rate_rad;
                e->facing_x = lane_dir_toward_player_x(g, e->b.x, uses_cylinder, period);
                e->facing_y = frands1() * 0.18f;
                normalize2(&e->facing_x, &e->facing_y);
                e->eel_heading_rad = atan2f(e->facing_y, e->facing_x);
                eel_seed_spine(e, uses_cylinder, period);
                e->armed = 1;
                e->weapon_id = ENEMY_WEAPON_BURST;
                e->fire_cooldown_s = 0.25f + frand01() * 0.35f;
                e->burst_shots_left = 0;
                e->burst_gap_timer_s = 0.0f;
            }
            enemy_adjust_spawn_clear(g, e, su);
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

        if (ce->kind == 4) {
            const float slot = (float)i - 0.5f * (float)(count - 1);
            const float base_x = g->world_w * ce->x01;
            const float base_y = g->world_h * ce->y01;
            const float spread_x = fmaxf(24.0f * su, g->world_w * 0.030f);
            const float jitter_x = (frand01() - 0.5f) * fmaxf(10.0f * su, g->world_w * 0.010f);
            const float jitter_y = (frand01() - 0.5f) * fmaxf(26.0f * su, g->world_h * 0.060f);
            e->archetype = ENEMY_ARCH_KAMIKAZE;
            e->state = ENEMY_STATE_KAMIKAZE_COIL;
            enemy_assign_combat_loadout(g, e, db);
            e->b.x = base_x + slot * spread_x + jitter_x;
            e->b.y = base_y + jitter_y;
            e->max_speed = ((ce->b > 0.0f) ? ce->b : lvl->kamikaze.max_speed) * su;
            e->accel = (ce->c > 0.0f) ? ce->c : lvl->kamikaze.accel;
            {
                const float r_span = fmaxf(lvl->kamikaze.radius_max - lvl->kamikaze.radius_min, 0.0f);
                const float r01 = (r_span > 1e-4f) ? frand01() : 0.5f;
                e->radius = (lvl->kamikaze.radius_min + r01 * r_span) * su;
                e->kamikaze_thrust_scale = lerpf(1.00f, 1.50f, r01);
                e->kamikaze_glide_scale = lerpf(1.00f, 1.70f, r01);
            }
            e->ai_timer_s = 0.0f;
            e->break_delay_s = 0.85f + frand01() * 0.60f;
            e->facing_x = lane_dir_toward_player_x(g, e->b.x, uses_cylinder, period);
            e->facing_y = 0.0f;
            e->kamikaze_tail = 0.18f;
            e->kamikaze_thrust = 0.0f;
            e->kamikaze_tail_start = e->kamikaze_tail;
            e->kamikaze_strike_x = e->b.x;
            e->kamikaze_strike_y = e->b.y;
            e->kamikaze_is_turning = 0;
            enemy_adjust_spawn_clear(g, e, su);
        } else {
            e->archetype = ENEMY_ARCH_FORMATION;
            e->state = ENEMY_STATE_FORMATION;
            e->formation_kind = (ce->kind == 3) ? ENEMY_FORMATION_V : ENEMY_FORMATION_SINE;
            enemy_assign_combat_loadout(g, e, db);
            e->home_y = g->world_h * ce->y01;
            e->b.y = e->home_y;
            e->form_phase = (float)i * 0.4f;
            e->form_amp = fmaxf(0.0f, ce->b) * su;
            e->form_freq = (ce->kind == 3) ? lvl->v.form_freq : lvl->sine.form_freq;
            e->break_delay_s = 0.8f + 0.14f * (float)i;
            e->max_speed = ((ce->c > 0.0f) ? ce->c : ((ce->kind == 3) ? lvl->v.max_speed : lvl->sine.max_speed)) * su;
            e->accel = (ce->kind == 3) ? lvl->v.accel : lvl->sine.accel;
            e->lane_dir = lane_dir_toward_player_x(g, e->b.x, uses_cylinder, period);
            if (ce->kind == 16) {
                const uint32_t seed = enemy_visual_seed_from_ids(wave_id, i);
                const float size_scale = clampf((ce->b > 0.0f) ? ce->b : 1.90f, 1.0f, 4.0f);
                const float slot = (float)i - 0.5f * (float)(count - 1);
                e->visual_kind = ENEMY_VISUAL_MANTA;
                e->visual_seed = seed;
                e->visual_phase = hash01_u32(seed ^ 0x6d2u) * 6.2831853f;
                e->visual_param_a = lerpf(1.2f, 1.8f, hash01_u32(seed ^ 0x39fu)); /* flap speed */
                e->visual_param_b = lerpf(0.12f, 0.22f, hash01_u32(seed ^ 0x8a1u)); /* flap amp */
                e->formation_kind = ENEMY_FORMATION_V;
                e->armed = 1;
                e->weapon_id = ENEMY_WEAPON_PULSE;
                e->hp = 3;
                e->missile_ammo = (ce->c > 0.0f) ? clampi((int)lroundf(ce->c), 0, 12) : 2;
                e->radius *= size_scale;
                {
                    const float spacing = fmaxf(78.0f * su, e->radius * 2.8f);
                    e->b.x = g->world_w * ce->x01 + slot * spacing;
                    if (count > 1) {
                        const float y_spacing = fmaxf(42.0f * su, e->radius * 1.55f);
                        e->home_y = clampf(e->home_y + slot * y_spacing, 26.0f * su, g->world_h - 26.0f * su);
                        e->b.y = e->home_y;
                    }
                }
                e->form_amp = 0.0f;
                e->form_freq = 0.0f;
                e->max_speed = 240.0f * su;
                e->accel = 1.9f;
                e->fire_cooldown_s = 1.0f + frand01() * 0.8f;
                e->missile_cooldown_s = 2.8f + frand01() * 2.0f;
            }
            enemy_adjust_spawn_clear(g, e, su);
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
            if (ce->kind == 10) {
                announce_wave(g, "curated fish swarm");
            } else if (ce->kind == 11) {
                announce_wave(g, "curated firefly swarm");
            } else if (ce->kind == 12) {
                announce_wave(g, "curated bird swarm");
            } else if (ce->kind == 5) {
                announce_wave(g, "curated boid contact");
            } else if (ce->kind == 15) {
                announce_wave(g, "curated jelly swarm");
            } else if (ce->kind == 16) {
                announce_wave(g, "curated manta ray");
            } else if (ce->kind == 17) {
                announce_wave(g, "curated electric eels");
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
        } else if (pattern == LEVELDEF_WAVE_SWARM ||
                   pattern == LEVELDEF_WAVE_SWARM_FISH ||
                   pattern == LEVELDEF_WAVE_SWARM_FIREFLY ||
                   pattern == LEVELDEF_WAVE_SWARM_BIRD) {
            const int profile_id = resolve_boid_profile_by_variant(db, lvl, pattern);
            const leveldef_boid_profile* profile = leveldef_get_boid_profile(db, profile_id);
            const char* wave_name = profile ? profile->wave_name : "boid swarm cluster";
            announce_wave(g, wave_name);
            {
                const float dir = bidirectional_spawns ? ((frand01() < 0.5f) ? -1.0f : 1.0f) : 1.0f;
                spawn_wave_swarm_profile(g, db, wave_id, profile_id, dir, bidirectional_spawns, su);
                if (bidirectional_spawns && g->wave_index >= 4 && frand01() < lvl->cylinder_double_swarm_chance) {
                    const int wave_id_2 = ++g->wave_id_alloc;
                    spawn_wave_swarm_profile(g, db, wave_id_2, profile_id, -dir, bidirectional_spawns, su);
                }
            }
        } else if (pattern == LEVELDEF_WAVE_ASTEROID_STORM) {
            announce_wave(g, "asteroid storm");
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

static int alloc_eel_arc_slot(game_state* g) {
    if (!g) {
        return -1;
    }
    for (int i = 0; i < MAX_EEL_ARCS; ++i) {
        if (!g->eel_arcs[i].active) {
            return i;
        }
    }
    return -1;
}

static void eel_arc_build_points(const enemy* e, eel_arc_effect* arc) {
    const int seg_n = EEL_ARC_MAX_POINTS - 1;
    float sx;
    float sy;
    float tx;
    float ty;
    float dir_x;
    float dir_y;
    float side_x;
    float side_y;
    float ray_x;
    float ray_y;
    float ray_nx;
    float ray_ny;
    uint32_t tick;
    if (!e || !arc || seg_n < 1) {
        return;
    }

    eel_sample_body_point(e, arc->start_u, &sx, &sy);
    eel_sample_body_point(e, clampf(arc->start_u + 0.03f, 0.0f, 1.0f), &tx, &ty);
    dir_x = tx - sx;
    dir_y = ty - sy;
    normalize2(&dir_x, &dir_y);
    if (length2(dir_x, dir_y) < 1.0e-5f) {
        eel_basis(e, &dir_x, &dir_y, NULL, NULL);
    }
    side_x = -dir_y;
    side_y = dir_x;

    {
        const float anim = sinf(arc->age_s * (2.2f + arc->start_u * 0.9f) + arc->base_angle * 0.65f) * 0.12f;
        const float a = arc->base_angle + anim;
        const float ca = cosf(a);
        const float sa = sinf(a);
        ray_x = dir_x * ca + side_x * sa;
        ray_y = dir_y * ca + side_y * sa;
        normalize2(&ray_x, &ray_y);
        ray_nx = -ray_y;
        ray_ny = ray_x;
    }
    tick = (uint32_t)floorf(arc->age_s * 9.0f);
    arc->point_count = EEL_ARC_MAX_POINTS;
    for (int i = 0; i <= seg_n; ++i) {
        const float u = (float)i / (float)seg_n;
        const float stem = 1.0f - u;
        const uint32_t h = arc->seed ^ (uint32_t)(i * 0x9e37u) ^ (tick * 0x85ebu);
        const float n0 = hash01_u32(h) * 2.0f - 1.0f;
        const float n1 = hash01_u32(h ^ 0x68c9u) * 2.0f - 1.0f;
        const float wobble = sinf(arc->age_s * (3.6f + 1.8f * n1) - u * (6.0f + 2.2f * arc->start_u) + n0 * 1.4f);
        const float jag = (n0 * 0.80f + wobble * 0.20f) * arc->range * (0.052f + 0.032f * stem) * stem;
        arc->point_x[i] = sx + ray_x * (arc->range * u) + ray_nx * jag;
        arc->point_y[i] = sy + ray_y * (arc->range * u) + ray_ny * jag;
    }
}

static int eel_arc_pulse_is_on(const eel_arc_effect* arc) {
    float phase;
    if (!arc) {
        return 0;
    }
    phase = fmodf(arc->age_s, EEL_ARC_PULSE_PERIOD_S);
    if (phase < 0.0f) {
        phase += EEL_ARC_PULSE_PERIOD_S;
    }
    return (phase <= EEL_ARC_PULSE_ON_S) ? 1 : 0;
}

static void spawn_eel_arc_burst(game_state* g, const enemy* e, int owner_index) {
    const int ray_count = 3;
    const float range = (e->eel_weapon_range > 1.0f) ? e->eel_weapon_range : fmaxf(96.0f, e->radius * 8.4f);
    const float life = (e->eel_weapon_duration_s > 0.2f) ? e->eel_weapon_duration_s : 2.2f;
    if (!g || !e || !e->active) {
        return;
    }
    for (int i = 0; i < ray_count; ++i) {
        const int slot = alloc_eel_arc_slot(g);
        const uint32_t seed = e->visual_seed ^ (uint32_t)(i * 0x517cu) ^ (uint32_t)(rand() & 0xffff);
        eel_arc_effect* arc;
        if (slot < 0) {
            break;
        }
        arc = &g->eel_arcs[slot];
        memset(arc, 0, sizeof(*arc));
        arc->active = 1;
        arc->owner_index = owner_index;
        arc->owner_wave_id = e->wave_id;
        arc->owner_slot_index = e->slot_index;
        arc->seed = seed;
        arc->start_u = clampf(0.24f + (float)i * 0.20f + frands1() * 0.04f, 0.12f, 0.84f);
        arc->base_angle = 0.0f;
        arc->range = range * (0.92f + 0.16f * hash01_u32(seed ^ 0x84du));
        arc->age_s = 0.0f;
        arc->life_s = life * (0.92f + 0.18f * hash01_u32(seed ^ 0x1fd3u));
        arc->damage_timer_s = 0.05f + 0.25f * hash01_u32(seed ^ 0x73eu);
        arc->pulse_prev_on = 0;
        arc->pulse_sound_anchor = (i == 0) ? 1 : 0;
        arc->strike_slot = i;
        eel_arc_build_points(e, arc);
    }
}

static void update_eel_arc_effects(
    game_state* g,
    float dt,
    float su,
    int uses_cylinder,
    float period,
    int* io_player_hit_this_frame
) {
    int active_n = 0;
    if (!g) {
        return;
    }
    for (int i = 0; i < MAX_EEL_ARCS; ++i) {
        eel_arc_effect* arc = &g->eel_arcs[i];
        enemy* owner = NULL;
        if (!arc->active) {
            continue;
        }
        if (arc->owner_index >= 0 && arc->owner_index < MAX_ENEMIES) {
            owner = &g->enemies[arc->owner_index];
        }
        if (!owner || !owner->active || owner->visual_kind != ENEMY_VISUAL_EEL ||
            owner->wave_id != arc->owner_wave_id || owner->slot_index != arc->owner_slot_index) {
            arc->active = 0;
            continue;
        }

        arc->age_s += dt;
        if (arc->damage_timer_s > 0.0f) {
            arc->damage_timer_s -= dt;
        }
        if (arc->age_s >= arc->life_s) {
            arc->active = 0;
            continue;
        }
        active_n += 1;
        {
            const int pulse_on = eel_arc_pulse_is_on(arc);
            if (pulse_on) {
                const float mid_u = clampf(arc->start_u, 0.0f, 1.0f);
                float sx = owner->b.x;
                float sy = owner->b.y;
                float dx;
                float dy;
                float d;
                float near01;
                eel_sample_body_point(owner, mid_u, &sx, &sy);
                dx = uses_cylinder ? wrap_delta(sx, g->player.b.x, period) : (sx - g->player.b.x);
                dy = sy - g->player.b.y;
                if (!arc->pulse_prev_on) {
                    const int slot = clampi(arc->strike_slot, 0, 2);
                    const uint32_t pulse_i = (uint32_t)floorf(arc->age_s / EEL_ARC_PULSE_PERIOD_S);
                    float aim_x = -dx;
                    float aim_y = -dy;
                    float tx = sx;
                    float ty = sy;
                    float dir_x;
                    float dir_y;
                    float side_x;
                    float side_y;
                    float spread = ((float)slot - 1.0f) * 0.20f;
                    float jitter = (hash01_u32(arc->seed ^ (pulse_i * 0x9e37u)) - 0.5f) * 0.12f;
                    if (length2(aim_x, aim_y) < 1.0e-4f) {
                        eel_basis(owner, &aim_x, &aim_y, NULL, NULL);
                    } else {
                        normalize2(&aim_x, &aim_y);
                    }
                    eel_sample_body_point(owner, clampf(arc->start_u + 0.03f, 0.0f, 1.0f), &tx, &ty);
                    dir_x = tx - sx;
                    dir_y = ty - sy;
                    normalize2(&dir_x, &dir_y);
                    if (length2(dir_x, dir_y) < 1.0e-5f) {
                        eel_basis(owner, &dir_x, &dir_y, NULL, NULL);
                    }
                    side_x = -dir_y;
                    side_y = dir_x;
                    {
                        const float ca = dir_x * aim_x + dir_y * aim_y;
                        const float sa = side_x * aim_x + side_y * aim_y;
                        arc->base_angle = atan2f(sa, ca) + spread + jitter;
                    }
                }
                eel_arc_build_points(owner, arc);
                d = sqrtf(dx * dx + dy * dy);
                near01 = 1.0f - smoothstepf(
                    fmaxf(owner->radius * 4.0f, owner->eel_weapon_range * 0.20f),
                    fmaxf(owner->eel_weapon_range * 1.18f, g->world_w * 0.72f),
                    d
                );
                if (near01 > 0.0f) {
                    const float pan = clampf(dx / (g->world_w * 0.45f), -1.0f, 1.0f);
                    g->lightning_active = 1;
                    if (near01 > g->lightning_audio_gain) {
                        g->lightning_audio_gain = near01;
                        g->lightning_audio_pan = pan;
                    }
                }
                if (!arc->pulse_prev_on && arc->pulse_sound_anchor) {
                    game_push_audio_event(g, GAME_AUDIO_EVENT_LIGHTNING, sx, sy);
                }
            }
            arc->pulse_prev_on = pulse_on;
            if (!pulse_on) {
                continue;
            }
        }

        if (!io_player_hit_this_frame || *io_player_hit_this_frame || g->shield_active || g->lives <= 0) {
            continue;
        }
        if (arc->damage_timer_s > 0.0f) {
            continue;
        }
        {
            const float arc_r = 8.0f * su;
            const float rr = arc_r + 10.0f * su;
            const float rr2 = rr * rr;
            for (int pi = 0; pi + 1 < arc->point_count; ++pi) {
                float ax = arc->point_x[pi];
                float ay = arc->point_y[pi];
                float bx = arc->point_x[pi + 1];
                float by = arc->point_y[pi + 1];
                float d2;
                if (uses_cylinder) {
                    ax = g->player.b.x + wrap_delta(ax, g->player.b.x, period);
                    bx = g->player.b.x + wrap_delta(bx, g->player.b.x, period);
                }
                d2 = point_segment_dist_sq(g->player.b.x, g->player.b.y, ax, ay, bx, by);
                if (d2 <= rr2) {
                    apply_player_hit(g, g->player.b.x, g->player.b.y, owner->b.vx, owner->b.vy, su);
                    *io_player_hit_this_frame = 1;
                    arc->damage_timer_s = fmaxf(owner->eel_weapon_damage_interval_s, 0.20f);
                    break;
                }
            }
        }
    }
    g->eel_arc_count = active_n;
}

static void enemy_try_fire(game_state* g, enemy* e, float dt, float su, const leveldef_db* db, int uses_cylinder, float period) {
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
    if (e->missile_cooldown_s > 0.0f) {
        e->missile_cooldown_s -= dt;
    }
    if (e->visual_kind == ENEMY_VISUAL_EEL) {
        const float range = (e->eel_weapon_range > 1.0f) ? e->eel_weapon_range : fmaxf(100.0f * su, g->world_w * 0.20f);
        const float rate = (e->eel_weapon_fire_rate > 0.01f) ? e->eel_weapon_fire_rate : 0.30f;
        const float dx = uses_cylinder ? wrap_delta(g->player.b.x, e->b.x, period) : (g->player.b.x - e->b.x);
        const float dy = g->player.b.y - e->b.y;
        const float d2 = dx * dx + dy * dy;
        const float close_r = range * 0.34f;
        int in_fire_region = 0;
        if (uses_cylinder) {
            in_fire_region = (fabsf(dx) <= period * 0.26f) ? 1 : 0;
        } else {
            in_fire_region =
                (fabsf(dx) <= g->world_w * 0.56f) &&
                (fabsf(dy) <= g->world_h * 0.56f);
        }
        if (e->fire_cooldown_s > 0.0f || !in_fire_region || d2 > range * range) {
            if (e->fire_cooldown_s <= 0.0f) {
                e->fire_cooldown_s = 0.05f + frand01() * 0.08f;
            }
            return;
        }
        {
            const float try_window_s = 0.18f;
            const float p_try = 1.0f - expf(-rate * try_window_s);
            if (d2 <= close_r * close_r || frand01() < p_try) {
                const int owner_index = (int)(e - g->enemies);
                spawn_eel_arc_burst(g, e, owner_index);
                e->fire_cooldown_s = 0.60f + frand01() * 0.45f;
            } else {
                e->fire_cooldown_s = 0.08f + frand01() * 0.12f;
            }
        }
        return;
    }
    if (e->visual_kind == ENEMY_VISUAL_MANTA && e->missile_charge_duration_s > 0.0f) {
        e->missile_charge_s += dt;
        if (e->missile_charge_s >= e->missile_charge_duration_s) {
            float dir_x = uses_cylinder ? wrap_delta(g->player.b.x, e->b.x, period) : (g->player.b.x - e->b.x);
            float dir_y = g->player.b.y - e->b.y;
            normalize2(&dir_x, &dir_y);
            if (game_spawn_enemy_missile(
                    g,
                    e->b.x + dir_x * (e->radius + 8.0f * su),
                    e->b.y + dir_y * (e->radius + 8.0f * su),
                    dir_x,
                    dir_y,
                    420.0f * su,
                    115.0f,
                    3.4f,
                    22.0f * su,
                    72.0f * su)) {
                game_push_audio_event(g, GAME_AUDIO_EVENT_ENEMY_FIRE, e->b.x, e->b.y);
                if (e->missile_ammo > 0) {
                    e->missile_ammo -= 1;
                }
            }
            e->missile_charge_s = 0.0f;
            e->missile_charge_duration_s = 0.0f;
            e->missile_cooldown_s = frand_range(4.8f, 8.2f);
        }
        return;
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
    if (!game_line_of_sight_clear(
            g,
            e->b.x,
            e->b.y,
            g->player.b.x,
            g->player.b.y,
            fmaxf(2.0f, e->radius * 0.22f))) {
        enemy_reset_fire_cooldown(w, &t, e);
        return;
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

    if (e->visual_kind == ENEMY_VISUAL_MANTA) {
        if (e->missile_ammo != 0 && e->missile_cooldown_s <= 0.0f) {
            const float mx = uses_cylinder ? wrap_delta(g->player.b.x, e->b.x, period) : (g->player.b.x - e->b.x);
            const float my = g->player.b.y - e->b.y;
            const float d2 = mx * mx + my * my;
            const float start_r = g->world_w * 0.18f;
            const float end_r = g->world_w * 0.52f;
            if (d2 >= start_r * start_r && d2 <= end_r * end_r) {
                e->missile_charge_duration_s = frand_range(0.55f, 0.95f);
                e->missile_charge_s = 0.0f;
                e->missile_cooldown_s = frand_range(4.8f, 8.2f);
                e->fire_cooldown_s = fmaxf(e->fire_cooldown_s, 0.45f);
                return;
            }
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
    const int is_manta = (e->visual_kind == ENEMY_VISUAL_MANTA);
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
                if (is_manta) {
                    const float flap_freq = (e->visual_param_a > 0.01f) ? e->visual_param_a : 1.5f;
                    const float glide_phase = g->t * flap_freq + e->visual_phase;
                    const float wing = 0.5f + 0.5f * sinf(glide_phase);
                    const float lane_dir = (e->lane_dir < 0.0f) ? -1.0f : 1.0f;
                    const float dx_to_player = uses_cylinder ? wrap_delta(g->player.b.x, e->b.x, period) : (g->player.b.x - e->b.x);
                    const float wing_zero_gate = fabsf(sinf(e->ai_timer_s * (2.2f + flap_freq) + e->visual_phase));
                    /* Mantas should not reverse direction while visible. */
                    if (!uses_cylinder) {
                        /* camera_x is the screen center in world coordinates. */
                        const float pad_x = fmaxf(fmaxf(48.0f * su, e->radius * 3.0f), g->world_w * 0.10f);
                        const float half_w = g->world_w * 0.5f;
                        const float left = g->camera_x - half_w - pad_x;
                        const float right = g->camera_x + half_w + pad_x;
                        const int offscreen_x = (e->b.x < left) || (e->b.x > right);
                        if (offscreen_x && e->ai_timer_s > 0.85f && wing_zero_gate <= 0.10f) {
                            e->lane_dir = (dx_to_player < 0.0f) ? -1.0f : 1.0f;
                            e->ai_timer_s = 0.0f;
                        }
                    }
                    {
                        const float target_vx = lane_dir * lerpf(0.72f, 1.05f, wing) * e->max_speed;
                        steer_to_velocity(&e->b, target_vx, 0.0f, e->accel, 1.05f);
                    }
                    e->b.y = e->home_y;
                    e->b.vy = 0.0f;
                    break;
                }
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
        if (is_manta) {
            const float lead = 0.32f;
            const float tx = g->player.b.x + g->player.b.vx * lead;
            const float ty = g->player.b.y + g->player.b.vy * lead;
            float to_x = uses_cylinder ? wrap_delta(tx, e->b.x, period) : (tx - e->b.x);
            float to_y = ty - e->b.y;
            float dir_x = to_x;
            float dir_y = to_y;
            normalize2(&dir_x, &dir_y);
            {
                const float glide_arc = sinf(g->t * 0.45f + e->visual_phase) * 0.40f;
                const float px = -dir_y;
                const float py = dir_x;
                dir_x += px * glide_arc;
                dir_y += py * glide_arc;
                normalize2(&dir_x, &dir_y);
            }
            steer_to_velocity(
                &e->b,
                dir_x * (e->max_speed * 1.08f),
                0.0f,
                e->accel * 0.95f,
                1.35f
            );
            e->b.y = e->home_y;
            e->b.vy = 0.0f;
            if (e->ai_timer_s > fmaxf(e->break_delay_s, 2.2f)) {
                e->state = ENEMY_STATE_FORMATION;
                e->ai_timer_s = 0.0f;
                e->break_delay_s = 0.0f;
            }
            return;
        }
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

static void update_enemy_eel(game_state* g, enemy* e, float dt, int uses_cylinder, float period, float su) {
    float desired_x;
    float desired_y;
    float avoid_x = 0.0f;
    float avoid_y = 0.0f;
    float to_player_x;
    float to_player_y;
    float heading;
    float speed;
    float min_speed;
    float max_turn;
    float desired_a;
    float target_speed;
    if (!g || !e || dt <= 0.0f) {
        return;
    }

    e->ai_timer_s += dt;
    heading = e->eel_heading_rad;
    if (length2(e->facing_x, e->facing_y) > 1.0e-5f) {
        heading = atan2f(e->facing_y, e->facing_x);
    } else if (length2(e->b.vx, e->b.vy) > 1.0e-5f) {
        heading = atan2f(e->b.vy, e->b.vx);
    }

    {
        const float lead_s = 0.26f;
        const float tx = g->player.b.x + g->player.b.vx * lead_s;
        const float ty = g->player.b.y + g->player.b.vy * lead_s;
        to_player_x = uses_cylinder ? wrap_delta(tx, e->b.x, period) : (tx - e->b.x);
        to_player_y = ty - e->b.y;
    }
    normalize2(&to_player_x, &to_player_y);

    if (!uses_cylinder) {
        const float pad_x = fmaxf(32.0f * su, e->radius * 2.4f);
        const float left = g->camera_x - g->world_w * 0.50f + pad_x;
        const float right = g->camera_x + g->world_w * 0.50f - pad_x;
        if (e->b.x < left) {
            avoid_x += clampf((left - e->b.x) / fmaxf(pad_x, 1.0f), 0.0f, 1.2f);
        } else if (e->b.x > right) {
            avoid_x -= clampf((e->b.x - right) / fmaxf(pad_x, 1.0f), 0.0f, 1.2f);
        }
    }
    {
        const float pad_y = fmaxf(30.0f * su, e->radius * 2.2f);
        if (e->b.y < pad_y) {
            avoid_y += clampf((pad_y - e->b.y) / fmaxf(pad_y, 1.0f), 0.0f, 1.2f);
        } else if (e->b.y > g->world_h - pad_y) {
            avoid_y -= clampf((e->b.y - (g->world_h - pad_y)) / fmaxf(pad_y, 1.0f), 0.0f, 1.2f);
        }
    }
    if (!uses_cylinder && g->render_style != LEVEL_RENDER_CYLINDER) {
        float sx = 0.0f;
        float sy = 0.0f;
        game_structure_avoidance_vector(
            g,
            e->b.x,
            e->b.y,
            fmaxf(8.0f * su, e->radius * 0.95f),
            fmaxf(32.0f * su, e->radius * 2.8f),
            &sx,
            &sy
        );
        if (fabsf(sx) > 1.0e-5f || fabsf(sy) > 1.0e-5f) {
            normalize2(&sx, &sy);
            avoid_x += sx * 1.6f;
            avoid_y += sy * 1.6f;
        }
    }
    for (int i = 0; i < MAX_ENEMIES; ++i) {
        const enemy* o = &g->enemies[i];
        float dx;
        float dy;
        float d2;
        if (!o->active || o == e || o->visual_kind != ENEMY_VISUAL_EEL) {
            continue;
        }
        dx = uses_cylinder ? wrap_delta(e->b.x, o->b.x, period) : (e->b.x - o->b.x);
        dy = e->b.y - o->b.y;
        d2 = dx * dx + dy * dy;
        if (d2 < 1.0e-4f || d2 > (110.0f * su) * (110.0f * su)) {
            continue;
        }
        avoid_x += (dx / d2) * (26.0f * su);
        avoid_y += (dy / d2) * (26.0f * su);
    }

    desired_x = to_player_x;
    desired_y = to_player_y;
    {
        const float weave = sinf(e->ai_timer_s * (0.80f + e->eel_wave_freq * 0.25f) + e->visual_phase) * 0.30f;
        const float px = -desired_y;
        const float py = desired_x;
        desired_x += px * weave;
        desired_y += py * weave;
    }
    desired_x += avoid_x;
    desired_y += avoid_y;
    if (length2(desired_x, desired_y) < 1.0e-5f) {
        desired_x = cosf(heading);
        desired_y = sinf(heading);
    }
    normalize2(&desired_x, &desired_y);

    desired_a = atan2f(desired_y, desired_x);
    max_turn = (e->eel_turn_rate_rad > 0.05f) ? e->eel_turn_rate_rad : (420.0f * (3.14159265359f / 180.0f));
    {
        const float max_da = max_turn * dt;
        float da = wrap_angle_pi(desired_a - heading);
        if (da > max_da) da = max_da;
        if (da < -max_da) da = -max_da;
        heading += da;
    }
    e->eel_heading_rad = heading;
    e->facing_x = cosf(heading);
    e->facing_y = sinf(heading);
    normalize2(&e->facing_x, &e->facing_y);
    e->lane_dir = (e->facing_x < 0.0f) ? -1.0f : 1.0f;

    speed = length2(e->b.vx, e->b.vy);
    min_speed = (e->eel_min_speed > 1.0f) ? e->eel_min_speed : fmaxf(70.0f * su, e->max_speed * 0.42f);
    target_speed = e->max_speed * (0.70f + 0.18f * sinf(e->ai_timer_s * 0.65f + e->visual_phase));
    target_speed += clampf(length2(avoid_x, avoid_y), 0.0f, 1.2f) * e->max_speed * 0.35f;
    if (speed < min_speed) {
        target_speed = fmaxf(target_speed, min_speed + (min_speed - speed) * 0.35f);
    }
    target_speed = clampf(target_speed, min_speed, fmaxf(min_speed, e->max_speed));
    steer_to_velocity(
        &e->b,
        e->facing_x * target_speed,
        e->facing_y * target_speed,
        e->accel * 1.42f,
        0.86f
    );
}

static void update_enemy_kamikaze(game_state* g, enemy* e, float dt, int uses_cylinder, float period, float su) {
    const float lead = 0.12f;
    const float tx = g->player.b.x + g->player.b.vx * lead;
    const float ty = g->player.b.y + g->player.b.vy * lead;
    float dir_x = uses_cylinder ? wrap_delta(tx, e->b.x, period) : (tx - e->b.x);
    float dir_y = ty - e->b.y;
    const float dist_to_player = length2(dir_x, dir_y);
    float player_dx = uses_cylinder ? wrap_delta(g->player.b.x, e->b.x, period) : (g->player.b.x - e->b.x);
    float player_dy = g->player.b.y - e->b.y;
    const float dist_to_player_now = length2(player_dx, player_dy);
    const int has_los = game_line_of_sight_clear(
        g,
        e->b.x,
        e->b.y,
        g->player.b.x,
        g->player.b.y,
        fmaxf(2.0f, e->radius * 0.28f)
    );
    float avoid_x = 0.0f;
    float avoid_y = 0.0f;
    if (!uses_cylinder && g->render_style != LEVEL_RENDER_CYLINDER) {
        game_structure_avoidance_vector(
            g,
            e->b.x,
            e->b.y,
            fmaxf(8.0f * su, e->radius),
            fmaxf(28.0f * su, e->radius * 2.4f),
            &avoid_x,
            &avoid_y
        );
        if (fabsf(avoid_x) > 1.0e-5f || fabsf(avoid_y) > 1.0e-5f) {
            normalize2(&avoid_x, &avoid_y);
        } else {
            avoid_x = 0.0f;
            avoid_y = 0.0f;
        }
    }
    normalize2(&dir_x, &dir_y);
    normalize2(&player_dx, &player_dy);
    {
        float fx = e->facing_x;
        float fy = e->facing_y;
        normalize2(&fx, &fy);
        if (fx * fx + fy * fy < 1e-6f) {
            fx = dir_x;
            fy = dir_y;
        }
        /* Keep kamikaze from drifting away forever: if far offscreen on defender levels,
         * force a reacquire turn toward player. */
        if (!uses_cylinder && fabsf(g->player.b.x - e->b.x) > g->world_w * 0.95f) {
            e->state = ENEMY_STATE_KAMIKAZE_COIL;
            e->ai_timer_s = 0.0f;
            e->break_delay_s = 0.35f + frand01() * 0.25f;
            e->facing_x = dir_x;
            e->facing_y = dir_y;
            e->kamikaze_tail = fminf(e->kamikaze_tail, 0.22f);
            e->kamikaze_tail_start = e->kamikaze_tail;
        }
    }

    if (e->state == ENEMY_STATE_KAMIKAZE_COIL) {
        const int strike_range = (dist_to_player_now <= g->world_w * 0.24f) ? 1 : 0;
        const float turn_rate = clampf(dt * (strike_range ? 22.0f : 12.0f), 0.0f, 1.0f);
        {
            float turn_x = strike_range ? player_dx : dir_x;
            float turn_y = strike_range ? player_dy : dir_y;
            if (!strike_range) {
                const float far_d = g->world_w * 0.72f;
                const float near_d = g->world_w * 0.26f;
                const float rand_w = clampf((dist_to_player_now - near_d) / fmaxf(far_d - near_d, 1.0f), 0.0f, 1.0f);
                if (rand_w > 0.0f) {
                    const uint32_t tick = (uint32_t)floorf((e->ai_timer_s + (float)e->slot_index * 0.17f) * 2.5f);
                    const uint32_t seed = (uint32_t)(e->wave_id * 73856093u) ^ (uint32_t)(e->slot_index * 19349663u) ^ tick;
                    const float r = hash01_u32(seed);
                    const float sign = (r < 0.5f) ? -1.0f : 1.0f;
                    const float mag = (0.10f + 0.60f * r) * rand_w;
                    const float px = -turn_y;
                    const float py = turn_x;
                    turn_x += px * sign * mag;
                    turn_y += py * sign * mag;
                    normalize2(&turn_x, &turn_y);
                }
            }
            if (avoid_x != 0.0f || avoid_y != 0.0f) {
                const float avoid_w = strike_range ? 0.10f : (has_los ? 0.30f : 0.55f);
                turn_x += avoid_x * avoid_w;
                turn_y += avoid_y * avoid_w;
                normalize2(&turn_x, &turn_y);
            }
            e->facing_x += (turn_x - e->facing_x) * turn_rate;
            e->facing_y += (turn_y - e->facing_y) * turn_rate;
        }
        normalize2(&e->facing_x, &e->facing_y);
        e->kamikaze_thrust = 0.0f;
        e->ai_timer_s += dt;
        {
            const float u = clampf(e->ai_timer_s / fmaxf(e->break_delay_s, 0.10f), 0.0f, 1.0f);
            e->kamikaze_tail = lerpf(e->kamikaze_tail_start, 0.10f, u);
        }
        {
            float drive_x = e->facing_x;
            float drive_y = e->facing_y;
            if (avoid_x != 0.0f || avoid_y != 0.0f) {
                drive_x += avoid_x * 0.25f;
                drive_y += avoid_y * 0.25f;
                normalize2(&drive_x, &drive_y);
            }
            steer_to_velocity(
            &e->b,
            drive_x * (e->max_speed * 0.10f),
            drive_y * (e->max_speed * 0.10f),
            e->accel * 1.05f,
            2.7f
            );
        }
        {
            const float screen_x = g->world_w * 0.52f;
            const float screen_y = g->world_h * 0.52f;
            const int same_screen = (fabsf(uses_cylinder ? wrap_delta(g->player.b.x, e->b.x, period) : (g->player.b.x - e->b.x)) <= screen_x) &&
                                    (fabsf(g->player.b.y - e->b.y) <= screen_y);
            if (same_screen && strike_range && has_los) {
                /* Kill-strike entry gate: only if target is within +/-90 degrees of facing. */
                e->kamikaze_strike_x = g->player.b.x;
                e->kamikaze_strike_y = g->player.b.y;
                {
                    float sx = uses_cylinder ? wrap_delta(e->kamikaze_strike_x, e->b.x, period) : (e->kamikaze_strike_x - e->b.x);
                    float sy = e->kamikaze_strike_y - e->b.y;
                    normalize2(&sx, &sy);
                    {
                        const float facing_dot = e->facing_x * sx + e->facing_y * sy;
                        if (facing_dot >= 0.0f) {
                            e->state = ENEMY_STATE_KAMIKAZE_STRIKE;
                            e->ai_timer_s = 0.0f;
                            e->break_delay_s = 0.56f + frand01() * 0.22f; /* strike dash duration after turn-in */
                            e->kamikaze_is_turning = 1;
                            e->kamikaze_thrust = 0.0f;
                            e->kamikaze_tail = 0.92f;
                            return;
                        }
                    }
                }
            }
            if (same_screen && has_los && e->ai_timer_s > 0.20f) {
                const float near01 = clampf(1.0f - dist_to_player / (g->world_w * 0.60f), 0.0f, 1.0f);
                const float lunge_rate = 0.35f + near01 * 2.05f; /* events/second */
                const float p_dt = 1.0f - expf(-lunge_rate * fmaxf(dt, 0.0f));
                const float facing_dot = e->facing_x * dir_x + e->facing_y * dir_y;
                if (facing_dot > 0.55f && frand01() < p_dt) {
                    e->ai_timer_s = e->break_delay_s;
                }
            }
        }
        if (e->ai_timer_s >= e->break_delay_s) {
            e->state = ENEMY_STATE_KAMIKAZE_THRUST;
            e->ai_timer_s = 0.0f;
            {
                const float glide_scale = fmaxf(e->kamikaze_glide_scale, 0.1f);
                e->break_delay_s = (1.15f + frand01() * 0.55f) * glide_scale;
            }
            e->kamikaze_thrust = 1.0f;
            e->kamikaze_tail = 1.0f;
        }
        return;
    }

    if (e->state == ENEMY_STATE_KAMIKAZE_STRIKE) {
        float sx = uses_cylinder ? wrap_delta(e->kamikaze_strike_x, e->b.x, period) : (e->kamikaze_strike_x - e->b.x);
        float sy = e->kamikaze_strike_y - e->b.y;
        normalize2(&sx, &sy);
        if (!game_line_of_sight_clear(
                g,
                e->b.x,
                e->b.y,
                e->kamikaze_strike_x,
                e->kamikaze_strike_y,
                fmaxf(2.0f, e->radius * 0.28f))) {
            e->state = ENEMY_STATE_KAMIKAZE_COIL;
            e->ai_timer_s = 0.0f;
            e->break_delay_s = 0.55f + frand01() * 0.35f;
            e->kamikaze_thrust = 0.0f;
            e->kamikaze_tail = 0.22f;
            e->kamikaze_tail_start = e->kamikaze_tail;
            e->kamikaze_is_turning = 0;
            return;
        }
        if (e->kamikaze_is_turning) {
            const float turn_rate = clampf(dt * 30.0f, 0.0f, 1.0f);
            const float facing_dot = e->facing_x * sx + e->facing_y * sy;
            e->facing_x += (sx - e->facing_x) * turn_rate;
            e->facing_y += (sy - e->facing_y) * turn_rate;
            normalize2(&e->facing_x, &e->facing_y);
            e->kamikaze_thrust = 0.0f;
            e->kamikaze_tail = 0.96f;
            {
                float drive_x = e->facing_x;
                float drive_y = e->facing_y;
                if (avoid_x != 0.0f || avoid_y != 0.0f) {
                    drive_x += avoid_x * 0.20f;
                    drive_y += avoid_y * 0.20f;
                    normalize2(&drive_x, &drive_y);
                }
                steer_to_velocity(
                &e->b,
                drive_x * (e->max_speed * 0.22f),
                drive_y * (e->max_speed * 0.22f),
                e->accel * 2.2f,
                0.55f
                );
            }
            if (facing_dot >= 0.96f) {
                e->kamikaze_is_turning = 0;
                e->ai_timer_s = 0.0f;
                e->facing_x = sx;
                e->facing_y = sy;
            }
            return;
        }
        {
            const float thrust_scale = fmaxf(e->kamikaze_thrust_scale, 0.1f);
            const float target_v = e->max_speed * (2.25f * thrust_scale);
            e->kamikaze_thrust = 1.0f;
            e->kamikaze_tail = 1.0f;
            e->ai_timer_s += dt;
            {
                float drive_x = e->facing_x;
                float drive_y = e->facing_y;
                if (avoid_x != 0.0f || avoid_y != 0.0f) {
                    drive_x += avoid_x * 0.10f;
                    drive_y += avoid_y * 0.10f;
                    normalize2(&drive_x, &drive_y);
                }
                steer_to_velocity(
                &e->b,
                drive_x * target_v,
                drive_y * target_v,
                e->accel * 4.2f,
                0.14f
                );
            }
        }
        if (e->ai_timer_s >= e->break_delay_s) {
            /* End strike into glide, not immediate coil, for smoother speed drop-off. */
            e->state = ENEMY_STATE_KAMIKAZE_THRUST;
            e->ai_timer_s = 0.0f;
            {
                const float glide_scale = fmaxf(e->kamikaze_glide_scale, 0.1f);
                e->break_delay_s = (0.82f + frand01() * 0.48f) * glide_scale;
            }
            e->kamikaze_thrust = 0.85f;
            e->kamikaze_tail = 1.0f;
            e->kamikaze_tail_start = e->kamikaze_tail;
            e->kamikaze_is_turning = 0;
        }
        return;
    }

    e->ai_timer_s += dt;
    {
        const float u = clampf(e->ai_timer_s / fmaxf(e->break_delay_s, 0.10f), 0.0f, 1.0f);
        const float launch = expf(-u * 4.4f);
        const float thrust_scale = fmaxf(e->kamikaze_thrust_scale, 0.1f);
        const float target_v = e->max_speed * (0.32f + launch * (1.75f * thrust_scale));
        float tail01;
        if (u < 0.09f) {
            /* Quick ramp prevents a hard discontinuity at phase switch. */
            const float grow_u = clampf(u / 0.09f, 0.0f, 1.0f);
            const float grow_s = grow_u * grow_u * (3.0f - 2.0f * grow_u);
            tail01 = lerpf(0.10f, 1.0f, grow_s);
        } else if (u > 0.72f) {
            const float tail_u = clampf((u - 0.72f) / 0.28f, 0.0f, 1.0f);
            const float tail_s = tail_u * tail_u * (3.0f - 2.0f * tail_u);
            tail01 = lerpf(1.0f, 0.24f, tail_s);
        } else {
            tail01 = 1.0f;
        }
        e->kamikaze_thrust = launch;
        e->kamikaze_tail = tail01;
        {
            float drive_x = e->facing_x;
            float drive_y = e->facing_y;
            if (avoid_x != 0.0f || avoid_y != 0.0f) {
                drive_x += avoid_x * 0.15f;
                drive_y += avoid_y * 0.15f;
                normalize2(&drive_x, &drive_y);
            }
            steer_to_velocity(
            &e->b,
            drive_x * target_v,
            drive_y * target_v,
            e->accel * (1.00f + launch * 1.45f),
            0.42f + (1.0f - launch) * 1.10f
            );
        }
    }
    if (e->ai_timer_s >= e->break_delay_s) {
        e->state = ENEMY_STATE_KAMIKAZE_COIL;
        e->ai_timer_s = 0.0f;
        e->break_delay_s = 0.85f + frand01() * 0.75f;
        e->kamikaze_thrust = 0.0f;
        e->kamikaze_tail = 0.22f;
        e->kamikaze_tail_start = e->kamikaze_tail;
    }
}

static void update_enemy_swarm(game_state* g, enemy* e, float dt, int uses_cylinder, float period, float su) {
    float sep_x = 0.0f, sep_y = 0.0f, ali_x = 0.0f, ali_y = 0.0f, coh_x = 0.0f, coh_y = 0.0f;
    int ali_n = 0, coh_n = 0;
    float sep_r = (e->swarm_sep_r > 1.0f) ? e->swarm_sep_r : (70.0f * su);
    float ali_r = (e->swarm_ali_r > 1.0f) ? e->swarm_ali_r : (180.0f * su);
    float coh_r = (e->swarm_coh_r > 1.0f) ? e->swarm_coh_r : (220.0f * su);
    float jelly_speed_scale = 1.0f;
    float jelly_sep_w_scale = 1.0f;
    float jelly_ali_w_scale = 1.0f;
    float jelly_coh_w_scale = 1.0f;
    float jelly_turn_gate = 1.0f;
    float jelly_speed_blend = 1.0f;
    float jelly_expand_phase = 0.0f;
    float jelly_compress_phase = 0.0f;

    if (e->visual_kind == ENEMY_VISUAL_JELLY) {
        const float pulse_freq = (e->visual_param_a > 0.01f) ? e->visual_param_a : 2.0f;
        const float phase = e->ai_timer_s * pulse_freq + e->visual_phase;
        const float pulse = sinf(phase);
        const float pulse01 = 0.5f + 0.5f * pulse;
        jelly_expand_phase = clampf(pulse, 0.0f, 1.0f);
        jelly_compress_phase = clampf(-pulse, 0.0f, 1.0f);
        /* Jelly school "breathes": wider + slower on expand, tighter on compression. */
        const float shape = 0.86f + 0.34f * pulse01;
        sep_r *= (0.90f + 0.42f * shape);
        ali_r *= (0.92f + 0.24f * shape);
        coh_r *= (0.90f + 0.26f * shape);
        /* Acceleration bias during expansion; slowdown during compression. */
        jelly_speed_scale = 0.60f + 0.90f * jelly_expand_phase;
        jelly_sep_w_scale = 0.90f + 0.38f * shape;
        jelly_ali_w_scale = 0.86f + 0.28f * (1.0f - shape);
        jelly_coh_w_scale = 0.88f + 0.34f * shape;
        {
            /* Turning occurs during compression; strongest in deep compression. */
            float turn_window = jelly_compress_phase * jelly_compress_phase;
            turn_window = turn_window * (3.0f - 2.0f * turn_window);
            jelly_turn_gate = 0.05f + 0.95f * turn_window;
        }
        /* Glide tail and slowdown during compression. */
        jelly_speed_blend = lerpf(0.14f, 0.56f, jelly_expand_phase);
    }

    const float sep_r2 = sep_r * sep_r;
    const float ali_r2 = ali_r * ali_r;
    const float coh_r2 = coh_r * coh_r;
    for (size_t i = 0; i < MAX_ENEMIES; ++i) {
        const enemy* o = &g->enemies[i];
        if (!o->active || o == e || o->archetype != ENEMY_ARCH_SWARM) {
            continue;
        }
        float dx = uses_cylinder ? wrap_delta(o->b.x, e->b.x, period) : (o->b.x - e->b.x);
        float dy = o->b.y - e->b.y;
        float d2 = dx * dx + dy * dy;
        if (d2 < 1e-4f) {
            const float phase = (float)(((e->slot_index * 97 + o->slot_index * 57 + e->wave_id * 17) & 255)) * 0.024543693f;
            dx = cosf(phase) * 0.01f;
            dy = sinf(phase) * 0.01f;
            d2 = dx * dx + dy * dy;
        }
        if (d2 < sep_r2) {
            const float near_falloff = 1.0f - (d2 / sep_r2);
            sep_x -= (dx / d2) * near_falloff;
            sep_y -= (dy / d2) * near_falloff;
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
    float avoid_boost = 0.0f;
    float player_avoid_boost = 0.0f;
    {
        const float lead = 0.22f;
        const float px = g->player.b.x + g->player.b.vx * lead;
        const float py = g->player.b.y + g->player.b.vy * lead;
        const float aware_r = 360.0f * su;
        const float aware_r2 = aware_r * aware_r;
        const float personal_r = 150.0f * su;
        const float personal_r2 = personal_r * personal_r;
        float boost = 0.0f;
        if (uses_cylinder) {
            const float wrapped_px = e->b.x - wrap_delta(e->b.x, px, period);
            boost = add_inverse_avoid_force(
                e->b.x, e->b.y, wrapped_px, py,
                aware_r2, personal_r2, 1.35f, 3.10f,
                &avoid_x, &avoid_y
            );
        } else {
            boost = add_inverse_avoid_force(
                e->b.x, e->b.y, px, py,
                aware_r2, personal_r2, 1.35f, 3.10f,
                &avoid_x, &avoid_y
            );
        }
        {
            /* Extra urgency when trajectories are converging toward imminent collision. */
            float rel_x = uses_cylinder ? wrap_delta(px, e->b.x, period) : (px - e->b.x);
            float rel_y = py - e->b.y;
            float rel_vx = g->player.b.vx - e->b.vx;
            float rel_vy = g->player.b.vy - e->b.vy;
            float rel_v2 = rel_vx * rel_vx + rel_vy * rel_vy;
            if (rel_v2 > 1.0e-5f) {
                float ttc = -((rel_x * rel_vx + rel_y * rel_vy) / rel_v2);
                if (ttc > 0.0f && ttc < 0.70f) {
                    float cx = rel_x + rel_vx * ttc;
                    float cy = rel_y + rel_vy * ttc;
                    float cd2 = cx * cx + cy * cy;
                    float avoid_dist = personal_r + fmaxf(e->radius, 12.0f * su);
                    if (cd2 < avoid_dist * avoid_dist) {
                        float away_x = -rel_x;
                        float away_y = -rel_y;
                        const float away_l2 = away_x * away_x + away_y * away_y;
                        if (away_l2 > 1.0e-6f) {
                            const float away_l = sqrtf(away_l2);
                            const float urgency = (1.0f - clampf(ttc / 0.70f, 0.0f, 1.0f));
                            away_x /= away_l;
                            away_y /= away_l;
                            avoid_x += away_x * (2.1f * urgency);
                            avoid_y += away_y * (2.1f * urgency);
                            {
                                float side_x = -away_y;
                                float side_y = away_x;
                                const float side_sign = (((e->wave_id * 31 + e->slot_index) & 1) == 0) ? 1.0f : -1.0f;
                                avoid_x += side_x * (1.65f * urgency * side_sign);
                                avoid_y += side_y * (1.65f * urgency * side_sign);
                            }
                            if (urgency > player_avoid_boost) {
                                player_avoid_boost = urgency;
                            }
                        }
                    }
                }
            }
        }
        if (boost > player_avoid_boost) {
            player_avoid_boost = boost;
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
        const leveldef_level* lvl = game_current_leveldef(g);
        if (!uses_cylinder && g->render_style != LEVEL_RENDER_CYLINDER && lvl && lvl->structure_count > 0) {
            float fwd_x = e->b.vx;
            float fwd_y = e->b.vy;
            const float fwd_v = length2(fwd_x, fwd_y);
            if (fwd_v > 1.0e-4f) {
                fwd_x /= fwd_v;
                fwd_y /= fwd_v;
            } else {
                fwd_x = (e->facing_x != 0.0f) ? e->facing_x : ((e->swarm_goal_dir < 0.0f) ? -1.0f : 1.0f);
                fwd_y = e->facing_y;
                normalize2(&fwd_x, &fwd_y);
            }
            for (int i = 0; i < lvl->structure_count && i < LEVELDEF_MAX_STRUCTURES; ++i) {
                const leveldef_structure_instance* st = &lvl->structures[i];
                float min_x;
                float min_y;
                float max_x;
                float max_y;
                float cx;
                float cy;
                float w;
                float h;
                float structure_r;
                float personal_r;
                float aware_r;
                float rel_x;
                float rel_y;
                float along;
                float lateral_x;
                float lateral_y;
                float lateral2;
                float lateral_limit;
                float lookahead;
                float course_w;
                if (st->layer != 0) {
                    continue;
                }
                structure_aabb_world_enemy(g, st, &min_x, &min_y, &max_x, &max_y);
                cx = 0.5f * (min_x + max_x);
                cy = 0.5f * (min_y + max_y);
                w = fmaxf(max_x - min_x, 1.0f);
                h = fmaxf(max_y - min_y, 1.0f);
                structure_r = 0.68f * sqrtf(w * w + h * h);
                personal_r = structure_r + fmaxf(e->radius, 6.0f * su);
                aware_r = personal_r + fmaxf(96.0f * su, 0.78f * structure_r);
                rel_x = cx - e->b.x;
                rel_y = cy - e->b.y;
                along = rel_x * fwd_x + rel_y * fwd_y;
                lateral_x = rel_x - along * fwd_x;
                lateral_y = rel_y - along * fwd_y;
                lateral2 = lateral_x * lateral_x + lateral_y * lateral_y;
                lateral_limit = personal_r + fmaxf(8.0f * su, e->radius * 0.35f);
                lookahead = aware_r + fmaxf(40.0f * su, e->radius * 1.5f);
                if (along < -e->radius) {
                    continue;
                }
                if (along > lookahead) {
                    continue;
                }
                if (lateral2 > lateral_limit * lateral_limit) {
                    continue;
                }
                course_w = 1.0f - clampf(along / fmaxf(lookahead, 1.0f), 0.0f, 1.0f);
                {
                    float boost = add_inverse_avoid_force(
                        e->b.x,
                        e->b.y,
                        cx,
                        cy,
                        aware_r * aware_r,
                        personal_r * personal_r,
                        1.0f * course_w,
                        1.75f * course_w,
                        &avoid_x,
                        &avoid_y
                    );
                    if (boost > avoid_boost) {
                        avoid_boost = boost;
                    }
                }
            }
        }
    }

    {
        const float goal_dir = (e->swarm_goal_dir < 0.0f) ? -1.0f : 1.0f;
        const float los_radius = fmaxf(4.0f * su, e->radius * 0.65f);
        const int player_los_clear = uses_cylinder ? 1 : game_line_of_sight_clear(
            g, e->b.x, e->b.y, g->player.b.x, g->player.b.y, los_radius
        );
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
        float wander_w_eff = wander_w;
        const float wander_freq = (e->swarm_wander_freq > 0.01f) ? e->swarm_wander_freq : 0.9f;
        const float steer_drag = (e->swarm_drag > 0.01f) ? e->swarm_drag : 1.3f;

        if (e->visual_kind == ENEMY_VISUAL_JELLY) {
            sep_w *= jelly_sep_w_scale;
            ali_w *= jelly_ali_w_scale;
            coh_w *= jelly_coh_w_scale;
        }

        if (uses_cylinder) {
            goal_x = wrap_delta(g->player.b.x + goal_dir * 280.0f * su, e->b.x, period);
        }
        {
            const float goal_amp = (e->swarm_goal_amp > 1.0f) ? e->swarm_goal_amp : (80.0f * su);
            const float goal_freq = (e->swarm_goal_freq > 0.01f) ? e->swarm_goal_freq : 0.70f;
            goal_y = (g->player.b.y + sinf(g->t * goal_freq + (float)e->slot_index * 0.35f) * goal_amp) - e->b.y;
        }

        {
            const float avoid_boost_total = fmaxf(avoid_boost, player_avoid_boost);
            const float phase = (float)(e->wave_id & 31) * 0.61f;
            const float breathe = 0.5f + 0.5f * sinf(g->t * 0.85f + phase);
            const float tightness = 0.80f + 0.40f * breathe;
            sep_w *= (1.20f - 0.28f * tightness);
            ali_w *= (0.90f + 0.25f * tightness);
            coh_w *= tightness;
            goal_w *= (0.92f + 0.18f * tightness);
            avoid_w *= (1.0f + 2.4f * avoid_boost_total);
            goal_w *= (1.0f - 0.45f * avoid_boost_total);
            if (!player_los_clear) {
                goal_x = goal_dir;
                goal_y = 0.0f;
                goal_w *= 0.38f;
                if (wander_w_eff < 0.20f) {
                    wander_w_eff = 0.20f;
                }
            }
        }

        {
            const float wp = g->t * wander_freq + (float)e->slot_index * 0.73f + (float)(e->wave_id & 31) * 0.29f;
            wander_x = cosf(wp) + 0.35f * sinf(wp * 0.57f + 1.3f);
            wander_y = sinf(wp * 1.11f + 0.8f) + 0.28f * cosf(wp * 0.49f + 0.4f);
        }

        {
            const float sep_mag = length2(sep_x, sep_y);
            if (sep_mag > 1.0e-5f) {
                normalize2(&sep_x, &sep_y);
                sep_w *= fminf(sep_mag * (0.16f * sep_r), 2.75f);
            } else {
                sep_x = 0.0f;
                sep_y = 0.0f;
                sep_w = 0.0f;
            }
        }
        normalize2(&ali_x, &ali_y);
        normalize2(&coh_x, &coh_y);
        normalize2(&avoid_x, &avoid_y);
        normalize2(&goal_x, &goal_y);
        normalize2(&wander_x, &wander_y);

        {
            const float fx = sep_x * sep_w + ali_x * ali_w + coh_x * coh_w + avoid_x * avoid_w + goal_x * goal_w + wander_x * wander_w_eff;
            const float fy = sep_y * sep_w + ali_y * ali_w + coh_y * coh_w + avoid_y * avoid_w + goal_y * goal_w + wander_y * wander_w_eff;
            const float force_mag = length2(fx, fy);
            const float max_turn = (e->swarm_turn_rate_rad > 1.0e-4f)
                ? e->swarm_turn_rate_rad
                : (120.0f * (3.14159265359f / 180.0f));
            float cur_x = e->b.vx;
            float cur_y = e->b.vy;
            float cur_v = length2(cur_x, cur_y);
            float desired_x = fx;
            float desired_y = fy;
            float desired_a;
            float cur_a;
            float out_a;
            float target_speed;
            float min_speed;
            float target_vx;
            float target_vy;
            float desired_vx;
            float desired_vy;
            float max_accel;

            if (cur_v < 1.0e-4f) {
                cur_x = e->facing_x;
                cur_y = e->facing_y;
                cur_v = length2(cur_x, cur_y);
            }
            if (cur_v < 1.0e-4f) {
                cur_x = (e->swarm_goal_dir < 0.0f) ? -1.0f : 1.0f;
                cur_y = 0.0f;
                cur_v = 1.0f;
            }
            normalize2(&cur_x, &cur_y);

            if (length2(desired_x, desired_y) < 1.0e-5f) {
                desired_x = cur_x;
                desired_y = cur_y;
            } else {
                normalize2(&desired_x, &desired_y);
            }

            if (e->visual_kind == ENEMY_VISUAL_JELLY) {
                const float dir_blend = 0.12f + 0.88f * jelly_turn_gate;
                desired_x = lerpf(cur_x, desired_x, dir_blend);
                desired_y = lerpf(cur_y, desired_y, dir_blend);
                normalize2(&desired_x, &desired_y);
            }

            cur_a = atan2f(cur_y, cur_x);
            desired_a = atan2f(desired_y, desired_x);
            out_a = cur_a;
            if (dt > 1.0e-5f && max_turn > 1.0e-4f) {
                float max_turn_eff = max_turn;
                if (e->visual_kind == ENEMY_VISUAL_JELLY) {
                    max_turn_eff *= (0.08f + 1.25f * jelly_turn_gate);
                }
                const float max_da = max_turn_eff * dt;
                float da = wrap_angle_pi(desired_a - cur_a);
                if (da > max_da) da = max_da;
                if (da < -max_da) da = -max_da;
                out_a = cur_a + da;
            }

            min_speed = (e->swarm_min_speed > 1.0f) ? e->swarm_min_speed : fmaxf(26.0f * su, e->max_speed * 0.24f);
            target_speed = cur_v + (force_mag * e->accel * 18.0f - cur_v * steer_drag) * dt;
            /* Tighten turns under structure/searchlight urgency. */
            target_speed -= avoid_boost * (0.28f * e->max_speed);
            /* Player collision urgency should trigger evasive acceleration. */
            target_speed += player_avoid_boost * (0.36f * e->max_speed);
                if (e->visual_kind == ENEMY_VISUAL_JELLY) {
                    target_speed *= jelly_speed_scale;
                    min_speed *= lerpf(0.94f, 1.08f, jelly_speed_scale);
                    target_speed = lerpf(cur_v, target_speed, jelly_speed_blend);
                }
            target_speed = clampf(target_speed, min_speed, e->max_speed);

            target_vx = cosf(out_a) * target_speed;
            target_vy = sinf(out_a) * target_speed;
            desired_vx = target_vx;
            desired_vy = target_vy;
            if (dt > 1.0e-5f) {
                const float prev_ax = e->b.ax;
                const float prev_ay = e->b.ay;
                e->b.ax = (desired_vx - e->b.vx) / dt;
                e->b.ay = (desired_vy - e->b.vy) / dt;
                max_accel = fmaxf(220.0f, e->accel * 280.0f);
                if (e->visual_kind == ENEMY_VISUAL_JELLY) {
                    const float accel_blend_base = 0.14f + 0.30f * jelly_turn_gate;
                    const float accel_blend = accel_blend_base * (0.70f + 0.60f * jelly_expand_phase) * (1.0f - 0.45f * jelly_compress_phase);
                    e->b.ax = lerpf(prev_ax, e->b.ax, accel_blend);
                    e->b.ay = lerpf(prev_ay, e->b.ay, accel_blend);
                    max_accel = fmaxf(80.0f, e->accel * lerpf(105.0f, 215.0f, jelly_expand_phase));
                }
                {
                    const float a_len = length2(e->b.ax, e->b.ay);
                    if (a_len > max_accel) {
                        const float s = max_accel / a_len;
                        e->b.ax *= s;
                        e->b.ay *= s;
                    }
                }
            } else {
                e->b.ax = 0.0f;
                e->b.ay = 0.0f;
            }
        }
    }
    e->ai_timer_s += dt;
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
        if (e->visual_kind == ENEMY_VISUAL_EEL) {
            update_enemy_eel(g, e, dt, uses_cylinder, period, su);
        } else if (e->archetype == ENEMY_ARCH_SWARM) {
            update_enemy_swarm(g, e, dt, uses_cylinder, period, su);
        } else if (e->archetype == ENEMY_ARCH_KAMIKAZE) {
            update_enemy_kamikaze(g, e, dt, uses_cylinder, period, su);
        } else {
            update_enemy_formation(g, e, dt, su, uses_cylinder, period);
        }
        if (e->emp_push_time_s > 0.0f) {
            const float emp_push01 = clampf(e->emp_push_time_s / 1.10f, 0.0f, 1.0f);
            e->b.ax += e->emp_push_ax * emp_push01;
            e->b.ay += e->emp_push_ay * emp_push01;
            e->emp_push_time_s -= dt;
            if (e->emp_push_time_s < 0.0f) {
                e->emp_push_time_s = 0.0f;
                e->emp_push_ax = 0.0f;
                e->emp_push_ay = 0.0f;
            }
        }
        integrate_body(&e->b, dt);
        if (e->visual_kind == ENEMY_VISUAL_MANTA) {
            e->b.y = e->home_y;
            e->b.vy = 0.0f;
            e->b.ay = 0.0f;
        }
        if (!uses_cylinder) {
            if (e->archetype != ENEMY_ARCH_SWARM) {
                float avoid_x = 0.0f;
                float avoid_y = 0.0f;
                game_structure_avoidance_vector(
                    g,
                    e->b.x,
                    e->b.y,
                    fmaxf(8.0f * su, e->radius),
                    fmaxf(14.0f * su, e->radius * 1.4f),
                    &avoid_x,
                    &avoid_y
                );
                if (fabsf(avoid_x) > 1.0e-4f || fabsf(avoid_y) > 1.0e-4f) {
                    normalize2(&avoid_x, &avoid_y);
                    e->b.vx += avoid_x * (620.0f * su) * dt;
                    e->b.vy += avoid_y * (620.0f * su) * dt;
                }
            }
            if (game_structure_circle_overlap(g, e->b.x, e->b.y, fmaxf(8.0f * su, e->radius))) {
                if (e->archetype == ENEMY_ARCH_SWARM || e->archetype == ENEMY_ARCH_KAMIKAZE) {
                    float n_x = 0.0f;
                    float n_y = 0.0f;
                    float penetration = 0.0f;
                    const float rr = fmaxf(8.0f * su, e->radius);
                    if (swarm_structure_collision_response(g, e->b.x, e->b.y, rr, &n_x, &n_y, &penetration)) {
                        const float vdot = e->b.vx * n_x + e->b.vy * n_y;
                        e->b.x += n_x * (penetration + 1.0f);
                        e->b.y += n_y * (penetration + 1.0f);
                        if (vdot < 0.0f) {
                            e->b.vx -= 2.0f * vdot * n_x;
                            e->b.vy -= 2.0f * vdot * n_y;
                        }
                        if (e->archetype == ENEMY_ARCH_KAMIKAZE) {
                            e->b.vx *= 0.98f;
                            e->b.vy *= 0.98f;
                            {
                                const float vv = length2(e->b.vx, e->b.vy);
                                if (vv > 1.0e-4f) {
                                    e->facing_x = e->b.vx / vv;
                                    e->facing_y = e->b.vy / vv;
                                }
                            }
                        } else {
                            e->b.vx *= 0.88f;
                            e->b.vy *= 0.88f;
                        }
                    }
                } else {
                    float nx = e->b.x;
                    float ny = e->b.y;
                    if (game_find_noncolliding_spawn(
                            g,
                            &nx,
                            &ny,
                            fmaxf(8.0f * su, e->radius),
                            fmaxf(8.0f * su, e->radius * 0.6f),
                            g->world_h * 0.45f)) {
                        e->b.x = nx;
                        e->b.y = ny;
                        e->b.vx *= -0.35f;
                        e->b.vy *= -0.35f;
                    }
                }
            }
        }
        {
            float v = length2(e->b.vx, e->b.vy);
            float speed_cap = e->max_speed;
            if (e->archetype == ENEMY_ARCH_KAMIKAZE &&
                (e->state == ENEMY_STATE_KAMIKAZE_THRUST || e->state == ENEMY_STATE_KAMIKAZE_STRIKE)) {
                speed_cap *= 1.95f * fmaxf(e->kamikaze_thrust_scale, 0.1f);
            }
            if (e->emp_push_time_s > 0.0f) {
                speed_cap *= 2.40f;
            }
            if (v > speed_cap) {
                const float s = speed_cap / v;
                e->b.vx *= s;
                e->b.vy *= s;
            }
            if (e->archetype == ENEMY_ARCH_SWARM) {
                const float min_speed = (e->swarm_min_speed > 1.0f) ? e->swarm_min_speed : fmaxf(26.0f * su, e->max_speed * 0.24f);
                v = length2(e->b.vx, e->b.vy);
                if (v < min_speed) {
                    float dir_x = e->b.vx;
                    float dir_y = e->b.vy;
                    if (v < 1.0e-4f) {
                        dir_x = e->facing_x;
                        dir_y = e->facing_y;
                    }
                    if (length2(dir_x, dir_y) < 1.0e-4f) {
                        dir_x = (e->swarm_goal_dir < 0.0f) ? -1.0f : 1.0f;
                        dir_y = 0.0f;
                    }
                    normalize2(&dir_x, &dir_y);
                    {
                        const float gain = clampf(dt * 6.0f, 0.0f, 1.0f);
                        const float tvx = dir_x * min_speed;
                        const float tvy = dir_y * min_speed;
                        e->b.vx += (tvx - e->b.vx) * gain;
                        e->b.vy += (tvy - e->b.vy) * gain;
                    }
                    v = length2(e->b.vx, e->b.vy);
                }
            }
            if (v > 1.0f && e->archetype != ENEMY_ARCH_KAMIKAZE) {
                e->facing_x = e->b.vx / v;
                e->facing_y = e->b.vy / v;
            }
        }

        if (e->visual_kind == ENEMY_VISUAL_MANTA) {
            emit_manta_turbulence(g, e, dt, su, uses_cylinder, period);
        }

        if (!uses_cylinder && e->b.x < g->camera_x - g->world_w * 0.72f) {
            if (e->archetype == ENEMY_ARCH_FORMATION && e->visual_kind != ENEMY_VISUAL_MANTA) {
                e->state = ENEMY_STATE_BREAK_ATTACK;
                e->ai_timer_s = 0.0f;
                e->break_delay_s = 1.0f + frand01() * 1.3f;
            }
        }
        if (e->b.y < 26.0f * su) {
            const float edge = 26.0f * su;
            e->b.y = edge;
            if (e->archetype == ENEMY_ARCH_KAMIKAZE) {
                if (e->b.vy < 0.0f) {
                    e->b.vy = -e->b.vy;
                }
                {
                    const float vv = length2(e->b.vx, e->b.vy);
                    if (vv > 1.0e-4f) {
                        e->facing_x = e->b.vx / vv;
                        e->facing_y = e->b.vy / vv;
                    }
                }
            } else if (e->b.vy < 0.0f) {
                e->b.vy = 0.0f;
            }
        }
        if (e->b.y > g->world_h - 26.0f * su) {
            const float edge = g->world_h - 26.0f * su;
            e->b.y = edge;
            if (e->archetype == ENEMY_ARCH_KAMIKAZE) {
                if (e->b.vy > 0.0f) {
                    e->b.vy = -e->b.vy;
                }
                {
                    const float vv = length2(e->b.vx, e->b.vy);
                    if (vv > 1.0e-4f) {
                        e->facing_x = e->b.vx / vv;
                        e->facing_y = e->b.vy / vv;
                    }
                }
            } else if (e->b.vy > 0.0f) {
                e->b.vy = 0.0f;
            }
        }
        if (g->lives > 0) {
            if (g->shield_active) {
                (void)shield_deflect_body(g, &e->b, e->radius, 580.0f * su, uses_cylinder, period);
            }
            const float hit_r = e->radius + 14.0f * su;
            if (!g->shield_active &&
                !player_hit_this_frame &&
                dist_sq_level(uses_cylinder, period, e->b.x, e->b.y, g->player.b.x, g->player.b.y) <= hit_r * hit_r) {
                emit_enemy_debris(g, e, g->player.b.vx, g->player.b.vy);
                e->active = 0;
                apply_player_hit(g, g->player.b.x, g->player.b.y, g->player.b.vx, g->player.b.vy, su);
                player_hit_this_frame = 1;
            }
        }
        if (e->active && e->visual_kind == ENEMY_VISUAL_EEL) {
            eel_update_spine(e, dt, uses_cylinder, period);
        }
        enemy_try_fire(g, e, dt, su, db, uses_cylinder, period);
    }

    if (g->eel_arc_count > 0 || g->lives > 0) {
        update_eel_arc_effects(g, dt, su, uses_cylinder, period, &player_hit_this_frame);
    }

    for (size_t i = 0; i < MAX_ENEMY_BULLETS; ++i) {
        enemy_bullet* b = &g->enemy_bullets[i];
        if (!b->active) {
            continue;
        }
        const float prev_x = b->b.x;
        const float prev_y = b->b.y;
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
        if (!uses_cylinder && game_structure_segment_blocked(g, prev_x, prev_y, b->b.x, b->b.y, b->radius)) {
            b->active = 0;
            continue;
        }
        if (g->shield_active) {
            if (shield_deflect_body(g, &b->b, b->radius + 2.0f * su, 760.0f * su, uses_cylinder, period)) {
                b->ttl_s = fmaxf(b->ttl_s, 0.06f);
            }
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
                {
                    enemy* hit = &g->enemies[ei];
                    const int hp_max = (hit->hp > 0) ? hit->hp : 1;
                    hit->hp = hp_max - 1;
                    if (hit->hp <= 0) {
                        emit_enemy_debris(g, hit, g->bullets[bi].b.vx, g->bullets[bi].b.vy);
                        hit->active = 0;
                        emit_explosion(g, hit->b.x, hit->b.y, hit->b.vx, hit->b.vy, 26, su);
                        game_on_enemy_destroyed(g, hit->b.x, hit->b.y, hit->b.vx, hit->b.vy, 100);
                    } else {
                        hit->b.vx += g->bullets[bi].b.vx * 0.05f;
                        hit->b.vy += g->bullets[bi].b.vy * 0.05f;
                        if (hit->visual_kind == ENEMY_VISUAL_MANTA) {
                            hit->missile_charge_s = fmaxf(0.0f, hit->missile_charge_s - 0.18f);
                        }
                    }
                }
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

void enemy_apply_emp(
    game_state* g,
    float primary_radius,
    float blast_radius,
    float blast_accel,
    float su,
    int uses_cylinder,
    float period
) {
    if (!g || primary_radius <= 0.0f || blast_radius <= 0.0f) {
        return;
    }
    if (blast_radius < primary_radius) {
        blast_radius = primary_radius;
    }

    const float primary_sq = primary_radius * primary_radius;
    const float blast_sq = blast_radius * blast_radius;
    const float px = g->player.b.x;
    const float py = g->player.b.y;

    for (size_t i = 0; i < MAX_ENEMIES; ++i) {
        enemy* e = &g->enemies[i];
        if (!e->active) {
            continue;
        }
        const float dx = uses_cylinder ? wrap_delta(e->b.x, px, period) : (e->b.x - px);
        const float dy = e->b.y - py;
        const float d2 = dx * dx + dy * dy;
        if (d2 <= primary_sq) {
            emit_enemy_debris(g, e, e->b.vx, e->b.vy);
            emit_explosion(g, e->b.x, e->b.y, e->b.vx, e->b.vy, 26, su);
            e->active = 0;
            game_on_enemy_destroyed(g, e->b.x, e->b.y, e->b.vx, e->b.vy, 100);
            continue;
        }
        if (d2 <= blast_sq) {
            const float d = sqrtf(fmaxf(d2, 1.0e-6f));
            float nx = dx / d;
            float ny = dy / d;
            if (d <= 1.0e-3f) {
                nx = (frand01() < 0.5f) ? -1.0f : 1.0f;
                ny = frands1() * 0.25f;
            }
            {
                const float t = 1.0f - clampf((d - primary_radius) / fmaxf(blast_radius - primary_radius, 1.0f), 0.0f, 1.0f);
                const float impulse = blast_accel * (0.80f + 0.85f * t);
                const float push_duration_s = 1.10f;
                e->b.vx += nx * (impulse * 0.48f);
                e->b.vy += ny * (impulse * 0.48f);
                e->emp_push_ax += nx * (impulse * 3.20f);
                e->emp_push_ay += ny * (impulse * 3.20f);
                if (e->emp_push_time_s < push_duration_s) {
                    e->emp_push_time_s = push_duration_s;
                }
            }
        }
    }

    for (size_t i = 0; i < MAX_ENEMY_BULLETS; ++i) {
        enemy_bullet* b = &g->enemy_bullets[i];
        if (!b->active) {
            continue;
        }
        const float dx = uses_cylinder ? wrap_delta(b->b.x, px, period) : (b->b.x - px);
        const float dy = b->b.y - py;
        const float d2 = dx * dx + dy * dy;
        if (d2 <= primary_sq) {
            b->active = 0;
            continue;
        }
        if (d2 <= blast_sq) {
            const float d = sqrtf(fmaxf(d2, 1.0e-6f));
            float nx = dx / d;
            float ny = dy / d;
            if (d <= 1.0e-3f) {
                nx = (frand01() < 0.5f) ? -1.0f : 1.0f;
                ny = frands1() * 0.25f;
            }
            {
                const float t = 1.0f - clampf((d - primary_radius) / fmaxf(blast_radius - primary_radius, 1.0f), 0.0f, 1.0f);
                const float impulse = blast_accel * (0.85f + 0.80f * t);
                b->b.vx += nx * impulse;
                b->b.vy += ny * impulse;
                b->ttl_s = fmaxf(b->ttl_s, 0.25f);
            }
        }
    }
}
