#include "level_editor.h"

#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static int clampi(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static void mark_editor_dirty(level_editor_state* s) {
    if (!s) {
        return;
    }
    s->dirty = 1;
    s->edit_revision += 1u;
}

static int structure_grid_x_steps_for_level(float level_screens) {
    const float ls = fmaxf(level_screens, 1.0f);
    const int base_steps = (int)lroundf(ls * (float)(LEVELDEF_STRUCTURE_GRID_W - 1));
    const int steps = base_steps / LEVELDEF_STRUCTURE_GRID_SCALE;
    return (steps < 1) ? 1 : steps;
}

static int structure_grid_y_steps(void) {
    const int steps = (LEVELDEF_STRUCTURE_GRID_H - 1) / LEVELDEF_STRUCTURE_GRID_SCALE;
    return (steps < 1) ? 1 : steps;
}

static int structure_cell_from_mouse01(float p01, int steps, int max_cell) {
    const float p = clampf(p01, 0.0f, 1.0f);
    int cell = (int)floorf(p * (float)steps + 1.0e-6f);
    if (cell > steps) {
        cell = steps;
    }
    return clampi(cell, 0, (max_cell > 0) ? max_cell : 0);
}

static float snap_x01(float x01) {
    const int steps = structure_grid_x_steps_for_level(1.0f);
    const float gx = roundf(clampf(x01, 0.0f, 1.0f) * (float)steps);
    return gx / (float)steps;
}

static float snap_x01_level(float x01, float level_screens) {
    const int steps = structure_grid_x_steps_for_level(level_screens);
    const float gx = roundf(clampf(x01, 0.0f, 1.0f) * (float)steps);
    return gx / (float)steps;
}

static float snap_y01(float y01) {
    const int steps = structure_grid_y_steps();
    const float gy = roundf(clampf(y01, 0.0f, 1.0f) * (float)steps);
    return gy / (float)steps;
}

static void structure_prefab_dims(int prefab_id, int* out_w, int* out_h) {
    int w = 1;
    int h = 1;
    if (prefab_id == 4) {
        h = 3;
    }
    if (out_w) *out_w = w;
    if (out_h) *out_h = h;
}

static int structure_overlaps_cell(
    const level_editor_state* s,
    int layer,
    int gx_steps,
    int ignore_marker_index,
    int gx,
    int gy,
    int w_units,
    int h_units
) {
    if (!s) {
        return 0;
    }
    const int ax0 = gx;
    const int ay0 = gy;
    const int ax1 = gx + w_units;
    const int ay1 = gy + h_units;
    for (int i = 0; i < s->marker_count; ++i) {
        if (i == ignore_marker_index) {
            continue;
        }
        const level_editor_marker* m = &s->markers[i];
        if (m->kind != LEVEL_EDITOR_MARKER_STRUCTURE || m->track != LEVEL_EDITOR_TRACK_SPATIAL) {
            continue;
        }
        const int mlayer = clampi((int)lroundf(m->b), 0, 1);
        if (mlayer != layer) {
            continue;
        }
        int mw = 1;
        int mh = 1;
        structure_prefab_dims(clampi((int)lroundf(m->a), 0, 31), &mw, &mh);
        const int mgx = clampi((int)lroundf(m->x01 * (float)gx_steps), 0, gx_steps);
        const int mgy = clampi((int)lroundf(m->y01 * (float)structure_grid_y_steps()), 0, structure_grid_y_steps());
        const int bx0 = mgx;
        const int by0 = mgy;
        const int bx1 = mgx + mw;
        const int by1 = mgy + mh;
        if (ax0 < bx1 && ax1 > bx0 && ay0 < by1 && ay1 > by0) {
            return 1;
        }
    }
    return 0;
}

static void place_structure_marker_from_view(level_editor_state* s, int marker_index, float mx01, float my01) {
    if (!s || marker_index < 0 || marker_index >= s->marker_count) {
        return;
    }
    level_editor_marker* m = &s->markers[marker_index];
    if (m->kind != LEVEL_EDITOR_MARKER_STRUCTURE || m->track != LEVEL_EDITOR_TRACK_SPATIAL) {
        return;
    }
    const float level_screens = fmaxf(s->level_length_screens, 1.0f);
    const float start_screen = s->timeline_01 * fmaxf(level_screens - 1.0f, 0.0f);
    const float view_min = start_screen / level_screens;
    const float view_max = (start_screen + 1.0f) / level_screens;
    const float x01 = view_min + clampf(mx01, 0.0f, 1.0f) * fmaxf(view_max - view_min, 1.0e-6f);
    const float y01 = clampf(my01, 0.0f, 1.0f);
    const int prefab_id = clampi((int)lroundf(m->a), 0, 31);
    const int layer = clampi((int)lroundf(m->b), 0, 1);
    const int gx_steps = structure_grid_x_steps_for_level(level_screens);
    int w_units = 1;
    int h_units = 1;
    structure_prefab_dims(prefab_id, &w_units, &h_units);
    const int max_gx = gx_steps - w_units + 1;
    const int max_gy = structure_grid_y_steps() - h_units + 1;
    int gx = structure_cell_from_mouse01(x01, gx_steps, max_gx);
    int gy = structure_cell_from_mouse01(y01, structure_grid_y_steps(), max_gy);
    if (structure_overlaps_cell(s, layer, gx_steps, marker_index, gx, gy, w_units, h_units)) {
        for (int radius = 1; radius <= 16; ++radius) {
            const int candidates[2] = {gx + radius, gx - radius};
            int found = 0;
            for (int ci = 0; ci < 2; ++ci) {
                const int cgx = clampi(candidates[ci], 0, (max_gx > 0) ? max_gx : 0);
                if (!structure_overlaps_cell(s, layer, gx_steps, marker_index, cgx, gy, w_units, h_units)) {
                    gx = cgx;
                    found = 1;
                    break;
                }
            }
            if (found) {
                break;
            }
        }
    }
    m->x01 = (float)gx / (float)gx_steps;
    m->y01 = (float)gy / (float)structure_grid_y_steps();
}

static int strieq(const char* a, const char* b) {
    if (!a || !b) {
        return 0;
    }
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
            return 0;
        }
        ++a;
        ++b;
    }
    return (*a == '\0' && *b == '\0');
}

static int stristarts(const char* s, const char* prefix) {
    if (!s || !prefix) {
        return 0;
    }
    while (*prefix) {
        if (*s == '\0') {
            return 0;
        }
        if (tolower((unsigned char)*s) != tolower((unsigned char)*prefix)) {
            return 0;
        }
        ++s;
        ++prefix;
    }
    return 1;
}

static int resolve_level_file_path(const char* level_name, char* out, size_t out_cap) {
    static const char* dirs[] = {"../data/levels", "data/levels"};
    char path[LEVEL_EDITOR_PATH_CAP];
    if (!level_name || !out || out_cap == 0) {
        return 0;
    }
    for (int i = 0; i < (int)(sizeof(dirs) / sizeof(dirs[0])); ++i) {
        if (snprintf(path, sizeof(path), "%s/%s.cfg", dirs[i], level_name) >= (int)sizeof(path)) {
            continue;
        }
        FILE* f = fopen(path, "rb");
        if (f) {
            fclose(f);
            snprintf(out, out_cap, "%s", path);
            return 1;
        }
    }
    return 0;
}

static int read_file_text(const char* path, char* out, size_t out_cap) {
    if (!path || !out || out_cap == 0) {
        return 0;
    }
    FILE* f = fopen(path, "rb");
    if (!f) {
        return 0;
    }
    const size_t n = fread(out, 1, out_cap - 1, f);
    out[n] = '\0';
    fclose(f);
    return 1;
}

static int write_file_text(const char* path, const char* text) {
    if (!path || !text) {
        return 0;
    }
    FILE* f = fopen(path, "wb");
    if (!f) {
        return 0;
    }
    const size_t n = strlen(text);
    const size_t w = fwrite(text, 1, n, f);
    fclose(f);
    return w == n;
}

static int path_exists(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        return 0;
    }
    fclose(f);
    return 1;
}

static int choose_levels_dir(char* out, size_t out_cap) {
    static const char* dirs[] = {"../data/levels", "data/levels"};
    char probe[LEVEL_EDITOR_PATH_CAP];
    if (!out || out_cap == 0) {
        return 0;
    }
    for (int i = 0; i < (int)(sizeof(dirs) / sizeof(dirs[0])); ++i) {
        if (snprintf(probe, sizeof(probe), "%s/combat.cfg", dirs[i]) >= (int)sizeof(probe)) {
            continue;
        }
        if (path_exists(probe)) {
            snprintf(out, out_cap, "%s", dirs[i]);
            return 1;
        }
    }
    return 0;
}

static int is_wave_kind(int kind) {
    return (kind == LEVEL_EDITOR_MARKER_WAVE_SINE ||
            kind == LEVEL_EDITOR_MARKER_WAVE_V ||
            kind == LEVEL_EDITOR_MARKER_WAVE_KAMIKAZE ||
            kind == LEVEL_EDITOR_MARKER_BOID ||
            kind == LEVEL_EDITOR_MARKER_BOID_FISH ||
            kind == LEVEL_EDITOR_MARKER_BOID_FIREFLY ||
            kind == LEVEL_EDITOR_MARKER_BOID_BIRD);
}

static int is_boid_wave_kind(int kind) {
    return (kind == LEVEL_EDITOR_MARKER_BOID ||
            kind == LEVEL_EDITOR_MARKER_BOID_FISH ||
            kind == LEVEL_EDITOR_MARKER_BOID_FIREFLY ||
            kind == LEVEL_EDITOR_MARKER_BOID_BIRD);
}

static float boid_turn_rate_default_deg_for_kind(int kind) {
    if (kind == LEVEL_EDITOR_MARKER_BOID_BIRD) {
        return 340.0f;
    }
    if (kind == LEVEL_EDITOR_MARKER_BOID_FIREFLY) {
        return 460.0f;
    }
    return 440.0f;
}

static float boid_speed_default_for_kind(int kind) {
    if (kind == LEVEL_EDITOR_MARKER_BOID_BIRD) {
        return 430.0f;
    }
    if (kind == LEVEL_EDITOR_MARKER_BOID_FIREFLY) {
        return 210.0f;
    }
    return 300.0f;
}

static float boid_accel_default_for_kind(int kind) {
    if (kind == LEVEL_EDITOR_MARKER_BOID_BIRD) {
        return 9.0f;
    }
    if (kind == LEVEL_EDITOR_MARKER_BOID_FIREFLY) {
        return 6.2f;
    }
    return 7.8f;
}

static int is_enemy_marker_kind(int kind) {
    return is_wave_kind(kind) || kind == LEVEL_EDITOR_MARKER_ASTEROID_STORM;
}

static int level_editor_enemy_spatial(const level_editor_state* s) {
    if (!s) {
        return 1;
    }
    return (s->level_wave_mode == LEVELDEF_WAVES_CURATED);
}

static int marker_is_event_item(const level_editor_state* s, const level_editor_marker* m) {
    if (!s || !m) {
        return 0;
    }
    if (!is_enemy_marker_kind(m->kind)) {
        return 0;
    }
    if (m->track == LEVEL_EDITOR_TRACK_EVENT) {
        return 1;
    }
    if (m->kind == LEVEL_EDITOR_MARKER_ASTEROID_STORM) {
        return 0;
    }
    return !level_editor_enemy_spatial(s);
}

static int next_event_order(const level_editor_state* s) {
    int max_order = 0;
    if (!s) {
        return 1;
    }
    for (int i = 0; i < s->marker_count; ++i) {
        const level_editor_marker* m = &s->markers[i];
        if (!marker_is_event_item(s, m)) {
            continue;
        }
        if (m->order > max_order) {
            max_order = m->order;
        }
    }
    return max_order + 1;
}

static int event_item_count(const level_editor_state* s) {
    int n = 0;
    if (!s) {
        return 0;
    }
    for (int i = 0; i < s->marker_count; ++i) {
        if (marker_is_event_item(s, &s->markers[i])) {
            n++;
        }
    }
    return n;
}

static void shift_event_orders(level_editor_state* s, int from_order) {
    if (!s) {
        return;
    }
    for (int i = 0; i < s->marker_count; ++i) {
        level_editor_marker* m = &s->markers[i];
        if (!marker_is_event_item(s, m)) {
            continue;
        }
        if (m->order >= from_order) {
            m->order += 1;
        }
    }
}

static void level_editor_save_snapshot(level_editor_state* s) {
    if (!s) {
        return;
    }
    s->snapshot_valid = 1;
    s->snapshot_level_length_screens = s->level_length_screens;
    s->snapshot_level_render_style = s->level_render_style;
    s->snapshot_level_wave_mode = s->level_wave_mode;
    s->snapshot_level_theme_palette = s->level_theme_palette;
    s->snapshot_level_asteroid_storm_enabled = s->level_asteroid_storm_enabled;
    s->snapshot_level_asteroid_storm_angle_deg = s->level_asteroid_storm_angle_deg;
    s->snapshot_level_asteroid_storm_speed = s->level_asteroid_storm_speed;
    s->snapshot_level_asteroid_storm_duration_s = s->level_asteroid_storm_duration_s;
    s->snapshot_level_asteroid_storm_density = s->level_asteroid_storm_density;
    s->snapshot_level_kamikaze_radius_min = s->level_kamikaze_radius_min;
    s->snapshot_level_kamikaze_radius_max = s->level_kamikaze_radius_max;
    snprintf(s->snapshot_level_name, sizeof(s->snapshot_level_name), "%s", s->level_name);
    s->snapshot_marker_count = s->marker_count;
    if (s->snapshot_marker_count > LEVEL_EDITOR_MAX_MARKERS) {
        s->snapshot_marker_count = LEVEL_EDITOR_MAX_MARKERS;
    }
    if (s->snapshot_marker_count > 0) {
        memcpy(
            s->snapshot_markers,
            s->markers,
            (size_t)s->snapshot_marker_count * sizeof(level_editor_marker)
        );
    }
}

static float marker_pick_radius01(int kind) {
    if (is_wave_kind(kind)) {
        return 0.045f;
    }
    if (kind == LEVEL_EDITOR_MARKER_SEARCHLIGHT) {
        return 0.040f;
    }
    if (kind == LEVEL_EDITOR_MARKER_EXIT) {
        return 0.038f;
    }
    if (kind == LEVEL_EDITOR_MARKER_MINEFIELD) {
        return 0.040f;
    }
    if (kind == LEVEL_EDITOR_MARKER_MISSILE) {
        return 0.040f;
    }
    if (kind == LEVEL_EDITOR_MARKER_ARC_NODE) {
        return 0.040f;
    }
    if (kind == LEVEL_EDITOR_MARKER_STRUCTURE) {
        return 0.030f;
    }
    return 0.032f;
}

static int editor_pick_trace_enabled(void) {
    static int cached = -1;
    if (cached >= 0) {
        return cached;
    }
    {
        const char* env = getenv("VTYPE_EDITOR_PICK_TRACE");
        cached = (env && env[0] && strcmp(env, "0") != 0) ? 1 : 0;
    }
    return cached;
}

static int pick_event_marker_in_enemy_timeline(
    const level_editor_state* s,
    float mouse_x,
    const level_editor_layout* l
) {
    if (!s || !l) {
        return -1;
    }
    const int n = event_item_count(s);
    if (n <= 0) {
        return -1;
    }
    const float slot_w = l->timeline_enemy_track.w / (float)n;
    const int slot = (int)clampf(
        floorf((mouse_x - l->timeline_enemy_track.x) / fmaxf(slot_w, 1.0f)),
        0.0f,
        (float)(n - 1)
    );
    for (int i = 0; i < s->marker_count; ++i) {
        const level_editor_marker* mi = &s->markers[i];
        if (!marker_is_event_item(s, mi)) {
            continue;
        }
        int rank = 0;
        for (int j = 0; j < s->marker_count; ++j) {
            const level_editor_marker* mj = &s->markers[j];
            if (!marker_is_event_item(s, mj)) {
                continue;
            }
            if (mj->order < mi->order || (mj->order == mi->order && j < i)) {
                rank += 1;
            }
        }
        if (rank == slot) {
            return i;
        }
    }
    return -1;
}

static int pick_spatial_marker_in_viewport(
    const level_editor_state* s,
    const level_editor_layout* l,
    float mx01,
    float my01,
    int current_selected
) {
    if (!s || !l) {
        return -1;
    }
    const float level_screens = fmaxf(s->level_length_screens, 1.0f);
    const float start_screen = s->timeline_01 * fmaxf(level_screens - 1.0f, 0.0f);
    const float view_min = start_screen / level_screens;
    const float view_max = (start_screen + 1.0f) / level_screens;
    const float view_span = fmaxf(view_max - view_min, 1.0e-6f);

    /* 1) Structure picking uses exact tile bounds in viewport coordinates. */
    {
        int hits[LEVEL_EDITOR_MAX_MARKERS];
        int hit_n = 0;
        const int gx_steps = structure_grid_x_steps_for_level(level_screens);
        const int gy_steps = structure_grid_y_steps();
        for (int i = 0; i < s->marker_count && hit_n < LEVEL_EDITOR_MAX_MARKERS; ++i) {
            const level_editor_marker* m = &s->markers[i];
            if (m->kind != LEVEL_EDITOR_MARKER_STRUCTURE) {
                continue;
            }
            if (!level_editor_enemy_spatial(s) && marker_is_event_item(s, m)) {
                continue;
            }
            int w_units = 1;
            int h_units = 1;
            int q = ((int)lroundf(m->c) % 4 + 4) % 4;
            const int gx = clampi((int)lroundf(m->x01 * (float)gx_steps), 0, gx_steps);
            const int gy = clampi((int)lroundf(m->y01 * (float)gy_steps), 0, gy_steps);
            structure_prefab_dims(clampi((int)lroundf(m->a), 0, 31), &w_units, &h_units);
            if ((q & 1) != 0) {
                const int tmp = w_units;
                w_units = h_units;
                h_units = tmp;
            }
            const float x0 = (float)gx / (float)gx_steps;
            const float x1 = (float)(gx + w_units) / (float)gx_steps;
            const float y0 = (float)gy / (float)gy_steps;
            const float y1 = (float)(gy + h_units) / (float)gy_steps;
            if (x1 < view_min || x0 > view_max) {
                continue;
            }
            const float vx0 = (x0 - view_min) / view_span;
            const float vx1 = (x1 - view_min) / view_span;
            const float vy0 = y0;
            const float vy1 = y1;
            const float min_x = fminf(vx0, vx1);
            const float max_x = fmaxf(vx0, vx1);
            const float min_y = fminf(vy0, vy1);
            const float max_y = fmaxf(vy0, vy1);
            if (mx01 >= min_x && mx01 <= max_x && my01 >= min_y && my01 <= max_y) {
                hits[hit_n++] = i;
            }
        }
        if (hit_n > 0) {
            for (int a = 0; a < hit_n - 1; ++a) {
                for (int b = a + 1; b < hit_n; ++b) {
                    const int ia = hits[a];
                    const int ib = hits[b];
                    const int la = clampi((int)lroundf(s->markers[ia].b), 0, 1);
                    const int lb = clampi((int)lroundf(s->markers[ib].b), 0, 1);
                    const int swap = (lb > la) || ((lb == la) && (ib < ia));
                    if (swap) {
                        const int tmp = hits[a];
                        hits[a] = hits[b];
                        hits[b] = tmp;
                    }
                }
            }
            int cur_pos = -1;
            for (int k = 0; k < hit_n; ++k) {
                if (hits[k] == current_selected) {
                    cur_pos = k;
                    break;
                }
            }
            if (editor_pick_trace_enabled()) {
                fprintf(stderr, "[editor_pick] structure_hits=%d selected=%d next=%d\n", hit_n, current_selected, (cur_pos >= 0) ? hits[(cur_pos + 1) % hit_n] : hits[0]);
            }
            if (cur_pos >= 0) {
                return hits[(cur_pos + 1) % hit_n];
            }
            return hits[0];
        }
    }

    /* 2) Non-structure markers use nearest-with-threshold. */
    int best = -1;
    float best_d2 = 1.0e9f;
    for (int i = 0; i < s->marker_count; ++i) {
        const level_editor_marker* m = &s->markers[i];
        if (m->kind == LEVEL_EDITOR_MARKER_STRUCTURE) {
            continue;
        }
        if (!level_editor_enemy_spatial(s) && marker_is_event_item(s, m)) {
            continue;
        }
        if (m->x01 < view_min || m->x01 > view_max) {
            continue;
        }
        const float vx = (m->x01 - view_min) / fmaxf(view_max - view_min, 1.0e-5f);
        const float vy = m->y01;
        const float dx = vx - mx01;
        const float dy = vy - my01;
        const float d2 = dx * dx + dy * dy;
        if (editor_pick_trace_enabled()) {
            fprintf(
                stderr,
                "[editor_pick] cand idx=%d kind=%d track=%d x01=%.3f y01=%.3f vx=%.3f vy=%.3f d2=%.6f\n",
                i,
                (int)m->kind,
                (int)m->track,
                m->x01,
                m->y01,
                vx,
                vy,
                d2
            );
        }
        if (d2 < best_d2) {
            best_d2 = d2;
            best = i;
        }
    }
    if (best < 0) {
        if (editor_pick_trace_enabled()) {
            fprintf(stderr, "[editor_pick] none mx01=%.3f my01=%.3f\n", mx01, my01);
        }
        return -1;
    }
    const float r = marker_pick_radius01(s->markers[best].kind);
    if (editor_pick_trace_enabled()) {
        fprintf(
            stderr,
            "[editor_pick] best idx=%d kind=%d d2=%.6f r=%.4f hit=%d\n",
            best,
            (int)s->markers[best].kind,
            best_d2,
            r,
            (best_d2 <= (r * r)) ? 1 : 0
        );
    }
    if (best_d2 <= (r * r)) {
        return best;
    }
    return -1;
}

