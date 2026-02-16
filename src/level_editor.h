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
    LEVEL_EDITOR_MARKER_BOID = 5
};

typedef struct level_editor_marker {
    int kind;
    float x01; /* normalized over full level length */
    float y01; /* normalized screen-space anchor */
    float a;
    float b;
    float c;
    float d;
} level_editor_marker;

typedef struct level_editor_layout {
    vg_rect viewport;
    vg_rect properties;
    vg_rect entities;
    vg_rect timeline;
    vg_rect timeline_track;
    vg_rect timeline_window;
    vg_rect load_button;
    vg_rect save_button;
    vg_rect save_new_button;
    vg_rect name_box;
    vg_rect prev_button;
    vg_rect next_button;
    vg_rect swarm_button;
    vg_rect watcher_button;
} level_editor_layout;

typedef struct level_editor_state {
    int level_style;
    int level_render_style;
    int level_wave_mode;
    char level_name[LEVEL_EDITOR_NAME_CAP];
    char status_text[LEVEL_EDITOR_STATUS_CAP];
    int entry_active;
    float timeline_01;
    float level_length_screens;
    int timeline_drag;
    int selected_marker;
    int selected_property;
    int entity_tool_selected; /* 0=none, LEVEL_EDITOR_MARKER_* */
    int entity_drag_active;
    int entity_drag_kind;
    float entity_drag_x;
    float entity_drag_y;
    int dirty;
    char source_path[LEVEL_EDITOR_PATH_CAP];
    char source_text[16384];
    int marker_count;
    level_editor_marker markers[LEVEL_EDITOR_MAX_MARKERS];
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

#endif
