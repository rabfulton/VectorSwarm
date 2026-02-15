#include "vg_internal.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifndef VG_HAS_VULKAN
#define VG_HAS_VULKAN 0
#endif

#ifndef VG_HAS_VK_INTERNAL_PIPELINE
#define VG_HAS_VK_INTERNAL_PIPELINE 0
#endif

#if VG_HAS_VULKAN
#include <vulkan/vulkan.h>
#if VG_HAS_VK_INTERNAL_PIPELINE
#include "line_vert_spv.h"
#include "line_frag_spv.h"
#endif
#endif

typedef struct vg_vk_draw_cmd {
    uint32_t first_vertex;
    uint32_t vertex_count;
    vg_stroke_style style;
    vg_stencil_state stencil;
    vg_rect clip_rect;
    int has_clip;
} vg_vk_draw_cmd;

#if VG_HAS_VULKAN
typedef struct vg_vk_gpu_buffer {
    VkBuffer buffer;
    VkDeviceMemory memory;
    VkDeviceSize size_bytes;
} vg_vk_gpu_buffer;

typedef struct vg_vk_push_constants {
    float color[4];
    float params[4];
} vg_vk_push_constants;

typedef struct vg_vk_pipeline_key {
    vg_blend_mode blend;
    int stencil_enabled;
    vg_compare_op compare_op;
    vg_stencil_op fail_op;
    vg_stencil_op pass_op;
    vg_stencil_op depth_fail_op;
} vg_vk_pipeline_key;

typedef struct vg_vk_pipeline_entry {
    vg_vk_pipeline_key key;
    VkPipeline pipeline;
} vg_vk_pipeline_entry;
#endif

typedef struct vg_vk_backend {
    vg_backend_vulkan_desc desc;
    vg_frame_desc frame;
    vg_retro_params retro;
    vg_crt_profile crt;
    uint64_t frame_index;
    uint32_t raster_samples;
    int has_stencil_attachment;
    int stencil_clear_requested;
    uint32_t stencil_clear_value;

    vg_vec2* stroke_vertices;
    uint32_t stroke_vertex_count;
    uint32_t stroke_vertex_cap;

    vg_vk_draw_cmd* draws;
    uint32_t draw_count;
    uint32_t draw_cap;

#if VG_HAS_VULKAN
    VkPhysicalDevice physical_device;
    VkDevice device;
    VkRenderPass render_pass;
    VkCommandBuffer command_buffer;
    uint32_t vertex_binding;
    uint32_t upload_memory_type_index;
    int gpu_ready;
    vg_vk_gpu_buffer vertex_buffer;
#if VG_HAS_VK_INTERNAL_PIPELINE
    VkPipelineLayout pipeline_layout;
    vg_vk_pipeline_entry pipeline_cache[32];
    uint32_t pipeline_cache_count;
#endif
#endif
} vg_vk_backend;

static uint32_t vg_vk_sanitize_raster_samples(uint32_t samples) {
    switch (samples) {
        case 1u:
        case 2u:
        case 4u:
        case 8u:
        case 16u:
        case 32u:
        case 64u:
            return samples;
        default:
            return 1u;
    }
}

/*
 * Vulkan backend entry points will live here.
 * This file is intentionally minimal until the framegraph and pipelines land.
 */
static vg_vk_backend* vg_vk_backend_from(vg_context* ctx) {
    return (vg_vk_backend*)ctx->backend.impl;
}

static int vg_vk_reserve_vertices(vg_vk_backend* backend, uint32_t extra) {
    if (backend->stroke_vertex_count + extra <= backend->stroke_vertex_cap) {
        return 1;
    }

    uint32_t new_cap = backend->stroke_vertex_cap == 0 ? 1024u : backend->stroke_vertex_cap * 2u;
    while (new_cap < backend->stroke_vertex_count + extra) {
        new_cap *= 2u;
    }

    vg_vec2* next = (vg_vec2*)realloc(backend->stroke_vertices, sizeof(*next) * (size_t)new_cap);
    if (!next) {
        return 0;
    }

    backend->stroke_vertices = next;
    backend->stroke_vertex_cap = new_cap;
    return 1;
}

static int vg_vk_reserve_draws(vg_vk_backend* backend, uint32_t extra) {
    if (backend->draw_count + extra <= backend->draw_cap) {
        return 1;
    }

    uint32_t new_cap = backend->draw_cap == 0 ? 64u : backend->draw_cap * 2u;
    while (new_cap < backend->draw_count + extra) {
        new_cap *= 2u;
    }

    vg_vk_draw_cmd* next = (vg_vk_draw_cmd*)realloc(backend->draws, sizeof(*next) * (size_t)new_cap);
    if (!next) {
        return 0;
    }

    backend->draws = next;
    backend->draw_cap = new_cap;
    return 1;
}

static int vg_vk_style_equal(const vg_stroke_style* a, const vg_stroke_style* b) {
    return a->width_px == b->width_px &&
           a->intensity == b->intensity &&
           a->color.r == b->color.r &&
           a->color.g == b->color.g &&
           a->color.b == b->color.b &&
           a->color.a == b->color.a &&
           a->cap == b->cap &&
           a->join == b->join &&
           a->miter_limit == b->miter_limit &&
           a->blend == b->blend;
}

static int vg_vk_stencil_equal(const vg_stencil_state* a, const vg_stencil_state* b) {
    return a->enabled == b->enabled &&
           a->compare_op == b->compare_op &&
           a->fail_op == b->fail_op &&
           a->pass_op == b->pass_op &&
           a->depth_fail_op == b->depth_fail_op &&
           a->reference == b->reference &&
           a->compare_mask == b->compare_mask &&
           a->write_mask == b->write_mask;
}

static int vg_vk_clip_equal(int has_a, vg_rect a, int has_b, vg_rect b) {
    if (has_a != has_b) {
        return 0;
    }
    if (!has_a) {
        return 1;
    }
    return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}

#if VG_HAS_VULKAN
static VkCompareOp vg_vk_compare_op(vg_compare_op op) {
    switch (op) {
        case VG_COMPARE_NEVER:
            return VK_COMPARE_OP_NEVER;
        case VG_COMPARE_LESS:
            return VK_COMPARE_OP_LESS;
        case VG_COMPARE_EQUAL:
            return VK_COMPARE_OP_EQUAL;
        case VG_COMPARE_LESS_OR_EQUAL:
            return VK_COMPARE_OP_LESS_OR_EQUAL;
        case VG_COMPARE_GREATER:
            return VK_COMPARE_OP_GREATER;
        case VG_COMPARE_NOT_EQUAL:
            return VK_COMPARE_OP_NOT_EQUAL;
        case VG_COMPARE_GREATER_OR_EQUAL:
            return VK_COMPARE_OP_GREATER_OR_EQUAL;
        case VG_COMPARE_ALWAYS:
        default:
            return VK_COMPARE_OP_ALWAYS;
    }
}

static VkStencilOp vg_vk_stencil_op(vg_stencil_op op) {
    switch (op) {
        case VG_STENCIL_OP_KEEP:
            return VK_STENCIL_OP_KEEP;
        case VG_STENCIL_OP_ZERO:
            return VK_STENCIL_OP_ZERO;
        case VG_STENCIL_OP_REPLACE:
            return VK_STENCIL_OP_REPLACE;
        case VG_STENCIL_OP_INCREMENT_AND_CLAMP:
            return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
        case VG_STENCIL_OP_DECREMENT_AND_CLAMP:
            return VK_STENCIL_OP_DECREMENT_AND_CLAMP;
        case VG_STENCIL_OP_INVERT:
            return VK_STENCIL_OP_INVERT;
        case VG_STENCIL_OP_INCREMENT_AND_WRAP:
            return VK_STENCIL_OP_INCREMENT_AND_WRAP;
        case VG_STENCIL_OP_DECREMENT_AND_WRAP:
        default:
            return VK_STENCIL_OP_DECREMENT_AND_WRAP;
    }
}

