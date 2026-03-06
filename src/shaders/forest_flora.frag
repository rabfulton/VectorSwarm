#version 450

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 2) uniform sampler2D u_noise;

layout(push_constant) uniform UnderwaterPC {
    vec4 p0; /* x=viewport_w, y=viewport_h, z=time_s, w=flora_density */
    vec4 p1; /* rgb=bark_col, w=canopy_density */
    vec4 p2; /* rgb=glow_col, w=membrane_glow */
    vec4 p3; /* x=world_origin_x, y=world_origin_y, z=wobble_amp, w=wobble_speed */
    vec4 p4; /* x=world_w, y=world_h, z=parallax, w=root_arch_density */
    vec4 p5; /* x=pulse_freq, y=foreground_alpha, z=high_quality */
    vec4 p6;
    vec4 p7;
} pc;

float sat(float x) {
    return clamp(x, 0.0, 1.0);
}

float noise01(vec2 uv) {
    vec4 n = texture(u_noise, uv);
    return dot(n.rgb, vec3(0.52, 0.31, 0.17));
}

float hash11(float p) {
    p = fract(p * 0.1031);
    p *= p + 33.33;
    p *= p + p;
    return fract(p);
}

vec4 composite_over(vec4 dst, vec3 src_col, float src_a) {
    float a = sat(src_a);
    dst.rgb += src_col * a * (1.0 - dst.a);
    dst.a += a * (1.0 - dst.a);
    return dst;
}

vec2 trunk_layer(vec2 uv, float camera_x, float layer_parallax, float density, float wobble_amp, float wobble_speed, float t, float size_scale) {
    if (uv.y > 1.04) {
        return vec2(0.0);
    }
    float lane_count = mix(6.0, 15.0, sat(density * 0.35));
    float x = (uv.x + camera_x * layer_parallax) * lane_count;
    float cell = floor(x);
    float fx = fract(x) - 0.5;
    float alpha = 0.0;
    float glow = 0.0;

    for (int i = -1; i <= 1; ++i) {
        float fi = float(i);
        float id = cell + fi;
        float seed = hash11(id * 17.13 + layer_parallax * 41.7 + 0.3);
        float seed_b = hash11(id * 29.71 + layer_parallax * 13.2 + 7.1);
        float seed_c = hash11(id * 41.17 + layer_parallax * 7.9 + 3.2);
        float size_bias = mix(seed, seed_b, 0.45);
        float sway = sin(t * (0.24 + wobble_speed * 0.26) + id * 0.73 + uv.y * 3.8);
        float center = fi + (seed - 0.5) * 0.72 + sway * wobble_amp * (0.08 + 0.07 * seed_b);
        float width = mix(0.06, 0.30, seed_b) * mix(0.70, 1.38, density * 0.18) * mix(0.78, 1.22, size_bias);
        float height = mix(0.18, 0.88, seed) * mix(0.72, 1.24, density * 0.20) * mix(0.74, 1.26, seed_c);
        width *= size_scale;
        height *= size_scale;
        float y_base = 1.02;
        float y_top = y_base - height;
        float cap_mode = step(0.52, seed_c);
        float cap_y = y_top + height * mix(0.07, 0.18, seed_b);
        float pod_ry = height * mix(0.15, 0.40, seed);
        float umb_ry = height * mix(0.06, 0.18, seed_b);
        float top_extent = min(y_top, min(cap_y + height * 0.05 - pod_ry, cap_y - umb_ry));
        float top_margin = 0.18;
        if (top_extent < top_margin) {
            float push_down = top_margin - top_extent;
            y_top += push_down;
            y_base += push_down;
            cap_y += push_down;
        }
        float shape_y_min = y_top - height * 0.12;
        float shape_y_max = y_base + 0.03;
        float shape_x_max = width * 5.0;

        if (abs(fx - center) > shape_x_max || uv.y < shape_y_min || uv.y > shape_y_max) {
            continue;
        }

        float stem_u = sat((uv.y - y_top) / max(y_base - y_top, 1.0e-4));
        float stem_width = mix(width * 0.18, width * (0.40 + 0.16 * seed_c), smoothstep(0.0, 1.0, stem_u));
        float stem = 1.0 - smoothstep(stem_width * 0.84, stem_width, abs(fx - center));
        float stem_v = smoothstep(y_top - 0.03, y_top + 0.03, uv.y) * (1.0 - smoothstep(y_base - 0.02, y_base + 0.03, uv.y));
        float body = stem * stem_v;
        float base_bulb = 1.0 - smoothstep(0.0, 1.0,
            ((fx - center) * (fx - center)) / max(width * width * 0.20, 1.0e-4) +
            ((uv.y - (y_base - 0.04 - 0.03 * seed_b)) * (uv.y - (y_base - 0.04 - 0.03 * seed_b))) / max(height * height * 0.008, 1.0e-4));
        body = max(body, base_bulb * 0.65);

        float edge_noise = noise01(vec2(id * 0.19 + fx * 2.4, uv.y * 4.8 + seed * 6.1));
        float pod_dx = (fx - center) / max(width * mix(0.95, 2.05, seed_b), 1.0e-4);
        float pod_dy = (uv.y - (cap_y + height * 0.05)) / max(pod_ry, 1.0e-4);
        float pod_r2 = pod_dx * pod_dx + pod_dy * pod_dy * mix(0.8, 1.25, edge_noise);
        float pod = 1.0 - smoothstep(0.74, 1.0, pod_r2);

        float umb_dx = (fx - center) / max(width * mix(2.0, 4.9, seed), 1.0e-4);
        float umb_dy = (uv.y - cap_y) / max(umb_ry, 1.0e-4);
        float umb_r2 = umb_dx * umb_dx + umb_dy * umb_dy * mix(1.6, 2.8, 1.0 - seed_b);
        float umbrella = 1.0 - smoothstep(0.74, 1.0, umb_r2 + (edge_noise - 0.5) * 0.14);
        float underside = smoothstep(cap_y - height * 0.01, cap_y + height * 0.08, uv.y) *
                          (1.0 - smoothstep(cap_y + height * 0.10, cap_y + height * 0.28, uv.y));
        underside *= 1.0 - smoothstep(width * 1.2, width * 3.6, abs(fx - center));
        float ribs = 1.0 - smoothstep(0.0, 0.22, abs(fract((fx - center) / max(width * 0.55, 1.0e-4) + seed * 3.0) - 0.5));
        umbrella = max(umbrella, underside * (0.42 + 0.32 * ribs));

        float tendril = 0.0;
        float tendril_glow = 0.0;

        float cap = mix(pod, umbrella, cap_mode);
        float cap_rim = smoothstep(0.12, 0.74, cap) * (1.0 - smoothstep(0.78, 0.98, cap));

        /* Reuse the cap breakup sample and add a cheap oscillating bias instead of a second noise fetch. */
        float veil_wave = 0.5 + 0.5 * sin(t * (0.34 + 0.18 * seed_b) + id * 0.91 + uv.y * (3.4 + 1.6 * seed));
        float veil_noise = sat(edge_noise * 0.62 + veil_wave * 0.32 + (seed_c - 0.5) * 0.16);
        float veil = smoothstep(0.54, 0.78, veil_noise) * umbrella * smoothstep(cap_y, cap_y + height * 0.20, uv.y);
        veil *= 1.0 - smoothstep(width * 0.4, width * 2.5, abs(fx - center));

        alpha = max(alpha, max(body * 0.86, cap * 0.74 + veil * 0.18 + tendril * 1.00));
        glow = max(glow, max(cap_rim * (0.42 + 0.40 * seed_b) * mix(0.65, 1.0, umbrella), tendril_glow));
    }

    return vec2(sat(alpha), sat(glow));
}

