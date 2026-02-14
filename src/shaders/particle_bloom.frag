#version 450

layout(location = 0) in vec4 v_col;
layout(location = 1) in vec2 v_uv;
layout(location = 2) in float v_kind;
layout(location = 3) in float v_trail;
layout(location = 4) in float v_heat;
layout(location = 0) out vec4 out_color;

layout(push_constant) uniform Push {
    vec4 params; /* x=viewport_width, y=viewport_height, z=core_gain, w=trail_gain */
} pc;

void main() {
    float r = length(v_uv);
    if (r > 1.0) {
        discard;
    }
    float tx = clamp(-v_uv.x, 0.0, 1.0);
    float bloom = exp(-2.1 * r);
    float core = 1.0 - smoothstep(0.0, 0.32, r);
    float streak = exp(-8.0 * abs(v_uv.y)) * tx * tx * (0.20 + 0.80 * v_trail);
    float heat = clamp(v_heat, 0.0, 2.0);
    vec3 hot = vec3(1.0, 0.95, 0.72);
    vec3 cool = vec3(1.0, 0.58, 0.22);
    vec3 tint = mix(cool, hot, clamp(heat, 0.0, 1.0));
    float gain = 0.36 + 0.16 * pc.params.w;
    if (v_kind > 1.5) {
        gain *= 1.05;
    }
    float intensity = (bloom * 0.40 + core * 0.24 + streak * 0.34) * gain * (0.62 + 0.24 * heat);
    float alpha = (bloom * 0.13 + streak * 0.06) * v_col.a;
    out_color = vec4(v_col.rgb * tint * intensity, alpha);
}
