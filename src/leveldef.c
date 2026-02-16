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
        return SEARCHLIGHT_MOTION_PENDULUM;
    }
    if (strcmp(name, "linear") == 0) return SEARCHLIGHT_MOTION_LINEAR;
    if (strcmp(name, "spin") == 0) return SEARCHLIGHT_MOTION_SPIN;
    return SEARCHLIGHT_MOTION_PENDULUM;
}

static int searchlight_source_from_name(const char* name) {
    if (!name) {
        return SEARCHLIGHT_SOURCE_DOME;
    }
    if (strcmp(name, "orb") == 0) return SEARCHLIGHT_SOURCE_ORB;
    return SEARCHLIGHT_SOURCE_DOME;
}

static int wave_pattern_from_name(const char* name) {
    if (!name) {
        return LEVELDEF_WAVE_SINE_SNAKE;
    }
    if (strcmp(name, "sine_snake") == 0) return LEVELDEF_WAVE_SINE_SNAKE;
    if (strcmp(name, "v_formation") == 0) return LEVELDEF_WAVE_V_FORMATION;
    if (strcmp(name, "swarm") == 0) return LEVELDEF_WAVE_SWARM;
    if (strcmp(name, "kamikaze") == 0) return LEVELDEF_WAVE_KAMIKAZE;
    return LEVELDEF_WAVE_SINE_SNAKE;
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

static int add_default_profile(leveldef_db* db, const leveldef_boid_profile* p) {
    if (!db || !p || db->profile_count >= LEVELDEF_MAX_BOID_PROFILES) {
        return -1;
    }
    db->profiles[db->profile_count] = *p;
    db->profile_count += 1;
    return db->profile_count - 1;
}

void leveldef_init_defaults(leveldef_db* db) {
    int i;
    if (!db) {
        return;
    }
    memset(db, 0, sizeof(*db));
    db->combat.weapon[0].cooldown_min_s = 1.10f;
    db->combat.weapon[0].cooldown_max_s = 1.90f;
    db->combat.weapon[0].burst_count = 1;
    db->combat.weapon[0].burst_gap_s = 0.0f;
    db->combat.weapon[0].projectiles_per_shot = 1;
    db->combat.weapon[0].spread_deg = 0.0f;
    db->combat.weapon[0].projectile_speed = 500.0f;
    db->combat.weapon[0].projectile_ttl_s = 2.35f;
    db->combat.weapon[0].projectile_radius = 4.0f;
    db->combat.weapon[0].aim_lead_s = 0.20f;
    db->combat.weapon[1].cooldown_min_s = 1.30f;
    db->combat.weapon[1].cooldown_max_s = 2.20f;
    db->combat.weapon[1].burst_count = 1;
    db->combat.weapon[1].burst_gap_s = 0.0f;
    db->combat.weapon[1].projectiles_per_shot = 3;
    db->combat.weapon[1].spread_deg = 11.0f;
    db->combat.weapon[1].projectile_speed = 440.0f;
    db->combat.weapon[1].projectile_ttl_s = 2.15f;
    db->combat.weapon[1].projectile_radius = 3.6f;
    db->combat.weapon[1].aim_lead_s = 0.17f;
    db->combat.weapon[2].cooldown_min_s = 1.90f;
    db->combat.weapon[2].cooldown_max_s = 2.70f;
    db->combat.weapon[2].burst_count = 3;
    db->combat.weapon[2].burst_gap_s = 0.085f;
    db->combat.weapon[2].projectiles_per_shot = 1;
    db->combat.weapon[2].spread_deg = 0.0f;
    db->combat.weapon[2].projectile_speed = 560.0f;
    db->combat.weapon[2].projectile_ttl_s = 2.00f;
    db->combat.weapon[2].projectile_radius = 3.4f;
    db->combat.weapon[2].aim_lead_s = 0.14f;
    db->combat.progression_wave_weight = 0.045f;
    db->combat.progression_score_weight = (1.0f / 22000.0f);
    db->combat.progression_level_weight = 0.0f;
    db->combat.armed_probability_base[0] = 0.32f;
    db->combat.armed_probability_base[1] = 0.48f;
    db->combat.armed_probability_base[2] = 0.24f;
    db->combat.armed_probability_progression_bonus[0] = 0.40f;
    db->combat.armed_probability_progression_bonus[1] = 0.35f;
    db->combat.armed_probability_progression_bonus[2] = 0.45f;
    db->combat.fire_range_min = 110.0f;
    db->combat.fire_range_max_base = 560.0f;
    db->combat.fire_range_max_progression_bonus = 180.0f;
    db->combat.aim_error_deg_start = 8.0f;
    db->combat.aim_error_deg_end = 2.2f;
    db->combat.cooldown_scale_start = 1.0f;
    db->combat.cooldown_scale_end = 0.62f;
    db->combat.projectile_speed_scale_start = 1.0f;
    db->combat.projectile_speed_scale_end = 1.28f;
    db->combat.spread_scale_start = 1.0f;
    db->combat.spread_scale_end = 0.70f;
    db->combat.swarm_armed_prob_start = 0.16f;
    db->combat.swarm_armed_prob_end = 0.72f;
    db->combat.swarm_spread_prob_start = 0.12f;
    db->combat.swarm_spread_prob_end = 0.86f;
    for (i = 0; i < LEVEL_STYLE_COUNT; ++i) {
        db->levels[i].wave_mode = LEVELDEF_WAVES_NORMAL;
        db->levels[i].default_boid_profile = -1;
        db->levels[i].wave_cooldown_initial_s = 0.65f;
        db->levels[i].wave_cooldown_between_s = 2.0f;
        db->levels[i].wave_cycle_count = 4;
        db->levels[i].wave_cycle[0] = LEVELDEF_WAVE_SINE_SNAKE;
        db->levels[i].wave_cycle[1] = LEVELDEF_WAVE_V_FORMATION;
        db->levels[i].wave_cycle[2] = LEVELDEF_WAVE_SWARM;
        db->levels[i].wave_cycle[3] = LEVELDEF_WAVE_KAMIKAZE;
        db->levels[i].sine = (leveldef_wave_sine_tuning){
            .count = 10,
            .start_x01 = 0.70f,
            .spacing_x = 44.0f,
            .home_y01 = 0.52f,
            .phase_step = 0.55f,
            .form_amp = 92.0f,
            .form_freq = 1.8f,
            .break_delay_base = 1.1f,
            .break_delay_step = 0.16f,
            .max_speed = 285.0f,
            .accel = 6.8f
        };
        db->levels[i].v = (leveldef_wave_v_tuning){
            .count = 11,
            .start_x01 = 0.74f,
            .spacing_x = 32.0f,
            .home_y01 = 0.55f,
            .home_y_step = 18.0f,
            .phase_step = 0.35f,
            .form_amp = 10.0f,
            .form_freq = 1.2f,
            .break_delay_min = 0.9f,
            .break_delay_rand = 1.8f,
            .max_speed = 295.0f,
            .accel = 7.5f
        };
        db->levels[i].kamikaze = (leveldef_wave_kamikaze_tuning){
            .count = 9,
            .start_x01 = 0.65f,
            .spacing_x = 34.0f,
            .y_margin = 64.0f,
            .max_speed = 360.0f,
            .accel = 9.0f,
            .radius_min = 11.0f,
            .radius_max = 17.0f
        };
    }

    {
        const leveldef_boid_profile firefly = {
            .name = "FIREFLY",
            .wave_name = "boid swarm: firefly scatter",
            .count = 18,
            .sep_w = 2.40f,
            .ali_w = 0.25f,
            .coh_w = 0.20f,
            .avoid_w = 2.90f,
            .goal_w = 0.55f,
            .sep_r = 84.0f,
            .ali_r = 168.0f,
            .coh_r = 205.0f,
            .goal_amp = 140.0f,
            .goal_freq = 1.40f,
            .wander_w = 1.30f,
            .wander_freq = 2.10f,
            .steer_drag = 1.55f,
            .max_speed = 210.0f,
            .accel = 6.2f,
            .radius_min = 8.0f,
            .radius_max = 12.0f,
            .spawn_x01 = 0.62f,
            .spawn_x_span = 260.0f,
            .spawn_y01 = 0.50f,
            .spawn_y_span = 140.0f
        };
        const leveldef_boid_profile fish = {
            .name = "FISH",
            .wave_name = "boid swarm: fish school",
            .count = 16,
            .sep_w = 1.60f,
            .ali_w = 1.10f,
            .coh_w = 1.00f,
            .avoid_w = 2.40f,
            .goal_w = 1.00f,
            .sep_r = 104.0f,
            .ali_r = 290.0f,
            .coh_r = 345.0f,
            .goal_amp = 52.0f,
            .goal_freq = 0.45f,
            .wander_w = 0.22f,
            .wander_freq = 0.80f,
            .steer_drag = 1.25f,
            .max_speed = 245.0f,
            .accel = 7.8f,
            .radius_min = 10.0f,
            .radius_max = 14.0f,
            .spawn_x01 = 0.62f,
            .spawn_x_span = 260.0f,
            .spawn_y01 = 0.50f,
            .spawn_y_span = 140.0f
        };
        const leveldef_boid_profile bird = {
            .name = "BIRD",
            .wave_name = "boid swarm: bird flock",
            .count = 12,
            .sep_w = 1.40f,
            .ali_w = 1.55f,
            .coh_w = 0.85f,
            .avoid_w = 2.20f,
            .goal_w = 1.35f,
            .sep_r = 126.0f,
            .ali_r = 336.0f,
            .coh_r = 392.0f,
            .goal_amp = 34.0f,
            .goal_freq = 0.28f,
            .wander_w = 0.08f,
            .wander_freq = 0.45f,
            .steer_drag = 1.08f,
            .max_speed = 300.0f,
            .accel = 9.0f,
            .radius_min = 11.0f,
            .radius_max = 16.0f,
            .spawn_x01 = 0.62f,
            .spawn_x_span = 260.0f,
            .spawn_y01 = 0.50f,
            .spawn_y_span = 140.0f
        };
        const int id_firefly = add_default_profile(db, &firefly);
        const int id_fish = add_default_profile(db, &fish);
        const int id_bird = add_default_profile(db, &bird);
        for (i = 0; i < LEVEL_STYLE_COUNT; ++i) {
            db->levels[i].default_boid_profile = id_fish;
        }
        db->levels[LEVEL_STYLE_FOG_OF_WAR].wave_mode = LEVELDEF_WAVES_BOID_ONLY;
        db->levels[LEVEL_STYLE_FOG_OF_WAR].boid_cycle_count = 3;
        db->levels[LEVEL_STYLE_FOG_OF_WAR].boid_cycle[0] = id_firefly;
        db->levels[LEVEL_STYLE_FOG_OF_WAR].boid_cycle[1] = id_fish;
        db->levels[LEVEL_STYLE_FOG_OF_WAR].boid_cycle[2] = id_bird;
    }

    {
        leveldef_level* defender = &db->levels[LEVEL_STYLE_DEFENDER];
        defender->searchlight_count = 2;
        defender->searchlights[0] = (leveldef_searchlight){
            .anchor_x01 = 0.82f,
            .anchor_y01 = 0.02f,
            .length_h01 = 0.72f,
            .half_angle_deg = 10.3f,
            .sweep_center_deg = 90.0f,
            .sweep_amplitude_deg = 70.0f,
            .sweep_speed = 1.25f,
            .sweep_phase_deg = 17.2f,
            .sweep_motion = SEARCHLIGHT_MOTION_PENDULUM,
            .source_type = SEARCHLIGHT_SOURCE_DOME,
            .source_radius = 16.0f,
            .clear_grace_s = 2.60f,
            .fire_interval_s = 0.058f,
            .projectile_speed = 820.0f,
            .projectile_ttl_s = 2.5f,
            .projectile_radius = 3.6f,
            .aim_jitter_deg = 1.43f
        };
        defender->searchlights[1] = (leveldef_searchlight){
            .anchor_x01 = 1.30f,
            .anchor_y01 = 0.50f,
            .length_h01 = 0.68f,
            .half_angle_deg = 9.0f,
            .sweep_center_deg = 0.0f,
            .sweep_amplitude_deg = 180.0f,
            .sweep_speed = 3.20f,
            .sweep_phase_deg = 0.0f,
            .sweep_motion = SEARCHLIGHT_MOTION_SPIN,
            .source_type = SEARCHLIGHT_SOURCE_ORB,
            .source_radius = 15.0f,
            .clear_grace_s = 2.10f,
            .fire_interval_s = 0.16f,
            .projectile_speed = 1220.0f,
            .projectile_ttl_s = 2.3f,
            .projectile_radius = 3.4f,
            .aim_jitter_deg = 1.10f
        };
    }

    {
        leveldef_level* fog = &db->levels[LEVEL_STYLE_FOG_OF_WAR];
        fog->searchlight_count = 3;
        fog->searchlights[0] = (leveldef_searchlight){
            .anchor_x01 = 3.30f,
            .anchor_y01 = 0.50f,
            .length_h01 = 0.74f,
            .half_angle_deg = 8.2f,
            .sweep_center_deg = 0.0f,
            .sweep_amplitude_deg = 180.0f,
            .sweep_speed = 1.70f,
            .sweep_phase_deg = 0.0f,
            .sweep_motion = SEARCHLIGHT_MOTION_SPIN,
            .source_type = SEARCHLIGHT_SOURCE_ORB,
            .source_radius = 14.5f,
            .clear_grace_s = 1.60f,
            .fire_interval_s = 0.030f,
            .projectile_speed = 980.0f,
            .projectile_ttl_s = 2.2f,
            .projectile_radius = 3.2f,
            .aim_jitter_deg = 1.35f
        };
        fog->searchlights[1] = (leveldef_searchlight){
            .anchor_x01 = 3.80f,
            .anchor_y01 = 0.50f,
            .length_h01 = 0.72f,
            .half_angle_deg = 8.2f,
            .sweep_center_deg = 0.0f,
            .sweep_amplitude_deg = 180.0f,
            .sweep_speed = 1.55f,
            .sweep_phase_deg = 120.0f,
            .sweep_motion = SEARCHLIGHT_MOTION_SPIN,
            .source_type = SEARCHLIGHT_SOURCE_ORB,
            .source_radius = 14.5f,
            .clear_grace_s = 1.65f,
            .fire_interval_s = 0.032f,
            .projectile_speed = 980.0f,
            .projectile_ttl_s = 2.2f,
            .projectile_radius = 3.2f,
            .aim_jitter_deg = 1.35f
        };
        fog->searchlights[2] = (leveldef_searchlight){
            .anchor_x01 = 4.30f,
            .anchor_y01 = 0.50f,
            .length_h01 = 0.74f,
            .half_angle_deg = 8.2f,
            .sweep_center_deg = 0.0f,
            .sweep_amplitude_deg = 180.0f,
            .sweep_speed = 1.85f,
            .sweep_phase_deg = 230.0f,
            .sweep_motion = SEARCHLIGHT_MOTION_SPIN,
            .source_type = SEARCHLIGHT_SOURCE_ORB,
            .source_radius = 14.5f,
            .clear_grace_s = 1.60f,
            .fire_interval_s = 0.030f,
            .projectile_speed = 980.0f,
            .projectile_ttl_s = 2.2f,
            .projectile_radius = 3.2f,
            .aim_jitter_deg = 1.35f
        };
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

    lvl->searchlights[lvl->searchlight_count++] = sl;
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
            fprintf(log_out, "leveldef: using built-in defaults (could not open %s)\n", path);
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
                        cur_level->boid_cycle_count = 0;
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
                    if (strcmp(k, "wave_mode") == 0) {
                        cur_level->wave_mode = (strcmp(v, "boid_only") == 0) ? LEVELDEF_WAVES_BOID_ONLY : LEVELDEF_WAVES_NORMAL;
                    } else if (strcmp(k, "default_boid_profile") == 0) {
                        cur_level->default_boid_profile = leveldef_find_boid_profile(db, v);
                    } else if (strcmp(k, "wave_cooldown_initial_s") == 0) {
                        cur_level->wave_cooldown_initial_s = strtof(v, NULL);
                    } else if (strcmp(k, "wave_cooldown_between_s") == 0) {
                        cur_level->wave_cooldown_between_s = strtof(v, NULL);
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
    return leveldef_apply_file(db, path, log_out);
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
    return ok;
}
