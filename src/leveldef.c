#include "leveldef.h"

#include <dirent.h>
#include <ctype.h>
#include <math.h>
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
    if (strcmp(name, "BLANK") == 0) return LEVEL_STYLE_BLANK;
    if (strcmp(name, "REVOLVER") == 0) return LEVEL_STYLE_REVOLVER;
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

static int mine_style_from_name(const char* name) {
    char* end = NULL;
    long v;
    if (!name) {
        return -1;
    }
    if (strcmp(name, "classic") == 0 || strcmp(name, "default") == 0) {
        return MINE_STYLE_CLASSIC;
    }
    if (strcmp(name, "anemone") == 0 ||
        strcmp(name, "sea_anemone") == 0 ||
        strcmp(name, "underwater") == 0) {
        return MINE_STYLE_ANEMONE;
    }
    v = strtol(name, &end, 10);
    if (end && *end == '\0') {
        return (int)v;
    }
    return -1;
}

static int wave_pattern_from_name(const char* name) {
    if (!name) {
        return -1;
    }
    if (strcmp(name, "sine_snake") == 0) return LEVELDEF_WAVE_SINE_SNAKE;
    if (strcmp(name, "v_formation") == 0) return LEVELDEF_WAVE_V_FORMATION;
    if (strcmp(name, "swarm") == 0) return LEVELDEF_WAVE_SWARM;
    if (strcmp(name, "swarm_fish") == 0 || strcmp(name, "boid_fish") == 0) return LEVELDEF_WAVE_SWARM_FISH;
    if (strcmp(name, "swarm_firefly") == 0 || strcmp(name, "boid_firefly") == 0) return LEVELDEF_WAVE_SWARM_FIREFLY;
    if (strcmp(name, "swarm_bird") == 0 || strcmp(name, "boid_bird") == 0) return LEVELDEF_WAVE_SWARM_BIRD;
    if (strcmp(name, "kamikaze") == 0) return LEVELDEF_WAVE_KAMIKAZE;
    if (strcmp(name, "asteroid") == 0 || strcmp(name, "asteroid_storm") == 0) return LEVELDEF_WAVE_ASTEROID_STORM;
    return -1;
}

static int event_kind_from_name(const char* name) {
    if (!name) {
        return -1;
    }
    if (strcmp(name, "sine") == 0 || strcmp(name, "sine_snake") == 0) return LEVELDEF_EVENT_WAVE_SINE;
    if (strcmp(name, "v") == 0 || strcmp(name, "v_formation") == 0) return LEVELDEF_EVENT_WAVE_V;
    if (strcmp(name, "swarm") == 0 || strcmp(name, "boid") == 0) return LEVELDEF_EVENT_WAVE_SWARM;
    if (strcmp(name, "swarm_fish") == 0 || strcmp(name, "boid_fish") == 0) return LEVELDEF_EVENT_WAVE_SWARM_FISH;
    if (strcmp(name, "swarm_firefly") == 0 || strcmp(name, "boid_firefly") == 0) return LEVELDEF_EVENT_WAVE_SWARM_FIREFLY;
    if (strcmp(name, "swarm_bird") == 0 || strcmp(name, "boid_bird") == 0) return LEVELDEF_EVENT_WAVE_SWARM_BIRD;
    if (strcmp(name, "kamikaze") == 0) return LEVELDEF_EVENT_WAVE_KAMIKAZE;
    if (strcmp(name, "asteroid") == 0 || strcmp(name, "asteroid_storm") == 0) return LEVELDEF_EVENT_ASTEROID_STORM;
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
    if (strcmp(name, "boid_fish") == 0 || strcmp(name, "swarm_fish") == 0) return 10;
    if (strcmp(name, "boid_firefly") == 0 || strcmp(name, "swarm_firefly") == 0) return 11;
    if (strcmp(name, "boid_bird") == 0 || strcmp(name, "swarm_bird") == 0) return 12;
    if (strcmp(name, "jelly_swarm") == 0 || strcmp(name, "jelly") == 0) return 15;
    if (strcmp(name, "manta_wing") == 0 || strcmp(name, "manta") == 0 || strcmp(name, "manta_ray") == 0) return 16;
    if (strcmp(name, "eel_swarm") == 0 || strcmp(name, "eel") == 0 || strcmp(name, "electric_eel") == 0) return 17;
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
    if (strcmp(name, "blank") == 0) return LEVEL_RENDER_BLANK;
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

static int background_style_from_name(const char* name) {
    if (!name) {
        return -1;
    }
    if (strcmp(name, "stars") == 0) return LEVELDEF_BACKGROUND_STARS;
    if (strcmp(name, "none") == 0) return LEVELDEF_BACKGROUND_NONE;
    if (strcmp(name, "nebula") == 0) return LEVELDEF_BACKGROUND_NEBULA;
    if (strcmp(name, "grid") == 0) return LEVELDEF_BACKGROUND_GRID;
    if (strcmp(name, "solid") == 0) return LEVELDEF_BACKGROUND_SOLID;
    if (strcmp(name, "underwater") == 0) return LEVELDEF_BACKGROUND_UNDERWATER;
    if (strcmp(name, "fire") == 0) return LEVELDEF_BACKGROUND_FIRE;
    return -1;
}

static int background_mask_style_from_name(const char* name) {
    if (!name) {
        return -1;
    }
    if (strcmp(name, "none") == 0) return LEVELDEF_BG_MASK_NONE;
    if (strcmp(name, "terrain") == 0) return LEVELDEF_BG_MASK_TERRAIN;
    if (strcmp(name, "windows") == 0) return LEVELDEF_BG_MASK_WINDOWS;
    return -1;
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

static int cmp_event_order(const void* a, const void* b) {
    const leveldef_event_entry* ea = (const leveldef_event_entry*)a;
    const leveldef_event_entry* eb = (const leveldef_event_entry*)b;
    if (ea->order < eb->order) return -1;
    if (ea->order > eb->order) return 1;
    return 0;
}

static int cmp_strptr(const void* a, const void* b) {
    const char* sa = *(const char* const*)a;
    const char* sb = *(const char* const*)b;
    return strcmp(sa, sb);
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

static void leveldef_init_builtin_boid_profiles(leveldef_db* db) {
    leveldef_boid_profile* p;
    if (!db) {
        return;
    }
    db->profile_count = 0;

    if (db->profile_count < LEVELDEF_MAX_BOID_PROFILES) {
        p = &db->profiles[db->profile_count++];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "FIREFLY");
        snprintf(p->wave_name, sizeof(p->wave_name), "boid swarm: firefly scatter");
        p->count = 18;
        p->sep_w = 2.40f;
        p->ali_w = 0.25f;
        p->coh_w = 0.20f;
        p->avoid_w = 2.90f;
        p->goal_w = 0.55f;
        p->sep_r = 84.0f;
        p->ali_r = 168.0f;
        p->coh_r = 205.0f;
        p->goal_amp = 140.0f;
        p->goal_freq = 1.40f;
        p->wander_w = 1.30f;
        p->wander_freq = 2.10f;
        p->steer_drag = 1.55f;
        p->max_turn_rate_deg = 460.0f;
        p->max_speed = 210.0f;
        p->min_speed = 62.0f;
        p->accel = 6.20f;
        p->radius_min = 12.0f;
        p->radius_max = 17.0f;
        p->spawn_x01 = 0.62f;
        p->spawn_x_span = 260.0f;
        p->spawn_y01 = 0.50f;
        p->spawn_y_span = 140.0f;
    }

    if (db->profile_count < LEVELDEF_MAX_BOID_PROFILES) {
        p = &db->profiles[db->profile_count++];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "FISH");
        snprintf(p->wave_name, sizeof(p->wave_name), "boid swarm: fish school");
        p->count = 16;
        p->sep_w = 1.60f;
        p->ali_w = 1.10f;
        p->coh_w = 1.00f;
        p->avoid_w = 2.40f;
        p->goal_w = 1.00f;
        p->sep_r = 104.0f;
        p->ali_r = 290.0f;
        p->coh_r = 345.0f;
        p->goal_amp = 52.0f;
        p->goal_freq = 0.45f;
        p->wander_w = 0.22f;
        p->wander_freq = 0.80f;
        p->steer_drag = 1.25f;
        p->max_turn_rate_deg = 440.0f;
        p->max_speed = 300.0f;
        p->min_speed = 78.0f;
        p->accel = 7.80f;
        p->radius_min = 12.0f;
        p->radius_max = 17.0f;
        p->spawn_x01 = 0.62f;
        p->spawn_x_span = 260.0f;
        p->spawn_y01 = 0.50f;
        p->spawn_y_span = 140.0f;
    }

    if (db->profile_count < LEVELDEF_MAX_BOID_PROFILES) {
        p = &db->profiles[db->profile_count++];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "BIRD");
        snprintf(p->wave_name, sizeof(p->wave_name), "boid swarm: bird flock");
        p->count = 12;
        p->sep_w = 1.40f;
        p->ali_w = 1.55f;
        p->coh_w = 0.85f;
        p->avoid_w = 2.20f;
        p->goal_w = 1.35f;
        p->sep_r = 126.0f;
        p->ali_r = 336.0f;
        p->coh_r = 392.0f;
        p->goal_amp = 34.0f;
        p->goal_freq = 0.28f;
        p->wander_w = 0.08f;
        p->wander_freq = 0.45f;
        p->steer_drag = 1.08f;
        p->max_turn_rate_deg = 340.0f;
        p->max_speed = 430.0f;
        p->min_speed = 150.0f;
        p->accel = 9.00f;
        p->radius_min = 11.0f;
        p->radius_max = 16.0f;
        p->spawn_x01 = 0.62f;
        p->spawn_x_span = 260.0f;
        p->spawn_y01 = 0.50f;
        p->spawn_y_span = 140.0f;
    }
}

