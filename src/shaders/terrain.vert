#version 450

layout(location = 0) in vec3 in_pos;
layout(location = 0) out vec4 v_color;
layout(location = 1) out vec3 v_pos;

layout(push_constant) uniform Push {
    vec4 color;
    vec4 params; /* x=viewport_width, y=viewport_height, z=intensity */
} pc;

void main() {
    vec2 ndc;
    ndc.x = (in_pos.x / pc.params.x) * 2.0 - 1.0;
    ndc.y = 1.0 - (in_pos.y / pc.params.y) * 2.0;
    gl_Position = vec4(ndc, in_pos.z, 1.0);
    v_color = vec4(pc.color.rgb * pc.params.z, pc.color.a);
    v_pos = in_pos;
}
