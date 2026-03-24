#version 450

layout(set = 0, binding = 0) uniform sampler2D u_blue_noise;
layout(set = 0, binding = 1) uniform sampler2D u_holo_mask;

layout(location = 0) in vec3 v_view;
layout(location = 1) in float v_kind;
layout(location = 2) in float v_arc;
layout(location = 3) in float v_weight;
layout(location = 4) in vec2 v_screen_uv;

layout(location = 0) out vec4 out_color;

layout(push_constant) uniform Push {
    vec4 p0;     /* x=viewport_w, y=viewport_h, z=time_s, w=style_gain */
    vec4 p1;     /* x=center_x, y=center_y, z=sphere_radius_px, w=dpi */
    vec4 q;
    vec4 color0;
    vec4 color1;
    vec4 color2;
    vec4 tune0;
    vec4 tune1;
} pc;

float wrap_dist(float a, float b) {
    float d = abs(a - b);
    return min(d, 1.0 - d);
}

void glow_kind_profile(float kind, out float glow_mul, out float sweep_mul) {
    if (kind < 0.5) {
        glow_mul = 0.22;
        sweep_mul = 0.16;
        return;
    }
    if (kind < 1.5) {
        glow_mul = 0.44;
        sweep_mul = 0.24;
        return;
    }
    if (kind < 2.5) {
        glow_mul = 0.30;
        sweep_mul = 0.18;
        return;
    }
    glow_mul = 0.40;
    sweep_mul = 0.34;
}

void main() {
    float glow_mul;
    float sweep_mul;
    glow_kind_profile(v_kind, glow_mul, sweep_mul);

    float front = smoothstep(-0.14, 0.20, v_view.z);
    float sweep = exp(-pow(wrap_dist(fract(v_arc), pc.tune1.x) / max(pc.tune1.y, 0.001), 2.0));
    float noise = texture(u_blue_noise, fract(gl_FragCoord.xy / 64.0 + vec2(pc.p0.z * 0.003, pc.p0.z * 0.007))).r;
    float holo = texture(u_holo_mask, fract(gl_FragCoord.xy / 64.0 + v_screen_uv * 0.10)).r;
    float glow = v_weight * glow_mul * (0.16 + sweep * sweep_mul * (0.46 + 0.30 * front));
    glow *= mix(0.94, 1.06, noise) * mix(0.92, 1.00, holo) * pc.p0.w;
    if (glow < 0.004) {
        discard;
    }

    vec3 base = mix(pc.color0.rgb, pc.color2.rgb, 0.12 + 0.10 * step(2.5, v_kind));
    out_color = vec4(base * glow, clamp(glow * mix(0.06, 0.12, front), 0.0, 1.0));
}