void leveldef_init_defaults(leveldef_db* db) {
    int i;
    if (!db) {
        return;
    }
    memset(db, 0, sizeof(*db));
    leveldef_init_builtin_boid_profiles(db);
    for (i = 0; i < LEVEL_STYLE_COUNT; ++i) {
        db->level_present[i] = 0;
        db->levels[i].editor_length_screens = 12.0f;
        db->levels[i].theme_palette = 0;
        db->levels[i].background_style = LEVELDEF_BACKGROUND_STARS;
        db->levels[i].background_mask_style = LEVELDEF_BG_MASK_NONE;
        db->levels[i].underwater_density = 1.0f;
        db->levels[i].underwater_caustic_strength = 1.0f;
        db->levels[i].underwater_caustic_scale = 1.0f;
        db->levels[i].underwater_bubble_rate = 1.0f;
        db->levels[i].underwater_haze_alpha = 1.0f;
        db->levels[i].underwater_current_speed = 1.0f;
        db->levels[i].underwater_palette_shift = 0.0f;
        db->levels[i].underwater_kelp_density = 1.0f;
        db->levels[i].underwater_kelp_sway_amp = 1.0f;
        db->levels[i].underwater_kelp_sway_speed = 1.0f;
        db->levels[i].underwater_kelp_height = 1.0f;
        db->levels[i].underwater_kelp_parallax_strength = 1.0f;
        db->levels[i].underwater_kelp_tint_r = 1.0f;
        db->levels[i].underwater_kelp_tint_g = 1.0f;
        db->levels[i].underwater_kelp_tint_b = 1.0f;
        db->levels[i].underwater_kelp_tint_strength = 0.0f;
        db->levels[i].fire_magma_scale = 1.10f;
        db->levels[i].fire_warp_amp = 0.14f;
        db->levels[i].fire_pulse_freq = 1.20f;
        db->levels[i].fire_plume_height = 1.00f;
        db->levels[i].fire_rise_speed = 1.35f;
        db->levels[i].fire_distortion_amp = 0.003f;
        db->levels[i].fire_smoke_alpha_cap = 0.34f;
        db->levels[i].fire_ember_spawn_rate = 90.0f;
        db->levels[i].wave_mode = -1;
        db->levels[i].render_style = -1;
        db->levels[i].spawn_mode = -1;
        db->levels[i].default_boid_profile = -1;
        db->levels[i].powerup_drop_chance = 0.12f;
        db->levels[i].powerup_double_shot_r = 0.90f;
        db->levels[i].powerup_double_shot_g = 0.95f;
        db->levels[i].powerup_double_shot_b = 1.00f;
        db->levels[i].powerup_triple_shot_r = 1.00f;
        db->levels[i].powerup_triple_shot_g = 0.86f;
        db->levels[i].powerup_triple_shot_b = 0.54f;
        db->levels[i].powerup_vitality_r = 0.64f;
        db->levels[i].powerup_vitality_g = 1.00f;
        db->levels[i].powerup_vitality_b = 0.72f;
        db->levels[i].powerup_orbital_boost_r = 0.70f;
        db->levels[i].powerup_orbital_boost_g = 0.86f;
        db->levels[i].powerup_orbital_boost_b = 1.00f;
        db->levels[i].powerup_magnet_r = 1.00f;
        db->levels[i].powerup_magnet_g = 0.76f;
        db->levels[i].powerup_magnet_b = 0.72f;
    }
    {
        leveldef_level* b = &db->levels[LEVEL_STYLE_BLANK];
        b->render_style = LEVEL_RENDER_BLANK;
        b->background_style = LEVELDEF_BACKGROUND_NONE;
        b->background_mask_style = LEVELDEF_BG_MASK_NONE;
        b->wave_mode = LEVELDEF_WAVES_NORMAL;
        b->spawn_mode = LEVELDEF_SPAWN_SEQUENCED_CLEAR;
        b->spawn_interval_s = 2.0f;
        b->default_boid_profile = 0;
        b->wave_cooldown_initial_s = 0.65f;
        b->wave_cooldown_between_s = 2.0f;
        b->bidirectional_spawns = 0;
        b->cylinder_double_swarm_chance = 0.0f;
        b->powerup_drop_chance = 0.12f;
        b->exit_enabled = 0;
        b->exit_x01 = 2.0f;
        b->exit_y01 = 0.5f;
        b->asteroid_storm_enabled = 0;
        b->asteroid_storm_start_x01 = 0.0f;
        b->asteroid_storm_angle_deg = 30.0f;
        b->asteroid_storm_speed = 520.0f;
        b->asteroid_storm_duration_s = 10.0f;
        b->asteroid_storm_density = 1.0f;
        /* Keep blank levels fully usable as authored levels even when
         * per-wave tuning keys are omitted in config files. */
        b->sine.count = 10;
        b->sine.start_x01 = 0.70f;
        b->sine.spacing_x = 44.0f;
        b->sine.home_y01 = 0.52f;
        b->sine.phase_step = 0.55f;
        b->sine.form_amp = 92.0f;
        b->sine.form_freq = 1.80f;
        b->sine.break_delay_base = 1.10f;
        b->sine.break_delay_step = 0.16f;
        b->sine.max_speed = 285.0f;
        b->sine.accel = 6.8f;
        b->v.count = 11;
        b->v.start_x01 = 0.70f;
        b->v.spacing_x = 32.0f;
        b->v.home_y01 = 0.55f;
        b->v.home_y_step = 18.0f;
        b->v.phase_step = 0.35f;
        b->v.form_amp = 10.0f;
        b->v.form_freq = 1.20f;
        b->v.break_delay_min = 0.90f;
        b->v.break_delay_rand = 1.80f;
        b->v.max_speed = 295.0f;
        b->v.accel = 7.5f;
        b->kamikaze.count = 9;
        b->kamikaze.start_x01 = 0.70f;
        b->kamikaze.spacing_x = 34.0f;
        b->kamikaze.y_margin = 64.0f;
        b->kamikaze.max_speed = 360.0f;
        b->kamikaze.accel = 9.0f;
        b->kamikaze.radius_min = 11.0f;
        b->kamikaze.radius_max = 17.0f;
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

static int parse_minefield(leveldef_level* lvl, const char* value, FILE* log_out) {
    char buf[192];
    char* tok;
    char* save = NULL;
    const int expected_min = 3;
    const int expected_max = 4;
    char* fields[4];
    int i = 0;
    leveldef_minefield mf;

    if (!lvl || !value || lvl->minefield_count >= LEVELDEF_MAX_MINEFIELDS) {
        return 0;
    }
    memset(&mf, 0, sizeof(mf));
    strncpy(buf, value, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    tok = strtok_r(buf, ",", &save);
    while (tok && i < expected_max) {
        fields[i++] = trim(tok);
        tok = strtok_r(NULL, ",", &save);
    }
    if (tok != NULL || i < expected_min || i > expected_max) {
        if (log_out) {
            fprintf(
                log_out,
                "leveldef: minefield expects %d or %d fields, got %d\n",
                expected_min,
                expected_max,
                i
            );
        }
        return 0;
    }

    mf.anchor_x01 = strtof(fields[0], NULL);
    mf.anchor_y01 = strtof(fields[1], NULL);
    mf.count = (int)strtol(fields[2], NULL, 10);
    mf.style = MINE_STYLE_CLASSIC;
    if (i >= 4) {
        mf.style = mine_style_from_name(fields[3]);
    }
    if (mf.count <= 0) {
        if (log_out) {
            fprintf(log_out, "leveldef: minefield count must be > 0\n");
        }
        return 0;
    }
    if (mf.style < MINE_STYLE_CLASSIC || mf.style > MINE_STYLE_ANEMONE) {
        if (log_out) {
            fprintf(log_out, "leveldef: minefield style must be CLASSIC or ANEMONE\n");
        }
        return 0;
    }
    lvl->minefields[lvl->minefield_count++] = mf;
    return 1;
}

static int parse_missile_launcher(leveldef_level* lvl, const char* value, FILE* log_out) {
    char buf[320];
    char* tok;
    char* save = NULL;
    const int expected = 10;
    char* fields[10];
    int i = 0;
    leveldef_missile_launcher ml;

    if (!lvl || !value || lvl->missile_launcher_count >= LEVELDEF_MAX_MISSILE_LAUNCHERS) {
        return 0;
    }
    memset(&ml, 0, sizeof(ml));
    strncpy(buf, value, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    tok = strtok_r(buf, ",", &save);
    while (tok && i < expected) {
        fields[i++] = trim(tok);
        tok = strtok_r(NULL, ",", &save);
    }
    if (i != expected) {
        if (log_out) {
            fprintf(log_out, "leveldef: missile_launcher expects %d fields, got %d\n", expected, i);
        }
        return 0;
    }

    ml.anchor_x01 = strtof(fields[0], NULL);
    ml.anchor_y01 = strtof(fields[1], NULL);
    ml.count = (int)strtol(fields[2], NULL, 10);
    ml.spacing = strtof(fields[3], NULL);
    ml.activation_range = strtof(fields[4], NULL);
    ml.missile_speed = strtof(fields[5], NULL);
    ml.missile_turn_rate_deg = strtof(fields[6], NULL);
    ml.missile_ttl_s = strtof(fields[7], NULL);
    ml.hit_radius = strtof(fields[8], NULL);
    ml.blast_radius = strtof(fields[9], NULL);
    if (ml.count <= 0) {
        if (log_out) {
            fprintf(log_out, "leveldef: missile_launcher count must be > 0\n");
        }
        return 0;
    }
    lvl->missile_launchers[lvl->missile_launcher_count++] = ml;
    return 1;
}

static int parse_arc_node(leveldef_level* lvl, const char* value, FILE* log_out) {
    char buf[256];
    char* tok;
    char* save = NULL;
    const int expected = 7;
    char* fields[7];
    int i = 0;
    leveldef_arc_node an;

    if (!lvl || !value || lvl->arc_node_count >= LEVELDEF_MAX_ARC_NODES) {
        return 0;
    }
    memset(&an, 0, sizeof(an));
    strncpy(buf, value, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    tok = strtok_r(buf, ",", &save);
    while (tok && i < expected) {
        fields[i++] = trim(tok);
        tok = strtok_r(NULL, ",", &save);
    }
    if (i != expected) {
        if (log_out) {
            fprintf(log_out, "leveldef: arc_node expects %d fields, got %d\n", expected, i);
        }
        return 0;
    }

    an.anchor_x01 = strtof(fields[0], NULL);
    an.anchor_y01 = strtof(fields[1], NULL);
    an.period_s = strtof(fields[2], NULL);
    an.on_s = strtof(fields[3], NULL);
    an.radius = strtof(fields[4], NULL);
    an.push_accel = strtof(fields[5], NULL);
    an.damage_interval_s = strtof(fields[6], NULL);
    if (an.period_s <= 0.0f || an.on_s < 0.0f || an.radius <= 0.0f || an.push_accel < 0.0f || an.damage_interval_s <= 0.0f) {
        if (log_out) {
            fprintf(log_out, "leveldef: invalid arc_node values\n");
        }
        return 0;
    }
    lvl->arc_nodes[lvl->arc_node_count++] = an;
    return 1;
}

static int parse_window_mask(leveldef_level* lvl, const char* value, FILE* log_out) {
    char buf[256];
    char* tok;
    char* save = NULL;
    const int expected = 5;
    char* fields[5];
    int i = 0;
    leveldef_window_mask wm;

    if (!lvl || !value || lvl->window_mask_count >= LEVELDEF_MAX_WINDOW_MASKS) {
        return 0;
    }
    memset(&wm, 0, sizeof(wm));
    strncpy(buf, value, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    tok = strtok_r(buf, ",", &save);
    while (tok && i < expected) {
        fields[i++] = trim(tok);
        tok = strtok_r(NULL, ",", &save);
    }
    if (i != expected) {
        if (log_out) {
            fprintf(log_out, "leveldef: window_mask expects %d fields, got %d\n", expected, i);
        }
        return 0;
    }

    wm.anchor_x01 = strtof(fields[0], NULL);
    wm.anchor_y01 = strtof(fields[1], NULL);
    wm.width_h01 = strtof(fields[2], NULL);
    wm.height_v01 = strtof(fields[3], NULL);
    wm.flip_vertical = atoi(fields[4]) ? 1 : 0;
    if (wm.width_h01 <= 0.0f || wm.height_v01 <= 0.0f) {
        if (log_out) {
            fprintf(log_out, "leveldef: window_mask width/height must be > 0\n");
        }
        return 0;
    }
    lvl->window_masks[lvl->window_mask_count++] = wm;
    return 1;
}

static int parse_structure_instance(leveldef_level* lvl, const char* value, FILE* log_out) {
    char buf[320];
    char* tok;
    char* save = NULL;
    const int expected_min = 10;
    const int expected_max = 13;
    char* fields[13];
    int i = 0;
    leveldef_structure_instance st;

    if (!lvl || !value || lvl->structure_count >= LEVELDEF_MAX_STRUCTURES) {
        return 0;
    }
    memset(&st, 0, sizeof(st));
    strncpy(buf, value, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    tok = strtok_r(buf, ",", &save);
    while (tok && i < expected_max) {
        fields[i++] = trim(tok);
        tok = strtok_r(NULL, ",", &save);
    }
    if (i < expected_min || i > expected_max) {
        if (log_out) {
            fprintf(log_out, "leveldef: structure expects %d..%d fields, got %d\n", expected_min, expected_max, i);
        }
        return 0;
    }
    st.prefab_id = (int)strtol(fields[0], NULL, 10);
    st.layer = (int)strtol(fields[1], NULL, 10);
    st.grid_x = (int)strtol(fields[2], NULL, 10);
    st.grid_y = (int)strtol(fields[3], NULL, 10);
    st.rotation_quadrants = (int)strtol(fields[4], NULL, 10);
    st.flip_x = (int)strtol(fields[5], NULL, 10);
    st.flip_y = (int)strtol(fields[6], NULL, 10);
    st.w_units = (int)strtol(fields[7], NULL, 10);
    st.h_units = (int)strtol(fields[8], NULL, 10);
    st.variant = (int)strtol(fields[9], NULL, 10);
    st.vent_density = (i >= 11) ? strtof(fields[10], NULL) : 1.0f;
    st.vent_opacity = (i >= 12) ? strtof(fields[11], NULL) : 1.0f;
    st.vent_plume_height = (i >= 13) ? strtof(fields[12], NULL) : 1.0f;
    if (st.vent_density <= 0.0f) st.vent_density = 1.0f;
    if (st.vent_opacity <= 0.0f) st.vent_opacity = 1.0f;
    if (st.vent_plume_height <= 0.0f) st.vent_plume_height = 1.0f;
    lvl->structures[lvl->structure_count++] = st;
    return 1;
}

static int parse_curated_enemy(leveldef_level* lvl, const char* value, FILE* log_out) {
    char buf[320];
    char* tok;
    char* save = NULL;
    const int min_expected = 6;
    const int max_expected = 7;
    char* fields[7];
    int i = 0;
    leveldef_curated_enemy ce;

    if (!lvl || !value || lvl->curated_count >= (int)(sizeof(lvl->curated) / sizeof(lvl->curated[0]))) {
        return 0;
    }
    memset(&ce, 0, sizeof(ce));
    strncpy(buf, value, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    tok = strtok_r(buf, ",", &save);
    while (tok && i < max_expected) {
        fields[i++] = trim(tok);
        tok = strtok_r(NULL, ",", &save);
    }
    if (i != min_expected && i != max_expected) {
        if (log_out) {
            fprintf(log_out, "leveldef: curated_enemy expects %d or %d fields, got %d\n", min_expected, max_expected, i);
        }
        return 0;
    }

    ce.kind = curated_kind_from_name(fields[0]);
    ce.x01 = strtof(fields[1], NULL);
    ce.y01 = strtof(fields[2], NULL);
    ce.a = strtof(fields[3], NULL);
    ce.b = strtof(fields[4], NULL);
    ce.c = strtof(fields[5], NULL);
    ce.d = (i >= 7) ? strtof(fields[6], NULL) : 0.0f;
    if (ce.kind < 0) {
        if (log_out) {
            fprintf(log_out, "leveldef: invalid curated_enemy kind '%s'\n", fields[0]);
        }
        return 0;
    }
    lvl->curated[lvl->curated_count++] = ce;
    return 1;
}

static int parse_event_entry(leveldef_level* lvl, const char* value, FILE* log_out) {
    char buf[192];
    char* tok;
    char* save = NULL;
    char* fields[3];
    int i = 0;
    leveldef_event_entry ev;
    if (!lvl || !value || lvl->event_count >= LEVELDEF_MAX_EVENTS) {
        return 0;
    }
    memset(&ev, 0, sizeof(ev));
    strncpy(buf, value, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    tok = strtok_r(buf, ",", &save);
    while (tok && i < 3) {
        fields[i++] = trim(tok);
        tok = strtok_r(NULL, ",", &save);
    }
    if (i != 2 && i != 3) {
        if (log_out) {
            fprintf(log_out, "leveldef: event expects 2 or 3 fields, got %d\n", i);
        }
        return 0;
    }
    ev.kind = event_kind_from_name(fields[0]);
    if (i == 3) {
        ev.order = (int)strtol(fields[1], NULL, 10);
        ev.delay_s = strtof(fields[2], NULL);
    } else {
        ev.order = lvl->event_count + 1;
        ev.delay_s = strtof(fields[1], NULL);
    }
    if (ev.kind < 0) {
        if (log_out) {
            fprintf(log_out, "leveldef: invalid event kind '%s'\n", fields[0]);
        }
        return 0;
    }
    lvl->events[lvl->event_count++] = ev;
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
                        db->level_present[sid] = 1;
                        cur_level = &db->levels[sid];
                        cur_level->searchlight_count = 0;
                        cur_level->minefield_count = 0;
                        cur_level->missile_launcher_count = 0;
                        cur_level->arc_node_count = 0;
                        cur_level->window_mask_count = 0;
                        cur_level->structure_count = 0;
                        cur_level->curated_count = 0;
                        cur_level->boid_cycle_count = 0;
                        cur_level->wave_cycle_count = 0;
                        cur_level->event_count = 0;
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
                    if (strcmp(k, "level_length_screens") == 0) {
                        cur_level->editor_length_screens = strtof(v, NULL);
                    } else if (strcmp(k, "theme_palette") == 0) {
                        cur_level->theme_palette = atoi(v);
                    } else if (strcmp(k, "background") == 0) {
                        cur_level->background_style = background_style_from_name(v);
                    } else if (strcmp(k, "background_mask") == 0) {
                        cur_level->background_mask_style = background_mask_style_from_name(v);
                    } else if (strcmp(k, "underwater.density") == 0) {
                        cur_level->underwater_density = strtof(v, NULL);
                    } else if (strcmp(k, "underwater.caustic_strength") == 0) {
                        cur_level->underwater_caustic_strength = strtof(v, NULL);
                    } else if (strcmp(k, "underwater.caustic_scale") == 0) {
                        cur_level->underwater_caustic_scale = strtof(v, NULL);
                    } else if (strcmp(k, "underwater.bubble_rate") == 0) {
                        cur_level->underwater_bubble_rate = strtof(v, NULL);
                    } else if (strcmp(k, "underwater.haze_alpha") == 0) {
                        cur_level->underwater_haze_alpha = strtof(v, NULL);
                    } else if (strcmp(k, "underwater.current_speed") == 0) {
                        cur_level->underwater_current_speed = strtof(v, NULL);
                    } else if (strcmp(k, "underwater.palette_shift") == 0) {
                        cur_level->underwater_palette_shift = strtof(v, NULL);
                    } else if (strcmp(k, "underwater.kelp_density") == 0) {
                        cur_level->underwater_kelp_density = strtof(v, NULL);
                    } else if (strcmp(k, "underwater.kelp_sway_amp") == 0) {
                        cur_level->underwater_kelp_sway_amp = strtof(v, NULL);
                    } else if (strcmp(k, "underwater.kelp_sway_speed") == 0) {
                        cur_level->underwater_kelp_sway_speed = strtof(v, NULL);
                    } else if (strcmp(k, "underwater.kelp_height") == 0) {
                        cur_level->underwater_kelp_height = strtof(v, NULL);
                    } else if (strcmp(k, "underwater.kelp_parallax_strength") == 0) {
                        cur_level->underwater_kelp_parallax_strength = strtof(v, NULL);
                    } else if (strcmp(k, "underwater.kelp_tint_r") == 0) {
                        cur_level->underwater_kelp_tint_r = strtof(v, NULL);
                    } else if (strcmp(k, "underwater.kelp_tint_g") == 0) {
                        cur_level->underwater_kelp_tint_g = strtof(v, NULL);
                    } else if (strcmp(k, "underwater.kelp_tint_b") == 0) {
                        cur_level->underwater_kelp_tint_b = strtof(v, NULL);
                    } else if (strcmp(k, "underwater.kelp_tint_strength") == 0) {
                        cur_level->underwater_kelp_tint_strength = strtof(v, NULL);
                    } else if (strcmp(k, "fire.magma_scale") == 0) {
                        cur_level->fire_magma_scale = strtof(v, NULL);
                    } else if (strcmp(k, "fire.warp_amp") == 0) {
                        cur_level->fire_warp_amp = strtof(v, NULL);
                    } else if (strcmp(k, "fire.pulse_freq") == 0) {
                        cur_level->fire_pulse_freq = strtof(v, NULL);
                    } else if (strcmp(k, "fire.plume_height") == 0) {
                        cur_level->fire_plume_height = strtof(v, NULL);
                    } else if (strcmp(k, "fire.rise_speed") == 0) {
                        cur_level->fire_rise_speed = strtof(v, NULL);
                    } else if (strcmp(k, "fire.distortion_amp") == 0) {
                        cur_level->fire_distortion_amp = strtof(v, NULL);
                    } else if (strcmp(k, "fire.smoke_alpha_cap") == 0) {
                        cur_level->fire_smoke_alpha_cap = strtof(v, NULL);
                    } else if (strcmp(k, "fire.ember_spawn_rate") == 0) {
                        cur_level->fire_ember_spawn_rate = strtof(v, NULL);
                    } else if (strcmp(k, "render_style") == 0) {
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
                    } else if (strcmp(k, "bidirectional_spawns") == 0) {
                        cur_level->bidirectional_spawns = atoi(v) ? 1 : 0;
                    } else if (strcmp(k, "cylinder_double_swarm_chance") == 0) {
                        cur_level->cylinder_double_swarm_chance = strtof(v, NULL);
                    } else if (strcmp(k, "powerup_drop_chance") == 0) {
                        cur_level->powerup_drop_chance = strtof(v, NULL);
                    } else if (strcmp(k, "powerup.double_shot_r") == 0) {
                        cur_level->powerup_double_shot_r = strtof(v, NULL);
                    } else if (strcmp(k, "powerup.double_shot_g") == 0) {
                        cur_level->powerup_double_shot_g = strtof(v, NULL);
                    } else if (strcmp(k, "powerup.double_shot_b") == 0) {
                        cur_level->powerup_double_shot_b = strtof(v, NULL);
                    } else if (strcmp(k, "powerup.triple_shot_r") == 0) {
                        cur_level->powerup_triple_shot_r = strtof(v, NULL);
                    } else if (strcmp(k, "powerup.triple_shot_g") == 0) {
                        cur_level->powerup_triple_shot_g = strtof(v, NULL);
                    } else if (strcmp(k, "powerup.triple_shot_b") == 0) {
                        cur_level->powerup_triple_shot_b = strtof(v, NULL);
                    } else if (strcmp(k, "powerup.vitality_r") == 0) {
                        cur_level->powerup_vitality_r = strtof(v, NULL);
                    } else if (strcmp(k, "powerup.vitality_g") == 0) {
                        cur_level->powerup_vitality_g = strtof(v, NULL);
                    } else if (strcmp(k, "powerup.vitality_b") == 0) {
                        cur_level->powerup_vitality_b = strtof(v, NULL);
                    } else if (strcmp(k, "powerup.orbital_boost_r") == 0) {
                        cur_level->powerup_orbital_boost_r = strtof(v, NULL);
                    } else if (strcmp(k, "powerup.orbital_boost_g") == 0) {
                        cur_level->powerup_orbital_boost_g = strtof(v, NULL);
                    } else if (strcmp(k, "powerup.orbital_boost_b") == 0) {
                        cur_level->powerup_orbital_boost_b = strtof(v, NULL);
                    } else if (strcmp(k, "powerup.magnet_r") == 0) {
                        cur_level->powerup_magnet_r = strtof(v, NULL);
                    } else if (strcmp(k, "powerup.magnet_g") == 0) {
                        cur_level->powerup_magnet_g = strtof(v, NULL);
                    } else if (strcmp(k, "powerup.magnet_b") == 0) {
                        cur_level->powerup_magnet_b = strtof(v, NULL);
                    } else if (strcmp(k, "exit_enabled") == 0) {
                        cur_level->exit_enabled = atoi(v) ? 1 : 0;
                    } else if (strcmp(k, "exit_x01") == 0) {
                        cur_level->exit_x01 = strtof(v, NULL);
                    } else if (strcmp(k, "exit_y01") == 0) {
                        cur_level->exit_y01 = strtof(v, NULL);
                    } else if (strcmp(k, "asteroid_storm_enabled") == 0) {
                        cur_level->asteroid_storm_enabled = atoi(v) ? 1 : 0;
                    } else if (strcmp(k, "asteroid_storm_start_x01") == 0) {
                        cur_level->asteroid_storm_start_x01 = strtof(v, NULL);
                    } else if (strcmp(k, "asteroid_storm_angle_deg") == 0) {
                        cur_level->asteroid_storm_angle_deg = strtof(v, NULL);
                    } else if (strcmp(k, "asteroid_storm_speed") == 0) {
                        cur_level->asteroid_storm_speed = strtof(v, NULL);
                    } else if (strcmp(k, "asteroid_storm_duration_s") == 0) {
                        cur_level->asteroid_storm_duration_s = strtof(v, NULL);
                    } else if (strcmp(k, "asteroid_storm_density") == 0) {
                        cur_level->asteroid_storm_density = strtof(v, NULL);
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
                    } else if (strcmp(k, "event") == 0) {
                        (void)parse_event_entry(cur_level, v, log_out);
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
                    } else if (strcmp(k, "minefield") == 0) {
                        (void)parse_minefield(cur_level, v, log_out);
                    } else if (strcmp(k, "missile_launcher") == 0) {
                        (void)parse_missile_launcher(cur_level, v, log_out);
                    } else if (strcmp(k, "arc_node") == 0) {
                        (void)parse_arc_node(cur_level, v, log_out);
                    } else if (strcmp(k, "window_mask") == 0) {
                        (void)parse_window_mask(cur_level, v, log_out);
                    } else if (strcmp(k, "structure") == 0) {
                        (void)parse_structure_instance(cur_level, v, log_out);
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
                    } else if (strcmp(k, "max_turn_rate_deg") == 0) {
                        cur_profile->max_turn_rate_deg = strtof(v, NULL);
                    } else if (strcmp(k, "max_speed") == 0) {
                        cur_profile->max_speed = strtof(v, NULL);
                    } else if (strcmp(k, "min_speed") == 0) {
                        cur_profile->min_speed = strtof(v, NULL);
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

    {
        int li;
        for (li = 0; li < LEVEL_STYLE_COUNT; ++li) {
            leveldef_level* l = &db->levels[li];
            if (l->event_count > 1) {
                qsort(l->events, (size_t)l->event_count, sizeof(l->events[0]), cmp_event_order);
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
    int present_n = 0;
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
        if (!db->level_present[i]) {
            continue;
        }
        present_n += 1;
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
        if (l->editor_length_screens <= 0.0f || !isfinite(l->editor_length_screens)) {
            if (log_out) {
                fprintf(log_out, "leveldef: level %d invalid level_length_screens\n", i);
            }
            ok = 0;
        }
        if (l->wave_cooldown_initial_s <= 0.0f || l->wave_cooldown_between_s <= 0.0f) {
            if (log_out) {
                fprintf(log_out, "leveldef: level %d invalid wave cooldowns\n", i);
            }
            ok = 0;
        }
        if (l->theme_palette < 0 || l->theme_palette > 2) {
            if (log_out) {
                fprintf(log_out, "leveldef: level %d invalid theme_palette (expected 0..2)\n", i);
            }
            ok = 0;
        }
        if (l->background_style < LEVELDEF_BACKGROUND_STARS || l->background_style > LEVELDEF_BACKGROUND_FIRE) {
            if (log_out) {
                fprintf(log_out, "leveldef: level %d invalid background (expected stars|none|nebula|grid|solid|underwater|fire)\n", i);
            }
            ok = 0;
        }
        if (l->underwater_density < 0.0f || !isfinite(l->underwater_density) ||
            l->underwater_caustic_strength < 0.0f || !isfinite(l->underwater_caustic_strength) ||
            l->underwater_caustic_scale <= 0.0f || !isfinite(l->underwater_caustic_scale) ||
            l->underwater_bubble_rate < 0.0f || !isfinite(l->underwater_bubble_rate) ||
            l->underwater_haze_alpha < 0.0f || !isfinite(l->underwater_haze_alpha) ||
            l->underwater_current_speed < 0.0f || !isfinite(l->underwater_current_speed) ||
            !isfinite(l->underwater_palette_shift) ||
            l->underwater_kelp_density < 0.0f || !isfinite(l->underwater_kelp_density) ||
            l->underwater_kelp_sway_amp < 0.0f || !isfinite(l->underwater_kelp_sway_amp) ||
            l->underwater_kelp_sway_speed < 0.0f || !isfinite(l->underwater_kelp_sway_speed) ||
            l->underwater_kelp_height <= 0.0f || !isfinite(l->underwater_kelp_height) ||
            l->underwater_kelp_parallax_strength < 0.0f || !isfinite(l->underwater_kelp_parallax_strength) ||
            l->underwater_kelp_tint_r < 0.0f || l->underwater_kelp_tint_r > 2.0f || !isfinite(l->underwater_kelp_tint_r) ||
            l->underwater_kelp_tint_g < 0.0f || l->underwater_kelp_tint_g > 2.0f || !isfinite(l->underwater_kelp_tint_g) ||
            l->underwater_kelp_tint_b < 0.0f || l->underwater_kelp_tint_b > 2.0f || !isfinite(l->underwater_kelp_tint_b) ||
            l->underwater_kelp_tint_strength < 0.0f || l->underwater_kelp_tint_strength > 1.0f || !isfinite(l->underwater_kelp_tint_strength)) {
            if (log_out) {
                fprintf(log_out, "leveldef: level %d invalid underwater tuning values\n", i);
            }
            ok = 0;
        }
        if (l->fire_magma_scale <= 0.0f || !isfinite(l->fire_magma_scale) ||
            l->fire_warp_amp < 0.0f || l->fire_warp_amp > 1.0f || !isfinite(l->fire_warp_amp) ||
            l->fire_pulse_freq <= 0.0f || l->fire_pulse_freq > 10.0f || !isfinite(l->fire_pulse_freq) ||
            l->fire_plume_height <= 0.0f || l->fire_plume_height > 8.0f || !isfinite(l->fire_plume_height) ||
            l->fire_rise_speed < 0.0f || l->fire_rise_speed > 10.0f || !isfinite(l->fire_rise_speed) ||
            l->fire_distortion_amp < 0.0f || l->fire_distortion_amp > 0.05f || !isfinite(l->fire_distortion_amp) ||
            l->fire_smoke_alpha_cap < 0.0f || l->fire_smoke_alpha_cap > 1.0f || !isfinite(l->fire_smoke_alpha_cap) ||
            l->fire_ember_spawn_rate < 0.0f || l->fire_ember_spawn_rate > 2000.0f || !isfinite(l->fire_ember_spawn_rate)) {
            if (log_out) {
                fprintf(log_out, "leveldef: level %d invalid fire tuning values\n", i);
            }
            ok = 0;
        }
        if (l->background_mask_style < LEVELDEF_BG_MASK_NONE || l->background_mask_style > LEVELDEF_BG_MASK_WINDOWS) {
            if (log_out) {
                fprintf(log_out, "leveldef: level %d invalid background_mask (expected none|terrain|windows)\n", i);
            }
            ok = 0;
        }
        if (l->bidirectional_spawns != 0 && l->bidirectional_spawns != 1) {
            if (log_out) {
                fprintf(log_out, "leveldef: level %d invalid bidirectional_spawns (expected 0/1)\n", i);
            }
            ok = 0;
        }
        if (l->cylinder_double_swarm_chance < 0.0f || l->cylinder_double_swarm_chance > 1.0f) {
            if (log_out) {
                fprintf(log_out, "leveldef: level %d invalid cylinder_double_swarm_chance (expected 0..1)\n", i);
            }
            ok = 0;
        }
        if (l->powerup_drop_chance < 0.0f || l->powerup_drop_chance > 1.0f) {
            if (log_out) {
                fprintf(log_out, "leveldef: level %d invalid powerup_drop_chance (expected 0..1)\n", i);
            }
            ok = 0;
        }
        if (l->powerup_double_shot_r < 0.0f || l->powerup_double_shot_r > 1.0f || !isfinite(l->powerup_double_shot_r) ||
            l->powerup_double_shot_g < 0.0f || l->powerup_double_shot_g > 1.0f || !isfinite(l->powerup_double_shot_g) ||
            l->powerup_double_shot_b < 0.0f || l->powerup_double_shot_b > 1.0f || !isfinite(l->powerup_double_shot_b) ||
            l->powerup_triple_shot_r < 0.0f || l->powerup_triple_shot_r > 1.0f || !isfinite(l->powerup_triple_shot_r) ||
            l->powerup_triple_shot_g < 0.0f || l->powerup_triple_shot_g > 1.0f || !isfinite(l->powerup_triple_shot_g) ||
            l->powerup_triple_shot_b < 0.0f || l->powerup_triple_shot_b > 1.0f || !isfinite(l->powerup_triple_shot_b) ||
            l->powerup_vitality_r < 0.0f || l->powerup_vitality_r > 1.0f || !isfinite(l->powerup_vitality_r) ||
            l->powerup_vitality_g < 0.0f || l->powerup_vitality_g > 1.0f || !isfinite(l->powerup_vitality_g) ||
            l->powerup_vitality_b < 0.0f || l->powerup_vitality_b > 1.0f || !isfinite(l->powerup_vitality_b) ||
            l->powerup_orbital_boost_r < 0.0f || l->powerup_orbital_boost_r > 1.0f || !isfinite(l->powerup_orbital_boost_r) ||
            l->powerup_orbital_boost_g < 0.0f || l->powerup_orbital_boost_g > 1.0f || !isfinite(l->powerup_orbital_boost_g) ||
            l->powerup_orbital_boost_b < 0.0f || l->powerup_orbital_boost_b > 1.0f || !isfinite(l->powerup_orbital_boost_b) ||
            l->powerup_magnet_r < 0.0f || l->powerup_magnet_r > 1.0f || !isfinite(l->powerup_magnet_r) ||
            l->powerup_magnet_g < 0.0f || l->powerup_magnet_g > 1.0f || !isfinite(l->powerup_magnet_g) ||
            l->powerup_magnet_b < 0.0f || l->powerup_magnet_b > 1.0f || !isfinite(l->powerup_magnet_b)) {
            if (log_out) {
                fprintf(log_out, "leveldef: level %d invalid powerup color channel (expected 0..1)\n", i);
            }
            ok = 0;
        }
        if (l->asteroid_storm_enabled) {
            if (l->asteroid_storm_speed <= 0.0f || l->asteroid_storm_duration_s <= 0.0f || l->asteroid_storm_density <= 0.0f) {
                if (log_out) {
                    fprintf(log_out, "leveldef: level %d invalid asteroid storm values (speed/duration/density)\n", i);
                }
                ok = 0;
            }
            if (l->asteroid_storm_start_x01 < 0.0f) {
                if (log_out) {
                    fprintf(log_out, "leveldef: level %d invalid asteroid_storm_start_x01 (expected >=0)\n", i);
                }
                ok = 0;
            }
        }
        if (l->default_boid_profile < 0 || l->default_boid_profile >= db->profile_count) {
            if (log_out) {
                fprintf(log_out, "leveldef: level %d invalid default_boid_profile\n", i);
            }
            ok = 0;
        }
        if (l->wave_mode == LEVELDEF_WAVES_BOID_ONLY) {
            if (l->boid_cycle_count > 0) {
                /* boid cycle present and valid */
            }
        } else if (l->wave_mode == LEVELDEF_WAVES_CURATED) {
            if (l->curated_count > 0) {
                /* curated entries present and valid */
            }
        } else if (l->wave_cycle_count > 0) {
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
        if (l->event_count > 0) {
            int j;
            for (j = 0; j < l->event_count; ++j) {
                if (l->events[j].kind < 0 || l->events[j].kind > LEVELDEF_EVENT_WAVE_SWARM_BIRD || l->events[j].order <= 0 || l->events[j].delay_s < 0.0f) {
                    if (log_out) {
                        fprintf(log_out, "leveldef: level %d has invalid event entry\n", i);
                    }
                    ok = 0;
                    break;
                }
            }
        }
        if (l->minefield_count < 0 || l->minefield_count > LEVELDEF_MAX_MINEFIELDS) {
            if (log_out) {
                fprintf(log_out, "leveldef: level %d invalid minefield_count\n", i);
            }
            ok = 0;
        }
        for (int m = 0; m < l->minefield_count; ++m) {
            const leveldef_minefield* mf = &l->minefields[m];
            if (mf->count <= 0 ||
                mf->anchor_x01 < 0.0f ||
                mf->anchor_y01 < 0.0f || mf->anchor_y01 > 1.0f ||
                mf->style < MINE_STYLE_CLASSIC || mf->style > MINE_STYLE_ANEMONE) {
                if (log_out) {
                    fprintf(log_out, "leveldef: level %d invalid minefield entry\n", i);
                }
                ok = 0;
                break;
            }
        }
        if (l->missile_launcher_count < 0 || l->missile_launcher_count > LEVELDEF_MAX_MISSILE_LAUNCHERS) {
            if (log_out) {
                fprintf(log_out, "leveldef: level %d invalid missile_launcher_count\n", i);
            }
            ok = 0;
        }
        for (int m = 0; m < l->missile_launcher_count; ++m) {
            const leveldef_missile_launcher* ml = &l->missile_launchers[m];
            if (ml->count <= 0 ||
                ml->anchor_x01 < 0.0f ||
                ml->anchor_y01 < 0.0f || ml->anchor_y01 > 1.0f ||
                ml->spacing < 0.0f ||
                ml->activation_range <= 0.0f ||
                ml->missile_speed <= 0.0f ||
                ml->missile_turn_rate_deg <= 0.0f ||
                ml->missile_ttl_s <= 0.0f ||
                ml->hit_radius <= 0.0f ||
                ml->blast_radius < 0.0f) {
                if (log_out) {
                    fprintf(log_out, "leveldef: level %d invalid missile_launcher entry\n", i);
                }
                ok = 0;
                break;
            }
        }
        if (l->arc_node_count < 0 || l->arc_node_count > LEVELDEF_MAX_ARC_NODES) {
            if (log_out) {
                fprintf(log_out, "leveldef: level %d invalid arc_node_count\n", i);
            }
            ok = 0;
        }
        for (int m = 0; m < l->arc_node_count; ++m) {
            const leveldef_arc_node* an = &l->arc_nodes[m];
            if (an->anchor_x01 < 0.0f ||
                an->anchor_y01 < 0.0f || an->anchor_y01 > 1.0f ||
                an->period_s <= 0.0f ||
                an->on_s < 0.0f ||
                an->radius <= 0.0f ||
                an->push_accel < 0.0f ||
                an->damage_interval_s <= 0.0f) {
                if (log_out) {
                    fprintf(log_out, "leveldef: level %d invalid arc_node entry\n", i);
                }
                ok = 0;
                break;
            }
        }
        if (l->window_mask_count < 0 || l->window_mask_count > LEVELDEF_MAX_WINDOW_MASKS) {
            if (log_out) {
                fprintf(log_out, "leveldef: level %d invalid window_mask_count\n", i);
            }
            ok = 0;
        }
        for (int m = 0; m < l->window_mask_count; ++m) {
            const leveldef_window_mask* wm = &l->window_masks[m];
            if (wm->width_h01 <= 0.0f || wm->height_v01 <= 0.0f || !isfinite(wm->anchor_x01) || !isfinite(wm->anchor_y01)) {
                if (log_out) {
                    fprintf(log_out, "leveldef: level %d window_mask[%d] invalid\n", i, m);
                }
                ok = 0;
            }
        }
        if (l->structure_count < 0 || l->structure_count > LEVELDEF_MAX_STRUCTURES) {
            if (log_out) {
                fprintf(log_out, "leveldef: level %d invalid structure_count\n", i);
            }
            ok = 0;
        }
        for (int m = 0; m < l->structure_count; ++m) {
            const leveldef_structure_instance* st = &l->structures[m];
            if (st->prefab_id < 0 || st->layer < 0 || st->layer > 1 ||
                st->w_units <= 0 || st->h_units <= 0) {
                if (log_out) {
                    fprintf(log_out, "leveldef: level %d invalid structure entry\n", i);
                }
                ok = 0;
                break;
            }
        }
    }
    if (present_n <= 0) {
        if (log_out) {
            fprintf(log_out, "leveldef: no [level ...] sections loaded from project files\n");
        }
        ok = 0;
    }
    return ok;
}

int leveldef_load_project_layout(leveldef_db* db, const char* dir_path, FILE* log_out) {
    static const char* files[] = {
        "combat.cfg"
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
        DIR* d = opendir(dir_path);
        if (!d) {
            ok = 0;
            if (log_out) {
                fprintf(log_out, "leveldef: could not open level directory %s\n", dir_path);
            }
        } else {
            struct dirent* de = NULL;
            while ((de = readdir(d)) != NULL) {
                const char* fn = de->d_name;
                if (!has_prefix(fn, "level_") || !has_suffix(fn, ".cfg")) {
                    continue;
                }
                if (snprintf(path, sizeof(path), "%s/%s", dir_path, fn) >= (int)sizeof(path)) {
                    ok = 0;
                    if (log_out) {
                        fprintf(log_out, "leveldef: path too long for %s/%s\n", dir_path, fn);
                    }
                    continue;
                }
                if (!leveldef_apply_file(db, path, log_out)) {
                    ok = 0;
                }
            }
            closedir(d);
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

int leveldef_discover_levels_from_dir(
    const leveldef_db* base_db,
    const char* dir_path,
    leveldef_discovered_level* out_levels,
    int out_cap,
    FILE* log_out
) {
    DIR* d = NULL;
    struct dirent* de = NULL;
    char names[LEVELDEF_MAX_DISCOVERED_LEVELS][64];
    const char* ordered[LEVELDEF_MAX_DISCOVERED_LEVELS];
    int file_n = 0;
    int out_n = 0;

    if (!base_db || !dir_path || !dir_path[0] || !out_levels || out_cap <= 0) {
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
        if (file_n >= LEVELDEF_MAX_DISCOVERED_LEVELS) {
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
    for (int i = 0; i < file_n && out_n < out_cap; ++i) {
        char path[512];
        char base_name[64];
        int style = -1;
        leveldef_level lvl;
        snprintf(path, sizeof(path), "%s/%s", dir_path, ordered[i]);
        if (!leveldef_load_level_file_with_base(base_db, path, &lvl, &style, log_out)) {
            continue;
        }
        snprintf(base_name, sizeof(base_name), "%s", ordered[i]);
        {
            const size_t n = strlen(base_name);
            if (n > 4 && strcmp(base_name + n - 4, ".cfg") == 0) {
                base_name[n - 4] = '\0';
            }
        }
        snprintf(out_levels[out_n].name, sizeof(out_levels[out_n].name), "%s", base_name);
        out_levels[out_n].style_hint = style;
        out_levels[out_n].level = lvl;
        ++out_n;
    }
    return out_n;
}
