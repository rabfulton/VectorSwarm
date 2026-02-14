#version 450

layout(location = 0) in vec3 in_pos;
layout(location = 1) in float in_fade;

layout(location = 0) out float v_fade;

layout(push_constant) uniform Push {
    vec4 params; /* x=viewport_width, y=viewport_height, z=intensity */
    vec4 color;
    vec4 offset; /* x=offset_px_x, y=offset_px_y */
} pc;

void main() {
    vec2 p = in_pos.xy + pc.offset.xy;
    vec2 ndc;
    ndc.x = (p.x / pc.params.x) * 2.0 - 1.0;
    ndc.y = 1.0 - (p.y / pc.params.y) * 2.0;
    gl_Position = vec4(ndc, in_pos.z, 1.0);
    v_fade = in_fade;
}
