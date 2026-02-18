#include "settings.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define SETTINGS_PATH "settings.cfg"

static float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static int ensure_dir_recursive(const char* path) {
    if (!path || path[0] == '\0') {
        return 0;
    }
    char tmp[PATH_MAX];
    size_t n = strlen(path);
    if (n >= sizeof(tmp)) {
        return 0;
    }
    memcpy(tmp, path, n + 1u);
    for (size_t i = 1; i < n; ++i) {
        if (tmp[i] == '/') {
            tmp[i] = '\0';
            if (tmp[0] != '\0') {
                if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
                    return 0;
                }
            }
            tmp[i] = '/';
        }
    }
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
        return 0;
    }
    return 1;
}

static int make_xdg_settings_path(char* out, size_t out_cap) {
    if (!out || out_cap == 0u) {
        return 0;
    }
    const char* xdg = getenv("XDG_CONFIG_HOME");
    if (xdg && xdg[0] != '\0') {
        int n = snprintf(out, out_cap, "%s/VectorSwarm/%s", xdg, SETTINGS_PATH);
        return (n > 0 && (size_t)n < out_cap) ? 1 : 0;
    }
    const char* home = getenv("HOME");
    if (!home || home[0] == '\0') {
        return 0;
    }
    int n = snprintf(out, out_cap, "%s/.config/VectorSwarm/%s", home, SETTINGS_PATH);
    return (n > 0 && (size_t)n < out_cap) ? 1 : 0;
}

static int resolution_index_from_wh(int w, int h, const settings_resolution* resolutions, int resolution_count) {
    if (!resolutions || resolution_count <= 0) {
        return -1;
    }
    for (int i = 0; i < resolution_count; ++i) {
        if (resolutions[i].w == w && resolutions[i].h == h) {
            return i;
        }
    }
    return -1;
}

static int save_settings_to_path(const app_settings* s, const char* path) {
    if (!s || !path || path[0] == '\0') {
        return 0;
    }
    FILE* f = fopen(path, "w");
    if (!f) {
        return 0;
    }
    fprintf(f, "fullscreen=%d\n", s->fullscreen ? 1 : 0);
    fprintf(f, "selected=%d\n", s->selected);
    fprintf(f, "width=%d\n", s->width);
    fprintf(f, "height=%d\n", s->height);
    fprintf(f, "palette=%d\n", s->palette);
    for (int i = 0; i < VIDEO_MENU_DIAL_COUNT; ++i) {
        fprintf(f, "dial%d=%.6f\n", i, clampf(s->video_dial_01[i], 0.0f, 1.0f));
    }
    fclose(f);
    return 1;
}

int settings_save(const app_settings* s) {
    char xdg_path[PATH_MAX];
    if (!s) {
        return 0;
    }
    if (make_xdg_settings_path(xdg_path, sizeof(xdg_path))) {
        char dir[PATH_MAX];
        snprintf(dir, sizeof(dir), "%s", xdg_path);
        char* slash = strrchr(dir, '/');
        if (slash) {
            *slash = '\0';
            (void)ensure_dir_recursive(dir);
        }
        return save_settings_to_path(s, xdg_path);
    }
    return 0;
}

int settings_load(app_settings* io, const settings_resolution* resolutions, int resolution_count, int default_selected) {
    if (!io) {
        return 0;
    }
    char xdg_path[PATH_MAX];
    if (!make_xdg_settings_path(xdg_path, sizeof(xdg_path))) {
        return 0;
    }

    FILE* f = fopen(xdg_path, "r");
    if (!f) {
        return 0;
    }

    int fullscreen = io->fullscreen;
    int selected = io->selected;
    int width = -1;
    int height = -1;
    int palette = io->palette;
    float dials[VIDEO_MENU_DIAL_COUNT];
    memcpy(dials, io->video_dial_01, sizeof(dials));

    char line[128];
    while (fgets(line, sizeof(line), f)) {
        char key[64];
        char value[64];
        if (sscanf(line, "%63[^=]=%63s", key, value) != 2) {
            continue;
        }
        if (strcmp(key, "fullscreen") == 0) {
            fullscreen = (atoi(value) != 0) ? 1 : 0;
        } else if (strcmp(key, "selected") == 0) {
            selected = atoi(value);
        } else if (strcmp(key, "width") == 0) {
            width = atoi(value);
        } else if (strcmp(key, "height") == 0) {
            height = atoi(value);
        } else if (strcmp(key, "palette") == 0) {
            palette = atoi(value);
        } else {
            int dial = -1;
            if (sscanf(key, "dial%d", &dial) == 1 && dial >= 0 && dial < VIDEO_MENU_DIAL_COUNT) {
                dials[dial] = clampf(strtof(value, NULL), 0.0f, 1.0f);
            }
        }
    }
    fclose(f);

    if (selected < 0 || selected > resolution_count) {
        selected = default_selected;
    }
    if (width > 0 && height > 0) {
        int idx = resolution_index_from_wh(width, height, resolutions, resolution_count);
        if (idx >= 0) {
            selected = idx + 1;
        }
    }
    if (!fullscreen && selected == 0) {
        selected = default_selected;
    }
    if (palette < 0 || palette > 2) {
        palette = 0;
    }

    io->fullscreen = fullscreen ? 1 : 0;
    io->selected = io->fullscreen ? 0 : selected;
    io->palette = palette;
    memcpy(io->video_dial_01, dials, sizeof(dials));
    return 1;
}
