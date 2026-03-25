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

vec3 kind_base_color(float kind) {
    if (kind < 0.5) {
        return mix(pc.color0.rgb, pc.color1.rgb, 0.10);
    }
    if (kind < 1.5) {
        return mix(pc.color0.rgb, vec3(1.0), 0.08);
    }
    if (kind < 2.5) {
        return mix(pc.color0.rgb, pc.color2.rgb, 0.18);
    }
    if (kind < 3.5) {
        return mix(pc.color0.rgb, pc.color2.rgb, 0.08);
    }
    if (kind < 4.5) {
        return mix(pc.color0.rgb, pc.color2.rgb, 0.40);
    }
    return mix(pc.color1.rgb, pc.color0.rgb, 0.34);
}

void kind_profile(
    float kind,
    out float base_alpha,
    out float back_alpha,
    out float dash_mix,
    out float pulse_gain,
    out float rim_gain
) {
    if (kind < 0.5) {
        base_alpha = 0.58;
        back_alpha = 0.12;
        dash_mix = 0.22;
        pulse_gain = 0.04;
        rim_gain = 0.18;
        return;
    }
    if (kind < 1.5) {
        base_alpha = 0.34;
        back_alpha = 0.08;
        dash_mix = 0.18;
        pulse_gain = 0.05;
        rim_gain = 0.08;
        return;
    }
    if (kind < 2.5) {
        base_alpha = 0.42;
        back_alpha = 0.10;
        dash_mix = 0.46;
        pulse_gain = 0.24;
        rim_gain = 0.10;
        return;
    }
    if (kind < 3.5) {
        base_alpha = 0.20;
        back_alpha = 0.05;
        dash_mix = 0.04;
        pulse_gain = 0.08;
        rim_gain = 0.08;
        return;
    }
    if (kind < 4.5) {
        base_alpha = 0.10;
        back_alpha = 0.03;
        dash_mix = 0.00;
        pulse_gain = 0.58;
        rim_gain = 0.04;
        return;
    }
    base_alpha = 0.40;
    back_alpha = 0.10;
    dash_mix = 0.22;
    pulse_gain = 0.12;
    rim_gain = 0.16;
}

void main() {
    float base_alpha;
    float back_alpha;
    float dash_mix;
    float pulse_gain;
    float rim_gain;
    float front = smoothstep(-0.01, 0.24, v_view.z);
    float back = 1.0 - front;
    float rear_alpha = mix(0.18, 1.0, front);
    float rear_rgb = mix(0.26, 1.0, front);
    float limb = pow(clamp(1.0 - abs(v_view.z), 0.0, 1.0), 1.35);
    vec2 screen_px = v_screen_uv * pc.p0.xy;
    float holo = texture(u_holo_mask, fract(screen_px / vec2(7.0, 5.0))).r;
    float noise = texture(u_blue_noise, fract(screen_px / 64.0)).r;
    float mask_strength = clamp(pc.tune1.z, 0.0, 1.0);
    float coverage = 1.0 + (holo - 0.5) * 0.10 * mask_strength + (noise - 0.5) * 0.08 * mask_strength;
    float dash_phase = fract(v_arc * (pc.tune0.z + v_kind * 2.7));
    float dash_wave = 0.5 + 0.5 * sin((dash_phase + v_kind * 0.17) * 6.28318530718);
    float back_dash = smoothstep(0.28, 0.76, dash_wave);
    float packet = exp(-pow(wrap_dist(fract(v_arc * pc.tune0.x - pc.p0.z * pc.tune0.y), 0.5) / 0.22, 2.0));
    float beacon = 0.5 + 0.5 * sin(pc.p0.z * pc.tune0.w + v_arc * 12.56637061436);
    float crown = 0.5 + 0.5 * sin(v_arc * 18.84955592154 + pc.p0.z * 0.32);
    float sweep = exp(-pow(wrap_dist(fract(v_arc), pc.tune1.x) / max(pc.tune1.y, 0.001), 2.0));
    float highlight = 0.0;
    vec3 accent = pc.color0.rgb;

    kind_profile(v_kind, base_alpha, back_alpha, dash_mix, pulse_gain, rim_gain);

    if (v_kind >= 2.0 && v_kind < 2.5) {
        highlight = packet * pulse_gain;
        accent = pc.color2.rgb;
    } else if (v_kind >= 3.0 && v_kind < 3.5) {
        highlight = (0.25 + 0.45 * beacon) * pulse_gain;
        accent = mix(pc.color0.rgb, pc.color2.rgb, 0.12);
    } else if (v_kind >= 4.0 && v_kind < 4.5) {
        highlight = sweep * pulse_gain;
        accent = mix(pc.color2.rgb, pc.color0.rgb, 0.40);
    } else if (v_kind >= 5.0) {
        highlight = crown * pulse_gain;
        accent = mix(pc.color0.rgb, vec3(1.0), 0.10);
    }

    float alpha = v_weight * mix(back_alpha, base_alpha, front);
    alpha *= mix(1.0, back_dash, back * dash_mix);
    alpha *= 0.84 + limb * rim_gain;
    alpha += highlight * (0.08 + 0.20 * front);
    alpha *= coverage * pc.p0.w * rear_alpha;
    alpha = clamp(alpha, 0.0, 1.0);
    if (alpha < 0.010) {
        discard;
    }

    vec3 rgb = kind_base_color(v_kind);
    rgb *= 0.76 + 0.34 * front + limb * rim_gain * 0.34;
    rgb += accent * highlight * (0.34 + 0.42 * front);
    rgb += pc.color1.rgb * limb * 0.03;
    rgb *= rear_rgb;
    out_color = vec4(rgb, alpha);
}
