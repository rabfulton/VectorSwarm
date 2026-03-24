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

float lon_wrap_dist(float a, float b) {
    float d = abs(a - b);
    return min(d, 1.0 - d);
}

float front_mask(float z) {
    float w = max(fwidth(z) * 2.4, 0.014);
    return smoothstep(-w, w * 3.0, z);
}

float rim_mask(float z) {
    float w = max(fwidth(z) * 3.4, 0.018);
    return 1.0 - smoothstep(w * 1.4, 0.18 + w * 8.2, max(z, 0.0));
}

void sample_band_profile(float lat, vec3 control, out float band_mix, out float boundary) {
    const int k_count = 7;
    const float centers[k_count] = float[k_count](-0.82, -0.56, -0.18, 0.07, 0.39, 0.68, 0.88);
    const float widths[k_count] = float[k_count](0.10, 0.19, 0.09, 0.22, 0.13, 0.17, 0.08);
    const float tones[k_count] = float[k_count](0.16, 0.88, 0.24, 0.72, 0.14, 0.60, 0.34);
    float tone_sum = 0.0;
    float weight_sum = 0.0;
    float edge = 0.0;
    for (int i = 0; i < k_count; ++i) {
        float center = centers[i] + (control.r - 0.5) * (0.022 + 0.004 * float(i));
        float width = widths[i] * (0.88 + 0.18 * control.g + 0.06 * sin(float(i) * 1.7 + control.b * 6.0));
        float dist = lat - center;
        float g = exp(-pow(dist / max(width, 0.001), 2.0));
        float edge_shape = exp(-pow((abs(dist) - width * 0.78) / max(width * 0.30, 0.001), 2.0));
        tone_sum += g * tones[i];
        weight_sum += g;
        edge = max(edge, edge_shape);
    }
    band_mix = clamp(tone_sum / max(weight_sum, 0.001), 0.0, 1.0);
    boundary = clamp(edge * (0.74 + 0.26 * control.g), 0.0, 1.0);
}

float junction_vortex_field(float lon, float lat, vec3 flow, float time_s) {
    const int k_count = 10;
    const vec4 cells[k_count] = vec4[k_count](
        vec4(0.08, -0.64, 0.10, 0.07),
        vec4(0.20, -0.31, 0.12, 0.08),
        vec4(0.34, -0.02, 0.14, 0.09),
        vec4(0.48,  0.26, 0.11, 0.08),
        vec4(0.64,  0.56, 0.13, 0.08),
        vec4(0.76, -0.60, 0.10, 0.07),
        vec4(0.88, -0.28, 0.11, 0.07),
        vec4(0.96,  0.04, 0.12, 0.09),
        vec4(0.58, -0.04, 0.10, 0.07),
        vec4(0.42,  0.58, 0.11, 0.08)
    );
    float accum = 0.0;
    for (int i = 0; i < k_count; ++i) {
        vec4 c = cells[i];
        float cx = fract(c.x + time_s * (0.00014 + 0.00004 * float(i)) + flow.x * 0.005);
        float dx = lon_wrap_dist(lon, cx);
        float dy = lat - c.y - flow.y * 0.006;
        float ex = dx / max(c.z, 0.001);
        float ey = dy / max(c.w, 0.001);
        float r = sqrt(ex * ex + ey * ey);
        float ang = atan(dy / max(c.w, 0.001), dx / max(c.z, 0.001));
        float oval = exp(-r * r * 2.6);
        float swirl = 0.5 + 0.5 * cos(ang * 2.8 - r * 5.0 + time_s * (0.05 + 0.01 * float(i)) + flow.z * 4.0);
        accum = max(accum, oval * (0.42 + 0.58 * swirl));
    }
    return accum;
}

