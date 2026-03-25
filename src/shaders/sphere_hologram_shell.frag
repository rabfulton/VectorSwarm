#version 450

layout(set = 0, binding = 0) uniform sampler2D u_blue_noise;
layout(set = 0, binding = 1) uniform sampler2D u_holo_mask;

layout(location = 0) in vec3 v_local;
layout(location = 1) in vec3 v_view;

layout(location = 0) out vec4 out_color;

layout(push_constant) uniform Push {
    vec4 p0;     /* x=viewport_w, y=viewport_h, z=time_s, w=style_gain */
    vec4 p1;     /* x=center_x, y=center_y, z=sphere_radius_px, w=dpi */
    vec4 q;
    vec4 color0;
    vec4 color1;
    vec4 color2;
    vec4 tune0;  /* x=shell_offset, y=layer_t, z=layer_alpha, w=seed */
    vec4 tune1;  /* x=sweep_phase, y=sweep_width, z=panel_gain, w=rim_gain */
} pc;

const float k_pi = 3.14159265359;

vec2 sphere_uv(vec3 n) {
    float lon = atan(n.z, n.x) / (2.0 * k_pi) + 0.5;
    return vec2(fract(lon), clamp(n.y * 0.5 + 0.5, 0.0, 1.0));
}

float wrap_dist(float a, float b) {
    float d = abs(a - b);
    return min(d, 1.0 - d);
}

void main() {
    vec3 local_n = normalize(v_local);
    vec3 view_n = normalize(v_view);
    vec2 uv = sphere_uv(local_n);
    float front = smoothstep(-0.01, 0.24, view_n.z);
    if (front <= 0.001) {
        discard;
    }
    float rear_alpha = mix(0.22, 1.0, front);
    float rear_rgb = mix(0.34, 1.0, front);

    float layer_t = clamp(pc.tune0.y, 0.0, 1.0);
    float layer_alpha = max(pc.tune0.z, 0.0);
    float rim_gain = clamp(pc.tune1.w, 0.0, 2.0);
    float panel_gain = clamp(pc.tune1.z, 0.0, 1.5);
    float core = pow(clamp(max(view_n.z, 0.0), 0.0, 1.0), 0.55);
    float rim = pow(clamp(1.0 - max(view_n.z, 0.0), 0.0, 1.0), mix(1.45, 0.82, layer_t));
    float disk = sqrt(clamp(1.0 - view_n.z * view_n.z, 0.0, 1.0));

    float mask_a = texture(
        u_holo_mask,
        fract(vec2(uv.x * 7.5 + uv.y * 0.18 + pc.tune0.w * 0.17, uv.y * 3.6 + pc.p0.z * 0.004))
    ).r;
    float mask_b = texture(
        u_holo_mask,
        fract(vec2(uv.x * 3.4 - uv.y * 0.46 + 0.31, uv.y * 7.2 + pc.tune0.w * 0.29))
    ).r;
    float noise = texture(
        u_blue_noise,
        fract(vec2(uv.x * 21.0 + pc.tune0.w * 0.13, uv.y * 11.0 + pc.tune0.w * 0.41))
    ).r;

    float lat_wave = 0.5 + 0.5 * cos((uv.y * 2.0 - 1.0) * k_pi * 3.0 + (mask_b - 0.5) * 1.6);
    float lon_wave = 0.5 + 0.5 * cos((uv.x * 6.0 + uv.y * 0.7 + pc.tune0.w * 0.5) * 6.28318530718);
    float panel_field = smoothstep(0.42, 0.90, mix(mask_a, lon_wave, 0.35)) * (0.42 + 0.58 * lat_wave);
    float veil = mix(0.45, 1.0, core) * (0.88 + 0.12 * mask_b);
    float sweep = exp(-pow(wrap_dist(fract(uv.x + uv.y * 0.08), pc.tune1.x) / max(pc.tune1.y, 0.001), 2.0));
    float equator = exp(-pow((uv.y - 0.5) / 0.18, 2.0));
    float polar = 1.0 - smoothstep(0.54, 0.96, abs(local_n.y));

    vec3 body_col = mix(pc.color1.rgb, pc.color0.rgb, 0.38 + 0.34 * lat_wave);
    vec3 rim_col = mix(pc.color0.rgb, pc.color2.rgb, 0.18 + 0.24 * mask_a);
    vec3 sweep_col = mix(pc.color2.rgb, vec3(1.0), 0.16);

    vec3 rgb;
    float alpha;
    if (layer_t > 0.5) {
        float outer = smoothstep(0.16, 0.98, rim);
        float shell_scan = outer * (0.32 + 0.68 * panel_field);
        rgb = rim_col * (0.14 + 0.42 * outer * rim_gain);
        rgb += sweep_col * sweep * outer * 0.18;
        rgb += pc.color0.rgb * shell_scan * 0.06;
        alpha = layer_alpha * front * outer * (0.10 + 0.14 * rim_gain + 0.08 * sweep + 0.05 * shell_scan);
    } else {
        float body = 0.18 + 0.28 * veil + 0.10 * panel_field * panel_gain + 0.12 * equator;
        float rim_fill = rim * rim_gain * (0.16 + 0.10 * panel_field);
        float sector = panel_field * panel_gain * (0.14 + 0.18 * disk);
        rgb = body_col * body;
        rgb += rim_col * rim_fill;
        rgb += sweep_col * sweep * (0.10 + 0.10 * core);
        rgb += pc.color0.rgb * polar * 0.05;
        alpha = layer_alpha * front * (0.08 + 0.14 * veil + sector + rim_fill + 0.06 * sweep);
    }

    float modulation = 0.96 + (mask_a - 0.5) * 0.10 + (noise - 0.5) * 0.05;
    rgb *= modulation * rear_rgb;
    alpha = clamp(alpha * modulation * pc.p0.w * rear_alpha, 0.0, 1.0);
    if (alpha < 0.010) {
        discard;
    }

    out_color = vec4(rgb, alpha);
}
