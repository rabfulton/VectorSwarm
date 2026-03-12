#include "texture_atlas.h"

#include <ctype.h>
#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEXTURE_ATLAS_MAX_COUNT 64
#define TEXTURE_ATLAS_NAME_CAP 64
#define TEXTURE_ATLAS_DISPLAY_CAP 64
#define TEXTURE_ATLAS_PATH_CAP 256

typedef struct texture_atlas_storage {
    texture_atlas_def def;
    char name[TEXTURE_ATLAS_NAME_CAP];
    char display_name[TEXTURE_ATLAS_DISPLAY_CAP];
    char asset_path[TEXTURE_ATLAS_PATH_CAP];
} texture_atlas_storage;

static texture_atlas_storage g_texture_atlases[TEXTURE_ATLAS_MAX_COUNT];
static int g_texture_atlas_count = -1;

static int has_suffix_ignore_case(const char* s, const char* suffix) {
    size_t ls = 0;
    size_t lx = 0;
    if (!s || !suffix) {
        return 0;
    }
    ls = strlen(s);
    lx = strlen(suffix);
    if (ls < lx) {
        return 0;
    }
    s += ls - lx;
    while (*s && *suffix) {
        const char a = (char)tolower((unsigned char)*s++);
        const char b = (char)tolower((unsigned char)*suffix++);
        if (a != b) {
            return 0;
        }
    }
    return 1;
}

static int contains_tiles_token(const char* s) {
    static const char* k_token = "tiles";
    const size_t token_len = 5u;
    if (!s) {
        return 0;
    }
    while (*s) {
        size_t i = 0;
        while (i < token_len && s[i] != '\0' &&
               tolower((unsigned char)s[i]) == k_token[i]) {
            ++i;
        }
        if (i == token_len) {
            return 1;
        }
        ++s;
    }
    return 0;
}

static int read_png_dims(const char* path, int* out_w, int* out_h) {
    static const unsigned char k_png_sig[8] = {137u, 80u, 78u, 71u, 13u, 10u, 26u, 10u};
    unsigned char buf[24];
    FILE* f = NULL;
    uint32_t w = 0u;
    uint32_t h = 0u;
    if (!path || !out_w || !out_h) {
        return 0;
    }
    f = fopen(path, "rb");
    if (!f) {
        return 0;
    }
    if (fread(buf, 1, sizeof(buf), f) != sizeof(buf)) {
        fclose(f);
        return 0;
    }
    fclose(f);
    if (memcmp(buf, k_png_sig, sizeof(k_png_sig)) != 0 || memcmp(buf + 12, "IHDR", 4) != 0) {
        return 0;
    }
    w = ((uint32_t)buf[16] << 24) | ((uint32_t)buf[17] << 16) | ((uint32_t)buf[18] << 8) | (uint32_t)buf[19];
    h = ((uint32_t)buf[20] << 24) | ((uint32_t)buf[21] << 16) | ((uint32_t)buf[22] << 8) | (uint32_t)buf[23];
    if (w == 0u || h == 0u || w > 32768u || h > 32768u) {
        return 0;
    }
    *out_w = (int)w;
    *out_h = (int)h;
    return 1;
}

static int atlas_name_exists(const char* name) {
    int i;
    for (i = 0; i < g_texture_atlas_count; ++i) {
        if (strcmp(g_texture_atlases[i].name, name) == 0) {
            return 1;
        }
    }
    return 0;
}

static int add_texture_atlas(const char* file_name, const char* read_path) {
    texture_atlas_storage* slot = NULL;
    const char* dot = NULL;
    size_t stem_len = 0;
    size_t i = 0;
    int width_px = 0;
    int height_px = 0;
    if (!file_name || !read_path || g_texture_atlas_count >= TEXTURE_ATLAS_MAX_COUNT) {
        return 0;
    }
    dot = strrchr(file_name, '.');
    stem_len = dot ? (size_t)(dot - file_name) : strlen(file_name);
    if (stem_len == 0 || stem_len >= TEXTURE_ATLAS_NAME_CAP) {
        return 0;
    }
    slot = &g_texture_atlases[g_texture_atlas_count];
    memset(slot, 0, sizeof(*slot));
    memcpy(slot->name, file_name, stem_len);
    slot->name[stem_len] = '\0';
    if (atlas_name_exists(slot->name)) {
        return 1;
    }
    if (!read_png_dims(read_path, &width_px, &height_px)) {
        fprintf(stderr, "texture_atlas: failed to read PNG dimensions from %s\n", read_path);
        return 0;
    }
    for (i = 0; i < stem_len && i + 1 < sizeof(slot->display_name); ++i) {
        char c = slot->name[i];
        slot->display_name[i] = (char)(((c >= 'a' && c <= 'z') ? (c - 'a' + 'A') : c));
    }
    slot->display_name[i] = '\0';
    if (snprintf(slot->asset_path, sizeof(slot->asset_path), "assets/images/%s", file_name) >= (int)sizeof(slot->asset_path)) {
        return 0;
    }
    slot->def.id = g_texture_atlas_count;
    slot->def.name = slot->name;
    slot->def.display_name = slot->display_name;
    slot->def.asset_path = slot->asset_path;
    slot->def.width_px = width_px;
    slot->def.height_px = height_px;
    slot->def.default_tile_w_px = 256;
    slot->def.default_tile_h_px = 256;
    g_texture_atlas_count += 1;
    return 1;
}