typedef struct wave_marker_ref {
    float x01;
    level_editor_marker marker;
} wave_marker_ref;

static int compare_wave_marker_ref(const void* a, const void* b) {
    const wave_marker_ref* wa = (const wave_marker_ref*)a;
    const wave_marker_ref* wb = (const wave_marker_ref*)b;
    if (wa->marker.track == LEVEL_EDITOR_TRACK_EVENT &&
        wb->marker.track == LEVEL_EDITOR_TRACK_EVENT) {
        if (wa->marker.order < wb->marker.order) return -1;
        if (wa->marker.order > wb->marker.order) return 1;
    }
    if (wa->x01 < wb->x01) return -1;
    if (wa->x01 > wb->x01) return 1;
    return 0;
}

static const char* style_header_name(int style) {
    switch (style) {
        case LEVEL_STYLE_DEFENDER: return "DEFENDER";
        case LEVEL_STYLE_ENEMY_RADAR: return "ENEMY_RADAR";
        case LEVEL_STYLE_EVENT_HORIZON: return "EVENT_HORIZON";
        case LEVEL_STYLE_EVENT_HORIZON_LEGACY: return "EVENT_HORIZON_LEGACY";
        case LEVEL_STYLE_HIGH_PLAINS_DRIFTER: return "HIGH_PLAINS_DRIFTER";
        case LEVEL_STYLE_HIGH_PLAINS_DRIFTER_2: return "HIGH_PLAINS_DRIFTER_2";
        case LEVEL_STYLE_FOG_OF_WAR: return "FOG_OF_WAR";
        case LEVEL_STYLE_BLANK: return "BLANK";
        default: return "UNKNOWN";
    }
}

static const char* render_style_name(int render_style) {
    switch (render_style) {
        case LEVEL_RENDER_DEFENDER: return "defender";
        case LEVEL_RENDER_CYLINDER: return "cylinder";
        case LEVEL_RENDER_DRIFTER: return "drifter";
        case LEVEL_RENDER_DRIFTER_SHADED: return "drifter_shaded";
        case LEVEL_RENDER_FOG: return "fog";
        case LEVEL_RENDER_BLANK: return "blank";
        default: return "defender";
    }
}

static const char* wave_mode_name(int mode) {
    if (mode == LEVELDEF_WAVES_BOID_ONLY) {
        return "boid_only";
    }
    if (mode == LEVELDEF_WAVES_CURATED) {
        return "curated";
    }
    return "normal";
}

static const char* spawn_mode_name(int mode) {
    switch (mode) {
        case LEVELDEF_SPAWN_SEQUENCED_CLEAR: return "sequenced_clear";
        case LEVELDEF_SPAWN_TIMED: return "timed";
        case LEVELDEF_SPAWN_TIMED_SEQUENCED: return "timed_sequenced";
        default: return "sequenced_clear";
    }
}

static const char* wave_pattern_name(int p) {
    switch (p) {
        case LEVELDEF_WAVE_SINE_SNAKE: return "sine_snake";
        case LEVELDEF_WAVE_V_FORMATION: return "v_formation";
        case LEVELDEF_WAVE_SWARM: return "swarm";
        case LEVELDEF_WAVE_SWARM_FISH: return "swarm_fish";
        case LEVELDEF_WAVE_SWARM_FIREFLY: return "swarm_firefly";
        case LEVELDEF_WAVE_SWARM_BIRD: return "swarm_bird";
        case LEVELDEF_WAVE_KAMIKAZE: return "kamikaze";
        case LEVELDEF_WAVE_ASTEROID_STORM: return "asteroid_storm";
        default: return "sine_snake";
    }
}

static const char* searchlight_motion_name(int motion) {
    switch (motion) {
        case SEARCHLIGHT_MOTION_LINEAR: return "linear";
        case SEARCHLIGHT_MOTION_SPIN: return "spin";
        case SEARCHLIGHT_MOTION_PENDULUM: return "pendulum";
        default: return "pendulum";
    }
}

static const char* searchlight_source_name(int source) {
    switch (source) {
        case SEARCHLIGHT_SOURCE_ORB: return "orb";
        case SEARCHLIGHT_SOURCE_DOME: return "dome";
        default: return "dome";
    }
}

static const char* curated_kind_name(int kind) {
    if (kind == LEVEL_EDITOR_MARKER_WAVE_SINE) return "sine";
    if (kind == LEVEL_EDITOR_MARKER_WAVE_V) return "v";
    if (kind == LEVEL_EDITOR_MARKER_WAVE_KAMIKAZE) return "kamikaze";
    if (kind == LEVEL_EDITOR_MARKER_BOID) return "boid";
    if (kind == LEVEL_EDITOR_MARKER_BOID_FISH) return "boid_fish";
    if (kind == LEVEL_EDITOR_MARKER_BOID_FIREFLY) return "boid_firefly";
    if (kind == LEVEL_EDITOR_MARKER_BOID_BIRD) return "boid_bird";
    return "sine";
}

static const char* marker_kind_name(int kind) {
    if (kind == LEVEL_EDITOR_MARKER_EXIT) return "exit";
    if (kind == LEVEL_EDITOR_MARKER_SEARCHLIGHT) return "searchlight";
    if (kind == LEVEL_EDITOR_MARKER_ASTEROID_STORM) return "asteroid_storm";
    if (kind == LEVEL_EDITOR_MARKER_MINEFIELD) return "minefield";
    if (kind == LEVEL_EDITOR_MARKER_MISSILE) return "missile_launcher";
    if (kind == LEVEL_EDITOR_MARKER_ARC_NODE) return "arc_node";
    if (kind == LEVEL_EDITOR_MARKER_STRUCTURE) return "structure";
    if (kind == LEVEL_EDITOR_MARKER_BOID_FISH) return "swarm_fish";
    if (kind == LEVEL_EDITOR_MARKER_BOID_FIREFLY) return "swarm_firefly";
    if (kind == LEVEL_EDITOR_MARKER_BOID_BIRD) return "swarm_bird";
    return "marker";
}

static const char* render_style_file_base(int render_style) {
    switch (render_style) {
        case LEVEL_RENDER_DEFENDER: return "level_defender";
        case LEVEL_RENDER_CYLINDER: return "level_enemy_radar";
        case LEVEL_RENDER_DRIFTER: return "level_high_plains_drifter";
        case LEVEL_RENDER_DRIFTER_SHADED: return "level_high_plains_drifter_2";
        case LEVEL_RENDER_FOG: return "level_fog_of_war";
        case LEVEL_RENDER_BLANK: return "level_blank";
        default: return "level_defender";
    }
}

static int event_kind_from_marker_kind(int marker_kind) {
    if (marker_kind == LEVEL_EDITOR_MARKER_WAVE_V) return LEVELDEF_EVENT_WAVE_V;
    if (marker_kind == LEVEL_EDITOR_MARKER_WAVE_KAMIKAZE) return LEVELDEF_EVENT_WAVE_KAMIKAZE;
    if (marker_kind == LEVEL_EDITOR_MARKER_ASTEROID_STORM) return LEVELDEF_EVENT_ASTEROID_STORM;
    if (marker_kind == LEVEL_EDITOR_MARKER_BOID_FISH) return LEVELDEF_EVENT_WAVE_SWARM_FISH;
    if (marker_kind == LEVEL_EDITOR_MARKER_BOID_FIREFLY) return LEVELDEF_EVENT_WAVE_SWARM_FIREFLY;
    if (marker_kind == LEVEL_EDITOR_MARKER_BOID_BIRD) return LEVELDEF_EVENT_WAVE_SWARM_BIRD;
    if (marker_kind == LEVEL_EDITOR_MARKER_BOID) return LEVELDEF_EVENT_WAVE_SWARM;
    return LEVELDEF_EVENT_WAVE_SINE;
}

static int wave_pattern_from_marker_kind(int marker_kind) {
    if (marker_kind == LEVEL_EDITOR_MARKER_WAVE_V) return LEVELDEF_WAVE_V_FORMATION;
    if (marker_kind == LEVEL_EDITOR_MARKER_WAVE_KAMIKAZE) return LEVELDEF_WAVE_KAMIKAZE;
    if (marker_kind == LEVEL_EDITOR_MARKER_ASTEROID_STORM) return LEVELDEF_WAVE_ASTEROID_STORM;
    if (marker_kind == LEVEL_EDITOR_MARKER_BOID_FISH) return LEVELDEF_WAVE_SWARM_FISH;
    if (marker_kind == LEVEL_EDITOR_MARKER_BOID_FIREFLY) return LEVELDEF_WAVE_SWARM_FIREFLY;
    if (marker_kind == LEVEL_EDITOR_MARKER_BOID_BIRD) return LEVELDEF_WAVE_SWARM_BIRD;
    if (marker_kind == LEVEL_EDITOR_MARKER_BOID) return LEVELDEF_WAVE_SWARM;
    return LEVELDEF_WAVE_SINE_SNAKE;
}

static int level_style_from_render_style(int render_style) {
    switch (render_style) {
        case LEVEL_RENDER_CYLINDER: return LEVEL_STYLE_ENEMY_RADAR;
        case LEVEL_RENDER_DRIFTER: return LEVEL_STYLE_HIGH_PLAINS_DRIFTER;
        case LEVEL_RENDER_DRIFTER_SHADED: return LEVEL_STYLE_HIGH_PLAINS_DRIFTER_2;
        case LEVEL_RENDER_FOG: return LEVEL_STYLE_FOG_OF_WAR;
        case LEVEL_RENDER_BLANK: return LEVEL_STYLE_BLANK;
        default: return LEVEL_STYLE_DEFENDER;
    }
}

static int appendf(char* out, size_t out_cap, size_t* used, const char* fmt, ...) {
    int n = 0;
    va_list ap;
    if (!out || !used || *used >= out_cap) {
        return 0;
    }
    va_start(ap, fmt);
    n = vsnprintf(out + *used, out_cap - *used, fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= (out_cap - *used)) {
        return 0;
    }
    *used += (size_t)n;
    return 1;
}

