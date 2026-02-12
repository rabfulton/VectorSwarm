#include "vg.h"

#include <stdio.h>

int main(void) {
    vg_context_desc ctx_desc = {0};
    ctx_desc.backend = VG_BACKEND_VULKAN;

    vg_context* ctx = NULL;
    vg_result res = vg_context_create(&ctx_desc, &ctx);
    if (res != VG_OK) {
        fprintf(stderr, "vg_context_create failed: %s\n", vg_result_string(res));
        return 1;
    }

    vg_frame_desc frame = {
        .width = 1280,
        .height = 720,
        .delta_time_s = 1.0f / 60.0f
    };

    res = vg_begin_frame(ctx, &frame);
    if (res != VG_OK) {
        fprintf(stderr, "vg_begin_frame failed: %s\n", vg_result_string(res));
        vg_context_destroy(ctx);
        return 1;
    }

    vg_vec2 line[] = {
        {100.0f, 100.0f},
        {300.0f, 220.0f},
        {540.0f, 160.0f}
    };

    vg_stroke_style style = {
        .width_px = 3.5f,
        .intensity = 1.0f,
        .color = {0.2f, 1.0f, 0.3f, 1.0f},
        .cap = VG_LINE_CAP_ROUND,
        .join = VG_LINE_JOIN_ROUND,
        .miter_limit = 4.0f,
        .blend = VG_BLEND_ADDITIVE
    };

    res = vg_draw_polyline(ctx, line, sizeof(line) / sizeof(line[0]), &style, 0);
    if (res != VG_OK) {
        fprintf(stderr, "vg_draw_polyline failed: %s\n", vg_result_string(res));
        vg_context_destroy(ctx);
        return 1;
    }

    vg_path* path = NULL;
    res = vg_path_create(ctx, &path);
    if (res != VG_OK) {
        fprintf(stderr, "vg_path_create failed: %s\n", vg_result_string(res));
        vg_context_destroy(ctx);
        return 1;
    }

    vg_path_move_to(path, (vg_vec2){700.0f, 200.0f});
    vg_path_line_to(path, (vg_vec2){780.0f, 140.0f});
    vg_path_cubic_to(path, (vg_vec2){860.0f, 220.0f}, (vg_vec2){920.0f, 120.0f}, (vg_vec2){980.0f, 200.0f});
    vg_path_close(path);

    res = vg_draw_path_stroke(ctx, path, &style);
    if (res != VG_OK) {
        fprintf(stderr, "vg_draw_path_stroke failed: %s\n", vg_result_string(res));
        vg_path_destroy(path);
        vg_context_destroy(ctx);
        return 1;
    }

    vg_path_destroy(path);

    res = vg_end_frame(ctx);
    if (res != VG_OK) {
        fprintf(stderr, "vg_end_frame failed: %s\n", vg_result_string(res));
        vg_context_destroy(ctx);
        return 1;
    }

    vg_context_destroy(ctx);
    puts("Vector demo API flow ran successfully.");
    return 0;
}
