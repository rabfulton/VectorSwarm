#version 450

layout(location = 0) in float v_depth;
layout(location = 1) in float v_fade;
layout(location = 2) flat in int v_cell_id;
layout(location = 0) out vec4 out_color;

layout(push_constant) uniform Push {
    vec4 params; /* x=viewport_width, y=viewport_height, z=intensity, w=depth_pow */
    vec4 color;
    vec4 offset;
} pc;

float hash11(float p) {
    p = fract(p * 0.1031);
    p *= p + 33.33;
    p *= p + p;
    return fract(p);
}

void main() {
    float depth_pow = max(pc.params.w, 0.01);
    float zfade = max(0.02, pow(clamp(v_depth, 0.0, 1.0), depth_pow));
    float line_fade = clamp(min(v_fade, 1.0), 0.0, 1.0);
    float flash = max(v_fade - 1.0, 0.0);
    float alpha = pc.color.a * line_fade * zfade;
    vec3 rgb = pc.color.rgb * pc.params.z * mix(0.18, 1.0, zfade);
    if (v_cell_id >= 0) {
        float cell_jit = hash11(float(v_cell_id) + 0.17);
        float cell_tone = mix(0.78, 1.18, cell_jit);
        float cell_alpha = mix(0.08, 0.22, cell_jit) * line_fade * zfade;
        rgb *= cell_tone;
        alpha += cell_alpha;
    }
    if (flash > 0.0 && v_cell_id >= 0) {
        float flash_gain = clamp(flash, 0.0, 0.9);
        vec3 flash_rgb = mix(pc.color.rgb, vec3(1.0), 0.18);
        rgb = mix(rgb, flash_rgb * (pc.params.z * (1.20 + 0.18 * flash_gain)), clamp(flash_gain * 0.55, 0.0, 1.0));
        alpha += flash_gain * line_fade * zfade * 0.42;
    } else if (flash > 0.0) {
        vec3 flash_rgb = mix(pc.color.rgb, vec3(1.0), 0.45);
        rgb = mix(rgb, flash_rgb * (pc.params.z * 1.25), 0.65 * flash);
        alpha += flash * line_fade * zfade * 0.34;
    }
    out_color = vec4(rgb, alpha);
}