static int build_level_serialized_text(
    const level_editor_state* s,
    const leveldef_db* db,
    char* out,
    size_t out_cap,
    leveldef_level* out_level
) {
    size_t used = 0;
    leveldef_level lvl;
    int searchlight_n = 0;
    int missile_n = 0;
    int i = 0;
    int found_exit = 0;
    int wave_n = 0;
    int has_spatial_wave = 0;
    int has_event_wave = 0;
    wave_marker_ref waves[LEVEL_EDITOR_MAX_MARKERS];
    const float level_len = fmaxf(s ? s->level_length_screens : 1.0f, 1.0f);

    if (!s || !db || (!out_level && (!out || out_cap == 0))) {
        return 0;
    }
    if (s->loaded_level_valid) {
        lvl = s->loaded_level;
    } else {
        const leveldef_level* base = leveldef_get_level(db, s->level_style);
        if (base) {
            lvl = *base;
        } else {
            memset(&lvl, 0, sizeof(lvl));
            lvl.spawn_mode = LEVELDEF_SPAWN_SEQUENCED_CLEAR;
            lvl.spawn_interval_s = 2.0f;
            lvl.default_boid_profile = leveldef_find_boid_profile(db, "FISH");
            if (lvl.default_boid_profile < 0) {
                lvl.default_boid_profile = 0;
            }
            lvl.wave_cooldown_initial_s = 0.65f;
            lvl.wave_cooldown_between_s = 2.0f;
            lvl.exit_y01 = 0.5f;
        }
    }
    lvl.render_style = s->level_render_style;
    lvl.wave_mode = s->level_wave_mode;
    lvl.theme_palette = s->level_theme_palette;
    lvl.editor_length_screens = level_len;
    lvl.searchlight_count = 0;
    lvl.minefield_count = 0;
    lvl.missile_launcher_count = 0;
    lvl.arc_node_count = 0;
    lvl.structure_count = 0;
    lvl.event_count = 0;
    lvl.curated_count = 0;
    lvl.wave_cycle_count = 0;
    lvl.boid_cycle_count = 0;
    /* Spatial asteroid storms are source-of-truth from spatial markers only.
       Event-lane storms are serialized as event entries, not this flag. */
    lvl.asteroid_storm_enabled = 0;
    lvl.asteroid_storm_angle_deg = s->level_asteroid_storm_angle_deg;
    lvl.asteroid_storm_speed = s->level_asteroid_storm_speed;
    lvl.asteroid_storm_duration_s = s->level_asteroid_storm_duration_s;
    lvl.asteroid_storm_density = s->level_asteroid_storm_density;
    lvl.asteroid_storm_start_x01 = 0.0f;

    lvl.exit_enabled = 0;
    for (i = 0; i < s->marker_count; ++i) {
        const level_editor_marker* m = &s->markers[i];
        if (is_wave_kind(m->kind)) {
            if (m->track == LEVEL_EDITOR_TRACK_SPATIAL) {
                has_spatial_wave = 1;
            } else if (m->track == LEVEL_EDITOR_TRACK_EVENT) {
                has_event_wave = 1;
            }
        }
        if (m->kind == LEVEL_EDITOR_MARKER_EXIT && !found_exit) {
            lvl.exit_enabled = 1;
            lvl.exit_x01 = m->x01 * level_len;
            lvl.exit_y01 = m->y01;
            found_exit = 1;
        } else if (m->kind == LEVEL_EDITOR_MARKER_ASTEROID_STORM &&
                   m->track == LEVEL_EDITOR_TRACK_SPATIAL) {
            lvl.asteroid_storm_enabled = 1;
            lvl.asteroid_storm_start_x01 = m->x01 * level_len;
            lvl.asteroid_storm_duration_s = fmaxf(m->a, 0.01f);
            lvl.asteroid_storm_angle_deg = m->b;
            lvl.asteroid_storm_speed = fmaxf(m->c, 1.0f);
            lvl.asteroid_storm_density = fmaxf(m->d, 0.01f);
        }
    }

    /* Serialization consistency rule:
       a level file cannot represent spatial swarm markers in non-curated mode,
       so we normalize wave_mode before writing the config text. */
    if (has_spatial_wave) {
        lvl.wave_mode = LEVELDEF_WAVES_CURATED;
    } else if (!has_event_wave && lvl.wave_mode == LEVELDEF_WAVES_CURATED) {
        /* No wave markers at all: keep file coherent in normal mode. */
        lvl.wave_mode = LEVELDEF_WAVES_NORMAL;
    }

    lvl.searchlight_count = 0;
    for (i = 0; i < s->marker_count && searchlight_n < MAX_SEARCHLIGHTS; ++i) {
        const level_editor_marker* m = &s->markers[i];
        leveldef_searchlight sl;
        if (m->kind != LEVEL_EDITOR_MARKER_SEARCHLIGHT) {
            continue;
        }
        memset(&sl, 0, sizeof(sl));
        sl.sweep_motion = clampi((int)lroundf(m->g), SEARCHLIGHT_MOTION_LINEAR, SEARCHLIGHT_MOTION_SPIN);
        sl.source_type = clampi((int)lroundf(m->e), SEARCHLIGHT_SOURCE_DOME, SEARCHLIGHT_SOURCE_ORB);
        sl.source_radius = (m->f > 0.0f) ? m->f : 14.0f;
        sl.clear_grace_s = 2.0f;
        sl.fire_interval_s = 0.08f;
        sl.projectile_speed = 900.0f;
        sl.projectile_ttl_s = 2.0f;
        sl.projectile_radius = 3.2f;
        sl.aim_jitter_deg = 1.0f;
        sl.anchor_x01 = m->x01 * level_len;
        sl.anchor_y01 = m->y01;
        sl.length_h01 = m->a;
        sl.half_angle_deg = m->b;
        sl.sweep_speed = m->c;
        sl.sweep_amplitude_deg = m->d * 0.5f;
        lvl.searchlights[searchlight_n++] = sl;
    }
    lvl.searchlight_count = searchlight_n;
    lvl.minefield_count = 0;
    for (i = 0; i < s->marker_count && lvl.minefield_count < LEVELDEF_MAX_MINEFIELDS; ++i) {
        const level_editor_marker* m = &s->markers[i];
        leveldef_minefield mf;
        if (m->kind != LEVEL_EDITOR_MARKER_MINEFIELD || m->track != LEVEL_EDITOR_TRACK_SPATIAL) {
            continue;
        }
        mf.anchor_x01 = m->x01 * level_len;
        mf.anchor_y01 = m->y01;
        mf.count = (int)lroundf(fmaxf(m->a, 1.0f));
        lvl.minefields[lvl.minefield_count++] = mf;
    }
    lvl.missile_launcher_count = 0;
    for (i = 0; i < s->marker_count && lvl.missile_launcher_count < LEVELDEF_MAX_MISSILE_LAUNCHERS; ++i) {
        const level_editor_marker* m = &s->markers[i];
        leveldef_missile_launcher ml;
        if (m->kind != LEVEL_EDITOR_MARKER_MISSILE || m->track != LEVEL_EDITOR_TRACK_SPATIAL) {
            continue;
        }
        memset(&ml, 0, sizeof(ml));
        ml.missile_speed = 430.0f;
        ml.missile_turn_rate_deg = 90.0f;
        ml.hit_radius = 20.0f;
        ml.blast_radius = 80.0f;
        ml.anchor_x01 = m->x01 * level_len;
        ml.anchor_y01 = m->y01;
        ml.count = (int)lroundf(fmaxf(m->a, 1.0f));
        ml.spacing = fmaxf(m->b, 0.0f);
        ml.activation_range = fmaxf(m->c, 10.0f);
        ml.missile_ttl_s = fmaxf(m->d, 0.20f);
        lvl.missile_launchers[lvl.missile_launcher_count++] = ml;
        missile_n += 1;
    }
    lvl.arc_node_count = 0;
    for (i = 0; i < s->marker_count && lvl.arc_node_count < LEVELDEF_MAX_ARC_NODES; ++i) {
        const level_editor_marker* m = &s->markers[i];
        leveldef_arc_node an;
        if (m->kind != LEVEL_EDITOR_MARKER_ARC_NODE || m->track != LEVEL_EDITOR_TRACK_SPATIAL) {
            continue;
        }
        memset(&an, 0, sizeof(an));
        an.anchor_x01 = m->x01 * level_len;
        an.anchor_y01 = m->y01;
        an.period_s = fmaxf(m->a, 0.10f);
        an.on_s = clampf(m->b, 0.0f, an.period_s);
        an.radius = fmaxf(m->c, 4.0f);
        an.push_accel = fmaxf(m->d, 0.0f);
        an.damage_interval_s = fmaxf(m->e, 0.02f);
        lvl.arc_nodes[lvl.arc_node_count++] = an;
        fprintf(
            stderr,
            "[arc_trace] editor->level marker idx=%d x01=%.6f y01=%.6f level_len=%.6f save_anchor_x01=%.6f save_anchor_y01=%.6f\n",
            i,
            m->x01,
            m->y01,
            level_len,
            an.anchor_x01,
            an.anchor_y01
        );
    }
    lvl.structure_count = 0;
    for (i = 0; i < s->marker_count && lvl.structure_count < LEVELDEF_MAX_STRUCTURES; ++i) {
        const level_editor_marker* m = &s->markers[i];
        leveldef_structure_instance st;
        if (m->kind != LEVEL_EDITOR_MARKER_STRUCTURE || m->track != LEVEL_EDITOR_TRACK_SPATIAL) {
            continue;
        }
        memset(&st, 0, sizeof(st));
        st.prefab_id = clampi((int)lroundf(m->a), 0, 31);
        st.layer = clampi((int)lroundf(m->b), 0, 1);
        st.grid_x = clampi(
            (int)lroundf(m->x01 * (float)structure_grid_x_steps_for_level(level_len)),
            0,
            structure_grid_x_steps_for_level(level_len)
        );
        st.grid_y = clampi((int)lroundf(m->y01 * (float)structure_grid_y_steps()), 0, structure_grid_y_steps());
        st.rotation_quadrants = clampi((int)lroundf(m->c), 0, 3);
        {
            const int flip = clampi((int)lroundf(m->d), 0, 3);
            st.flip_x = (flip & 1) ? 1 : 0;
            st.flip_y = (flip & 2) ? 1 : 0;
        }
        structure_prefab_dims(st.prefab_id, &st.w_units, &st.h_units);
        {
            const int max_gx = structure_grid_x_steps_for_level(level_len) - st.w_units + 1;
            const int max_gy = structure_grid_y_steps() - st.h_units + 1;
            st.grid_x = clampi(st.grid_x, 0, (max_gx > 0) ? max_gx : 0);
            st.grid_y = clampi(st.grid_y, 0, (max_gy > 0) ? max_gy : 0);
        }
        st.variant = 0;
        st.vent_density = fmaxf(m->e, 0.01f);
        st.vent_opacity = fmaxf(m->f, 0.01f);
        st.vent_plume_height = fmaxf(m->g, 0.01f);
        lvl.structures[lvl.structure_count++] = st;
    }

    for (i = 0; i < s->marker_count; ++i) {
        const level_editor_marker* m = &s->markers[i];
        int include = 0;
        if (lvl.wave_mode == LEVELDEF_WAVES_CURATED) {
            include = is_wave_kind(m->kind) && (m->track == LEVEL_EDITOR_TRACK_SPATIAL);
        } else {
            include = marker_is_event_item(s, m);
        }
        if (!include || wave_n >= LEVEL_EDITOR_MAX_MARKERS) {
            continue;
        }
        waves[wave_n].x01 = m->x01;
        waves[wave_n].marker = *m;
        ++wave_n;
    }
    if (wave_n > 1) {
        qsort(waves, (size_t)wave_n, sizeof(waves[0]), compare_wave_marker_ref);
    }
    lvl.kamikaze.radius_min = fmaxf(1.0f, s->level_kamikaze_radius_min);
    lvl.kamikaze.radius_max = fmaxf(lvl.kamikaze.radius_min, s->level_kamikaze_radius_max);
    lvl.event_count = 0;
    if (lvl.wave_mode != LEVELDEF_WAVES_CURATED) {
        for (i = 0; i < wave_n && lvl.event_count < LEVELDEF_MAX_EVENTS; ++i) {
            const level_editor_marker* m = &waves[i].marker;
            leveldef_event_entry ev;
            ev.kind = event_kind_from_marker_kind(m->kind);
            ev.order = (m->order > 0) ? m->order : (lvl.event_count + 1);
            ev.delay_s = fmaxf(m->delay_s, 0.0f);
            lvl.events[lvl.event_count++] = ev;
        }
    }

    if (lvl.wave_mode == LEVELDEF_WAVES_CURATED) {
        int curated_n = 0;
        for (i = 0; i < wave_n && curated_n < (int)(sizeof(lvl.curated) / sizeof(lvl.curated[0])); ++i) {
            const level_editor_marker* m = &waves[i].marker;
            if (!is_wave_kind(m->kind)) {
                continue;
            }
            lvl.curated[curated_n].kind = m->kind;
            lvl.curated[curated_n].x01 = m->x01 * level_len;
            lvl.curated[curated_n].y01 = m->y01;
            lvl.curated[curated_n].a = m->a;
            lvl.curated[curated_n].b = m->b;
            lvl.curated[curated_n].c = m->c;
            ++curated_n;
        }
        lvl.curated_count = curated_n;
    } else {
        int cycle_n = 0;
        if (lvl.event_count > 0) {
            for (i = 0; i < lvl.event_count && cycle_n < LEVELDEF_MAX_BOID_CYCLE; ++i) {
                if (lvl.events[i].kind == LEVELDEF_EVENT_WAVE_V) {
                    lvl.wave_cycle[cycle_n++] = LEVELDEF_WAVE_V_FORMATION;
                } else if (lvl.events[i].kind == LEVELDEF_EVENT_WAVE_KAMIKAZE) {
                    lvl.wave_cycle[cycle_n++] = LEVELDEF_WAVE_KAMIKAZE;
                } else if (lvl.events[i].kind == LEVELDEF_EVENT_WAVE_SWARM) {
                    lvl.wave_cycle[cycle_n++] = LEVELDEF_WAVE_SWARM;
                } else if (lvl.events[i].kind == LEVELDEF_EVENT_WAVE_SWARM_FISH) {
                    lvl.wave_cycle[cycle_n++] = LEVELDEF_WAVE_SWARM_FISH;
                } else if (lvl.events[i].kind == LEVELDEF_EVENT_WAVE_SWARM_FIREFLY) {
                    lvl.wave_cycle[cycle_n++] = LEVELDEF_WAVE_SWARM_FIREFLY;
                } else if (lvl.events[i].kind == LEVELDEF_EVENT_WAVE_SWARM_BIRD) {
                    lvl.wave_cycle[cycle_n++] = LEVELDEF_WAVE_SWARM_BIRD;
                } else if (lvl.events[i].kind == LEVELDEF_EVENT_ASTEROID_STORM) {
                    lvl.wave_cycle[cycle_n++] = LEVELDEF_WAVE_ASTEROID_STORM;
                } else {
                    lvl.wave_cycle[cycle_n++] = LEVELDEF_WAVE_SINE_SNAKE;
                }
            }
            lvl.wave_cycle_count = cycle_n;
        } else {
        int idx = 0;
        for (i = 0; i < wave_n && cycle_n < LEVELDEF_MAX_BOID_CYCLE; ++i) {
            const level_editor_marker* m = &waves[i].marker;
            if (is_boid_wave_kind(m->kind)) {
                lvl.wave_cycle[cycle_n++] = wave_pattern_from_marker_kind(m->kind);
                continue;
            }
            if (m->kind == LEVEL_EDITOR_MARKER_WAVE_SINE) {
                lvl.wave_cycle[cycle_n++] = LEVELDEF_WAVE_SINE_SNAKE;
            } else if (m->kind == LEVEL_EDITOR_MARKER_WAVE_V) {
                lvl.wave_cycle[cycle_n++] = LEVELDEF_WAVE_V_FORMATION;
            } else if (m->kind == LEVEL_EDITOR_MARKER_WAVE_KAMIKAZE) {
                lvl.wave_cycle[cycle_n++] = LEVELDEF_WAVE_KAMIKAZE;
            }
        }
        if (cycle_n > 0) {
            lvl.wave_cycle_count = cycle_n;
        }
        for (i = 0; i < lvl.wave_cycle_count; ++i) {
            const level_editor_marker* m = &waves[i].marker;
            const float slots = (float)((lvl.wave_cycle_count > 0) ? lvl.wave_cycle_count : 1);
            const float wave_base = ((float)i / slots) * (level_len - 1.0f);
            const float local_start = m->x01 * level_len - wave_base;
            if (m->kind == LEVEL_EDITOR_MARKER_WAVE_SINE) {
                lvl.sine.start_x01 = local_start;
                lvl.sine.home_y01 = m->y01;
                lvl.sine.count = (int)lroundf(m->a);
                lvl.sine.form_amp = m->b;
                lvl.sine.max_speed = m->c;
            } else if (m->kind == LEVEL_EDITOR_MARKER_WAVE_V) {
                lvl.v.start_x01 = local_start;
                lvl.v.home_y01 = m->y01;
                lvl.v.count = (int)lroundf(m->a);
                lvl.v.form_amp = m->b;
                lvl.v.max_speed = m->c;
            } else if (m->kind == LEVEL_EDITOR_MARKER_WAVE_KAMIKAZE) {
                lvl.kamikaze.start_x01 = local_start;
                lvl.kamikaze.count = (int)lroundf(m->a);
                lvl.kamikaze.max_speed = m->b;
                lvl.kamikaze.accel = m->c;
            }
        }
        }
    }

    if (out_level) {
        *out_level = lvl;
    }
    if (!out || out_cap == 0) {
        return 1;
    }

    if (!appendf(out, out_cap, &used, "# LevelDef v1\n")) return 0;
    if (!appendf(out, out_cap, &used, "# wave_cycle tokens: sine_snake,v_formation,swarm,swarm_fish,swarm_firefly,swarm_bird,kamikaze,asteroid_storm\n")) return 0;
    if (!appendf(out, out_cap, &used, "# event fields: kind,order,delay_s\n")) return 0;
    if (!appendf(out, out_cap, &used, "# searchlight CSV fields:\n")) return 0;
    if (!appendf(out, out_cap, &used, "# anchor_x01,anchor_y01,length_h01,half_angle_deg,sweep_center_deg,sweep_amplitude_deg,\n")) return 0;
    if (!appendf(out, out_cap, &used, "# sweep_speed,sweep_phase_deg,sweep_motion,source_type,source_radius,clear_grace_s,\n")) return 0;
    if (!appendf(out, out_cap, &used, "# fire_interval_s,projectile_speed,projectile_ttl_s,projectile_radius,aim_jitter_deg\n")) return 0;
    if (!appendf(out, out_cap, &used, "# minefield CSV fields: anchor_x01,anchor_y01,count\n")) return 0;
    if (!appendf(out, out_cap, &used, "# missile_launcher CSV fields:\n")) return 0;
    if (!appendf(out, out_cap, &used, "# anchor_x01,anchor_y01,count,spacing,activation_range,missile_speed,\n")) return 0;
    if (!appendf(out, out_cap, &used, "# missile_turn_rate_deg,missile_ttl_s,hit_radius,blast_radius\n")) return 0;
    if (!appendf(out, out_cap, &used, "# arc_node CSV fields: anchor_x01,anchor_y01,period_s,on_s,radius,push_accel,damage_interval_s\n")) return 0;
    if (!appendf(out, out_cap, &used, "# structure CSV fields:\n")) return 0;
    if (!appendf(out, out_cap, &used, "# prefab_id,layer,grid_x,grid_y,rotation_quadrants,flip_x,flip_y,w_units,h_units,variant,vent_density,vent_opacity,vent_plume_height\n")) return 0;
    if (!appendf(out, out_cap, &used, "[level %s]\n", style_header_name(s->level_style))) return 0;
    if (!appendf(out, out_cap, &used, "level_length_screens=%.3f\n", lvl.editor_length_screens)) return 0;
    if (!appendf(out, out_cap, &used, "render_style=%s\n", render_style_name(lvl.render_style))) return 0;
    if (!appendf(out, out_cap, &used, "wave_mode=%s\n", wave_mode_name(lvl.wave_mode))) return 0;
    if (!appendf(out, out_cap, &used, "theme_palette=%d\n", clampi(lvl.theme_palette, 0, 2))) return 0;
    if (!appendf(out, out_cap, &used, "spawn_mode=%s\n", spawn_mode_name(lvl.spawn_mode))) return 0;
    if (!appendf(out, out_cap, &used, "spawn_interval_s=%.3f\n", lvl.spawn_interval_s)) return 0;
    if (lvl.default_boid_profile >= 0 && lvl.default_boid_profile < db->profile_count) {
        if (!appendf(out, out_cap, &used, "default_boid_profile=%s\n", db->profiles[lvl.default_boid_profile].name)) return 0;
    } else {
        if (!appendf(out, out_cap, &used, "default_boid_profile=FISH\n")) return 0;
    }
    if (!appendf(out, out_cap, &used, "wave_cooldown_initial_s=%.3f\n", lvl.wave_cooldown_initial_s)) return 0;
    if (!appendf(out, out_cap, &used, "wave_cooldown_between_s=%.3f\n", lvl.wave_cooldown_between_s)) return 0;
    if (!appendf(out, out_cap, &used, "bidirectional_spawns=%d\n", lvl.bidirectional_spawns ? 1 : 0)) return 0;
    if (!appendf(out, out_cap, &used, "cylinder_double_swarm_chance=%.3f\n", lvl.cylinder_double_swarm_chance)) return 0;
    if (!appendf(out, out_cap, &used, "exit_enabled=%d\n", lvl.exit_enabled ? 1 : 0)) return 0;
    if (!appendf(out, out_cap, &used, "exit_x01=%.3f\n", lvl.exit_x01)) return 0;
    if (!appendf(out, out_cap, &used, "exit_y01=%.3f\n", lvl.exit_y01)) return 0;
    if (!appendf(out, out_cap, &used, "asteroid_storm_enabled=%d\n", lvl.asteroid_storm_enabled ? 1 : 0)) return 0;
    if (!appendf(out, out_cap, &used, "asteroid_storm_start_x01=%.3f\n", lvl.asteroid_storm_start_x01)) return 0;
    if (!appendf(out, out_cap, &used, "asteroid_storm_angle_deg=%.3f\n", lvl.asteroid_storm_angle_deg)) return 0;
    if (!appendf(out, out_cap, &used, "asteroid_storm_speed=%.3f\n", lvl.asteroid_storm_speed)) return 0;
    if (!appendf(out, out_cap, &used, "asteroid_storm_duration_s=%.3f\n", lvl.asteroid_storm_duration_s)) return 0;
    if (!appendf(out, out_cap, &used, "asteroid_storm_density=%.3f\n", lvl.asteroid_storm_density)) return 0;

    if (lvl.wave_mode == LEVELDEF_WAVES_CURATED) {
        for (i = 0; i < lvl.curated_count; ++i) {
            const leveldef_curated_enemy* ce = &lvl.curated[i];
            if (!appendf(out, out_cap, &used, "curated_enemy=%s,%.3f,%.3f,%.3f,%.3f,%.3f\n",
                curated_kind_name(ce->kind), ce->x01, ce->y01, ce->a, ce->b, ce->c)) return 0;
        }
    } else {
        if (!appendf(out, out_cap, &used, "wave_cycle=")) return 0;
        for (i = 0; i < lvl.wave_cycle_count; ++i) {
            if (!appendf(out, out_cap, &used, "%s%s", (i > 0) ? "," : "", wave_pattern_name(lvl.wave_cycle[i]))) return 0;
        }
        if (!appendf(out, out_cap, &used, "\n")) return 0;
    }
    if (lvl.event_count > 0) {
        static const char* names[] = {"sine", "v", "swarm", "kamikaze", "asteroid", "swarm_fish", "swarm_firefly", "swarm_bird"};
        for (i = 0; i < lvl.event_count; ++i) {
            const int ek = lvl.events[i].kind;
            const char* en = (ek >= 0 && ek <= 7) ? names[ek] : "sine";
            if (!appendf(out, out_cap, &used, "event=%s,%d,%.3f\n", en, lvl.events[i].order, lvl.events[i].delay_s)) return 0;
        }
    }

    {
        int need_sine = (lvl.sine.count > 0);
        int need_v = (lvl.v.count > 0);
        int need_kamikaze = (lvl.kamikaze.count > 0);
        for (i = 0; i < lvl.wave_cycle_count; ++i) {
            if (lvl.wave_cycle[i] == LEVELDEF_WAVE_SINE_SNAKE) need_sine = 1;
            if (lvl.wave_cycle[i] == LEVELDEF_WAVE_V_FORMATION) need_v = 1;
            if (lvl.wave_cycle[i] == LEVELDEF_WAVE_KAMIKAZE) need_kamikaze = 1;
        }
        for (i = 0; i < lvl.event_count; ++i) {
            if (lvl.events[i].kind == LEVELDEF_EVENT_WAVE_SINE) need_sine = 1;
            if (lvl.events[i].kind == LEVELDEF_EVENT_WAVE_V) need_v = 1;
            if (lvl.events[i].kind == LEVELDEF_EVENT_WAVE_KAMIKAZE) need_kamikaze = 1;
        }
        if (need_sine) {
            if (!appendf(out, out_cap, &used, "sine.count=%d\n", lvl.sine.count)) return 0;
            if (!appendf(out, out_cap, &used, "sine.start_x01=%.3f\n", lvl.sine.start_x01)) return 0;
            if (!appendf(out, out_cap, &used, "sine.spacing_x=%.3f\n", lvl.sine.spacing_x)) return 0;
            if (!appendf(out, out_cap, &used, "sine.home_y01=%.3f\n", lvl.sine.home_y01)) return 0;
            if (!appendf(out, out_cap, &used, "sine.phase_step=%.3f\n", lvl.sine.phase_step)) return 0;
            if (!appendf(out, out_cap, &used, "sine.form_amp=%.3f\n", lvl.sine.form_amp)) return 0;
            if (!appendf(out, out_cap, &used, "sine.form_freq=%.3f\n", lvl.sine.form_freq)) return 0;
            if (!appendf(out, out_cap, &used, "sine.break_delay_base=%.3f\n", lvl.sine.break_delay_base)) return 0;
            if (!appendf(out, out_cap, &used, "sine.break_delay_step=%.3f\n", lvl.sine.break_delay_step)) return 0;
            if (!appendf(out, out_cap, &used, "sine.max_speed=%.3f\n", lvl.sine.max_speed)) return 0;
            if (!appendf(out, out_cap, &used, "sine.accel=%.3f\n", lvl.sine.accel)) return 0;
        }
        if (need_v) {
            if (!appendf(out, out_cap, &used, "v.count=%d\n", lvl.v.count)) return 0;
            if (!appendf(out, out_cap, &used, "v.start_x01=%.3f\n", lvl.v.start_x01)) return 0;
            if (!appendf(out, out_cap, &used, "v.spacing_x=%.3f\n", lvl.v.spacing_x)) return 0;
            if (!appendf(out, out_cap, &used, "v.home_y01=%.3f\n", lvl.v.home_y01)) return 0;
            if (!appendf(out, out_cap, &used, "v.home_y_step=%.3f\n", lvl.v.home_y_step)) return 0;
            if (!appendf(out, out_cap, &used, "v.phase_step=%.3f\n", lvl.v.phase_step)) return 0;
            if (!appendf(out, out_cap, &used, "v.form_amp=%.3f\n", lvl.v.form_amp)) return 0;
            if (!appendf(out, out_cap, &used, "v.form_freq=%.3f\n", lvl.v.form_freq)) return 0;
            if (!appendf(out, out_cap, &used, "v.break_delay_min=%.3f\n", lvl.v.break_delay_min)) return 0;
            if (!appendf(out, out_cap, &used, "v.break_delay_rand=%.3f\n", lvl.v.break_delay_rand)) return 0;
            if (!appendf(out, out_cap, &used, "v.max_speed=%.3f\n", lvl.v.max_speed)) return 0;
            if (!appendf(out, out_cap, &used, "v.accel=%.3f\n", lvl.v.accel)) return 0;
        }
        if (need_kamikaze) {
            if (!appendf(out, out_cap, &used, "kamikaze.count=%d\n", lvl.kamikaze.count)) return 0;
            if (!appendf(out, out_cap, &used, "kamikaze.start_x01=%.3f\n", lvl.kamikaze.start_x01)) return 0;
            if (!appendf(out, out_cap, &used, "kamikaze.spacing_x=%.3f\n", lvl.kamikaze.spacing_x)) return 0;
            if (!appendf(out, out_cap, &used, "kamikaze.y_margin=%.3f\n", lvl.kamikaze.y_margin)) return 0;
            if (!appendf(out, out_cap, &used, "kamikaze.max_speed=%.3f\n", lvl.kamikaze.max_speed)) return 0;
            if (!appendf(out, out_cap, &used, "kamikaze.accel=%.3f\n", lvl.kamikaze.accel)) return 0;
            if (!appendf(out, out_cap, &used, "kamikaze.radius_min=%.3f\n", lvl.kamikaze.radius_min)) return 0;
            if (!appendf(out, out_cap, &used, "kamikaze.radius_max=%.3f\n", lvl.kamikaze.radius_max)) return 0;
        }
    }

    for (i = 0; i < lvl.searchlight_count; ++i) {
        const leveldef_searchlight* sl = &lvl.searchlights[i];
        if (!appendf(out, out_cap, &used,
            "searchlight=%.6f,%.6f,%.6f,%.3f,%.3f,%.3f,%.3f,%.3f,%s,%s,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f\n",
            sl->anchor_x01, sl->anchor_y01, sl->length_h01, sl->half_angle_deg, sl->sweep_center_deg,
            sl->sweep_amplitude_deg, sl->sweep_speed, sl->sweep_phase_deg, searchlight_motion_name(sl->sweep_motion),
            searchlight_source_name(sl->source_type), sl->source_radius, sl->clear_grace_s, sl->fire_interval_s,
            sl->projectile_speed, sl->projectile_ttl_s, sl->projectile_radius, sl->aim_jitter_deg)) return 0;
    }
    for (i = 0; i < lvl.minefield_count; ++i) {
        const leveldef_minefield* mf = &lvl.minefields[i];
        if (!appendf(
                out,
                out_cap,
                &used,
                "minefield=%.6f,%.6f,%d\n",
                mf->anchor_x01,
                mf->anchor_y01,
                mf->count)) return 0;
    }
    for (i = 0; i < lvl.missile_launcher_count; ++i) {
        const leveldef_missile_launcher* ml = &lvl.missile_launchers[i];
        if (!appendf(
                out,
                out_cap,
                &used,
                "missile_launcher=%.6f,%.6f,%d,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f\n",
                ml->anchor_x01,
                ml->anchor_y01,
                ml->count,
                ml->spacing,
                ml->activation_range,
                ml->missile_speed,
                ml->missile_turn_rate_deg,
                ml->missile_ttl_s,
                ml->hit_radius,
                ml->blast_radius)) return 0;
    }
    for (i = 0; i < lvl.arc_node_count; ++i) {
        const leveldef_arc_node* an = &lvl.arc_nodes[i];
        if (!appendf(
                out,
                out_cap,
                &used,
                "arc_node=%.6f,%.6f,%.3f,%.3f,%.3f,%.3f,%.3f\n",
                an->anchor_x01,
                an->anchor_y01,
                an->period_s,
                an->on_s,
                an->radius,
                an->push_accel,
                an->damage_interval_s)) return 0;
    }
    for (i = 0; i < lvl.structure_count; ++i) {
        const leveldef_structure_instance* st = &lvl.structures[i];
        if (!appendf(
                out,
                out_cap,
                &used,
                "structure=%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%.3f,%.3f,%.3f\n",
                st->prefab_id,
                st->layer,
                st->grid_x,
                st->grid_y,
                st->rotation_quadrants,
                st->flip_x,
                st->flip_y,
                st->w_units,
                st->h_units,
                st->variant,
                (st->vent_density > 0.0f) ? st->vent_density : 1.0f,
                (st->vent_opacity > 0.0f) ? st->vent_opacity : 1.0f,
                (st->vent_plume_height > 0.0f) ? st->vent_plume_height : 1.0f)) return 0;
    }

    return 1;
}