float root_arch_band(vec2 uv, float camera_x, float parallax, float density) {
    if (uv.y < 0.54 || uv.y > 1.04) {
        return 0.0;
    }
    float arc_count = mix(4.0, 9.0, sat(density * 0.30));
    float x = (uv.x + camera_x * parallax) * arc_count;
    float cell = floor(x);
    float fx = fract(x) - 0.5;
    float accum = 0.0;

    for (int i = -1; i <= 1; ++i) {
        float fi = float(i);
        float id = cell + fi;
        float seed = hash11(id * 11.7 + 5.3);
        float center = fi + (seed - 0.5) * 0.34;
        float span = mix(0.42, 0.82, seed);
        float rise = mix(0.08, 0.22, seed);
        float y = 1.03 - uv.y;
        if (abs(fx - center) > span * 1.3 || y < -0.08 || y > rise + 0.24) {
            continue;
        }
        float dx = (fx - center) / span;
        float dy = (y - rise) / max(rise, 1.0e-4);
        float ring = abs(dot(vec2(dx, dy), vec2(dx, dy)) - 1.0);
        float arch = 1.0 - smoothstep(0.04, 0.22, ring);
        arch *= smoothstep(-0.2, 0.3, y);
        arch *= 1.0 - smoothstep(rise + 0.02, rise + 0.18, y);
        accum = max(accum, arch);
    }

    return sat(accum);
}