float storm_field(float lon, float lat, vec3 tex_mod, float time_s) {
    const int k_count = 8;
    const vec4 storms[k_count] = vec4[k_count](
        vec4(0.05, -0.52, 0.13, 0.08),
        vec4(0.17, -0.27, 0.11, 0.07),
        vec4(0.30, -0.02, 0.14, 0.09),
        vec4(0.44,  0.24, 0.12, 0.08),
        vec4(0.58,  0.48, 0.14, 0.09),
        vec4(0.70, -0.49, 0.12, 0.08),
        vec4(0.83, -0.01, 0.11, 0.07),
        vec4(0.93,  0.26, 0.12, 0.08)
    );
    float accum = 0.0;
    for (int i = 0; i < k_count; ++i) {
        vec4 s = storms[i];
        float cx = fract(s.x + time_s * (0.00022 + 0.00006 * float(i)) + tex_mod.r * 0.006);
        float dx = lon_wrap_dist(lon, cx);
        float dy = lat - s.y - tex_mod.g * 0.008;
        float ex = dx / max(s.z, 0.001);
        float ey = dy / max(s.w, 0.001);
        float r = sqrt(ex * ex + ey * ey);
        float oval = exp(-r * r * 2.0);
        float ring = exp(-pow(r - 0.42, 2.0) * 18.0);
        float ang = atan(dy / max(s.w, 0.001), dx / max(s.z, 0.001));
        float spiral = 0.5 + 0.5 * sin(ang * 4.0 - r * 6.6 + time_s * (0.08 + 0.02 * float(i)) + tex_mod.b * 4.0);
        accum = max(accum, oval * (0.52 + 0.34 * spiral) + ring * 0.16);
    }
    return accum;
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
    float layer_t = clamp(pc.tune0.y, 0.0, 1.0);
    float layer_alpha = max(pc.tune0.z, 0.0);
    float fresnel = pow(clamp(1.0 - max(view_n.z, 0.0), 0.0, 1.0), mix(4.8, 3.4, layer_t));

    if (layer_t > 0.5) {
        float atmo_visible = smoothstep(0.10, 0.84, rim);
        if (atmo_visible <= 0.001) {
            discard;
        }

        vec3 haze_tex = texture(
            u_band_warp,
            vec2(
                fract(base_uv.x * 0.74 + pc.p0.z * 0.00012 + pc.tune0.w * 0.09),
                clamp(base_uv.y * 0.70 + 0.15 + local_n.y * 0.02, 0.0, 1.0)
            )
        ).rgb;
        float lat_soft = 1.0 - smoothstep(0.72, 0.98, abs(local_n.y));
        float high_haze = (0.34 + 0.66 * haze_tex.g) * (0.82 + 0.18 * haze_tex.r) * (0.74 + 0.26 * lat_soft);
        float atmo_rim = pow(rim, 0.92) * (0.62 + 0.38 * high_haze);
        float atmo_band = smoothstep(0.24, 0.90, rim) * (0.24 + 0.76 * high_haze) * lat_soft;
        float atmo_veil = high_haze * (0.040 + 0.075 * rim + 0.022 * fresnel) * (0.58 + 0.42 * lat_soft);
        vec3 haze_col = mix(pc.color0.rgb, vec3(0.98, 0.98, 1.00), 0.24 + 0.08 * haze_tex.b);

        vec3 rgb = haze_col * (
            atmo_veil * (0.34 + 0.18 * pc.tune1.w) +
            atmo_band * (0.10 + 0.10 * pc.tune1.w) +
            atmo_rim * (0.48 + 0.30 * pc.tune1.w)
        );
        float dither = texture(
            u_blue_noise,
            fract(base_uv * vec2(23.0, 11.0) + vec2(pc.tune0.w * 0.37, pc.tune0.y * 0.29))
        ).r;
        float alpha = layer_alpha * front * atmo_visible *
            (0.11 * atmo_veil + 0.11 * atmo_band + 0.24 * atmo_rim * pc.tune1.w);
        alpha *= mix(0.997, 1.003, dither);
        alpha = clamp(alpha, 0.0, 1.0);
        if (alpha < 0.010) {
            discard;
        }

        out_color = vec4(rgb, alpha);
        return;
    }

    vec4 curl_lo_s = texture(
        u_curl_volume,
        fract(local_n * 0.28 + 0.5 + vec3(pc.p0.z * 0.00045, pc.p0.z * 0.00035, pc.tune0.w * 0.07))
    );
    vec3 curl_lo = curl_lo_s.rgb * 2.0 - 1.0;
    vec2 advect_lo = vec2(curl_lo.x, curl_lo.y) * (0.018 + 0.008 * pc.tune1.y) * pc.tune1.x * azimuth_t;

    vec4 curl_mid_s = texture(
        u_curl_volume,
        fract(local_n * 0.76 + 0.5 + vec3(advect_lo, curl_lo.z) * 0.34 + vec3(pc.p0.z * 0.0008, -pc.p0.z * 0.0006, pc.tune0.w * 0.11))
    );
    vec3 curl_mid = curl_mid_s.rgb * 2.0 - 1.0;
    vec2 advect_mid = advect_lo + vec2(curl_mid.x, curl_mid.y) * 0.010 * pc.tune1.x * azimuth_t;

    vec4 curl_hi_s = texture(
        u_curl_volume,
        fract(local_n * 1.54 + 0.5 + vec3(advect_mid, curl_mid.z) * 0.48 + vec3(-pc.p0.z * 0.0015, pc.p0.z * 0.0011, pc.tune0.w * 0.15))
    );
    vec3 curl_hi = curl_hi_s.rgb * 2.0 - 1.0;
    float density = clamp(curl_lo_s.a * 0.40 + curl_mid_s.a * 0.35 + curl_hi_s.a * 0.25, 0.0, 1.0);
    float atmo_shell = smoothstep(0.55, 1.0, layer_t);
    float body_shell = 1.0 - atmo_shell;

    float lon = fract(base_uv.x + advect_mid.x + pc.p0.z * 0.00025);
    float lat = clamp(base_uv.y * 2.0 - 1.0 + advect_mid.y * (1.05 + 0.12 * pc.tune1.y), -1.0, 1.0);

    vec3 control_band = texture(
        u_band_warp,
        vec2(
            fract(lon * 0.34 + base_uv.y * 0.08 + pc.p0.z * 0.00014),
            clamp(base_uv.y * 0.68 + 0.16, 0.0, 1.0)
        )
    ).rgb;
    float band_mix;
    float boundary;
    sample_band_profile(lat + (control_band.r - 0.5) * 0.040, control_band, band_mix, boundary);

    vec3 band_hi = texture(
        u_band_warp,
        vec2(
            fract(lon * 2.8 + curl_hi.x * 0.020 * boundary + pc.p0.z * 0.0008),
            clamp(base_uv.y * 1.8 + curl_hi.y * 0.016 * boundary, 0.0, 1.0)
        )
    ).rgb;
    float detail_mask = boundary * azimuth_t *
        junction_vortex_field(lon, lat, vec3(curl_mid.x, curl_hi.y, band_hi.b), pc.p0.z) *
        (0.38 + 0.62 * band_hi.g);
    float shear = 0.5 + 0.5 * sin(
        (lon + advect_mid.x * 0.5) * 6.28318530718 * 1.8 +
        lat * 6.28318530718 * 0.9 +
        control_band.b * 3.4
    );

    vec3 deep_band = mix(
        pc.color1.rgb * 0.56,
        pc.color1.rgb * 0.42 + pc.color0.rgb * 0.08,
        0.28 + 0.22 * control_band.b
    );
    vec3 mid_band = mix(
        pc.color1.rgb * 0.74 + pc.color0.rgb * 0.14,
        pc.color0.rgb * 0.46,
        0.34 + 0.22 * shear
    );
    vec3 pale_band = mix(
        pc.color0.rgb * 0.88,
        mix(pc.color0.rgb, pc.color2.rgb, 0.26),
        0.48 + 0.18 * control_band.g
    );
    vec3 bright_band = mix(pc.color0.rgb, pc.color2.rgb, 0.56);
    vec3 base = mix(deep_band, pale_band, band_mix);
    base = mix(base, mid_band, boundary * 0.18);
    base = mix(base, bright_band, detail_mask * 0.16);
    base += bright_band * detail_mask * 0.12;

    vec3 storm_tex = texture(
        u_storm_shape,
        vec2(
            fract(lon * 1.4 + curl_mid.x * 0.018 * azimuth_t + pc.p0.z * 0.0004 + pc.tune0.w * 0.04),
            clamp(base_uv.y * 1.2 + curl_mid.y * 0.008 * azimuth_t, 0.0, 1.0)
        )
    ).rgb;
    float storm = storm_field(lon, lat, vec3(curl_lo.x, curl_mid.y, storm_tex.b), pc.p0.z);
    storm *= (0.74 + 0.26 * storm_tex.r) * (0.34 + 0.66 * boundary) * (0.60 + 0.40 * azimuth_t);
    vec3 storm_col = mix(
        pc.color0.rgb * 1.02,
        pc.color2.rgb,
        0.50 + 0.24 * storm_tex.g
    );
    base = mix(base, storm_col, storm * (0.30 + 0.18 * boundary));
    base += storm_col * storm * 0.18;

    vec3 haze_tex = texture(
        u_band_warp,
        vec2(
            fract(lon * 0.72 + curl_lo.x * 0.015 * azimuth_t + pc.p0.z * 0.00012 + pc.tune0.w * 0.09),
            clamp(base_uv.y * 0.70 + 0.15 + curl_lo.z * 0.012 * azimuth_t, 0.0, 1.0)
        )
    ).rgb;
    float upper_cloud = smoothstep(0.44, 0.86, band_mix) * (0.40 + 0.60 * haze_tex.g) * (0.68 + 0.32 * azimuth_t);
    float high_haze = (0.26 + 0.74 * upper_cloud) * (0.78 + 0.22 * haze_tex.r);
    float atmo_rim = pow(rim, 1.08) * (0.46 + 0.54 * high_haze);
    float atmo_band = smoothstep(0.20, 0.92, rim) * (0.22 + 0.78 * high_haze);
    float atmo_veil = high_haze * (0.050 + 0.080 * rim + 0.018 * fresnel) * (0.70 + 0.30 * boundary);
    vec3 haze_col = mix(pc.color0.rgb, vec3(0.98, 0.98, 1.00), 0.24 + 0.08 * haze_tex.b);

    vec3 aurora_tex = texture(
        u_aurora_mask,
        vec2(
            fract(base_uv.x + pc.p0.z * 0.0014 + curl_mid.x * 0.014 * azimuth_t + pc.tune0.w * 0.05),
            clamp((abs(local_n.y) - 0.60) / 0.36 + curl_mid.z * 0.010 * azimuth_t, 0.0, 1.0)
        )
    ).rgb;
    float pole = smoothstep(0.66, 0.94, abs(local_n.y));
    float aurora = pole * aurora_tex.r * (0.24 + 0.26 * aurora_tex.g) * pc.tune1.z;
    aurora *= 0.18 + 0.10 * azimuth_t;
    vec3 aurora_col = mix(pc.color0.rgb, pc.color2.rgb, 0.04 + 0.04 * aurora_tex.b);

    float body_weight = mix(1.0, 0.05, atmo_shell);
    float haze_weight = mix(1.0, 1.65, atmo_shell);
    vec3 rgb = base * (pc.p0.w * (0.98 + (density - 0.5) * 0.10)) * body_weight;
    rgb += base * detail_mask * 0.12 * body_shell;
    rgb += aurora_col * aurora * 0.10 * body_weight;
    rgb += pc.color0.rgb * fresnel * 0.010 * pc.tune1.w * body_weight;
    rgb += mix(pc.color0.rgb, pc.color2.rgb, 0.10) * rim * 0.0018 * pc.tune1.w * body_weight;
    rgb += haze_col * atmo_veil * (0.34 + 0.22 * pc.tune1.w) * haze_weight;
    rgb += haze_col * atmo_band * (0.10 + 0.10 * pc.tune1.w) * haze_weight;
    rgb += haze_col * atmo_rim * (0.42 + 0.28 * pc.tune1.w) * haze_weight;

    float dither = texture(
        u_blue_noise,
        fract(base_uv * vec2(23.0, 11.0) + vec2(pc.tune0.w * 0.37, pc.tune0.y * 0.29))
    ).r;
    float alpha = layer_alpha * front *
        (body_shell * (0.34 + 0.08 * boundary + 0.10 * detail_mask + 0.08 * storm + 0.03 * aurora + 0.008 * fresnel * pc.tune1.w) +
         haze_weight * (0.12 * atmo_veil + 0.09 * atmo_band + 0.20 * atmo_rim * pc.tune1.w));
    alpha *= 0.92 + 0.08 * density;
    alpha *= mix(0.995, 1.005, dither);
    alpha = clamp(alpha, 0.0, 1.0);
    if (alpha < 0.010) {
        discard;
    }

    out_color = vec4(rgb, alpha);
}