static int marker_property_count(const level_editor_state* s) {
    if (!s) {
        return 0;
    }
    if (s->selected_marker < 0 || s->selected_marker >= s->marker_count) {
        return 4; /* WAVE MODE, RENDER STYLE, THEME, LENGTH */
    }
    const int kind = s->markers[s->selected_marker].kind;
    if (kind == LEVEL_EDITOR_MARKER_ASTEROID_STORM) {
        return 6; /* event: ORDER,DELAY,DUR,ANGLE,SPEED,DENSITY | spatial: X,Y,DUR,ANGLE,SPEED,DENSITY */
    }
    if (kind == LEVEL_EDITOR_MARKER_SEARCHLIGHT) {
        return 9;
    }
    if (kind == LEVEL_EDITOR_MARKER_MINEFIELD) {
        return 3;
    }
    if (kind == LEVEL_EDITOR_MARKER_MISSILE) {
        return 6;
    }
    if (kind == LEVEL_EDITOR_MARKER_ARC_NODE) {
        return 7;
    }
    if (kind == LEVEL_EDITOR_MARKER_STRUCTURE) {
        return 9;
    }
    if (kind == LEVEL_EDITOR_MARKER_EXIT) {
        return 2;
    }
    if (is_wave_kind(kind)) {
        const int ev_item = marker_is_event_item(s, &s->markers[s->selected_marker]);
        if (is_boid_wave_kind(kind)) {
            return ev_item ? 7 : 8; /* event: TYPE,ORDER,DELAY,COUNT,SPEED,ACCEL,TURN | spatial: TYPE,X,Y,COUNT,SPEED,ACCEL,TURN,DELAY */
        }
        if (kind == LEVEL_EDITOR_MARKER_WAVE_KAMIKAZE) {
            return ev_item ? 8 : 9; /* event: TYPE,ORDER,DELAY,COUNT,SPEED,ACCEL,R MIN,R MAX | spatial adds DELAY */
        }
        return ev_item ? 6 : 7; /* event: TYPE,ORDER,DELAY,A,B,C | spatial: TYPE,X,Y,A,B,C,DELAY */
    }
    return 2;
}

static int cycle_wave_kind(int kind, int step) {
    static const int kinds[] = {
        LEVEL_EDITOR_MARKER_WAVE_SINE,
        LEVEL_EDITOR_MARKER_WAVE_V,
        LEVEL_EDITOR_MARKER_WAVE_KAMIKAZE,
        LEVEL_EDITOR_MARKER_BOID_FISH,
        LEVEL_EDITOR_MARKER_BOID_FIREFLY,
        LEVEL_EDITOR_MARKER_BOID_BIRD
    };
    const int nk = (int)(sizeof(kinds) / sizeof(kinds[0]));
    int idx = 0;
    for (int i = 0; i < nk; ++i) {
        if (kinds[i] == kind) {
            idx = i;
            break;
        }
    }
    idx = (idx + step + nk) % nk;
    return kinds[idx];
}

static void remap_level_length(level_editor_state* s, float new_len, int selected_index, float selected_abs_x) {
    if (!s) {
        return;
    }
    const float old_len = fmaxf(s->level_length_screens, 1.0f);
    new_len = fmaxf(new_len, 1.0f);
    if (fabsf(new_len - old_len) < 1.0e-4f) {
        return;
    }
    for (int i = 0; i < s->marker_count; ++i) {
        const float abs_x = (i == selected_index) ? selected_abs_x : (s->markers[i].x01 * old_len);
        s->markers[i].x01 = clampf(abs_x / new_len, 0.0f, 1.0f);
    }
    s->level_length_screens = new_len;
}

static void auto_pan_to_marker(level_editor_state* s, int marker_index) {
    if (!s || marker_index < 0 || marker_index >= s->marker_count) {
        return;
    }
    const float len = fmaxf(s->level_length_screens, 1.0f);
    if (len <= 1.0f) {
        s->timeline_01 = 0.0f;
        return;
    }

    const float selected_screen = s->markers[marker_index].x01 * len;
    const float span = len - 1.0f;
    float start_screen = clampf(s->timeline_01, 0.0f, 1.0f) * span;

    const float left_margin = 0.10f;
    const float right_margin = 0.90f;
    const float min_visible = start_screen + left_margin;
    const float max_visible = start_screen + right_margin;

    if (selected_screen < min_visible) {
        start_screen = selected_screen - left_margin;
    } else if (selected_screen > max_visible) {
        start_screen = selected_screen - right_margin;
    }

    start_screen = clampf(start_screen, 0.0f, span);
    s->timeline_01 = (span > 0.0f) ? (start_screen / span) : 0.0f;
}

static void move_marker_x(level_editor_state* s, int marker_index, float delta01) {
    if (!s || marker_index < 0 || marker_index >= s->marker_count || delta01 == 0.0f) {
        return;
    }
    level_editor_marker* m = &s->markers[marker_index];
    const float old_len = fmaxf(s->level_length_screens, 1.0f);
    float abs_x = m->x01 * old_len + delta01 * old_len;
    if (abs_x < 0.0f) {
        abs_x = 0.0f;
    }
    if (abs_x > old_len) {
        const float new_len = ceilf(abs_x + 0.25f);
        remap_level_length(s, new_len, marker_index, abs_x);
    } else {
        m->x01 = clampf(abs_x / old_len, 0.0f, 1.0f);
    }
    auto_pan_to_marker(s, marker_index);
}

static int level_style_from_name_loose(const char* name) {
    if (!name || !name[0]) {
        return -1;
    }
    if (strieq(name, "defender") || strieq(name, "level_defender") || strieq(name, "LEVEL_STYLE_DEFENDER")) {
        return LEVEL_STYLE_DEFENDER;
    }
    if (strieq(name, "enemy_radar") || strieq(name, "level_enemy_radar") || strieq(name, "cylinder")) {
        return LEVEL_STYLE_ENEMY_RADAR;
    }
    if (strieq(name, "event_horizon") || strieq(name, "level_event_horizon")) {
        return LEVEL_STYLE_EVENT_HORIZON;
    }
    if (strieq(name, "event_horizon_legacy") || strieq(name, "level_event_horizon_legacy")) {
        return LEVEL_STYLE_EVENT_HORIZON_LEGACY;
    }
    if (strieq(name, "high_plains_drifter") || strieq(name, "level_high_plains_drifter")) {
        return LEVEL_STYLE_HIGH_PLAINS_DRIFTER;
    }
    if (strieq(name, "high_plains_drifter_2") || strieq(name, "level_high_plains_drifter_2")) {
        return LEVEL_STYLE_HIGH_PLAINS_DRIFTER_2;
    }
    if (strieq(name, "fog_of_war") || strieq(name, "level_fog_of_war")) {
        return LEVEL_STYLE_FOG_OF_WAR;
    }
    if (strieq(name, "blank") || strieq(name, "level_blank")) {
        return LEVEL_STYLE_BLANK;
    }
    if (stristarts(name, "level_defender")) {
        return LEVEL_STYLE_DEFENDER;
    }
    if (stristarts(name, "level_enemy_radar")) {
        return LEVEL_STYLE_ENEMY_RADAR;
    }
    if (stristarts(name, "level_event_horizon_legacy")) {
        return LEVEL_STYLE_EVENT_HORIZON_LEGACY;
    }
    if (stristarts(name, "level_event_horizon")) {
        return LEVEL_STYLE_EVENT_HORIZON;
    }
    if (stristarts(name, "level_high_plains_drifter_2")) {
        return LEVEL_STYLE_HIGH_PLAINS_DRIFTER_2;
    }
    if (stristarts(name, "level_high_plains_drifter")) {
        return LEVEL_STYLE_HIGH_PLAINS_DRIFTER;
    }
    if (stristarts(name, "level_fog_of_war")) {
        return LEVEL_STYLE_FOG_OF_WAR;
    }
    if (stristarts(name, "level_blank")) {
        return LEVEL_STYLE_BLANK;
    }
    return -1;
}

static const char* level_style_name(int style) {
    switch (style) {
        case LEVEL_STYLE_DEFENDER: return "level_defender";
        case LEVEL_STYLE_ENEMY_RADAR: return "level_enemy_radar";
        case LEVEL_STYLE_EVENT_HORIZON: return "level_event_horizon";
        case LEVEL_STYLE_EVENT_HORIZON_LEGACY: return "level_event_horizon_legacy";
        case LEVEL_STYLE_HIGH_PLAINS_DRIFTER: return "level_high_plains_drifter";
        case LEVEL_STYLE_HIGH_PLAINS_DRIFTER_2: return "level_high_plains_drifter_2";
        case LEVEL_STYLE_FOG_OF_WAR: return "level_fog_of_war";
        case LEVEL_STYLE_BLANK: return "level_blank";
        default: return "level_unknown";
    }
}

static int level_style_count(void) {
    return LEVEL_STYLE_COUNT;
}

static void normalize_level_name_for_file(const char* in, char* out, size_t out_cap) {
    if (!out || out_cap == 0) {
        return;
    }
    out[0] = '\0';
    if (!in || !in[0]) {
        snprintf(out, out_cap, "level_untitled");
        return;
    }
    if (stristarts(in, "level_")) {
        snprintf(out, out_cap, "%s", in);
    } else {
        snprintf(out, out_cap, "level_%s", in);
    }
}

static void clear_markers(level_editor_state* s) {
    if (!s) {
        return;
    }
    s->marker_count = 0;
    s->selected_marker = -1;
}

static void push_marker(level_editor_state* s, int kind, int track, int order, float delay_s, float x01, float y01, float a, float b, float c, float d) {
    if (!s || s->marker_count >= LEVEL_EDITOR_MAX_MARKERS) {
        return;
    }
    level_editor_marker* m = &s->markers[s->marker_count++];
    m->kind = kind;
    m->track = track;
    m->order = (order > 0) ? order : 1;
    m->delay_s = fmaxf(delay_s, 0.0f);
    m->x01 = clampf(x01, 0.0f, 1.0f);
    m->y01 = clampf(y01, 0.0f, 1.0f);
    m->a = a;
    m->b = b;
    m->c = c;
    m->d = d;
    m->e = 0.0f;
    m->f = 0.0f;
    m->g = 0.0f;
}

void level_editor_compute_layout(float w, float h, level_editor_layout* out) {
    if (!out) {
        return;
    }
    const float ui = fmaxf(0.70f, fminf(w / 1920.0f, h / 1080.0f));
    const float m = 22.0f * ui;
    const float gap = 16.0f * ui;
    const float right_total_w = w * 0.30f;
    const float left_w = w - right_total_w - m * 2.0f - gap;
    const float timeline_h = h * 0.18f;
    const float toolbar_h = 44.0f * ui;
    const float top_h = h - m * 2.0f - timeline_h - gap - toolbar_h - gap;
    const float side_gap = 10.0f * ui;
    const float props_w = right_total_w * 0.72f;
    const float entities_w = right_total_w - props_w - side_gap;

    out->viewport = (vg_rect){m, m + timeline_h + gap, left_w, top_h};
    out->construction_toolbar = (vg_rect){out->viewport.x, out->viewport.y + out->viewport.h + gap, out->viewport.w, toolbar_h};
    out->timeline = (vg_rect){m, m, left_w, timeline_h};
    out->timeline_track = (vg_rect){
        out->timeline.x + 14.0f * ui,
        out->timeline.y + out->timeline.h * 0.36f + 8.0f * ui,
        out->timeline.w - 28.0f * ui,
        out->timeline.h * 0.40f
    };
    {
        out->timeline_enemy_track = (vg_rect){
            out->timeline_track.x,
            out->timeline_track.y - out->timeline_track.h + 3.0f * ui,
            out->timeline_track.w,
            out->timeline_track.h * 0.60f
        };
    }
    out->properties = (vg_rect){m + left_w + gap, m + timeline_h + gap, props_w, top_h};
    out->entities = (vg_rect){out->properties.x + out->properties.w + side_gap, out->properties.y, entities_w, top_h};

    const float row_h = 42.0f * ui;
    const float nav_w = row_h * 0.92f;
    const float name_gap = 8.0f * ui;
    const float controls_w = right_total_w;
    const float controls_x = out->properties.x;
    out->name_box = (vg_rect){
        controls_x + nav_w + name_gap,
        m + timeline_h - row_h,
        controls_w - (nav_w * 2.0f + name_gap * 2.0f),
        row_h
    };
    out->prev_button = (vg_rect){
        controls_x,
        m + timeline_h - row_h,
        nav_w,
        row_h
    };
    out->next_button = (vg_rect){
        out->name_box.x + out->name_box.w + name_gap,
        out->name_box.y,
        nav_w,
        row_h
    };
    out->load_button = (vg_rect){
        controls_x,
        m,
        controls_w * 0.48f,
        row_h
    };
    out->delete_button = (vg_rect){
        controls_x,
        m + row_h + 8.0f * ui,
        controls_w * 0.48f,
        row_h
    };
    out->new_button = (vg_rect){
        controls_x,
        m + (row_h + 8.0f * ui) * 2.0f,
        controls_w * 0.48f,
        row_h
    };
    out->save_new_button = (vg_rect){
        controls_x + controls_w * 0.52f,
        m + row_h + 8.0f * ui,
        controls_w * 0.48f,
        row_h
    };
    out->save_button = (vg_rect){
        controls_x + controls_w * 0.52f,
        m,
        controls_w * 0.48f,
        row_h
    };
    out->swarm_button = (vg_rect){
        out->entities.x + 8.0f * ui,
        out->entities.y + out->entities.h - 54.0f * ui,
        out->entities.w - 16.0f * ui,
        42.0f * ui
    };
    out->watcher_button = (vg_rect){
        out->entities.x + 8.0f * ui,
        out->entities.y + out->entities.h - 106.0f * ui,
        out->entities.w - 16.0f * ui,
        42.0f * ui
    };
    out->asteroid_button = (vg_rect){
        out->entities.x + 8.0f * ui,
        out->entities.y + out->entities.h - 158.0f * ui,
        out->entities.w - 16.0f * ui,
        42.0f * ui
    };
    out->mine_button = (vg_rect){
        out->entities.x + 8.0f * ui,
        out->entities.y + out->entities.h - 210.0f * ui,
        out->entities.w - 16.0f * ui,
        42.0f * ui
    };
    out->missile_button = (vg_rect){
        out->entities.x + 8.0f * ui,
        out->entities.y + out->entities.h - 262.0f * ui,
        out->entities.w - 16.0f * ui,
        42.0f * ui
    };
    out->arc_button = (vg_rect){
        out->entities.x + 8.0f * ui,
        out->entities.y + out->entities.h - 314.0f * ui,
        out->entities.w - 16.0f * ui,
        42.0f * ui
    };
    {
        const float tb_x = out->construction_toolbar.x + 8.0f * ui;
        const float tb_y = out->construction_toolbar.y + 3.0f * ui;
        const float tb_w = out->construction_toolbar.w - 16.0f * ui;
        const float btn_gap = 8.0f * ui;
        const float btn_w = (tb_w - btn_gap * 9.0f) / 10.0f;
        const float btn_h = out->construction_toolbar.h - 6.0f * ui;
        out->construction_button_0 = (vg_rect){tb_x + (btn_w + btn_gap) * 0.0f, tb_y, btn_w, btn_h};
        out->construction_button_1 = (vg_rect){tb_x + (btn_w + btn_gap) * 1.0f, tb_y, btn_w, btn_h};
        out->construction_button_2 = (vg_rect){tb_x + (btn_w + btn_gap) * 2.0f, tb_y, btn_w, btn_h};
        out->construction_button_3 = (vg_rect){tb_x + (btn_w + btn_gap) * 3.0f, tb_y, btn_w, btn_h};
        out->construction_button_4 = (vg_rect){tb_x + (btn_w + btn_gap) * 4.0f, tb_y, btn_w, btn_h};
        out->construction_button_5 = (vg_rect){tb_x + (btn_w + btn_gap) * 5.0f, tb_y, btn_w, btn_h};
        out->construction_button_6 = (vg_rect){tb_x + (btn_w + btn_gap) * 6.0f, tb_y, btn_w, btn_h};
        out->construction_button_7 = (vg_rect){tb_x + (btn_w + btn_gap) * 7.0f, tb_y, btn_w, btn_h};
    }
    const float len_screens = 1.0f;
    const float window_w = out->timeline_track.w / len_screens;
    out->timeline_window = (vg_rect){
        out->timeline_track.x,
        out->timeline_track.y,
        window_w,
        out->timeline_track.h
    };
}