static uint32_t vg_vk_find_memory_type(
    const VkPhysicalDeviceMemoryProperties* props,
    uint32_t type_bits,
    VkMemoryPropertyFlags required
) {
    for (uint32_t i = 0; i < props->memoryTypeCount; ++i) {
        uint32_t type_ok = (type_bits & (1u << i)) != 0u;
        uint32_t flags_ok = (props->memoryTypes[i].propertyFlags & required) == required;
        if (type_ok && flags_ok) {
            return i;
        }
    }
    return UINT32_MAX;
}

static void vg_vk_destroy_gpu_buffer(vg_vk_backend* backend, vg_vk_gpu_buffer* buf) {
    if (!backend || !buf || !backend->device) {
        return;
    }
    if (buf->buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(backend->device, buf->buffer, NULL);
    }
    if (buf->memory != VK_NULL_HANDLE) {
        vkFreeMemory(backend->device, buf->memory, NULL);
    }
    buf->buffer = VK_NULL_HANDLE;
    buf->memory = VK_NULL_HANDLE;
    buf->size_bytes = 0;
}

static vg_result vg_vk_ensure_vertex_buffer(vg_vk_backend* backend, VkDeviceSize required_size) {
    if (!backend || !backend->gpu_ready) {
        return VG_ERROR_INVALID_ARGUMENT;
    }
    if (required_size == 0) {
        required_size = sizeof(vg_vec2);
    }
    if (backend->vertex_buffer.size_bytes >= required_size) {
        return VG_OK;
    }

    vkDeviceWaitIdle(backend->device);
    vg_vk_destroy_gpu_buffer(backend, &backend->vertex_buffer);

    VkBufferCreateInfo buffer_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = required_size,
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };

    if (vkCreateBuffer(backend->device, &buffer_info, NULL, &backend->vertex_buffer.buffer) != VK_SUCCESS) {
        return VG_ERROR_BACKEND;
    }

    VkMemoryRequirements req = {0};
    vkGetBufferMemoryRequirements(backend->device, backend->vertex_buffer.buffer, &req);

    VkMemoryAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = req.size,
        .memoryTypeIndex = backend->upload_memory_type_index
    };

    if (vkAllocateMemory(backend->device, &alloc_info, NULL, &backend->vertex_buffer.memory) != VK_SUCCESS) {
        vg_vk_destroy_gpu_buffer(backend, &backend->vertex_buffer);
        return VG_ERROR_BACKEND;
    }

    if (vkBindBufferMemory(backend->device, backend->vertex_buffer.buffer, backend->vertex_buffer.memory, 0) != VK_SUCCESS) {
        vg_vk_destroy_gpu_buffer(backend, &backend->vertex_buffer);
        return VG_ERROR_BACKEND;
    }

    backend->vertex_buffer.size_bytes = req.size;
    return VG_OK;
}

static vg_result vg_vk_upload_vertices(vg_vk_backend* backend) {
    if (!backend) {
        return VG_ERROR_INVALID_ARGUMENT;
    }
    if (!backend->gpu_ready || backend->stroke_vertex_count == 0u) {
        return VG_OK;
    }

    VkDeviceSize bytes = (VkDeviceSize)backend->stroke_vertex_count * (VkDeviceSize)sizeof(vg_vec2);
    vg_result ensure = vg_vk_ensure_vertex_buffer(backend, bytes);
    if (ensure != VG_OK) {
        return ensure;
    }

    void* mapped = NULL;
    if (vkMapMemory(backend->device, backend->vertex_buffer.memory, 0, bytes, 0, &mapped) != VK_SUCCESS) {
        return VG_ERROR_BACKEND;
    }

    memcpy(mapped, backend->stroke_vertices, (size_t)bytes);
    vkUnmapMemory(backend->device, backend->vertex_buffer.memory);
    return VG_OK;
}

#if VG_HAS_VK_INTERNAL_PIPELINE
static VkShaderModule vg_vk_create_shader_module(vg_vk_backend* backend, const unsigned char* code, size_t code_size) {
    VkShaderModuleCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = code_size,
        .pCode = (const uint32_t*)code
    };
    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(backend->device, &create_info, NULL, &module) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    return module;
}

static void vg_vk_destroy_pipelines(vg_vk_backend* backend) {
    if (!backend || !backend->device) {
        return;
    }
    for (uint32_t i = 0; i < backend->pipeline_cache_count; ++i) {
        if (backend->pipeline_cache[i].pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(backend->device, backend->pipeline_cache[i].pipeline, NULL);
            backend->pipeline_cache[i].pipeline = VK_NULL_HANDLE;
        }
    }
    backend->pipeline_cache_count = 0;
    if (backend->pipeline_layout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(backend->device, backend->pipeline_layout, NULL);
        backend->pipeline_layout = VK_NULL_HANDLE;
    }
}

static vg_result vg_vk_create_pipeline(
    vg_vk_backend* backend,
    VkShaderModule vert,
    VkShaderModule frag,
    const vg_vk_pipeline_key* key,
    VkPipeline* out_pipeline
) {
    if (!backend || !backend->device || backend->render_pass == VK_NULL_HANDLE) {
        return VG_ERROR_INVALID_ARGUMENT;
    }

    VkPipelineShaderStageCreateInfo stages[2] = {
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = vert,
            .pName = "main"
        },
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = frag,
            .pName = "main"
        }
    };

    VkVertexInputBindingDescription binding = {
        .binding = backend->vertex_binding,
        .stride = sizeof(vg_vec2),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
    };
    VkVertexInputAttributeDescription attribute = {
        .location = 0,
        .binding = backend->vertex_binding,
        .format = VK_FORMAT_R32G32_SFLOAT,
        .offset = 0
    };
    VkPipelineVertexInputStateCreateInfo vertex_input = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &binding,
        .vertexAttributeDescriptionCount = 1,
        .pVertexAttributeDescriptions = &attribute
    };
    VkPipelineInputAssemblyStateCreateInfo input_assembly = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
    };
    VkPipelineViewportStateCreateInfo viewport_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1
    };
    VkPipelineRasterizationStateCreateInfo raster = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .lineWidth = 1.0f,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE
    };
    VkPipelineMultisampleStateCreateInfo msaa = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = (VkSampleCountFlagBits)backend->raster_samples
    };
    VkStencilOpState stencil = {
        .failOp = vg_vk_stencil_op(key->fail_op),
        .passOp = vg_vk_stencil_op(key->pass_op),
        .depthFailOp = vg_vk_stencil_op(key->depth_fail_op),
        .compareOp = vg_vk_compare_op(key->compare_op),
        .compareMask = 0xffu,
        .writeMask = 0xffu,
        .reference = 0u
    };
    VkPipelineDepthStencilStateCreateInfo depth_stencil = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_FALSE,
        .depthWriteEnable = VK_FALSE,
        .depthCompareOp = VK_COMPARE_OP_ALWAYS,
        .depthBoundsTestEnable = VK_FALSE,
        .stencilTestEnable = key->stencil_enabled ? VK_TRUE : VK_FALSE,
        .front = stencil,
        .back = stencil
    };

    VkPipelineColorBlendAttachmentState blend_attachment = {
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
        .blendEnable = VK_TRUE
    };
    VkPipelineColorBlendStateCreateInfo blend_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &blend_attachment
    };
    VkDynamicState dyn_states[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
        VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK,
        VK_DYNAMIC_STATE_STENCIL_WRITE_MASK,
        VK_DYNAMIC_STATE_STENCIL_REFERENCE
    };
    VkPipelineDynamicStateCreateInfo dynamic_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 5,
        .pDynamicStates = dyn_states
    };

    VkGraphicsPipelineCreateInfo pipeline_info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2,
        .pStages = stages,
        .pVertexInputState = &vertex_input,
        .pInputAssemblyState = &input_assembly,
        .pViewportState = &viewport_state,
        .pRasterizationState = &raster,
        .pMultisampleState = &msaa,
        .pDepthStencilState = &depth_stencil,
        .pColorBlendState = &blend_state,
        .pDynamicState = &dynamic_state,
        .layout = backend->pipeline_layout,
        .renderPass = backend->render_pass,
        .subpass = 0
    };

    if (key->blend == VG_BLEND_ADDITIVE) {
        blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
        blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
        blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;
    } else {
        blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
        blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;
    }

    if (vkCreateGraphicsPipelines(backend->device, VK_NULL_HANDLE, 1, &pipeline_info, NULL, out_pipeline) != VK_SUCCESS) {
        return VG_ERROR_BACKEND;
    }
    return VG_OK;
}

