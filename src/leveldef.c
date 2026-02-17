#include "leveldef.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static int level_style_from_name(const char* name) {
    if (!name) {
        return -1;
    }
    if (strcmp(name, "DEFENDER") == 0) return LEVEL_STYLE_DEFENDER;
    if (strcmp(name, "ENEMY_RADAR") == 0) return LEVEL_STYLE_ENEMY_RADAR;
    if (strcmp(name, "EVENT_HORIZON") == 0) return LEVEL_STYLE_EVENT_HORIZON;
    if (strcmp(name, "EVENT_HORIZON_LEGACY") == 0) return LEVEL_STYLE_EVENT_HORIZON_LEGACY;
    if (strcmp(name, "HIGH_PLAINS_DRIFTER") == 0) return LEVEL_STYLE_HIGH_PLAINS_DRIFTER;
    if (strcmp(name, "HIGH_PLAINS_DRIFTER_2") == 0) return LEVEL_STYLE_HIGH_PLAINS_DRIFTER_2;
    if (strcmp(name, "FOG_OF_WAR") == 0) return LEVEL_STYLE_FOG_OF_WAR;
    return -1;
}

static int searchlight_motion_from_name(const char* name) {
    if (!name) {
        return -1;
    }
    if (strcmp(name, "linear") == 0) return SEARCHLIGHT_MOTION_LINEAR;
    if (strcmp(name, "spin") == 0) return SEARCHLIGHT_MOTION_SPIN;
    if (strcmp(name, "pendulum") == 0) return SEARCHLIGHT_MOTION_PENDULUM;
    return -1;
}

static int searchlight_source_from_name(const char* name) {
    if (!name) {
        return -1;
    }
    if (strcmp(name, "orb") == 0) return SEARCHLIGHT_SOURCE_ORB;
    if (strcmp(name, "dome") == 0) return SEARCHLIGHT_SOURCE_DOME;
    return -1;
}

static int wave_pattern_from_name(const char* name) {
    if (!name) {
        return -1;
    }
    if (strcmp(name, "sine_snake") == 0) return LEVELDEF_WAVE_SINE_SNAKE;
    if (strcmp(name, "v_formation") == 0) return LEVELDEF_WAVE_V_FORMATION;
    if (strcmp(name, "swarm") == 0) return LEVELDEF_WAVE_SWARM;
    if (strcmp(name, "kamikaze") == 0) return LEVELDEF_WAVE_KAMIKAZE;
    return -1;
}

static int wave_mode_from_name(const char* name) {
    if (!name) {
        return -1;
    }
    if (strcmp(name, "normal") == 0) return LEVELDEF_WAVES_NORMAL;
    if (strcmp(name, "boid_only") == 0) return LEVELDEF_WAVES_BOID_ONLY;
    if (strcmp(name, "curated") == 0) return LEVELDEF_WAVES_CURATED;
    return -1;
}

static int curated_kind_from_name(const char* name) {
    if (!name) {
        return -1;
    }
    if (strcmp(name, "sine") == 0 || strcmp(name, "sine_snake") == 0) return 2;
    if (strcmp(name, "v") == 0 || strcmp(name, "v_formation") == 0) return 3;
    if (strcmp(name, "kamikaze") == 0) return 4;
    if (strcmp(name, "boid") == 0 || strcmp(name, "swarm") == 0) return 5;
    return -1;
}

static int render_style_from_name(const char* name) {
    if (!name) {
        return -1;
    }
    if (strcmp(name, "defender") == 0) return LEVEL_RENDER_DEFENDER;
    if (strcmp(name, "cylinder") == 0) return LEVEL_RENDER_CYLINDER;
    if (strcmp(name, "drifter") == 0) return LEVEL_RENDER_DRIFTER;
    if (strcmp(name, "drifter_shaded") == 0) return LEVEL_RENDER_DRIFTER_SHADED;
    if (strcmp(name, "fog") == 0) return LEVEL_RENDER_FOG;
    return -1;
}

static int spawn_mode_from_name(const char* name) {
    if (!name) {
        return -1;
    }
    if (strcmp(name, "sequenced_clear") == 0) return LEVELDEF_SPAWN_SEQUENCED_CLEAR;
    if (strcmp(name, "timed") == 0) return LEVELDEF_SPAWN_TIMED;
    if (strcmp(name, "timed_sequenced") == 0) return LEVELDEF_SPAWN_TIMED_SEQUENCED;
    return -1;
}

static char* trim(char* s) {
    char* e;
    while (*s && isspace((unsigned char)*s)) {
        s++;
    }
    if (*s == '\0') {
        return s;
    }
    e = s + strlen(s) - 1;
    while (e > s && isspace((unsigned char)*e)) {
        *e-- = '\0';
    }
    return s;
}

static int leveldef_validate(const leveldef_db* db, FILE* log_out);

