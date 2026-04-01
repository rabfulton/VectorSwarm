#version 450

layout(set = 0, binding = 0) uniform sampler2D u_blue_noise;
layout(set = 0, binding = 2) uniform sampler2D u_band_warp;
layout(set = 0, binding = 3) uniform sampler2D u_aurora_mask;
layout(set = 0, binding = 4) uniform sampler2D u_storm_shape;
layout(set = 0, binding = 5) uniform sampler3D u_curl_volume;

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
    vec4 tune0;
    vec4 tune1;
} pc;

const float k_pi = 3.14159265359;

vec2 sphere_uv(vec3 n) {
    float lon = atan(n.z, n.x) / (2.0 * k_pi) + 0.5;
    return vec2(fract(lon), clamp(n.y * 0.5 + 0.5, 0.0, 1.0));
}

float front_mask(float z) {
    float w = max(fwidth(z) * 2.8, 0.016);
    return smoothstep(-w, w * 3.2, z);
}

float rim_mask(float z) {
    float w = max(fwidth(z) * 4.0, 0.020);
    return 1.0 - smoothstep(w * 1.6, 0.24 + w * 9.0, max(z, 0.0));
}

void main() {
    vec3 local_n = normalize(v_local);
    vec3 view_n = normalize(v_view);
    float front = front_mask(view_n.z);
    if (front <= 0.001) {
        discard;
    }

    vec2 base_uv = sphere_uv(local_n);
    float azimuth = length(local_n.xz);
    float azimuth_t = smoothstep(0.18, 0.52, azimuth);
    float rim = rim_mask(view_n.z);
    float rim_core = pow(rim, 2.4);
    vec4 curl_tex = texture(
        u_curl_volume,
        fract(local_n * 0.5 + 0.5 + vec3(pc.p0.z * 0.0016, pc.p0.z * 0.0011, pc.tune0.w * 0.17))
    );
    vec3 curl = curl_tex.rgb * 2.0 - 1.0;
    float layer_t = clamp(pc.tune0.y, 0.0, 1.0);
    float layer_alpha = max(pc.tune0.z, 0.0);

    vec2 belt_uv = vec2(
        fract(base_uv.x + pc.p0.z * 0.0008 + curl.x * 0.020 * pc.tune1.x * azimuth_t + layer_t * 0.04),
        clamp(base_uv.y + curl.y * 0.026 * pc.tune1.x * azimuth_t, 0.0, 1.0)
    );
    belt_uv.x = mix(0.5 + curl.x * 0.008, belt_uv.x, azimuth_t);
    vec3 band_tex = texture(u_band_warp, belt_uv).rgb;

    vec2 storm_uv = vec2(
        fract(base_uv.x + pc.p0.z * 0.0006 + curl.x * 0.010 * azimuth_t + pc.tune0.w * 0.06),
        clamp(base_uv.y * 0.95 + curl.y * 0.012 * azimuth_t, 0.0, 1.0)
    );
    storm_uv.x = mix(0.5 + curl.x * 0.005, storm_uv.x, azimuth_t);
    vec3 storm_tex = texture(u_storm_shape, storm_uv).rgb;
    float storm_band = (1.0 - smoothstep(0.64, 0.88, abs(local_n.y))) * azimuth_t;
    float storm = storm_tex.r * storm_band * (0.42 + 0.58 * storm_tex.g);

    vec2 aurora_uv = vec2(
        fract(base_uv.x + pc.p0.z * 0.0018 + curl.x * 0.016 * azimuth_t + pc.tune0.w * 0.04),
        clamp((abs(local_n.y) - 0.60) / 0.36 + curl.z * 0.012 * azimuth_t, 0.0, 1.0)
    );
    aurora_uv.x = mix(0.5 + curl.x * 0.010, aurora_uv.x, azimuth_t);
    vec3 aurora_tex = texture(u_aurora_mask, aurora_uv).rgb;
    float pole = smoothstep(0.66, 0.94, abs(local_n.y));
    float aurora = pole * aurora_tex.r * (0.24 + 0.28 * aurora_tex.g) * pc.tune1.z;
    aurora *= 0.22 + 0.10 * azimuth_t;
    vec3 aurora_col = mix(pc.color0.rgb, pc.color2.rgb, 0.04 + 0.04 * aurora_tex.b);

    float fresnel = pow(clamp(1.0 - max(view_n.z, 0.0), 0.0, 1.0), mix(3.3, 2.5, layer_t));
    float bright_storm = storm * (0.24 + 0.76 * storm_tex.b);
    vec3 rgb = pc.color0.rgb * rim_core * (0.09 + 0.03 * pc.tune1.w);
    rgb += aurora_col * aurora * (0.10 + 0.06 * layer_t);
    rgb += pc.color0.rgb * fresnel * 0.02 * pc.tune1.w;
    rgb += mix(pc.color1.rgb, pc.color0.rgb, 0.26) * bright_storm * 0.02;
    rgb *= pc.p0.w * (0.94 + 0.06 * band_tex.b);

    float alpha = layer_alpha * front * (0.014 * rim_core * pc.tune1.w + 0.006 * aurora + 0.004 * bright_storm);
    alpha = clamp(alpha, 0.0, 1.0);
    if (max(max(rgb.r, rgb.g), rgb.b) < 0.003 && alpha < 0.003) {
        discard;
    }

    out_color = vec4(rgb, alpha);
}