static vg_result vg_vk_create_pipelines(vg_vk_backend* backend) {
    if (!backend || !backend->device || backend->render_pass == VK_NULL_HANDLE) {
        return VG_ERROR_INVALID_ARGUMENT;
    }

    VkShaderModule vert = vg_vk_create_shader_module(backend, vg_line_vert_spv, vg_line_vert_spv_len);
    VkShaderModule frag = vg_vk_create_shader_module(backend, vg_line_frag_spv, vg_line_frag_spv_len);
    if (vert == VK_NULL_HANDLE || frag == VK_NULL_HANDLE) {
        if (vert != VK_NULL_HANDLE) {
            vkDestroyShaderModule(backend->device, vert, NULL);
        }
        if (frag != VK_NULL_HANDLE) {
            vkDestroyShaderModule(backend->device, frag, NULL);
        }
        return VG_ERROR_BACKEND;
    }

    VkPushConstantRange push_range = {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = sizeof(vg_vk_push_constants)
    };
    VkPipelineLayoutCreateInfo layout_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &push_range
    };
    if (vkCreatePipelineLayout(backend->device, &layout_info, NULL, &backend->pipeline_layout) != VK_SUCCESS) {
        vkDestroyShaderModule(backend->device, vert, NULL);
        vkDestroyShaderModule(backend->device, frag, NULL);
        return VG_ERROR_BACKEND;
    }

    vkDestroyShaderModule(backend->device, vert, NULL);
    vkDestroyShaderModule(backend->device, frag, NULL);
    return VG_OK;
}

static int vg_vk_pipeline_key_equal(const vg_vk_pipeline_key* a, const vg_vk_pipeline_key* b) {
    return a->blend == b->blend &&
           a->stencil_enabled == b->stencil_enabled &&
           a->compare_op == b->compare_op &&
           a->fail_op == b->fail_op &&
           a->pass_op == b->pass_op &&
           a->depth_fail_op == b->depth_fail_op;
}

static vg_result vg_vk_get_pipeline(vg_vk_backend* backend, const vg_vk_pipeline_key* key, VkPipeline* out_pipeline) {
    if (!backend || !key || !out_pipeline || backend->pipeline_layout == VK_NULL_HANDLE) {
        return VG_ERROR_INVALID_ARGUMENT;
    }
    for (uint32_t i = 0; i < backend->pipeline_cache_count; ++i) {
        if (vg_vk_pipeline_key_equal(&backend->pipeline_cache[i].key, key)) {
            *out_pipeline = backend->pipeline_cache[i].pipeline;
            return VG_OK;
        }
    }
    if (backend->pipeline_cache_count >= (uint32_t)(sizeof(backend->pipeline_cache) / sizeof(backend->pipeline_cache[0]))) {
        return VG_ERROR_BACKEND;
    }

    VkShaderModule vert = vg_vk_create_shader_module(backend, vg_line_vert_spv, vg_line_vert_spv_len);
    VkShaderModule frag = vg_vk_create_shader_module(backend, vg_line_frag_spv, vg_line_frag_spv_len);
    if (vert == VK_NULL_HANDLE || frag == VK_NULL_HANDLE) {
        if (vert != VK_NULL_HANDLE) {
            vkDestroyShaderModule(backend->device, vert, NULL);
        }
        if (frag != VK_NULL_HANDLE) {
            vkDestroyShaderModule(backend->device, frag, NULL);
        }
        return VG_ERROR_BACKEND;
    }

    VkPipeline pipe = VK_NULL_HANDLE;
    vg_result r = vg_vk_create_pipeline(backend, vert, frag, key, &pipe);
    vkDestroyShaderModule(backend->device, vert, NULL);
    vkDestroyShaderModule(backend->device, frag, NULL);
    if (r != VG_OK) {
        return r;
    }

    backend->pipeline_cache[backend->pipeline_cache_count].key = *key;
    backend->pipeline_cache[backend->pipeline_cache_count].pipeline = pipe;
    backend->pipeline_cache_count++;
    *out_pipeline = pipe;
    return VG_OK;
}
#endif
#endif

static float vg_vk_len(vg_vec2 v) {
    return sqrtf(v.x * v.x + v.y * v.y);
}

static vg_vec2 vg_vk_sub(vg_vec2 a, vg_vec2 b) {
    vg_vec2 out = {a.x - b.x, a.y - b.y};
    return out;
}

static vg_vec2 vg_vk_add(vg_vec2 a, vg_vec2 b) {
    vg_vec2 out = {a.x + b.x, a.y + b.y};
    return out;
}

static vg_vec2 vg_vk_scale(vg_vec2 v, float s) {
    vg_vec2 out = {v.x * s, v.y * s};
    return out;
}

static vg_vec2 vg_vk_perp(vg_vec2 v) {
    vg_vec2 out = {-v.y, v.x};
    return out;
}

static vg_vec2 vg_vk_normalize(vg_vec2 v) {
    float l = vg_vk_len(v);
    if (l <= 1e-6f) {
        vg_vec2 zero = {0.0f, 0.0f};
        return zero;
    }
    return vg_vk_scale(v, 1.0f / l);
}

static int vg_vk_emit_triangle(vg_vk_backend* backend, vg_vec2 a, vg_vec2 b, vg_vec2 c) {
    if (!vg_vk_reserve_vertices(backend, 3u)) {
        return 0;
    }

    backend->stroke_vertices[backend->stroke_vertex_count++] = a;
    backend->stroke_vertices[backend->stroke_vertex_count++] = b;
    backend->stroke_vertices[backend->stroke_vertex_count++] = c;
    return 1;
}

static int vg_vk_emit_quad(vg_vk_backend* backend, vg_vec2 p0, vg_vec2 p1, float half_width, float extend_start, float extend_end) {
    vg_vec2 axis = vg_vk_sub(p1, p0);
    vg_vec2 dir = vg_vk_normalize(axis);
    if (dir.x == 0.0f && dir.y == 0.0f) {
        return 1;
    }

    vg_vec2 n = vg_vk_scale(vg_vk_perp(dir), half_width);
    vg_vec2 t0 = vg_vk_sub(p0, vg_vk_scale(dir, extend_start));
    vg_vec2 t1 = vg_vk_add(p1, vg_vk_scale(dir, extend_end));

    vg_vec2 v0 = vg_vk_add(t0, n);
    vg_vec2 v1 = vg_vk_sub(t0, n);
    vg_vec2 v2 = vg_vk_add(t1, n);
    vg_vec2 v3 = vg_vk_sub(t1, n);

    if (!vg_vk_emit_triangle(backend, v0, v1, v2)) {
        return 0;
    }
    if (!vg_vk_emit_triangle(backend, v2, v1, v3)) {
        return 0;
    }
    return 1;
}

