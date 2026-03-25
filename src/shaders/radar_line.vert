#version 450

layout(location = 0) in vec3 in_pos;
layout(location = 1) in float in_fade;
layout(location = 2) in float in_cell_id;

layout(set = 0, binding = 0) uniform sampler2D u_grid_state;

layout(location = 0) out float v_depth;
layout(location = 1) out float v_fade;
layout(location = 2) flat out int v_cell_id;

layout(push_constant) uniform Push {
    vec4 params; /* x=viewport_width, y=viewport_height, z=intensity, w=depth_pow */
    vec4 color;
    vec4 offset; /* x=offset_px_x, y=offset_px_y, z=grid_distort_px */
} pc;

void main() {
    vec2 p = in_pos.xy + pc.offset.xy;
    if (pc.offset.z > 0.0) {
        vec2 uv = vec2(
            clamp(p.x / max(pc.params.x, 1.0), 0.0, 1.0),
            clamp(p.y / max(pc.params.y, 1.0), 0.0, 1.0)
        );
        vec2 disp = textureLod(u_grid_state, uv, 0.0).xy * pc.offset.z;
        p += disp;
    }
    vec2 ndc;
    ndc.x = (p.x / pc.params.x) * 2.0 - 1.0;
    ndc.y = 1.0 - (p.y / pc.params.y) * 2.0;
    gl_Position = vec4(ndc, in_pos.z, 1.0);
    v_depth = clamp(in_pos.z, 0.0, 1.0);
    v_fade = in_fade;
    v_cell_id = (in_cell_id >= 0.0) ? int(in_cell_id + 0.5) : -1;
}
