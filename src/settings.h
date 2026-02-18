#ifndef V_TYPE_SETTINGS_H
#define V_TYPE_SETTINGS_H

#include "render.h"

typedef struct settings_resolution {
    int w;
    int h;
} settings_resolution;

typedef struct app_settings {
    int fullscreen;
    int selected;
    int palette;
    int width;
    int height;
    float video_dial_01[VIDEO_MENU_DIAL_COUNT];
} app_settings;

int settings_save(const app_settings* s);
int settings_load(app_settings* io, const settings_resolution* resolutions, int resolution_count, int default_selected);

#endif