static int vg_vk_emit_round_cap(vg_vk_backend* backend, vg_vec2 center, vg_vec2 dir, float radius) {
    const float k_pi = 3.14159265358979323846f;
    const int steps = 12;
    vg_vec2 outward = dir;
    vg_vec2 normal = vg_vk_perp(outward);
    float prev = -k_pi * 0.5f;

    for (int i = 1; i <= steps; ++i) {
        float cur = -k_pi * 0.5f + ((float)i / (float)steps) * k_pi;

        vg_vec2 p_prev = vg_vk_add(
            center,
            vg_vk_add(vg_vk_scale(outward, cosf(prev) * radius), vg_vk_scale(normal, sinf(prev) * radius))
        );
        vg_vec2 p_cur = vg_vk_add(
            center,
            vg_vk_add(vg_vk_scale(outward, cosf(cur) * radius), vg_vk_scale(normal, sinf(cur) * radius))
        );

        if (!vg_vk_emit_triangle(backend, center, p_prev, p_cur)) {
            return 0;
        }
        prev = cur;
    }

    return 1;
}

static vg_result vg_vk_push_draw(
    vg_context* ctx,
    vg_vk_backend* backend,
    uint32_t first_vertex,
    uint32_t vertex_count,
    const vg_stroke_style* style,
    const vg_stencil_state* stencil
) {
    vg_stencil_state effective_stencil = {0};
    if (stencil) {
        effective_stencil = *stencil;
    }
    if (!vg_vk_reserve_draws(backend, 1u)) {
        return VG_ERROR_OUT_OF_MEMORY;
    }
    if (effective_stencil.enabled) {
        if (!backend->has_stencil_attachment) {
            return VG_ERROR_UNSUPPORTED;
        }
#if !VG_HAS_VK_INTERNAL_PIPELINE
        return VG_ERROR_UNSUPPORTED;
#endif
    }
    vg_rect clip_rect = {0};
    int has_clip = vg_context_get_clip(ctx, &clip_rect);
    if (backend->draw_count > 0u) {
        vg_vk_draw_cmd* prev = &backend->draws[backend->draw_count - 1u];
        if (prev->first_vertex + prev->vertex_count == first_vertex &&
            vg_vk_style_equal(&prev->style, style) &&
            vg_vk_stencil_equal(&prev->stencil, &effective_stencil) &&
            vg_vk_clip_equal(prev->has_clip, prev->clip_rect, has_clip, clip_rect)) {
            prev->vertex_count += vertex_count;
            return VG_OK;
        }
    }
    backend->draws[backend->draw_count].first_vertex = first_vertex;
    backend->draws[backend->draw_count].vertex_count = vertex_count;
    backend->draws[backend->draw_count].style = *style;
    backend->draws[backend->draw_count].stencil = effective_stencil;
    backend->draws[backend->draw_count].clip_rect = clip_rect;
    backend->draws[backend->draw_count].has_clip = has_clip;
    backend->draw_count++;
    return VG_OK;
}

static vg_result vg_vk_draw_polyline_impl(
    vg_context* ctx,
    vg_vk_backend* backend,
    const vg_vec2* points,
    size_t count,
    const vg_stroke_style* style,
    int closed
) {
    if (!backend || !points || !style || count < 2) {
        return VG_ERROR_INVALID_ARGUMENT;
    }

    float half_width = style->width_px * 0.5f;
    uint32_t first_vertex = backend->stroke_vertex_count;
    size_t seg_count = closed ? count : (count - 1u);

    for (size_t i = 0; i < seg_count; ++i) {
        size_t i0 = i;
        size_t i1 = (i + 1u) % count;

        float extend_start = 0.0f;
        float extend_end = 0.0f;
        if (!closed && style->cap == VG_LINE_CAP_SQUARE) {
            if (i == 0u) {
                extend_start = half_width;
            }
            if (i == seg_count - 1u) {
                extend_end = half_width;
            }
        }

        if (!vg_vk_emit_quad(backend, points[i0], points[i1], half_width, extend_start, extend_end)) {
            return VG_ERROR_OUT_OF_MEMORY;
        }
    }

    if (!closed && style->cap == VG_LINE_CAP_ROUND) {
        vg_vec2 start_dir = vg_vk_normalize(vg_vk_sub(points[0], points[1]));
        vg_vec2 end_dir = vg_vk_normalize(vg_vk_sub(points[count - 1u], points[count - 2u]));

        if (!vg_vk_emit_round_cap(backend, points[0], start_dir, half_width)) {
            return VG_ERROR_OUT_OF_MEMORY;
        }
        if (!vg_vk_emit_round_cap(backend, points[count - 1u], end_dir, half_width)) {
            return VG_ERROR_OUT_OF_MEMORY;
        }
    }

    return vg_vk_push_draw(ctx, backend, first_vertex, backend->stroke_vertex_count - first_vertex, style, &style->stencil);
}

static int vg_vk_append_point(vg_vec2** points, size_t* count, size_t* cap, vg_vec2 p) {
    if (*count == *cap) {
        size_t next_cap = *cap == 0 ? 64u : *cap * 2u;
        vg_vec2* next = (vg_vec2*)realloc(*points, sizeof(*next) * next_cap);
        if (!next) {
            return 0;
        }
        *points = next;
        *cap = next_cap;
    }

    (*points)[*count] = p;
    (*count)++;
    return 1;
}

static int vg_vk_append_cubic(
    vg_vec2** points,
    size_t* count,
    size_t* cap,
    vg_vec2 p0,
    vg_vec2 c0,
    vg_vec2 c1,
    vg_vec2 p1
) {
    const int subdivisions = 16;
    for (int s = 1; s <= subdivisions; ++s) {
        float t = (float)s / (float)subdivisions;
        float omt = 1.0f - t;
        vg_vec2 pt = {
            omt * omt * omt * p0.x + 3.0f * omt * omt * t * c0.x + 3.0f * omt * t * t * c1.x + t * t * t * p1.x,
            omt * omt * omt * p0.y + 3.0f * omt * omt * t * c0.y + 3.0f * omt * t * t * c1.y + t * t * t * p1.y
        };
        if (!vg_vk_append_point(points, count, cap, pt)) {
            return 0;
        }
    }
    return 1;
}

static vg_result vg_vk_flush_subpath(
    vg_context* ctx,
    vg_vk_backend* backend,
    vg_vec2** points,
    size_t* count,
    const vg_stroke_style* style,
    int closed
) {
    vg_result out = VG_OK;
    if (*count >= 2u) {
        out = vg_vk_draw_polyline_impl(ctx, backend, *points, *count, style, closed);
    }
    *count = 0;
    return out;
}

