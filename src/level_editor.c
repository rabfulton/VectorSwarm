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
    static const char* dirs[] = {"data/levels", "../data/levels"};
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
            kind == LEVEL_EDITOR_MARKER_BOID);
}

static int is_enemy_marker_kind(int kind) {
    return is_wave_kind(kind);
}

static int level_editor_enemy_spatial(const level_editor_state* s) {
    if (!s) {
        return 1;
    }
    return (s->level_wave_mode == LEVELDEF_WAVES_CURATED &&
            s->level_render_style == LEVEL_RENDER_DEFENDER);
}

static void level_editor_save_snapshot(level_editor_state* s) {
    if (!s) {
        return;
    }
    s->snapshot_valid = 1;
    s->snapshot_level_length_screens = s->level_length_screens;
    s->snapshot_level_render_style = s->level_render_style;
    s->snapshot_level_wave_mode = s->level_wave_mode;
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
    if (kind == LEVEL_EDITOR_MARKER_BOID ||
        kind == LEVEL_EDITOR_MARKER_WAVE_SINE ||
        kind == LEVEL_EDITOR_MARKER_WAVE_V ||
        kind == LEVEL_EDITOR_MARKER_WAVE_KAMIKAZE) {
        return 0.18f;
    }
    if (kind == LEVEL_EDITOR_MARKER_SEARCHLIGHT) {
        return 0.12f;
    }
    if (kind == LEVEL_EDITOR_MARKER_EXIT) {
        return 0.11f;
    }
    return 0.08f;
}

typedef struct wave_marker_ref {
    float x01;
    level_editor_marker marker;
} wave_marker_ref;

static int compare_wave_marker_ref(const void* a, const void* b) {
    const wave_marker_ref* wa = (const wave_marker_ref*)a;
    const wave_marker_ref* wb = (const wave_marker_ref*)b;
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
        case LEVELDEF_WAVE_KAMIKAZE: return "kamikaze";
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
    return "sine";
}

static const char* render_style_file_base(int render_style) {
    switch (render_style) {
        case LEVEL_RENDER_DEFENDER: return "level_defender";
        case LEVEL_RENDER_CYLINDER: return "level_enemy_radar";
        case LEVEL_RENDER_DRIFTER: return "level_high_plains_drifter";
        case LEVEL_RENDER_DRIFTER_SHADED: return "level_high_plains_drifter_2";
        case LEVEL_RENDER_FOG: return "level_fog_of_war";
        default: return "level_defender";
    }
}

