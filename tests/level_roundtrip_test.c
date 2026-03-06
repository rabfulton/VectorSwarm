#include "level_editor.h"
#include "leveldef.h"

#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const float kFloatEps = 0.002f;

static int has_prefix(const char* s, const char* prefix) {
    if (!s || !prefix) {
        return 0;
    }
    while (*prefix) {
        if (*s++ != *prefix++) {
            return 0;
        }
    }
    return 1;
}

static int has_suffix(const char* s, const char* suffix) {
    size_t ls;
    size_t lx;
    if (!s || !suffix) {
        return 0;
    }
    ls = strlen(s);
    lx = strlen(suffix);
    if (ls < lx) {
        return 0;
    }
    return strcmp(s + (ls - lx), suffix) == 0;
}

static int pick_roundtrip_level_name(char* out, size_t out_cap) {
    const char* dirs[] = {
        "data/levels",
        "../data/levels",
        VTYPE_SOURCE_DIR "/data/levels"
    };
    char best[LEVEL_EDITOR_NAME_CAP];
    int found = 0;
    int i;
    if (!out || out_cap == 0) {
        return 0;
    }
    out[0] = '\0';
    best[0] = '\0';
    for (i = 0; i < (int)(sizeof(dirs) / sizeof(dirs[0])); ++i) {
        DIR* d = opendir(dirs[i]);
        if (!d) {
            continue;
        }
        for (;;) {
            struct dirent* de = readdir(d);
            char candidate[LEVEL_EDITOR_NAME_CAP];
            size_t name_len;
            if (!de) {
                break;
            }
            if (!has_prefix(de->d_name, "level_") || !has_suffix(de->d_name, ".cfg")) {
                continue;
            }
            name_len = strlen(de->d_name) - strlen(".cfg");
            if (name_len == 0 || name_len >= sizeof(candidate)) {
                continue;
            }
            memcpy(candidate, de->d_name, name_len);
            candidate[name_len] = '\0';
            if (!found || strcmp(candidate, best) < 0) {
                snprintf(best, sizeof(best), "%s", candidate);
                found = 1;
            }
        }
        closedir(d);
    }
    if (!found) {
        return 0;
    }
    snprintf(out, out_cap, "%s", best);
    return 1;
}

static int read_file_bytes(const char* path, char** out_data, size_t* out_size) {
    FILE* f = NULL;
    long len = 0;
    size_t nread = 0;
    char* data = NULL;
    if (!path || !out_data || !out_size) {
        return 0;
    }
    *out_data = NULL;
    *out_size = 0;
    f = fopen(path, "rb");
    if (!f) {
        return 0;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return 0;
    }
    len = ftell(f);
    if (len < 0) {
        fclose(f);
        return 0;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return 0;
    }
    data = (char*)malloc((size_t)len + 1u);
    if (!data) {
        fclose(f);
        return 0;
    }
    nread = fread(data, 1, (size_t)len, f);
    fclose(f);
    if (nread != (size_t)len) {
        free(data);
        return 0;
    }
    data[nread] = '\0';
    *out_data = data;
    *out_size = nread;
    return 1;
}

static int write_file_bytes(const char* path, const char* data, size_t size) {
    FILE* f = NULL;
    size_t n = 0;
    if (!path || !data) {
        return 0;
    }
    f = fopen(path, "wb");
    if (!f) {
        return 0;
    }
    n = fwrite(data, 1, size, f);
    fclose(f);
    return n == size;
}

static int cmp_int(const char* ctx, const char* field, int a, int b) {
    if (a != b) {
        fprintf(stderr, "%s: mismatch %s (%d vs %d)\n", ctx, field, a, b);
        return 0;
    }
    return 1;
}

static int cmp_float(const char* ctx, const char* field, float a, float b) {
    if (fabsf(a - b) > kFloatEps) {
        fprintf(stderr, "%s: mismatch %s (%.6f vs %.6f)\n", ctx, field, a, b);
        return 0;
    }
    return 1;
}

