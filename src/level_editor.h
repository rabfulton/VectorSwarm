#ifndef V_TYPE_LEVEL_EDITOR_H
#define V_TYPE_LEVEL_EDITOR_H

#include "leveldef.h"
#include "vg.h"

#define LEVEL_EDITOR_MAX_MARKERS 256
#define LEVEL_EDITOR_NAME_CAP 64
#define LEVEL_EDITOR_STATUS_CAP 128
#define LEVEL_EDITOR_PATH_CAP 512

enum level_editor_marker_kind {
    LEVEL_EDITOR_MARKER_EXIT = 0,
    LEVEL_EDITOR_MARKER_SEARCHLIGHT = 1,
    LEVEL_EDITOR_MARKER_WAVE_SINE = 2,
    LEVEL_EDITOR_MARKER_WAVE_V = 3,
    LEVEL_EDITOR_MARKER_WAVE_KAMIKAZE = 4,
    LEVEL_EDITOR_MARKER_BOID = 5,
    LEVEL_EDITOR_MARKER_BOID_FISH = 10,
    LEVEL_EDITOR_MARKER_BOID_FIREFLY = 11,
    LEVEL_EDITOR_MARKER_BOID_BIRD = 12,
    LEVEL_EDITOR_MARKER_JELLY_SWARM = 15,
    LEVEL_EDITOR_MARKER_ASTEROID_STORM = 6,
    LEVEL_EDITOR_MARKER_MINEFIELD = 7,
    LEVEL_EDITOR_MARKER_MISSILE = 8,
    LEVEL_EDITOR_MARKER_STRUCTURE = 9,
    LEVEL_EDITOR_MARKER_ARC_NODE = 13,
    LEVEL_EDITOR_MARKER_WINDOW_MASK = 14
};

enum level_editor_marker_track {
    LEVEL_EDITOR_TRACK_SPATIAL = 0,
    LEVEL_EDITOR_TRACK_EVENT = 1
};

typedef struct level_editor_marker {
    int kind;
    int track; /* enum level_editor_marker_track */
    int order; /* 1-based order for event-track items */
    float delay_s; /* event delay after previous event ends */
    float x01; /* normalized over full level length */
    float y01; /* normalized screen-space anchor */
    float a;
    float b;
    float c;
    float d;
    float e;
    float f;
    float g;
} level_editor_marker;

typedef struct level_editor_layout {
    vg_rect viewport;
    vg_rect construction_toolbar;
    vg_rect properties;
    vg_rect entities;
    vg_rect timeline;
    vg_rect timeline_track;
    vg_rect timeline_enemy_track;
    vg_rect timeline_window;
    vg_rect load_button;
    vg_rect delete_button;
    vg_rect save_button;
    vg_rect new_button;
    vg_rect save_new_button;
    vg_rect name_box;
    vg_rect prev_button;
    vg_rect next_button;
    vg_rect swarm_button;
    vg_rect watcher_button;
    vg_rect asteroid_button;
    vg_rect mine_button;
    vg_rect missile_button;
    vg_rect arc_button;
    vg_rect window_button;
    vg_rect construction_button_0;
    vg_rect construction_button_1;
    vg_rect construction_button_2;
    vg_rect construction_button_3;
    vg_rect construction_button_4;
    vg_rect construction_button_5;
    vg_rect construction_button_6;
    vg_rect construction_button_7;
} level_editor_layout;

typedef struct level_editor_state {
    int level_style;
    int level_render_style;
    int level_wave_mode;
    int level_theme_palette;
    int level_background_style;
    int level_background_mask_style;
    int level_asteroid_storm_enabled;
    float level_asteroid_storm_angle_deg;
    float level_asteroid_storm_speed;
    float level_asteroid_storm_duration_s;
    float level_asteroid_storm_density;
    float level_kamikaze_radius_min;
    float level_kamikaze_radius_max;
    char level_name[LEVEL_EDITOR_NAME_CAP];
    char status_text[LEVEL_EDITOR_STATUS_CAP];
    int entry_active;
    float timeline_01;
    float level_length_screens;
    int timeline_drag;
    int selected_marker;
    int selected_property;
    int entity_tool_selected; /* 0=none, LEVEL_EDITOR_MARKER_* */
    int structure_tool_selected; /* 0=none, 1..5 construction palette */
    int entity_drag_active;
    int entity_drag_kind;
    float entity_drag_x;
    float entity_drag_y;
    int marker_drag_active;
    int marker_drag_index;
    int dirty;
    unsigned edit_revision;
    char source_path[LEVEL_EDITOR_PATH_CAP];
    char source_text[16384];
    int loaded_level_valid;
    leveldef_level loaded_level;
    int marker_count;
    level_editor_marker markers[LEVEL_EDITOR_MAX_MARKERS];
    int snapshot_valid;
    float snapshot_level_length_screens;
    int snapshot_level_render_style;
    int snapshot_level_wave_mode;
    int snapshot_level_theme_palette;
    int snapshot_level_background_style;
    int snapshot_level_background_mask_style;
    int snapshot_level_asteroid_storm_enabled;
    float snapshot_level_asteroid_storm_angle_deg;
    float snapshot_level_asteroid_storm_speed;
    float snapshot_level_asteroid_storm_duration_s;
    float snapshot_level_asteroid_storm_density;
    float snapshot_level_kamikaze_radius_min;
    float snapshot_level_kamikaze_radius_max;
    char snapshot_level_name[LEVEL_EDITOR_NAME_CAP];
    int snapshot_marker_count;
    level_editor_marker snapshot_markers[LEVEL_EDITOR_MAX_MARKERS];
} level_editor_state;

void level_editor_init(level_editor_state* s);
void level_editor_compute_layout(float w, float h, level_editor_layout* out);
int level_editor_load_by_name(level_editor_state* s, const leveldef_db* db, const char* name);
void level_editor_append_text(level_editor_state* s, const char* utf8);
void level_editor_backspace(level_editor_state* s);
int level_editor_handle_mouse(level_editor_state* s, float mouse_x, float mouse_y, float w, float h, int mouse_down, int mouse_pressed);
int level_editor_handle_mouse_release(level_editor_state* s, float mouse_x, float mouse_y, float w, float h);
void level_editor_select_marker(level_editor_state* s, int delta);
void level_editor_select_property(level_editor_state* s, int delta);
void level_editor_adjust_selected_property(level_editor_state* s, float delta);
const char* level_editor_selected_property_name(const level_editor_state* s);
int level_editor_cycle_level(level_editor_state* s, const leveldef_db* db, int delta);
int level_editor_save_current(level_editor_state* s, const leveldef_db* db, char* out_path, size_t out_path_cap);
int level_editor_save_new(level_editor_state* s, const leveldef_db* db, char* out_path, size_t out_path_cap);
int level_editor_revert(level_editor_state* s);
void level_editor_new_blank(level_editor_state* s);
int level_editor_delete_selected(level_editor_state* s);
int level_editor_build_level(const level_editor_state* s, const leveldef_db* db, leveldef_level* out_level);
int level_editor_rotate_selected_structure(level_editor_state* s, int delta_quadrants);

#endif
