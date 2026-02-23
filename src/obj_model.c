#include "obj_model.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct u32_pair {
    uint32_t a;
    uint32_t b;
} u32_pair;

static int append_vertex(float** out, uint32_t* count, uint32_t* cap, float x, float y, float z) {
    if (!out || !count || !cap) {
        return 0;
    }
    if (*count >= *cap) {
        const uint32_t next_cap = (*cap == 0u) ? 64u : (*cap * 2u);
        float* next = (float*)realloc(*out, sizeof(float) * (size_t)next_cap * 3u);
        if (!next) {
            return 0;
        }
        *out = next;
        *cap = next_cap;
    }
    const size_t idx = (size_t)(*count) * 3u;
    (*out)[idx + 0u] = x;
    (*out)[idx + 1u] = y;
    (*out)[idx + 2u] = z;
    *count += 1u;
    return 1;
}

static int append_edge_unique(u32_pair** out, uint32_t* count, uint32_t* cap, uint32_t i0, uint32_t i1) {
    if (!out || !count || !cap || i0 == i1) {
        return 0;
    }
    const uint32_t a = (i0 < i1) ? i0 : i1;
    const uint32_t b = (i0 < i1) ? i1 : i0;
    for (uint32_t i = 0u; i < *count; ++i) {
        if ((*out)[i].a == a && (*out)[i].b == b) {
            return 1;
        }
    }
    if (*count >= *cap) {
        const uint32_t next_cap = (*cap == 0u) ? 128u : (*cap * 2u);
        u32_pair* next = (u32_pair*)realloc(*out, sizeof(u32_pair) * (size_t)next_cap);
        if (!next) {
            return 0;
        }
        *out = next;
        *cap = next_cap;
    }
    (*out)[*count].a = a;
    (*out)[*count].b = b;
    *count += 1u;
    return 1;
}

static int parse_face_index(const char* token, int* out_index) {
    if (!token || !out_index || !token[0]) {
        return 0;
    }
    char* end = NULL;
    const long v = strtol(token, &end, 10);
    if (end == token || v == 0L) {
        return 0;
    }
    *out_index = (int)v;
    return 1;
}

int obj_wire_model_load_file(const char* path, obj_wire_model* out_model) {
    if (!path || !out_model) {
        return 0;
    }

    FILE* f = fopen(path, "rb");
    if (!f) {
        return 0;
    }

    float* verts = NULL;
    uint32_t vert_count = 0u;
    uint32_t vert_cap = 0u;
    u32_pair* edges = NULL;
    uint32_t edge_count = 0u;
    uint32_t edge_cap = 0u;

    char line[2048];
    int ok = 1;
    while (ok && fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') {
            continue;
        }
        if (line[0] == 'v' && (line[1] == ' ' || line[1] == '\t')) {
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
            if (sscanf(line + 1, "%f %f %f", &x, &y, &z) == 3) {
                ok = append_vertex(&verts, &vert_count, &vert_cap, x, y, z);
            }
            continue;
        }
        if (line[0] == 'f' && (line[1] == ' ' || line[1] == '\t')) {
            int face_idx[128];
            int face_count = 0;
            char* save_ptr = NULL;
            char* tok = strtok_r(line + 2, " \t\r\n", &save_ptr);
            while (tok && face_count < (int)(sizeof(face_idx) / sizeof(face_idx[0]))) {
                int idx = 0;
                if (parse_face_index(tok, &idx)) {
                    int resolved = -1;
                    if (idx > 0) {
                        resolved = idx - 1;
                    } else if (idx < 0) {
                        resolved = (int)vert_count + idx;
                    }
                    if (resolved >= 0 && resolved < (int)vert_count) {
                        face_idx[face_count++] = resolved;
                    }
                }
                tok = strtok_r(NULL, " \t\r\n", &save_ptr);
            }
            if (face_count >= 2) {
                for (int i = 0; i < face_count; ++i) {
                    const uint32_t a = (uint32_t)face_idx[i];
                    const uint32_t b = (uint32_t)face_idx[(i + 1) % face_count];
                    ok = append_edge_unique(&edges, &edge_count, &edge_cap, a, b);
                    if (!ok) {
                        break;
                    }
                }
            }
            continue;
        }
    }

    fclose(f);

    if (!ok || vert_count == 0u || edge_count == 0u) {
        free(verts);
        free(edges);
        return 0;
    }

    obj_wire_model_destroy(out_model);
    out_model->positions_xyz = verts;
    out_model->vertex_count = vert_count;
    out_model->edge_count = edge_count;
    out_model->edges = (uint32_t*)malloc(sizeof(uint32_t) * (size_t)edge_count * 2u);
    if (!out_model->edges) {
        free(verts);
        free(edges);
        memset(out_model, 0, sizeof(*out_model));
        return 0;
    }
    for (uint32_t i = 0u; i < edge_count; ++i) {
        out_model->edges[i * 2u + 0u] = edges[i].a;
        out_model->edges[i * 2u + 1u] = edges[i].b;
    }
    free(edges);
    return 1;
}

void obj_wire_model_destroy(obj_wire_model* model) {
    if (!model) {
        return;
    }
    free(model->positions_xyz);
    free(model->edges);
    memset(model, 0, sizeof(*model));
}