#define CMP_INT_FIELD(ctx, a, b, field) \
    do { if (!cmp_int((ctx), #field, (a)->field, (b)->field)) return 0; } while (0)

#define CMP_FLOAT_FIELD(ctx, a, b, field) \
    do { if (!cmp_float((ctx), #field, (a)->field, (b)->field)) return 0; } while (0)

static int compare_curated_combat(
    const char* ctx,
    const leveldef_curated_combat_tuning* a,
    const leveldef_curated_combat_tuning* b
) {
    if (!a || !b) {
        return 0;
    }
    if (!cmp_float(ctx, "curated.formation.fire_prob_mul", a->formation.fire_prob_mul, b->formation.fire_prob_mul)) return 0;
    if (!cmp_float(ctx, "curated.formation.cooldown_mul", a->formation.cooldown_mul, b->formation.cooldown_mul)) return 0;
    if (!cmp_int(ctx, "curated.formation.shot_count", a->formation.shot_count, b->formation.shot_count)) return 0;
    if (!cmp_float(ctx, "curated.formation.aim_error_mul", a->formation.aim_error_mul, b->formation.aim_error_mul)) return 0;
    if (!cmp_float(ctx, "curated.formation.projectile_speed_mul", a->formation.projectile_speed_mul, b->formation.projectile_speed_mul)) return 0;
    if (!cmp_float(ctx, "curated.formation.spread_mul", a->formation.spread_mul, b->formation.spread_mul)) return 0;

    if (!cmp_float(ctx, "curated.swarm.fire_prob_mul", a->swarm.fire_prob_mul, b->swarm.fire_prob_mul)) return 0;
    if (!cmp_float(ctx, "curated.swarm.spread_prob_mul", a->swarm.spread_prob_mul, b->swarm.spread_prob_mul)) return 0;
    if (!cmp_float(ctx, "curated.swarm.cooldown_mul", a->swarm.cooldown_mul, b->swarm.cooldown_mul)) return 0;
    if (!cmp_int(ctx, "curated.swarm.shot_count", a->swarm.shot_count, b->swarm.shot_count)) return 0;
    if (!cmp_float(ctx, "curated.swarm.aim_error_mul", a->swarm.aim_error_mul, b->swarm.aim_error_mul)) return 0;
    if (!cmp_float(ctx, "curated.swarm.projectile_speed_mul", a->swarm.projectile_speed_mul, b->swarm.projectile_speed_mul)) return 0;
    if (!cmp_float(ctx, "curated.swarm.spread_mul", a->swarm.spread_mul, b->swarm.spread_mul)) return 0;

    if (!cmp_float(ctx, "curated.kamikaze.fire_prob_mul", a->kamikaze.fire_prob_mul, b->kamikaze.fire_prob_mul)) return 0;
    if (!cmp_float(ctx, "curated.kamikaze.speed_mul", a->kamikaze.speed_mul, b->kamikaze.speed_mul)) return 0;
    if (!cmp_float(ctx, "curated.kamikaze.accel_mul", a->kamikaze.accel_mul, b->kamikaze.accel_mul)) return 0;

    if (!cmp_float(ctx, "curated.manta.fire_prob_mul", a->manta.fire_prob_mul, b->manta.fire_prob_mul)) return 0;
    if (!cmp_int(ctx, "curated.manta.missile_count_bonus", a->manta.missile_count_bonus, b->manta.missile_count_bonus)) return 0;
    if (!cmp_float(ctx, "curated.manta.missile_cooldown_mul", a->manta.missile_cooldown_mul, b->manta.missile_cooldown_mul)) return 0;
    if (!cmp_float(ctx, "curated.manta.missile_charge_mul", a->manta.missile_charge_mul, b->manta.missile_charge_mul)) return 0;

    if (!cmp_float(ctx, "curated.eel.fire_prob_mul", a->eel.fire_prob_mul, b->eel.fire_prob_mul)) return 0;
    if (!cmp_float(ctx, "curated.eel.arc_fire_rate_mul", a->eel.arc_fire_rate_mul, b->eel.arc_fire_rate_mul)) return 0;
    if (!cmp_float(ctx, "curated.eel.arc_duration_mul", a->eel.arc_duration_mul, b->eel.arc_duration_mul)) return 0;
    if (!cmp_float(ctx, "curated.eel.arc_range_mul", a->eel.arc_range_mul, b->eel.arc_range_mul)) return 0;
    if (!cmp_float(ctx, "curated.eel.arc_damage_interval_mul", a->eel.arc_damage_interval_mul, b->eel.arc_damage_interval_mul)) return 0;

    return 1;
}

static int compare_levels_semantic(const char* ctx, const leveldef_level* a, const leveldef_level* b) {
    int i;
    if (!a || !b) {
        fprintf(stderr, "%s: null level pointer\n", ctx);
        return 0;
    }

    CMP_FLOAT_FIELD(ctx, a, b, editor_length_screens);
    CMP_INT_FIELD(ctx, a, b, theme_palette);
    CMP_INT_FIELD(ctx, a, b, background_style);
    CMP_INT_FIELD(ctx, a, b, background_mask_style);
    CMP_FLOAT_FIELD(ctx, a, b, underwater_density);
    CMP_FLOAT_FIELD(ctx, a, b, underwater_caustic_strength);
    CMP_FLOAT_FIELD(ctx, a, b, underwater_caustic_scale);
    CMP_FLOAT_FIELD(ctx, a, b, underwater_bubble_rate);
    CMP_FLOAT_FIELD(ctx, a, b, underwater_haze_alpha);
    CMP_FLOAT_FIELD(ctx, a, b, underwater_current_speed);
    CMP_FLOAT_FIELD(ctx, a, b, underwater_palette_shift);
    CMP_FLOAT_FIELD(ctx, a, b, underwater_kelp_density);
    CMP_FLOAT_FIELD(ctx, a, b, underwater_kelp_sway_amp);
    CMP_FLOAT_FIELD(ctx, a, b, underwater_kelp_sway_speed);
    CMP_FLOAT_FIELD(ctx, a, b, underwater_kelp_height);
    CMP_FLOAT_FIELD(ctx, a, b, underwater_kelp_parallax_strength);
    CMP_FLOAT_FIELD(ctx, a, b, underwater_kelp_tint_r);
    CMP_FLOAT_FIELD(ctx, a, b, underwater_kelp_tint_g);
    CMP_FLOAT_FIELD(ctx, a, b, underwater_kelp_tint_b);
    CMP_FLOAT_FIELD(ctx, a, b, underwater_kelp_tint_strength);
    CMP_FLOAT_FIELD(ctx, a, b, fire_magma_scale);
    CMP_FLOAT_FIELD(ctx, a, b, fire_warp_amp);
    CMP_FLOAT_FIELD(ctx, a, b, fire_pulse_freq);
    CMP_FLOAT_FIELD(ctx, a, b, fire_plume_height);
    CMP_FLOAT_FIELD(ctx, a, b, fire_rise_speed);
    CMP_FLOAT_FIELD(ctx, a, b, fire_distortion_amp);
    CMP_FLOAT_FIELD(ctx, a, b, fire_smoke_alpha_cap);
    CMP_FLOAT_FIELD(ctx, a, b, fire_ember_spawn_rate);
    CMP_FLOAT_FIELD(ctx, a, b, ice_voronoi_scale);
    CMP_FLOAT_FIELD(ctx, a, b, ice_crack_width);
    CMP_FLOAT_FIELD(ctx, a, b, ice_distort_amp);
    CMP_FLOAT_FIELD(ctx, a, b, ice_parallax);
    CMP_FLOAT_FIELD(ctx, a, b, ice_shimmer);
    CMP_FLOAT_FIELD(ctx, a, b, ice_snow_density);
    CMP_FLOAT_FIELD(ctx, a, b, ice_snow_angle_deg);
    CMP_FLOAT_FIELD(ctx, a, b, ice_snow_speed);
    CMP_INT_FIELD(ctx, a, b, render_style);
    CMP_INT_FIELD(ctx, a, b, wave_mode);
    CMP_INT_FIELD(ctx, a, b, spawn_mode);
    CMP_FLOAT_FIELD(ctx, a, b, spawn_interval_s);
    CMP_INT_FIELD(ctx, a, b, default_boid_profile);
    CMP_FLOAT_FIELD(ctx, a, b, wave_cooldown_initial_s);
    CMP_FLOAT_FIELD(ctx, a, b, wave_cooldown_between_s);
    CMP_INT_FIELD(ctx, a, b, bidirectional_spawns);
    CMP_FLOAT_FIELD(ctx, a, b, cylinder_double_swarm_chance);
    CMP_FLOAT_FIELD(ctx, a, b, powerup_drop_chance);
    CMP_FLOAT_FIELD(ctx, a, b, powerup_double_shot_r);
    CMP_FLOAT_FIELD(ctx, a, b, powerup_double_shot_g);
    CMP_FLOAT_FIELD(ctx, a, b, powerup_double_shot_b);
    CMP_FLOAT_FIELD(ctx, a, b, powerup_triple_shot_r);
    CMP_FLOAT_FIELD(ctx, a, b, powerup_triple_shot_g);
    CMP_FLOAT_FIELD(ctx, a, b, powerup_triple_shot_b);
    CMP_FLOAT_FIELD(ctx, a, b, powerup_vitality_r);
    CMP_FLOAT_FIELD(ctx, a, b, powerup_vitality_g);
    CMP_FLOAT_FIELD(ctx, a, b, powerup_vitality_b);
    CMP_FLOAT_FIELD(ctx, a, b, powerup_orbital_boost_r);
    CMP_FLOAT_FIELD(ctx, a, b, powerup_orbital_boost_g);
    CMP_FLOAT_FIELD(ctx, a, b, powerup_orbital_boost_b);
    CMP_FLOAT_FIELD(ctx, a, b, powerup_magnet_r);
    CMP_FLOAT_FIELD(ctx, a, b, powerup_magnet_g);
    CMP_FLOAT_FIELD(ctx, a, b, powerup_magnet_b);
    CMP_INT_FIELD(ctx, a, b, exit_enabled);
    CMP_FLOAT_FIELD(ctx, a, b, exit_x01);
    CMP_FLOAT_FIELD(ctx, a, b, exit_y01);
    CMP_INT_FIELD(ctx, a, b, asteroid_storm_enabled);
    CMP_FLOAT_FIELD(ctx, a, b, asteroid_storm_start_x01);
    CMP_FLOAT_FIELD(ctx, a, b, asteroid_storm_angle_deg);
    CMP_FLOAT_FIELD(ctx, a, b, asteroid_storm_speed);
    CMP_FLOAT_FIELD(ctx, a, b, asteroid_storm_duration_s);
    CMP_FLOAT_FIELD(ctx, a, b, asteroid_storm_density);

    CMP_INT_FIELD(ctx, a, b, boid_cycle_count);
    for (i = 0; i < a->boid_cycle_count; ++i) {
        if (!cmp_int(ctx, "boid_cycle", a->boid_cycle[i], b->boid_cycle[i])) return 0;
    }
    CMP_INT_FIELD(ctx, a, b, wave_cycle_count);
    for (i = 0; i < a->wave_cycle_count; ++i) {
        if (!cmp_int(ctx, "wave_cycle", a->wave_cycle[i], b->wave_cycle[i])) return 0;
    }

    CMP_INT_FIELD(ctx, a, b, event_count);
    for (i = 0; i < a->event_count; ++i) {
        if (!cmp_int(ctx, "events.kind", a->events[i].kind, b->events[i].kind)) return 0;
        if (!cmp_int(ctx, "events.order", a->events[i].order, b->events[i].order)) return 0;
        if (!cmp_float(ctx, "events.delay_s", a->events[i].delay_s, b->events[i].delay_s)) return 0;
    }

    if (!compare_curated_combat(ctx, &a->curated_combat, &b->curated_combat)) {
        return 0;
    }

    CMP_INT_FIELD(ctx, a, b, curated_count);
    for (i = 0; i < a->curated_count; ++i) {
        if (!cmp_int(ctx, "curated.kind", a->curated[i].kind, b->curated[i].kind)) return 0;
        if (!cmp_float(ctx, "curated.x01", a->curated[i].x01, b->curated[i].x01)) return 0;
        if (!cmp_float(ctx, "curated.y01", a->curated[i].y01, b->curated[i].y01)) return 0;
        if (!cmp_float(ctx, "curated.a", a->curated[i].a, b->curated[i].a)) return 0;
        if (!cmp_float(ctx, "curated.b", a->curated[i].b, b->curated[i].b)) return 0;
        if (!cmp_float(ctx, "curated.c", a->curated[i].c, b->curated[i].c)) return 0;
        if (!cmp_float(ctx, "curated.d", a->curated[i].d, b->curated[i].d)) return 0;
        if (!cmp_float(ctx, "curated.e", a->curated[i].e, b->curated[i].e)) return 0;
    }

    CMP_INT_FIELD(ctx, a, b, searchlight_count);
    for (i = 0; i < a->searchlight_count; ++i) {
        if (!cmp_float(ctx, "searchlight.anchor_x01", a->searchlights[i].anchor_x01, b->searchlights[i].anchor_x01)) return 0;
        if (!cmp_float(ctx, "searchlight.anchor_y01", a->searchlights[i].anchor_y01, b->searchlights[i].anchor_y01)) return 0;
        if (!cmp_float(ctx, "searchlight.length_h01", a->searchlights[i].length_h01, b->searchlights[i].length_h01)) return 0;
        if (!cmp_float(ctx, "searchlight.half_angle_deg", a->searchlights[i].half_angle_deg, b->searchlights[i].half_angle_deg)) return 0;
        if (!cmp_float(ctx, "searchlight.sweep_center_deg", a->searchlights[i].sweep_center_deg, b->searchlights[i].sweep_center_deg)) return 0;
        if (!cmp_float(ctx, "searchlight.sweep_amplitude_deg", a->searchlights[i].sweep_amplitude_deg, b->searchlights[i].sweep_amplitude_deg)) return 0;
        if (!cmp_float(ctx, "searchlight.sweep_speed", a->searchlights[i].sweep_speed, b->searchlights[i].sweep_speed)) return 0;
        if (!cmp_float(ctx, "searchlight.sweep_phase_deg", a->searchlights[i].sweep_phase_deg, b->searchlights[i].sweep_phase_deg)) return 0;
        if (!cmp_int(ctx, "searchlight.sweep_motion", a->searchlights[i].sweep_motion, b->searchlights[i].sweep_motion)) return 0;
        if (!cmp_int(ctx, "searchlight.source_type", a->searchlights[i].source_type, b->searchlights[i].source_type)) return 0;
        if (!cmp_float(ctx, "searchlight.source_radius", a->searchlights[i].source_radius, b->searchlights[i].source_radius)) return 0;
        if (!cmp_float(ctx, "searchlight.clear_grace_s", a->searchlights[i].clear_grace_s, b->searchlights[i].clear_grace_s)) return 0;
        if (!cmp_float(ctx, "searchlight.fire_interval_s", a->searchlights[i].fire_interval_s, b->searchlights[i].fire_interval_s)) return 0;
        if (!cmp_float(ctx, "searchlight.projectile_speed", a->searchlights[i].projectile_speed, b->searchlights[i].projectile_speed)) return 0;
        if (!cmp_float(ctx, "searchlight.projectile_ttl_s", a->searchlights[i].projectile_ttl_s, b->searchlights[i].projectile_ttl_s)) return 0;
        if (!cmp_float(ctx, "searchlight.projectile_radius", a->searchlights[i].projectile_radius, b->searchlights[i].projectile_radius)) return 0;
        if (!cmp_float(ctx, "searchlight.aim_jitter_deg", a->searchlights[i].aim_jitter_deg, b->searchlights[i].aim_jitter_deg)) return 0;
    }

    CMP_INT_FIELD(ctx, a, b, minefield_count);
    for (i = 0; i < a->minefield_count; ++i) {
        if (!cmp_float(ctx, "minefield.anchor_x01", a->minefields[i].anchor_x01, b->minefields[i].anchor_x01)) return 0;
        if (!cmp_float(ctx, "minefield.anchor_y01", a->minefields[i].anchor_y01, b->minefields[i].anchor_y01)) return 0;
        if (!cmp_int(ctx, "minefield.count", a->minefields[i].count, b->minefields[i].count)) return 0;
        if (!cmp_int(ctx, "minefield.style", a->minefields[i].style, b->minefields[i].style)) return 0;
    }

    CMP_INT_FIELD(ctx, a, b, missile_launcher_count);
    for (i = 0; i < a->missile_launcher_count; ++i) {
        if (!cmp_float(ctx, "missile.anchor_x01", a->missile_launchers[i].anchor_x01, b->missile_launchers[i].anchor_x01)) return 0;
        if (!cmp_float(ctx, "missile.anchor_y01", a->missile_launchers[i].anchor_y01, b->missile_launchers[i].anchor_y01)) return 0;
        if (!cmp_int(ctx, "missile.count", a->missile_launchers[i].count, b->missile_launchers[i].count)) return 0;
        if (!cmp_float(ctx, "missile.spacing", a->missile_launchers[i].spacing, b->missile_launchers[i].spacing)) return 0;
        if (!cmp_float(ctx, "missile.activation_range", a->missile_launchers[i].activation_range, b->missile_launchers[i].activation_range)) return 0;
        if (!cmp_float(ctx, "missile.missile_speed", a->missile_launchers[i].missile_speed, b->missile_launchers[i].missile_speed)) return 0;
        if (!cmp_float(ctx, "missile.missile_turn_rate_deg", a->missile_launchers[i].missile_turn_rate_deg, b->missile_launchers[i].missile_turn_rate_deg)) return 0;
        if (!cmp_float(ctx, "missile.missile_ttl_s", a->missile_launchers[i].missile_ttl_s, b->missile_launchers[i].missile_ttl_s)) return 0;
        if (!cmp_float(ctx, "missile.hit_radius", a->missile_launchers[i].hit_radius, b->missile_launchers[i].hit_radius)) return 0;
        if (!cmp_float(ctx, "missile.blast_radius", a->missile_launchers[i].blast_radius, b->missile_launchers[i].blast_radius)) return 0;
    }

    CMP_INT_FIELD(ctx, a, b, arc_node_count);
    for (i = 0; i < a->arc_node_count; ++i) {
        if (!cmp_float(ctx, "arc.anchor_x01", a->arc_nodes[i].anchor_x01, b->arc_nodes[i].anchor_x01)) return 0;
        if (!cmp_float(ctx, "arc.anchor_y01", a->arc_nodes[i].anchor_y01, b->arc_nodes[i].anchor_y01)) return 0;
        if (!cmp_float(ctx, "arc.period_s", a->arc_nodes[i].period_s, b->arc_nodes[i].period_s)) return 0;
        if (!cmp_float(ctx, "arc.on_s", a->arc_nodes[i].on_s, b->arc_nodes[i].on_s)) return 0;
        if (!cmp_float(ctx, "arc.radius", a->arc_nodes[i].radius, b->arc_nodes[i].radius)) return 0;
        if (!cmp_float(ctx, "arc.push_accel", a->arc_nodes[i].push_accel, b->arc_nodes[i].push_accel)) return 0;
        if (!cmp_float(ctx, "arc.damage_interval_s", a->arc_nodes[i].damage_interval_s, b->arc_nodes[i].damage_interval_s)) return 0;
    }

    CMP_INT_FIELD(ctx, a, b, window_mask_count);
    for (i = 0; i < a->window_mask_count; ++i) {
        if (!cmp_float(ctx, "window.anchor_x01", a->window_masks[i].anchor_x01, b->window_masks[i].anchor_x01)) return 0;
        if (!cmp_float(ctx, "window.anchor_y01", a->window_masks[i].anchor_y01, b->window_masks[i].anchor_y01)) return 0;
        if (!cmp_float(ctx, "window.width_h01", a->window_masks[i].width_h01, b->window_masks[i].width_h01)) return 0;
        if (!cmp_float(ctx, "window.height_v01", a->window_masks[i].height_v01, b->window_masks[i].height_v01)) return 0;
        if (!cmp_int(ctx, "window.flip_vertical", a->window_masks[i].flip_vertical, b->window_masks[i].flip_vertical)) return 0;
    }

    CMP_INT_FIELD(ctx, a, b, structure_count);
    for (i = 0; i < a->structure_count; ++i) {
        if (!cmp_int(ctx, "structure.prefab_id", a->structures[i].prefab_id, b->structures[i].prefab_id)) return 0;
        if (!cmp_int(ctx, "structure.layer", a->structures[i].layer, b->structures[i].layer)) return 0;
        if (!cmp_int(ctx, "structure.grid_x", a->structures[i].grid_x, b->structures[i].grid_x)) return 0;
        if (!cmp_int(ctx, "structure.grid_y", a->structures[i].grid_y, b->structures[i].grid_y)) return 0;
        if (!cmp_int(ctx, "structure.rotation_quadrants", a->structures[i].rotation_quadrants, b->structures[i].rotation_quadrants)) return 0;
        if (!cmp_int(ctx, "structure.flip_x", a->structures[i].flip_x, b->structures[i].flip_x)) return 0;
        if (!cmp_int(ctx, "structure.flip_y", a->structures[i].flip_y, b->structures[i].flip_y)) return 0;
        if (!cmp_int(ctx, "structure.w_units", a->structures[i].w_units, b->structures[i].w_units)) return 0;
        if (!cmp_int(ctx, "structure.h_units", a->structures[i].h_units, b->structures[i].h_units)) return 0;
        if (!cmp_int(ctx, "structure.variant", a->structures[i].variant, b->structures[i].variant)) return 0;
        if (!cmp_float(ctx, "structure.vent_density", a->structures[i].vent_density, b->structures[i].vent_density)) return 0;
        if (!cmp_float(ctx, "structure.vent_opacity", a->structures[i].vent_opacity, b->structures[i].vent_opacity)) return 0;
        if (!cmp_float(ctx, "structure.vent_plume_height", a->structures[i].vent_plume_height, b->structures[i].vent_plume_height)) return 0;
    }

    CMP_INT_FIELD(ctx, a, b, sine.count);
    CMP_FLOAT_FIELD(ctx, a, b, sine.start_x01);
    CMP_FLOAT_FIELD(ctx, a, b, sine.spacing_x);
    CMP_FLOAT_FIELD(ctx, a, b, sine.home_y01);
    CMP_FLOAT_FIELD(ctx, a, b, sine.phase_step);
    CMP_FLOAT_FIELD(ctx, a, b, sine.form_amp);
    CMP_FLOAT_FIELD(ctx, a, b, sine.form_freq);
    CMP_FLOAT_FIELD(ctx, a, b, sine.break_delay_base);
    CMP_FLOAT_FIELD(ctx, a, b, sine.break_delay_step);
    CMP_FLOAT_FIELD(ctx, a, b, sine.max_speed);
    CMP_FLOAT_FIELD(ctx, a, b, sine.accel);

    CMP_INT_FIELD(ctx, a, b, v.count);
    CMP_FLOAT_FIELD(ctx, a, b, v.start_x01);
    CMP_FLOAT_FIELD(ctx, a, b, v.spacing_x);
    CMP_FLOAT_FIELD(ctx, a, b, v.home_y01);
    CMP_FLOAT_FIELD(ctx, a, b, v.home_y_step);
    CMP_FLOAT_FIELD(ctx, a, b, v.phase_step);
    CMP_FLOAT_FIELD(ctx, a, b, v.form_amp);
    CMP_FLOAT_FIELD(ctx, a, b, v.form_freq);
    CMP_FLOAT_FIELD(ctx, a, b, v.break_delay_min);
    CMP_FLOAT_FIELD(ctx, a, b, v.break_delay_rand);
    CMP_FLOAT_FIELD(ctx, a, b, v.max_speed);
    CMP_FLOAT_FIELD(ctx, a, b, v.accel);

    CMP_INT_FIELD(ctx, a, b, kamikaze.count);
    CMP_FLOAT_FIELD(ctx, a, b, kamikaze.start_x01);
    CMP_FLOAT_FIELD(ctx, a, b, kamikaze.spacing_x);
    CMP_FLOAT_FIELD(ctx, a, b, kamikaze.y_margin);
    CMP_FLOAT_FIELD(ctx, a, b, kamikaze.max_speed);
    CMP_FLOAT_FIELD(ctx, a, b, kamikaze.accel);
    CMP_FLOAT_FIELD(ctx, a, b, kamikaze.radius_min);
    CMP_FLOAT_FIELD(ctx, a, b, kamikaze.radius_max);
    CMP_INT_FIELD(ctx, a, b, kamikaze.style);

    return 1;
}

static int count_selected_properties(level_editor_state* s) {
    int n = 1;
    if (!s) {
        return 0;
    }
    s->selected_property = LEVEL_EDITOR_PROP_FIRST;
    level_editor_select_property(s, 1);
    while (s->selected_property != LEVEL_EDITOR_PROP_FIRST && n < 64) {
        n += 1;
        level_editor_select_property(s, 1);
    }
    if (n >= 64) {
        return 0;
    }
    return n;
}

static int verify_curated_property_adjustments(void) {
    level_editor_state editor;
    level_editor_marker* m = NULL;

    level_editor_init(&editor);
    editor.level_wave_mode = LEVELDEF_WAVES_CURATED;
    editor.marker_count = 1;
    editor.selected_marker = 0;
    editor.selected_property = LEVEL_EDITOR_WAVE_PROP_TYPE;

    m = &editor.markers[0];
    memset(m, 0, sizeof(*m));
    m->track = LEVEL_EDITOR_TRACK_SPATIAL;
    m->x01 = 0.50f;
    m->y01 = 0.50f;
    m->a = 8.0f;
    m->b = 250.0f;
    m->c = 7.0f;

    m->kind = LEVEL_EDITOR_MARKER_WAVE_SINE;
    if (count_selected_properties(&editor) != 13) {
        fprintf(stderr, "roundtrip: curated sine property count mismatch\n");
        return 0;
    }
    editor.selected_property = LEVEL_EDITOR_WAVE_SINE_PROP_FORMATION_FIRE_PROB_MUL;
    level_editor_adjust_selected_property(&editor, 1.0f);
    if (fabsf(editor.level_curated_combat.formation.fire_prob_mul - 1.05f) > 1.0e-6f) {
        fprintf(stderr, "roundtrip: sine fire_prob multiplier mapping failed\n");
        return 0;
    }
    editor.selected_property = LEVEL_EDITOR_WAVE_SINE_PROP_FORMATION_SHOT_COUNT;
    level_editor_adjust_selected_property(&editor, 1.0f);
    if (editor.level_curated_combat.formation.shot_count != 1) {
        fprintf(stderr, "roundtrip: sine shot_count mapping failed\n");
        return 0;
    }

    m->kind = LEVEL_EDITOR_MARKER_BOID_FISH;
    if (count_selected_properties(&editor) != 16) {
        fprintf(stderr, "roundtrip: curated swarm property count mismatch\n");
        return 0;
    }
    editor.selected_property = LEVEL_EDITOR_BOID_PROP_SIZE_SCALE;
    m->e = 1.0f;
    level_editor_adjust_selected_property(&editor, 1.0f);
    if (fabsf(m->e - 1.05f) > 1.0e-6f) {
        fprintf(stderr, "roundtrip: swarm size-scale mapping failed\n");
        return 0;
    }
    editor.selected_property = LEVEL_EDITOR_BOID_PROP_SWARM_FIRE_PROB_MUL;
    level_editor_adjust_selected_property(&editor, 1.0f);
    if (fabsf(editor.level_curated_combat.swarm.fire_prob_mul - 1.05f) > 1.0e-6f) {
        fprintf(stderr, "roundtrip: swarm fire_prob multiplier mapping failed\n");
        return 0;
    }
    editor.selected_property = LEVEL_EDITOR_BOID_PROP_SWARM_SHOT_COUNT;
    level_editor_adjust_selected_property(&editor, 1.0f);
    if (editor.level_curated_combat.swarm.shot_count != 1) {
        fprintf(stderr, "roundtrip: swarm shot_count mapping failed\n");
        return 0;
    }

    m->kind = LEVEL_EDITOR_MARKER_WAVE_KAMIKAZE;
    if (count_selected_properties(&editor) != 13) {
        fprintf(stderr, "roundtrip: curated kamikaze property count mismatch\n");
        return 0;
    }
    editor.selected_property = LEVEL_EDITOR_WAVE_KAMIKAZE_PROP_FIRE_PROB_MUL;
    level_editor_adjust_selected_property(&editor, 1.0f);
    if (fabsf(editor.level_curated_combat.kamikaze.fire_prob_mul - 1.05f) > 1.0e-6f) {
        fprintf(stderr, "roundtrip: kamikaze fire_prob multiplier mapping failed\n");
        return 0;
    }

    m->kind = LEVEL_EDITOR_MARKER_MANTA_WING;
    if (count_selected_properties(&editor) != 11) {
        fprintf(stderr, "roundtrip: curated manta property count mismatch\n");
        return 0;
    }
    editor.selected_property = LEVEL_EDITOR_MANTA_WING_PROP_MISSILE_COUNT_BONUS;
    level_editor_adjust_selected_property(&editor, 1.0f);
    if (editor.level_curated_combat.manta.missile_count_bonus != 1) {
        fprintf(stderr, "roundtrip: manta missile bonus mapping failed\n");
        return 0;
    }

    m->kind = LEVEL_EDITOR_MARKER_EEL_SWARM;
    if (count_selected_properties(&editor) != 13) {
        fprintf(stderr, "roundtrip: curated eel property count mismatch\n");
        return 0;
    }
    editor.selected_property = LEVEL_EDITOR_EEL_SWARM_PROP_ARC_FIRE_RATE_MUL;
    level_editor_adjust_selected_property(&editor, 1.0f);
    if (fabsf(editor.level_curated_combat.eel.arc_fire_rate_mul - 1.05f) > 1.0e-6f) {
        fprintf(stderr, "roundtrip: eel arc rate multiplier mapping failed\n");
        return 0;
    }

    return 1;
}

static int verify_wave_type_remap_semantics(void) {
    level_editor_state editor;
    level_editor_marker* m = NULL;

    level_editor_init(&editor);
    editor.level_wave_mode = LEVELDEF_WAVES_CURATED;
    editor.marker_count = 1;
    editor.selected_marker = 0;
    editor.selected_property = LEVEL_EDITOR_WAVE_PROP_TYPE;

    m = &editor.markers[0];
    memset(m, 0, sizeof(*m));
    m->track = LEVEL_EDITOR_TRACK_SPATIAL;
    m->x01 = 0.50f;
    m->y01 = 0.50f;

    m->kind = LEVEL_EDITOR_MARKER_MANTA_WING;
    m->a = 2.0f;
    m->b = 4.0f;
    m->c = 2.0f;
    m->d = 0.0f;
    level_editor_adjust_selected_property(&editor, 1.0f);
    if (m->kind != LEVEL_EDITOR_MARKER_WAVE_KAMIKAZE) {
        fprintf(stderr, "roundtrip: wave remap failed to cycle manta->kamikaze\n");
        return 0;
    }
    if (m->b < 100.0f || m->c < 1.0f) {
        fprintf(stderr, "roundtrip: wave remap kept incompatible manta params for kamikaze (b=%.3f c=%.3f)\n", m->b, m->c);
        return 0;
    }

    m->kind = LEVEL_EDITOR_MARKER_BOID_BIRD;
    m->a = 12.0f;
    m->b = 300.0f;
    m->c = 7.8f;
    m->d = 440.0f;
    level_editor_adjust_selected_property(&editor, 1.0f);
    if (m->kind != LEVEL_EDITOR_MARKER_JELLY_SWARM) {
        fprintf(stderr, "roundtrip: wave remap failed to cycle bird->jelly\n");
        return 0;
    }
    if (m->d > 4.0f) {
        fprintf(stderr, "roundtrip: wave remap kept incompatible turn-rate as jelly size (d=%.3f)\n", m->d);
        return 0;
    }
    level_editor_adjust_selected_property(&editor, 1.0f);
    if (m->kind != LEVEL_EDITOR_MARKER_EEL_SWARM) {
        fprintf(stderr, "roundtrip: wave remap failed to cycle jelly->eel\n");
        return 0;
    }
    if (m->d <= 0.0f || m->d > 4.0f) {
        fprintf(stderr, "roundtrip: wave remap produced invalid eel size scale (d=%.3f)\n", m->d);
        return 0;
    }

    return 1;
}

static int verify_exit_portal_tool_placement(void) {
    level_editor_state editor;
    level_editor_layout layout;
    const float w = 1280.0f;
    const float h = 720.0f;

    level_editor_init(&editor);
    level_editor_compute_layout(w, h, &layout);

    editor.level_background_mask_style = LEVELDEF_BG_MASK_WINDOWS;
    editor.level_wave_mode = LEVELDEF_WAVES_CURATED;

    {
        float bx = layout.exit_button.x + layout.exit_button.w * 0.5f;
        float by = layout.exit_button.y + layout.exit_button.h * 0.5f;
        float vx = layout.viewport.x + layout.viewport.w * 0.5f;
        float vy = layout.viewport.y + layout.viewport.h * 0.5f;
        if (!level_editor_handle_mouse(&editor, bx, by, w, h, 1, 1)) {
            fprintf(stderr, "roundtrip: exit button click was not handled\n");
            return 0;
        }
        if (!editor.entity_drag_active || editor.entity_drag_kind != LEVEL_EDITOR_MARKER_EXIT) {
            fprintf(stderr, "roundtrip: exit button did not arm exit drag tool\n");
            return 0;
        }
        if (!level_editor_handle_mouse_release(&editor, vx, vy, w, h)) {
            fprintf(stderr, "roundtrip: exit drag release did not place marker\n");
            return 0;
        }
    }

    if (editor.marker_count != 1 || editor.markers[0].kind != LEVEL_EDITOR_MARKER_EXIT) {
        fprintf(stderr, "roundtrip: exit tool placement failed (count=%d kind=%d)\n",
                editor.marker_count,
                (editor.marker_count > 0) ? editor.markers[0].kind : -1);
        return 0;
    }
    if (editor.entity_tool_selected != LEVEL_EDITOR_TOOL_NONE) {
        fprintf(stderr, "roundtrip: exit tool was not cleared after placement\n");
        return 0;
    }
    return 1;
}

static int verify_level_semantic_roundtrip(const leveldef_db* db) {
    level_editor_state editor;
    leveldef_level expected_level;
    leveldef_level reloaded_level;
    leveldef_level baseline_level;
    char* original_bytes = NULL;
    size_t original_size = 0;
    char path[LEVEL_EDITOR_PATH_CAP];
    char saved_path[LEVEL_EDITOR_PATH_CAP];
    char level_name[LEVEL_EDITOR_NAME_CAP];
    char roundtrip_level_name[LEVEL_EDITOR_NAME_CAP];
    int ok = 0;

    if (!db) {
        return 0;
    }
    if (!pick_roundtrip_level_name(roundtrip_level_name, sizeof(roundtrip_level_name))) {
        fprintf(stderr, "roundtrip: no level_*.cfg found; semantic roundtrip skipped\n");
        return 1;
    }

    level_editor_init(&editor);
    if (!level_editor_load_by_name(&editor, db, roundtrip_level_name)) {
        fprintf(stderr, "roundtrip: level load failed (%s)\n", roundtrip_level_name);
        return 0;
    }
    if (editor.source_path[0] == '\0') {
        fprintf(stderr, "roundtrip: source path unresolved\n");
        return 0;
    }

    snprintf(path, sizeof(path), "%s", editor.source_path);
    snprintf(level_name, sizeof(level_name), "%s", editor.level_name);

    if (!read_file_bytes(path, &original_bytes, &original_size)) {
        fprintf(stderr, "roundtrip: read backup failed (%s)\n", path);
        return 0;
    }

    if (!level_editor_build_level(&editor, db, &expected_level)) {
        fprintf(stderr, "roundtrip: build expected baseline failed\n");
        goto cleanup;
    }
    baseline_level = expected_level;

    if (!level_editor_save_current(&editor, db, saved_path, sizeof(saved_path))) {
        fprintf(stderr, "roundtrip: save baseline failed (%s)\n", editor.status_text);
        goto cleanup;
    }
    if (strcmp(saved_path, path) != 0) {
        fprintf(stderr, "roundtrip: baseline saved path mismatch\n");
        goto cleanup;
    }

    if (!level_editor_load_by_name(&editor, db, level_name)) {
        fprintf(stderr, "roundtrip: reload baseline failed\n");
        goto cleanup;
    }
    if (!level_editor_build_level(&editor, db, &reloaded_level)) {
        fprintf(stderr, "roundtrip: build baseline after reload failed\n");
        goto cleanup;
    }
    if (!compare_levels_semantic("roundtrip baseline", &expected_level, &reloaded_level)) {
        goto cleanup;
    }

    editor.level_wave_mode = LEVELDEF_WAVES_CURATED;
    editor.marker_count = 1;
    editor.selected_marker = 0;
    editor.selected_property = LEVEL_EDITOR_WAVE_PROP_TYPE;
    memset(&editor.markers[0], 0, sizeof(editor.markers[0]));
    editor.markers[0].kind = LEVEL_EDITOR_MARKER_WAVE_SINE;
    editor.markers[0].track = LEVEL_EDITOR_TRACK_SPATIAL;
    editor.markers[0].order = 1;
    editor.markers[0].x01 = 0.35f;
    editor.markers[0].y01 = 0.42f;
    editor.markers[0].a = 9.0f;
    editor.markers[0].b = 84.0f;
    editor.markers[0].c = 280.0f;

    editor.level_curated_combat.formation.fire_prob_mul = 0.25f;
    editor.level_curated_combat.formation.cooldown_mul = 1.70f;
    editor.level_curated_combat.formation.shot_count = 3;
    editor.level_curated_combat.formation.aim_error_mul = 0.80f;
    editor.level_curated_combat.formation.projectile_speed_mul = 1.20f;
    editor.level_curated_combat.formation.spread_mul = 1.10f;

    editor.level_curated_combat.swarm.fire_prob_mul = 0.40f;
    editor.level_curated_combat.swarm.spread_prob_mul = 1.60f;
    editor.level_curated_combat.swarm.cooldown_mul = 1.30f;
    editor.level_curated_combat.swarm.shot_count = 2;
    editor.level_curated_combat.swarm.aim_error_mul = 0.90f;
    editor.level_curated_combat.swarm.projectile_speed_mul = 1.15f;
    editor.level_curated_combat.swarm.spread_mul = 1.35f;

    editor.level_curated_combat.kamikaze.fire_prob_mul = 0.0f;
    editor.level_curated_combat.kamikaze.speed_mul = 1.25f;
    editor.level_curated_combat.kamikaze.accel_mul = 1.50f;

    editor.level_curated_combat.manta.fire_prob_mul = 0.70f;
    editor.level_curated_combat.manta.missile_count_bonus = 2;
    editor.level_curated_combat.manta.missile_cooldown_mul = 0.80f;
    editor.level_curated_combat.manta.missile_charge_mul = 1.40f;

    editor.level_curated_combat.eel.fire_prob_mul = 0.60f;
    editor.level_curated_combat.eel.arc_fire_rate_mul = 1.35f;
    editor.level_curated_combat.eel.arc_duration_mul = 0.75f;
    editor.level_curated_combat.eel.arc_range_mul = 1.10f;
    editor.level_curated_combat.eel.arc_damage_interval_mul = 0.85f;

    if (!level_editor_build_level(&editor, db, &expected_level)) {
        fprintf(stderr, "roundtrip: build expected curated failed\n");
        goto cleanup;
    }

    if (!level_editor_save_current(&editor, db, saved_path, sizeof(saved_path))) {
        fprintf(stderr, "roundtrip: save curated failed (%s)\n", editor.status_text);
        goto cleanup;
    }
    if (!level_editor_load_by_name(&editor, db, level_name)) {
        fprintf(stderr, "roundtrip: reload curated failed\n");
        goto cleanup;
    }
    if (!level_editor_build_level(&editor, db, &reloaded_level)) {
        fprintf(stderr, "roundtrip: build curated after reload failed\n");
        goto cleanup;
    }
    if (!compare_levels_semantic("roundtrip curated", &expected_level, &reloaded_level)) {
        goto cleanup;
    }

    editor.selected_marker = -1;
    editor.selected_property = LEVEL_EDITOR_LEVEL_PROP_POWERUP_DROP;
    level_editor_adjust_selected_property(&editor, 1.0f);
    if (!level_editor_build_level(&editor, db, &expected_level)) {
        fprintf(stderr, "roundtrip: build expected modified failed\n");
        goto cleanup;
    }
    if (expected_level.powerup_drop_chance <= baseline_level.powerup_drop_chance) {
        fprintf(stderr, "roundtrip: powerup_drop_chance did not increase after editor adjustment\n");
        goto cleanup;
    }
    if (!level_editor_save_current(&editor, db, saved_path, sizeof(saved_path))) {
        fprintf(stderr, "roundtrip: save modified failed (%s)\n", editor.status_text);
        goto cleanup;
    }
    if (!level_editor_load_by_name(&editor, db, level_name)) {
        fprintf(stderr, "roundtrip: reload modified failed\n");
        goto cleanup;
    }
    if (!level_editor_build_level(&editor, db, &reloaded_level)) {
        fprintf(stderr, "roundtrip: build modified after reload failed\n");
        goto cleanup;
    }
    if (!compare_levels_semantic("roundtrip modified", &expected_level, &reloaded_level)) {
        goto cleanup;
    }

    ok = 1;

cleanup:
    if (original_bytes && path[0] != '\0') {
        if (!write_file_bytes(path, original_bytes, original_size)) {
            fprintf(stderr, "roundtrip: restore original failed (%s)\n", path);
            ok = 0;
        }
    }
    free(original_bytes);
    return ok;
}

int main(void) {
    leveldef_db db;

    if (chdir(VTYPE_SOURCE_DIR) != 0) {
        fprintf(stderr, "roundtrip: chdir to source root failed\n");
        return 1;
    }
    if (!leveldef_load_project_layout(&db, "data/levels", stderr)) {
        fprintf(stderr, "roundtrip: failed to load leveldef db\n");
        return 1;
    }
    if (!verify_wave_type_remap_semantics()) {
        return 1;
    }
    if (!verify_exit_portal_tool_placement()) {
        return 1;
    }
    if (!verify_curated_property_adjustments()) {
        return 1;
    }
    if (!verify_level_semantic_roundtrip(&db)) {
        return 1;
    }

    return 0;
}