static int level_style_from_render_style(int render_style) {
    switch (render_style) {
        case LEVEL_RENDER_CYLINDER: return LEVEL_STYLE_ENEMY_RADAR;
        case LEVEL_RENDER_DRIFTER: return LEVEL_STYLE_HIGH_PLAINS_DRIFTER;
        case LEVEL_RENDER_DRIFTER_SHADED: return LEVEL_STYLE_HIGH_PLAINS_DRIFTER_2;
        case LEVEL_RENDER_FOG: return LEVEL_STYLE_FOG_OF_WAR;
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

static int build_level_serialized_text(const level_editor_state* s, const leveldef_db* db, char* out, size_t out_cap) {
    size_t used = 0;
    leveldef_level lvl;
    int searchlight_n = 0;
    int i = 0;
    int found_exit = 0;
    int wave_n = 0;
    wave_marker_ref waves[LEVEL_EDITOR_MAX_MARKERS];
    const leveldef_level* base = NULL;
    const float level_len = fmaxf(s ? s->level_length_screens : 1.0f, 1.0f);

    if (!s || !db || !out || out_cap == 0) {
        return 0;
    }
    base = leveldef_get_level(db, s->level_style);
    if (!base) {
        return 0;
    }
    lvl = *base;
    lvl.render_style = s->level_render_style;
    lvl.wave_mode = s->level_wave_mode;

    lvl.exit_enabled = 0;
    for (i = 0; i < s->marker_count; ++i) {
        const level_editor_marker* m = &s->markers[i];
        if (m->kind == LEVEL_EDITOR_MARKER_EXIT && !found_exit) {
            lvl.exit_enabled = 1;
            lvl.exit_x01 = m->x01 * level_len;
            lvl.exit_y01 = m->y01;
            found_exit = 1;
        }
    }

    lvl.searchlight_count = 0;
    for (i = 0; i < s->marker_count && searchlight_n < MAX_SEARCHLIGHTS; ++i) {
        const level_editor_marker* m = &s->markers[i];
        leveldef_searchlight sl;
        if (m->kind != LEVEL_EDITOR_MARKER_SEARCHLIGHT) {
            continue;
        }
        if (searchlight_n < base->searchlight_count) {
            sl = base->searchlights[searchlight_n];
        } else if (base->searchlight_count > 0) {
            sl = base->searchlights[base->searchlight_count - 1];
        } else {
            memset(&sl, 0, sizeof(sl));
            sl.sweep_motion = SEARCHLIGHT_MOTION_PENDULUM;
            sl.source_type = SEARCHLIGHT_SOURCE_DOME;
            sl.source_radius = 14.0f;
            sl.clear_grace_s = 2.0f;
            sl.fire_interval_s = 0.08f;
            sl.projectile_speed = 900.0f;
            sl.projectile_ttl_s = 2.0f;
            sl.projectile_radius = 3.2f;
            sl.aim_jitter_deg = 1.0f;
        }
        sl.anchor_x01 = m->x01 * level_len;
        sl.anchor_y01 = m->y01;
        sl.length_h01 = m->a;
        sl.half_angle_deg = m->b;
        sl.sweep_speed = m->c;
        sl.sweep_amplitude_deg = m->d;
        lvl.searchlights[searchlight_n++] = sl;
    }
    lvl.searchlight_count = searchlight_n;

    for (i = 0; i < s->marker_count; ++i) {
        const level_editor_marker* m = &s->markers[i];
        if (!is_wave_kind(m->kind) || wave_n >= LEVEL_EDITOR_MAX_MARKERS) {
            continue;
        }
        waves[wave_n].x01 = m->x01;
        waves[wave_n].marker = *m;
        ++wave_n;
    }
    if (wave_n > 1) {
        qsort(waves, (size_t)wave_n, sizeof(waves[0]), compare_wave_marker_ref);
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
    } else if (lvl.wave_mode == LEVELDEF_WAVES_BOID_ONLY) {
        const int base_cycle_n = (base->boid_cycle_count > 0) ? base->boid_cycle_count : 1;
        int boid_n = 0;
        for (i = 0; i < wave_n && boid_n < LEVELDEF_MAX_BOID_CYCLE; ++i) {
            if (waves[i].marker.kind != LEVEL_EDITOR_MARKER_BOID) {
                continue;
            }
            lvl.boid_cycle[boid_n] = (base->boid_cycle_count > 0)
                ? base->boid_cycle[boid_n % base_cycle_n]
                : ((lvl.default_boid_profile >= 0) ? lvl.default_boid_profile : 0);
            ++boid_n;
        }
        if (boid_n > 0) {
            lvl.boid_cycle_count = boid_n;
        }
    } else {
        int cycle_n = 0;
        int idx = 0;
        for (i = 0; i < wave_n && cycle_n < LEVELDEF_MAX_BOID_CYCLE; ++i) {
            const level_editor_marker* m = &waves[i].marker;
            if (m->kind == LEVEL_EDITOR_MARKER_BOID) {
                lvl.wave_cycle[cycle_n++] = LEVELDEF_WAVE_SWARM;
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

    if (!appendf(out, out_cap, &used, "# LevelDef v1\n")) return 0;
    if (!appendf(out, out_cap, &used, "# wave_cycle tokens: sine_snake,v_formation,swarm,kamikaze\n")) return 0;
    if (!appendf(out, out_cap, &used, "# searchlight CSV fields:\n")) return 0;
    if (!appendf(out, out_cap, &used, "# anchor_x01,anchor_y01,length_h01,half_angle_deg,sweep_center_deg,sweep_amplitude_deg,\n")) return 0;
    if (!appendf(out, out_cap, &used, "# sweep_speed,sweep_phase_deg,sweep_motion,source_type,source_radius,clear_grace_s,\n")) return 0;
    if (!appendf(out, out_cap, &used, "# fire_interval_s,projectile_speed,projectile_ttl_s,projectile_radius,aim_jitter_deg\n")) return 0;
    if (!appendf(out, out_cap, &used, "[level %s]\n", style_header_name(s->level_style))) return 0;
    if (!appendf(out, out_cap, &used, "render_style=%s\n", render_style_name(lvl.render_style))) return 0;
    if (!appendf(out, out_cap, &used, "wave_mode=%s\n", wave_mode_name(lvl.wave_mode))) return 0;
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

    if (lvl.wave_mode == LEVELDEF_WAVES_BOID_ONLY) {
        if (!appendf(out, out_cap, &used, "boid_cycle=")) return 0;
        for (i = 0; i < lvl.boid_cycle_count; ++i) {
            const int pid = lvl.boid_cycle[i];
            const char* pname = (pid >= 0 && pid < db->profile_count) ? db->profiles[pid].name : "FISH";
            if (!appendf(out, out_cap, &used, "%s%s", (i > 0) ? "," : "", pname)) return 0;
        }
        if (!appendf(out, out_cap, &used, "\n")) return 0;
    } else if (lvl.wave_mode == LEVELDEF_WAVES_CURATED) {
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
    if (!appendf(out, out_cap, &used, "kamikaze.count=%d\n", lvl.kamikaze.count)) return 0;
    if (!appendf(out, out_cap, &used, "kamikaze.start_x01=%.3f\n", lvl.kamikaze.start_x01)) return 0;
    if (!appendf(out, out_cap, &used, "kamikaze.spacing_x=%.3f\n", lvl.kamikaze.spacing_x)) return 0;
    if (!appendf(out, out_cap, &used, "kamikaze.y_margin=%.3f\n", lvl.kamikaze.y_margin)) return 0;
    if (!appendf(out, out_cap, &used, "kamikaze.max_speed=%.3f\n", lvl.kamikaze.max_speed)) return 0;
    if (!appendf(out, out_cap, &used, "kamikaze.accel=%.3f\n", lvl.kamikaze.accel)) return 0;
    if (!appendf(out, out_cap, &used, "kamikaze.radius_min=%.3f\n", lvl.kamikaze.radius_min)) return 0;
    if (!appendf(out, out_cap, &used, "kamikaze.radius_max=%.3f\n", lvl.kamikaze.radius_max)) return 0;

    for (i = 0; i < lvl.searchlight_count; ++i) {
        const leveldef_searchlight* sl = &lvl.searchlights[i];
        if (!appendf(out, out_cap, &used,
            "searchlight=%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%s,%s,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f\n",
            sl->anchor_x01, sl->anchor_y01, sl->length_h01, sl->half_angle_deg, sl->sweep_center_deg,
            sl->sweep_amplitude_deg, sl->sweep_speed, sl->sweep_phase_deg, searchlight_motion_name(sl->sweep_motion),
            searchlight_source_name(sl->source_type), sl->source_radius, sl->clear_grace_s, sl->fire_interval_s,
            sl->projectile_speed, sl->projectile_ttl_s, sl->projectile_radius, sl->aim_jitter_deg)) return 0;
    }

    return 1;
}

static int marker_property_count(const level_editor_state* s) {
    if (!s) {
        return 0;
    }
    if (s->selected_marker < 0 || s->selected_marker >= s->marker_count) {
        return 3; /* WAVE MODE, RENDER STYLE, LENGTH */
    }
    const int kind = s->markers[s->selected_marker].kind;
    if (kind == LEVEL_EDITOR_MARKER_SEARCHLIGHT) {
        return 6;
    }
    if (kind == LEVEL_EDITOR_MARKER_EXIT) {
        return 2;
    }
    if (is_wave_kind(kind)) {
        return 6; /* TYPE, X, Y, A, B, C */
    }
    return 2;
}

static int cycle_wave_kind(int kind, int step) {
    static const int kinds[4] = {
        LEVEL_EDITOR_MARKER_WAVE_SINE,
        LEVEL_EDITOR_MARKER_WAVE_V,
        LEVEL_EDITOR_MARKER_WAVE_KAMIKAZE,
        LEVEL_EDITOR_MARKER_BOID
    };
    int idx = 0;
    for (int i = 0; i < 4; ++i) {
        if (kinds[i] == kind) {
            idx = i;
            break;
        }
    }
    idx = (idx + step + 4) % 4;
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
        default: return "level_unknown";
    }
}

static int level_style_count(void) {
    return LEVEL_STYLE_COUNT;
}

static void clear_markers(level_editor_state* s) {
    if (!s) {
        return;
    }
    s->marker_count = 0;
    s->selected_marker = -1;
}

static void push_marker(level_editor_state* s, int kind, float x01, float y01, float a, float b, float c, float d) {
    if (!s || s->marker_count >= LEVEL_EDITOR_MAX_MARKERS) {
        return;
    }
    level_editor_marker* m = &s->markers[s->marker_count++];
    m->kind = kind;
    m->x01 = clampf(x01, 0.0f, 1.0f);
    m->y01 = clampf(y01, 0.0f, 1.0f);
    m->a = a;
    m->b = b;
    m->c = c;
    m->d = d;
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
    const float top_h = h - m * 2.0f - timeline_h - gap;
    const float side_gap = 10.0f * ui;
    const float props_w = right_total_w * 0.72f;
    const float entities_w = right_total_w - props_w - side_gap;

    out->viewport = (vg_rect){m, m + timeline_h + gap, left_w, top_h};
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
    out->new_button = (vg_rect){
        controls_x,
        m + row_h + 8.0f * ui,
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

static void build_markers(level_editor_state* s, const leveldef_db* db, int style) {
    clear_markers(s);
    if (!s || !db || style < 0 || style >= LEVEL_STYLE_COUNT) {
        return;
    }

    const leveldef_level* lvl = leveldef_get_level(db, style);
    if (!lvl) {
        return;
    }

    if (lvl->exit_enabled) {
        push_marker(s, LEVEL_EDITOR_MARKER_EXIT, lvl->exit_x01, lvl->exit_y01, 0.0f, 0.0f, 0.0f, 0.0f);
    }

    for (int i = 0; i < lvl->searchlight_count; ++i) {
        const leveldef_searchlight* sl = &lvl->searchlights[i];
        push_marker(
            s,
            LEVEL_EDITOR_MARKER_SEARCHLIGHT,
            sl->anchor_x01,
            sl->anchor_y01,
            sl->length_h01,
            sl->half_angle_deg,
            sl->sweep_speed,
            sl->sweep_amplitude_deg
        );
    }

    const int cycle_n = (lvl->wave_mode == LEVELDEF_WAVES_BOID_ONLY) ? lvl->boid_cycle_count : lvl->wave_cycle_count;
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
                ce->x01 / fmaxf(s->level_length_screens, 1.0f),
                ce->y01,
                ce->a,
                ce->b,
                ce->c,
                0.0f
            );
        }
    } else if (lvl->wave_mode == LEVELDEF_WAVES_BOID_ONLY) {
        for (int i = 0; i < lvl->boid_cycle_count; ++i) {
            const int pid = lvl->boid_cycle[i];
            const leveldef_boid_profile* p = leveldef_get_boid_profile(db, pid);
            if (!p) {
                continue;
            }
            const float wave_base = ((float)i / slots) * (s->level_length_screens - 1.0f);
            const float x01 = (wave_base + p->spawn_x01) / s->level_length_screens;
            push_marker(s, LEVEL_EDITOR_MARKER_BOID, x01, p->spawn_y01, p->count, p->max_speed, p->accel, 0.0f);
        }
    } else {
        for (int i = 0; i < lvl->wave_cycle_count; ++i) {
            const int pattern = lvl->wave_cycle[i];
            const float wave_base = ((float)i / slots) * (s->level_length_screens - 1.0f);
            if (pattern == LEVELDEF_WAVE_SINE_SNAKE) {
                const float x01 = (wave_base + lvl->sine.start_x01) / s->level_length_screens;
                push_marker(s, LEVEL_EDITOR_MARKER_WAVE_SINE, x01, lvl->sine.home_y01, lvl->sine.count, lvl->sine.form_amp, lvl->sine.max_speed, 0.0f);
            } else if (pattern == LEVELDEF_WAVE_V_FORMATION) {
                const float x01 = (wave_base + lvl->v.start_x01) / s->level_length_screens;
                push_marker(s, LEVEL_EDITOR_MARKER_WAVE_V, x01, lvl->v.home_y01, lvl->v.count, lvl->v.form_amp, lvl->v.max_speed, 0.0f);
            } else if (pattern == LEVELDEF_WAVE_KAMIKAZE) {
                const float x01 = (wave_base + lvl->kamikaze.start_x01) / s->level_length_screens;
                push_marker(s, LEVEL_EDITOR_MARKER_WAVE_KAMIKAZE, x01, 0.50f, lvl->kamikaze.count, lvl->kamikaze.max_speed, lvl->kamikaze.accel, 0.0f);
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
    s->level_style = LEVEL_STYLE_DEFENDER;
    s->level_render_style = LEVEL_RENDER_DEFENDER;
    s->level_wave_mode = LEVELDEF_WAVES_NORMAL;
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
    s->dirty = 0;
    s->source_path[0] = '\0';
    s->source_text[0] = '\0';
    s->snapshot_valid = 0;
}

int level_editor_load_by_name(level_editor_state* s, const leveldef_db* db, const char* name) {
    if (!s || !db) {
        return 0;
    }
    const int style = level_style_from_name_loose(name ? name : s->level_name);
    if (style < 0 || style >= LEVEL_STYLE_COUNT) {
        snprintf(s->status_text, sizeof(s->status_text), "unknown level name");
        return 0;
    }
    s->level_style = style;
    if (name && name[0] != '\0') {
        snprintf(s->level_name, sizeof(s->level_name), "%s", name);
    } else {
        snprintf(s->level_name, sizeof(s->level_name), "%s", level_style_name(style));
    }
    s->timeline_01 = 0.0f;

    {
        const leveldef_level* lvl = leveldef_get_level(db, style);
        const int cycle_n = lvl ? ((lvl->wave_mode == LEVELDEF_WAVES_BOID_ONLY) ? lvl->boid_cycle_count :
                                   (lvl->wave_mode == LEVELDEF_WAVES_CURATED) ? lvl->curated_count :
                                   lvl->wave_cycle_count) : 0;
        const float cycle_len = fmaxf(8.0f, 6.0f + (float)cycle_n * 1.2f);
        const float data_len = lvl ? fmaxf(1.0f, lvl->exit_x01 + 0.75f) : cycle_len;
        s->level_length_screens = fmaxf(cycle_len, data_len);
        if (lvl) {
            s->level_render_style = lvl->render_style;
            s->level_wave_mode = lvl->wave_mode;
        }
    }
    build_markers(s, db, style);
    s->dirty = 0;
    s->source_path[0] = '\0';
    s->source_text[0] = '\0';
    if (resolve_level_file_path(s->level_name, s->source_path, sizeof(s->source_path))) {
        (void)read_file_text(s->source_path, s->source_text, sizeof(s->source_text));
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
        push_marker(s, LEVEL_EDITOR_MARKER_SEARCHLIGHT, x01, y01, 0.36f, 12.0f, 1.2f, 45.0f);
    } else if (kind == LEVEL_EDITOR_MARKER_BOID) {
        push_marker(s, LEVEL_EDITOR_MARKER_BOID, x01, y01, 12.0f, 190.0f, 90.0f, 0.0f);
    }
    s->selected_marker = s->marker_count - 1;
    s->selected_property = 0;
    s->dirty = 1;
}

static void add_marker_at_timeline(level_editor_state* s, int kind, float x01) {
    if (!s || s->marker_count >= LEVEL_EDITOR_MAX_MARKERS) {
        return;
    }
    const float cx = clampf(x01, 0.0f, 1.0f);
    if (kind == LEVEL_EDITOR_MARKER_BOID) {
        push_marker(s, LEVEL_EDITOR_MARKER_BOID, cx, 0.50f, 12.0f, 190.0f, 90.0f, 0.0f);
    } else if (kind == LEVEL_EDITOR_MARKER_WAVE_SINE) {
        push_marker(s, LEVEL_EDITOR_MARKER_WAVE_SINE, cx, 0.50f, 10.0f, 92.0f, 285.0f, 0.0f);
    } else if (kind == LEVEL_EDITOR_MARKER_WAVE_V) {
        push_marker(s, LEVEL_EDITOR_MARKER_WAVE_V, cx, 0.55f, 11.0f, 10.0f, 295.0f, 0.0f);
    } else if (kind == LEVEL_EDITOR_MARKER_WAVE_KAMIKAZE) {
        push_marker(s, LEVEL_EDITOR_MARKER_WAVE_KAMIKAZE, cx, 0.50f, 9.0f, 360.0f, 9.0f, 0.0f);
    } else {
        return;
    }
    s->selected_marker = s->marker_count - 1;
    s->selected_property = 0;
    s->dirty = 1;
}

int level_editor_handle_mouse(level_editor_state* s, float mouse_x, float mouse_y, float w, float h, int mouse_down, int mouse_pressed) {
    if (!s) {
        return 0;
    }
    level_editor_layout l;
    level_editor_compute_layout(w, h, &l);
    sync_timeline_window(s, &l);
    if (s->entity_drag_active) {
        s->entity_drag_x = mouse_x;
        s->entity_drag_y = mouse_y;
    }

    if (mouse_pressed) {
        if (point_in_rect(mouse_x, mouse_y, l.name_box)) {
            s->entry_active = 1;
            return 1;
        }
        if (point_in_rect(mouse_x, mouse_y, l.load_button)) {
            s->entry_active = 0;
            return 2;
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
        if (point_in_rect(mouse_x, mouse_y, l.swarm_button)) {
            s->entity_tool_selected = LEVEL_EDITOR_MARKER_BOID;
            s->entity_drag_active = 1;
            s->entity_drag_kind = LEVEL_EDITOR_MARKER_BOID;
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
        if (point_in_rect(mouse_x, mouse_y, l.timeline_window) || point_in_rect(mouse_x, mouse_y, l.timeline_track)) {
            s->timeline_drag = 1;
        }
        if (!level_editor_enemy_spatial(s) && point_in_rect(mouse_x, mouse_y, l.timeline_enemy_track)) {
            const float tx01 = clampf((mouse_x - l.timeline_enemy_track.x) / fmaxf(l.timeline_enemy_track.w, 1.0f), 0.0f, 1.0f);
            int best = -1;
            float best_dx = 1.0e9f;
            for (int i = 0; i < s->marker_count; ++i) {
                if (!is_enemy_marker_kind(s->markers[i].kind)) {
                    continue;
                }
                const float dx = fabsf(s->markers[i].x01 - tx01);
                if (dx < best_dx) {
                    best_dx = dx;
                    best = i;
                }
            }
            if (best >= 0 && best_dx < 0.03f) {
                s->selected_marker = best;
                s->selected_property = 0;
                return 1;
            }
            if (s->entity_tool_selected == LEVEL_EDITOR_MARKER_BOID) {
                add_marker_at_timeline(s, LEVEL_EDITOR_MARKER_BOID, tx01);
                return 1;
            }
        }

        if (point_in_rect(mouse_x, mouse_y, l.viewport)) {
            const float level_screens = fmaxf(s->level_length_screens, 1.0f);
            const float start_screen = s->timeline_01 * fmaxf(level_screens - 1.0f, 0.0f);
            const float view_min = start_screen / level_screens;
            const float view_max = (start_screen + 1.0f) / level_screens;
            const float mx01 = (mouse_x - l.viewport.x) / fmaxf(l.viewport.w, 1.0f);
            const float my01 = (mouse_y - l.viewport.y) / fmaxf(l.viewport.h, 1.0f);
            int best = -1;
            float best_d2 = 1.0e9f;
            for (int i = 0; i < s->marker_count; ++i) {
                const level_editor_marker* m = &s->markers[i];
                if (!level_editor_enemy_spatial(s) && is_enemy_marker_kind(m->kind)) {
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
                if (d2 < best_d2) {
                    best_d2 = d2;
                    best = i;
                }
            }
            float pick_d2 = 0.006f;
            if (best >= 0) {
                const float r = marker_pick_radius01(s->markers[best].kind);
                pick_d2 = r * r;
            }
            if (best >= 0 && best_d2 < pick_d2) {
                s->selected_marker = best;
            } else {
                if (!level_editor_enemy_spatial(s) && s->entity_tool_selected == LEVEL_EDITOR_MARKER_BOID) {
                    s->selected_marker = -1;
                } else if (s->entity_tool_selected == LEVEL_EDITOR_MARKER_BOID || s->entity_tool_selected == LEVEL_EDITOR_MARKER_SEARCHLIGHT) {
                    add_marker_at_view(s, s->entity_tool_selected, mx01, my01);
                } else {
                    s->selected_marker = -1;
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
    if (!s->entity_drag_active) {
        return 0;
    }
    level_editor_layout l;
    level_editor_compute_layout(w, h, &l);
    if (point_in_rect(mouse_x, mouse_y, l.viewport)) {
        const float mx01 = (mouse_x - l.viewport.x) / fmaxf(l.viewport.w, 1.0f);
        const float my01 = (mouse_y - l.viewport.y) / fmaxf(l.viewport.h, 1.0f);
        add_marker_at_view(s, s->entity_drag_kind, mx01, my01);
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
    static const char* names[6] = {"X", "Y", "A", "B", "C", "D"};
    if (!s) {
        return "X";
    }
    const int idx = (s->selected_property >= 0 && s->selected_property < 6) ? s->selected_property : 0;
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
                const int n = 5;
                int r = s->level_render_style;
                if (r < 0 || r >= n) {
                    r = LEVEL_RENDER_DEFENDER;
                }
                s->level_render_style = (r + dir + n) % n;
                s->level_style = level_style_from_render_style(s->level_render_style);
            } break;
            case 2:
                s->level_length_screens = clampf(
                    s->level_length_screens + delta * 20.0f,
                    1.0f,
                    400.0f
                );
                break;
            default:
                break;
        }
        s->dirty = 1;
        return;
    }
    level_editor_marker* m = &s->markers[s->selected_marker];

    if (m->kind == LEVEL_EDITOR_MARKER_SEARCHLIGHT) {
        switch (s->selected_property) {
            case 0: move_marker_x(s, s->selected_marker, delta); break;
            case 1: m->y01 = clampf(m->y01 + delta, 0.0f, 1.0f); break;
            case 2: m->a += delta; break;
            case 3: m->b += delta * 20.0f; break;
            case 4: m->c += delta * 5.0f; break;
            case 5: m->d += delta * 20.0f; break;
            default: break;
        }
        s->dirty = 1;
        return;
    }

    if (m->kind == LEVEL_EDITOR_MARKER_EXIT) {
        switch (s->selected_property) {
            case 0: move_marker_x(s, s->selected_marker, delta); break;
            case 1: m->y01 = clampf(m->y01 + delta, 0.0f, 1.0f); break;
            default: break;
        }
        s->dirty = 1;
        return;
    }

    if (is_wave_kind(m->kind)) {
        switch (s->selected_property) {
            case 0:
                m->kind = cycle_wave_kind(m->kind, (delta >= 0.0f) ? 1 : -1);
                break;
            case 1: move_marker_x(s, s->selected_marker, delta); break;
            case 2: m->y01 = clampf(m->y01 + delta, 0.0f, 1.0f); break;
            case 3: m->a += delta * 80.0f; break;
            case 4: m->b += delta * 30.0f; break;
            case 5: m->c += delta * 30.0f; break;
            default: break;
        }
        s->dirty = 1;
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
    (void)db;
    if (!s) {
        return 0;
    }
    if (out_path && out_path_cap > 0) {
        out_path[0] = '\0';
    }

    if (s->source_path[0] == '\0') {
        if (!resolve_level_file_path(s->level_name, s->source_path, sizeof(s->source_path))) {
            snprintf(s->status_text, sizeof(s->status_text), "save failed: level file not found");
            return 0;
        }
    }
    if (s->source_text[0] == '\0') {
        if (!read_file_text(s->source_path, s->source_text, sizeof(s->source_text))) {
            snprintf(s->status_text, sizeof(s->status_text), "save failed: read source");
            return 0;
        }
    }
    if (s->dirty) {
        char serialized[16384];
        if (!build_level_serialized_text(s, db, serialized, sizeof(serialized))) {
            snprintf(s->status_text, sizeof(s->status_text), "save failed: serialize");
            return 0;
        }
        if (!write_file_text(s->source_path, serialized)) {
            snprintf(s->status_text, sizeof(s->status_text), "save failed: write");
            return 0;
        }
        snprintf(s->source_text, sizeof(s->source_text), "%s", serialized);
        s->dirty = 0;
        level_editor_save_snapshot(s);
    } else if (!write_file_text(s->source_path, s->source_text)) {
        snprintf(s->status_text, sizeof(s->status_text), "save failed: write");
        return 0;
    }
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
    if (!build_level_serialized_text(s, db, serialized, sizeof(serialized))) {
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
    s->snapshot_valid = 0;
    s->dirty = 1;
    s->level_style = level_style_from_render_style(s->level_render_style);
    snprintf(s->level_name, sizeof(s->level_name), "untitled");
    snprintf(s->status_text, sizeof(s->status_text), "new level");
}