static vg_result vg_vk_submit_recorded_draws(vg_vk_backend* backend) {
    if (!backend) {
        return VG_ERROR_INVALID_ARGUMENT;
    }

    for (uint32_t i = 0; i < backend->draw_count; ++i) {
        const vg_vk_draw_cmd* cmd = &backend->draws[i];
        if (cmd->vertex_count == 0u) {
            continue;
        }
        if (cmd->first_vertex + cmd->vertex_count > backend->stroke_vertex_count) {
            return VG_ERROR_BACKEND;
        }
    }

#if VG_HAS_VULKAN
    if (backend->gpu_ready && backend->frame.command_buffer) {
        backend->command_buffer = (VkCommandBuffer)backend->frame.command_buffer;

        vg_result upload = vg_vk_upload_vertices(backend);
        if (upload != VG_OK) {
            return upload;
        }

        if (backend->stroke_vertex_count > 0u) {
            VkDeviceSize offset = 0;
            vkCmdBindVertexBuffers(
                backend->command_buffer,
                backend->vertex_binding,
                1,
                &backend->vertex_buffer.buffer,
                &offset
            );

            VkViewport viewport = {
                .x = 0.0f,
                .y = 0.0f,
                .width = (float)backend->frame.width,
                .height = (float)backend->frame.height,
                .minDepth = 0.0f,
                .maxDepth = 1.0f
            };
            VkRect2D scissor = {
                .offset = {0, 0},
                .extent = {backend->frame.width, backend->frame.height}
            };
            vkCmdSetViewport(backend->command_buffer, 0, 1, &viewport);
            vkCmdSetScissor(backend->command_buffer, 0, 1, &scissor);

#if VG_HAS_VK_INTERNAL_PIPELINE
            VkPipeline current_pipeline = VK_NULL_HANDLE;
            vg_stencil_state current_stencil = {0};
            int have_stencil_state = 0;
#endif
            VkRect2D current_scissor = scissor;
            for (int pass = 0; pass < 2; ++pass) {
                vg_blend_mode want_blend = pass == 0 ? VG_BLEND_ALPHA : VG_BLEND_ADDITIVE;
                for (uint32_t i = 0; i < backend->draw_count; ++i) {
                    const vg_vk_draw_cmd* cmd = &backend->draws[i];
                    if (cmd->vertex_count == 0u || cmd->style.blend != want_blend) {
                        continue;
                    }

                    VkRect2D draw_scissor = scissor;
                    if (cmd->has_clip) {
                        int32_t x0 = (int32_t)floorf(cmd->clip_rect.x);
                        int32_t y0 = (int32_t)floorf((float)backend->frame.height - (cmd->clip_rect.y + cmd->clip_rect.h));
                        int32_t x1 = (int32_t)ceilf(cmd->clip_rect.x + cmd->clip_rect.w);
                        int32_t y1 = (int32_t)ceilf((float)backend->frame.height - cmd->clip_rect.y);
                        if (x0 < 0) {
                            x0 = 0;
                        }
                        if (y0 < 0) {
                            y0 = 0;
                        }
                        if (x1 > (int32_t)backend->frame.width) {
                            x1 = (int32_t)backend->frame.width;
                        }
                        if (y1 > (int32_t)backend->frame.height) {
                            y1 = (int32_t)backend->frame.height;
                        }
                        if (x1 <= x0 || y1 <= y0) {
                            continue;
                        }
                        draw_scissor.offset.x = x0;
                        draw_scissor.offset.y = y0;
                        draw_scissor.extent.width = (uint32_t)(x1 - x0);
                        draw_scissor.extent.height = (uint32_t)(y1 - y0);
                    }
                    if (draw_scissor.offset.x != current_scissor.offset.x ||
                        draw_scissor.offset.y != current_scissor.offset.y ||
                        draw_scissor.extent.width != current_scissor.extent.width ||
                        draw_scissor.extent.height != current_scissor.extent.height) {
                        vkCmdSetScissor(backend->command_buffer, 0, 1, &draw_scissor);
                        current_scissor = draw_scissor;
                    }

#if VG_HAS_VK_INTERNAL_PIPELINE
                    if (backend->pipeline_layout != VK_NULL_HANDLE) {
                        vg_vk_pipeline_key key = {
                            .blend = cmd->style.blend,
                            .stencil_enabled = cmd->stencil.enabled ? 1 : 0,
                            .compare_op = cmd->stencil.enabled ? cmd->stencil.compare_op : VG_COMPARE_ALWAYS,
                            .fail_op = cmd->stencil.enabled ? cmd->stencil.fail_op : VG_STENCIL_OP_KEEP,
                            .pass_op = cmd->stencil.enabled ? cmd->stencil.pass_op : VG_STENCIL_OP_KEEP,
                            .depth_fail_op = cmd->stencil.enabled ? cmd->stencil.depth_fail_op : VG_STENCIL_OP_KEEP
                        };
                        VkPipeline needed = VK_NULL_HANDLE;
                        vg_result pipe_res = vg_vk_get_pipeline(backend, &key, &needed);
                        if (pipe_res != VG_OK) {
                            return pipe_res;
                        }
                        if (needed != VK_NULL_HANDLE && needed != current_pipeline) {
                            vkCmdBindPipeline(backend->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, needed);
                            current_pipeline = needed;
                        }
                        if (!have_stencil_state || !vg_vk_stencil_equal(&current_stencil, &cmd->stencil)) {
                            uint32_t compare_mask = cmd->stencil.compare_mask;
                            uint32_t write_mask = cmd->stencil.write_mask;
                            uint32_t reference = cmd->stencil.reference;
                            if (!cmd->stencil.enabled) {
                                compare_mask = 0xffu;
                                write_mask = 0xffu;
                                reference = 0u;
                            }
                            vkCmdSetStencilCompareMask(backend->command_buffer, VK_STENCIL_FACE_FRONT_AND_BACK, compare_mask);
                            vkCmdSetStencilWriteMask(backend->command_buffer, VK_STENCIL_FACE_FRONT_AND_BACK, write_mask);
                            vkCmdSetStencilReference(backend->command_buffer, VK_STENCIL_FACE_FRONT_AND_BACK, reference);
                            current_stencil = cmd->stencil;
                            have_stencil_state = 1;
                        }

                        vg_vk_push_constants pc = {0};
                        pc.color[0] = cmd->style.color.r;
                        pc.color[1] = cmd->style.color.g;
                        pc.color[2] = cmd->style.color.b;
                        pc.color[3] = cmd->style.color.a;
                        pc.params[0] = (float)backend->frame.width;
                        pc.params[1] = (float)backend->frame.height;
                        pc.params[2] = cmd->style.intensity;
                        pc.params[3] = 0.0f;

                        vkCmdPushConstants(
                            backend->command_buffer,
                            backend->pipeline_layout,
                            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                            0,
                            sizeof(pc),
                            &pc
                        );
                    }
#endif
                    vkCmdDraw(backend->command_buffer, cmd->vertex_count, 1, cmd->first_vertex, 0);
                }
            }
            /* Restore full scissor so client rendering after vg_end_frame is not clipped. */
            if (current_scissor.offset.x != scissor.offset.x ||
                current_scissor.offset.y != scissor.offset.y ||
                current_scissor.extent.width != scissor.extent.width ||
                current_scissor.extent.height != scissor.extent.height) {
                vkCmdSetScissor(backend->command_buffer, 0, 1, &scissor);
            }
        }
    }
#endif

    return VG_OK;
}

static float vg_vk_clamp01(float x) {
    if (x < 0.0f) {
        return 0.0f;
    }
    if (x > 1.0f) {
        return 1.0f;
    }
    return x;
}

