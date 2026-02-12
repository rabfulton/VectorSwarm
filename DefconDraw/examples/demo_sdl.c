#include "vg.h"

#include <SDL2/SDL.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static vg_vec2 rot(vg_vec2 p, float a) {
    float c = cosf(a);
    float s = sinf(a);
    vg_vec2 out = {p.x * c - p.y * s, p.x * s + p.y * c};
    return out;
}

static vg_vec2 add(vg_vec2 a, vg_vec2 b) {
    vg_vec2 out = {a.x + b.x, a.y + b.y};
    return out;
}

int main(void) {
    const uint32_t width = 1280;
    const uint32_t height = 720;

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow(
        "vectorgfx preview",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        (int)width,
        (int)height,
        SDL_WINDOW_SHOWN
    );
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    SDL_Texture* texture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_RGBA32,
        SDL_TEXTUREACCESS_STREAMING,
        (int)width,
        (int)height
    );
    if (!texture) {
        fprintf(stderr, "SDL_CreateTexture failed: %s\n", SDL_GetError());
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    uint8_t* pixels = (uint8_t*)calloc((size_t)width * (size_t)height * 4u, 1u);
    if (!pixels) {
        fprintf(stderr, "pixel allocation failed\n");
        SDL_DestroyTexture(texture);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    vg_context_desc ctx_desc = {0};
    ctx_desc.backend = VG_BACKEND_VULKAN;

    vg_context* ctx = NULL;
    vg_result res = vg_context_create(&ctx_desc, &ctx);
    if (res != VG_OK) {
        fprintf(stderr, "vg_context_create failed: %s\n", vg_result_string(res));
        free(pixels);
        SDL_DestroyTexture(texture);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    vg_path* path = NULL;
    res = vg_path_create(ctx, &path);
    if (res != VG_OK) {
        fprintf(stderr, "vg_path_create failed: %s\n", vg_result_string(res));
        vg_context_destroy(ctx);
        free(pixels);
        SDL_DestroyTexture(texture);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    int running = 1;
    uint64_t last_ticks = SDL_GetPerformanceCounter();
    const float freq = (float)SDL_GetPerformanceFrequency();

    vg_retro_params retro = {
        .bloom_strength = 0.85f,
        .bloom_radius_px = 4.0f,
        .persistence_decay = 0.90f,
        .jitter_amount = 0.30f,
        .flicker_amount = 0.18f
    };
    vg_set_retro_params(ctx, &retro);

    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) {
                running = 0;
            }
        }

        uint64_t now_ticks = SDL_GetPerformanceCounter();
        float dt = (float)(now_ticks - last_ticks) / freq;
        last_ticks = now_ticks;

        if (dt <= 0.0f) {
            dt = 1.0f / 60.0f;
        }

        vg_get_retro_params(ctx, &retro);
        float persistence = retro.persistence_decay;
        if (persistence < 0.0f) {
            persistence = 0.0f;
        }
        if (persistence > 1.0f) {
            persistence = 1.0f;
        }
        float frame_decay = powf(persistence, dt * 60.0f);

        /* Persistence decay for retro monitor feel. */
        for (size_t i = 0; i < (size_t)width * (size_t)height; ++i) {
            pixels[i * 4u + 0u] = (uint8_t)((float)pixels[i * 4u + 0u] * frame_decay);
            pixels[i * 4u + 1u] = (uint8_t)((float)pixels[i * 4u + 1u] * frame_decay);
            pixels[i * 4u + 2u] = (uint8_t)((float)pixels[i * 4u + 2u] * frame_decay);
            pixels[i * 4u + 3u] = (uint8_t)((float)pixels[i * 4u + 3u] * (0.7f + 0.3f * frame_decay));
        }

        vg_frame_desc frame = {
            .width = width,
            .height = height,
            .delta_time_s = dt
        };

        res = vg_begin_frame(ctx, &frame);
        if (res != VG_OK) {
            fprintf(stderr, "vg_begin_frame failed: %s\n", vg_result_string(res));
            break;
        }

        float t = (float)SDL_GetTicks() * 0.001f;
        vg_vec2 center = {width * 0.5f, height * 0.5f};

        vg_stroke_style style_main = {
            .width_px = 4.5f,
            .intensity = 1.2f,
            .color = {0.15f, 1.0f, 0.35f, 1.0f},
            .cap = VG_LINE_CAP_ROUND,
            .join = VG_LINE_JOIN_ROUND,
            .miter_limit = 4.0f,
            .blend = VG_BLEND_ADDITIVE
        };

        vg_stroke_style style_alt = style_main;
        style_alt.width_px = 2.0f;
        style_alt.intensity = 0.8f;
        style_alt.color = (vg_color){0.4f, 0.9f, 1.0f, 1.0f};

        vg_vec2 ship_local[] = {
            {0.0f, -90.0f},
            {70.0f, 50.0f},
            {0.0f, 20.0f},
            {-70.0f, 50.0f},
            {0.0f, -90.0f}
        };
        vg_vec2 ship_world[sizeof(ship_local) / sizeof(ship_local[0])];
        for (size_t i = 0; i < sizeof(ship_local) / sizeof(ship_local[0]); ++i) {
            vg_vec2 p = rot(ship_local[i], t * 0.8f);
            ship_world[i] = add(p, center);
        }

        res = vg_draw_polyline(ctx, ship_world, sizeof(ship_world) / sizeof(ship_world[0]), &style_main, 0);
        if (res != VG_OK) {
            fprintf(stderr, "vg_draw_polyline(ship) failed: %s\n", vg_result_string(res));
            break;
        }

        vg_vec2 orbit_local[] = {
            {180.0f, 0.0f},
            {120.0f, 120.0f},
            {0.0f, 180.0f},
            {-120.0f, 120.0f},
            {-180.0f, 0.0f},
            {-120.0f, -120.0f},
            {0.0f, -180.0f},
            {120.0f, -120.0f}
        };
        vg_vec2 orbit_world[sizeof(orbit_local) / sizeof(orbit_local[0])];
        for (size_t i = 0; i < sizeof(orbit_local) / sizeof(orbit_local[0]); ++i) {
            vg_vec2 p = rot(orbit_local[i], -t * 0.35f);
            orbit_world[i] = add(p, center);
        }

        res = vg_draw_polyline(ctx, orbit_world, sizeof(orbit_world) / sizeof(orbit_world[0]), &style_alt, 1);
        if (res != VG_OK) {
            fprintf(stderr, "vg_draw_polyline(orbit) failed: %s\n", vg_result_string(res));
            break;
        }

        vg_path_clear(path);
        vg_path_move_to(path, (vg_vec2){140.0f, 560.0f});
        vg_path_cubic_to(path, (vg_vec2){220.0f + sinf(t) * 80.0f, 430.0f}, (vg_vec2){340.0f, 690.0f}, (vg_vec2){440.0f, 560.0f});
        vg_path_cubic_to(path, (vg_vec2){560.0f, 440.0f}, (vg_vec2){670.0f + cosf(t * 1.3f) * 60.0f, 700.0f}, (vg_vec2){790.0f, 560.0f});

        res = vg_draw_path_stroke(ctx, path, &style_main);
        if (res != VG_OK) {
            fprintf(stderr, "vg_draw_path_stroke failed: %s\n", vg_result_string(res));
            break;
        }

        res = vg_debug_rasterize_rgba8(ctx, pixels, width, height, width * 4u);
        if (res != VG_OK) {
            fprintf(stderr, "vg_debug_rasterize_rgba8 failed: %s\n", vg_result_string(res));
            break;
        }

        res = vg_end_frame(ctx);
        if (res != VG_OK) {
            fprintf(stderr, "vg_end_frame failed: %s\n", vg_result_string(res));
            break;
        }

        SDL_UpdateTexture(texture, NULL, pixels, (int)(width * 4u));
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, NULL, NULL);
        SDL_RenderPresent(renderer);
    }

    vg_path_destroy(path);
    vg_context_destroy(ctx);
    free(pixels);
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
