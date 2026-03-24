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

vec3 line_base_color(float kind) {
    if (kind < 0.5) {
        return mix(pc.color0.rgb, pc.color1.rgb, 0.12);
    }
    if (kind < 1.5) {
        return mix(pc.color0.rgb, pc.color1.rgb, 0.22);
    }
    if (kind < 2.5) {
        return mix(pc.color0.rgb, pc.color2.rgb, 0.10);
    }
    return mix(pc.color1.rgb, pc.color2.rgb, 0.34);
}

void line_kind_profile(float kind, out float alpha_mul, out float sweep_mul, out float back_mul) {
    if (kind < 0.5) {
        alpha_mul = 0.74;
        sweep_mul = 0.18;
        back_mul = 0.62;
        return;
    }
    if (kind < 1.5) {
        alpha_mul = 0.96;
        sweep_mul = 0.24;
        back_mul = 0.74;
        return;
    }
    if (kind < 2.5) {
        alpha_mul = 0.82;
        sweep_mul = 0.20;
        back_mul = 0.68;
        return;
    }
    alpha_mul = 0.84;
    sweep_mul = 0.34;
    back_mul = 0.60;
}

void main() {
    float alpha_mul;
    float sweep_mul;
    float back_mul;
    line_kind_profile(v_kind, alpha_mul, sweep_mul, back_mul);

    float front = smoothstep(-0.12, 0.18, v_view.z);
    float sweep = exp(-pow(wrap_dist(fract(v_arc), pc.tune1.x) / max(pc.tune1.y, 0.001), 2.0));
    float dash_phase = v_arc * (4.2 + pc.tune0.z * 0.48) + pc.p0.z * (0.04 + 0.012 * v_kind);
    float dash_wave = 0.5 + 0.5 * sin(dash_phase * 6.28318530718);
    float back_dash = smoothstep(0.46, 0.82, dash_wave);
    float visibility = mix(0.74 * back_mul, 1.0, front);
    float noise = texture(u_blue_noise, fract(gl_FragCoord.xy / 64.0 + vec2(pc.p0.z * 0.005, pc.p0.z * 0.008))).r;
    float holo = texture(u_holo_mask, fract(gl_FragCoord.xy / 64.0 + v_screen_uv * 0.08)).r;
    float mod = mix(0.95, 1.00, holo) * mix(0.96, 1.04, noise);

    vec3 base = line_base_color(v_kind);
    float alpha = v_weight * alpha_mul * visibility * mix(0.82 + 0.18 * back_dash, 1.0, front);
    alpha *= mod;
    alpha += sweep * sweep_mul * (0.04 + 0.06 * front);
    alpha = clamp(alpha * pc.p0.w, 0.0, 1.0);
    if (alpha < 0.010) {
        discard;
    }

    vec3 rgb = base * (0.80 + 0.34 * visibility);
    rgb += pc.color2.rgb * sweep * sweep_mul * 0.16;
    out_color = vec4(rgb, alpha);
}