static uint32_t vg_vk_hash_u32(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

static float vg_vk_rand_signed(uint32_t seed) {
    uint32_t h = vg_vk_hash_u32(seed);
    float t = (float)(h & 0x00ffffffu) / 8388607.5f;
    return t - 1.0f;
}

static float vg_vk_edge(vg_vec2 a, vg_vec2 b, vg_vec2 p) {
    return (p.x - a.x) * (b.y - a.y) - (p.y - a.y) * (b.x - a.x);
}

static void vg_vk_blend_pixel(
    uint8_t* px,
    vg_color color,
    float intensity,
    vg_blend_mode blend
) {
    float src_r = vg_vk_clamp01(color.r * intensity);
    float src_g = vg_vk_clamp01(color.g * intensity);
    float src_b = vg_vk_clamp01(color.b * intensity);
    float src_a = vg_vk_clamp01(color.a);

    float dst_r = px[0] / 255.0f;
    float dst_g = px[1] / 255.0f;
    float dst_b = px[2] / 255.0f;
    float dst_a = px[3] / 255.0f;

    float out_r = dst_r;
    float out_g = dst_g;
    float out_b = dst_b;
    float out_a = dst_a;

    if (blend == VG_BLEND_ADDITIVE) {
        out_r = vg_vk_clamp01(dst_r + src_r * src_a);
        out_g = vg_vk_clamp01(dst_g + src_g * src_a);
        out_b = vg_vk_clamp01(dst_b + src_b * src_a);
        out_a = vg_vk_clamp01(dst_a + src_a);
    } else {
        out_r = src_r * src_a + dst_r * (1.0f - src_a);
        out_g = src_g * src_a + dst_g * (1.0f - src_a);
        out_b = src_b * src_a + dst_b * (1.0f - src_a);
        out_a = src_a + dst_a * (1.0f - src_a);
    }

    px[0] = (uint8_t)(out_r * 255.0f + 0.5f);
    px[1] = (uint8_t)(out_g * 255.0f + 0.5f);
    px[2] = (uint8_t)(out_b * 255.0f + 0.5f);
    px[3] = (uint8_t)(out_a * 255.0f + 0.5f);
}

static int vg_vk_stencil_compare(vg_compare_op op, uint8_t current, uint8_t reference, uint8_t compare_mask) {
    uint32_t a = (uint32_t)current & compare_mask;
    uint32_t b = (uint32_t)reference & compare_mask;
    switch (op) {
        case VG_COMPARE_NEVER:
            return 0;
        case VG_COMPARE_LESS:
            return a < b;
        case VG_COMPARE_EQUAL:
            return a == b;
        case VG_COMPARE_LESS_OR_EQUAL:
            return a <= b;
        case VG_COMPARE_GREATER:
            return a > b;
        case VG_COMPARE_NOT_EQUAL:
            return a != b;
        case VG_COMPARE_GREATER_OR_EQUAL:
            return a >= b;
        case VG_COMPARE_ALWAYS:
        default:
            return 1;
    }
}

static uint8_t vg_vk_stencil_apply_op(vg_stencil_op op, uint8_t current, uint8_t reference) {
    switch (op) {
        case VG_STENCIL_OP_KEEP:
            return current;
        case VG_STENCIL_OP_ZERO:
            return 0u;
        case VG_STENCIL_OP_REPLACE:
            return reference;
        case VG_STENCIL_OP_INCREMENT_AND_CLAMP:
            return current == 0xffu ? 0xffu : (uint8_t)(current + 1u);
        case VG_STENCIL_OP_DECREMENT_AND_CLAMP:
            return current == 0u ? 0u : (uint8_t)(current - 1u);
        case VG_STENCIL_OP_INVERT:
            return (uint8_t)~current;
        case VG_STENCIL_OP_INCREMENT_AND_WRAP:
            return (uint8_t)(current + 1u);
        case VG_STENCIL_OP_DECREMENT_AND_WRAP:
        default:
            return (uint8_t)(current - 1u);
    }
}

static void vg_vk_raster_triangle(
    uint8_t* pixels,
    uint8_t* stencil,
    uint32_t width,
    uint32_t height,
    uint32_t stride,
    vg_vec2 a,
    vg_vec2 b,
    vg_vec2 c,
    vg_color color,
    float intensity,
    vg_blend_mode blend,
    const vg_stencil_state* stencil_state,
    int has_clip,
    vg_rect clip_rect
) {
    float area = vg_vk_edge(a, b, c);
    if (fabsf(area) <= 1e-8f) {
        return;
    }

    float min_xf = fminf(a.x, fminf(b.x, c.x));
    float min_yf = fminf(a.y, fminf(b.y, c.y));
    float max_xf = fmaxf(a.x, fmaxf(b.x, c.x));
    float max_yf = fmaxf(a.y, fmaxf(b.y, c.y));

    int min_x = (int)floorf(min_xf);
    int min_y = (int)floorf(min_yf);
    int max_x = (int)ceilf(max_xf);
    int max_y = (int)ceilf(max_yf);

    if (max_x < 0 || max_y < 0 || min_x >= (int)width || min_y >= (int)height) {
        return;
    }

    if (min_x < 0) {
        min_x = 0;
    }
    if (min_y < 0) {
        min_y = 0;
    }
    if (max_x > (int)width - 1) {
        max_x = (int)width - 1;
    }
    if (max_y > (int)height - 1) {
        max_y = (int)height - 1;
    }
    if (has_clip) {
        int clip_x0 = (int)floorf(clip_rect.x);
        int clip_y0 = (int)floorf((float)height - (clip_rect.y + clip_rect.h));
        int clip_x1 = (int)ceilf(clip_rect.x + clip_rect.w);
        int clip_y1 = (int)ceilf((float)height - clip_rect.y);
        if (clip_x0 < 0) {
            clip_x0 = 0;
        }
        if (clip_y0 < 0) {
            clip_y0 = 0;
        }
        if (clip_x1 > (int)width) {
            clip_x1 = (int)width;
        }
        if (clip_y1 > (int)height) {
            clip_y1 = (int)height;
        }
        if (clip_x1 <= clip_x0 || clip_y1 <= clip_y0) {
            return;
        }
        if (min_x < clip_x0) {
            min_x = clip_x0;
        }
        if (min_y < clip_y0) {
            min_y = clip_y0;
        }
        if (max_x > clip_x1 - 1) {
            max_x = clip_x1 - 1;
        }
        if (max_y > clip_y1 - 1) {
            max_y = clip_y1 - 1;
        }
        if (max_x < min_x || max_y < min_y) {
            return;
        }
    }

    float sign = area > 0.0f ? 1.0f : -1.0f;
    for (int y = min_y; y <= max_y; ++y) {
        for (int x = min_x; x <= max_x; ++x) {
            vg_vec2 p = {(float)x + 0.5f, (float)y + 0.5f};
            float e0 = sign * vg_vk_edge(a, b, p);
            float e1 = sign * vg_vk_edge(b, c, p);
            float e2 = sign * vg_vk_edge(c, a, p);
            if (e0 >= 0.0f && e1 >= 0.0f && e2 >= 0.0f) {
                if (stencil && stencil_state && stencil_state->enabled) {
                    size_t si = (size_t)y * (size_t)width + (size_t)x;
                    uint8_t old = stencil[si];
                    uint8_t ref = (uint8_t)(stencil_state->reference & 0xffu);
                    uint8_t compare_mask = (uint8_t)(stencil_state->compare_mask & 0xffu);
                    uint8_t write_mask = (uint8_t)(stencil_state->write_mask & 0xffu);
                    int pass = vg_vk_stencil_compare(stencil_state->compare_op, old, ref, compare_mask);
                    vg_stencil_op op = pass ? stencil_state->pass_op : stencil_state->fail_op;
                    uint8_t next = vg_vk_stencil_apply_op(op, old, ref);
                    stencil[si] = (uint8_t)((old & (uint8_t)(~write_mask)) | (next & write_mask));
                    if (!pass) {
                        continue;
                    }
                }
                uint8_t* px = pixels + (size_t)y * stride + (size_t)x * 4u;
                vg_vk_blend_pixel(px, color, intensity, blend);
            }
        }
    }
}

static void vg_vk_apply_bloom_rgba8(
    const vg_vk_backend* backend,
    uint8_t* pixels,
    uint32_t width,
    uint32_t height,
    uint32_t stride_bytes
) {
    if (!backend || !pixels || width == 0u || height == 0u) {
        return;
    }

    float strength = backend->crt.bloom_strength;
    if (strength <= 0.0f) {
        return;
    }

    int radius = (int)(backend->crt.bloom_radius_px + 0.5f);
    if (radius < 1) {
        radius = 1;
    }
    if (radius > 12) {
        radius = 12;
    }

    size_t count = (size_t)width * (size_t)height;
    float* src = (float*)malloc(count * 3u * sizeof(float));
    float* tmp = (float*)malloc(count * 3u * sizeof(float));
    if (!src || !tmp) {
        free(src);
        free(tmp);
        return;
    }

    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const uint8_t* px = pixels + (size_t)y * stride_bytes + (size_t)x * 4u;
            size_t i = ((size_t)y * (size_t)width + (size_t)x) * 3u;
            src[i + 0u] = px[0] / 255.0f;
            src[i + 1u] = px[1] / 255.0f;
            src[i + 2u] = px[2] / 255.0f;
        }
    }

    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            float acc[3] = {0.0f, 0.0f, 0.0f};
            int taps = 0;
            int x0 = (int)x - radius;
            int x1 = (int)x + radius;
            if (x0 < 0) {
                x0 = 0;
            }
            if (x1 >= (int)width) {
                x1 = (int)width - 1;
            }
            for (int sx = x0; sx <= x1; ++sx) {
                size_t i = ((size_t)y * (size_t)width + (size_t)sx) * 3u;
                acc[0] += src[i + 0u];
                acc[1] += src[i + 1u];
                acc[2] += src[i + 2u];
                taps++;
            }
            size_t o = ((size_t)y * (size_t)width + (size_t)x) * 3u;
            tmp[o + 0u] = acc[0] / (float)taps;
            tmp[o + 1u] = acc[1] / (float)taps;
            tmp[o + 2u] = acc[2] / (float)taps;
        }
    }

    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            float acc[3] = {0.0f, 0.0f, 0.0f};
            int taps = 0;
            int y0 = (int)y - radius;
            int y1 = (int)y + radius;
            if (y0 < 0) {
                y0 = 0;
            }
            if (y1 >= (int)height) {
                y1 = (int)height - 1;
            }
            for (int sy = y0; sy <= y1; ++sy) {
                size_t i = ((size_t)sy * (size_t)width + (size_t)x) * 3u;
                acc[0] += tmp[i + 0u];
                acc[1] += tmp[i + 1u];
                acc[2] += tmp[i + 2u];
                taps++;
            }

            uint8_t* px = pixels + (size_t)y * stride_bytes + (size_t)x * 4u;
            float out_r = vg_vk_clamp01(px[0] / 255.0f + (acc[0] / (float)taps) * strength * 0.6f);
            float out_g = vg_vk_clamp01(px[1] / 255.0f + (acc[1] / (float)taps) * strength * 0.6f);
            float out_b = vg_vk_clamp01(px[2] / 255.0f + (acc[2] / (float)taps) * strength * 0.6f);
            px[0] = (uint8_t)(out_r * 255.0f + 0.5f);
            px[1] = (uint8_t)(out_g * 255.0f + 0.5f);
            px[2] = (uint8_t)(out_b * 255.0f + 0.5f);
        }
    }

    free(src);
    free(tmp);
}