static int atlas_sort_cmp(const void* a, const void* b) {
    const texture_atlas_storage* aa = (const texture_atlas_storage*)a;
    const texture_atlas_storage* bb = (const texture_atlas_storage*)b;
    return strcmp(aa->name, bb->name);
}

static void normalize_atlas_ids(void) {
    int i;
    for (i = 0; i < g_texture_atlas_count; ++i) {
        g_texture_atlases[i].def.id = i;
        g_texture_atlases[i].def.name = g_texture_atlases[i].name;
        g_texture_atlases[i].def.display_name = g_texture_atlases[i].display_name;
        g_texture_atlases[i].def.asset_path = g_texture_atlases[i].asset_path;
    }
}

int texture_atlas_refresh(void) {
    const char* dirs[] = {
        "assets/images",
        "../assets/images",
        "../../assets/images"
    };
    int i;
    g_texture_atlas_count = 0;
    for (i = 0; i < (int)(sizeof(dirs) / sizeof(dirs[0])); ++i) {
        DIR* dir = opendir(dirs[i]);
        if (!dir) {
            continue;
        }
        for (;;) {
            struct dirent* de = readdir(dir);
            char read_path[TEXTURE_ATLAS_PATH_CAP];
            if (!de) {
                break;
            }
            if (!contains_tiles_token(de->d_name) || !has_suffix_ignore_case(de->d_name, ".png")) {
                continue;
            }
            if (snprintf(read_path, sizeof(read_path), "%s/%s", dirs[i], de->d_name) >= (int)sizeof(read_path)) {
                continue;
            }
            if (!add_texture_atlas(de->d_name, read_path)) {
                closedir(dir);
                return 0;
            }
        }
        closedir(dir);
        if (g_texture_atlas_count > 0) {
            break;
        }
    }
    if (g_texture_atlas_count > 1) {
        qsort(g_texture_atlases, (size_t)g_texture_atlas_count, sizeof(g_texture_atlases[0]), atlas_sort_cmp);
    }
    normalize_atlas_ids();
    return 1;
}

static void ensure_texture_atlases_loaded(void) {
    if (g_texture_atlas_count < 0) {
        (void)texture_atlas_refresh();
    }
}

int texture_atlas_count(void) {
    ensure_texture_atlases_loaded();
    return (g_texture_atlas_count > 0) ? g_texture_atlas_count : 0;
}

const texture_atlas_def* texture_atlas_get(int id) {
    ensure_texture_atlases_loaded();
    if (id < 0 || id >= g_texture_atlas_count) {
        return NULL;
    }
    return &g_texture_atlases[id].def;
}

const texture_atlas_def* texture_atlas_find_by_name(const char* name) {
    int i;
    ensure_texture_atlases_loaded();
    if (!name) {
        return NULL;
    }
    for (i = 0; i < g_texture_atlas_count; ++i) {
        if (strcmp(g_texture_atlases[i].name, name) == 0) {
            return &g_texture_atlases[i].def;
        }
    }
    return NULL;
}

const char* texture_atlas_name(int id) {
    const texture_atlas_def* def = texture_atlas_get(id);
    return def ? def->name : "none";
}

const char* texture_atlas_display_name(int id) {
    const texture_atlas_def* def = texture_atlas_get(id);
    return def ? def->display_name : "NONE";
}

int texture_atlas_default_tile_w(int id) {
    const texture_atlas_def* def = texture_atlas_get(id);
    return def ? def->default_tile_w_px : 256;
}

int texture_atlas_default_tile_h(int id) {
    const texture_atlas_def* def = texture_atlas_get(id);
    return def ? def->default_tile_h_px : 256;
}

int texture_atlas_grid_dims(int id, int tile_w_px, int tile_h_px, int* out_cols, int* out_rows) {
    const texture_atlas_def* def = texture_atlas_get(id);
    if (!def || tile_w_px <= 0 || tile_h_px <= 0) {
        return 0;
    }
    if ((def->width_px % tile_w_px) != 0 || (def->height_px % tile_h_px) != 0) {
        return 0;
    }
    if (out_cols) {
        *out_cols = def->width_px / tile_w_px;
    }
    if (out_rows) {
        *out_rows = def->height_px / tile_h_px;
    }
    return ((def->width_px / tile_w_px) > 0 && (def->height_px / tile_h_px) > 0) ? 1 : 0;
}
