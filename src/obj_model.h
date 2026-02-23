#ifndef V_TYPE_OBJ_MODEL_H
#define V_TYPE_OBJ_MODEL_H

#include <stdint.h>

typedef struct obj_wire_model {
    float* positions_xyz;
    uint32_t vertex_count;
    uint32_t* edges;
    uint32_t edge_count;
} obj_wire_model;

int obj_wire_model_load_file(const char* path, obj_wire_model* out_model);
void obj_wire_model_destroy(obj_wire_model* model);

#endif