static void sync_timeline_window(level_editor_state* s, level_editor_layout* l) {
    const float level_screens = fmaxf(s ? s->level_length_screens : 1.0f, 1.0f);
    const float span_screens = fmaxf(level_screens - 1.0f, 0.0f);
    const float t = s ? clampf(s->timeline_01, 0.0f, 1.0f) : 0.0f;
    const float w = l->timeline_track.w / level_screens;
    const float x = l->timeline_track.x + t * span_screens * w;
    l->timeline_window.x = x;
    l->timeline_window.y = l->timeline_track.y;
    l->timeline_window.w = w;
    l->timeline_window.h = l->timeline_track.h;
}

static void build_markers(level_editor_state* s, const leveldef_db* db, int style, const leveldef_level* lvl_override) {
    clear_markers(s);
    if (!s || !db || style < 0 || style >= LEVEL_STYLE_COUNT) {
        return;
    }

    const leveldef_level* lvl = lvl_override ? lvl_override : leveldef_get_level(db, style);
    if (!lvl) {
        return;
    }

    if (lvl->exit_enabled) {
        push_marker(
            s,
            LEVEL_EDITOR_MARKER_EXIT,
            LEVEL_EDITOR_TRACK_SPATIAL,
            1,
            0.0f,
            lvl->exit_x01 / fmaxf(s->level_length_screens, 1.0f),
            lvl->exit_y01,
            0.0f,
            0.0f,
            0.0f,
            0.0f
        );
    }
    if (lvl->asteroid_storm_enabled) {
        push_marker(
            s,
            LEVEL_EDITOR_MARKER_ASTEROID_STORM,
            LEVEL_EDITOR_TRACK_SPATIAL,
            1,
            0.0f,
            lvl->asteroid_storm_start_x01 / fmaxf(s->level_length_screens, 1.0f),
            0.50f,
            lvl->asteroid_storm_duration_s,
            lvl->asteroid_storm_angle_deg,
            lvl->asteroid_storm_speed,
            lvl->asteroid_storm_density
        );
    }

    for (int i = 0; i < lvl->searchlight_count; ++i) {
        const leveldef_searchlight* sl = &lvl->searchlights[i];
        const int before = s->marker_count;
        push_marker(
            s,
            LEVEL_EDITOR_MARKER_SEARCHLIGHT,
            LEVEL_EDITOR_TRACK_SPATIAL,
            1,
            0.0f,
            sl->anchor_x01 / fmaxf(s->level_length_screens, 1.0f),
            sl->anchor_y01,
            sl->length_h01,
            sl->half_angle_deg,
            sl->sweep_speed,
            sl->sweep_amplitude_deg * 2.0f
        );
        if (s->marker_count > before) {
            level_editor_marker* m = &s->markers[s->marker_count - 1];
            m->e = (float)sl->source_type;
            m->f = sl->source_radius;
            m->g = (float)sl->sweep_motion;
        }
    }
    for (int i = 0; i < lvl->minefield_count; ++i) {
        const leveldef_minefield* mf = &lvl->minefields[i];
        push_marker(
            s,
            LEVEL_EDITOR_MARKER_MINEFIELD,
            LEVEL_EDITOR_TRACK_SPATIAL,
            1,
            0.0f,
            mf->anchor_x01 / fmaxf(s->level_length_screens, 1.0f),
            mf->anchor_y01,
            (float)mf->count,
            0.0f,
            0.0f,
            0.0f
        );
    }
    for (int i = 0; i < lvl->missile_launcher_count; ++i) {
        const leveldef_missile_launcher* ml = &lvl->missile_launchers[i];
        push_marker(
            s,
            LEVEL_EDITOR_MARKER_MISSILE,
            LEVEL_EDITOR_TRACK_SPATIAL,
            1,
            0.0f,
            ml->anchor_x01 / fmaxf(s->level_length_screens, 1.0f),
            ml->anchor_y01,
            (float)ml->count,
            ml->spacing,
            ml->activation_range,
            ml->missile_ttl_s
        );
    }
    for (int i = 0; i < lvl->arc_node_count; ++i) {
        const leveldef_arc_node* an = &lvl->arc_nodes[i];
        const int before = s->marker_count;
        push_marker(
            s,
            LEVEL_EDITOR_MARKER_ARC_NODE,
            LEVEL_EDITOR_TRACK_SPATIAL,
            1,
            0.0f,
            an->anchor_x01 / fmaxf(s->level_length_screens, 1.0f),
            an->anchor_y01,
            an->period_s,
            an->on_s,
            an->radius,
            an->push_accel
        );
        if (s->marker_count > before) {
            s->markers[s->marker_count - 1].e = an->damage_interval_s;
            fprintf(
                stderr,
                "[arc_trace] level->editor arc idx=%d anchor_x01=%.6f anchor_y01=%.6f level_len=%.6f marker_x01=%.6f marker_y01=%.6f\n",
                i,
                an->anchor_x01,
                an->anchor_y01,
                fmaxf(s->level_length_screens, 1.0f),
                s->markers[s->marker_count - 1].x01,
                s->markers[s->marker_count - 1].y01
            );
        }
    }
    for (int i = 0; i < lvl->structure_count; ++i) {
        const leveldef_structure_instance* st = &lvl->structures[i];
        const int before = s->marker_count;
        push_marker(
            s,
            LEVEL_EDITOR_MARKER_STRUCTURE,
            LEVEL_EDITOR_TRACK_SPATIAL,
            1,
            0.0f,
            (float)st->grid_x / (float)structure_grid_x_steps_for_level(s->level_length_screens),
            (float)st->grid_y / (float)structure_grid_y_steps(),
            (float)st->prefab_id,
            (float)st->layer,
            (float)(st->rotation_quadrants & 3),
            (float)((st->flip_x ? 1 : 0) | ((st->flip_y ? 1 : 0) << 1))
        );
        if (s->marker_count > before) {
            level_editor_marker* m = &s->markers[s->marker_count - 1];
            m->e = (st->vent_density > 0.0f) ? st->vent_density : 1.0f;
            m->f = (st->vent_opacity > 0.0f) ? st->vent_opacity : 1.0f;
            m->g = (st->vent_plume_height > 0.0f) ? st->vent_plume_height : 1.0f;
        }
    }

    const int cycle_n = lvl->wave_cycle_count;
    const float slots = (float)((cycle_n > 0) ? cycle_n : 1);

    if (lvl->wave_mode == LEVELDEF_WAVES_CURATED) {
        for (int i = 0; i < lvl->curated_count; ++i) {
            const leveldef_curated_enemy* ce = &lvl->curated[i];
            if (!is_wave_kind(ce->kind)) {
                continue;
            }
            push_marker(
                s,
                ce->kind,
                LEVEL_EDITOR_TRACK_SPATIAL,
                1,
                0.0f,
                ce->x01 / fmaxf(s->level_length_screens, 1.0f),
                ce->y01,
                ce->a,
                (is_boid_wave_kind(ce->kind) && ce->b <= 0.0f) ? boid_speed_default_for_kind(ce->kind) : ce->b,
                (is_boid_wave_kind(ce->kind) && ce->c <= 0.0f) ? boid_accel_default_for_kind(ce->kind) : ce->c,
                0.0f
            );
        }
    } else if (lvl->event_count > 0) {
        float cursor = 0.0f;
        for (int i = 0; i < lvl->event_count; ++i) {
            const leveldef_event_entry* ev = &lvl->events[i];
            cursor += fmaxf(ev->delay_s, 0.0f);
            {
                const float x01 = clampf(cursor / fmaxf(s->level_length_screens, 1.0f), 0.0f, 1.0f);
                if (ev->kind == LEVELDEF_EVENT_ASTEROID_STORM) {
                    push_marker(s, LEVEL_EDITOR_MARKER_ASTEROID_STORM, LEVEL_EDITOR_TRACK_EVENT, ev->order, ev->delay_s, x01, 0.5f, lvl->asteroid_storm_duration_s, lvl->asteroid_storm_angle_deg, lvl->asteroid_storm_speed, lvl->asteroid_storm_density);
                } else if (ev->kind == LEVELDEF_EVENT_WAVE_V) {
                    push_marker(s, LEVEL_EDITOR_MARKER_WAVE_V, LEVEL_EDITOR_TRACK_EVENT, ev->order, ev->delay_s, x01, lvl->v.home_y01, lvl->v.count, lvl->v.form_amp, lvl->v.max_speed, 0.0f);
                } else if (ev->kind == LEVELDEF_EVENT_WAVE_KAMIKAZE) {
                    push_marker(s, LEVEL_EDITOR_MARKER_WAVE_KAMIKAZE, LEVEL_EDITOR_TRACK_EVENT, ev->order, ev->delay_s, x01, 0.5f, lvl->kamikaze.count, lvl->kamikaze.max_speed, lvl->kamikaze.accel, 0.0f);
                } else if (ev->kind == LEVELDEF_EVENT_WAVE_SWARM) {
                    const leveldef_boid_profile* p = leveldef_get_boid_profile(db, lvl->default_boid_profile);
                    push_marker(s, LEVEL_EDITOR_MARKER_BOID, LEVEL_EDITOR_TRACK_EVENT, ev->order, ev->delay_s, x01, p ? p->spawn_y01 : 0.5f, p ? p->count : 12.0f, p ? p->max_speed : boid_speed_default_for_kind(LEVEL_EDITOR_MARKER_BOID), p ? p->accel : boid_accel_default_for_kind(LEVEL_EDITOR_MARKER_BOID), p ? p->max_turn_rate_deg : boid_turn_rate_default_deg_for_kind(LEVEL_EDITOR_MARKER_BOID));
                } else if (ev->kind == LEVELDEF_EVENT_WAVE_SWARM_FISH) {
                    const int pid = leveldef_find_boid_profile(db, "FISH");
                    const leveldef_boid_profile* p = leveldef_get_boid_profile(db, (pid >= 0) ? pid : lvl->default_boid_profile);
                    push_marker(s, LEVEL_EDITOR_MARKER_BOID_FISH, LEVEL_EDITOR_TRACK_EVENT, ev->order, ev->delay_s, x01, p ? p->spawn_y01 : 0.5f, p ? p->count : 12.0f, p ? p->max_speed : boid_speed_default_for_kind(LEVEL_EDITOR_MARKER_BOID_FISH), p ? p->accel : boid_accel_default_for_kind(LEVEL_EDITOR_MARKER_BOID_FISH), p ? p->max_turn_rate_deg : boid_turn_rate_default_deg_for_kind(LEVEL_EDITOR_MARKER_BOID_FISH));
                } else if (ev->kind == LEVELDEF_EVENT_WAVE_SWARM_FIREFLY) {
                    const int pid = leveldef_find_boid_profile(db, "FIREFLY");
                    const leveldef_boid_profile* p = leveldef_get_boid_profile(db, (pid >= 0) ? pid : lvl->default_boid_profile);
                    push_marker(s, LEVEL_EDITOR_MARKER_BOID_FIREFLY, LEVEL_EDITOR_TRACK_EVENT, ev->order, ev->delay_s, x01, p ? p->spawn_y01 : 0.5f, p ? p->count : 12.0f, p ? p->max_speed : boid_speed_default_for_kind(LEVEL_EDITOR_MARKER_BOID_FIREFLY), p ? p->accel : boid_accel_default_for_kind(LEVEL_EDITOR_MARKER_BOID_FIREFLY), p ? p->max_turn_rate_deg : boid_turn_rate_default_deg_for_kind(LEVEL_EDITOR_MARKER_BOID_FIREFLY));
                } else if (ev->kind == LEVELDEF_EVENT_WAVE_SWARM_BIRD) {
                    const int pid = leveldef_find_boid_profile(db, "BIRD");
                    const leveldef_boid_profile* p = leveldef_get_boid_profile(db, (pid >= 0) ? pid : lvl->default_boid_profile);
                    push_marker(s, LEVEL_EDITOR_MARKER_BOID_BIRD, LEVEL_EDITOR_TRACK_EVENT, ev->order, ev->delay_s, x01, p ? p->spawn_y01 : 0.5f, p ? p->count : 12.0f, p ? p->max_speed : boid_speed_default_for_kind(LEVEL_EDITOR_MARKER_BOID_BIRD), p ? p->accel : boid_accel_default_for_kind(LEVEL_EDITOR_MARKER_BOID_BIRD), p ? p->max_turn_rate_deg : boid_turn_rate_default_deg_for_kind(LEVEL_EDITOR_MARKER_BOID_BIRD));
                } else {
                    push_marker(s, LEVEL_EDITOR_MARKER_WAVE_SINE, LEVEL_EDITOR_TRACK_EVENT, ev->order, ev->delay_s, x01, lvl->sine.home_y01, lvl->sine.count, lvl->sine.form_amp, lvl->sine.max_speed, 0.0f);
                }
            }
        }
    } else {
        for (int i = 0; i < lvl->wave_cycle_count; ++i) {
            const int pattern = lvl->wave_cycle[i];
            const float wave_base = ((float)i / slots) * (s->level_length_screens - 1.0f);
            if (pattern == LEVELDEF_WAVE_SINE_SNAKE) {
                const float x01 = (wave_base + lvl->sine.start_x01) / s->level_length_screens;
                push_marker(s, LEVEL_EDITOR_MARKER_WAVE_SINE, LEVEL_EDITOR_TRACK_EVENT, i + 1, 0.0f, x01, lvl->sine.home_y01, lvl->sine.count, lvl->sine.form_amp, lvl->sine.max_speed, 0.0f);
            } else if (pattern == LEVELDEF_WAVE_V_FORMATION) {
                const float x01 = (wave_base + lvl->v.start_x01) / s->level_length_screens;
                push_marker(s, LEVEL_EDITOR_MARKER_WAVE_V, LEVEL_EDITOR_TRACK_EVENT, i + 1, 0.0f, x01, lvl->v.home_y01, lvl->v.count, lvl->v.form_amp, lvl->v.max_speed, 0.0f);
            } else if (pattern == LEVELDEF_WAVE_KAMIKAZE) {
                const float x01 = (wave_base + lvl->kamikaze.start_x01) / s->level_length_screens;
                push_marker(s, LEVEL_EDITOR_MARKER_WAVE_KAMIKAZE, LEVEL_EDITOR_TRACK_EVENT, i + 1, 0.0f, x01, 0.50f, lvl->kamikaze.count, lvl->kamikaze.max_speed, lvl->kamikaze.accel, 0.0f);
            } else if (pattern == LEVELDEF_WAVE_ASTEROID_STORM) {
                const float x01 = (float)(i + 1) / fmaxf((float)(lvl->wave_cycle_count + 1), 1.0f);
                push_marker(s, LEVEL_EDITOR_MARKER_ASTEROID_STORM, LEVEL_EDITOR_TRACK_EVENT, i + 1, 0.0f, x01, 0.50f, lvl->asteroid_storm_duration_s, lvl->asteroid_storm_angle_deg, lvl->asteroid_storm_speed, lvl->asteroid_storm_density);
            } else if (pattern == LEVELDEF_WAVE_SWARM_FISH || pattern == LEVELDEF_WAVE_SWARM_FIREFLY || pattern == LEVELDEF_WAVE_SWARM_BIRD || pattern == LEVELDEF_WAVE_SWARM) {
                int pid = lvl->default_boid_profile;
                int mk = LEVEL_EDITOR_MARKER_BOID;
                if (pattern == LEVELDEF_WAVE_SWARM_FISH) {
                    mk = LEVEL_EDITOR_MARKER_BOID_FISH;
                    pid = leveldef_find_boid_profile(db, "FISH");
                } else if (pattern == LEVELDEF_WAVE_SWARM_FIREFLY) {
                    mk = LEVEL_EDITOR_MARKER_BOID_FIREFLY;
                    pid = leveldef_find_boid_profile(db, "FIREFLY");
                } else if (pattern == LEVELDEF_WAVE_SWARM_BIRD) {
                    mk = LEVEL_EDITOR_MARKER_BOID_BIRD;
                    pid = leveldef_find_boid_profile(db, "BIRD");
                }
                const leveldef_boid_profile* p = leveldef_get_boid_profile(db, (pid >= 0) ? pid : lvl->default_boid_profile);
                const float x01 = (wave_base + (p ? p->spawn_x01 : 0.6f)) / s->level_length_screens;
                push_marker(s, mk, LEVEL_EDITOR_TRACK_EVENT, i + 1, 0.0f, x01, p ? p->spawn_y01 : 0.5f, p ? p->count : 12.0f, p ? p->max_speed : boid_speed_default_for_kind(mk), p ? p->accel : boid_accel_default_for_kind(mk), p ? p->max_turn_rate_deg : boid_turn_rate_default_deg_for_kind(mk));
            }
        }
    }
    s->selected_marker = (s->marker_count > 0) ? 0 : -1;
    s->selected_property = 0;
}