static vg_result vg_vk_debug_rasterize_rgba8(
    vg_context* ctx,
    uint8_t* pixels,
    uint32_t width,
    uint32_t height,
    uint32_t stride_bytes
) {
    vg_vk_backend* backend = vg_vk_backend_from(ctx);
    if (!backend || !pixels || width == 0u || height == 0u || stride_bytes < width * 4u) {
        return VG_ERROR_INVALID_ARGUMENT;
    }
    uint8_t* stencil = NULL;
    int need_stencil = 0;
    for (uint32_t i = 0; i < backend->draw_count; ++i) {
        if (backend->draws[i].stencil.enabled) {
            need_stencil = 1;
            break;
        }
    }
    if (need_stencil) {
        size_t stencil_count = (size_t)width * (size_t)height;
        stencil = (uint8_t*)malloc(stencil_count);
        if (!stencil) {
            return VG_ERROR_OUT_OF_MEMORY;
        }
        memset(stencil, (int)(backend->stencil_clear_requested ? (backend->stencil_clear_value & 0xffu) : 0u), stencil_count);
    }

    for (uint32_t i = 0; i < backend->draw_count; ++i) {
        const vg_vk_draw_cmd* cmd = &backend->draws[i];
        if (cmd->vertex_count < 3u) {
            continue;
        }
        uint32_t end = cmd->first_vertex + cmd->vertex_count;
        if (end > backend->stroke_vertex_count) {
            return VG_ERROR_BACKEND;
        }
        float flicker = backend->crt.flicker_amount;
        float flicker_noise = vg_vk_rand_signed((uint32_t)backend->frame_index * 7919u + i * 104729u);
        float cmd_intensity = cmd->style.intensity * (1.0f + flicker * flicker_noise);
        if (cmd_intensity < 0.0f) {
            cmd_intensity = 0.0f;
        }

        float jitter = backend->crt.jitter_amount;
        float jx = jitter * vg_vk_rand_signed((uint32_t)backend->frame_index * 1009u + i * 9176u);
        float jy = jitter * vg_vk_rand_signed((uint32_t)backend->frame_index * 2473u + i * 3083u);

        for (uint32_t v = cmd->first_vertex; v + 2u < end; v += 3u) {
            vg_vec2 a = backend->stroke_vertices[v];
            vg_vec2 b = backend->stroke_vertices[v + 1u];
            vg_vec2 c = backend->stroke_vertices[v + 2u];
            a.x += jx;
            a.y += jy;
            b.x += jx;
            b.y += jy;
            c.x += jx;
            c.y += jy;
            vg_vk_raster_triangle(
                pixels,
                stencil,
                width,
                height,
                stride_bytes,
                a,
                b,
                c,
                cmd->style.color,
                cmd_intensity,
                cmd->style.blend,
                &cmd->stencil,
                cmd->has_clip,
                cmd->clip_rect
            );
        }
    }

    vg_vk_apply_bloom_rgba8(backend, pixels, width, height, stride_bytes);
    free(stencil);
    return VG_OK;
}

static void vg_vk_destroy(vg_context* ctx) {
    vg_vk_backend* backend = vg_vk_backend_from(ctx);
#if VG_HAS_VULKAN
    if (backend && backend->gpu_ready) {
        vkDeviceWaitIdle(backend->device);
#if VG_HAS_VK_INTERNAL_PIPELINE
        vg_vk_destroy_pipelines(backend);
#endif
        vg_vk_destroy_gpu_buffer(backend, &backend->vertex_buffer);
    }
#endif
    free(backend->stroke_vertices);
    free(backend->draws);
    free(backend);
    ctx->backend.impl = NULL;
}

static vg_result vg_vk_begin_frame(vg_context* ctx, const vg_frame_desc* frame) {
    vg_vk_backend* backend = vg_vk_backend_from(ctx);
    if (!backend || !frame) {
        return VG_ERROR_INVALID_ARGUMENT;
    }

    backend->frame = *frame;
    backend->frame_index++;
    backend->stroke_vertex_count = 0;
    backend->draw_count = 0;
    backend->stencil_clear_requested = 0;
    backend->stencil_clear_value = 0u;
    return VG_OK;
}

static vg_result vg_vk_end_frame(vg_context* ctx) {
    vg_vk_backend* backend = vg_vk_backend_from(ctx);
    return vg_vk_submit_recorded_draws(backend);
}

static vg_result vg_vk_stencil_clear(vg_context* ctx, uint32_t value) {
    vg_vk_backend* backend = vg_vk_backend_from(ctx);
    if (!backend) {
        return VG_ERROR_INVALID_ARGUMENT;
    }
    if (!backend->has_stencil_attachment) {
        return VG_ERROR_UNSUPPORTED;
    }
    backend->stencil_clear_requested = 1;
    backend->stencil_clear_value = value & 0xffu;
#if VG_HAS_VULKAN
    if (!backend->gpu_ready || !backend->frame.command_buffer) {
        return VG_OK;
    }
    VkCommandBuffer cmd = (VkCommandBuffer)backend->frame.command_buffer;
    VkClearAttachment clear = {
        .aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT,
        .clearValue = {.depthStencil = {.depth = 1.0f, .stencil = value & 0xffu}}
    };
    VkClearRect rect = {
        .rect = {
            .offset = {0, 0},
            .extent = {backend->frame.width, backend->frame.height}
        },
        .baseArrayLayer = 0,
        .layerCount = 1
    };
    vkCmdClearAttachments(cmd, 1, &clear, 1, &rect);
    return VG_OK;
#else
    return VG_OK;
#endif
}

