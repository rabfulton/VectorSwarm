#include "vg_palette.h"

#include "vg_internal.h"

#include <ctype.h>
#include <string.h>

static int vg_palette_valid_index(uint32_t index) {
    return index < VG_PALETTE_MAX_ENTRIES;
}

static void vg_palette_copy_name(char dst[VG_PALETTE_NAME_MAX], const char* src) {
    if (!dst) {
        return;
    }
    dst[0] = '\0';
    if (!src || src[0] == '\0') {
        return;
    }
    size_t i = 0u;
    for (; i + 1u < VG_PALETTE_NAME_MAX && src[i] != '\0'; ++i) {
        dst[i] = src[i];
    }
    dst[i] = '\0';
}

static int vg_palette_name_equal_nocase(const char* a, const char* b) {
    if (!a || !b) {
        return 0;
    }
    size_t i = 0u;
    for (;;) {
        unsigned char ca = (unsigned char)a[i];
        unsigned char cb = (unsigned char)b[i];
        if (tolower((int)ca) != tolower((int)cb)) {
            return 0;
        }
        if (ca == '\0') {
            return 1;
        }
        i++;
    }
}

void vg_palette_init(vg_palette* palette) {
    if (!palette) {
        return;
    }
    memset(palette, 0, sizeof(*palette));
}

void vg_palette_make_wopr(vg_palette* palette) {
    if (!palette) {
        return;
    }
    vg_palette_init(palette);
    (void)vg_palette_set_entry(palette, 0u, (vg_color){0.04f, 0.12f, 0.04f, 1.0f}, "deep");
    (void)vg_palette_set_entry(palette, 1u, (vg_color){0.09f, 0.26f, 0.09f, 1.0f}, "shadow");
    (void)vg_palette_set_entry(palette, 2u, (vg_color){0.15f, 0.48f, 0.15f, 1.0f}, "mid");
    (void)vg_palette_set_entry(palette, 3u, (vg_color){0.26f, 0.75f, 0.24f, 1.0f}, "bright");
    (void)vg_palette_set_entry(palette, 4u, (vg_color){0.76f, 1.00f, 0.76f, 1.0f}, "peak");
}

vg_result vg_palette_set_entry(vg_palette* palette, uint32_t index, vg_color color, const char* name) {
    if (!palette || !vg_palette_valid_index(index)) {
        return VG_ERROR_INVALID_ARGUMENT;
    }
    palette->entries[index].color = color;
    vg_palette_copy_name(palette->entries[index].name, name);
    if (palette->count <= index) {
        palette->count = index + 1u;
    }
    return VG_OK;
}

vg_result vg_palette_set_color(vg_palette* palette, uint32_t index, vg_color color) {
    if (!palette || !vg_palette_valid_index(index)) {
        return VG_ERROR_INVALID_ARGUMENT;
    }
    palette->entries[index].color = color;
    if (palette->count <= index) {
        palette->count = index + 1u;
    }
    return VG_OK;
}

vg_result vg_palette_set_name(vg_palette* palette, uint32_t index, const char* name) {
    if (!palette || !vg_palette_valid_index(index)) {
        return VG_ERROR_INVALID_ARGUMENT;
    }
    vg_palette_copy_name(palette->entries[index].name, name);
    if (palette->count <= index) {
        palette->count = index + 1u;
    }
    return VG_OK;
}

vg_result vg_palette_get_color(const vg_palette* palette, uint32_t index, vg_color* out_color) {
    if (!palette || !out_color || index >= palette->count || !vg_palette_valid_index(index)) {
        return VG_ERROR_INVALID_ARGUMENT;
    }
    *out_color = palette->entries[index].color;
    return VG_OK;
}

const char* vg_palette_get_name(const vg_palette* palette, uint32_t index) {
    if (!palette || index >= palette->count || !vg_palette_valid_index(index)) {
        return "";
    }
    return palette->entries[index].name;
}

vg_result vg_palette_find(const vg_palette* palette, const char* name, uint32_t* out_index) {
    if (!palette || !name || !out_index || name[0] == '\0') {
        return VG_ERROR_INVALID_ARGUMENT;
    }
    uint32_t n = palette->count;
    if (n > VG_PALETTE_MAX_ENTRIES) {
        n = VG_PALETTE_MAX_ENTRIES;
    }
    for (uint32_t i = 0u; i < n; ++i) {
        if (vg_palette_name_equal_nocase(palette->entries[i].name, name)) {
            *out_index = i;
            return VG_OK;
        }
    }
    return VG_ERROR_UNSUPPORTED;
}

void vg_set_palette(vg_context* ctx, const vg_palette* palette) {
    if (!ctx || !palette) {
        return;
    }
    ctx->palette = *palette;
    if (ctx->palette.count > VG_PALETTE_MAX_ENTRIES) {
        ctx->palette.count = VG_PALETTE_MAX_ENTRIES;
    }
}

void vg_get_palette(vg_context* ctx, vg_palette* out_palette) {
    if (!ctx || !out_palette) {
        return;
    }
    *out_palette = ctx->palette;
}
