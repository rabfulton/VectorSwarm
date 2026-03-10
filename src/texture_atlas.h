#ifndef V_TYPE_TEXTURE_ATLAS_H
#define V_TYPE_TEXTURE_ATLAS_H

typedef enum texture_atlas_id {
    TEXTURE_ATLAS_TILES = 0,
    TEXTURE_ATLAS_COUNT
} texture_atlas_id;

typedef struct texture_atlas_def {
    int id;
    const char* name;
    const char* display_name;
    const char* asset_path;
    int width_px;
    int height_px;
    int default_tile_w_px;
    int default_tile_h_px;
} texture_atlas_def;

const texture_atlas_def* texture_atlas_get(int id);
const texture_atlas_def* texture_atlas_find_by_name(const char* name);
const char* texture_atlas_name(int id);
int texture_atlas_default_tile_w(int id);
int texture_atlas_default_tile_h(int id);
int texture_atlas_grid_dims(int id, int tile_w_px, int tile_h_px, int* out_cols, int* out_rows);

#endif
