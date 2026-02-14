#version 450

layout(location = 0) in float v_fade;
layout(location = 0) out vec4 out_color;

layout(push_constant) uniform Push {
    vec4 params; /* x=viewport_width, y=viewport_height, z=intensity */
    vec4 color;
    vec4 offset;
} pc;

void main() {
    out_color = vec4(pc.color.rgb * pc.params.z, pc.color.a * clamp(v_fade, 0.0, 1.0));
}