void level_editor_init(level_editor_state* s) {
    if (!s) {
        return;
    }
    memset(s, 0, sizeof(*s));
    s->level_style = LEVEL_STYLE_BLANK;
    s->level_render_style = LEVEL_RENDER_BLANK;
    s->level_wave_mode = LEVELDEF_WAVES_NORMAL;
    s->level_theme_palette = 0;
    s->level_asteroid_storm_enabled = 0;
    s->level_asteroid_storm_angle_deg = 180.0f;
    s->level_asteroid_storm_speed = 190.0f;
    s->level_asteroid_storm_duration_s = 12.0f;
    s->level_asteroid_storm_density = 1.0f;
    s->level_kamikaze_radius_min = 11.0f;
    s->level_kamikaze_radius_max = 17.0f;
    snprintf(s->level_name, sizeof(s->level_name), "%s", level_style_name(s->level_style));
    snprintf(s->status_text, sizeof(s->status_text), "ready");
    s->entry_active = 0;
    s->timeline_01 = 0.0f;
    s->level_length_screens = 12.0f;
    s->timeline_drag = 0;
    s->selected_marker = -1;
    s->selected_property = 0;
    s->entity_tool_selected = 0;
    s->entity_drag_active = 0;
    s->entity_drag_kind = 0;
    s->entity_drag_x = 0.0f;
    s->entity_drag_y = 0.0f;
    s->marker_drag_active = 0;
    s->marker_drag_index = -1;
    s->dirty = 0;
    s->edit_revision = 1u;
    s->source_path[0] = '\0';
    s->source_text[0] = '\0';
    s->loaded_level_valid = 0;
    memset(&s->loaded_level, 0, sizeof(s->loaded_level));
    s->snapshot_valid = 0;
}

int level_editor_load_by_name(level_editor_state* s, const leveldef_db* db, const char* name) {
    leveldef_level loaded_level;
    const leveldef_level* lvl = NULL;
    int style = -1;
    char requested_name[LEVEL_EDITOR_NAME_CAP];
    if (!s || !db) {
        return 0;
    }
    s->entry_active = 0;
    requested_name[0] = '\0';
    if (name && name[0] != '\0') {
        snprintf(requested_name, sizeof(requested_name), "%s", name);
    }
    style = level_style_from_name_loose(requested_name[0] ? requested_name : s->level_name);
    if (style < 0 || style >= LEVEL_STYLE_COUNT) {
        snprintf(s->status_text, sizeof(s->status_text), "unknown level name");
        return 0;
    }
    memset(&loaded_level, 0, sizeof(loaded_level));
    if (requested_name[0]) {
        snprintf(s->level_name, sizeof(s->level_name), "%s", requested_name);
    } else {
        snprintf(s->level_name, sizeof(s->level_name), "%s", level_style_name(style));
    }
    s->source_path[0] = '\0';
    s->source_text[0] = '\0';
    if (resolve_level_file_path(s->level_name, s->source_path, sizeof(s->source_path)) &&
        leveldef_load_level_file_with_base(db, s->source_path, &loaded_level, &style, NULL)) {
        lvl = &loaded_level;
        (void)read_file_text(s->source_path, s->source_text, sizeof(s->source_text));
    } else {
        lvl = leveldef_get_level(db, style);
    }
    s->level_style = style;
    s->timeline_01 = 0.0f;

    {
        const int cycle_n = lvl ? ((lvl->wave_mode == LEVELDEF_WAVES_CURATED) ? lvl->curated_count :
                                   lvl->wave_cycle_count) : 0;
        const float cycle_len = fmaxf(8.0f, 6.0f + (float)cycle_n * 1.2f);
        float data_len = lvl ? fmaxf(1.0f, lvl->exit_x01 + 0.75f) : cycle_len;
        if (lvl) {
            data_len = fmaxf(data_len, lvl->asteroid_storm_start_x01 + 1.0f);
            {
                const float steps_per_screen = (float)structure_grid_x_steps_for_level(1.0f);
                float struct_len = 1.0f;
                for (int i = 0; i < lvl->structure_count; ++i) {
                    const leveldef_structure_instance* st = &lvl->structures[i];
                    int w_units = 1;
                    int h_units = 1;
                    int q = 0;
                    float right_screens = 0.0f;
                    structure_prefab_dims(st->prefab_id, &w_units, &h_units);
                    q = ((st->rotation_quadrants % 4) + 4) % 4;
                    if ((q & 1) != 0) {
                        const int tmp = w_units;
                        w_units = h_units;
                        h_units = tmp;
                    }
                    right_screens = ((float)st->grid_x + (float)w_units) / fmaxf(steps_per_screen, 1.0f);
                    struct_len = fmaxf(struct_len, right_screens + 0.25f);
                }
                data_len = fmaxf(data_len, struct_len);
            }
            for (int i = 0; i < lvl->searchlight_count; ++i) {
                data_len = fmaxf(data_len, lvl->searchlights[i].anchor_x01 + 0.5f);
            }
            for (int i = 0; i < lvl->minefield_count; ++i) {
                data_len = fmaxf(data_len, lvl->minefields[i].anchor_x01 + 0.5f);
            }
            for (int i = 0; i < lvl->missile_launcher_count; ++i) {
                data_len = fmaxf(data_len, lvl->missile_launchers[i].anchor_x01 + 0.5f);
            }
            for (int i = 0; i < lvl->arc_node_count; ++i) {
                data_len = fmaxf(data_len, lvl->arc_nodes[i].anchor_x01 + 0.5f);
            }
            for (int i = 0; i < lvl->curated_count; ++i) {
                data_len = fmaxf(data_len, lvl->curated[i].x01 + 0.5f);
            }
            if (lvl->event_count > 0) {
                float sum = 0.0f;
                for (int i = 0; i < lvl->event_count; ++i) {
                    sum += fmaxf(lvl->events[i].delay_s, 0.0f);
                }
                data_len = fmaxf(data_len, sum + 1.0f);
            }
        }
        if (lvl && lvl->editor_length_screens > 0.0f) {
            s->level_length_screens = lvl->editor_length_screens;
        } else {
            s->level_length_screens = fmaxf(cycle_len, data_len);
        }
        if (lvl) {
            s->level_render_style = lvl->render_style;
            s->level_wave_mode = lvl->wave_mode;
            s->level_theme_palette = clampi(lvl->theme_palette, 0, 2);
            s->level_asteroid_storm_enabled = lvl->asteroid_storm_enabled ? 1 : 0;
            s->level_asteroid_storm_angle_deg = lvl->asteroid_storm_angle_deg;
            s->level_asteroid_storm_speed = lvl->asteroid_storm_speed;
            s->level_asteroid_storm_duration_s = lvl->asteroid_storm_duration_s;
            s->level_asteroid_storm_density = lvl->asteroid_storm_density;
            s->level_kamikaze_radius_min = lvl->kamikaze.radius_min;
            s->level_kamikaze_radius_max = lvl->kamikaze.radius_max;
        }
    }
    build_markers(s, db, style, lvl);
    s->dirty = 0;
    s->edit_revision += 1u;
    s->loaded_level_valid = (lvl != NULL) ? 1 : 0;
    if (lvl) {
        s->loaded_level = *lvl;
    }
    level_editor_save_snapshot(s);
    snprintf(s->status_text, sizeof(s->status_text), "loaded %s (%d objects)", s->level_name, s->marker_count);
    return 1;
}

void level_editor_append_text(level_editor_state* s, const char* utf8) {
    if (!s || !utf8 || !utf8[0]) {
        return;
    }
    size_t n = strlen(s->level_name);
    for (const unsigned char* p = (const unsigned char*)utf8; *p; ++p) {
        if (n + 1 >= sizeof(s->level_name)) {
            break;
        }
        if (*p >= 32 && *p <= 126) {
            s->level_name[n++] = (char)*p;
        }
    }
    s->level_name[n] = '\0';
}

void level_editor_backspace(level_editor_state* s) {
    if (!s) {
        return;
    }
    size_t n = strlen(s->level_name);
    if (n > 0) {
        s->level_name[n - 1] = '\0';
    }
}

static int point_in_rect(float x, float y, vg_rect r) {
    return (x >= r.x && x <= (r.x + r.w) && y >= r.y && y <= (r.y + r.h));
}

static void add_marker_at_view(
    level_editor_state* s,
    int kind,
    float view_x01,
    float view_y01
) {
    if (!s || s->marker_count >= LEVEL_EDITOR_MAX_MARKERS) {
        return;
    }
    const float level_screens = fmaxf(s->level_length_screens, 1.0f);
    const float start_screen = s->timeline_01 * fmaxf(level_screens - 1.0f, 0.0f);
    const float view_min = start_screen / level_screens;
    const float view_max = (start_screen + 1.0f) / level_screens;
    const float x01 = view_min + clampf(view_x01, 0.0f, 1.0f) * fmaxf(view_max - view_min, 1.0e-6f);
    const float y01 = clampf(view_y01, 0.0f, 1.0f);
    if (kind == LEVEL_EDITOR_MARKER_SEARCHLIGHT) {
        const int before = s->marker_count;
        push_marker(s, LEVEL_EDITOR_MARKER_SEARCHLIGHT, LEVEL_EDITOR_TRACK_SPATIAL, 1, 0.0f, x01, y01, 0.36f, 12.0f, 1.2f, 45.0f);
        if (s->marker_count > before) {
            level_editor_marker* m = &s->markers[s->marker_count - 1];
            m->e = (float)SEARCHLIGHT_SOURCE_DOME;
            m->f = 14.0f;
            m->g = (float)SEARCHLIGHT_MOTION_PENDULUM;
        }
    } else if (is_boid_wave_kind(kind)) {
        if (s->level_wave_mode != LEVELDEF_WAVES_CURATED) {
            s->level_wave_mode = LEVELDEF_WAVES_CURATED;
        }
        push_marker(
            s,
            kind,
            LEVEL_EDITOR_TRACK_SPATIAL,
            1,
            0.0f,
            x01,
            y01,
            12.0f,
            boid_speed_default_for_kind(kind),
            boid_accel_default_for_kind(kind),
            boid_turn_rate_default_deg_for_kind(kind)
        );
    } else if (kind == LEVEL_EDITOR_MARKER_ASTEROID_STORM) {
        push_marker(s, LEVEL_EDITOR_MARKER_ASTEROID_STORM, LEVEL_EDITOR_TRACK_SPATIAL, 1, 0.0f, x01, y01, 10.0f, 30.0f, 520.0f, 1.3f);
    } else if (kind == LEVEL_EDITOR_MARKER_MINEFIELD) {
        push_marker(s, LEVEL_EDITOR_MARKER_MINEFIELD, LEVEL_EDITOR_TRACK_SPATIAL, 1, 0.0f, x01, y01, 12.0f, 0.0f, 0.0f, 0.0f);
    } else if (kind == LEVEL_EDITOR_MARKER_MISSILE) {
        push_marker(s, LEVEL_EDITOR_MARKER_MISSILE, LEVEL_EDITOR_TRACK_SPATIAL, 1, 0.0f, x01, y01, 6.0f, 64.0f, 760.0f, 3.6f);
    } else if (kind == LEVEL_EDITOR_MARKER_ARC_NODE) {
        const int before = s->marker_count;
        push_marker(s, LEVEL_EDITOR_MARKER_ARC_NODE, LEVEL_EDITOR_TRACK_SPATIAL, 1, 0.0f, x01, y01, 2.4f, 0.8f, 40.0f, 1800.0f);
        if (s->marker_count > before) {
            s->markers[s->marker_count - 1].e = 0.35f;
        }
    } else if (kind == LEVEL_EDITOR_MARKER_STRUCTURE) {
        const int before = s->marker_count;
        push_marker(
            s,
            LEVEL_EDITOR_MARKER_STRUCTURE,
            LEVEL_EDITOR_TRACK_SPATIAL,
            1,
            0.0f,
            snap_x01(x01),
            snap_y01(y01),
            0.0f,
            0.0f,
            0.0f,
            0.0f
        );
        if (s->marker_count > before) {
            level_editor_marker* m = &s->markers[s->marker_count - 1];
            m->e = 1.0f; /* vent density */
            m->f = 1.0f; /* vent opacity */
            m->g = 1.0f; /* vent plume height */
        }
    }
    s->selected_marker = s->marker_count - 1;
    s->selected_property = 0;
    s->entity_tool_selected = 0;
    mark_editor_dirty(s);
    snprintf(s->status_text, sizeof(s->status_text), "added %s", marker_kind_name(kind));
}

static void add_spatial_marker_at_x01(level_editor_state* s, int kind, float x01, float y01) {
    if (!s || s->marker_count >= LEVEL_EDITOR_MAX_MARKERS) {
        return;
    }
    const float xx = clampf(x01, 0.0f, 1.0f);
    const float yy = clampf(y01, 0.0f, 1.0f);
    if (kind == LEVEL_EDITOR_MARKER_SEARCHLIGHT) {
        const int before = s->marker_count;
        push_marker(s, LEVEL_EDITOR_MARKER_SEARCHLIGHT, LEVEL_EDITOR_TRACK_SPATIAL, 1, 0.0f, xx, yy, 0.36f, 12.0f, 1.2f, 45.0f);
        if (s->marker_count > before) {
            level_editor_marker* m = &s->markers[s->marker_count - 1];
            m->e = (float)SEARCHLIGHT_SOURCE_DOME;
            m->f = 14.0f;
            m->g = (float)SEARCHLIGHT_MOTION_PENDULUM;
        }
    } else if (is_boid_wave_kind(kind)) {
        if (s->level_wave_mode != LEVELDEF_WAVES_CURATED) {
            s->level_wave_mode = LEVELDEF_WAVES_CURATED;
        }
        push_marker(
            s,
            kind,
            LEVEL_EDITOR_TRACK_SPATIAL,
            1,
            0.0f,
            xx,
            yy,
            12.0f,
            boid_speed_default_for_kind(kind),
            boid_accel_default_for_kind(kind),
            boid_turn_rate_default_deg_for_kind(kind)
        );
    } else if (kind == LEVEL_EDITOR_MARKER_ASTEROID_STORM) {
        push_marker(s, LEVEL_EDITOR_MARKER_ASTEROID_STORM, LEVEL_EDITOR_TRACK_SPATIAL, 1, 0.0f, xx, yy, 10.0f, 30.0f, 520.0f, 1.3f);
    } else if (kind == LEVEL_EDITOR_MARKER_MINEFIELD) {
        push_marker(s, LEVEL_EDITOR_MARKER_MINEFIELD, LEVEL_EDITOR_TRACK_SPATIAL, 1, 0.0f, xx, yy, 12.0f, 0.0f, 0.0f, 0.0f);
    } else if (kind == LEVEL_EDITOR_MARKER_MISSILE) {
        push_marker(s, LEVEL_EDITOR_MARKER_MISSILE, LEVEL_EDITOR_TRACK_SPATIAL, 1, 0.0f, xx, yy, 6.0f, 64.0f, 760.0f, 3.6f);
    } else if (kind == LEVEL_EDITOR_MARKER_ARC_NODE) {
        const int before = s->marker_count;
        push_marker(s, LEVEL_EDITOR_MARKER_ARC_NODE, LEVEL_EDITOR_TRACK_SPATIAL, 1, 0.0f, xx, yy, 2.4f, 0.8f, 40.0f, 1800.0f);
        if (s->marker_count > before) {
            s->markers[s->marker_count - 1].e = 0.35f;
        }
    } else {
        return;
    }
    s->selected_marker = s->marker_count - 1;
    s->selected_property = 0;
    s->entity_tool_selected = 0;
    mark_editor_dirty(s);
    snprintf(s->status_text, sizeof(s->status_text), "added %s", marker_kind_name(kind));
}

static void add_marker_at_timeline(level_editor_state* s, int kind, float x01) {
    if (!s || s->marker_count >= LEVEL_EDITOR_MAX_MARKERS) {
        return;
    }
    const float cx = clampf(x01, 0.0f, 1.0f);
    {
        const int n = event_item_count(s);
        int ord = (int)floorf(cx * (float)(n + 1)) + 1;
        if (ord < 1) ord = 1;
        if (ord > n + 1) ord = n + 1;
        shift_event_orders(s, ord);
        if (is_boid_wave_kind(kind)) {
            push_marker(
                s,
                kind,
                LEVEL_EDITOR_TRACK_EVENT,
                ord,
                0.0f,
                cx,
                0.50f,
                12.0f,
                boid_speed_default_for_kind(kind),
                boid_accel_default_for_kind(kind),
                boid_turn_rate_default_deg_for_kind(kind)
            );
        } else if (kind == LEVEL_EDITOR_MARKER_WAVE_SINE) {
            push_marker(s, LEVEL_EDITOR_MARKER_WAVE_SINE, LEVEL_EDITOR_TRACK_EVENT, ord, 0.0f, cx, 0.50f, 10.0f, 92.0f, 285.0f, 0.0f);
        } else if (kind == LEVEL_EDITOR_MARKER_WAVE_V) {
            push_marker(s, LEVEL_EDITOR_MARKER_WAVE_V, LEVEL_EDITOR_TRACK_EVENT, ord, 0.0f, cx, 0.55f, 11.0f, 10.0f, 295.0f, 0.0f);
        } else if (kind == LEVEL_EDITOR_MARKER_WAVE_KAMIKAZE) {
            push_marker(s, LEVEL_EDITOR_MARKER_WAVE_KAMIKAZE, LEVEL_EDITOR_TRACK_EVENT, ord, 0.0f, cx, 0.50f, 9.0f, 360.0f, 9.0f, 0.0f);
        } else if (kind == LEVEL_EDITOR_MARKER_ASTEROID_STORM) {
            push_marker(s, LEVEL_EDITOR_MARKER_ASTEROID_STORM, LEVEL_EDITOR_TRACK_EVENT, ord, 5.0f, cx, 0.50f, 10.0f, 30.0f, 520.0f, 1.3f);
        } else {
            return;
        }
    }
    s->selected_marker = s->marker_count - 1;
    s->selected_property = 0;
    s->entity_tool_selected = 0;
    mark_editor_dirty(s);
    snprintf(s->status_text, sizeof(s->status_text), "added %s", marker_kind_name(kind));
}

