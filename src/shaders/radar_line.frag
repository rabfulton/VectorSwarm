#version 450

layout(location = 0) in float v_depth;
layout(location = 1) in float v_fade;
layout(location = 0) out vec4 out_color;

layout(push_constant) uniform Push {
    vec4 params; /* x=viewport_width, y=viewport_height, z=intensity, w=depth_pow */
    vec4 color;
    vec4 offset;
} pc;

void main() {
    float depth_pow = max(pc.params.w, 0.01);
    float zfade = max(0.02, pow(clamp(v_depth, 0.0, 1.0), depth_pow));
    float line_fade = clamp(v_fade, 0.0, 1.0);
    float alpha = pc.color.a * line_fade * zfade;
    vec3 rgb = pc.color.rgb * pc.params.z * mix(0.18, 1.0, zfade);
    out_color = vec4(rgb, alpha);
}
