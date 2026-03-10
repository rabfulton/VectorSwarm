#include "texture_atlas.h"

#include <string.h>

static const texture_atlas_def k_texture_atlases[] = {
    {
        .id = TEXTURE_ATLAS_TILES,
        .name = "tiles",
        .display_name = "TILES",
        .asset_path = "assets/images/tiles.png",
        .width_px = 1024,
        .height_px = 1024,
        .default_tile_w_px = 256,
        .default_tile_h_px = 256
    }
};

const texture_atlas_def* texture_atlas_get(int id) {
    if (id < 0 || id >= (int)(sizeof(k_texture_atlases) / sizeof(k_texture_atlases[0]))) {
        return NULL;
    }
    return &k_texture_atlases[id];
}

const texture_atlas_def* texture_atlas_find_by_name(const char* name) {
    size_t i;
    if (!name) {
        return NULL;
    }
    for (i = 0; i < sizeof(k_texture_atlases) / sizeof(k_texture_atlases[0]); ++i) {
        if (strcmp(k_texture_atlases[i].name, name) == 0) {
            return &k_texture_atlases[i];
        }
    }
    return NULL;
}

const char* texture_atlas_name(int id) {
    const texture_atlas_def* def = texture_atlas_get(id);
    return def ? def->name : "tiles";
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
    if (out_cols) {
        *out_cols = def->width_px / tile_w_px;
    }
    if (out_rows) {
        *out_rows = def->height_px / tile_h_px;
    }
    return ((def->width_px / tile_w_px) > 0 && (def->height_px / tile_h_px) > 0) ? 1 : 0;
}