static void add_structure_marker_at_view(level_editor_state* s, float mx01, float my01) {
    if (!s || s->marker_count >= LEVEL_EDITOR_MAX_MARKERS || s->structure_tool_selected <= 0) {
        return;
    }
    const float level_screens = fmaxf(s->level_length_screens, 1.0f);
    const float start_screen = s->timeline_01 * fmaxf(level_screens - 1.0f, 0.0f);
    const float view_min = start_screen / level_screens;
    const float view_max = (start_screen + 1.0f) / level_screens;
    const float x01 = view_min + clampf(mx01, 0.0f, 1.0f) * fmaxf(view_max - view_min, 1.0e-6f);
    const float y01 = clampf(my01, 0.0f, 1.0f);
    const int prefab_id = clampi(s->structure_tool_selected - 1, 0, 31);
    const int layer = (prefab_id >= 5) ? 1 : 0;
    const int gx_steps = structure_grid_x_steps_for_level(level_screens);
    int w_units = 1;
    int h_units = 1;
    structure_prefab_dims(prefab_id, &w_units, &h_units);
    const int max_gx = gx_steps - w_units + 1;
    const int max_gy = structure_grid_y_steps() - h_units + 1;
    int gx = structure_cell_from_mouse01(x01, gx_steps, max_gx);
    int gy = structure_cell_from_mouse01(y01, structure_grid_y_steps(), max_gy);
    if (structure_overlaps_cell(s, layer, gx_steps, -1, gx, gy, w_units, h_units)) {
        for (int radius = 1; radius <= 16; ++radius) {
            const int candidates[2] = {gx + radius, gx - radius};
            int found = 0;
            for (int ci = 0; ci < 2; ++ci) {
                const int cgx = clampi(candidates[ci], 0, (max_gx > 0) ? max_gx : 0);
                if (!structure_overlaps_cell(s, layer, gx_steps, -1, cgx, gy, w_units, h_units)) {
                    gx = cgx;
                    found = 1;
                    break;
                }
            }
            if (found) {
                break;
            }
        }
    }
    push_marker(
        s,
        LEVEL_EDITOR_MARKER_STRUCTURE,
        LEVEL_EDITOR_TRACK_SPATIAL,
        1,
        0.0f,
        (float)gx / (float)gx_steps,
        (float)gy / (float)structure_grid_y_steps(),
        (float)prefab_id,
        (float)layer,
        0.0f,
        0.0f
    );
    if (s->marker_count > 0) {
        level_editor_marker* m = &s->markers[s->marker_count - 1];
        m->e = 1.0f;
        m->f = 1.0f;
        m->g = 1.0f;
    }
    s->selected_marker = s->marker_count - 1;
    s->selected_property = 0;
    mark_editor_dirty(s);
}

static void toggle_structure_tool(level_editor_state* s, int tool_id, const char* label, float mouse_x, float mouse_y) {
    if (!s) {
        return;
    }
    if (s->structure_tool_selected == tool_id) {
        s->structure_tool_selected = 0;
        s->entity_drag_active = 0;
        s->entity_drag_kind = 0;
        snprintf(s->status_text, sizeof(s->status_text), "construction tool: off");
        return;
    }
    s->structure_tool_selected = tool_id;
    s->entity_tool_selected = 0;
    s->entity_drag_active = 1;
    s->entity_drag_kind = LEVEL_EDITOR_MARKER_STRUCTURE;
    s->entity_drag_x = mouse_x;
    s->entity_drag_y = mouse_y;
    snprintf(s->status_text, sizeof(s->status_text), "construction tool: %s", label ? label : "on");
}

int level_editor_handle_mouse(level_editor_state* s, float mouse_x, float mouse_y, float w, float h, int mouse_down, int mouse_pressed) {
    if (!s) {
        return 0;
    }
    level_editor_layout l;
    level_editor_compute_layout(w, h, &l);
    sync_timeline_window(s, &l);
    if (!mouse_down) {
        s->marker_drag_active = 0;
        s->marker_drag_index = -1;
    }
    if (s->entity_drag_active) {
        s->entity_drag_x = mouse_x;
        s->entity_drag_y = mouse_y;
    }
    if (s->marker_drag_active &&
        s->marker_drag_index >= 0 &&
        s->marker_drag_index < s->marker_count &&
        mouse_down &&
        point_in_rect(mouse_x, mouse_y, l.viewport)) {
        const float mx01 = (mouse_x - l.viewport.x) / fmaxf(l.viewport.w, 1.0f);
        const float my01 = (mouse_y - l.viewport.y) / fmaxf(l.viewport.h, 1.0f);
        place_structure_marker_from_view(s, s->marker_drag_index, mx01, my01);
        s->selected_marker = s->marker_drag_index;
        s->selected_property = 0;
        mark_editor_dirty(s);
        return 1;
    }

    if (mouse_pressed) {
        if (!point_in_rect(mouse_x, mouse_y, l.name_box)) {
            s->entry_active = 0;
        }
        if (point_in_rect(mouse_x, mouse_y, l.name_box)) {
            s->entry_active = 1;
            return 1;
        }
        if (point_in_rect(mouse_x, mouse_y, l.load_button)) {
            s->entry_active = 0;
            return 2;
        }
        if (point_in_rect(mouse_x, mouse_y, l.delete_button)) {
            s->entry_active = 0;
            return 8;
        }
        if (point_in_rect(mouse_x, mouse_y, l.new_button)) {
            s->entry_active = 0;
            return 7;
        }
        if (point_in_rect(mouse_x, mouse_y, l.save_button)) {
            s->entry_active = 0;
            return 3;
        }
        if (point_in_rect(mouse_x, mouse_y, l.save_new_button)) {
            s->entry_active = 0;
            return 6;
        }
        if (point_in_rect(mouse_x, mouse_y, l.prev_button)) {
            s->entry_active = 0;
            return 4;
        }
        if (point_in_rect(mouse_x, mouse_y, l.next_button)) {
            s->entry_active = 0;
            return 5;
        }
        if (point_in_rect(mouse_x, mouse_y, l.construction_button_0)) {
            toggle_structure_tool(s, 1, "panel square", mouse_x, mouse_y);
            return 1;
        }
        if (point_in_rect(mouse_x, mouse_y, l.construction_button_1)) {
            toggle_structure_tool(s, 2, "inset square", mouse_x, mouse_y);
            return 1;
        }
        if (point_in_rect(mouse_x, mouse_y, l.construction_button_2)) {
            toggle_structure_tool(s, 3, "cross brace", mouse_x, mouse_y);
            return 1;
        }
        if (point_in_rect(mouse_x, mouse_y, l.construction_button_3)) {
            toggle_structure_tool(s, 4, "tri", mouse_x, mouse_y);
            return 1;
        }
        if (point_in_rect(mouse_x, mouse_y, l.construction_button_4)) {
            toggle_structure_tool(s, 5, "triple inset", mouse_x, mouse_y);
            return 1;
        }
        if (point_in_rect(mouse_x, mouse_y, l.construction_button_5)) {
            toggle_structure_tool(s, 6, "pipe section", mouse_x, mouse_y);
            return 1;
        }
        if (point_in_rect(mouse_x, mouse_y, l.construction_button_6)) {
            toggle_structure_tool(s, 7, "valve wheel", mouse_x, mouse_y);
            return 1;
        }
        if (point_in_rect(mouse_x, mouse_y, l.construction_button_7)) {
            toggle_structure_tool(s, 8, "duct vent", mouse_x, mouse_y);
            return 1;
        }
        if (point_in_rect(mouse_x, mouse_y, l.swarm_button)) {
            s->entity_tool_selected = LEVEL_EDITOR_MARKER_BOID_FISH;
            s->entity_drag_active = 1;
            s->entity_drag_kind = LEVEL_EDITOR_MARKER_BOID_FISH;
            s->entity_drag_x = mouse_x;
            s->entity_drag_y = mouse_y;
            return 1;
        }
        if (point_in_rect(mouse_x, mouse_y, l.watcher_button)) {
            s->entity_tool_selected = LEVEL_EDITOR_MARKER_SEARCHLIGHT;
            s->entity_drag_active = 1;
            s->entity_drag_kind = LEVEL_EDITOR_MARKER_SEARCHLIGHT;
            s->entity_drag_x = mouse_x;
            s->entity_drag_y = mouse_y;
            return 1;
        }
        if (point_in_rect(mouse_x, mouse_y, l.asteroid_button)) {
            s->entity_tool_selected = LEVEL_EDITOR_MARKER_ASTEROID_STORM;
            s->entity_drag_active = 1;
            s->entity_drag_kind = LEVEL_EDITOR_MARKER_ASTEROID_STORM;
            s->entity_drag_x = mouse_x;
            s->entity_drag_y = mouse_y;
            return 1;
        }
        if (point_in_rect(mouse_x, mouse_y, l.mine_button)) {
            s->entity_tool_selected = LEVEL_EDITOR_MARKER_MINEFIELD;
            s->entity_drag_active = 1;
            s->entity_drag_kind = LEVEL_EDITOR_MARKER_MINEFIELD;
            s->entity_drag_x = mouse_x;
            s->entity_drag_y = mouse_y;
            return 1;
        }
        if (point_in_rect(mouse_x, mouse_y, l.missile_button)) {
            s->entity_tool_selected = LEVEL_EDITOR_MARKER_MISSILE;
            s->entity_drag_active = 1;
            s->entity_drag_kind = LEVEL_EDITOR_MARKER_MISSILE;
            s->entity_drag_x = mouse_x;
            s->entity_drag_y = mouse_y;
            return 1;
        }
        if (point_in_rect(mouse_x, mouse_y, l.arc_button)) {
            s->entity_tool_selected = LEVEL_EDITOR_MARKER_ARC_NODE;
            s->entity_drag_active = 1;
            s->entity_drag_kind = LEVEL_EDITOR_MARKER_ARC_NODE;
            s->entity_drag_x = mouse_x;
            s->entity_drag_y = mouse_y;
            return 1;
        }
        if (point_in_rect(mouse_x, mouse_y, l.timeline_window) || point_in_rect(mouse_x, mouse_y, l.timeline_track)) {
            s->timeline_drag = 1;
        }
        if (!level_editor_enemy_spatial(s) && point_in_rect(mouse_x, mouse_y, l.timeline_enemy_track)) {
            const int picked = pick_event_marker_in_enemy_timeline(s, mouse_x, &l);
            if (picked >= 0) {
                s->selected_marker = picked;
                s->selected_property = 0;
                return 1;
            }
            const float tx01 = clampf((mouse_x - l.timeline_enemy_track.x) / fmaxf(l.timeline_enemy_track.w, 1.0f), 0.0f, 1.0f);
            if (is_boid_wave_kind(s->entity_tool_selected) ||
                s->entity_tool_selected == LEVEL_EDITOR_MARKER_ASTEROID_STORM) {
                add_marker_at_timeline(s, s->entity_tool_selected, tx01);
                return 1;
            }
        }

        if (point_in_rect(mouse_x, mouse_y, l.viewport)) {
            const float mx01 = (mouse_x - l.viewport.x) / fmaxf(l.viewport.w, 1.0f);
            const float my01 = (mouse_y - l.viewport.y) / fmaxf(l.viewport.h, 1.0f);
            const int best = pick_spatial_marker_in_viewport(s, &l, mx01, my01, s->selected_marker);
            if (editor_pick_trace_enabled()) {
                fprintf(
                    stderr,
                    "[editor_pick] click viewport mx01=%.3f my01=%.3f best=%d struct_tool=%d entity_tool=%d enemy_spatial=%d\n",
                    mx01,
                    my01,
                    best,
                    s->structure_tool_selected,
                    s->entity_tool_selected,
                    level_editor_enemy_spatial(s)
                );
            }
            if (s->structure_tool_selected > 0 && s->entity_tool_selected == 0) {
                if (best >= 0) {
                    s->selected_marker = best;
                    s->selected_property = 0;
                    if (s->markers[best].kind == LEVEL_EDITOR_MARKER_STRUCTURE &&
                        s->markers[best].track == LEVEL_EDITOR_TRACK_SPATIAL) {
                        s->marker_drag_active = 1;
                        s->marker_drag_index = best;
                    }
                } else {
                    add_structure_marker_at_view(s, mx01, my01);
                }
            } else if (is_boid_wave_kind(s->entity_tool_selected) ||
                       s->entity_tool_selected == LEVEL_EDITOR_MARKER_SEARCHLIGHT ||
                       s->entity_tool_selected == LEVEL_EDITOR_MARKER_ASTEROID_STORM ||
                       s->entity_tool_selected == LEVEL_EDITOR_MARKER_MINEFIELD ||
                       s->entity_tool_selected == LEVEL_EDITOR_MARKER_MISSILE ||
                       s->entity_tool_selected == LEVEL_EDITOR_MARKER_ARC_NODE) {
                if (best >= 0) {
                    s->selected_marker = best;
                } else {
                    add_marker_at_view(s, s->entity_tool_selected, mx01, my01);
                }
            } else if (best >= 0) {
                s->selected_marker = best;
                if (s->markers[best].kind == LEVEL_EDITOR_MARKER_STRUCTURE &&
                    s->markers[best].track == LEVEL_EDITOR_TRACK_SPATIAL) {
                    s->marker_drag_active = 1;
                    s->marker_drag_index = best;
                }
                if (editor_pick_trace_enabled()) {
                    fprintf(stderr, "[editor_pick] action=select idx=%d kind=%d\n", best, (int)s->markers[best].kind);
                }
            } else {
                s->selected_marker = -1;
                if (editor_pick_trace_enabled()) {
                    fprintf(stderr, "[editor_pick] action=clear_selection\n");
                }
            }
            s->selected_property = 0;
            return 1;
        }
    }

    if (!mouse_down) {
        s->timeline_drag = 0;
    }
    if (s->timeline_drag) {
        const float level_screens = fmaxf(s->level_length_screens, 1.0f);
        const float window_w = l.timeline_track.w / level_screens;
        const float min_x = l.timeline_track.x;
        const float max_x = l.timeline_track.x + l.timeline_track.w - window_w;
        const float tx = clampf(mouse_x - window_w * 0.5f, min_x, max_x);
        if (max_x > min_x) {
            s->timeline_01 = (tx - min_x) / (max_x - min_x);
        } else {
            s->timeline_01 = 0.0f;
        }
        return 1;
    }

    return 0;
}

int level_editor_handle_mouse_release(level_editor_state* s, float mouse_x, float mouse_y, float w, float h) {
    if (!s) {
        return 0;
    }
    s->marker_drag_active = 0;
    s->marker_drag_index = -1;
    if (!s->entity_drag_active) {
        return 0;
    }
    level_editor_layout l;
    level_editor_compute_layout(w, h, &l);
    if (point_in_rect(mouse_x, mouse_y, l.viewport)) {
        const float mx01 = (mouse_x - l.viewport.x) / fmaxf(l.viewport.w, 1.0f);
        const float my01 = (mouse_y - l.viewport.y) / fmaxf(l.viewport.h, 1.0f);
        if (s->entity_drag_kind == LEVEL_EDITOR_MARKER_STRUCTURE) {
            add_structure_marker_at_view(s, mx01, my01);
        } else {
            add_marker_at_view(s, s->entity_drag_kind, mx01, my01);
        }
    } else if (point_in_rect(mouse_x, mouse_y, l.timeline_track)) {
        const float tx01 = clampf((mouse_x - l.timeline_track.x) / fmaxf(l.timeline_track.w, 1.0f), 0.0f, 1.0f);
        if (s->entity_drag_kind == LEVEL_EDITOR_MARKER_SEARCHLIGHT ||
            is_boid_wave_kind(s->entity_drag_kind) ||
            s->entity_drag_kind == LEVEL_EDITOR_MARKER_MINEFIELD ||
            s->entity_drag_kind == LEVEL_EDITOR_MARKER_MISSILE ||
            s->entity_drag_kind == LEVEL_EDITOR_MARKER_ARC_NODE ||
            s->entity_drag_kind == LEVEL_EDITOR_MARKER_ASTEROID_STORM) {
            add_spatial_marker_at_x01(
                s,
                s->entity_drag_kind,
                tx01,
                (is_boid_wave_kind(s->entity_drag_kind)) ? 0.50f : 0.10f
            );
        }
    } else if (point_in_rect(mouse_x, mouse_y, l.timeline_enemy_track)) {
        const float tx01 = clampf((mouse_x - l.timeline_enemy_track.x) / fmaxf(l.timeline_enemy_track.w, 1.0f), 0.0f, 1.0f);
        if (!level_editor_enemy_spatial(s)) {
            add_marker_at_timeline(s, s->entity_drag_kind, tx01);
        }
    }
    if (s->entity_drag_kind != LEVEL_EDITOR_MARKER_STRUCTURE) {
        s->entity_tool_selected = 0;
    }
    s->entity_drag_active = 0;
    s->entity_drag_kind = 0;
    return 1;
}

void level_editor_select_marker(level_editor_state* s, int delta) {
    if (!s || s->marker_count <= 0 || delta == 0) {
        return;
    }
    if (s->selected_marker < 0 || s->selected_marker >= s->marker_count) {
        s->selected_marker = 0;
        return;
    }
    s->selected_marker = (s->selected_marker + delta + s->marker_count) % s->marker_count;
}

void level_editor_select_property(level_editor_state* s, int delta) {
    if (!s || delta == 0) {
        return;
    }
    const int prop_n = marker_property_count(s);
    if (prop_n <= 0) {
        s->selected_property = 0;
        return;
    }
    s->selected_property = (s->selected_property + delta + prop_n) % prop_n;
}

const char* level_editor_selected_property_name(const level_editor_state* s) {
    static const char* names[10] = {"X", "Y", "A", "B", "C", "D", "E", "F", "G", "H"};
    if (!s) {
        return "X";
    }
    const int idx = (s->selected_property >= 0 && s->selected_property < 10) ? s->selected_property : 0;
    return names[idx];
}

