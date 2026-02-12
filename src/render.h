#ifndef V_TYPE_RENDER_H
#define V_TYPE_RENDER_H

#include "game.h"
#include "vg.h"

#define ACOUSTICS_SLIDER_COUNT 14
#define ACOUSTICS_SCOPE_SAMPLES 192
#define ACOUSTICS_SLOT_COUNT 5
#define VIDEO_MENU_RES_COUNT 6
#define VIDEO_MENU_DIAL_COUNT 9

typedef struct render_metrics {
    float fps;
    float dt;
    int force_clear;
    int show_crt_ui;
    int crt_ui_selected;
    const char* teletype_text;
    int show_acoustics;
    int show_video_menu;
    int video_menu_selected;
    int video_menu_fullscreen;
    int palette_mode;
    float video_dial_01[VIDEO_MENU_DIAL_COUNT];
    int acoustics_selected;
    int acoustics_fire_slot_selected;
    int acoustics_thr_slot_selected;
    int mouse_in_window;
    float mouse_x;
    float mouse_y;
    int acoustics_fire_slot_defined[ACOUSTICS_SLOT_COUNT];
    int acoustics_thr_slot_defined[ACOUSTICS_SLOT_COUNT];
    float acoustics_value_01[ACOUSTICS_SLIDER_COUNT];
    float acoustics_display[ACOUSTICS_SLIDER_COUNT];
    float acoustics_scope[ACOUSTICS_SCOPE_SAMPLES];
    int video_res_w[VIDEO_MENU_RES_COUNT];
    int video_res_h[VIDEO_MENU_RES_COUNT];
} render_metrics;

vg_result render_frame(vg_context* ctx, const game_state* g, const render_metrics* metrics);

#endif
