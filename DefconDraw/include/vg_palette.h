#ifndef VG_PALETTE_H
#define VG_PALETTE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "vg.h"

#define VG_PALETTE_MAX_ENTRIES 64u
#define VG_PALETTE_NAME_MAX 24u

typedef struct vg_palette_entry {
    vg_color color;
    char name[VG_PALETTE_NAME_MAX];
} vg_palette_entry;

typedef struct vg_palette {
    uint32_t count;
    vg_palette_entry entries[VG_PALETTE_MAX_ENTRIES];
} vg_palette;

void vg_palette_init(vg_palette* palette);
void vg_palette_make_wopr(vg_palette* palette);

vg_result vg_palette_set_entry(vg_palette* palette, uint32_t index, vg_color color, const char* name);
vg_result vg_palette_set_color(vg_palette* palette, uint32_t index, vg_color color);
vg_result vg_palette_set_name(vg_palette* palette, uint32_t index, const char* name);
vg_result vg_palette_get_color(const vg_palette* palette, uint32_t index, vg_color* out_color);
const char* vg_palette_get_name(const vg_palette* palette, uint32_t index);
vg_result vg_palette_find(const vg_palette* palette, const char* name, uint32_t* out_index);

void vg_set_palette(vg_context* ctx, const vg_palette* palette);
void vg_get_palette(vg_context* ctx, vg_palette* out_palette);

#ifdef __cplusplus
}
#endif

#endif