void level_editor_adjust_selected_property(level_editor_state* s, float delta) {
    if (!s || delta == 0.0f) {
        return;
    }
    const int prop_count = marker_property_count(s);
    if (prop_count <= 0) {
        return;
    }
    if (s->selected_property < 0) {
        s->selected_property = 0;
    }
    if (s->selected_property >= prop_count) {
        s->selected_property = prop_count - 1;
    }
    if (s->selected_marker < 0 || s->selected_marker >= s->marker_count) {
        switch (s->selected_property) {
            case 0: {
                const int dir = (delta >= 0.0f) ? 1 : -1;
                const int n = 3;
                int m = s->level_wave_mode;
                if (m < 0 || m >= n) {
                    m = 0;
                }
                s->level_wave_mode = (m + dir + n) % n;
            } break;
            case 1: {
                const int dir = (delta >= 0.0f) ? 1 : -1;
                const int n = 6;
                int r = s->level_render_style;
                if (r < 0 || r >= n) {
                    r = LEVEL_RENDER_DEFENDER;
                }
                s->level_render_style = (r + dir + n) % n;
                s->level_style = level_style_from_render_style(s->level_render_style);
            } break;
            case 2: {
                const int dir = (delta >= 0.0f) ? 1 : -1;
                const int n = 3;
                int p = clampi(s->level_theme_palette, 0, n - 1);
                s->level_theme_palette = (p + dir + n) % n;
            } break;
            case 3:
                s->level_length_screens = clampf(
                    s->level_length_screens + delta * 1.0f,
                    1.0f,
                    400.0f
                );
                break;
            default:
                break;
        }
        mark_editor_dirty(s);
        return;
    }
    level_editor_marker* m = &s->markers[s->selected_marker];

    if (m->kind == LEVEL_EDITOR_MARKER_ASTEROID_STORM) {
        const int ev_item = marker_is_event_item(s, m);
        switch (s->selected_property) {
            case 0:
                if (ev_item) m->order = (int)fmaxf(1.0f, (float)m->order + ((delta >= 0.0f) ? 1.0f : -1.0f));
                else move_marker_x(s, s->selected_marker, delta);
                break;
            case 1:
                if (ev_item) m->delay_s = fmaxf(0.0f, m->delay_s + delta * 0.1f);
                else m->y01 = clampf(m->y01 + delta, 0.0f, 1.0f);
                break;
            case 2: m->a = fmaxf(0.01f, m->a + delta * 0.5f); break;
            case 3: m->b += delta * 1.0f; break;
            case 4: m->c = clampf(m->c + delta * 10.0f, 1.0f, 4000.0f); break;
            case 5: m->d = clampf(m->d + delta * 0.05f, 0.01f, 8.0f); break;
            default: break;
        }
        mark_editor_dirty(s);
        return;
    }

    if (m->kind == LEVEL_EDITOR_MARKER_SEARCHLIGHT) {
        switch (s->selected_property) {
            case 0: move_marker_x(s, s->selected_marker, delta); break;
            case 1: m->y01 = clampf(m->y01 + delta, 0.0f, 1.0f); break;
            case 2: m->a = clampf(m->a + delta * 0.01f, 0.05f, 2.5f); break;
            case 3: m->b = clampf(m->b + delta * 1.0f, 1.0f, 179.0f); break;
            case 4: m->c = clampf(m->c + delta * 0.1f, 0.01f, 50.0f); break;
            case 5: m->d = clampf(m->d + delta * 1.0f, 0.0f, 360.0f); break;
            case 6: {
                const int dir = (delta >= 0.0f) ? 1 : -1;
                const int n = 3;
                int mode = clampi((int)lroundf(m->g), 0, n - 1);
                mode = (mode + dir + n) % n;
                m->g = (float)mode;
            } break;
            case 7: {
                const int dir = (delta >= 0.0f) ? 1 : -1;
                const int n = 2;
                int src = clampi((int)lroundf(m->e), 0, n - 1);
                src = (src + dir + n) % n;
                m->e = (float)src;
            } break;
            case 8: m->f = clampf(m->f + delta * 1.0f, 4.0f, 120.0f); break;
            default: break;
        }
        mark_editor_dirty(s);
        return;
    }
    if (m->kind == LEVEL_EDITOR_MARKER_MINEFIELD) {
        switch (s->selected_property) {
            case 0: move_marker_x(s, s->selected_marker, delta); break;
            case 1: m->y01 = clampf(m->y01 + delta, 0.0f, 1.0f); break;
            case 2: m->a = clampf(m->a + delta * 1.0f, 1.0f, 128.0f); break;
            default: break;
        }
        mark_editor_dirty(s);
        return;
    }
    if (m->kind == LEVEL_EDITOR_MARKER_MISSILE) {
        switch (s->selected_property) {
            case 0: move_marker_x(s, s->selected_marker, delta); break;
            case 1: m->y01 = clampf(m->y01 + delta, 0.0f, 1.0f); break;
            case 2: m->a = clampf(m->a + delta * 1.0f, 1.0f, 128.0f); break;
            case 3: m->b = clampf(m->b + delta * 5.0f, 0.0f, 400.0f); break;
            case 4: m->c = clampf(m->c + delta * 10.0f, 20.0f, 2500.0f); break;
            case 5: m->d = clampf(m->d + delta * 0.1f, 0.20f, 30.0f); break;
            default: break;
        }
        mark_editor_dirty(s);
        return;
    }
    if (m->kind == LEVEL_EDITOR_MARKER_ARC_NODE) {
        const float gx_step = 1.0f / (float)structure_grid_x_steps_for_level(s->level_length_screens);
        const float gy_step = 1.0f / (float)structure_grid_y_steps();
        const float coarse = (fabsf(delta) >= 9.0f) ? 1.0f : 0.25f;
        switch (s->selected_property) {
            case 0: m->x01 = clampf(m->x01 + ((delta >= 0.0f) ? gx_step * coarse : -gx_step * coarse), 0.0f, 1.0f); break;
            case 1: m->y01 = clampf(m->y01 + ((delta >= 0.0f) ? gy_step * coarse : -gy_step * coarse), 0.0f, 1.0f); break;
            case 2: m->a = clampf(m->a + delta * 0.1f, 0.10f, 60.0f); break;
            case 3: m->b = clampf(m->b + delta * 0.1f, 0.0f, m->a); break;
            case 4: m->c = clampf(m->c + delta * 2.0f, 4.0f, 400.0f); break;
            case 5: m->d = clampf(m->d + delta * 50.0f, 0.0f, 20000.0f); break;
            case 6: m->e = clampf(m->e + delta * 0.02f, 0.02f, 3.00f); break;
            default: break;
        }
        mark_editor_dirty(s);
        return;
    }
    if (m->kind == LEVEL_EDITOR_MARKER_STRUCTURE) {
        const float gx_step = 1.0f / (float)structure_grid_x_steps_for_level(s->level_length_screens);
        const float gy_step = 1.0f / (float)structure_grid_y_steps();
        switch (s->selected_property) {
            case 0: m->x01 = clampf(m->x01 + ((delta >= 0.0f) ? gx_step : -gx_step), 0.0f, 1.0f); break;
            case 1: m->y01 = clampf(m->y01 + ((delta >= 0.0f) ? gy_step : -gy_step), 0.0f, 1.0f); break;
            case 2: {
                int id = (int)lroundf(m->a) + ((delta >= 0.0f) ? 1 : -1);
                if (id < 0) id = 7;
                if (id > 7) id = 0;
                m->a = (float)id;
            } break;
            case 3: m->b = (m->b >= 0.5f) ? 0.0f : 1.0f; break;
            case 4: m->c = (float)(((int)lroundf(m->c) + ((delta >= 0.0f) ? 1 : 3)) & 3); break;
            case 5: {
                int flip = ((int)lroundf(m->d)) & 3;
                flip ^= 1;
                m->d = (float)flip;
            } break;
            case 6: m->e = clampf(m->e + delta * 0.1f, 0.10f, 6.00f); break;
            case 7: m->f = clampf(m->f + delta * 0.1f, 0.10f, 6.00f); break;
            case 8: m->g = clampf(m->g + delta * 0.1f, 0.20f, 8.00f); break;
            default: break;
        }
        m->x01 = snap_x01_level(m->x01, s->level_length_screens);
        m->y01 = snap_y01(m->y01);
        {
            const int gx_steps = structure_grid_x_steps_for_level(s->level_length_screens);
            int w_units = 1;
            int h_units = 1;
            int gx = clampi((int)lroundf(m->x01 * (float)gx_steps), 0, gx_steps);
            int gy = clampi((int)lroundf(m->y01 * (float)structure_grid_y_steps()), 0, structure_grid_y_steps());
            structure_prefab_dims(clampi((int)lroundf(m->a), 0, 31), &w_units, &h_units);
            gx = clampi(gx, 0, ((gx_steps - w_units + 1) > 0) ? (gx_steps - w_units + 1) : 0);
            gy = clampi(gy, 0, ((structure_grid_y_steps() - h_units + 1) > 0) ? (structure_grid_y_steps() - h_units + 1) : 0);
            m->x01 = (float)gx / (float)gx_steps;
            m->y01 = (float)gy / (float)structure_grid_y_steps();
        }
        mark_editor_dirty(s);
        return;
    }

    if (m->kind == LEVEL_EDITOR_MARKER_EXIT) {
        switch (s->selected_property) {
            case 0: move_marker_x(s, s->selected_marker, delta); break;
            case 1: m->y01 = clampf(m->y01 + delta, 0.0f, 1.0f); break;
            default: break;
        }
        mark_editor_dirty(s);
        return;
    }

    if (is_wave_kind(m->kind)) {
        const int ev_item = marker_is_event_item(s, m);
        const int boid_item = is_boid_wave_kind(m->kind);
        switch (s->selected_property) {
            case 0:
                m->kind = cycle_wave_kind(m->kind, (delta >= 0.0f) ? 1 : -1);
                if (is_boid_wave_kind(m->kind) && m->d <= 0.0f) {
                    m->d = boid_turn_rate_default_deg_for_kind(m->kind);
                }
                break;
            case 1:
                if (ev_item) m->order = (int)fmaxf(1.0f, (float)m->order + ((delta >= 0.0f) ? 1.0f : -1.0f));
                else move_marker_x(s, s->selected_marker, delta);
                break;
            case 2:
                if (ev_item) m->delay_s = fmaxf(0.0f, m->delay_s + delta * 0.1f);
                else m->y01 = clampf(m->y01 + delta, 0.0f, 1.0f);
                break;
            case 3: m->a += delta * 1.0f; break;
            case 4: m->b += delta * 5.0f; break;
            case 5: m->c += delta * 5.0f; break;
            case 6:
                if (boid_item) {
                    m->d = clampf(m->d + delta * 1.0f, 10.0f, 720.0f);
                } else if (m->kind == LEVEL_EDITOR_MARKER_WAVE_KAMIKAZE) {
                    s->level_kamikaze_radius_min = clampf(s->level_kamikaze_radius_min + delta * 1.0f, 1.0f, 200.0f);
                    if (s->level_kamikaze_radius_max < s->level_kamikaze_radius_min) {
                        s->level_kamikaze_radius_max = s->level_kamikaze_radius_min;
                    }
                } else if (!ev_item) {
                    m->delay_s = fmaxf(0.0f, m->delay_s + delta * 0.1f);
                }
                break;
            case 7:
                if (m->kind == LEVEL_EDITOR_MARKER_WAVE_KAMIKAZE) {
                    s->level_kamikaze_radius_max = clampf(s->level_kamikaze_radius_max + delta * 1.0f, 1.0f, 240.0f);
                    if (s->level_kamikaze_radius_max < s->level_kamikaze_radius_min) {
                        s->level_kamikaze_radius_min = s->level_kamikaze_radius_max;
                    }
                } else if (boid_item && !ev_item) {
                    m->delay_s = fmaxf(0.0f, m->delay_s + delta * 0.1f);
                }
                break;
            case 8:
                if (m->kind == LEVEL_EDITOR_MARKER_WAVE_KAMIKAZE && !ev_item) {
                    m->delay_s = fmaxf(0.0f, m->delay_s + delta * 0.1f);
                }
                break;
            default: break;
        }
        mark_editor_dirty(s);
        return;
    }
}

int level_editor_cycle_level(level_editor_state* s, const leveldef_db* db, int delta) {
    if (!s || !db || delta == 0) {
        return 0;
    }
    const int n = level_style_count();
    if (n <= 0) {
        return 0;
    }
    int style = s->level_style;
    if (style < 0 || style >= n) {
        style = 0;
    }
    style = (style + delta + n) % n;
    return level_editor_load_by_name(s, db, level_style_name(style));
}

int level_editor_save_current(level_editor_state* s, const leveldef_db* db, char* out_path, size_t out_path_cap) {
    char level_name_file[LEVEL_EDITOR_NAME_CAP];
    char serialized[16384];
    if (!s) {
        return 0;
    }
    if (out_path && out_path_cap > 0) {
        out_path[0] = '\0';
    }

    if (s->source_path[0] == '\0') {
        normalize_level_name_for_file(s->level_name, level_name_file, sizeof(level_name_file));
        snprintf(s->level_name, sizeof(s->level_name), "%s", level_name_file);
        if (!resolve_level_file_path(s->level_name, s->source_path, sizeof(s->source_path))) {
            char dir[LEVEL_EDITOR_PATH_CAP];
            if (!choose_levels_dir(dir, sizeof(dir))) {
                snprintf(s->status_text, sizeof(s->status_text), "save failed: level file not found");
                return 0;
            }
            if (snprintf(s->source_path, sizeof(s->source_path), "%s/%s.cfg", dir, s->level_name) >= (int)sizeof(s->source_path)) {
                snprintf(s->status_text, sizeof(s->status_text), "save failed: path too long");
                return 0;
            }
        }
    }
    if (!build_level_serialized_text(s, db, serialized, sizeof(serialized), NULL)) {
        snprintf(s->status_text, sizeof(s->status_text), "save failed: serialize");
        return 0;
    }
    if (!write_file_text(s->source_path, serialized)) {
        snprintf(s->status_text, sizeof(s->status_text), "save failed: write");
        return 0;
    }
    snprintf(s->source_text, sizeof(s->source_text), "%s", serialized);
    s->dirty = 0;
    {
        leveldef_level ll;
        int style = s->level_style;
        if (db && leveldef_load_level_file_with_base(db, s->source_path, &ll, &style, NULL)) {
            s->loaded_level = ll;
            s->loaded_level_valid = 1;
            s->level_style = style;
        }
    }
    level_editor_save_snapshot(s);
    if (out_path && out_path_cap > 0) {
        snprintf(out_path, out_path_cap, "%s", s->source_path);
    }
    snprintf(s->status_text, sizeof(s->status_text), "saved %s", s->level_name);
    return 1;
}

int level_editor_save_new(level_editor_state* s, const leveldef_db* db, char* out_path, size_t out_path_cap) {
    char dir[LEVEL_EDITOR_PATH_CAP];
    char serialized[16384];
    char path[LEVEL_EDITOR_PATH_CAP];
    char level_name[LEVEL_EDITOR_NAME_CAP];
    const char* base;
    int next = 1;

    if (!s || !db) {
        return 0;
    }
    if (out_path && out_path_cap > 0) {
        out_path[0] = '\0';
    }
    if (!choose_levels_dir(dir, sizeof(dir))) {
        snprintf(s->status_text, sizeof(s->status_text), "save new failed: levels dir not found");
        return 0;
    }
    if (!build_level_serialized_text(s, db, serialized, sizeof(serialized), NULL)) {
        snprintf(s->status_text, sizeof(s->status_text), "save new failed: serialize");
        return 0;
    }

    base = render_style_file_base(s->level_render_style);
    for (next = 1; next <= 999; ++next) {
        if (snprintf(level_name, sizeof(level_name), "%s_%02d", base, next) >= (int)sizeof(level_name)) {
            continue;
        }
        if (snprintf(path, sizeof(path), "%s/%s.cfg", dir, level_name) >= (int)sizeof(path)) {
            continue;
        }
        if (!path_exists(path)) {
            break;
        }
    }
    if (next > 999) {
        snprintf(s->status_text, sizeof(s->status_text), "save new failed: no free slot");
        return 0;
    }
    if (!write_file_text(path, serialized)) {
        snprintf(s->status_text, sizeof(s->status_text), "save new failed: write");
        return 0;
    }

    snprintf(s->level_name, sizeof(s->level_name), "%s", level_name);
    snprintf(s->source_path, sizeof(s->source_path), "%s", path);
    snprintf(s->source_text, sizeof(s->source_text), "%s", serialized);
    {
        leveldef_level ll;
        int style = s->level_style;
        if (leveldef_load_level_file_with_base(db, s->source_path, &ll, &style, NULL)) {
            s->loaded_level = ll;
            s->loaded_level_valid = 1;
            s->level_style = style;
        }
    }
    s->dirty = 0;
    level_editor_save_snapshot(s);
    if (out_path && out_path_cap > 0) {
        snprintf(out_path, out_path_cap, "%s", path);
    }
    snprintf(s->status_text, sizeof(s->status_text), "saved new %s", level_name);
    return 1;
}

int level_editor_revert(level_editor_state* s) {
    if (!s || !s->snapshot_valid) {
        if (s) {
            snprintf(s->status_text, sizeof(s->status_text), "revert failed: no snapshot");
        }
        return 0;
    }
    s->level_length_screens = s->snapshot_level_length_screens;
    s->level_render_style = s->snapshot_level_render_style;
    s->level_wave_mode = s->snapshot_level_wave_mode;
    s->level_theme_palette = s->snapshot_level_theme_palette;
    s->level_asteroid_storm_enabled = s->snapshot_level_asteroid_storm_enabled;
    s->level_asteroid_storm_angle_deg = s->snapshot_level_asteroid_storm_angle_deg;
    s->level_asteroid_storm_speed = s->snapshot_level_asteroid_storm_speed;
    s->level_asteroid_storm_duration_s = s->snapshot_level_asteroid_storm_duration_s;
    s->level_asteroid_storm_density = s->snapshot_level_asteroid_storm_density;
    s->level_kamikaze_radius_min = s->snapshot_level_kamikaze_radius_min;
    s->level_kamikaze_radius_max = s->snapshot_level_kamikaze_radius_max;
    s->level_style = level_style_from_render_style(s->level_render_style);
    snprintf(s->level_name, sizeof(s->level_name), "%s", s->snapshot_level_name);
    s->marker_count = s->snapshot_marker_count;
    if (s->marker_count > 0) {
        memcpy(
            s->markers,
            s->snapshot_markers,
            (size_t)s->marker_count * sizeof(level_editor_marker)
        );
    }
    s->selected_marker = (s->marker_count > 0) ? 0 : -1;
    s->selected_property = 0;
    s->dirty = 0;
    s->edit_revision += 1u;
    snprintf(s->status_text, sizeof(s->status_text), "reverted %s", s->level_name);
    return 1;
}

void level_editor_new_blank(level_editor_state* s) {
    if (!s) {
        return;
    }
    clear_markers(s);
    s->timeline_01 = 0.0f;
    s->selected_property = 0;
    s->entry_active = 1;
    s->source_path[0] = '\0';
    s->source_text[0] = '\0';
    s->loaded_level_valid = 0;
    memset(&s->loaded_level, 0, sizeof(s->loaded_level));
    s->snapshot_valid = 0;
    s->marker_drag_active = 0;
    s->marker_drag_index = -1;
    mark_editor_dirty(s);
    s->level_render_style = LEVEL_RENDER_BLANK;
    s->level_style = LEVEL_STYLE_BLANK;
    s->level_theme_palette = 0;
    s->level_asteroid_storm_enabled = 0;
    s->level_asteroid_storm_angle_deg = 180.0f;
    s->level_asteroid_storm_speed = 190.0f;
    s->level_asteroid_storm_duration_s = 12.0f;
    s->level_asteroid_storm_density = 1.0f;
    s->level_kamikaze_radius_min = 11.0f;
    s->level_kamikaze_radius_max = 17.0f;
    snprintf(s->level_name, sizeof(s->level_name), "level_blank");
    snprintf(s->status_text, sizeof(s->status_text), "new level");
}

int level_editor_delete_selected(level_editor_state* s) {
    if (!s || s->selected_marker < 0 || s->selected_marker >= s->marker_count) {
        return 0;
    }
    const level_editor_marker removed = s->markers[s->selected_marker];
    for (int i = s->selected_marker; i + 1 < s->marker_count; ++i) {
        s->markers[i] = s->markers[i + 1];
    }
    s->marker_count -= 1;
    if (s->selected_marker >= s->marker_count) {
        s->selected_marker = s->marker_count - 1;
    }
    if (s->selected_marker < 0) {
        s->selected_property = 0;
    } else {
        const int prop_n = marker_property_count(s);
        if (s->selected_property >= prop_n) {
            s->selected_property = (prop_n > 0) ? (prop_n - 1) : 0;
        }
    }
    if (removed.track == LEVEL_EDITOR_TRACK_EVENT && removed.order > 0) {
        for (int i = 0; i < s->marker_count; ++i) {
            level_editor_marker* m = &s->markers[i];
            if (m->track == LEVEL_EDITOR_TRACK_EVENT && m->order > removed.order) {
                m->order -= 1;
            }
        }
    }
    mark_editor_dirty(s);
    return 1;
}

int level_editor_build_level(const level_editor_state* s, const leveldef_db* db, leveldef_level* out_level) {
    if (!s || !db || !out_level) {
        return 0;
    }
    return build_level_serialized_text(s, db, NULL, 0, out_level);
}

int level_editor_rotate_selected_structure(level_editor_state* s, int delta_quadrants) {
    int q = 0;
    if (!s || s->selected_marker < 0 || s->selected_marker >= s->marker_count || delta_quadrants == 0) {
        return 0;
    }
    level_editor_marker* m = &s->markers[s->selected_marker];
    if (m->kind != LEVEL_EDITOR_MARKER_STRUCTURE || m->track != LEVEL_EDITOR_TRACK_SPATIAL) {
        return 0;
    }
    q = (int)lroundf(m->c);
    q = (q + delta_quadrants) % 4;
    if (q < 0) {
        q += 4;
    }
    m->c = (float)q;
    mark_editor_dirty(s);
    return 1;
}
