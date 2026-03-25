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

void glow_profile(float kind, out float base_glow, out float event_glow, out float rim_gain) {
    if (kind < 0.5) {
        base_glow = 0.08;
        event_glow = 0.04;
        rim_gain = 0.16;
        return;
    }
    if (kind < 1.5) {
        base_glow = 0.04;
        event_glow = 0.06;
        rim_gain = 0.06;
        return;
    }
    if (kind < 2.5) {
        base_glow = 0.12;
        event_glow = 0.22;
        rim_gain = 0.08;
        return;
    }
    if (kind < 3.5) {
        base_glow = 0.05;
        event_glow = 0.07;
        rim_gain = 0.04;
        return;
    }
    if (kind < 4.5) {
        base_glow = 0.05;
        event_glow = 0.42;
        rim_gain = 0.04;
        return;
    }
    base_glow = 0.10;
    event_glow = 0.14;
    rim_gain = 0.14;
}

void main() {
    float base_glow;
    float event_glow;
    float rim_gain;
    float front = smoothstep(0.00, 0.24, v_view.z);
    float rear_dim = mix(0.10, 1.0, front);
    float limb = pow(clamp(1.0 - abs(v_view.z), 0.0, 1.0), 1.10);
    float packet = exp(-pow(wrap_dist(fract(v_arc * pc.tune0.x - pc.p0.z * pc.tune0.y), 0.5) / 0.26, 2.0));
    float beacon = 0.5 + 0.5 * sin(pc.p0.z * pc.tune0.w + v_arc * 12.56637061436);
    float sweep = exp(-pow(wrap_dist(fract(v_arc), pc.tune1.x) / max(pc.tune1.y, 0.001), 2.0));
    float crown = 0.5 + 0.5 * sin(v_arc * 18.84955592154 + pc.p0.z * 0.32);
    vec2 screen_px = v_screen_uv * pc.p0.xy;
    float mask = texture(u_holo_mask, fract(screen_px / vec2(9.0, 6.0))).r;
    float noise = texture(u_blue_noise, fract(screen_px / 64.0)).r;
    float pass_boost = mix(1.0, 1.55, clamp(pc.tune1.w, 0.0, 1.0));
    float event = 0.0;
    vec3 color = pc.color0.rgb;

    glow_profile(v_kind, base_glow, event_glow, rim_gain);

    if (v_kind >= 2.0 && v_kind < 2.5) {
        event = packet;
        color = pc.color2.rgb;
    } else if (v_kind >= 3.0 && v_kind < 3.5) {
        event = 0.25 + 0.45 * beacon;
        color = mix(pc.color0.rgb, pc.color2.rgb, 0.14);
    } else if (v_kind >= 4.0 && v_kind < 4.5) {
        event = sweep;
        color = mix(pc.color2.rgb, pc.color0.rgb, 0.32);
    } else if (v_kind >= 5.0) {
        event = crown;
        color = mix(pc.color0.rgb, vec3(1.0), 0.14);
    }

    float glow = v_weight * base_glow;
    glow += event * event_glow;
    glow += limb * rim_gain * (0.18 + 0.30 * front);
    glow *= (0.94 + (mask - 0.5) * 0.08 + (noise - 0.5) * 0.06);
    glow *= pc.p0.w * pass_boost * rear_dim;
    if (glow < 0.004) {
        discard;
    }

    out_color = vec4(color * glow * rear_dim, clamp(glow * (0.08 + 0.10 * front), 0.0, 1.0));
}