void main() {
    vec2 frag_px = gl_FragCoord.xy;
    vec2 vp = vec2(max(pc.p0.x, 1.0), max(pc.p0.y, 1.0));
    vec2 uv = frag_px / vp;
    float t = pc.p0.z;

    float flora_density = clamp(pc.p0.w, 0.0, 4.0);
    float wobble_amp = clamp(pc.p3.z, 0.0, 4.0);
    float wobble_speed = clamp(pc.p3.w, 0.0, 6.0);
    float parallax = clamp(pc.p4.z, 0.0, 4.0);
    float root_arch_density = clamp(pc.p4.w, 0.0, 4.0);
    float pulse_freq = clamp(pc.p5.x, 0.0, 6.0);
    float foreground_alpha = clamp(pc.p5.y, 0.0, 1.0);
    float high_quality = step(0.5, pc.p5.z);

    vec2 cam_uv = vec2(pc.p3.x / max(pc.p4.x, 1.0), pc.p3.y / max(pc.p4.y, 1.0));

    vec3 bark_col = pc.p1.rgb;
    vec3 glow_col = pc.p2.rgb;
    float species_a = noise01(vec2((uv.x + cam_uv.x * 0.32) * 2.6, uv.y * 0.55 + 1.4));
    float species_b = noise01(vec2((uv.x + cam_uv.x * 0.28) * 5.2 + 3.7, uv.y * 1.8 - 1.1));
    float species_c_wave = 0.5 + 0.5 * sin((uv.x + cam_uv.x * 0.24) * 7.3 + uv.y * 1.7 + species_a * 4.2 - species_b * 3.1);
    float species_c = sat(mix(species_a, species_b, 0.58) * 0.82 + species_c_wave * 0.18);

    vec3 stem_a = vec3(0.20, 0.32, 0.28);
    vec3 stem_b = vec3(0.28, 0.18, 0.32);
    vec3 stem_c = vec3(0.17, 0.26, 0.36);
    vec3 cap_a = vec3(0.44, 0.68, 0.60);
    vec3 cap_b = vec3(0.70, 0.48, 0.62);
    vec3 cap_c = vec3(0.56, 0.54, 0.80);
    vec3 cap_d = vec3(0.80, 0.70, 0.44);
    vec3 stem_col = mix(stem_a, stem_b, species_a);
    stem_col = mix(stem_col, stem_c, species_b * 0.55);
    vec3 cap_col = mix(cap_a, cap_b, species_a);
    cap_col = mix(cap_col, cap_c, species_b * 0.62);
    cap_col = mix(cap_col, cap_d, species_c * 0.35);
    stem_col *= mix(vec3(0.92, 1.02, 0.96), vec3(1.06, 0.92, 1.02), species_c * 0.75);
    cap_col *= mix(vec3(0.94, 1.04, 0.94), vec3(1.08, 0.92, 1.08), species_b * 0.72);

    vec2 far = trunk_layer(uv, cam_uv.x, 0.12 + 0.08 * parallax, flora_density * 0.72, wobble_amp * 0.28, wobble_speed * 0.65, t, 0.94);
    vec2 mid = trunk_layer(uv, cam_uv.x, 0.28 + 0.14 * parallax, flora_density, wobble_amp * 0.46, wobble_speed * 0.88, t + 3.7, 0.88);
    vec2 near = vec2(0.0);
    if (high_quality > 0.5) {
        near = trunk_layer(uv, cam_uv.x, 0.56 + 0.18 * parallax, flora_density * 1.12, wobble_amp * 0.70, wobble_speed * 1.08, t + 7.9, 0.72);
    }
    float roots = root_arch_band(uv, cam_uv.x, 0.36 + 0.18 * parallax, root_arch_density);
    float pulse = 0.55 + 0.45 * sin(t * (0.35 + pulse_freq * 0.55) + uv.x * 9.0);

    float far_cover = sat(smoothstep(0.10, 0.48, far.x));
    float mid_cover = sat(smoothstep(0.12, 0.54, mid.x));
    float near_cover = sat(smoothstep(0.14, 0.58, near.x));
    float root_cover = sat(smoothstep(0.18, 0.52, roots));

    float far_a = far.x * 0.46 * (1.0 - root_cover) * (1.0 - mid_cover) * (1.0 - near_cover);
    float mid_a = mid.x * 0.70 * (1.0 - near_cover);
    float root_a = roots * (0.34 + 0.12 * foreground_alpha) * (1.0 - mid_cover) * (1.0 - near_cover);
    float near_a = near.x * 0.98;

    float far_glow = far.y * pc.p2.w * (0.22 + 0.10 * pulse);
    float mid_glow = mid.y * pc.p2.w * (0.46 + 0.18 * pulse);
    float near_glow = near.y * pc.p2.w * (0.34 + 0.12 * pulse);

    vec3 far_col = mix(bark_col * 0.24, stem_col * 0.48, 0.64) * (0.18 + 0.10 * far.x);
    vec3 root_col = bark_col * 0.26 + stem_col * 0.06;
    vec3 mid_col = stem_col * 0.56 + cap_col * 0.08;
    vec3 near_col = stem_col * 0.10 + cap_col * 0.98 + glow_col * 0.02;

    vec4 layers = vec4(0.0);
    layers = composite_over(layers, far_col, far_a);
    layers = composite_over(layers, root_col, root_a);
    layers = composite_over(layers, mid_col, mid_a);
    if (high_quality > 0.5) {
        layers = composite_over(layers, near_col, near_a);
    }

    float alpha = sat(layers.a);
    vec3 col = layers.rgb;
    col += stem_col * far_glow * 0.16;
    col += cap_col * mid_glow * 0.30;
    col += cap_col * near_glow * 0.18;
    col += glow_col * (mid_glow * 0.03 + near_glow * 0.02);
    float surface_breakup = mix(species_a, species_b, 0.44);
    col *= 0.85 + 0.09 * surface_breakup;

    out_color = vec4(col, alpha);
}
