#include "level_editor.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
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

static int is_wave_kind(int kind) {
    return (kind == LEVEL_EDITOR_MARKER_WAVE_SINE ||
            kind == LEVEL_EDITOR_MARKER_WAVE_V ||
            kind == LEVEL_EDITOR_MARKER_WAVE_KAMIKAZE ||
            kind == LEVEL_EDITOR_MARKER_BOID);
}

static int marker_property_count(const level_editor_state* s) {
    if (!s || s->selected_marker < 0 || s->selected_marker >= s->marker_count) {
        return 0;
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
        out->timeline.y + out->timeline.h * 0.32f,
        out->timeline.w - 28.0f * ui,
        out->timeline.h * 0.40f
    };
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

    if (lvl->wave_mode == LEVELDEF_WAVES_BOID_ONLY) {
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
    snprintf(s->level_name, sizeof(s->level_name), "%s", level_style_name(s->level_style));
    snprintf(s->status_text, sizeof(s->status_text), "ready");
    s->entry_active = 1;
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
    snprintf(s->level_name, sizeof(s->level_name), "%s", level_style_name(style));
    s->timeline_01 = 0.0f;

    {
        const leveldef_level* lvl = leveldef_get_level(db, style);
        const int cycle_n = lvl ? ((lvl->wave_mode == LEVELDEF_WAVES_BOID_ONLY) ? lvl->boid_cycle_count : lvl->wave_cycle_count) : 0;
        s->level_length_screens = fmaxf(8.0f, 6.0f + (float)cycle_n * 1.2f);
    }
    build_markers(s, db, style);
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
        if (point_in_rect(mouse_x, mouse_y, l.save_button)) {
            s->entry_active = 0;
            return 3;
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
            if (best >= 0 && best_d2 < 0.006f) {
                s->selected_marker = best;
            } else {
                if (s->entity_tool_selected == LEVEL_EDITOR_MARKER_BOID || s->entity_tool_selected == LEVEL_EDITOR_MARKER_SEARCHLIGHT) {
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
    if (!s || s->selected_marker < 0 || s->selected_marker >= s->marker_count || delta == 0.0f) {
        return;
    }
    level_editor_marker* m = &s->markers[s->selected_marker];
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

    if (m->kind == LEVEL_EDITOR_MARKER_SEARCHLIGHT) {
        switch (s->selected_property) {
            case 0: {
                const float old_len = fmaxf(s->level_length_screens, 1.0f);
                float abs_x = m->x01 * old_len + delta * old_len;
                if (abs_x < 0.0f) {
                    abs_x = 0.0f;
                }
                float new_len = old_len;
                if (abs_x > old_len) {
                    new_len = ceilf(abs_x + 0.25f);
                }
                remap_level_length(s, new_len, s->selected_marker, abs_x);
            } break;
            case 1: m->y01 = clampf(m->y01 + delta, 0.0f, 1.0f); break;
            case 2: m->a += delta; break;
            case 3: m->b += delta * 20.0f; break;
            case 4: m->c += delta * 5.0f; break;
            case 5: m->d += delta * 20.0f; break;
            default: break;
        }
        return;
    }

    if (m->kind == LEVEL_EDITOR_MARKER_EXIT) {
        switch (s->selected_property) {
            case 0: {
                const float old_len = fmaxf(s->level_length_screens, 1.0f);
                float abs_x = m->x01 * old_len + delta * old_len;
                if (abs_x < 0.0f) {
                    abs_x = 0.0f;
                }
                float new_len = old_len;
                if (abs_x > old_len) {
                    new_len = ceilf(abs_x + 0.25f);
                }
                remap_level_length(s, new_len, s->selected_marker, abs_x);
            } break;
            case 1: m->y01 = clampf(m->y01 + delta, 0.0f, 1.0f); break;
            default: break;
        }
        return;
    }

    if (is_wave_kind(m->kind)) {
        switch (s->selected_property) {
            case 0:
                m->kind = cycle_wave_kind(m->kind, (delta >= 0.0f) ? 1 : -1);
                break;
            case 1: {
                const float old_len = fmaxf(s->level_length_screens, 1.0f);
                float abs_x = m->x01 * old_len + delta * old_len;
                if (abs_x < 0.0f) {
                    abs_x = 0.0f;
                }
                float new_len = old_len;
                if (abs_x > old_len) {
                    new_len = ceilf(abs_x + 0.25f);
                }
                remap_level_length(s, new_len, s->selected_marker, abs_x);
            } break;
            case 2: m->y01 = clampf(m->y01 + delta, 0.0f, 1.0f); break;
            case 3: m->a += delta * 80.0f; break;
            case 4: m->b += delta * 30.0f; break;
            case 5: m->c += delta * 30.0f; break;
            default: break;
        }
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