static void vg_vk_set_retro_params(vg_context* ctx, const vg_retro_params* params) {
    vg_vk_backend* backend = vg_vk_backend_from(ctx);
    if (!backend || !params) {
        return;
    }
    backend->retro = *params;
    backend->crt.bloom_strength = params->bloom_strength;
    backend->crt.bloom_radius_px = params->bloom_radius_px;
    backend->crt.persistence_decay = params->persistence_decay;
    backend->crt.jitter_amount = params->jitter_amount;
    backend->crt.flicker_amount = params->flicker_amount;
}

static void vg_vk_set_crt_profile(vg_context* ctx, const vg_crt_profile* profile) {
    vg_vk_backend* backend = vg_vk_backend_from(ctx);
    if (!backend || !profile) {
        return;
    }
    backend->crt = *profile;
    backend->retro.bloom_strength = profile->bloom_strength;
    backend->retro.bloom_radius_px = profile->bloom_radius_px;
    backend->retro.persistence_decay = profile->persistence_decay;
    backend->retro.jitter_amount = profile->jitter_amount;
    backend->retro.flicker_amount = profile->flicker_amount;
}

static vg_result vg_vk_draw_path_stroke(vg_context* ctx, const vg_path* path, const vg_stroke_style* style) {
    vg_vk_backend* backend = vg_vk_backend_from(ctx);
    if (!backend || !path || !style) {
        return VG_ERROR_INVALID_ARGUMENT;
    }

    vg_vec2* points = NULL;
    size_t count = 0;
    size_t cap = 0;
    for (size_t i = 0; i < path->count; ++i) {
        vg_path_cmd cmd = path->cmds[i];
        switch (cmd.type) {
            case VG_CMD_MOVE_TO: {
                vg_result flush = vg_vk_flush_subpath(ctx, backend, &points, &count, style, 0);
                if (flush != VG_OK) {
                    free(points);
                    return flush;
                }
                if (!vg_vk_append_point(&points, &count, &cap, cmd.p[0])) {
                    free(points);
                    return VG_ERROR_OUT_OF_MEMORY;
                }
                break;
            }
            case VG_CMD_LINE_TO:
                if (count == 0u) {
                    free(points);
                    return VG_ERROR_INVALID_ARGUMENT;
                }
                if (!vg_vk_append_point(&points, &count, &cap, cmd.p[0])) {
                    free(points);
                    return VG_ERROR_OUT_OF_MEMORY;
                }
                break;
            case VG_CMD_CUBIC_TO:
                if (count == 0u) {
                    free(points);
                    return VG_ERROR_INVALID_ARGUMENT;
                }
                if (!vg_vk_append_cubic(&points, &count, &cap, points[count - 1u], cmd.p[0], cmd.p[1], cmd.p[2])) {
                    free(points);
                    return VG_ERROR_OUT_OF_MEMORY;
                }
                break;
            case VG_CMD_CLOSE: {
                vg_result flush = vg_vk_flush_subpath(ctx, backend, &points, &count, style, 1);
                if (flush != VG_OK) {
                    free(points);
                    return flush;
                }
                break;
            }
            default:
                free(points);
                return VG_ERROR_INVALID_ARGUMENT;
                break;
        }
    }

    vg_result out = vg_vk_flush_subpath(ctx, backend, &points, &count, style, 0);
    free(points);
    return out;
}

static vg_result vg_vk_draw_polyline(vg_context* ctx, const vg_vec2* points, size_t count, const vg_stroke_style* style, int closed) {
    vg_vk_backend* backend = vg_vk_backend_from(ctx);
    return vg_vk_draw_polyline_impl(ctx, backend, points, count, style, closed);
}

static vg_result vg_vk_fill_convex(vg_context* ctx, const vg_vec2* points, size_t count, const vg_fill_style* style) {
    vg_vk_backend* backend = vg_vk_backend_from(ctx);
    if (!backend || !points || !style || count < 3u) {
        return VG_ERROR_INVALID_ARGUMENT;
    }
    uint32_t first_vertex = backend->stroke_vertex_count;
    for (size_t i = 1; i + 1 < count; ++i) {
        if (!vg_vk_emit_triangle(backend, points[0], points[i], points[i + 1u])) {
            return VG_ERROR_OUT_OF_MEMORY;
        }
    }
    vg_stroke_style draw_style = {
        .width_px = 1.0f,
        .intensity = style->intensity,
        .color = style->color,
        .cap = VG_LINE_CAP_BUTT,
        .join = VG_LINE_JOIN_BEVEL,
        .miter_limit = 1.0f,
        .blend = style->blend,
        .stencil = style->stencil
    };
    return vg_vk_push_draw(ctx, backend, first_vertex, backend->stroke_vertex_count - first_vertex, &draw_style, &style->stencil);
}

vg_result vg_vk_backend_create(vg_context* ctx) {
    static const vg_backend_ops k_ops = {
        .destroy = vg_vk_destroy,
        .begin_frame = vg_vk_begin_frame,
        .end_frame = vg_vk_end_frame,
        .set_retro_params = vg_vk_set_retro_params,
        .set_crt_profile = vg_vk_set_crt_profile,
        .draw_path_stroke = vg_vk_draw_path_stroke,
        .draw_polyline = vg_vk_draw_polyline,
        .fill_convex = vg_vk_fill_convex,
        .stencil_clear = vg_vk_stencil_clear,
        .debug_rasterize_rgba8 = vg_vk_debug_rasterize_rgba8
    };

    if (!ctx) {
        return VG_ERROR_INVALID_ARGUMENT;
    }

    vg_vk_backend* backend = (vg_vk_backend*)calloc(1, sizeof(*backend));
    if (!backend) {
        return VG_ERROR_OUT_OF_MEMORY;
    }

    backend->desc = ctx->desc.api.vulkan;
    if (backend->desc.max_frames_in_flight == 0) {
        backend->desc.max_frames_in_flight = 2;
    }
    if (backend->desc.vertex_binding > 15u) {
        backend->desc.vertex_binding = 0u;
    }
    backend->desc.raster_samples = vg_vk_sanitize_raster_samples(backend->desc.raster_samples);
    backend->retro = ctx->retro;
    backend->crt = ctx->crt;
    backend->raster_samples = backend->desc.raster_samples;
    backend->has_stencil_attachment = backend->desc.has_stencil_attachment ? 1 : 0;

#if VG_HAS_VULKAN
    backend->physical_device = (VkPhysicalDevice)backend->desc.physical_device;
    backend->device = (VkDevice)backend->desc.device;
    backend->render_pass = (VkRenderPass)backend->desc.render_pass;
    backend->vertex_binding = backend->desc.vertex_binding;

    if (backend->physical_device != VK_NULL_HANDLE && backend->device != VK_NULL_HANDLE) {
        VkPhysicalDeviceMemoryProperties props = {0};
        vkGetPhysicalDeviceMemoryProperties(backend->physical_device, &props);

        uint32_t all_bits = 0xffffffffu;
        uint32_t memory_type = vg_vk_find_memory_type(
            &props,
            all_bits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );
        if (memory_type != UINT32_MAX) {
            backend->upload_memory_type_index = memory_type;
            backend->gpu_ready = 1;
        }
    }

#if VG_HAS_VK_INTERNAL_PIPELINE
    if (backend->gpu_ready && backend->render_pass != VK_NULL_HANDLE) {
        vg_result pipe_res = vg_vk_create_pipelines(backend);
        if (pipe_res != VG_OK) {
            vg_vk_destroy_pipelines(backend);
        }
    }
#endif
#endif

    ctx->backend.ops = &k_ops;
    ctx->backend.impl = backend;
    return VG_OK;
}