int leveldef_find_boid_profile(const leveldef_db* db, const char* name) {
    int i;
    if (!db || !name) {
        return -1;
    }
    for (i = 0; i < db->profile_count; ++i) {
        if (strcmp(db->profiles[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

const leveldef_boid_profile* leveldef_get_boid_profile(const leveldef_db* db, int profile_id) {
    if (!db || profile_id < 0 || profile_id >= db->profile_count) {
        return NULL;
    }
    return &db->profiles[profile_id];
}

const leveldef_level* leveldef_get_level(const leveldef_db* db, int level_style) {
    if (!db || level_style < 0 || level_style >= LEVEL_STYLE_COUNT) {
        return NULL;
    }
    return &db->levels[level_style];
}

void leveldef_init_defaults(leveldef_db* db) {
    int i;
    if (!db) {
        return;
    }
    memset(db, 0, sizeof(*db));
    for (i = 0; i < LEVEL_STYLE_COUNT; ++i) {
        db->levels[i].wave_mode = -1;
        db->levels[i].render_style = -1;
        db->levels[i].spawn_mode = -1;
        db->levels[i].default_boid_profile = -1;
    }
}

static int parse_searchlight(leveldef_level* lvl, const char* value, FILE* log_out) {
    char buf[512];
    char* tok;
    char* save = NULL;
    const int expected = 17;
    char* fields[17];
    int i = 0;
    leveldef_searchlight sl;

    if (!lvl || !value || lvl->searchlight_count >= MAX_SEARCHLIGHTS) {
        return 0;
    }
    memset(&sl, 0, sizeof(sl));
    strncpy(buf, value, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    tok = strtok_r(buf, ",", &save);
    while (tok && i < expected) {
        fields[i++] = trim(tok);
        tok = strtok_r(NULL, ",", &save);
    }
    if (i != expected) {
        if (log_out) {
            fprintf(log_out, "leveldef: searchlight expects %d fields, got %d\n", expected, i);
        }
        return 0;
    }

    sl.anchor_x01 = strtof(fields[0], NULL);
    sl.anchor_y01 = strtof(fields[1], NULL);
    sl.length_h01 = strtof(fields[2], NULL);
    sl.half_angle_deg = strtof(fields[3], NULL);
    sl.sweep_center_deg = strtof(fields[4], NULL);
    sl.sweep_amplitude_deg = strtof(fields[5], NULL);
    sl.sweep_speed = strtof(fields[6], NULL);
    sl.sweep_phase_deg = strtof(fields[7], NULL);
    sl.sweep_motion = searchlight_motion_from_name(fields[8]);
    sl.source_type = searchlight_source_from_name(fields[9]);
    sl.source_radius = strtof(fields[10], NULL);
    sl.clear_grace_s = strtof(fields[11], NULL);
    sl.fire_interval_s = strtof(fields[12], NULL);
    sl.projectile_speed = strtof(fields[13], NULL);
    sl.projectile_ttl_s = strtof(fields[14], NULL);
    sl.projectile_radius = strtof(fields[15], NULL);
    sl.aim_jitter_deg = strtof(fields[16], NULL);
    if (sl.sweep_motion < 0 || sl.source_type < 0) {
        if (log_out) {
            fprintf(log_out, "leveldef: invalid searchlight enum token(s)\n");
        }
        return 0;
    }

    lvl->searchlights[lvl->searchlight_count++] = sl;
    return 1;
}

static int parse_curated_enemy(leveldef_level* lvl, const char* value, FILE* log_out) {
    char buf[320];
    char* tok;
    char* save = NULL;
    const int expected = 6;
    char* fields[6];
    int i = 0;
    leveldef_curated_enemy ce;

    if (!lvl || !value || lvl->curated_count >= (int)(sizeof(lvl->curated) / sizeof(lvl->curated[0]))) {
        return 0;
    }
    memset(&ce, 0, sizeof(ce));
    strncpy(buf, value, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    tok = strtok_r(buf, ",", &save);
    while (tok && i < expected) {
        fields[i++] = trim(tok);
        tok = strtok_r(NULL, ",", &save);
    }
    if (i != expected) {
        if (log_out) {
            fprintf(log_out, "leveldef: curated_enemy expects %d fields, got %d\n", expected, i);
        }
        return 0;
    }

    ce.kind = curated_kind_from_name(fields[0]);
    ce.x01 = strtof(fields[1], NULL);
    ce.y01 = strtof(fields[2], NULL);
    ce.a = strtof(fields[3], NULL);
    ce.b = strtof(fields[4], NULL);
    ce.c = strtof(fields[5], NULL);
    if (ce.kind < 0) {
        if (log_out) {
            fprintf(log_out, "leveldef: invalid curated_enemy kind '%s'\n", fields[0]);
        }
        return 0;
    }
    lvl->curated[lvl->curated_count++] = ce;
    return 1;
}

static int leveldef_apply_file(leveldef_db* db, const char* path, FILE* log_out) {
    FILE* f;
    char line[640];
    enum { SEC_NONE = 0, SEC_LEVEL = 1, SEC_PROFILE = 2, SEC_COMBAT = 3 } sec = SEC_NONE;
    leveldef_level* cur_level = NULL;
    leveldef_boid_profile* cur_profile = NULL;
    if (!db) {
        return 0;
    }
    if (!path || path[0] == '\0') {
        return 1;
    }

    f = fopen(path, "r");
    if (!f) {
        if (log_out) {
            fprintf(log_out, "leveldef: could not open %s\n", path);
        }
        return 0;
    }

    while (fgets(line, sizeof(line), f)) {
        char* s = trim(line);
        if (s[0] == '\0' || s[0] == '#') {
            continue;
        }
        if (s[0] == '[') {
            if (strcmp(s, "[combat]") == 0) {
                sec = SEC_COMBAT;
                cur_level = NULL;
                cur_profile = NULL;
                continue;
            }
            char kind[32];
            char name[64];
            kind[0] = '\0';
            name[0] = '\0';
            if (sscanf(s, "[%31s %63[^]]", kind, name) == 2) {
                size_t n = strlen(kind);
                if (n > 0 && kind[n - 1] == ']') {
                    kind[n - 1] = '\0';
                }
                {
                    char* nn = trim(name);
                    int k;
                    for (k = (int)strlen(nn) - 1; k >= 0; --k) {
                        if (!isspace((unsigned char)nn[k])) {
                            break;
                        }
                        nn[k] = '\0';
                    }
                }
                if (strcmp(kind, "level") == 0) {
                    const int sid = level_style_from_name(trim(name));
                    sec = SEC_LEVEL;
                    cur_profile = NULL;
                    cur_level = NULL;
                    if (sid >= 0 && sid < LEVEL_STYLE_COUNT) {
                        cur_level = &db->levels[sid];
                        cur_level->searchlight_count = 0;
                        cur_level->curated_count = 0;
                        cur_level->boid_cycle_count = 0;
                        cur_level->wave_cycle_count = 0;
                    } else if (log_out) {
                        fprintf(log_out, "leveldef: unknown level '%s'\n", name);
                    }
                } else if (strcmp(kind, "boid_profile") == 0) {
                    int pid = leveldef_find_boid_profile(db, trim(name));
                    sec = SEC_PROFILE;
                    cur_level = NULL;
                    if (pid < 0 && db->profile_count < LEVELDEF_MAX_BOID_PROFILES) {
                        pid = db->profile_count++;
                        memset(&db->profiles[pid], 0, sizeof(db->profiles[pid]));
                        strncpy(db->profiles[pid].name, trim(name), sizeof(db->profiles[pid].name) - 1);
                        strncpy(db->profiles[pid].wave_name, trim(name), sizeof(db->profiles[pid].wave_name) - 1);
                    }
                    cur_profile = (pid >= 0 && pid < db->profile_count) ? &db->profiles[pid] : NULL;
                    if (!cur_profile && log_out) {
                        fprintf(log_out, "leveldef: could not allocate boid profile '%s'\n", name);
                    }
                } else {
                    sec = SEC_NONE;
                    cur_level = NULL;
                    cur_profile = NULL;
                }
            }
            continue;
        }

        {
            char* eq = strchr(s, '=');
            if (!eq) {
                continue;
            }
            *eq = '\0';
            {
                char* k = trim(s);
                char* v = trim(eq + 1);
                if (sec == SEC_LEVEL && cur_level) {
                    if (strcmp(k, "render_style") == 0) {
                        cur_level->render_style = render_style_from_name(v);
                    } else if (strcmp(k, "wave_mode") == 0) {
                        cur_level->wave_mode = wave_mode_from_name(v);
                    } else if (strcmp(k, "spawn_mode") == 0) {
                        cur_level->spawn_mode = spawn_mode_from_name(v);
                    } else if (strcmp(k, "spawn_interval_s") == 0) {
                        cur_level->spawn_interval_s = strtof(v, NULL);
                    } else if (strcmp(k, "default_boid_profile") == 0) {
                        cur_level->default_boid_profile = leveldef_find_boid_profile(db, v);
                    } else if (strcmp(k, "wave_cooldown_initial_s") == 0) {
                        cur_level->wave_cooldown_initial_s = strtof(v, NULL);
                    } else if (strcmp(k, "wave_cooldown_between_s") == 0) {
                        cur_level->wave_cooldown_between_s = strtof(v, NULL);
                    } else if (strcmp(k, "exit_enabled") == 0) {
                        cur_level->exit_enabled = atoi(v) ? 1 : 0;
                    } else if (strcmp(k, "exit_x01") == 0) {
                        cur_level->exit_x01 = strtof(v, NULL);
                    } else if (strcmp(k, "exit_y01") == 0) {
                        cur_level->exit_y01 = strtof(v, NULL);
                    } else if (strcmp(k, "boid_cycle") == 0) {
                        char tmp[256];
                        char* save = NULL;
                        char* tok;
                        strncpy(tmp, v, sizeof(tmp) - 1);
                        tmp[sizeof(tmp) - 1] = '\0';
                        tok = strtok_r(tmp, ",", &save);
                        while (tok && cur_level->boid_cycle_count < LEVELDEF_MAX_BOID_CYCLE) {
                            const int pid = leveldef_find_boid_profile(db, trim(tok));
                            if (pid >= 0) {
                                cur_level->boid_cycle[cur_level->boid_cycle_count++] = pid;
                            }
                            tok = strtok_r(NULL, ",", &save);
                        }
                    } else if (strcmp(k, "wave_cycle") == 0) {
                        char tmp[256];
                        char* save = NULL;
                        char* tok;
                        cur_level->wave_cycle_count = 0;
                        strncpy(tmp, v, sizeof(tmp) - 1);
                        tmp[sizeof(tmp) - 1] = '\0';
                        tok = strtok_r(tmp, ",", &save);
                        while (tok && cur_level->wave_cycle_count < LEVELDEF_MAX_BOID_CYCLE) {
                            cur_level->wave_cycle[cur_level->wave_cycle_count++] = wave_pattern_from_name(trim(tok));
                            tok = strtok_r(NULL, ",", &save);
                        }
                    } else if (strcmp(k, "sine.count") == 0) {
                        cur_level->sine.count = atoi(v);
                    } else if (strcmp(k, "sine.start_x01") == 0) {
                        cur_level->sine.start_x01 = strtof(v, NULL);
                    } else if (strcmp(k, "sine.spacing_x") == 0) {
                        cur_level->sine.spacing_x = strtof(v, NULL);
                    } else if (strcmp(k, "sine.home_y01") == 0) {
                        cur_level->sine.home_y01 = strtof(v, NULL);
                    } else if (strcmp(k, "sine.phase_step") == 0) {
                        cur_level->sine.phase_step = strtof(v, NULL);
                    } else if (strcmp(k, "sine.form_amp") == 0) {
                        cur_level->sine.form_amp = strtof(v, NULL);
                    } else if (strcmp(k, "sine.form_freq") == 0) {
                        cur_level->sine.form_freq = strtof(v, NULL);
                    } else if (strcmp(k, "sine.break_delay_base") == 0) {
                        cur_level->sine.break_delay_base = strtof(v, NULL);
                    } else if (strcmp(k, "sine.break_delay_step") == 0) {
                        cur_level->sine.break_delay_step = strtof(v, NULL);
                    } else if (strcmp(k, "sine.max_speed") == 0) {
                        cur_level->sine.max_speed = strtof(v, NULL);
                    } else if (strcmp(k, "sine.accel") == 0) {
                        cur_level->sine.accel = strtof(v, NULL);
                    } else if (strcmp(k, "v.count") == 0) {
                        cur_level->v.count = atoi(v);
                    } else if (strcmp(k, "v.start_x01") == 0) {
                        cur_level->v.start_x01 = strtof(v, NULL);
                    } else if (strcmp(k, "v.spacing_x") == 0) {
                        cur_level->v.spacing_x = strtof(v, NULL);
                    } else if (strcmp(k, "v.home_y01") == 0) {
                        cur_level->v.home_y01 = strtof(v, NULL);
                    } else if (strcmp(k, "v.home_y_step") == 0) {
                        cur_level->v.home_y_step = strtof(v, NULL);
                    } else if (strcmp(k, "v.phase_step") == 0) {
                        cur_level->v.phase_step = strtof(v, NULL);
                    } else if (strcmp(k, "v.form_amp") == 0) {
                        cur_level->v.form_amp = strtof(v, NULL);
                    } else if (strcmp(k, "v.form_freq") == 0) {
                        cur_level->v.form_freq = strtof(v, NULL);
                    } else if (strcmp(k, "v.break_delay_min") == 0) {
                        cur_level->v.break_delay_min = strtof(v, NULL);
                    } else if (strcmp(k, "v.break_delay_rand") == 0) {
                        cur_level->v.break_delay_rand = strtof(v, NULL);
                    } else if (strcmp(k, "v.max_speed") == 0) {
                        cur_level->v.max_speed = strtof(v, NULL);
                    } else if (strcmp(k, "v.accel") == 0) {
                        cur_level->v.accel = strtof(v, NULL);
                    } else if (strcmp(k, "kamikaze.count") == 0) {
                        cur_level->kamikaze.count = atoi(v);
                    } else if (strcmp(k, "kamikaze.start_x01") == 0) {
                        cur_level->kamikaze.start_x01 = strtof(v, NULL);
                    } else if (strcmp(k, "kamikaze.spacing_x") == 0) {
                        cur_level->kamikaze.spacing_x = strtof(v, NULL);
                    } else if (strcmp(k, "kamikaze.y_margin") == 0) {
                        cur_level->kamikaze.y_margin = strtof(v, NULL);
                    } else if (strcmp(k, "kamikaze.max_speed") == 0) {
                        cur_level->kamikaze.max_speed = strtof(v, NULL);
                    } else if (strcmp(k, "kamikaze.accel") == 0) {
                        cur_level->kamikaze.accel = strtof(v, NULL);
                    } else if (strcmp(k, "kamikaze.radius_min") == 0) {
                        cur_level->kamikaze.radius_min = strtof(v, NULL);
                    } else if (strcmp(k, "kamikaze.radius_max") == 0) {
                        cur_level->kamikaze.radius_max = strtof(v, NULL);
                    } else if (strcmp(k, "searchlight") == 0) {
                        (void)parse_searchlight(cur_level, v, log_out);
                    } else if (strcmp(k, "curated_enemy") == 0) {
                        (void)parse_curated_enemy(cur_level, v, log_out);
                    }
                } else if (sec == SEC_PROFILE && cur_profile) {
                    if (strcmp(k, "wave_name") == 0) {
                        strncpy(cur_profile->wave_name, v, sizeof(cur_profile->wave_name) - 1);
                    } else if (strcmp(k, "count") == 0) {
                        cur_profile->count = atoi(v);
                    } else if (strcmp(k, "sep_w") == 0) {
                        cur_profile->sep_w = strtof(v, NULL);
                    } else if (strcmp(k, "ali_w") == 0) {
                        cur_profile->ali_w = strtof(v, NULL);
                    } else if (strcmp(k, "coh_w") == 0) {
                        cur_profile->coh_w = strtof(v, NULL);
                    } else if (strcmp(k, "avoid_w") == 0) {
                        cur_profile->avoid_w = strtof(v, NULL);
                    } else if (strcmp(k, "goal_w") == 0) {
                        cur_profile->goal_w = strtof(v, NULL);
                    } else if (strcmp(k, "sep_r") == 0) {
                        cur_profile->sep_r = strtof(v, NULL);
                    } else if (strcmp(k, "ali_r") == 0) {
                        cur_profile->ali_r = strtof(v, NULL);
                    } else if (strcmp(k, "coh_r") == 0) {
                        cur_profile->coh_r = strtof(v, NULL);
                    } else if (strcmp(k, "goal_amp") == 0) {
                        cur_profile->goal_amp = strtof(v, NULL);
                    } else if (strcmp(k, "goal_freq") == 0) {
                        cur_profile->goal_freq = strtof(v, NULL);
                    } else if (strcmp(k, "wander_w") == 0) {
                        cur_profile->wander_w = strtof(v, NULL);
                    } else if (strcmp(k, "wander_freq") == 0) {
                        cur_profile->wander_freq = strtof(v, NULL);
                    } else if (strcmp(k, "steer_drag") == 0) {
                        cur_profile->steer_drag = strtof(v, NULL);
                    } else if (strcmp(k, "max_speed") == 0) {
                        cur_profile->max_speed = strtof(v, NULL);
                    } else if (strcmp(k, "accel") == 0) {
                        cur_profile->accel = strtof(v, NULL);
                    } else if (strcmp(k, "radius_min") == 0) {
                        cur_profile->radius_min = strtof(v, NULL);
                    } else if (strcmp(k, "radius_max") == 0) {
                        cur_profile->radius_max = strtof(v, NULL);
                    } else if (strcmp(k, "spawn_x01") == 0) {
                        cur_profile->spawn_x01 = strtof(v, NULL);
                    } else if (strcmp(k, "spawn_x_span") == 0) {
                        cur_profile->spawn_x_span = strtof(v, NULL);
                    } else if (strcmp(k, "spawn_y01") == 0) {
                        cur_profile->spawn_y01 = strtof(v, NULL);
                    } else if (strcmp(k, "spawn_y_span") == 0) {
                        cur_profile->spawn_y_span = strtof(v, NULL);
                    }
                } else if (sec == SEC_COMBAT) {
                    if (strcmp(k, "swarm_armed_prob_start") == 0) {
                        db->combat.swarm_armed_prob_start = strtof(v, NULL);
                    } else if (strcmp(k, "swarm_armed_prob_end") == 0) {
                        db->combat.swarm_armed_prob_end = strtof(v, NULL);
                    } else if (strcmp(k, "swarm_spread_prob_start") == 0) {
                        db->combat.swarm_spread_prob_start = strtof(v, NULL);
                    } else if (strcmp(k, "swarm_spread_prob_end") == 0) {
                        db->combat.swarm_spread_prob_end = strtof(v, NULL);
                    } else if (strcmp(k, "weapon.pulse.cooldown_min_s") == 0) {
                        db->combat.weapon[0].cooldown_min_s = strtof(v, NULL);
                    } else if (strcmp(k, "weapon.pulse.cooldown_max_s") == 0) {
                        db->combat.weapon[0].cooldown_max_s = strtof(v, NULL);
                    } else if (strcmp(k, "weapon.pulse.burst_count") == 0) {
                        db->combat.weapon[0].burst_count = atoi(v);
                    } else if (strcmp(k, "weapon.pulse.burst_gap_s") == 0) {
                        db->combat.weapon[0].burst_gap_s = strtof(v, NULL);
                    } else if (strcmp(k, "weapon.pulse.projectiles_per_shot") == 0) {
                        db->combat.weapon[0].projectiles_per_shot = atoi(v);
                    } else if (strcmp(k, "weapon.pulse.spread_deg") == 0) {
                        db->combat.weapon[0].spread_deg = strtof(v, NULL);
                    } else if (strcmp(k, "weapon.pulse.projectile_speed") == 0) {
                        db->combat.weapon[0].projectile_speed = strtof(v, NULL);
                    } else if (strcmp(k, "weapon.pulse.projectile_ttl_s") == 0) {
                        db->combat.weapon[0].projectile_ttl_s = strtof(v, NULL);
                    } else if (strcmp(k, "weapon.pulse.projectile_radius") == 0) {
                        db->combat.weapon[0].projectile_radius = strtof(v, NULL);
                    } else if (strcmp(k, "weapon.pulse.aim_lead_s") == 0) {
                        db->combat.weapon[0].aim_lead_s = strtof(v, NULL);
                    } else if (strcmp(k, "weapon.spread.cooldown_min_s") == 0) {
                        db->combat.weapon[1].cooldown_min_s = strtof(v, NULL);
                    } else if (strcmp(k, "weapon.spread.cooldown_max_s") == 0) {
                        db->combat.weapon[1].cooldown_max_s = strtof(v, NULL);
                    } else if (strcmp(k, "weapon.spread.burst_count") == 0) {
                        db->combat.weapon[1].burst_count = atoi(v);
                    } else if (strcmp(k, "weapon.spread.burst_gap_s") == 0) {
                        db->combat.weapon[1].burst_gap_s = strtof(v, NULL);
                    } else if (strcmp(k, "weapon.spread.projectiles_per_shot") == 0) {
                        db->combat.weapon[1].projectiles_per_shot = atoi(v);
                    } else if (strcmp(k, "weapon.spread.spread_deg") == 0) {
                        db->combat.weapon[1].spread_deg = strtof(v, NULL);
                    } else if (strcmp(k, "weapon.spread.projectile_speed") == 0) {
                        db->combat.weapon[1].projectile_speed = strtof(v, NULL);
                    } else if (strcmp(k, "weapon.spread.projectile_ttl_s") == 0) {
                        db->combat.weapon[1].projectile_ttl_s = strtof(v, NULL);
                    } else if (strcmp(k, "weapon.spread.projectile_radius") == 0) {
                        db->combat.weapon[1].projectile_radius = strtof(v, NULL);
                    } else if (strcmp(k, "weapon.spread.aim_lead_s") == 0) {
                        db->combat.weapon[1].aim_lead_s = strtof(v, NULL);
                    } else if (strcmp(k, "weapon.burst.cooldown_min_s") == 0) {
                        db->combat.weapon[2].cooldown_min_s = strtof(v, NULL);
                    } else if (strcmp(k, "weapon.burst.cooldown_max_s") == 0) {
                        db->combat.weapon[2].cooldown_max_s = strtof(v, NULL);
                    } else if (strcmp(k, "weapon.burst.burst_count") == 0) {
                        db->combat.weapon[2].burst_count = atoi(v);
                    } else if (strcmp(k, "weapon.burst.burst_gap_s") == 0) {
                        db->combat.weapon[2].burst_gap_s = strtof(v, NULL);
                    } else if (strcmp(k, "weapon.burst.projectiles_per_shot") == 0) {
                        db->combat.weapon[2].projectiles_per_shot = atoi(v);
                    } else if (strcmp(k, "weapon.burst.spread_deg") == 0) {
                        db->combat.weapon[2].spread_deg = strtof(v, NULL);
                    } else if (strcmp(k, "weapon.burst.projectile_speed") == 0) {
                        db->combat.weapon[2].projectile_speed = strtof(v, NULL);
                    } else if (strcmp(k, "weapon.burst.projectile_ttl_s") == 0) {
                        db->combat.weapon[2].projectile_ttl_s = strtof(v, NULL);
                    } else if (strcmp(k, "weapon.burst.projectile_radius") == 0) {
                        db->combat.weapon[2].projectile_radius = strtof(v, NULL);
                    } else if (strcmp(k, "weapon.burst.aim_lead_s") == 0) {
                        db->combat.weapon[2].aim_lead_s = strtof(v, NULL);
                    } else if (strcmp(k, "progression_wave_weight") == 0) {
                        db->combat.progression_wave_weight = strtof(v, NULL);
                    } else if (strcmp(k, "progression_score_weight") == 0) {
                        db->combat.progression_score_weight = strtof(v, NULL);
                    } else if (strcmp(k, "progression_level_weight") == 0) {
                        db->combat.progression_level_weight = strtof(v, NULL);
                    } else if (strcmp(k, "armed_probability_base_formation") == 0) {
                        db->combat.armed_probability_base[0] = strtof(v, NULL);
                    } else if (strcmp(k, "armed_probability_base_swarm") == 0) {
                        db->combat.armed_probability_base[1] = strtof(v, NULL);
                    } else if (strcmp(k, "armed_probability_base_kamikaze") == 0) {
                        db->combat.armed_probability_base[2] = strtof(v, NULL);
                    } else if (strcmp(k, "armed_probability_progression_bonus_formation") == 0) {
                        db->combat.armed_probability_progression_bonus[0] = strtof(v, NULL);
                    } else if (strcmp(k, "armed_probability_progression_bonus_swarm") == 0) {
                        db->combat.armed_probability_progression_bonus[1] = strtof(v, NULL);
                    } else if (strcmp(k, "armed_probability_progression_bonus_kamikaze") == 0) {
                        db->combat.armed_probability_progression_bonus[2] = strtof(v, NULL);
                    } else if (strcmp(k, "fire_range_min") == 0) {
                        db->combat.fire_range_min = strtof(v, NULL);
                    } else if (strcmp(k, "fire_range_max_base") == 0) {
                        db->combat.fire_range_max_base = strtof(v, NULL);
                    } else if (strcmp(k, "fire_range_max_progression_bonus") == 0) {
                        db->combat.fire_range_max_progression_bonus = strtof(v, NULL);
                    } else if (strcmp(k, "aim_error_deg_start") == 0) {
                        db->combat.aim_error_deg_start = strtof(v, NULL);
                    } else if (strcmp(k, "aim_error_deg_end") == 0) {
                        db->combat.aim_error_deg_end = strtof(v, NULL);
                    } else if (strcmp(k, "cooldown_scale_start") == 0) {
                        db->combat.cooldown_scale_start = strtof(v, NULL);
                    } else if (strcmp(k, "cooldown_scale_end") == 0) {
                        db->combat.cooldown_scale_end = strtof(v, NULL);
                    } else if (strcmp(k, "projectile_speed_scale_start") == 0) {
                        db->combat.projectile_speed_scale_start = strtof(v, NULL);
                    } else if (strcmp(k, "projectile_speed_scale_end") == 0) {
                        db->combat.projectile_speed_scale_end = strtof(v, NULL);
                    } else if (strcmp(k, "spread_scale_start") == 0) {
                        db->combat.spread_scale_start = strtof(v, NULL);
                    } else if (strcmp(k, "spread_scale_end") == 0) {
                        db->combat.spread_scale_end = strtof(v, NULL);
                    }
                }
            }
        }
    }

    fclose(f);
    return 1;
}

int leveldef_load_with_defaults(leveldef_db* db, const char* path, FILE* log_out) {
    leveldef_init_defaults(db);
    if (!leveldef_apply_file(db, path, log_out)) {
        return 0;
    }
    return leveldef_validate(db, log_out);
}

static int leveldef_validate(const leveldef_db* db, FILE* log_out) {
    int ok = 1;
    int i;
    if (!db) {
        return 0;
    }
    if (db->profile_count <= 0) {
        if (log_out) {
            fprintf(log_out, "leveldef: no boid profiles loaded\n");
        }
        return 0;
    }
    for (i = 0; i < LEVEL_STYLE_COUNT; ++i) {
        const leveldef_level* l = &db->levels[i];
        if (l->render_style < 0) {
            if (log_out) {
                fprintf(log_out, "leveldef: level %d missing render_style\n", i);
            }
            ok = 0;
        }
        if (l->wave_mode < 0) {
            if (log_out) {
                fprintf(log_out, "leveldef: level %d missing wave_mode\n", i);
            }
            ok = 0;
        }
        if (l->spawn_mode < 0) {
            if (log_out) {
                fprintf(log_out, "leveldef: level %d missing spawn_mode\n", i);
            }
            ok = 0;
        }
        if ((l->spawn_mode == LEVELDEF_SPAWN_TIMED ||
             l->spawn_mode == LEVELDEF_SPAWN_TIMED_SEQUENCED) &&
            l->spawn_interval_s <= 0.0f) {
            if (log_out) {
                fprintf(log_out, "leveldef: level %d invalid spawn_interval_s\n", i);
            }
            ok = 0;
        }
        if (l->wave_cooldown_initial_s <= 0.0f || l->wave_cooldown_between_s <= 0.0f) {
            if (log_out) {
                fprintf(log_out, "leveldef: level %d invalid wave cooldowns\n", i);
            }
            ok = 0;
        }
        if (l->default_boid_profile < 0 || l->default_boid_profile >= db->profile_count) {
            if (log_out) {
                fprintf(log_out, "leveldef: level %d invalid default_boid_profile\n", i);
            }
            ok = 0;
        }
        if (l->wave_mode == LEVELDEF_WAVES_BOID_ONLY) {
            if (l->boid_cycle_count <= 0) {
                if (log_out) {
                    fprintf(log_out, "leveldef: level %d boid_only missing boid_cycle\n", i);
                }
                ok = 0;
            }
        } else if (l->wave_mode == LEVELDEF_WAVES_CURATED) {
            if (l->curated_count <= 0) {
                if (log_out) {
                    fprintf(log_out, "leveldef: level %d curated mode missing curated_enemy entries\n", i);
                }
                ok = 0;
            }
        } else if (l->wave_cycle_count <= 0) {
            if (log_out) {
                fprintf(log_out, "leveldef: level %d normal mode missing wave_cycle\n", i);
            }
            ok = 0;
        } else {
            int j;
            for (j = 0; j < l->wave_cycle_count; ++j) {
                if (l->wave_cycle[j] < 0) {
                    if (log_out) {
                        fprintf(log_out, "leveldef: level %d has invalid wave_cycle token\n", i);
                    }
                    ok = 0;
                    break;
                }
            }
        }
    }
    return ok;
}

int leveldef_load_project_layout(leveldef_db* db, const char* dir_path, FILE* log_out) {
    static const char* files[] = {
        "combat.cfg",
        "boids.cfg",
        "level_defender.cfg",
        "level_enemy_radar.cfg",
        "level_event_horizon.cfg",
        "level_event_horizon_legacy.cfg",
        "level_high_plains_drifter.cfg",
        "level_high_plains_drifter_2.cfg",
        "level_fog_of_war.cfg"
    };
    char path[512];
    int i;
    int ok = 1;

    if (!db) {
        return 0;
    }
    leveldef_init_defaults(db);
    if (!dir_path || dir_path[0] == '\0') {
        return 1;
    }

    for (i = 0; i < (int)(sizeof(files) / sizeof(files[0])); ++i) {
        if (snprintf(path, sizeof(path), "%s/%s", dir_path, files[i]) >= (int)sizeof(path)) {
            if (log_out) {
                fprintf(log_out, "leveldef: path too long for %s/%s\n", dir_path, files[i]);
            }
            ok = 0;
            continue;
        }
        if (!leveldef_apply_file(db, path, log_out)) {
            ok = 0;
        }
    }
    if (ok) {
        ok = leveldef_validate(db, log_out);
    }
    return ok;
}

int leveldef_load_level_file_with_base(
    const leveldef_db* base_db,
    const char* level_path,
    leveldef_level* out_level,
    int* out_style,
    FILE* log_out
) {
    FILE* f = NULL;
    char line[320];
    int style = -1;
    leveldef_db tmp;

    if (!base_db || !level_path || !out_level) {
        return 0;
    }
    f = fopen(level_path, "r");
    if (!f) {
        return 0;
    }
    while (fgets(line, sizeof(line), f)) {
        char* s = trim(line);
        char kind[32];
        char name[64];
        if (s[0] == '\0' || s[0] == '#') {
            continue;
        }
        kind[0] = '\0';
        name[0] = '\0';
        if (sscanf(s, "[%31s %63[^]]", kind, name) == 2) {
            size_t n = strlen(kind);
            if (n > 0 && kind[n - 1] == ']') {
                kind[n - 1] = '\0';
            }
            if (strcmp(kind, "level") == 0) {
                style = level_style_from_name(trim(name));
                break;
            }
        }
    }
    fclose(f);
    if (style < 0 || style >= LEVEL_STYLE_COUNT) {
        if (log_out) {
            fprintf(log_out, "leveldef: %s has unknown [level ...] header\n", level_path);
        }
        return 0;
    }

    tmp = *base_db;
    if (!leveldef_apply_file(&tmp, level_path, log_out)) {
        return 0;
    }
    *out_level = tmp.levels[style];
    if (out_style) {
        *out_style = style;
    }
    return 1;
}
